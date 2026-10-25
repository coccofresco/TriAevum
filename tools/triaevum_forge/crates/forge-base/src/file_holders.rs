// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The programs holding a file open, for locked-file errors (issue #110).
//! Windows answers through the Restart Manager; other systems do not lock
//! files against replacement, so the list is empty there.

use std::path::Path;

/// `name (process ID)` for each process using `path`; empty when unknown.
#[cfg(windows)]
pub fn processes_using(path: &Path) -> Vec<String> {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Foundation::{ERROR_MORE_DATA, ERROR_SUCCESS};
    use windows_sys::Win32::System::RestartManager::{
        RmEndSession, RmGetList, RmRegisterResources, RmStartSession, CCH_RM_SESSION_KEY, RM_PROCESS_INFO,
    };

    let Ok(path) = std::path::absolute(path) else { return Vec::new() };
    let wide: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
    let mut session = 0u32;
    let mut key = [0u16; CCH_RM_SESSION_KEY as usize + 1];
    // SAFETY: Restart Manager calls with live, correctly sized buffers; the
    // session is always ended.
    unsafe {
        if RmStartSession(&mut session, 0, key.as_mut_ptr()) != ERROR_SUCCESS {
            return Vec::new();
        }
        let files = [wide.as_ptr()];
        let mut processes: Vec<RM_PROCESS_INFO> = Vec::new();
        let mut listed = RmRegisterResources(session, 1, files.as_ptr(), 0, std::ptr::null(), 0, std::ptr::null());
        // The holders may change between the two calls: ask again a few times.
        for _ in 0..3 {
            if listed != ERROR_SUCCESS && listed != ERROR_MORE_DATA {
                break;
            }
            let (mut needed, mut count, mut reasons) = (0u32, processes.len() as u32, 0u32);
            listed = RmGetList(session, &mut needed, &mut count, processes.as_mut_ptr(), &mut reasons);
            if listed == ERROR_SUCCESS {
                processes.truncate(count as usize);
                break;
            }
            processes.resize(needed as usize, RM_PROCESS_INFO::default());
        }
        RmEndSession(session);
        if listed != ERROR_SUCCESS {
            return Vec::new();
        }
        processes
            .iter()
            .map(|process| {
                let name = &process.strAppName;
                let length = name.iter().position(|&unit| unit == 0).unwrap_or(name.len());
                format!("{} (process {})", String::from_utf16_lossy(&name[..length]), process.Process.dwProcessId)
            })
            .collect()
    }
}

#[cfg(not(windows))]
pub fn processes_using(_path: &Path) -> Vec<String> {
    Vec::new()
}
