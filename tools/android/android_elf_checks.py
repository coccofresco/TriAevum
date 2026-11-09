#!/usr/bin/env python3
"""Release checks of the staged Android libraries (no NDK tool needed).

- 16 KiB pages: every LOAD segment of every library is aligned to 16 KiB
  (Android 15+ devices with 16 KiB pages; tools/triaevum_release/
  android_release_policy.py elf_errors, also run by the APK audit).
- One NDK: every library that carries the NDK's `.note.android.ident`
  names the same NDK version and build (a library linked by another NDK can
  disagree with libc++_shared.so about the C++ ABI).
- libc++ closure: every libc++ symbol (`std::__ndk1`) a library imports is
  exported by the staged libc++_shared.so.
"""
from __future__ import annotations

from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "triaevum_release"))
from android_release_policy import elf_errors  # noqa: E402

LIBCXX = "libc++_shared.so"
LIBCXX_NAMESPACE = b"_ZNSt6__ndk1"
NOTE_SECTION = b".note.android.ident"


def _sections(data: bytes) -> dict[bytes, tuple[int, int, int, int]]:
    """name -> (type, offset, size, link) of an ELF64 little-endian file."""
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
    if not shnum or shstrndx >= shnum or shoff + shnum * shentsize > len(data):
        return {}
    headers = []
    for index in range(shnum):
        base = shoff + index * shentsize
        name, kind = struct.unpack_from("<II", data, base)
        offset, size = struct.unpack_from("<QQ", data, base + 24)
        link = struct.unpack_from("<I", data, base + 40)[0]
        headers.append((name, kind, offset, size, link))
    strings_offset, strings_size = headers[shstrndx][2], headers[shstrndx][3]
    strings = data[strings_offset:strings_offset + strings_size]
    result = {}
    for index, (name, kind, offset, size, link) in enumerate(headers):
        end = strings.find(b"\0", name)
        result[strings[name:end if end >= 0 else len(strings)]] = (kind, offset, size, headers[link][2] if link < shnum else 0,
                                                                   headers[link][3] if link < shnum else 0)
    return result


def android_ident(data: bytes) -> tuple[int, str, str] | None:
    """(API level, NDK version, NDK build) from `.note.android.ident`."""
    section = _sections(data).get(NOTE_SECTION)
    if section is None:
        return None
    _, offset, size, *_ = section
    at, end = offset, offset + size
    while at + 12 <= end:
        namesz, descsz, kind = struct.unpack_from("<III", data, at)
        name = data[at + 12:at + 12 + namesz].rstrip(b"\0")
        desc_at = at + 12 + ((namesz + 3) & ~3)
        desc = data[desc_at:desc_at + descsz]
        if name == b"Android" and kind == 1 and len(desc) >= 4:
            api = struct.unpack_from("<I", desc)[0]
            version = desc[4:68].split(b"\0", 1)[0].decode("ascii", "replace") if len(desc) >= 68 else ""
            build = desc[68:132].split(b"\0", 1)[0].decode("ascii", "replace") if len(desc) >= 132 else ""
            return api, version, build
        at = desc_at + ((descsz + 3) & ~3)
    return None


def dynamic_symbols(data: bytes) -> tuple[set[str], set[str]]:
    """(exported, imported) names of `.dynsym`."""
    section = _sections(data).get(b".dynsym")
    if section is None:
        return set(), set()
    _, offset, size, strings_offset, strings_size = section
    strings = data[strings_offset:strings_offset + strings_size]
    exported, imported = set(), set()
    for at in range(offset + 24, offset + size, 24):  # entry 0 is the null symbol
        name, info, _, shndx = struct.unpack_from("<IBBH", data, at)
        end = strings.find(b"\0", name)
        symbol = strings[name:end if end >= 0 else len(strings)].decode("ascii", "replace")
        if not symbol or info >> 4 == 0:  # STB_LOCAL
            continue
        (imported if shndx == 0 else exported).add(symbol)
    return exported, imported


def stage_errors(libraries: dict[str, bytes]) -> list[str]:
    """Every release check over the staged libraries {file name: bytes}."""
    errors = []
    for name, data in sorted(libraries.items()):
        errors.extend(elf_errors(name, data))
    idents = {name: android_ident(data) for name, data in libraries.items()}
    ndks = {(ident[1], ident[2]) for ident in idents.values() if ident and ident[1]}
    if len(ndks) > 1:
        errors.append("libraries were linked by different NDKs: " + ", ".join(
            f"{name}={ident[1]} {ident[2]}" for name, ident in sorted(idents.items()) if ident))
    elif not ndks:
        errors.append("no library names its NDK (.note.android.ident)")
    if LIBCXX not in libraries:
        errors.append(f"{LIBCXX} is not staged")
        return errors
    provided, _ = dynamic_symbols(libraries[LIBCXX])
    for name, data in sorted(libraries.items()):
        if name == LIBCXX:
            continue
        exported, imported = dynamic_symbols(data)
        missing = sorted(symbol for symbol in imported
                         if symbol.startswith(LIBCXX_NAMESPACE.decode()) and symbol not in provided)
        if missing:
            errors.append(f"{name}: {len(missing)} libc++ symbols not in the staged {LIBCXX} ({missing[0]} ...)")
    return errors


def check_stage(directory: Path) -> list[str]:
    libraries = {path.name: path.read_bytes() for path in sorted(directory.glob("*.so"))}
    return stage_errors(libraries)


if __name__ == "__main__":
    problems = check_stage(Path(sys.argv[1]))
    print("\n".join(problems) or "ok")
    sys.exit(1 if problems else 0)
