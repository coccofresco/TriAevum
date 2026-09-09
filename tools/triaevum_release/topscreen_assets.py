"""Install pinned TopScreen textures locally, never its executable patches."""

from __future__ import annotations

import hashlib
import os
import tempfile
import time
import urllib.request
from pathlib import Path
from typing import Callable

from tools.oot3d.decomp_support.scripts.build_topscreen_texture_override_pack import build_texture_pack

try:
    from .common import atomic_write_bytes, atomic_write_json, load_json_object, sha256_file
    from .https_transport import download_ssl_context
    from .release_platform import host_platform
except ImportError:
    from common import atomic_write_bytes, atomic_write_json, load_json_object, sha256_file
    from https_transport import download_ssl_context
    from release_platform import host_platform


IMPORT_VERSION = 1
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
    cache.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(contract["url"], headers={"User-Agent": "TriAevum-Forge"})
    if not request.full_url.startswith("https://"):
        raise ValueError("TopScreen downloads require HTTPS")
    temporary: Path | None = None
    started = time.monotonic()
    try:
        digest = hashlib.sha256()
        count = 0
        with urllib.request.urlopen(request, timeout=30, context=download_ssl_context()) as response:
            if not response.geturl().startswith("https://"):
                raise ValueError("TopScreen download redirected to an insecure URL")
            with tempfile.NamedTemporaryFile(dir=cache.parent, suffix=".partial", delete=False) as stream:
                temporary = Path(stream.name)
                while block := response.read(1024 * 1024):
                    count += len(block)
                    if count > contract["bytes"] or time.monotonic() - started > 900:
                        raise ValueError("TopScreen download exceeded its size or time limit")
                    digest.update(block)
                    stream.write(block)
                    report("topscreen", f"Downloading TopScreen textures: {count * 100 // contract['bytes']}%")
        if count != contract["bytes"] or digest.hexdigest() != contract["sha256"]:
            raise ValueError("TopScreen download failed integrity verification")
        os.replace(temporary, cache)
        return cache
    except (OSError, ValueError) as exc:
        raise ValueError(
            f"Could not prepare TopScreen textures: {exc}. Retry with internet access, "
            f"or place the official {ARCHIVE_NAME} beside {host_platform().forge}.") from exc
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


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
    receipt_path = directory / "import.json"
    if pack.is_file() and receipt_path.is_file():
        receipt = load_json_object(receipt_path)
        if receipt.get("source") == identity and receipt.get("pack_sha256") == sha256_file(pack):
            return pack

    archive = acquire_archive(root, data_root, contract, report)
    report("topscreen", "Importing native TopScreen UI textures...")
    payload = build_texture_pack(archive=archive, original_romfs_image=romfs)
    directory.mkdir(parents=True, exist_ok=True)
    atomic_write_bytes(pack, payload)
    atomic_write_json(receipt_path, {"source": identity, "pack_sha256": sha256_file(pack),
                                   "attribution": "TopScreen / Single Screen Experience by M-1",
                                   "source_url": "https://gamebanana.com/mods/695893"})
    return pack
