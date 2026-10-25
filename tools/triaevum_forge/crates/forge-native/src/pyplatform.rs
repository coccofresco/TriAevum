// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `platform.platform()` as Python 3.13 spells it, for the `platform` field
//! of `TriAevum-preflight-error.json`: `Windows-11-10.0.26200-SP0`,
//! `Linux-6.8.0-45-generic-x86_64-with-glibc2.39`. Other hosts get their
//! system name (Python's Darwin form is not modelled).

/// `platform._platform(*parts)`: dash-joined, filename-safe, without
/// "unknown" fields, doubled dashes or a trailing dash.
fn clean(parts: &[&str]) -> String {
    let joined: Vec<&str> = parts.iter().map(|part| part.trim()).filter(|part| !part.is_empty()).collect();
    let mut text = joined.join("-").replace(' ', "_");
    for character in ['/', '\\', ':', ';', '"', '(', ')'] {
        text = text.replace(character, "-");
    }
    text = text.replace("unknown", "");
    while text.contains("--") {
        text = text.replace("--", "-");
    }
    text.trim_end_matches('-').to_string()
}

#[cfg(windows)]
pub fn platform() -> String {
    use windows_sys::Wdk::System::SystemServices::RtlGetVersion;
    use windows_sys::Win32::System::SystemInformation::{OSVERSIONINFOEXW, OSVERSIONINFOW};

    // SAFETY: a zeroed OSVERSIONINFOEXW with its size set is the documented
    // input of RtlGetVersion, which only writes inside it.
    let info = unsafe {
        let mut info: OSVERSIONINFOEXW = std::mem::zeroed();
        info.dwOSVersionInfoSize = std::mem::size_of::<OSVERSIONINFOEXW>() as u32;
        RtlGetVersion((&mut info as *mut OSVERSIONINFOEXW).cast::<OSVERSIONINFOW>());
        info
    };
    let version = (info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
    // platform._WIN32_CLIENT_RELEASES / _WIN32_SERVER_RELEASES.
    let client: &[((u32, u32, u32), &str)] = &[
        ((10, 1, 0), "post11"),
        ((10, 0, 22000), "11"),
        ((6, 4, 0), "10"),
        ((6, 3, 0), "8.1"),
        ((6, 2, 0), "8"),
        ((6, 1, 0), "7"),
        ((6, 0, 0), "Vista"),
    ];
    let server: &[((u32, u32, u32), &str)] = &[
        ((10, 1, 0), "post2025Server"),
        ((10, 0, 26100), "2025Server"),
        ((10, 0, 20348), "2022Server"),
        ((10, 0, 17763), "2019Server"),
        ((6, 4, 0), "2016Server"),
        ((6, 3, 0), "2012ServerR2"),
        ((6, 2, 0), "2012Server"),
        ((6, 1, 0), "2008ServerR2"),
        ((6, 0, 0), "2008Server"),
    ];
    // VER_NT_WORKSTATION.
    let releases = if info.wProductType == 1 { client } else { server };
    let release = releases.iter().find(|(minimum, _)| *minimum <= version).map_or("", |(_, name)| name);
    let csd = if info.wServicePackMinor != 0 {
        format!("SP{}.{}", info.wServicePackMajor, info.wServicePackMinor)
    } else {
        format!("SP{}", info.wServicePackMajor)
    };
    let number = format!("{}.{}.{}", version.0, version.1, version.2);
    clean(&["Windows", release, &number, &csd])
}

#[cfg(target_os = "linux")]
pub fn platform() -> String {
    use std::ffi::CStr;

    fn field(raw: &[libc::c_char]) -> String {
        // SAFETY: uname fills NUL-terminated strings inside each array.
        unsafe { CStr::from_ptr(raw.as_ptr()) }.to_string_lossy().into_owned()
    }
    // SAFETY: uname writes only into the zeroed struct it is given.
    let names = unsafe {
        let mut names: libc::utsname = std::mem::zeroed();
        libc::uname(&mut names);
        names
    };
    let libc_version = libc_version();
    let with = if libc_version.is_empty() { String::new() } else { format!("glibc{libc_version}") };
    clean(&[&field(&names.sysname), &field(&names.release), &field(&names.machine), "with", &with])
}

#[cfg(all(target_os = "linux", target_env = "gnu"))]
fn libc_version() -> String {
    // SAFETY: gnu_get_libc_version returns a static NUL-terminated string.
    unsafe { std::ffi::CStr::from_ptr(libc::gnu_get_libc_version()) }.to_string_lossy().into_owned()
}

#[cfg(all(target_os = "linux", not(target_env = "gnu")))]
fn libc_version() -> String {
    String::new()
}

#[cfg(not(any(windows, target_os = "linux")))]
pub fn platform() -> String {
    clean(&[std::env::consts::OS, std::env::consts::ARCH])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cleanup_matches_python() {
        assert_eq!(
            clean(&["Linux", "6.8 (x)", "x86_64", "unknown", "with", "glibc2.39"]),
            "Linux-6.8_-x-x86_64-with-glibc2.39"
        );
        assert_eq!(clean(&["Windows", "11", "10.0.26200", "SP0"]), "Windows-11-10.0.26200-SP0");
        assert!(!platform().is_empty());
    }
}
