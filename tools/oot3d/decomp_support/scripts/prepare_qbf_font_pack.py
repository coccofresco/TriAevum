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


FONT_SOURCES = (
    ("message/eu/ltn16.qbf", "TopScreen2.1.1/EUR/4K Textures/load/mods/0004000000033600/romfs/message/eu/ltn16.qbf"),
    ("message/us/ltn16.qbf", "TopScreen2.1.1/USA/4K Textures/load/mods/0004000000033500/romfs/message/us/ltn16.qbf"),
)


def prepare_font_pack(*, archive: Path, romfs: Path, output: Path) -> dict:
    assets = {}
    fonts = []
    with zipfile.ZipFile(archive) as source:
        for native_path, archive_path in FONT_SOURCES:
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
            assets[asset] = replacement.data
            fonts.append({"romfs_path": native_path, "coverage_file": asset, **contract})
    if not fonts:
        raise ValueError("ROM contains no supported Latin QBF font")
    archive_hash = hashlib.sha256()
    with archive.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            archive_hash.update(chunk)
    manifest = {"format": "oot3d_qbf_coverage_v1", "runtime_enabled": False,
                "status": "prepared_only_pending_native_atlas_consumer",
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
    args = parser.parse_args()
    manifest = prepare_font_pack(archive=args.archive, romfs=args.romfs, output=args.output)
    print(json.dumps({"output": str(args.output), "runtime_enabled": False,
                      "fonts": [{k: v for k, v in font.items() if k != "characters"}
                                for font in manifest["fonts"]]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
