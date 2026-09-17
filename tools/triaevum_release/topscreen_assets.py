"""Install pinned TopScreen textures and font coverage, never executable patches."""

from __future__ import annotations

import hashlib
import http.client
from pathlib import Path
from typing import Callable

from tools.oot3d.decomp_support.scripts.build_topscreen_texture_override_pack import build_texture_pack
from tools.oot3d.decomp_support.scripts.prepare_qbf_font_pack import prepare_font_pack

try:
    from .common import atomic_write_bytes, atomic_write_json, load_json_object, sha256_file
    from .verified_download import download_verified
    from .release_platform import host_platform
except ImportError:
    from common import atomic_write_bytes, atomic_write_json, load_json_object, sha256_file
    from verified_download import download_verified
    from release_platform import host_platform


IMPORT_VERSION = 1
FONT_IMPORT_VERSION = 2
ARCHIVE_NAME = "topscreen211.zip"


def acquire_archive(root: Path, data_root: Path, contract: dict,
                    report: Callable[[str, str], None]) -> Path:
    cache = data_root / "downloads" / ARCHIVE_NAME
    for candidate in (root / ARCHIVE_NAME, root / "mods" / ARCHIVE_NAME, cache):
        if candidate.is_file():
            if (candidate.stat().st_size != contract["bytes"] or
                    sha256_file(candidate) != contract["sha256"]):
                raise ValueError(f"TopScreen 2.1.1 archive failed verification: {candidate}")
            return candidate

    report("topscreen", "Downloading the official TopScreen 2.1.1 texture source...")
    try:
        return download_verified(contract["url"], cache,
            size=contract["bytes"], sha256=contract["sha256"],
            progress=lambda count, size: report("topscreen", f"Downloading TopScreen textures: {count * 100 // size}%"),
            retry=lambda attempt, error: report("topscreen", f"Retrying TopScreen download ({attempt}/3): {error}"))
    except (OSError, EOFError, http.client.HTTPException, ValueError) as exc:
        raise ValueError(
            f"Could not prepare TopScreen textures: {exc}. Retry with internet access, "
            f"or place the official {ARCHIVE_NAME} beside {host_platform().forge}.") from exc


def prepare_topscreen_assets(*, root: Path, data_root: Path, recipe: dict,
                            romfs: Path,
                            report: Callable[[str, str], None] = lambda *_: None) -> Path | None:
    contract = recipe.get("optional_inputs", {}).get("topscreen_2_1_1_archive")
    if contract is None:
        return None  # Other title/revision recipes do not inherit OOT3D UI assets.
    identity = {"import_version": IMPORT_VERSION, "archive_sha256": contract["sha256"],
                "romfs_sha256": recipe["inputs"]["romfs"]["sha256"]}
    key = hashlib.sha256(repr(sorted(identity.items())).encode("ascii")).hexdigest()
    directory = data_root / "mods" / "topscreen" / key
    pack = directory / "atlas_overrides.o3tu"
    font_pack = directory / "font_coverage.zip"
    font_receipt_path = directory / "fonts_import.json"
    receipt_path = directory / "import.json"
    textures_valid = False
    if pack.is_file() and receipt_path.is_file():
        receipt = load_json_object(receipt_path)
        textures_valid = (receipt.get("source") == identity and
                          receipt.get("pack_sha256") == sha256_file(pack))
    font_identity = {**identity, "font_import_version": FONT_IMPORT_VERSION}
    fonts_valid = False
    if font_pack.is_file() and font_receipt_path.is_file():
        receipt = load_json_object(font_receipt_path)
        fonts_valid = (receipt.get("source") == font_identity and
                       receipt.get("pack_sha256") == sha256_file(font_pack))
    if textures_valid and fonts_valid:
        return pack

    archive = acquire_archive(root, data_root, contract, report)
    directory.mkdir(parents=True, exist_ok=True)
    if not textures_valid:
        report("topscreen", "Importing native TopScreen UI textures...")
        payload = build_texture_pack(archive=archive, original_romfs_image=romfs)
        atomic_write_bytes(pack, payload)
        atomic_write_json(receipt_path, {"source": identity, "pack_sha256": sha256_file(pack),
                                       "attribution": "TopScreen / Single Screen Experience by M-1",
                                       "source_url": "https://gamebanana.com/mods/695893"})
    if not fonts_valid:
        report("topscreen", "Preparing TopScreen font coverage...")
        manifest = prepare_font_pack(archive=archive, romfs=romfs, output=font_pack)
        atomic_write_json(font_receipt_path, {
            "source": font_identity, "pack_sha256": sha256_file(font_pack),
            "runtime_enabled": True,
            "minimum_output_height": 480,
            "status": "native_atlas_coverage_ready",
            "font_count": len(manifest["fonts"]),
            "attribution": "TopScreen / Single Screen Experience by M-1 / rlgcarrot",
            "source_url": "https://gamebanana.com/mods/695893"})
    return pack
