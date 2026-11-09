#!/usr/bin/env python3
"""Tests of android_elf_checks.py on synthetic ELF files (no NDK needed)."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from android_elf_checks import (LIBCXX, android_ident, check_stage, dynamic_symbols,  # noqa: E402
                                stage_errors)


def note(api=35, ndk="r30", build="16248370"):
    desc = struct.pack("<I", api) + ndk.encode().ljust(64, b"\0") + build.encode().ljust(64, b"\0")
    return struct.pack("<III", 8, len(desc), 1) + b"Android\0" + desc


def elf(*, imports=(), exports=(), ndk="r30", align=0x4000, with_note=True):
    """An AArch64 shared object with one LOAD, an NDK note and a .dynsym."""
    names = [""] + list(imports) + list(exports)
    dynstr = b"\0"
    offsets = [0]
    for name in names[1:]:
        offsets.append(len(dynstr))
        dynstr += name.encode() + b"\0"
    dynsym = b"\0" * 24
    for index, name in enumerate(names[1:], 1):
        shndx = 0 if name in imports else 7
        dynsym += struct.pack("<IBBHQQ", offsets[index], (1 << 4) | 2, 0, shndx, 0, 0)
    sections = [(".note.android.ident", 7, note(ndk=ndk) if with_note else b""), (".dynstr", 3, dynstr),
                (".dynsym", 11, dynsym)]
    shstrtab = b"\0"
    name_offsets = {}
    for name in [".shstrtab"] + [section[0] for section in sections]:
        name_offsets[name] = len(shstrtab)
        shstrtab += name.encode() + b"\0"
    body = bytearray(64 + 56)
    placed = []
    for name, kind, data in [(".shstrtab", 3, shstrtab)] + sections:
        placed.append((name, kind, len(body), len(data)))
        body += data
        body += b"\0" * (-len(body) % 8)
    shoff = len(body)
    headers = b"\0" * 64
    index_of = {name: index + 1 for index, (name, *_rest) in enumerate(placed)}
    for name, kind, offset, size in placed:
        link = index_of[".dynstr"] if name == ".dynsym" else 0
        headers += struct.pack("<IIQQQQIIQQ", name_offsets[name], kind, 0, 0, offset, size, link, 0, 1,
                               24 if name == ".dynsym" else 0)
    body += headers
    struct.pack_into("<4sBBB", body, 0, b"\x7fELF", 2, 1, 1)
    struct.pack_into("<HHI", body, 16, 3, 183, 1)
    struct.pack_into("<QQQ", body, 24, 0, 64, shoff)
    struct.pack_into("<IHHHHHH", body, 48, 0, 64, 56, 1, 64, len(placed) + 1, 1)
    struct.pack_into("<IIQQQQQQ", body, 64, 1, 5, 0, 0, 0, len(body), len(body), align)
    return bytes(body)


SYMBOL = "_ZNSt6__ndk112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6appendEPKc"


def good():
    return {LIBCXX: elf(exports=[SYMBOL]), "libTriAevum.so": elf(imports=[SYMBOL, "malloc"]),
            "libtriaevum_forge.so": elf(with_note=False)}


class ElfChecksTests(unittest.TestCase):
    def test_parsers(self):
        data = elf(imports=[SYMBOL], exports=["triaevum_entry"])
        self.assertEqual(android_ident(data), (35, "r30", "16248370"))
        self.assertEqual(dynamic_symbols(data), ({"triaevum_entry"}, {SYMBOL}))

    def test_consistent_stage_passes(self):
        self.assertEqual(stage_errors(good()), [])
        with tempfile.TemporaryDirectory() as directory:
            for name, data in good().items():
                (Path(directory) / name).write_bytes(data)
            self.assertEqual(check_stage(Path(directory)), [])

    def test_negative_4k_pages(self):
        libraries = dict(good(), **{"libTriAevum.so": elf(imports=[SYMBOL], align=0x1000)})
        self.assertIn("libTriAevum.so: LOAD segment aligned to 0x1000, needs 0x4000", stage_errors(libraries))

    def test_negative_two_ndks(self):
        libraries = dict(good(), **{"libtriaevum_title_aot.so": elf(ndk="r27")})
        self.assertTrue(any("different NDKs" in error for error in stage_errors(libraries)))

    def test_negative_libcxx_closure(self):
        libraries = dict(good(), **{LIBCXX: elf(exports=["_ZNSt6__ndk1other"])})
        self.assertTrue(any("libc++ symbols not in the staged" in error for error in stage_errors(libraries)))
        missing = good()
        del missing[LIBCXX]
        self.assertIn(f"{LIBCXX} is not staged", stage_errors(missing))


if __name__ == "__main__":
    unittest.main()
