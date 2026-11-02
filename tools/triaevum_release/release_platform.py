"""Names and ABI targets at the release/installation platform boundary."""

from dataclasses import dataclass
import os
from pathlib import Path
import platform
import sys


@dataclass(frozen=True)
class ReleasePlatform:
    target: str
    # The runtime executable's path in the package (runtime) and in an
    # installation (installed_runtime): the Windows package keeps it in runtime/
    # with its DLLs and Forge moves it to the root when it installs a title, so
    # Forge is the only application in the package root.
    runtime: str
    forge: str
    native_module: str
    title_module: str
    profile: str
    compiler: str
    archiver: str
    support_library: str
    archive_suffix: str
    installed_runtime: str


WINDOWS = ReleasePlatform("x86_64-pc-windows-msvc", "runtime/TriAevum.exe",
                          "TriAevumForge.exe", "forge/oot3d_game_module.dll",
                          "triaevum_title_aot.dll", "x86_64-windows-thinlto-release-v1",
                          "clang-cl.exe", "llvm-lib.exe", "triaevum_title_whole_aot_support.lib", ".zip",
                          "TriAevum.exe")
LINUX = ReleasePlatform("x86_64-unknown-linux-gnu", "TriAevum",
                        "TriAevumForge", "forge/oot3d_game_module.so",
                        "triaevum_title_aot.so", "x86_64-linux-thinlto-release-v1",
                        "clang++", "llvm-ar", "libtriaevum_title_whole_aot_support.a", ".tar.gz",
                        "TriAevum")
# Catalog paths are relative to TriAevum.app/Contents/Resources. The signed
# executables remain there; only user data is activated outside the bundle.
MACOS = ReleasePlatform("aarch64-apple-darwin", "TriAevum",
                        "TriAevumForge", "forge/oot3d_game_module.dylib",
                        "triaevum_title_aot.dylib", "aarch64-macos-release-v1",
                        "clang++", "llvm-ar", "libtriaevum_title_whole_aot_support.a", ".zip",
                        "TriAevum")
# The APK: Android installs the libraries in the application's native library
# folder, never in the Forge package; Forge resolves lib/arm64-v8a/ records there.
ANDROID = ReleasePlatform("aarch64-linux-android", "lib/arm64-v8a/libTriAevum.so",
                          "lib/arm64-v8a/libtriaevum_forge.so", "",
                          "libtriaevum_title_aot.so", "aarch64-android-release-v1",
                          "clang++", "llvm-ar", "libtriaevum_title_whole_aot_support.a", ".apk",
                          "lib/arm64-v8a/libTriAevum.so")


def installed_runtime_path(root: Path, value: ReleasePlatform) -> Path:
    """The runtime of an installation: where a (new) package put it until Forge places it."""
    packaged = root / value.runtime
    return packaged if packaged.is_file() else root / value.installed_runtime


def _remove_shadowing_library(path: Path) -> None:
    try:
        path.unlink(missing_ok=True)
    except PermissionError:
        # A loaded DLL cannot be deleted but can be renamed. Forge itself loads
        # the CRT a previous package left in its folder (issue #110).
        os.replace(path, path.with_name(f".{path.name}.{os.getpid()}.stale"))


def place_installed_runtime(root: Path, value: ReleasePlatform) -> Path:
    """Move a packaged runtime to the installation root, replacing the previous one.

    The Windows runtime finds its DLLs in runtime/, but its own folder is searched
    first: DLLs a previous package left in the root would shadow the new ones.
    """
    if value == MACOS:
        return root / value.runtime
    for stale in root.glob(".*.stale"):
        try:
            stale.unlink()
        except OSError:
            pass  # Still loaded by another Forge process; a later run removes it.
    packaged, installed = root / value.runtime, root / value.installed_runtime
    if packaged != installed and packaged.is_file():
        for library in packaged.parent.glob("*.dll"):
            _remove_shadowing_library(root / library.name)
        os.replace(packaged, installed)
    return installed


def is_windows(value: ReleasePlatform) -> bool:
    return value == WINDOWS


def for_target(target: str) -> ReleasePlatform:
    for item in (WINDOWS, LINUX, MACOS):
        if target == item.target:
            return item
    raise ValueError(f"Unsupported release target: {target}")


def host_platform() -> ReleasePlatform:
    machine = platform.machine().lower()
    if sys.platform == "darwin" and machine in ("arm64", "aarch64"):
        return MACOS
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
