// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Host storage policy and release platforms (`host_layout.py`,
//! `release_platform.py`). Import code consumes these paths, not sandbox
//! rules.

use std::path::{Path, PathBuf};

use forge_base::{ForgeError, Result};
use forge_prepare::paths::resolve;

pub const FLATPAK_ID: &str = "io.github.coccofresco.TriAevum";
const APPIMAGE_PAYLOAD: [&str; 3] = ["usr", "lib", "triaevum"];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HostOs {
    Windows,
    Linux,
    Android,
    MacOs,
}

impl HostOs {
    pub fn current() -> Self {
        if cfg!(target_os = "windows") {
            Self::Windows
        } else if cfg!(target_os = "android") {
            Self::Android
        } else if cfg!(target_os = "macos") {
            Self::MacOs
        } else {
            Self::Linux
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HostLayout {
    pub package: PathBuf,
    pub activation: PathBuf,
    pub use_file_portal: bool,
}

impl HostLayout {
    pub fn data(&self) -> PathBuf {
        self.activation.join("data")
    }
}

/// Where the launch profile and activation journal live: the Windows root
/// holds only executables, README.md and LICENSE, so they go to data/.
pub fn installation_state(installation: &Path, os: HostOs) -> PathBuf {
    if os == HostOs::Windows {
        installation.join("data")
    } else {
        installation.to_path_buf()
    }
}

/// The AppDir owning `package`, or None outside an AppImage. The layout is
/// authoritative; `APPDIR` only corroborates it.
pub fn appimage_root(package: &Path, appdir: &str) -> Option<PathBuf> {
    let mut candidates = Vec::new();
    if !appdir.is_empty() && Path::new(appdir).is_absolute() {
        if let Ok(resolved) = resolve(Path::new(appdir)) {
            candidates.push(resolved);
        }
    }
    let parts: Vec<_> = package.components().collect();
    if parts.len() > APPIMAGE_PAYLOAD.len() {
        let tail = &parts[parts.len() - APPIMAGE_PAYLOAD.len()..];
        if tail.iter().zip(APPIMAGE_PAYLOAD).all(|(part, name)| part.as_os_str() == name) {
            candidates.push(parts[..parts.len() - APPIMAGE_PAYLOAD.len()].iter().collect());
        }
    }
    candidates.into_iter().find(|candidate| candidate.parent().is_some() && package.starts_with(candidate))
}

/// `host_layout.for_package`. `env` reads environment variables.
pub fn for_package(package: &Path, env: &dyn Fn(&str) -> Option<String>, os: HostOs) -> Result<HostLayout> {
    let package = resolve(package)?;
    let get = |name: &str| env(name).unwrap_or_default();
    let plain = HostLayout { package: package.clone(), activation: package.clone(), use_file_portal: false };
    if os != HostOs::Linux {
        return Ok(plain);
    }
    if let Some(appdir) = appimage_root(&package, &get("APPDIR")) {
        let mut data_home = get("XDG_DATA_HOME");
        if data_home.is_empty() || !Path::new(&data_home).is_absolute() {
            let home = get("HOME");
            if home.is_empty() || !Path::new(&home).is_absolute() {
                return Err(ForgeError::invalid("AppImage requires an absolute HOME or XDG_DATA_HOME"));
            }
            data_home = Path::new(&home).join(".local/share").to_string_lossy().into_owned();
        }
        let activation = resolve(&Path::new(&data_home).join("TriAevum"))?;
        if activation.starts_with(&appdir) {
            return Err(ForgeError::invalid("AppImage activation must be outside the read-only package"));
        }
        return Ok(HostLayout { package, activation, use_file_portal: false });
    }
    if get("FLATPAK_ID") != FLATPAK_ID {
        return Ok(plain);
    }
    let data_home = get("XDG_DATA_HOME");
    if data_home.is_empty() || !Path::new(&data_home).is_absolute() {
        return Err(ForgeError::invalid("Flatpak did not provide an absolute XDG_DATA_HOME"));
    }
    let activation = resolve(&Path::new(&data_home).join("TriAevum"))?;
    if activation.starts_with(&package) {
        return Err(ForgeError::invalid("Flatpak activation must be outside the read-only package"));
    }
    Ok(HostLayout { package, activation, use_file_portal: true })
}

pub fn process_env(name: &str) -> Option<String> {
    std::env::var(name).ok()
}

/// A desktop release target (`release_platform.ReleasePlatform`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReleasePlatform {
    pub target: &'static str,
    /// Where a package places the runtime before Forge installs it.
    pub runtime: &'static str,
    pub forge: &'static str,
    pub native_module: &'static str,
    pub title_module: &'static str,
    pub installed_runtime: &'static str,
}

pub const WINDOWS: ReleasePlatform = ReleasePlatform {
    target: "x86_64-pc-windows-msvc",
    runtime: "runtime/TriAevum.exe",
    forge: "TriAevumForge.exe",
    native_module: "forge/oot3d_game_module.dll",
    title_module: "triaevum_title_aot.dll",
    installed_runtime: "TriAevum.exe",
};

pub const LINUX: ReleasePlatform = ReleasePlatform {
    target: "x86_64-unknown-linux-gnu",
    runtime: "TriAevum",
    forge: "TriAevumForge",
    native_module: "forge/oot3d_game_module.so",
    title_module: "triaevum_title_aot.so",
    installed_runtime: "TriAevum",
};

pub fn for_target(target: &str) -> Result<ReleasePlatform> {
    [WINDOWS, LINUX].into_iter().find(|platform| platform.target == target).ok_or_else(|| {
        ForgeError::new(forge_base::ErrorKind::IncompatibleTarget, format!("Unsupported release target: {target}"))
    })
}

/// `release_platform.host_platform()`: the release platform of this process
/// (x86-64 Windows or Linux), with the catalog crate's messages.
pub fn host_platform() -> Result<ReleasePlatform> {
    for_target(forge_catalog::host_platform()?.target)
}

/// The runtime of an installation: where a (new) package put it until Forge
/// places it.
pub fn installed_runtime_path(root: &Path, platform: &ReleasePlatform) -> PathBuf {
    let packaged = root.join(platform.runtime);
    if packaged.is_file() {
        packaged
    } else {
        root.join(platform.installed_runtime)
    }
}

/// Moves a packaged runtime to the installation root, replacing the previous
/// one. The Windows runtime finds its DLLs in runtime/, but its own folder is
/// searched first: DLLs an earlier package left in the root would shadow the
/// new ones, so they are removed. A loaded DLL (Forge itself loads the CRT it
/// finds beside it) cannot be deleted but can be renamed aside (issue #110);
/// later runs remove released `.stale` files.
pub fn place_installed_runtime(root: &Path, platform: &ReleasePlatform) -> Result<PathBuf> {
    if let Ok(entries) = std::fs::read_dir(root) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().into_owned();
            if name.starts_with('.') && name.ends_with(".stale") {
                let _ = std::fs::remove_file(entry.path());
            }
        }
    }
    let packaged = root.join(platform.runtime);
    let installed = root.join(platform.installed_runtime);
    if packaged != installed && packaged.is_file() {
        if let Some(runtime_dir) = packaged.parent() {
            let libraries = std::fs::read_dir(runtime_dir)
                .map_err(|error| ForgeError::io(format!("Cannot list {}", runtime_dir.display()), error))?;
            for library in libraries.flatten() {
                let name = library.file_name();
                if Path::new(&name).extension().is_some_and(|ext| ext.eq_ignore_ascii_case("dll")) {
                    remove_shadowing_library(&root.join(&name))?;
                }
            }
        }
        std::fs::rename(&packaged, &installed)
            .map_err(|error| ForgeError::io(format!("Cannot place {}", installed.display()), error))?;
    }
    Ok(installed)
}

fn remove_shadowing_library(path: &Path) -> Result<()> {
    match std::fs::remove_file(path) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) if error.kind() == std::io::ErrorKind::PermissionDenied => {
            let name = path.file_name().unwrap_or_default().to_string_lossy();
            let aside = path.with_file_name(format!(".{name}.{}.stale", std::process::id()));
            std::fs::rename(path, &aside)
                .map_err(|error| ForgeError::io(format!("Cannot replace {}", path.display()), error))
        }
        Err(error) => Err(ForgeError::io(format!("Cannot remove {}", path.display()), error)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn env_of(pairs: &'static [(&'static str, &'static str)]) -> impl Fn(&str) -> Option<String> {
        move |name| pairs.iter().find(|(key, _)| *key == name).map(|(_, value)| value.to_string())
    }

    #[test]
    fn windows_and_plain_linux_activate_in_the_package() {
        let directory = tempfile::tempdir().unwrap();
        for os in [HostOs::Windows, HostOs::Linux] {
            let layout = for_package(directory.path(), &env_of(&[]), os).unwrap();
            assert_eq!(layout.activation, layout.package);
            assert!(!layout.use_file_portal);
        }
        assert_eq!(installation_state(Path::new("x"), HostOs::Windows), Path::new("x").join("data"));
        assert_eq!(installation_state(Path::new("x"), HostOs::Linux), Path::new("x"));
    }

    #[cfg(unix)]
    #[test]
    fn appimage_and_flatpak_activate_outside_the_package() {
        let layout =
            for_package(Path::new("/tmp/.mount_x/usr/lib/triaevum"), &env_of(&[("HOME", "/home/u")]), HostOs::Linux)
                .unwrap();
        assert_eq!(layout.activation, Path::new("/home/u/.local/share/TriAevum"));
        let flatpak = for_package(
            Path::new("/app/lib/triaevum"),
            &env_of(&[("FLATPAK_ID", FLATPAK_ID), ("XDG_DATA_HOME", "/home/u/.var/app/x/data")]),
            HostOs::Linux,
        )
        .unwrap();
        assert!(flatpak.use_file_portal);
        assert!(
            for_package(Path::new("/app/lib/triaevum"), &env_of(&[("FLATPAK_ID", FLATPAK_ID)]), HostOs::Linux).is_err()
        );
    }

    #[test]
    fn places_the_runtime_and_removes_shadowing_libraries() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        std::fs::create_dir(root.join("runtime")).unwrap();
        std::fs::write(root.join(WINDOWS.runtime), b"new runtime").unwrap();
        std::fs::write(root.join("runtime/SDL2.dll"), b"new").unwrap();
        std::fs::write(root.join("TriAevum.exe"), b"old runtime").unwrap();
        std::fs::write(root.join("SDL2.dll"), b"old").unwrap();
        std::fs::write(root.join("unrelated.dll"), b"user").unwrap();
        std::fs::write(root.join(".vcruntime140.dll.1.stale"), b"released").unwrap();
        assert_eq!(installed_runtime_path(root, &WINDOWS), root.join(WINDOWS.runtime));
        assert_eq!(place_installed_runtime(root, &WINDOWS).unwrap(), root.join("TriAevum.exe"));
        assert_eq!(std::fs::read(root.join("TriAevum.exe")).unwrap(), b"new runtime");
        assert!(!root.join("SDL2.dll").exists());
        assert!(!root.join(".vcruntime140.dll.1.stale").exists());
        assert!(root.join("unrelated.dll").is_file());
        assert_eq!(installed_runtime_path(root, &WINDOWS), root.join("TriAevum.exe"));
    }
}
