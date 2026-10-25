// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `worker_job.py`: keeps a background installer bound to its GUI owner.
//!
//! [`bind_worker_lifetime`] puts the worker in a Windows kill-on-close Job
//! Object, so killing or losing the worker also kills every descendant
//! helper. [`watch_owner`] exits the worker with status 125 when the owner
//! process ends (Linux pidfd, Windows process handle). Other hosts have no
//! owner watch, as in Python.

use forge_base::{ErrorKind, ForgeError, Result};

/// Exit status of a worker whose owner died.
pub const OWNER_EXIT_STATUS: i32 = 125;

#[cfg(any(windows, target_os = "linux"))]
fn os_failure(error: std::io::Error) -> ForgeError {
    let message = crate::pyerr::os_error(&error, None);
    crate::pyerr::io_failure(error, message)
}

/// `watch_owner(owner_pid)`: a daemon thread that ends this process with
/// [`OWNER_EXIT_STATUS`] once the owner exits. The handle/pidfd tracks the
/// owner instance, so PID reuse cannot keep a detached installer alive.
pub fn watch_owner(owner_pid: i64) -> Result<()> {
    if owner_pid <= 0 || owner_pid == i64::from(std::process::id()) {
        return Err(ForgeError::new(ErrorKind::InvalidInput, "Invalid worker owner PID"));
    }
    platform::watch_owner(owner_pid)
}

/// `bind_worker_lifetime()`: Windows only, once per process. The job handle
/// is never closed: closing it would terminate this process; the OS closes
/// it when the worker exits or dies.
pub fn bind_worker_lifetime() -> Result<()> {
    platform::bind_worker_lifetime()
}

#[cfg(windows)]
mod platform {
    use std::sync::Mutex;

    use windows_sys::Win32::Foundation::{CloseHandle, HANDLE, WAIT_OBJECT_0};
    use windows_sys::Win32::System::JobObjects::{
        AssignProcessToJobObject, CreateJobObjectW, JobObjectExtendedLimitInformation, SetInformationJobObject,
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
    };
    use windows_sys::Win32::System::Threading::{
        GetCurrentProcess, OpenProcess, WaitForSingleObject, INFINITE, PROCESS_SYNCHRONIZE,
    };

    use super::{os_failure, OWNER_EXIT_STATUS};
    use forge_base::Result;

    /// The job handle, kept for the life of the process (`_job` in Python).
    static JOB: Mutex<usize> = Mutex::new(0);

    struct OwnerHandle(HANDLE);
    // SAFETY: a process handle is a kernel object reference usable from any thread.
    unsafe impl Send for OwnerHandle {}

    pub fn watch_owner(owner_pid: i64) -> Result<()> {
        let pid = u32::try_from(owner_pid).map_err(|_| os_failure(std::io::Error::from_raw_os_error(87)))?;
        // SAFETY: plain Win32 call; a null result is checked.
        let handle = unsafe { OpenProcess(PROCESS_SYNCHRONIZE, 0, pid) };
        if handle.is_null() {
            return Err(os_failure(std::io::Error::last_os_error()));
        }
        let owner = OwnerHandle(handle);
        std::thread::Builder::new()
            .name("ForgeOwnerLifetime".into())
            .spawn(move || {
                let owner = owner;
                // SAFETY: the handle stays open until CloseHandle below.
                let result = unsafe { WaitForSingleObject(owner.0, INFINITE) };
                // SAFETY: closes the handle this thread owns.
                unsafe { CloseHandle(owner.0) };
                if result == WAIT_OBJECT_0 {
                    // Ending the worker also closes its job, killing descendants.
                    std::process::exit(OWNER_EXIT_STATUS);
                }
            })
            .map_err(os_failure)?;
        Ok(())
    }

    pub fn bind_worker_lifetime() -> Result<()> {
        let mut job = JOB.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        if *job != 0 {
            return Ok(());
        }
        // SAFETY: Win32 calls with checked results; the limit structure is a
        // zeroed plain-data struct whose size is passed alongside it.
        unsafe {
            let handle = CreateJobObjectW(std::ptr::null(), std::ptr::null());
            if handle.is_null() {
                return Err(os_failure(std::io::Error::last_os_error()));
            }
            let mut limits: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            let size = std::mem::size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32;
            let configured = SetInformationJobObject(
                handle,
                JobObjectExtendedLimitInformation,
                (&limits as *const JOBOBJECT_EXTENDED_LIMIT_INFORMATION).cast(),
                size,
            );
            if configured == 0 || AssignProcessToJobObject(handle, GetCurrentProcess()) == 0 {
                let error = std::io::Error::last_os_error();
                CloseHandle(handle);
                return Err(os_failure(error));
            }
            *job = handle as usize;
        }
        Ok(())
    }
}

#[cfg(target_os = "linux")]
mod platform {
    use super::{os_failure, OWNER_EXIT_STATUS};
    use forge_base::Result;

    pub fn watch_owner(owner_pid: i64) -> Result<()> {
        let pid =
            libc::pid_t::try_from(owner_pid).map_err(|_| os_failure(std::io::Error::from_raw_os_error(libc::ESRCH)))?;
        // SAFETY: pidfd_open takes a pid and flags and returns a descriptor or -1.
        let descriptor = unsafe { libc::syscall(libc::SYS_pidfd_open, pid, 0) };
        if descriptor < 0 {
            return Err(os_failure(std::io::Error::last_os_error()));
        }
        let descriptor = descriptor as libc::c_int;
        std::thread::Builder::new()
            .name("ForgeOwnerLifetime".into())
            .spawn(move || {
                let mut poll = libc::pollfd { fd: descriptor, events: libc::POLLIN, revents: 0 };
                // SAFETY: one valid pollfd; a pidfd becomes readable when its process exits.
                let exited = loop {
                    let ready = unsafe { libc::poll(&mut poll, 1, -1) };
                    if ready > 0 {
                        break true;
                    }
                    if ready < 0 && std::io::Error::last_os_error().kind() != std::io::ErrorKind::Interrupted {
                        break false;
                    }
                };
                // SAFETY: closes the descriptor this thread owns.
                unsafe { libc::close(descriptor) };
                if exited {
                    std::process::exit(OWNER_EXIT_STATUS);
                }
            })
            .map_err(os_failure)?;
        Ok(())
    }

    pub fn bind_worker_lifetime() -> Result<()> {
        Ok(())
    }
}

#[cfg(not(any(windows, target_os = "linux")))]
mod platform {
    use forge_base::Result;

    pub fn watch_owner(_owner_pid: i64) -> Result<()> {
        Ok(())
    }

    pub fn bind_worker_lifetime() -> Result<()> {
        Ok(())
    }
}
