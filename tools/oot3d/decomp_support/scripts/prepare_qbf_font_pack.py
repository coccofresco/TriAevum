"""Forge and developer preparation of HD coverage; not a runtime activation switch.

Output is local, ROM-derived user data, never a public release artifact. The HD
font itself ships in the TriAevum package; the pack pairs it with the ROM's own
native fonts.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from tools.oot3d.decomp_support.scripts.build_topscreen_texture_override_pack import read_level3_romfs_file
from tools.oot3d.decomp_support.scripts.qbf_font import MAX_FONT_BYTES, pair_font_coverage, parse_qbf


# SingleScreen 3.1's HD Latin font replaces the same font in both regions'
# RomFS (its EUR and USA layouts carry the same bytes). Its sys8.qbf is not an
# A4 coverage font and stays native.
NATIVE_FONTS = ("message/eu/ltn16.qbf", "message/us/ltn16.qbf")
ATTRIBUTION = "SingleScreen (TopScreen / Single Screen Experience) by M-1 / rlgcarrot"


def prepare_font_pack(*, font: Path, romfs: Path, output: Path) -> dict:
    """Pair the ROM's Latin fonts with the HD font."""
    if font.stat().st_size > MAX_FONT_BYTES:
        raise ValueError(f"oversized QBF: {font}")
    font_bytes = font.read_bytes()
    replacement = parse_qbf(font_bytes)
    assets = {}
    fonts = []
    for native_path in NATIVE_FONTS:
        try:
            native = parse_qbf(read_level3_romfs_file(romfs, native_path))
        except FileNotFoundError:
            continue  # A region's ROM need not contain the other region.
        contract = pair_font_coverage(native, replacement)
        asset = f"coverage/{replacement.sha256}.qbf"
        original_asset = f"native/{native.sha256}.qbf"
        assets[asset] = replacement.data
        assets[original_asset] = native.data
        fonts.append({"romfs_path": native_path, "coverage_file": asset,
                      "native_file": original_asset, **contract})
    if not fonts:
        raise ValueError("ROM contains no supported Latin QBF font")
    manifest = {"format": "oot3d_qbf_coverage_v1", "runtime_enabled": True,
                "minimum_output_height": 480,
                "status": "native_atlas_coverage_ready",
                "fonts": fonts,
                "source_font_sha256": hashlib.sha256(font_bytes).hexdigest(),
                "attribution": ATTRIBUTION}
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
    parser.add_argument("--font", required=True, type=Path,
                        help="the HD ltn16.qbf, e.g. runtime/triaevum_resources/topscreen/"
                             "singlescreen-3.1/ltn16.qbf")
    parser.add_argument("--romfs", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    manifest = prepare_font_pack(font=args.font, romfs=args.romfs, output=args.output)
    print(json.dumps({"output": str(args.output), "runtime_enabled": True,
                      "fonts": [{k: v for k, v in font.items() if k != "characters"}
                                for font in manifest["fonts"]]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
