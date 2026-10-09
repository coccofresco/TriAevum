import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import water_textures
from water_textures import (CATALOG_HEADER, ROLE_RIVER, ROLE_SURFACE, ROLE_WATERFALL, build_catalog,
                            content_hash, prepare_water_textures, water_role)

ETC1A4 = (0x675B, 0x0000)  # 8 bits per pixel


def cmb(textures):
    """A version-6 CMB with only its texture chunk: [(name, width, height, data)]."""
    entries, payload = b"", b""
    for name, width, height, data in textures:
        entries += struct.pack("<IHHHHHHI16s", len(data), 1, 0, width, height, *ETC1A4, len(payload),
                               name.encode("ascii"))
        payload += data
    chunk = b"tex " + struct.pack("<II", 12 + len(entries), len(textures)) + entries
    texture_data = 0x44 + len(chunk)
    header = bytearray(0x44)
    header[0:4] = b"cmb "
    struct.pack_into("<II", header, 4, 0x44 + len(chunk) + len(payload), 6)
    struct.pack_into("<I", header, 0x2C, 0x44)
    struct.pack_into("<I", header, 0x40, texture_data)
    return bytes(header) + chunk + payload


def zar(files):
    """A ZAR archive of [(name, data)], all typed cmb."""
    names = b"cmb\0" + b"".join(name.encode("ascii") + b"\0" for name, _ in files)
    type_section = 0x18
    meta_section = type_section + 0x10
    list_offset = meta_section + 8 * len(files)
    data_section = list_offset + 4 * len(files)
    names_offset = data_section + 4 * len(files)
    data_offset = names_offset + len(names)
    head = bytearray(b"ZAR\x01" + struct.pack("<IHHIII", 0, 1, len(files), type_section, meta_section, data_section))
    head += struct.pack("<IIII", len(files), list_offset, names_offset, 0)
    name_offset, offsets, blob = names_offset + 4, [], b""
    for name, data in files:
        head += struct.pack("<II", len(data), name_offset)
        name_offset += len(name) + 1
        offsets.append(data_offset + len(blob))
        blob += data
    head += struct.pack(f"<{len(files)}I", *range(len(files)))
    head += struct.pack(f"<{len(files)}I", *offsets) + names
    struct.pack_into("<I", head, 4, len(head) + len(blob))
    return bytes(head) + blob


def level3_romfs(directories):
    """A Level-3 RomFS image: {directory: {file: data}} under the root."""
    def name(text):
        encoded = text.encode("utf-16-le")
        return encoded + b"\0" * (-len(encoded) % 4)

    directory_entries, file_entries, file_data = [], [], b""
    directory_offset = 24 + 4  # after the root (empty name, padded)
    layout = []
    for directory, files in directories.items():
        layout.append((directory_offset, directory, files))
        directory_offset += 24 + len(name(directory))
    directory_table = struct.pack("<6I", 0, 0xFFFFFFFF, layout[0][0] if layout else 0xFFFFFFFF,
                                  0xFFFFFFFF, 0xFFFFFFFF, 0) + b"\0" * 4
    file_offset = 0
    file_table = b""
    for index, (offset, directory, files) in enumerate(layout):
        sibling = layout[index + 1][0] if index + 1 < len(layout) else 0xFFFFFFFF
        first = file_offset if files else 0xFFFFFFFF
        for position, (file_name, data) in enumerate(files.items()):
            entry_size = 32 + len(name(file_name))
            next_file = file_offset + entry_size if position + 1 < len(files) else 0xFFFFFFFF
            file_table += struct.pack("<IIQQII", offset, next_file, len(file_data), len(data), 0xFFFFFFFF,
                                      len(file_name) * 2) + name(file_name)
            file_offset += entry_size
            file_data += data
        directory_table += struct.pack("<6I", 0, sibling, 0xFFFFFFFF, first, 0xFFFFFFFF,
                                       len(directory) * 2) + name(directory)
    directory_meta = 0x28
    file_meta = directory_meta + len(directory_table)
    data_start = file_meta + len(file_table)
    header = struct.pack("<10I", 0x28, 0, 0, directory_meta, len(directory_table), 0, 0, file_meta,
                         len(file_table), data_start)
    return header + directory_table + file_table + file_data


class WaterTexturesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.lake = bytes(range(256)) * 4  # 32x32 at 8 bits per pixel
        self.fall = bytes(reversed(range(256))) * 4
        room = b"ZSI\x09" + b"\0" * 60 + cmb([("spot_water", 32, 32, self.lake),
                                              ("spot_water_kabe", 32, 32, b"\1" * 1024)])
        actor = zar([("model.cmb", cmb([("hylia_taki_01", 32, 32, self.fall),
                                        ("riv_water", 32, 32, self.fall)]))])
        self.files = {"scene": {"spot04_info.zsi": room, "spot04_0_info.zsi": b"ZSI\x09"},
                      "actor": {"zelda_spot.zar": actor, "readme.txt": b"cmb "}}
        self.expected = (f"{CATALOG_HEADER}\n" + "\n".join(sorted([
            f"{content_hash(self.lake):016x} 32 32 {ROLE_SURFACE}",
            f"{content_hash(self.fall):016x} 32 32 {ROLE_WATERFALL | ROLE_RIVER}"])) + "\n").encode("ascii")

    def test_names_classify_water_and_its_roles(self):
        self.assertEqual(water_role("hylia_water_01"), ROLE_SURFACE)
        self.assertEqual(water_role("spot_taki"), ROLE_WATERFALL)
        self.assertEqual(water_role("kawa_02"), ROLE_RIVER)
        self.assertEqual(water_role("uvwater_01"), water_textures.ROLE_UNDERWATER)
        self.assertEqual(water_role("s04_kawa_01"), ROLE_RIVER)  # not bubbles (awa_)
        for name in ("wtr_kabe_01", "izumi_kabe", "kasumi_water", "mizukusa_01", "labo_awa_01", "grass_01"):
            self.assertEqual(water_role(name), 0, name)

    def test_renderer_hash_uses_its_offset_basis(self):
        self.assertEqual(content_hash(b""), 1469598103934665603)
        self.assertEqual(content_hash(b"a"), ((1469598103934665603 ^ 0x61) * 1099511628211) & (2 ** 64 - 1))

    def test_catalog_from_an_extracted_romfs_directory(self):
        for directory, files in self.files.items():
            (self.root / "romfs" / directory).mkdir(parents=True)
            for name, data in files.items():
                (self.root / "romfs" / directory / name).write_bytes(data)
        payload, summary = build_catalog(self.root / "romfs")
        self.assertEqual(payload, self.expected)
        self.assertEqual(summary, {"model_files": 3, "textures": 4, "water_textures": 2})

    def test_catalog_from_level3_and_ivfc_romfs_images(self):
        image = level3_romfs(self.files)
        for name, data in (("level3.bin", image), ("ivfc.bin", b"IVFC" + b"\0" * 0xFFC + image)):
            (self.root / name).write_bytes(data)
            self.assertEqual(build_catalog(self.root / name)[0], self.expected, name)

    def test_prepared_catalog_is_reused_while_its_receipt_matches(self):
        romfs = self.root / "romfs.bin"
        romfs.write_bytes(level3_romfs(self.files))
        recipe = {"inputs": {"romfs": {"sha256": "a" * 64}}}
        data = self.root / "data"
        with patch("water_textures.build_catalog", wraps=build_catalog) as build:
            catalog = prepare_water_textures(data_root=data, recipe=recipe, romfs=romfs)
            self.assertEqual(catalog.read_bytes(), self.expected)
            self.assertEqual(prepare_water_textures(data_root=data, recipe=recipe, romfs=romfs), catalog)
            self.assertEqual(build.call_count, 1)
            catalog.write_bytes(b"tampered")
            self.assertEqual(prepare_water_textures(data_root=data, recipe=recipe, romfs=romfs), catalog)
            self.assertEqual(build.call_count, 2)
            self.assertEqual(catalog.read_bytes(), self.expected)
        self.assertIsNone(prepare_water_textures(data_root=data, recipe={}, romfs=romfs))


if __name__ == "__main__":
    unittest.main()
