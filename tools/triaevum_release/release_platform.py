"""Names and ABI targets at the release/installation platform boundary."""

from dataclasses import dataclass, replace
import json
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


TITLES_ROOT = Path(__file__).resolve().parents[2] / "titles"
# The root runtime of Windows installations made before the product names.
LEGACY_WINDOWS_RUNTIME = "TriAevum.exe"
LEGACY_LAUNCH_PROFILE = "TriAevum.launch.json"


def product_executable(title: str, titles_root: Path = TITLES_ROOT, *, os_name: str = "windows") -> str:
    """The installed game executable of `title` on Windows or Linux: the
    product name (owner decisions 2026-11-08), a title fact in
    titles/<title>/product.json (windows_executable, linux_executable)."""
    if os_name not in ("windows", "linux"):
        raise ValueError(f"No product executable fact for {os_name}")
    facts = json.loads((titles_root / title / "product.json").read_text(encoding="utf-8"))
    name = facts.get(f"{os_name}_executable")
    windows = os_name == "windows"
    if (facts.get("title") != title or not isinstance(name, str) or not name or "/" in name or "\\" in name
            or name.endswith(".exe") != windows or name.startswith(".")):
        raise ValueError(f"Invalid product facts for {title}")
    return name


# The Linux package keeps the build output TriAevum at its root; installing
# renames it in place to the product name ("Echoes_of_Time", for_title for MM3D).
# The package keeps the build output runtime/TriAevum.exe; installing renames it
# to the product name ("Echoes_of_Time.exe" for OoT3D, the Python Forge's title;
# the MM3D Forge is the Rust one built with title-mm3d: for_title("mm3d")).
WINDOWS = ReleasePlatform("x86_64-pc-windows-msvc", "runtime/TriAevum.exe",
                          "TriAevumForge.exe", "forge/oot3d_game_module.dll",
                          "triaevum_title_aot.dll", "x86_64-windows-thinlto-release-v1",
                          "clang-cl.exe", "llvm-lib.exe", "triaevum_title_whole_aot_support.lib", ".zip",
                          "Echoes_of_Time.exe")
# The same Windows product built with llvm-mingw (Itanium C++ ABI, libc++,
# UCRT) on the Linux build machine: a platform of its own (XBUILD Q2), so that
# modules and title caches of the MSVC-ABI product are never mixed with it.
# Its title module and support library come from GNU-style Clang (linux_title).
WINDOWS_GNU = replace(WINDOWS, target="x86_64-w64-windows-gnu",
                      profile="x86_64-windows-gnu-release-v1", compiler="clang++",
                      archiver="llvm-ar", support_library="libtriaevum_title_whole_aot_support.a")
LINUX = ReleasePlatform("x86_64-unknown-linux-gnu", "TriAevum",
                        "TriAevumForge", "forge/oot3d_game_module.so",
                        "triaevum_title_aot.so", "x86_64-linux-thinlto-release-v1",
                        "clang++", "llvm-ar", "libtriaevum_title_whole_aot_support.a", ".tar.gz",
                        "Echoes_of_Time")
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


def for_title(title: str, value: ReleasePlatform = None) -> ReleasePlatform:
    """The Windows or Linux platform of `title`'s product (its installed executable name)."""
    value = WINDOWS if value is None else value
    if is_windows(value):
        return replace(value, installed_runtime=product_executable(title))
    if value.target == LINUX.target:
        return replace(value, installed_runtime=product_executable(title, os_name="linux"))
    return value


def launch_profile_name(value: ReleasePlatform) -> str:
    """The profile the runtime finds by itself: its executable's stem + .launch.json."""
    name = value.installed_runtime.rsplit("/", 1)[-1]
    return (name[:-4] if name.endswith(".exe") else name) + ".launch.json"


def installed_runtime_path(root: Path, value: ReleasePlatform) -> Path:
    """The runtime of an installation: where a (new) package put it until Forge places it.

    A Windows installation made before the product names (0.8.0-beta.1.2 and
    earlier) runs its root TriAevum.exe until then (R-A G6-F2).
    """
    packaged = root / value.runtime
    if packaged.is_file():
        return packaged
    installed, legacy = root / value.installed_runtime, root / LEGACY_WINDOWS_RUNTIME
    if is_windows(value) and not installed.is_file() and legacy.is_file():
        return legacy
    return installed


def _remove_shadowing_library(path: Path) -> None:
    try:
        path.unlink(missing_ok=True)
    except PermissionError:
        # A loaded DLL cannot be deleted but can be renamed. Forge itself loads
        # the CRT a previous package left in its folder (issue #110).
        os.replace(path, path.with_name(f".{path.name}.{os.getpid()}.stale"))


def sealed_linux_payload(root: Path) -> bool:
    """An AppImage (<AppDir>/usr/lib/triaevum) or Flatpak (/app/lib/triaevum)
    payload: read-only, so like the macOS bundle it keeps the build output
    TriAevum; only a portable Linux installation takes the product name."""
    return Path(root).parts[-3:] == ("usr", "lib", "triaevum") or Path(root).as_posix() == "/app/lib/triaevum"


def place_installed_runtime(root: Path, value: ReleasePlatform) -> Path:
    """Move a packaged runtime to the installation root, replacing the previous one.

    The Windows runtime finds its DLLs in runtime/, but its own folder is searched
    first: DLLs a previous package left in the root would shadow the new ones.
    """
    if value == MACOS or (value.target == LINUX.target and sealed_linux_payload(root)):
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
        # No second, stale game executable under the pre-product name.
        legacy = root / LEGACY_WINDOWS_RUNTIME
        if is_windows(value) and legacy != installed and legacy.is_file():
            legacy.unlink()
    return installed


WINDOWS_TARGETS = ("x86_64-pc-windows-msvc", "x86_64-w64-windows-gnu")


def is_windows(value: "ReleasePlatform | str") -> bool:
    """Any Windows product (MSVC-ABI or llvm-mingw); takes a platform or a target."""
    return (value if isinstance(value, str) else value.target) in WINDOWS_TARGETS


def for_target(target: str) -> ReleasePlatform:
    for item in (WINDOWS, WINDOWS_GNU, LINUX, MACOS):
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
