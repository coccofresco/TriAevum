import hashlib
import io
import struct
import tempfile
import unittest
from pathlib import Path

from vertex_family import extract, load_facts, romfs_files, summary

NONE = 0xFFFFFFFF


def name16(name):
    data = name.encode("utf-16le")
    return data + b"\0" * (-len(data) % 4)


def build_romfs(files, directory="shaders"):
    """A minimal IVFC image: root -> /directory -> files (level 3 at 0x1000)."""
    root = struct.pack("<6I", 0, NONE, 0x18, NONE, NONE, 0)
    sub_name = name16(directory)
    sub = struct.pack("<6I", 0, NONE, NONE, 0 if files else NONE, NONE, len(directory) * 2) + sub_name
    dirs = root + sub
    meta, data = b"", b""
    for index, (name, payload) in enumerate(files):
        entry_name = name16(name)
        size = 0x20 + len(entry_name)
        sibling = len(meta) + size if index + 1 < len(files) else NONE
        meta += struct.pack("<IIQQII", 0x18, sibling, len(data), len(payload), NONE, len(name) * 2) + entry_name
        data += payload + b"\0" * (-len(payload) % 16)
    header_len = 0x28
    dir_off = header_len
    file_off = dir_off + len(dirs)
    data_off = file_off + len(meta)
    level3 = struct.pack("<10I", header_len, 0, 0, dir_off, len(dirs), 0, 0, file_off, len(meta), data_off)
    ivfc = bytearray(0x60)
    ivfc[:4] = b"IVFC"
    struct.pack_into("<I", ivfc, 0x08, 0x20)
    struct.pack_into("<I", ivfc, 0x4C, 12)
    return bytes(ivfc) + b"\0" * (0x1000 - 0x60) + level3 + dirs + meta + data


class VertexFamilyTests(unittest.TestCase):
    def facts(self, files):
        return {"romfs_directory": "shaders",
                "shbin": {name: hashlib.sha256(data).hexdigest() for name, data in files}}

    def test_real_facts_guard_five_programs(self):
        facts = load_facts()
        self.assertEqual(len(facts["shbin"]), 5)
        self.assertEqual(facts["expected"]["untranslated_geometry_dvle"], 1)

    def test_extracts_guarded_files(self):
        files = [("A.shbin", b"DVLB-a"), ("B.shbin", b"DVLB-bb"), ("other.bin", b"x")]
        image = build_romfs(files)
        self.assertEqual(list(romfs_files(io.BytesIO(image), "shaders")), files)
        with tempfile.TemporaryDirectory() as out, tempfile.TemporaryDirectory() as src:
            Path(src, "romfs.bin").write_bytes(image)
            paths = extract(Path(src, "romfs.bin"), self.facts(files[:2]), out)
            self.assertEqual([p.read_bytes() for p in paths], [b"DVLB-a", b"DVLB-bb"])

    def test_rejects_other_revision(self):
        files = [("A.shbin", b"DVLB-a")]
        facts = self.facts(files)
        facts["shbin"]["A.shbin"] = "0" * 64
        with tempfile.TemporaryDirectory() as out, tempfile.TemporaryDirectory() as src:
            Path(src, "romfs.bin").write_bytes(build_romfs(files))
            with self.assertRaisesRegex(ValueError, "not the supported revision"):
                extract(Path(src, "romfs.bin"), facts, out)

    def test_rejects_missing_program_and_non_ivfc(self):
        files = [("A.shbin", b"DVLB-a")]
        facts = self.facts(files + [("B.shbin", b"absent")])
        with tempfile.TemporaryDirectory() as out, tempfile.TemporaryDirectory() as src:
            Path(src, "romfs.bin").write_bytes(build_romfs(files))
            with self.assertRaisesRegex(ValueError, "lacks B.shbin"):
                extract(Path(src, "romfs.bin"), facts, out)
        with self.assertRaisesRegex(ValueError, "IVFC"):
            list(romfs_files(io.BytesIO(b"NCCH" + bytes(0x100)), "shaders"))

    def test_summary_parsing(self):
        line = "native_vertex_artifacts=14 title_pass_artifacts=9 vertex_dvle=6 untranslated_geometry_dvle=1"
        self.assertEqual(summary("x\n" + line + "\n")["title_pass_artifacts"], 9)
        with self.assertRaises(ValueError):
            summary("nothing")


if __name__ == "__main__":
    unittest.main()
