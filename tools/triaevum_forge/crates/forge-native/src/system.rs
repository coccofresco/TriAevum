// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Facts about the host Forge checks before it prepares a title and names
//! in diagnostics: free space on a volume, whether the Vulkan loader the
//! game needs can be found, and the operating system's identity.

use std::path::Path;

/// The operating system as Python's `platform.platform()` spells it
/// (`Windows-11-10.0.26200-SP0`, `Linux-6.8.0-45-generic-x86_64-...`).
pub fn os_identity() -> String {
    crate::pyplatform::platform()
}

/// Bytes available to this user on the volume holding `path` (the nearest
/// existing ancestor when `path` does not exist yet); None when the system
/// does not say.
pub fn available_space(path: &Path) -> Option<u64> {
    let existing = path.ancestors().find(|ancestor| !ancestor.as_os_str().is_empty() && ancestor.exists())?;
    available_space_at(existing)
}

#[cfg(windows)]
fn available_space_at(path: &Path) -> Option<u64> {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Storage::FileSystem::GetDiskFreeSpaceExW;

    let wide: Vec<u16> = path.as_os_str().encode_wide().chain(std::iter::once(0)).collect();
    let mut available = 0u64;
    // SAFETY: a NUL-terminated path and a valid output pointer; the other
    // outputs are optional.
    let ok = unsafe { GetDiskFreeSpaceExW(wide.as_ptr(), &mut available, std::ptr::null_mut(), std::ptr::null_mut()) };
    (ok != 0).then_some(available)
}

#[cfg(unix)]
fn available_space_at(path: &Path) -> Option<u64> {
    use std::os::unix::ffi::OsStrExt;

    let path = std::ffi::CString::new(path.as_os_str().as_bytes()).ok()?;
    // SAFETY: a NUL-terminated path and a zeroed statvfs the call fills.
    unsafe {
        let mut stats: libc::statvfs = std::mem::zeroed();
        if libc::statvfs(path.as_ptr(), &mut stats) != 0 {
            return None;
        }
        #[allow(clippy::unnecessary_cast)]
        Some(stats.f_bavail as u64 * stats.f_frsize as u64)
    }
}

#[cfg(not(any(windows, unix)))]
fn available_space_at(_: &Path) -> Option<u64> {
    None
}

/// The Vulkan loader library the game opens by name on this system.
pub const VULKAN_LOADER: &str = if cfg!(windows) {
    "vulkan-1.dll"
} else if cfg!(target_os = "macos") {
    "libvulkan.1.dylib"
} else {
    "libvulkan.so.1"
};

#[cfg(target_os = "macos")]
fn bundled_vulkan_loader(executable: &Path) -> Option<std::path::PathBuf> {
    executable
        .ancestors()
        .find(|path| path.extension().is_some_and(|extension| extension == "app"))
        .map(|bundle| bundle.join("Contents/Frameworks").join(VULKAN_LOADER))
}

/// Whether the system resolves [`VULKAN_LOADER`] by name, as the game does.
/// Loading it runs no Vulkan code: the library is released at once.
#[cfg(windows)]
pub fn vulkan_loader_present() -> bool {
    use windows_sys::Win32::Foundation::FreeLibrary;
    use windows_sys::Win32::System::LibraryLoader::LoadLibraryW;

    let name: Vec<u16> = VULKAN_LOADER.encode_utf16().chain(std::iter::once(0)).collect();
    // SAFETY: a NUL-terminated library name; the handle is released once.
    unsafe {
        let module = LoadLibraryW(name.as_ptr());
        if module.is_null() {
            return false;
        }
        FreeLibrary(module);
    }
    true
}

#[cfg(unix)]
pub fn vulkan_loader_present() -> bool {
    if cfg!(target_os = "android") {
        // Every Android device the APK installs on has a Vulkan loader.
        return true;
    }
    let name = std::ffi::CString::new(VULKAN_LOADER).expect("library name");
    #[cfg(target_os = "macos")]
    let name = {
        use std::os::unix::ffi::OsStrExt;
        std::env::current_exe()
            .ok()
            .and_then(|executable| bundled_vulkan_loader(&executable))
            .and_then(|path| std::ffi::CString::new(path.as_os_str().as_bytes()).ok())
            .unwrap_or(name)
    };
    // SAFETY: a NUL-terminated library name; the handle is released once.
    unsafe {
        let handle = libc::dlopen(name.as_ptr(), libc::RTLD_LAZY | libc::RTLD_LOCAL);
        if handle.is_null() {
            return false;
        }
        libc::dlclose(handle);
    }
    true
}

#[cfg(not(any(windows, unix)))]
pub fn vulkan_loader_present() -> bool {
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[cfg(target_os = "macos")]
    #[test]
    fn desktop_and_cli_probe_the_bundled_loader() {
        for executable in ["TriAevumForge", "forge/triaevum-forge"] {
            let path = Path::new("/Applications/TriAevum.app/Contents/Resources").join(executable);
            assert_eq!(
                bundled_vulkan_loader(&path).unwrap(),
                Path::new("/Applications/TriAevum.app/Contents/Frameworks/libvulkan.1.dylib")
            );
        }
        assert!(bundled_vulkan_loader(Path::new("/tmp/triaevum-forge")).is_none());
    }

    #[test]
    fn free_space_of_a_missing_path_is_its_volume_s() {
        let directory = tempfile::tempdir().unwrap();
        let existing = available_space(directory.path()).expect("free space");
        let missing = available_space(&directory.path().join("not").join("yet")).expect("free space");
        // Other processes write meanwhile; the same volume answers about the same.
        assert!(existing.abs_diff(missing) < 1 << 30, "{existing} vs {missing}");
        assert!(!os_identity().is_empty());
    }
}
