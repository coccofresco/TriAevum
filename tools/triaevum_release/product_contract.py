"""Query the actual runtime and initialize only absent user configuration."""

from __future__ import annotations

import json
import platform
import subprocess
from pathlib import Path
from typing import Any

try:
    from .common import atomic_write_json, load_json_object, sha256_file
    from .native_process import run_native, describe_exit_status
except ImportError:
    from common import atomic_write_json, load_json_object, sha256_file
    from native_process import run_native, describe_exit_status


def validate_product_info(info: dict[str, Any], source_commit: str = "") -> None:
    if (info.get("format") != "triaevum_product_info_v1"
            or info.get("runtime") != "oot3d_native_game"
            or info.get("whole_aot_plugin_abi") != 2):
        raise ValueError("This is not the playable TriAevum ABI-v2 runtime")
    capabilities = info.get("capabilities", {})
    if not isinstance(capabilities, dict) or any(
        capabilities.get(name) is not True for name in ("nri", "f1", "topscreen")
    ):
        raise ValueError("TriAevum is missing NRI, F1 or TopScreen support")
    if source_commit and info.get("source_commit") != source_commit:
        raise ValueError("Runtime source commit does not match the release")
    defaults = info.get("default_config")
    if not isinstance(defaults, dict) or not isinstance(defaults.get("Graphics"), dict):
        raise ValueError("Runtime did not publish its graphics defaults")
    graphics = defaults["Graphics"]
    if (graphics.get("Preset") != "Custom"
            or graphics.get("FrameRate", {}).get("Mode") != "Interpolated2x"):
        raise ValueError("Runtime defaults do not enable visual interpolation x2")


def query_product(executable: Path, source_commit: str = "", *, plugin: Path | None = None) -> dict[str, Any]:
    executable = executable.resolve(strict=True)
    try:
        result = run_native(
            ([str(executable), "--product-info"] if plugin is None else
             [str(executable), "--verify-title-plugin", str(plugin.resolve(strict=True))]),
            cwd=executable.parent,
            capture_output=True, text=True, timeout=15, check=False,
        )
        if result.returncode != 0:
            status = describe_exit_status(result.returncode)
            log = executable.parent / "TriAevum-preflight-error.json"
            report = {
                "format": "triaevum_preflight_error_v1",
                "platform": platform.platform(),
                "runtime_sha256": sha256_file(executable),
                "operation": "verify-title-plugin" if plugin else "product-info",
                "exit_status": f"0x{result.returncode & 0xFFFFFFFF:08X}",
                "description": status,
                "stdout": (result.stdout or "")[-8000:],
                "stderr": (result.stderr or "")[-8000:],
            }
            try:
                atomic_write_json(log, report)
                logging = f"Diagnostic log: {log}. Review personal paths before sharing."
            except OSError as error:
                logging = f"Could not save diagnostic log at {log}: {error}"
            raise ValueError(
                f"Runtime preflight failed ({status}): "
                f"{(result.stderr or result.stdout or '')[-2000:]}\n{logging}"
            )
        info = json.loads(result.stdout)
    except (OSError, subprocess.TimeoutExpired, json.JSONDecodeError) as exc:
        raise ValueError(f"Cannot query the playable runtime: {exc}") from exc
    if not isinstance(info, dict):
        raise ValueError("Runtime product information is not an object")
    validate_product_info(info, source_commit)
    if plugin is not None and info.get("private_title_loaded") is not True:
        raise ValueError("The selected private title failed its native ABI preflight")
    return {"runtime_sha256": sha256_file(executable), "product": info}


def ensure_runtime_config(path: Path, product: dict[str, Any]) -> bool:
    """Keep every existing graphics/profile choice, including legacy settings."""
    validate_product_info(product)
    if path.exists():
        # A malformed user file must not be silently replaced either.
        load_json_object(path)
        return False
    atomic_write_json(path, product["default_config"])
    return True
