// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `release_platform.py`: the execution targets a precompiled catalog can
//! bind. The target is always explicit here; [`host_platform`] is only one
//! way to choose it, so a desktop can prepare data for another target.

use std::path::{Path, PathBuf};

use forge_base::{ErrorKind, ForgeError, Result};
use serde_json::{Map, Value};

use crate::pyjson::py_str;

/// The installed Windows game executable takes the PRODUCT name (owner
/// decision 2026-11-08): a title fact, `windows_executable` of
/// `titles/<title>/product.json`. The Forge is built per title, so the MM3D
/// Forge (feature `title-mm3d`) installs "Moonfall_Curse.exe" and the OoT3D
/// Forge "Echoes_of_Time.exe". The package keeps the build output
/// `runtime/TriAevum.exe`; Forge's install step renames it.
#[cfg(not(feature = "title-mm3d"))]
pub const WINDOWS_PRODUCT_EXECUTABLE: &str = "Echoes_of_Time.exe";
#[cfg(feature = "title-mm3d")]
pub const WINDOWS_PRODUCT_EXECUTABLE: &str = "Moonfall_Curse.exe";
/// The installed Linux game binary takes the product name too (owner
/// decision 2026-11-08, Linux names): `linux_executable` of
/// `titles/<title>/product.json`. The Linux package keeps the build output
/// `TriAevum` at its root; Forge's install step renames it in place.
#[cfg(not(feature = "title-mm3d"))]
pub const LINUX_PRODUCT_EXECUTABLE: &str = "Echoes_of_Time";
#[cfg(feature = "title-mm3d")]
pub const LINUX_PRODUCT_EXECUTABLE: &str = "Moonfall_Curse";
/// The root runtime of Windows installations made before the product names:
/// removed when Forge places the renamed one.
pub const LEGACY_WINDOWS_RUNTIME: &str = "TriAevum.exe";
/// The launch profile name of those installations.
pub const LEGACY_LAUNCH_PROFILE: &str = "TriAevum.launch.json";

/// The launch profile the runtime finds by itself when started without
/// arguments: its executable's stem with `.launch.json`
/// (`DefaultNativeGameLaunchProfilePath`), so it follows the installed name.
pub fn launch_profile_name(installed_runtime: &str) -> String {
    let name = installed_runtime.rsplit('/').next().unwrap_or(installed_runtime);
    let stem = name.strip_suffix(".exe").unwrap_or(name);
    format!("{stem}.launch.json")
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReleasePlatform {
    pub target: &'static str,
    /// The runtime's path in the package; Forge moves it to `installed_runtime`.
    pub runtime: &'static str,
    pub forge: &'static str,
    pub native_module: &'static str,
    pub title_module: &'static str,
    pub profile: &'static str,
    pub compiler: &'static str,
    pub archiver: &'static str,
    pub support_library: &'static str,
    pub archive_suffix: &'static str,
    pub installed_runtime: &'static str,
}

pub const WINDOWS: ReleasePlatform = ReleasePlatform {
    target: "x86_64-pc-windows-msvc",
    runtime: "runtime/TriAevum.exe",
    forge: "TriAevumForge.exe",
    native_module: "forge/oot3d_game_module.dll",
    title_module: "triaevum_title_aot.dll",
    profile: "x86_64-windows-thinlto-release-v1",
    compiler: "clang-cl.exe",
    archiver: "llvm-lib.exe",
    support_library: "triaevum_title_whole_aot_support.lib",
    archive_suffix: ".zip",
    installed_runtime: WINDOWS_PRODUCT_EXECUTABLE,
};

pub const LINUX: ReleasePlatform = ReleasePlatform {
    target: "x86_64-unknown-linux-gnu",
    runtime: "TriAevum",
    forge: "TriAevumForge",
    native_module: "forge/oot3d_game_module.so",
    title_module: "triaevum_title_aot.so",
    profile: "x86_64-linux-thinlto-release-v1",
    compiler: "clang++",
    archiver: "llvm-ar",
    support_library: "libtriaevum_title_whole_aot_support.a",
    archive_suffix: ".tar.gz",
    installed_runtime: LINUX_PRODUCT_EXECUTABLE,
};

/// The APK: Android installs its libraries in the application's native
/// library directory (`lib/arm64-v8a/` in the APK, the paths its catalog
/// records, see [`crate::precompiled::set_native_library_directory`]). The
/// runtime library is also the pass compiler and the device helper; there is
/// no separate native module and no installed copy of the runtime.
pub const ANDROID: ReleasePlatform = ReleasePlatform {
    target: "aarch64-linux-android",
    runtime: "lib/arm64-v8a/libTriAevum.so",
    forge: "lib/arm64-v8a/libtriaevum_forge.so",
    native_module: "",
    title_module: "libtriaevum_title_aot.so",
    profile: "aarch64-android-release-v1",
    compiler: "clang++",
    archiver: "llvm-ar",
    support_library: "libtriaevum_title_whole_aot_support.a",
    archive_suffix: ".apk",
    installed_runtime: "lib/arm64-v8a/libTriAevum.so",
};

/// Paths are relative to the signed app's Contents/Resources directory.
pub const MACOS: ReleasePlatform = ReleasePlatform {
    target: "aarch64-apple-darwin",
    runtime: "TriAevum",
    forge: "TriAevumForge",
    native_module: "forge/oot3d_game_module.dylib",
    title_module: "triaevum_title_aot.dylib",
    profile: "aarch64-macos-release-v1",
    compiler: "clang++",
    archiver: "llvm-ar",
    support_library: "libtriaevum_title_whole_aot_support.a",
    archive_suffix: ".zip",
    installed_runtime: "TriAevum",
};

pub const TARGETS: [ReleasePlatform; 4] = [WINDOWS, LINUX, ANDROID, MACOS];

pub fn for_target(target: &str) -> Result<ReleasePlatform> {
    find_target(&TARGETS, target)
}

/// Python's `release_platform.for_target`: the targets the desktop publisher
/// bindings accept. Android catalogs come from the Android package publisher
/// (`android_forge_package.py`), which binds the runtime library itself.
pub fn desktop_target(target: &str) -> Result<ReleasePlatform> {
    find_target(&[WINDOWS, LINUX, MACOS], target)
}

fn find_target(targets: &[ReleasePlatform], target: &str) -> Result<ReleasePlatform> {
    targets
        .iter()
        .find(|item| item.target == target)
        .copied()
        .ok_or_else(|| ForgeError::new(ErrorKind::IncompatibleTarget, format!("Unsupported release target: {target}")))
}

/// `catalog_platform`: released Windows v1 catalogs predate `target`.
pub fn catalog_platform(catalog: &Map<String, Value>) -> Result<ReleasePlatform> {
    match catalog.get("target") {
        None => Ok(WINDOWS),
        Some(Value::String(target)) => for_target(target),
        Some(other) => Err(ForgeError::new(
            ErrorKind::IncompatibleTarget,
            format!("Unsupported release target: {}", py_str(other)),
        )),
    }
}

/// `host_platform()`: the platform of the running process, x86-64 desktops
/// and the ARM64 Android application.
pub fn host_platform() -> Result<ReleasePlatform> {
    if cfg!(all(target_os = "android", target_arch = "aarch64")) {
        return Ok(ANDROID);
    }
    if cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        return Ok(MACOS);
    }
    let machine = std::env::consts::ARCH;
    if machine != "x86_64" {
        return Err(ForgeError::new(
            ErrorKind::IncompatibleTarget,
            format!("This release requires an x86-64 host, not {}", python_machine(machine)),
        ));
    }
    match std::env::consts::OS {
        "windows" => Ok(WINDOWS),
        "linux" => Ok(LINUX),
        other => Err(ForgeError::new(
            ErrorKind::IncompatibleTarget,
            format!("Unsupported release platform: {}", python_sys_platform(other)),
        )),
    }
}

fn python_machine(arch: &str) -> &str {
    // platform.machine() says ARM64 on Windows and aarch64 elsewhere.
    if cfg!(windows) && arch == "aarch64" {
        "arm64"
    } else {
        arch
    }
}

fn python_sys_platform(os: &str) -> &str {
    match os {
        "macos" => "darwin",
        other => other,
    }
}

/// The installation's runtime: in the packaged place until Forge moves it.
pub fn installed_runtime_path(root: &Path, platform: &ReleasePlatform) -> PathBuf {
    let packaged = crate::paths::join_posix(root, platform.runtime);
    if packaged.is_file() {
        packaged
    } else {
        crate::paths::join_posix(root, platform.installed_runtime)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn legacy_windows_and_explicit_linux() {
        assert_eq!(catalog_platform(json!({}).as_object().unwrap()).unwrap(), WINDOWS);
        assert_eq!(catalog_platform(json!({"target": LINUX.target}).as_object().unwrap()).unwrap(), LINUX);
        let error = for_target("aarch64-unknown-linux-gnu").unwrap_err();
        assert_eq!(error.message, "Unsupported release target: aarch64-unknown-linux-gnu");
        let error = catalog_platform(json!({"target": null}).as_object().unwrap()).unwrap_err();
        assert_eq!(error.message, "Unsupported release target: None");
    }

    /// The product name is the title fact of `titles/<title>/product.json`.
    #[test]
    fn windows_executable_is_the_title_product_name() {
        #[cfg(not(feature = "title-mm3d"))]
        let (facts, expected) = (include_str!("../../../../../titles/oot3d/product.json"), "Echoes_of_Time.exe");
        #[cfg(feature = "title-mm3d")]
        let (facts, expected) = (include_str!("../../../../../titles/mm3d/product.json"), "Moonfall_Curse.exe");
        let facts: Value = serde_json::from_str(facts).unwrap();
        assert_eq!(facts["windows_executable"], expected);
        assert_eq!(WINDOWS.installed_runtime, expected);
        assert_eq!(WINDOWS.runtime, "runtime/TriAevum.exe");
        assert_ne!(WINDOWS.installed_runtime, LEGACY_WINDOWS_RUNTIME);
        assert_eq!(launch_profile_name(WINDOWS.installed_runtime), expected.replace(".exe", ".launch.json"));
        // Linux: the product name without an extension (linux_executable);
        // the package keeps the build output TriAevum and the Forge its name.
        let linux = facts["linux_executable"].as_str().unwrap();
        assert_eq!(format!("{linux}.exe"), expected);
        assert_eq!(LINUX.installed_runtime, linux);
        assert_eq!(LINUX.runtime, "TriAevum");
        assert_eq!(LINUX.forge, "TriAevumForge");
        assert_eq!(launch_profile_name(LINUX.installed_runtime), expected.replace(".exe", ".launch.json"));
        assert_ne!(launch_profile_name(LINUX.installed_runtime), LEGACY_LAUNCH_PROFILE);
    }
}
