import copy
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import model_catalog as models
import water_textures as water
from test_water_textures import level3_romfs

# Synthetic layout, not a title-id fallback. Production facts live in titles/.
PROFILE = {"title": "fixture", "revision": 1, "directories": ["rooms", "objects"],
           "suffixes": ["_info.zsi", ".gar.lzs", ".gar"], "zsi_magic": "5a534909",
           "cmb": {"version": 123, "header_bytes": 72, "texture_chunk_offset": 48, "texture_data_offset": 68},
           "water_rules": {"exclude": ["wall"], "roles": [
               {"mask": 4, "contains": ["fall"]}, {"mask": 1, "contains": ["water"]}]}}


def cmb(textures, profile=PROFILE):
    entries, payload = b"", b""
    for name, width, height, data in textures:
        entries += struct.pack("<IHHHHHHI16s", len(data), 1, 0, width, height, 0x675B, 0,
                               len(payload), name.encode("ascii"))
        payload += data
    layout = profile["cmb"]
    header = bytearray(layout["header_bytes"])
    header[:4] = b"cmb "
    chunk = b"tex " + struct.pack("<II", 12 + len(entries), len(textures)) + entries
    struct.pack_into("<II", header, 4, len(header) + len(chunk) + len(payload), layout["version"])
    struct.pack_into("<I", header, layout["texture_chunk_offset"], len(header))
    struct.pack_into("<I", header, layout["texture_data_offset"], len(header) + len(chunk))
    return bytes(header) + chunk + payload


def gar(files):
    count = len(files)
    table, offsets = 24, 24 + count * 12
    names_at = offsets + count * 4
    names = b"".join(n.encode("ascii") + b"\0" for n, _ in files)
    start = names_at + len(names)
    head = bytearray(start)
    head[:4] = b"GAR\2"
    struct.pack_into("<H", head, 10, count)
    struct.pack_into("<II", head, 16, table, offsets)
    head[names_at:] = names
    payload = bytearray()
    for index, (name, data) in enumerate(files):
        struct.pack_into("<III", head, table + index * 12, len(data), 0, names_at)
        struct.pack_into("<I", head, offsets + index * 4, start + len(payload))
        names_at += len(name) + 1
        payload.extend(data)
    struct.pack_into("<I", head, 4, len(head) + len(payload))
    return bytes(head + payload)


def lzs(data):
    payload = b"".join(bytes([(1 << len(data[i:i + 8])) - 1]) + data[i:i + 8] for i in range(0, len(data), 8))
    return b"LzS\1" + bytes(4) + struct.pack("<II", len(data), len(payload)) + payload


def fixture_files():
    base = bytes(range(64))
    room = b"ZSI\x09" + bytes(12) + cmb([("water", 8, 8, base), ("water_wall", 8, 8, bytes(64))])
    archive = gar([("a.cmb", cmb([("fall", 8, 8, base)])), ("ignore.bin", b"cmb not a model")])
    return {"rooms": {"one_info.zsi": lzs(room), "ignore.txt": b"not a model"},
            "objects": {"one.gar.lzs": lzs(archive), "two.gar": archive}}


class ModelCatalogTests(unittest.TestCase):
    def test_directory_image_and_compressed_archives(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            files = fixture_files()
            for directory, entries in files.items():
                (root / directory).mkdir()
                for name, payload in entries.items():
                    (root / directory / name).write_bytes(payload)
            expected = f"{water.CATALOG_HEADER}\n{water.content_hash(bytes(range(64))):016x} 8 8 5\n".encode()
            self.assertEqual(water.build_catalog(root, PROFILE),
                             (expected, {"model_files": 3, "textures": 4, "water_textures": 1}))
            image = level3_romfs(files)
            for payload in (image, b"IVFC" + bytes(0xFFC) + image):
                (root / "image.bin").write_bytes(payload)
                self.assertEqual(water.build_catalog(root / "image.bin", PROFILE)[0], expected)

    def test_profile_cache_invalidation_and_tamper(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            romfs = root / "romfs.bin"
            romfs.write_bytes(level3_romfs(fixture_files()))
            recipe = {"inputs": {"romfs": {"sha256": "a" * 64}}, "water_catalog": copy.deepcopy(PROFILE)}
            with patch.object(water, "build_catalog", wraps=water.build_catalog) as build:
                first = water.prepare_water_textures(data_root=root, recipe=recipe, romfs=romfs)
                self.assertEqual(water.prepare_water_textures(data_root=root, recipe=recipe, romfs=romfs), first)
                self.assertEqual(build.call_count, 1)
                first.write_bytes(b"tampered")
                water.prepare_water_textures(data_root=root, recipe=recipe, romfs=romfs)
                self.assertEqual(build.call_count, 2)
                recipe["water_catalog"]["revision"] += 1
                second = water.prepare_water_textures(data_root=root, recipe=recipe, romfs=romfs)
                self.assertNotEqual(first, second)
                self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_bounded_lzs_and_overlapping_ring(self):
        payload = b"\x01a\xee\xf2"  # one literal then five overlapping copies
        self.assertEqual(models.unpack_lzs(b"LzS\1" + bytes(4) + struct.pack("<II", 6, 4) + payload), b"aaaaaa")
        for raw in (b"LzS\1", lzs(b"abc")[:-1], b"LzS\1" + bytes(4) + struct.pack("<II", 2, 3) + b"\0\0\0"):
            with self.assertRaises(ValueError): models.unpack_lzs(raw)

    def test_cmb_member_bounds_not_enclosing_container(self):
        good = cmb([("water", 8, 8, bytes(64))])
        bad = bytearray(good)
        struct.pack_into("<I", bad, 4, len(good) - 1)
        with self.assertRaises(ValueError): list(models.cmb_textures(bytes(bad) + bytes(1024), PROFILE["cmb"]))
        bad = bytearray(good)
        struct.pack_into("<I", bad, 8, 124)
        with self.assertRaises(ValueError): list(models.cmb_textures(bytes(bad), PROFILE["cmb"]))
        with self.assertRaises(ValueError): list(models.gar_models(gar([("a.cmb", good), ("a.cmb", good)])))

    def test_profile_rejects_path_escape_and_bad_role(self):
        for field, value in (("directories", ["../other"]), ("suffixes", [""]), ("revision", 0)):
            profile = copy.deepcopy(PROFILE)
            profile[field] = value
            with self.assertRaises(ValueError): models.validate_profile(profile)
        profile = copy.deepcopy(PROFILE)
        profile["water_rules"]["roles"][0]["mask"] = 16
        with self.assertRaises(ValueError): models.validate_profile(profile)


if __name__ == "__main__": unittest.main()
