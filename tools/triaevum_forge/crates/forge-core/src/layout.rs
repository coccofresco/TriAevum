// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Host storage policy and release platforms (`host_layout.py`,
//! `release_platform.py`). Import code consumes these paths, not sandbox
//! rules.

use std::path::{Path, PathBuf};

use forge_base::fsutil::{self, file_error};
use forge_base::{ForgeError, Result};
use forge_prepare::paths::resolve;

pub const FLATPAK_ID: &str = forge_catalog::title::ACTIVE.package_identity;
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
    if os == HostOs::MacOs {
        return macos_layout(package, &get);
    }
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
        let activation = resolve(&Path::new(&data_home).join(forge_catalog::title::ACTIVE.data_namespace))?;
        if activation.starts_with(&appdir) {
            return Err(ForgeError::invalid("AppImage activation must be outside the read-only package")
                .because(forge_base::Reason::ReadOnlyDestination));
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
    let activation = resolve(&Path::new(&data_home).join(forge_catalog::title::ACTIVE.data_namespace))?;
    if activation.starts_with(&package) {
        return Err(ForgeError::invalid("Flatpak activation must be outside the read-only package")
            .because(forge_base::Reason::ReadOnlyDestination));
    }
    Ok(HostLayout { package, activation, use_file_portal: true })
}

/// Environment override of the macOS data root (an absolute folder), used by
/// tests and developer runs that must not write to ~/Library.
pub const MACOS_DATA_ROOT_ENV: &str = "TRIAEVUM_MACOS_DATA_ROOT";

/// The `.app` bundle containing `package`, if any. A bundle is signed and may
/// run translocated from a read-only path, so nothing is written inside it.
pub fn macos_bundle_root(package: &Path) -> Option<PathBuf> {
    package
        .ancestors()
        .find(|ancestor| ancestor.extension().is_some_and(|extension| extension == "app"))
        .map(Path::to_path_buf)
}

/// macOS: a package inside `TriAevum.app` activates in
/// `~/Library/Application Support/TriAevum` (or [`MACOS_DATA_ROOT_ENV`]); a
/// plain developer folder activates in place, like a portable Linux package.
fn macos_layout(package: PathBuf, get: &dyn Fn(&str) -> String) -> Result<HostLayout> {
    let Some(bundle) = macos_bundle_root(&package) else {
        return Ok(HostLayout { package: package.clone(), activation: package, use_file_portal: false });
    };
    let explicit = get(MACOS_DATA_ROOT_ENV);
    let activation = if !explicit.is_empty() {
        if !Path::new(&explicit).is_absolute() {
            return Err(ForgeError::invalid("TRIAEVUM_MACOS_DATA_ROOT must be an absolute folder"));
        }
        PathBuf::from(explicit)
    } else {
        let home = get("HOME");
        if home.is_empty() || !Path::new(&home).is_absolute() {
            return Err(ForgeError::invalid("the macOS app requires an absolute HOME"));
        }
        Path::new(&home).join("Library/Application Support").join(forge_catalog::title::ACTIVE.data_namespace)
    };
    let activation = resolve(&activation)?;
    if activation.starts_with(&bundle) {
        return Err(ForgeError::invalid("macOS activation must be outside the signed app bundle")
            .because(forge_base::Reason::ReadOnlyDestination));
    }
    Ok(HostLayout { package, activation, use_file_portal: false })
}

pub fn process_env(name: &str) -> Option<String> {
    std::env::var(name).ok()
}

/// The folder of a package that holds the command-line tool
/// (`forge/triaevum-forge[.exe]`, release_policy.json `forge_command_line`).
pub const COMMAND_LINE_FOLDER: &str = "forge";

/// The package of a Forge executable: its own folder (TriAevumForge sits in the
/// package root), or the folder above for the command-line tool in `forge/`.
pub fn package_root_of(executable: &Path) -> PathBuf {
    let directory = executable.parent().map(Path::to_path_buf).unwrap_or_default();
    match (directory.file_name(), directory.parent()) {
        (Some(name), Some(parent)) if name == COMMAND_LINE_FOLDER => parent.to_path_buf(),
        _ => directory,
    }
}

/// [`package_root_of`] this process's executable.
pub fn executable_package_root() -> Result<PathBuf> {
    let executable =
        std::env::current_exe().map_err(|error| ForgeError::io("Cannot locate the Forge executable", error))?;
    Ok(package_root_of(&executable))
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
    // The product name, a title fact (forge-catalog platform.rs).
    installed_runtime: forge_catalog::platform::WINDOWS_PRODUCT_EXECUTABLE,
};

pub const LINUX: ReleasePlatform = ReleasePlatform {
    target: "x86_64-unknown-linux-gnu",
    runtime: "TriAevum",
    forge: "TriAevumForge",
    native_module: "forge/oot3d_game_module.so",
    title_module: "triaevum_title_aot.so",
    // The product name, a title fact (forge-catalog platform.rs).
    installed_runtime: forge_catalog::platform::LINUX_PRODUCT_EXECUTABLE,
};

/// The APK: its libraries stay where Android installed them.
pub const ANDROID: ReleasePlatform = ReleasePlatform {
    target: "aarch64-linux-android",
    runtime: "lib/arm64-v8a/libTriAevum.so",
    forge: "lib/arm64-v8a/libtriaevum_forge.so",
    native_module: "",
    title_module: "libtriaevum_title_aot.so",
    installed_runtime: "lib/arm64-v8a/libTriAevum.so",
};

pub const MACOS: ReleasePlatform = ReleasePlatform {
    target: "aarch64-apple-darwin",
    runtime: "TriAevum",
    forge: "TriAevumForge",
    native_module: "forge/oot3d_game_module.dylib",
    title_module: "triaevum_title_aot.dylib",
    installed_runtime: "TriAevum",
};

pub fn for_target(target: &str) -> Result<ReleasePlatform> {
    [WINDOWS, LINUX, ANDROID, MACOS].into_iter().find(|platform| platform.target == target).ok_or_else(|| {
        ForgeError::new(forge_base::ErrorKind::IncompatibleTarget, format!("Unsupported release target: {target}"))
    })
}

/// `release_platform.host_platform()`: the release platform of this process
/// (x86-64 Windows or Linux, ARM64 Android), with the catalog crate's messages.
pub fn host_platform() -> Result<ReleasePlatform> {
    for_target(forge_catalog::host_platform()?.target)
}

/// The runtime of an installation: where a (new) package put it until Forge
/// places it; for a Windows installation made before the product names, its
/// root `TriAevum.exe` until then (`forge_catalog::platform::installed_runtime_path`).
pub fn installed_runtime_path(root: &Path, platform: &ReleasePlatform) -> PathBuf {
    let packaged = root.join(platform.runtime);
    if packaged.is_file() {
        return packaged;
    }
    let installed = root.join(platform.installed_runtime);
    let legacy = root.join(forge_catalog::platform::LEGACY_WINDOWS_RUNTIME);
    if *platform == WINDOWS && !installed.is_file() && legacy.is_file() {
        return legacy;
    }
    installed
}

/// Refuses to install while the game runs from this installation: Windows
/// cannot replace a running executable, and the failure would otherwise
/// come only after the long shader preparation (issue #110). Only Windows
/// can name the processes; elsewhere this always passes.
pub fn ensure_runtime_closed(root: &Path, platform: &ReleasePlatform) -> Result<()> {
    let mut runtimes = vec![root.join(platform.installed_runtime), root.join(platform.runtime)];
    if *platform == WINDOWS {
        // An installation made before the product names runs its old root runtime.
        runtimes.push(root.join(forge_catalog::platform::LEGACY_WINDOWS_RUNTIME));
    }
    for runtime in runtimes {
        let holders = forge_base::file_holders::processes_using(&runtime);
        if !holders.is_empty() {
            let message = format!(
                "TriAevum is running from this installation ({} is in use by {}). Close the game, then install again.",
                runtime.display(),
                holders.join(", ")
            );
            return Err(ForgeError::new(forge_base::ErrorKind::Busy, message)
                .with_path(runtime)
                .during(forge_base::Operation::PackageReplace)
                .because(forge_base::Reason::VerifiedFileHolder));
        }
    }
    Ok(())
}

/// Moves a packaged runtime to the installation root, replacing the previous
/// one. The Windows runtime finds its DLLs in runtime/, but its own folder is
/// searched first: DLLs an earlier package left in the root would shadow the
/// new ones, so they are removed. A loaded DLL (Forge itself loads the CRT it
/// finds beside it) cannot be deleted but can be renamed aside (issue #110);
/// later runs remove released `.stale` files. Read-only files are replaced,
/// briefly locked ones retried, and a file still held names its holder.
pub fn place_installed_runtime(root: &Path, platform: &ReleasePlatform) -> Result<PathBuf> {
    if *platform == MACOS || (*platform == LINUX && sealed_linux_payload(root)) {
        return Ok(root.join(platform.runtime));
    }
    if let Ok(entries) = std::fs::read_dir(root) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().into_owned();
            if name.starts_with('.') && name.ends_with(".stale") {
                let _ = fsutil::try_remove_file(&entry.path());
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
        fsutil::replace_file(&packaged, &installed).map_err(|error| file_error("Cannot place", &installed, error))?;
        // An installation made before the product names keeps no second,
        // stale game executable under the old name.
        let legacy = root.join(forge_catalog::platform::LEGACY_WINDOWS_RUNTIME);
        if *platform == WINDOWS && legacy != installed && legacy.is_file() {
            fsutil::remove_file(&legacy).map_err(|error| file_error("Cannot remove", &legacy, error))?;
        }
    }
    Ok(installed)
}

/// The read-only payload of an AppImage (`<AppDir>/usr/lib/triaevum`) or a
/// Flatpak (`/app/lib/triaevum`): like the signed macOS bundle, it keeps the
/// build output `TriAevum`; only a portable (writable) Linux installation
/// renames it to the product name. The desktop entry names the product.
pub fn sealed_linux_payload(root: &Path) -> bool {
    let parts: Vec<_> = root.components().collect();
    let tail = parts.len() >= APPIMAGE_PAYLOAD.len()
        && parts[parts.len() - APPIMAGE_PAYLOAD.len()..]
            .iter()
            .zip(APPIMAGE_PAYLOAD)
            .all(|(part, name)| part.as_os_str() == name);
    tail || root == Path::new("/app/lib/triaevum")
}

/// A loaded library is renamed aside at once rather than waited for.
fn remove_shadowing_library(path: &Path) -> Result<()> {
    match fsutil::try_remove_file(path) {
        Ok(()) => Ok(()),
        Err(error) if fsutil::is_locked(&error) => {
            let name = path.file_name().unwrap_or_default().to_string_lossy();
            let aside = path.with_file_name(format!(".{name}.{}.stale", std::process::id()));
            fsutil::replace_file(path, &aside).map_err(|error| file_error("Cannot replace", path, error))
        }
        Err(error) => Err(file_error("Cannot remove", path, error)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn env_of(pairs: &'static [(&'static str, &'static str)]) -> impl Fn(&str) -> Option<String> {
        move |name| pairs.iter().find(|(key, _)| *key == name).map(|(_, value)| value.to_string())
    }

    #[test]
    fn macos_runtime_placement_does_not_touch_the_signed_package() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        let stale = root.join(".sealed.1.stale");
        std::fs::write(&stale, b"sealed").unwrap();
        assert_eq!(place_installed_runtime(root, &MACOS).unwrap(), root.join(MACOS.runtime));
        assert_eq!(std::fs::read(stale).unwrap(), b"sealed");
        assert_eq!(for_target(MACOS.target).unwrap(), MACOS);
    }

    #[test]
    fn command_line_in_forge_folder_belongs_to_the_package_above() {
        let package = Path::new("pkg");
        assert_eq!(package_root_of(&package.join("forge").join("triaevum-forge.exe")), package);
        assert_eq!(package_root_of(&package.join("TriAevumForge.exe")), package);
        assert_eq!(
            package_root_of(&Path::new("target").join("release").join("triaevum-forge")),
            Path::new("target/release")
        );
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
        // resolve(): /home is a firmlink to /System/Volumes/Data/home on macOS.
        // The title's data namespace (TriAevum, or TriAevum-MM3D with title-mm3d).
        let namespace = forge_catalog::title::ACTIVE.data_namespace;
        assert_eq!(layout.activation, resolve(&Path::new("/home/u/.local/share").join(namespace)).unwrap());
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

    #[cfg(unix)]
    #[test]
    fn macos_app_activates_in_application_support_or_the_override() {
        let package = Path::new("/Applications/TriAevum.app/Contents/Resources");
        assert_eq!(macos_bundle_root(package), Some(PathBuf::from("/Applications/TriAevum.app")));
        let layout = for_package(package, &env_of(&[("HOME", "/Users/u")]), HostOs::MacOs).unwrap();
        let namespace = forge_catalog::title::ACTIVE.data_namespace;
        assert_eq!(layout.activation, Path::new("/Users/u/Library/Application Support").join(namespace));
        assert!(!layout.use_file_portal);
        let redirected = for_package(
            package,
            &env_of(&[("HOME", "/Users/u"), (MACOS_DATA_ROOT_ENV, "/Users/u/dev/data-root")]),
            HostOs::MacOs,
        )
        .unwrap();
        assert_eq!(redirected.activation, Path::new("/Users/u/dev/data-root"));
        assert!(for_package(package, &env_of(&[(MACOS_DATA_ROOT_ENV, "relative")]), HostOs::MacOs).is_err());
        assert!(for_package(package, &env_of(&[]), HostOs::MacOs).is_err());
        assert!(for_package(
            package,
            &env_of(&[(MACOS_DATA_ROOT_ENV, "/Applications/TriAevum.app/Contents/data")]),
            HostOs::MacOs
        )
        .is_err());
        let directory = tempfile::tempdir().unwrap();
        let plain = for_package(directory.path(), &env_of(&[]), HostOs::MacOs).unwrap();
        assert_eq!(plain.activation, plain.package);
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
        let product = root.join(WINDOWS.installed_runtime);
        assert_eq!(place_installed_runtime(root, &WINDOWS).unwrap(), product);
        assert_eq!(std::fs::read(&product).unwrap(), b"new runtime");
        // The product name replaces the old root runtime (owner decision 2026-11-08).
        assert!(!root.join("TriAevum.exe").exists(), "the legacy TriAevum.exe is removed");
        assert!(!root.join("SDL2.dll").exists());
        assert!(!root.join(".vcruntime140.dll.1.stale").exists());
        assert!(root.join("unrelated.dll").is_file());
        assert_eq!(installed_runtime_path(root, &WINDOWS), product);
    }

    fn read_only(path: &Path, contents: &[u8]) {
        std::fs::write(path, contents).unwrap();
        let mut permissions = std::fs::metadata(path).unwrap().permissions();
        permissions.set_readonly(true);
        std::fs::set_permissions(path, permissions).unwrap();
    }

    #[test]
    fn linux_installs_the_product_name_except_in_a_sealed_payload() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        std::fs::write(root.join(LINUX.runtime), b"linux runtime").unwrap();
        let installed = place_installed_runtime(root, &LINUX).unwrap();
        assert_eq!(installed, root.join(forge_catalog::platform::LINUX_PRODUCT_EXECUTABLE));
        assert_ne!(LINUX.installed_runtime, LINUX.runtime);
        assert!(!root.join(LINUX.runtime).exists());
        assert_eq!(std::fs::read(&installed).unwrap(), b"linux runtime");
        assert_eq!(installed_runtime_path(root, &LINUX), installed);
        // Negative: AppImage and Flatpak payloads keep the build output name.
        let payload = root.join("AppDir/usr/lib/triaevum");
        std::fs::create_dir_all(&payload).unwrap();
        std::fs::write(payload.join(LINUX.runtime), b"sealed").unwrap();
        assert_eq!(place_installed_runtime(&payload, &LINUX).unwrap(), payload.join(LINUX.runtime));
        assert!(payload.join(LINUX.runtime).is_file());
        assert!(sealed_linux_payload(Path::new("/app/lib/triaevum")));
        assert!(!sealed_linux_payload(root));
    }

    #[test]
    fn read_only_files_of_an_earlier_package_are_replaced() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        std::fs::create_dir(root.join("runtime")).unwrap();
        read_only(&root.join(WINDOWS.runtime), b"new runtime");
        std::fs::write(root.join("runtime/vcruntime140.dll"), b"new").unwrap();
        read_only(&root.join("TriAevum.exe"), b"old runtime");
        read_only(&root.join("vcruntime140.dll"), b"old");
        read_only(&root.join(".msvcp140.dll.1.stale"), b"released");
        ensure_runtime_closed(root, &WINDOWS).unwrap();
        let product = root.join(WINDOWS.installed_runtime);
        assert_eq!(place_installed_runtime(root, &WINDOWS).unwrap(), product);
        assert_eq!(std::fs::read(&product).unwrap(), b"new runtime");
        let mut names: Vec<String> = std::fs::read_dir(root)
            .unwrap()
            .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
            .collect();
        names.sort();
        assert_eq!(names, [WINDOWS.installed_runtime, "runtime"]);
    }

    /// A copy of the command interpreter stands in for the running game.
    /// (Rust's POSIX-semantics rename could even replace its executable,
    /// which Python's os.replace could not; the game must still be closed.)
    #[cfg(windows)]
    #[test]
    fn a_running_game_is_named_before_the_install_starts() {
        use std::process::{Command, Stdio};

        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        std::fs::create_dir(root.join("runtime")).unwrap();
        std::fs::write(root.join(WINDOWS.runtime), b"new runtime").unwrap();
        let shell = std::env::var_os("ComSpec").map(PathBuf::from).expect("ComSpec names cmd.exe");
        // The game of an earlier package still runs under the legacy name.
        let installed = root.join("TriAevum.exe");
        std::fs::copy(&shell, &installed).unwrap();
        let mut game = Command::new(&installed)
            .arg("/k")
            .stdin(Stdio::piped())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let running = format!("(process {})", game.id());
        let closed = ensure_runtime_closed(root, &WINDOWS).unwrap_err();
        game.kill().unwrap();
        game.wait().unwrap();
        assert_eq!(closed.kind, forge_base::ErrorKind::Busy);
        assert_eq!(closed.paths, std::slice::from_ref(&installed));
        let prefix = format!("TriAevum is running from this installation ({} is in use by ", installed.display());
        assert!(closed.message.starts_with(&prefix), "{}", closed.message);
        assert!(closed.message.contains(&running), "{}", closed.message);
        assert!(closed.message.ends_with("Close the game, then install again."), "{}", closed.message);

        ensure_runtime_closed(root, &WINDOWS).unwrap();
        place_installed_runtime(root, &WINDOWS).unwrap();
        assert_eq!(std::fs::read(root.join(WINDOWS.installed_runtime)).unwrap(), b"new runtime");
        assert!(!installed.exists());
    }
}
