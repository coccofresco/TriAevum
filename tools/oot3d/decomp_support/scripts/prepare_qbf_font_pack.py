"""Forge and developer preparation of HD coverage; not a runtime activation switch.

Output is local, ROM/mod-derived user data, never a public release artifact.
Reads only exact font entries; never extracts executable patches from the mod.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from tools.oot3d.decomp_support.scripts.build_topscreen_texture_override_pack import read_level3_romfs_file
from tools.oot3d.decomp_support.scripts.qbf_font import MAX_FONT_BYTES, pair_font_coverage, parse_qbf


# SingleScreen 3.0 and 3.1 carry the HD Latin font in their emulator layout
# (TopScreen 2.1.1 is retired). Their sys8.qbf is not an A4 coverage font and
# stays native. Paths are relative to the archive's release directory.
SINGLESCREEN_FONT_SOURCES = (
    ("message/eu/ltn16.qbf", "EUR/Emulator/load/mods/0004000000033600/romfs/message/eu/ltn16.qbf"),
    ("message/us/ltn16.qbf", "USA/Emulator/load/mods/0004000000033500/romfs/message/us/ltn16.qbf"),
)

def prepare_font_pack(*, archive: Path, romfs: Path, output: Path,
                      archive_root: str = "SingleScreen_3.0") -> dict:
    """Pair the ROM's fonts with a SingleScreen archive."""
    assets = {}
    fonts = []
    with zipfile.ZipFile(archive) as source:
        sources = [(native, f"{archive_root}/{member}") for native, member in SINGLESCREEN_FONT_SOURCES]
        for native_path, archive_path in sources:
            try:
                native = parse_qbf(read_level3_romfs_file(romfs, native_path))
            except FileNotFoundError:
                continue  # A region's ROM need not contain the other region.
            entries = [entry for entry in source.infolist() if entry.filename == archive_path]
            if len(entries) != 1 or entries[0].file_size > MAX_FONT_BYTES:
                raise ValueError(f"absent, ambiguous or oversized QBF: {archive_path}")
            replacement = parse_qbf(source.read(entries[0]))
            contract = pair_font_coverage(native, replacement)
            asset = f"coverage/{replacement.sha256}.qbf"
            original_asset = f"native/{native.sha256}.qbf"
            assets[asset] = replacement.data
            assets[original_asset] = native.data
            fonts.append({"romfs_path": native_path, "coverage_file": asset,
                          "native_file": original_asset, **contract})
    if not fonts:
        raise ValueError("ROM contains no supported Latin QBF font")
    archive_hash = hashlib.sha256()
    with archive.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            archive_hash.update(chunk)
    manifest = {"format": "oot3d_qbf_coverage_v1", "runtime_enabled": True,
                "minimum_output_height": 480,
                "status": "native_atlas_coverage_ready",
                "fonts": fonts,
                "source_archive_sha256": archive_hash.hexdigest(),
                "attribution": "TopScreen / Single Screen Experience by M-1 / rlgcarrot"}
    output.parent.mkdir(parents=True, exist_ok=True)
    # Publish only after validating every font. Fixed ZIP timestamps permit
    # byte-for-byte reproducibility and keep generated data separate from code.
    temporary = output.with_name(output.name + ".partial")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_STORED) as target:
            for name, payload in sorted(assets.items()):
                target.writestr(zipfile.ZipInfo(name), payload)
            target.writestr(zipfile.ZipInfo("manifest.json"),
                            json.dumps(manifest, sort_keys=True, indent=2).encode("utf-8"))
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--romfs", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--archive-root", default="SingleScreen_3.0",
                        help="the archive's release directory, e.g. SingleScreen_3.1")
    args = parser.parse_args()
    manifest = prepare_font_pack(archive=args.archive, romfs=args.romfs, output=args.output,
                                 archive_root=args.archive_root)
    print(json.dumps({"output": str(args.output), "runtime_enabled": True,
                      "fonts": [{k: v for k, v in font.items() if k != "characters"}
                                for font in manifest["fonts"]]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
