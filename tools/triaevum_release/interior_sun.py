"""ROM-derived interior sun, prepared by the shipped runtime without booting a title."""
from __future__ import annotations

import hashlib
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path
from typing import Callable

try:
    from .common import atomic_write_json, load_json_object, sha256_file
    from .precompiled_titles import checked_file
    from .native_process import run_native, native_helper_environment
except ImportError:
    from common import atomic_write_json, load_json_object, sha256_file
    from precompiled_titles import checked_file
    from native_process import run_native, native_helper_environment

FORMAT = "triaevum_interior_sun_v1"
CRITERION_VERSION = 3


def _outputs(result: dict, cache: Path) -> dict[str, str]:
    if (result.get("format") != FORMAT or result.get("criterion_version") != CRITERION_VERSION
            or result.get("game_booted") is not False
            or not isinstance(result.get("cache_directory"), str)
            or Path(result["cache_directory"]).resolve() != cache
            or type(result.get("rooms")) is not int or not 0 < result["rooms"] <= 4096
            or not isinstance(result.get("entries"), list) or not result["entries"]
            or len(result["entries"]) > 4096 * 17):
        raise ValueError("Invalid interior-sun prebuild manifest")
    hashes = {}
    for entry in result["entries"]:
        if not isinstance(entry, dict) or entry.get("status") not in ("built", "hit", "precomputed", "fallback"):
            raise ValueError("Invalid interior-sun room result")
        if entry["status"] == "fallback":
            if not isinstance(entry.get("reason"), str) or not entry["reason"]:
                raise ValueError("Unexplained interior-sun fallback")
            continue
        fp, interior, relative = (entry.get(k) for k in ("fingerprint", "interior", "path"))
        if not isinstance(fp, str) or not re.fullmatch(r"[0-9a-f]{16}", fp) or type(interior) is not bool:
            raise ValueError("Invalid interior-sun cache identity")
        expected = f"shadow-shells/{fp}{'' if interior else '-exterior'}.bin"
        if relative != expected:
            raise ValueError("Invalid interior-sun cache path")
        path = cache / relative
        if not path.resolve().is_relative_to(cache) or not path.is_file():
            raise ValueError("Interior-sun cache is missing")
        if relative in hashes:
            continue
        size = path.stat().st_size
        if not 48 <= size <= 28_000_048:
            raise ValueError("Invalid interior-sun cache size")
        with path.open("rb") as stream:
            magic, version, actual_fp, sectors, raster, vertices, triangles, kind, reserved = struct.unpack(
                "<4sIQ6I", stream.read(40))
        if (magic != b"RSSH" or version != CRITERION_VERSION or actual_fp != int(fp, 16)
                or sectors != 64 or raster != 512 or vertices > 1_500_000 or triangles > 500_000
                or kind != int(interior) or reserved or size != 48 + vertices * 12 + triangles * 20):
            raise ValueError("Invalid interior-sun cache header")
        hashes[relative] = sha256_file(path)
    return hashes


def prepare_interior_sun(*, root: Path, data_root: Path, runtime_record: dict,
                         romfs: Path, romfs_sha256: str, cache_directory: Path,
                         mask_table: Path | None = None,
                         report: Callable[[str, str], None] = lambda *_: None) -> dict:
    """A release mask table (integrity-checked by the caller) supplies the
    masks of the rooms it covers; the runtime builds the others."""
    runtime = checked_file(root, runtime_record)
    if not re.fullmatch(r"[0-9a-f]{64}", romfs_sha256):
        raise ValueError("Invalid RomFS content identity")
    cache = cache_directory.resolve()
    identity = {"romfs_sha256": romfs_sha256, "criterion_version": CRITERION_VERSION,
                "runtime_sha256": sha256_file(runtime), "cache_directory": str(cache)}
    if mask_table is not None:
        identity["mask_table_sha256"] = sha256_file(mask_table)
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    receipts = data_root.resolve() / "interior-sun"
    receipt_path = receipts / f"{key}.json"
    try:
        receipt = load_json_object(receipt_path)
        if receipt.get("identity") == identity and receipt.get("outputs") == _outputs(receipt["result"], cache):
            report("interior-sun", "Reusing offline interior sun...")
            return {**receipt["result"], "receipt_reused": True}
    except (OSError, ValueError, KeyError, TypeError, struct.error):
        pass
    receipts.mkdir(parents=True, exist_ok=True)
    report("interior-sun", "Preparing interior sun from your ROM (offline)...")
    with tempfile.TemporaryDirectory(prefix=".preparing-", dir=receipts) as temporary:
        manifest = Path(temporary) / "result.json"
        command = [str(runtime), "--prebuild-interior-sun", "--romfs", str(romfs.resolve()),
                   "--renderer-cache-directory", str(cache), "--manifest", str(manifest)]
        if mask_table is not None:
            command += ["--mask-table", str(mask_table.resolve())]
        completed = run_native(command, cwd=root, env=native_helper_environment(runtime),
                               capture_output=True, text=True, errors="replace", timeout=3600,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if completed.returncode:
            raise ValueError("Interior-sun preparation failed: " + (completed.stderr or completed.stdout)[-4000:])
        result = load_json_object(manifest)
        outputs = _outputs(result, cache)
        atomic_write_json(receipt_path, {"identity": identity, "outputs": outputs, "result": result})
    report("interior-sun", f"Interior sun ready: {len(outputs)} caches ({result.get('precomputed', 0)} from the "
                           f"release table), {result.get('fallback', 0)} native fallbacks")
    return {**result, "receipt_reused": False}
