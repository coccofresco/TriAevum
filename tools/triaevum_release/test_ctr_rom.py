from __future__ import annotations

import hashlib
import struct
import tempfile
import unittest
from pathlib import Path

from ctr_rom import (
    CtrRomError,
    extract_decrypted_rom,
    is_supported_rom_path,
    publish_extracted_inputs,
)


PARTITION_BASE = 0x4000
PARTITION_SIZE = 0x4000
CODE = b"synthetic-uncompressed-exefs-code"
PROGRAM_ID = 0x0004000000033600


def _u32_into(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def _build_rom(path: Path, *, declares_decrypted: bool = True) -> bytes:
    image = bytearray(PARTITION_BASE + PARTITION_SIZE)
    image[0x100:0x104] = b"NCSD"
    _u32_into(image, 0x120, PARTITION_BASE // 0x200)
    _u32_into(image, 0x124, PARTITION_SIZE // 0x200)

    ncch = PARTITION_BASE
    image[ncch + 0x100 : ncch + 0x104] = b"NCCH"
    _u32_into(image, ncch + 0x108, PARTITION_SIZE // 0x200)
    struct.pack_into("<Q", image, ncch + 0x118, PROGRAM_ID)
    _u32_into(image, ncch + 0x180, 0x400)
    image[ncch + 0x18D] = 0x02
    image[ncch + 0x18F] = 0x04 if declares_decrypted else 0

    exheader = ncch + 0x200
    image[exheader : exheader + 0x800] = bytes(index & 0xFF for index in range(0x800))
    image[exheader + 0x0D] = 0

    exefs = ncch + 0xC00
    _u32_into(image, ncch + 0x1A0, 0xC00 // 0x200)
    _u32_into(image, ncch + 0x1A4, 2)
    image[exefs : exefs + 8] = b".code\0\0\0"
    _u32_into(image, exefs + 8, 0)
    _u32_into(image, exefs + 12, len(CODE))
    image[exefs + 0x200 : exefs + 0x200 + len(CODE)] = CODE

    romfs = ncch + 0x2000
    _u32_into(image, ncch + 0x1B0, 0x2000 // 0x200)
    _u32_into(image, ncch + 0x1B4, 0x2000 // 0x200)
    image[romfs : romfs + 4] = b"IVFC"
    image[romfs + 4 : romfs + 0x2000] = b"R" * (0x2000 - 4)
    path.write_bytes(image)
    return bytes(image[exheader : exheader + 0x800])


class CtrRomTests(unittest.TestCase):
    def test_accepts_3ds_and_cci_as_the_same_ncsd_container(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for suffix in (".3ds", ".cci", ".CCI"):
                with self.subTest(suffix=suffix):
                    rom = root / f"game{suffix}"
                    expected_exheader = _build_rom(rom)
                    output = (
                        root / f"out-{suffix[1:].lower()}-{len(list(root.iterdir()))}"
                    )
                    extracted = extract_decrypted_rom(rom, output)

                    self.assertEqual(extracted.container_kind, "NCSD")
                    self.assertEqual(extracted.partition_index, 0)
                    self.assertEqual(extracted.program_id, PROGRAM_ID)
                    self.assertEqual(extracted.code.path.read_bytes(), CODE)
                    self.assertEqual(
                        extracted.exheader.path.read_bytes(), expected_exheader
                    )
                    self.assertEqual(extracted.romfs.bytes, 0x2000)
                    self.assertEqual(
                        extracted.romfs.sha256,
                        hashlib.sha256(b"IVFC" + b"R" * (0x2000 - 4)).hexdigest(),
                    )

    def test_accepts_plaintext_when_legacy_crypto_flag_is_retained(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rom = root / "legacy-flag.cci"
            _build_rom(rom, declares_decrypted=False)
            extracted = extract_decrypted_rom(rom, root / "output")
            self.assertEqual(extracted.code.path.read_bytes(), CODE)

    def test_public_import_requires_a_rom_extension(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rom = root / "game.bin"
            _build_rom(rom)
            with self.assertRaisesRegex(CtrRomError, r"\.3ds or \.cci"):
                extract_decrypted_rom(rom, root / "output")
            self.assertFalse((root / "output").exists())

    def test_supported_suffix_check_is_case_insensitive(self) -> None:
        self.assertTrue(is_supported_rom_path(Path("game.3DS")))
        self.assertTrue(is_supported_rom_path(Path("game.CCI")))
        self.assertFalse(is_supported_rom_path(Path("game.cia")))

    def test_publishes_inputs_under_a_stable_content_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first_rom = root / "first.cci"
            _build_rom(first_rom)
            first_staging = root / "first-staging"
            first = publish_extracted_inputs(
                extract_decrypted_rom(first_rom, first_staging), root / "sources"
            )
            self.assertFalse(first_staging.exists())

            second_rom = root / "second.3ds"
            _build_rom(second_rom)
            second_staging = root / "second-staging"
            second = publish_extracted_inputs(
                extract_decrypted_rom(second_rom, second_staging), root / "sources"
            )
            self.assertFalse(second_staging.exists())
            self.assertEqual(first.code.path, second.code.path)
            self.assertEqual(first.romfs.path, second.romfs.path)


if __name__ == "__main__":
    unittest.main()
