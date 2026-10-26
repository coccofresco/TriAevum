// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Kernel-owned locks (`cache_lock.py`): process termination cannot leave an
//! active lock behind, and Python and Rust Forges exclude each other.
//!
//! Python locks byte 0 with `msvcrt.locking` (Windows `LockFile`) or `flock`
//! (Unix). `File::try_lock` takes an exclusive `LockFileEx` over the whole
//! file or an `flock`, which conflicts with both.

use std::fs::{File, OpenOptions, TryLockError};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use forge_base::{ErrorKind, ForgeError, Operation, Reason, Result};

/// Held until dropped. The file is never unlinked: another process may
/// already have opened the same inode.
#[derive(Debug)]
pub struct KernelLock {
    _file: File,
    pub path: PathBuf,
}

pub fn acquire(path: &Path) -> Result<KernelLock> {
    let mut options = OpenOptions::new();
    options.read(true).write(true).create(true).truncate(false);
    #[cfg(unix)]
    std::os::unix::fs::OpenOptionsExt::mode(&mut options, 0o600);
    let mut file =
        options.open(path).map_err(|error| ForgeError::io(format!("Cannot open lock {}", path.display()), error))?;
    let mut prefix = [0u8; 4];
    let read = file.read(&mut prefix).map_err(|error| {
        // Windows locks are mandatory: a held lock fails the read itself.
        if cfg!(windows) && error.raw_os_error() == Some(33) {
            busy(path)
        } else {
            ForgeError::io("Cannot read the lock", error)
        }
    })?;
    if &prefix[..read] == b"pid=" {
        return Err(ForgeError::invalid(format!(
            "Legacy build lock exists; close the old Forge before removing it: {}",
            path.display()
        ))
        .during(Operation::LockInspection)
        .because(Reason::LegacyLock));
    }
    let empty = file.metadata().map_err(|error| ForgeError::io("Cannot inspect the lock", error))?.len() == 0;
    if empty {
        // Python locks byte 0, which must exist for msvcrt.locking.
        file.write_all(b"\0").map_err(|error| ForgeError::io("Cannot initialize the lock", error))?;
    }
    file.seek(SeekFrom::Start(0)).map_err(|error| ForgeError::io("Cannot rewind the lock", error))?;
    match file.try_lock() {
        Ok(()) => Ok(KernelLock { _file: file, path: path.to_path_buf() }),
        Err(TryLockError::WouldBlock) => Err(busy(path)),
        Err(TryLockError::Error(error)) => Err(ForgeError::io(format!("Cannot lock {}", path.display()), error)),
    }
}

/// Whether another holder has the lock at `path`, without creating or
/// writing it: installation inspection must not change the installation.
pub fn is_held(path: &Path) -> bool {
    let Ok(file) = File::open(path) else { return false };
    matches!(file.try_lock(), Err(TryLockError::WouldBlock))
}

fn busy(path: &Path) -> ForgeError {
    ForgeError::new(
        ErrorKind::Busy,
        format!("Another Forge or the game is using this installation: {}", path.display()),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn excludes_a_second_holder_until_released() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join(".forge-activation.lock");
        let first = acquire(&path).unwrap();
        assert_eq!(acquire(&path).unwrap_err().kind, ErrorKind::Busy);
        drop(first);
        assert!(acquire(&path).is_ok());
        assert_eq!(std::fs::read(&path).unwrap(), b"\0");
        let held = acquire(&path).unwrap();
        assert!(is_held(&path));
        drop(held);
        assert!(!is_held(&path));
        assert!(!is_held(&directory.path().join("absent.lock")));
    }

    #[test]
    fn refuses_a_legacy_pid_lock() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("lock");
        std::fs::write(&path, b"pid=123").unwrap();
        let error = acquire(&path).unwrap_err();
        assert!(error.message.starts_with("Legacy build lock exists"));
        assert_eq!(error.reason, Some(Reason::LegacyLock));
    }
}
