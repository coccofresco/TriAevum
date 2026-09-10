"""Optional Forge host for the renderer-owned, headless NRI preparation job.

Only process launch and the SDL preference location are platform-specific. GPU
identity, pipeline layouts, cache validation and creation stay in the renderer.
No title boot, window, game compiler, SDK download or cross-device cache reuse.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable

try:
    from .common import atomic_write_json, load_json_object, sha256_file
    from .native_process import native_process_environment
    from .precompiled_titles import checked_file
except ImportError:
    from common import atomic_write_json, load_json_object, sha256_file
    from native_process import native_process_environment
    from precompiled_titles import checked_file

FORMAT = "triaevum_device_pipeline_preparation_v1"
CACHE_FILENAME = "nri_pipeline_cache.bin"


def renderer_cache_directory() -> Path:
    """Match SDL_GetPrefPath(nullptr, 'oot3d_native_vulkan')/shader_cache.

    Android's in-process host supplies its app-private directory instead. Under
    Flatpak these environment paths already refer to the sandbox's data area.
    """
    override = os.environ.get("TRIAEVUM_RENDERER_CACHE_DIR")
    if override:
        directory = Path(override)
        if not directory.is_absolute():
            raise ValueError("TRIAEVUM_RENDERER_CACHE_DIR must be absolute")
        return directory
    if sys.platform == "win32":
        base = os.environ.get("APPDATA")
        if not base:
            raise ValueError("Windows roaming application-data directory is unavailable")
        root = Path(base)
    elif sys.platform.startswith("linux"):
        root = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share")
    else:
        raise ValueError("This host must supply the renderer cache directory")
    if not root.is_absolute():
        raise ValueError("Renderer cache directory must be absolute")
    return root / "oot3d_native_vulkan" / "shader_cache"


def _run(command: list[str], root: Path, stage: Path, cancel: Callable[[], bool],
         report: Callable[[str, str], None], timeout: float = 600) -> int:
    import json
    started = time.monotonic()
    cancel_at = None
    pending = ""
    with (stage / "stdout.log").open("w", encoding="utf-8") as output, \
            (stage / "stderr.log").open("w", encoding="utf-8") as errors, \
            (stage / "stdout.log").open(encoding="utf-8", errors="replace") as progress:
        process = subprocess.Popen(command, cwd=root, env=native_process_environment(),
                                   stdout=output, stderr=errors,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            while process.poll() is None:
                now = time.monotonic()
                if cancel_at is None and cancel():
                    (stage / "cancel").touch()
                    cancel_at = now
                if now - started > timeout or (cancel_at is not None and now - cancel_at > 10):
                    raise TimeoutError("Device pipeline preparation exceeded its time budget")
                if max(output.tell(), errors.tell()) > 16 * 1024 * 1024:
                    raise ValueError("Device pipeline preparation exceeded its diagnostic budget")
                pending += progress.read(65536)
                lines = pending.split("\n")
                pending = lines.pop()
                if len(pending) > 65536:
                    raise ValueError("Invalid pipeline progress record")
                for line in lines:
                    try:
                        value = json.loads(line)
                    except ValueError:
                        continue
                    if isinstance(value, dict) and value.get("event") == "pipeline_progress":
                        report("pipelines", f"Preparing GPU pipelines: {value.get('attempted')}/{value.get('total')}")
                time.sleep(0.05)
            return process.returncode
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()


def prepare_device_pipelines(*, root: Path, data_root: Path, title: dict,
                             pack: Path | None, cache_directory: Path | None = None,
                             report: Callable[[str, str], None] = lambda *_: None,
                             cancel: Callable[[], bool] = lambda: False) -> dict | None:
    contract = title.get("device_pipeline_preparation")
    if contract is None:
        return None
    if not isinstance(contract, dict) or contract.get("format") != FORMAT:
        raise ValueError("Unsupported device pipeline preparation contract")
    if pack is None or not pack.is_file():
        raise ValueError("Device pipeline preparation requires prepared portable shaders")
    helper = checked_file(root, contract["helper"])
    manifest = checked_file(root, contract["manifest"])
    dependencies = [checked_file(root, item) for item in contract.get("dependencies", [])]
    adapter = contract.get("adapter", 0)
    if type(adapter) is not int or not 0 <= adapter < 64:
        raise ValueError("Invalid device preparation adapter")
    cache = (cache_directory or renderer_cache_directory()).resolve()
    receipts = data_root.resolve() / "shader-seeds" / "device-preparation"
    receipts.mkdir(parents=True, exist_ok=True)
    # Never trust yesterday's success for today's GPU/driver. The helper loads
    # only a matching cache and checks every recipe again (fast on a warm cache).
    report("pipelines", "Preparing Vulkan pipelines on this GPU (no game boot)...")
    with tempfile.TemporaryDirectory(prefix=".preparing-", dir=receipts) as temporary:
        stage = Path(temporary)
        command = [str(helper), "--manifest", str(manifest), "--pack", str(pack),
                   "--cache-dir", str(cache), "--report", str(stage / "result.json"),
                   "--cancel-file", str(stage / "cancel"), "--adapter", str(adapter)]
        try:
            status = _run(command, root, stage, cancel, report)
            result = load_json_object(stage / "result.json")
            if result.get("format") != FORMAT:
                raise ValueError("Invalid device preparation receipt")
            complete = (status == 0 and result.get("device_pipeline_prewarm") == "complete"
                        and type(result.get("total")) is int and result["total"] > 0
                        and result.get("prepared") == result["total"] and result.get("failed") == 0
                        and result.get("game_booted") is False)
            if complete:
                actual_cache = cache / CACHE_FILENAME
                if (Path(result.get("cache_path", "")).resolve() != actual_cache.resolve()
                        or not actual_cache.is_file()):
                    raise ValueError("Preparation did not produce the requested device cache")
                result["cache_sha256"] = sha256_file(actual_cache)
            elif result.get("device_pipeline_prewarm") != "cancelled":
                raise ValueError("Device pipeline preparation was incomplete")
        except (OSError, ValueError, TimeoutError) as error:
            # An optional acceleration step must not make a playable install
            # unusable. Corrupt/missing catalog artifacts fail before launching.
            result = {"format": FORMAT, "device_pipeline_prewarm": "failed", "error": str(error)}
            diagnostics = stage / "stderr.log"
            if diagnostics.is_file():
                with diagnostics.open("rb") as stream:
                    stream.seek(max(0, diagnostics.stat().st_size - 4000))
                    result["diagnostics"] = stream.read().decode("utf-8", errors="replace")
        result.update(game_coverage_proven=False, pack_sha256=sha256_file(pack),
                      manifest_sha256=sha256_file(manifest), helper_sha256=sha256_file(helper),
                      dependency_sha256=[sha256_file(path) for path in dependencies])
        atomic_write_json(receipts / "latest.json", result)
        report("pipelines", "GPU pipeline preparation: " + result["device_pipeline_prewarm"])
        return result
