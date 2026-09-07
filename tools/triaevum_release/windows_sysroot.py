"""Verified, explicit MSVC/Windows/Clang headers and libraries for Forge.

This describes privately acquired SDK files, not permission to redistribute them.
"""

from __future__ import annotations

import hashlib
import json
import os
from dataclasses import dataclass
from pathlib import Path

try:
    from .common import load_json_object, normalize_relative_path, sha256_file
except ImportError:
    from common import load_json_object, normalize_relative_path, sha256_file


@dataclass(frozen=True)
class WindowsSysroot:
    root: Path
    sdk_version: str
    identity: str
    msvc_compatibility_version: str

    def arguments(self) -> tuple[str, ...]:
        return ("/vctoolsdir", str(self.root / "msvc"),
                "/winsdkdir", str(self.root / "sdk"),
                "/winsdkversion", self.sdk_version,
                "-resource-dir=" + str(self.root / "clang"),
                "-fms-compatibility-version=" + self.msvc_compatibility_version)


def build_environment() -> dict[str, str]:
    # Flags or search paths inherited from a developer shell must not override
    # a verified sysroot or silently change object-cache semantics.
    removed = {"CL", "_CL_", "LINK", "_LINK_", "INCLUDE", "LIB", "LIBPATH",
               "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "CCC_OVERRIDE_OPTIONS",
               "VCTOOLSINSTALLDIR", "VCINSTALLDIR", "WINDOWSSDKDIR",
               "WINDOWSSDKVERSION", "UNIVERSALCRTSDKDIR", "UCRTVERSION"}
    return {key: value for key, value in os.environ.items() if key.upper() not in removed}


def load_sysroot(root: Path) -> WindowsSysroot:
    root = root.resolve(strict=True)
    manifest = load_json_object(root / "sysroot.json")
    if manifest.get("format") != "triaevum_windows_sysroot_v1":
        raise ValueError("Unsupported Forge Windows sysroot manifest")
    version = manifest.get("sdk_version")
    if not isinstance(version, str) or not version or any(
        not part.isdigit() for part in version.split(".")
    ):
        raise ValueError("Invalid Windows SDK version")
    records = manifest.get("files")
    msvc = str(manifest.get("msvc_version", "")).split(".")
    if len(msvc) != 3 or msvc[0] != "14" or not all(part.isdigit() for part in msvc):
        raise ValueError("Unsupported MSVC toolset version")
    if not isinstance(records, dict) or not records:
        raise ValueError("Empty Forge Windows sysroot inventory")
    for relative, descriptor in records.items():
        name = normalize_relative_path(relative)
        path = root / name
        if (not path.resolve().is_relative_to(root) or path.is_symlink()
                or not path.is_file() or not isinstance(descriptor, dict)
                or path.stat().st_size != descriptor.get("bytes")
                or sha256_file(path) != descriptor.get("sha256")):
            raise ValueError(f"Forge sysroot file is missing or changed: {name}")
    actual = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}
    if actual != set(records) | {"sysroot.json"}:
        raise ValueError("Forge sysroot contains unindexed files")
    windows_names = {name.casefold() for name in records}
    if len(windows_names) != len(records):
        raise ValueError("Ambiguous Windows sysroot filenames")
    for required in ("msvc/include/vector", "msvc/lib/x64/libcmt.lib",
                     f"sdk/Include/{version}/ucrt/stdio.h",
                     f"sdk/Lib/{version}/ucrt/x64/libucrt.lib",
                     f"sdk/Lib/{version}/um/x64/kernel32.lib",
                     "clang/include/stddef.h"):
        if required.casefold() not in windows_names:
            raise ValueError(f"Incomplete Forge sysroot: {required}")
    identity = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    # MSVC v14.xx implements the compiler compatibility contract v19.xx.
    # Explicit /vctoolsdir alone cannot infer this from our neutral folder name.
    return WindowsSysroot(root, version, identity, "19." + msvc[1])


def compiler_sysroot(compiler: Path, explicit: Path | None = None,
                     *, verified: WindowsSysroot | None = None) -> WindowsSysroot | None:
    root = explicit if explicit is not None else compiler.resolve().parent / "sysroot"
    # Share only within one build operation; never persist this lease or skip
    # verification on a subsequent build. Toolchain files must remain immutable.
    if verified is not None:
        if root.resolve() != verified.root:
            raise ValueError("Verified sysroot does not match the requested toolchain")
        return verified
    if explicit is None and not root.exists():
        return None
    return load_sysroot(root)
