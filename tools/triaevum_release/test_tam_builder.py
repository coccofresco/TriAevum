from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tam_builder import (
    FORMAT_MAJOR,
    HEADER,
    HEADER_SIZE,
    MAGIC,
    RUNTIME_ABI_V1,
    SECTION,
    SECTION_ENTRY_SIZE,
    SECTION_METADATA_JSON,
    SECTION_NATIVE_IMAGE,
    SECTION_TITLE_AOT_IMAGE,
    build_tam,
)


class TamBuilderTests(unittest.TestCase):
    def test_builds_canonical_hashed_sections(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = root / "module.dll"
            native.write_bytes(b"MZ native test module")
            module, metadata = build_tam(
                native,
                recipe="fixture",
                target_triple="x86_64-pc-windows-msvc",
                source_identity="a" * 64,
                translator_identity="b" * 64,
                required_services=(0x41434950, 0x49445541),
                physical_memory_regions=(
                    (0x18000000, 0x1F000000, 0x00600000),
                    (0x20000000, 0x14000000, 0x08000000),
                ),
            )
            header = HEADER.unpack_from(module)
            self.assertEqual(header[0], MAGIC)
            self.assertEqual(header[1], FORMAT_MAJOR)
            self.assertEqual(header[3], HEADER_SIZE)
            self.assertEqual(header[4], SECTION_ENTRY_SIZE)
            self.assertEqual(header[5], RUNTIME_ABI_V1)
            self.assertEqual(header[6], 2)
            self.assertEqual(header[7], len(module))
            first = SECTION.unpack_from(module, HEADER_SIZE)
            second = SECTION.unpack_from(module, HEADER_SIZE + SECTION_ENTRY_SIZE)
            self.assertEqual(first[0], SECTION_METADATA_JSON)
            self.assertEqual(second[0], SECTION_NATIVE_IMAGE)
            for descriptor in (first, second):
                offset, size, expected = descriptor[2], descriptor[3], descriptor[4]
                self.assertEqual(
                    hashlib.sha256(module[offset : offset + size]).digest(), expected
                )
            decoded = json.loads(module[first[2] : first[2] + first[3]])
            self.assertEqual(decoded, metadata)
            self.assertFalse(decoded["redistributable"])
            self.assertEqual(
                decoded["required_services"],
                [
                    {"id": 0x41434950, "schema_version": 1},
                    {"id": 0x49445541, "schema_version": 1},
                ],
            )
            self.assertEqual(
                decoded["physical_memory_regions"][0],
                {
                    "physical_base": 0x18000000,
                    "guest_base": 0x1F000000,
                    "bytes": 0x00600000,
                },
            )

    def test_bundles_hashed_title_aot_companion(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = root / "oot3d_game_module.dll"
            title_aot = root / "triaevum_title_aot.dll"
            native.write_bytes(b"MZ generic runtime module")
            title_aot.write_bytes(b"MZ private title AOT")
            module, metadata = build_tam(
                native,
                title_aot_image=title_aot,
                recipe="fixture",
                target_triple="x86_64-pc-windows-msvc",
                source_identity="a" * 64,
                translator_identity="b" * 64,
            )
            header = HEADER.unpack_from(module)
            self.assertEqual(header[2], 1)
            self.assertEqual(header[6], 3)
            title = SECTION.unpack_from(
                module, HEADER_SIZE + 2 * SECTION_ENTRY_SIZE
            )
            self.assertEqual(title[0], SECTION_TITLE_AOT_IMAGE)
            self.assertEqual(
                hashlib.sha256(module[title[2] : title[2] + title[3]]).digest(),
                title[4],
            )
            self.assertEqual(metadata["title_aot_image"]["abi"], 1)
            self.assertEqual(
                metadata["title_aot_image"]["sha256"],
                hashlib.sha256(title_aot.read_bytes()).hexdigest(),
            )

    def test_rejects_non_hash_source_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            native = Path(temporary) / "module.dll"
            native.write_bytes(b"MZ")
            with self.assertRaises(ValueError):
                build_tam(
                    native,
                    recipe="fixture",
                    target_triple="x86_64-pc-windows-msvc",
                    source_identity="not-a-hash",
                    translator_identity="b" * 64,
                )

    def test_rejects_unknown_target_triple(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            native = Path(temporary) / "module.dll"
            native.write_bytes(b"MZ")
            with self.assertRaises(ValueError):
                build_tam(
                    native,
                    recipe="fixture",
                    target_triple="host-default",
                    source_identity="a" * 64,
                    translator_identity="b" * 64,
                )

    def test_rejects_duplicate_required_service(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            native = Path(temporary) / "module.dll"
            native.write_bytes(b"MZ")
            with self.assertRaises(ValueError):
                build_tam(
                    native,
                    recipe="fixture",
                    target_triple="x86_64-pc-windows-msvc",
                    source_identity="a" * 64,
                    translator_identity="b" * 64,
                    required_services=(0x41434950, 0x41434950),
                )

    def test_rejects_pica_without_physical_memory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            native = Path(temporary) / "module.dll"
            native.write_bytes(b"MZ")
            with self.assertRaises(ValueError):
                build_tam(
                    native,
                    recipe="fixture",
                    target_triple="x86_64-pc-windows-msvc",
                    source_identity="a" * 64,
                    translator_identity="b" * 64,
                    required_services=(0x41434950,),
                )

    def test_rejects_overlapping_physical_memory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            native = Path(temporary) / "module.dll"
            native.write_bytes(b"MZ")
            with self.assertRaises(ValueError):
                build_tam(
                    native,
                    recipe="fixture",
                    target_triple="x86_64-pc-windows-msvc",
                    source_identity="a" * 64,
                    translator_identity="b" * 64,
                    physical_memory_regions=(
                        (0x18000000, 0x1F000000, 0x1000),
                        (0x18000800, 0x14000000, 0x1000),
                    ),
                )


if __name__ == "__main__":
    unittest.main()
