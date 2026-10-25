// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `release_platform.py`: the execution targets a precompiled catalog can
//! bind. The target is always explicit here; [`host_platform`] is only one
//! way to choose it, so a desktop can prepare data for another target.

use std::path::{Path, PathBuf};

use forge_base::{ErrorKind, ForgeError, Result};
use serde_json::{Map, Value};

use crate::pyjson::py_str;

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
    installed_runtime: "TriAevum.exe",
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
    installed_runtime: "TriAevum",
};

/// The targets a catalog may name. Android/macOS catalogs do not exist yet;
/// adding one is a format decision, not something to infer here.
pub const TARGETS: [ReleasePlatform; 2] = [WINDOWS, LINUX];

pub fn for_target(target: &str) -> Result<ReleasePlatform> {
    TARGETS
        .into_iter()
        .find(|item| item.target == target)
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

/// `host_platform()`: the platform of the running process, x86-64 only.
pub fn host_platform() -> Result<ReleasePlatform> {
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
}
