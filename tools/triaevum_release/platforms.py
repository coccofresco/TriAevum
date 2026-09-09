"""Per-platform artifact names shared by Forge, the publisher and the audit.

End-user code selects ``host()``. Publisher and audit code must not assume the
host: a Linux CI runner audits Windows packages, so they resolve the platform
from the package itself (``for_runtime``/``for_triple``).
"""

from __future__ import annotations

import sys
from dataclasses import dataclass


@dataclass(frozen=True)
class Platform:
    name: str
    runtime: str
    forge: str
    plugin: str
    native_module: str
    triple: str
    profile: str
    compiler: str
    archiver: str
    support_library: str
    shaderc_runtime: str | None

    @property
    def native_module_path(self) -> str:
        return f"forge/{self.native_module}"


WINDOWS = Platform(
    name="windows-x64",
    runtime="TriAevum.exe",
    forge="TriAevumForge.exe",
    plugin="triaevum_title_aot.dll",
    native_module="oot3d_game_module.dll",
    triple="x86_64-pc-windows-msvc",
    profile="x86_64-windows-thinlto-release-v1",
    compiler="clang-cl.exe",
    archiver="llvm-lib.exe",
    support_library="triaevum_title_whole_aot_support.lib",
    shaderc_runtime="shaderc_shared.dll",
)

LINUX = Platform(
    name="linux-x64",
    runtime="TriAevum",
    forge="TriAevumForge",
    plugin="triaevum_title_aot.so",
    native_module="oot3d_game_module.so",
    triple="x86_64-unknown-linux-gnu",
    profile="x86_64-linux-thinlto-release-v1",
    compiler="clang++",
    archiver="llvm-ar",
    support_library="libtriaevum_title_whole_aot_support.a",
    shaderc_runtime=None,
)

PLATFORMS = {platform.name: platform for platform in (WINDOWS, LINUX)}


def host() -> Platform:
    if sys.platform == "win32":
        return WINDOWS
    if sys.platform.startswith("linux"):
        return LINUX
    raise ValueError(f"TriAevum has no product definition for host platform {sys.platform}")


def for_triple(triple: str) -> Platform:
    for platform in PLATFORMS.values():
        if platform.triple == triple:
            return platform
    raise ValueError(f"Unsupported title target: {triple}")


def for_runtime(runtime: str) -> Platform:
    for platform in PLATFORMS.values():
        if platform.runtime == runtime:
            return platform
    raise ValueError(f"Unsupported runtime executable: {runtime}")


def is_windows(platform: Platform) -> bool:
    return platform is WINDOWS
