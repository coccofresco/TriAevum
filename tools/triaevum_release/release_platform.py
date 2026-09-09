"""Names and ABI targets at the release/installation platform boundary."""

from dataclasses import dataclass
import platform
import sys


@dataclass(frozen=True)
class ReleasePlatform:
    target: str
    runtime: str
    forge: str
    native_module: str
    title_module: str
    profile: str
    compiler: str
    archiver: str
    support_library: str


WINDOWS = ReleasePlatform("x86_64-pc-windows-msvc", "TriAevum.exe",
                          "TriAevumForge.exe", "forge/oot3d_game_module.dll",
                          "triaevum_title_aot.dll", "x86_64-windows-thinlto-release-v1",
                          "clang-cl.exe", "llvm-lib.exe", "triaevum_title_whole_aot_support.lib")
LINUX = ReleasePlatform("x86_64-unknown-linux-gnu", "TriAevum",
                        "TriAevumForge", "forge/oot3d_game_module.so",
                        "triaevum_title_aot.so", "x86_64-linux-thinlto-release-v1",
                        "clang++", "llvm-ar", "libtriaevum_title_whole_aot_support.a")


def is_windows(value: ReleasePlatform) -> bool:
    return value == WINDOWS


def for_target(target: str) -> ReleasePlatform:
    for item in (WINDOWS, LINUX):
        if target == item.target:
            return item
    raise ValueError(f"Unsupported release target: {target}")


def host_platform() -> ReleasePlatform:
    machine = platform.machine().lower()
    if machine not in ("amd64", "x86_64"):
        raise ValueError(f"This release requires an x86-64 host, not {machine}")
    if sys.platform == "win32":
        return WINDOWS
    if sys.platform.startswith("linux"):
        return LINUX
    raise ValueError(f"Unsupported release platform: {sys.platform}")


def catalog_platform(catalog: dict) -> ReleasePlatform:
    # Released Windows v1 catalogs predate the explicit platform field.
    return for_target(catalog.get("target", WINDOWS.target))
