// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `common.py` equivalents: durable atomic writes, JSON object loading and
//! relative-path normalization, plus file replacement that survives the
//! transient locks of Windows (issue #110).

use std::io::{self, Write};
use std::path::Path;

use serde_json::{Map, Value};
use tempfile::NamedTempFile;

use crate::error::{ErrorKind, ForgeError, Result};
use crate::legacy_json;

/// Writes `payload` to a short hidden temporary file beside `path`, flushes
/// it to disk and renames it over `path`. The temporary name stays shorter
/// than content-addressed destinations near the Win32 path limit (issue 53).
pub fn atomic_write_bytes(path: &Path, payload: &[u8]) -> Result<()> {
    let parent = path.parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or(Path::new("."));
    std::fs::create_dir_all(parent)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    let mut temporary = tempfile::Builder::new()
        .prefix(".")
        .suffix(".tmp")
        .tempfile_in(parent)
        .map_err(|error| ForgeError::io(format!("Cannot write in {}", parent.display()), error))?;
    temporary
        .write_all(payload)
        .and_then(|()| temporary.as_file().sync_all())
        .map_err(|error| ForgeError::io(format!("Cannot write {}", path.display()), error))?;
    persist(temporary, path)
}

/// Closes a written temporary file and renames it over `target` with
/// [`replace_file`]; the temporary is removed when that fails.
pub fn persist(temporary: NamedTempFile, target: &Path) -> Result<()> {
    let temporary = temporary.into_temp_path();
    replace_file(&temporary, target).map_err(|error| file_error("Cannot replace", target, error))?;
    let _ = temporary.keep();
    Ok(())
}

// Windows refuses to replace or delete a file that is read-only (some archive
// tools extract files that way) or that another program holds open: the game,
// a second Forge, or an antivirus scanning a file that was just written. The
// helpers below clear the read-only attribute and retry for about 1.5 s
// before giving up. Python's os.replace/os.unlink failed at once with a bare
// "[WinError 5]"; this is a deliberate difference.
const RETRY_DELAYS_MS: [u64; 6] = [25, 50, 100, 200, 400, 800];

/// Renames `source` over the file `target`, replacing it.
pub fn replace_file(source: &Path, target: &Path) -> io::Result<()> {
    retrying(target, true, || std::fs::rename(source, target))
}

/// Deletes `path`, read-only or briefly locked; a missing file is not an error.
pub fn remove_file(path: &Path) -> io::Result<()> {
    remove(path, true)
}

/// [`remove_file`] without waiting: for leftovers another process may keep
/// loaded for a long time (a later run removes them).
pub fn try_remove_file(path: &Path) -> io::Result<()> {
    remove(path, false)
}

fn remove(path: &Path, wait: bool) -> io::Result<()> {
    match retrying(path, wait, || std::fs::remove_file(path)) {
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
        outcome => outcome,
    }
}

/// Access denied, or the file opened by another program without sharing.
pub fn is_locked(error: &io::Error) -> bool {
    const ERROR_SHARING_VIOLATION: i32 = 32;
    const ERROR_LOCK_VIOLATION: i32 = 33;
    error.kind() == io::ErrorKind::PermissionDenied
        || (cfg!(windows) && matches!(error.raw_os_error(), Some(ERROR_SHARING_VIOLATION | ERROR_LOCK_VIOLATION)))
}

/// Runs `operation`; on Windows a locked `target` is made writable once and
/// then retried with backoff while `wait`. Elsewhere a refusal is final.
fn retrying(target: &Path, wait: bool, mut operation: impl FnMut() -> io::Result<()>) -> io::Result<()> {
    let mut delays = RETRY_DELAYS_MS.iter();
    let mut cleared = false;
    loop {
        let error = match operation() {
            Ok(()) => return Ok(()),
            Err(error) if cfg!(windows) && is_locked(&error) => error,
            Err(error) => return Err(error),
        };
        if !cleared {
            cleared = true;
            if clear_readonly(target) {
                continue;
            }
        }
        match delays.next() {
            Some(delay) if wait => std::thread::sleep(std::time::Duration::from_millis(*delay)),
            _ => return Err(error),
        }
    }
}

/// Clears the read-only attribute of an existing file; true when it was set.
fn clear_readonly(path: &Path) -> bool {
    let Ok(metadata) = std::fs::symlink_metadata(path) else { return false };
    let mut permissions = metadata.permissions();
    if !metadata.is_file() || !permissions.readonly() {
        return false;
    }
    // Only reached on Windows, where this clears the attribute alone.
    #[allow(clippy::permissions_set_readonly_false)]
    permissions.set_readonly(false);
    std::fs::set_permissions(path, permissions).is_ok()
}

/// The error for a file Forge could not replace or delete. A locked file
/// names the programs holding it when Windows can tell, and says what to do,
/// instead of a bare "Access is denied".
pub fn file_error(action: &str, path: &Path, error: io::Error) -> ForgeError {
    let locked = is_locked(&error);
    let mut forge_error = ForgeError::io(format!("{action} {}", path.display()), error).with_path(path);
    if locked {
        let holders = crate::file_holders::processes_using(path);
        let (reason, advice) = if holders.is_empty() {
            (
                "the file is read-only or in use by another program".to_string(),
                "Close TriAevum and any program using the installation folder (an antivirus scan can hold new \
                 files briefly), then try again.",
            )
        } else {
            forge_error.reason = Some(crate::error::Reason::VerifiedFileHolder);
            (format!("the file is in use by {}", holders.join(", ")), "Close it, then try again.")
        };
        let code = forge_error.os_code.map(|code| format!(" (os error {code})")).unwrap_or_default();
        forge_error.kind = ErrorKind::PermissionDenied;
        forge_error.message = format!("{action} {}: {reason}{code}. {advice}", path.display());
    }
    forge_error
}

/// `common.atomic_write_json`: pretty sorted ASCII JSON plus LF.
pub fn atomic_write_json(path: &Path, value: &Value) -> Result<()> {
    atomic_write_bytes(path, &legacy_json::pretty_file_bytes(value))
}

/// `common.load_json_object`: UTF-8 with optional BOM, top level an object.
pub fn load_json_object(path: &Path) -> Result<Map<String, Value>> {
    let bytes =
        std::fs::read(path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    legacy_json::load_object(&bytes)
        .map_err(|error| ForgeError::invalid(format!("{} does not contain a JSON object: {error}", path.display())))
}

/// `common.normalize_relative_path`: backslashes become slashes; empty,
/// absolute and drive-qualified paths and `..` components are rejected.
/// Like `PurePosixPath`, empty and `.` components collapse (`a//b/` is
/// `a/b`, and `.` stays `.`).
pub fn normalize_relative_path(value: &str) -> Result<String> {
    let normalized = value.replace('\\', "/");
    let parts: Vec<&str> = normalized.split('/').filter(|part| !part.is_empty() && *part != ".").collect();
    if normalized.is_empty() || normalized.starts_with('/') || normalized.contains(':') || parts.contains(&"..") {
        return Err(ForgeError::invalid(format!("unsafe relative path: {value:?}")));
    }
    Ok(if parts.is_empty() { ".".to_string() } else { parts.join("/") })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn relative_paths() {
        assert_eq!(normalize_relative_path("a\\b/c.bin").unwrap(), "a/b/c.bin");
        assert_eq!(normalize_relative_path("./a//b/").unwrap(), "a/b");
        assert_eq!(normalize_relative_path(".").unwrap(), ".");
        for bad in ["", "/a", "\\a", "C:/a", "a/../b", "a\\..\\b"] {
            assert!(normalize_relative_path(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn atomic_json_round_trip() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("deep/x.json");
        atomic_write_json(&path, &serde_json::json!({"b": 1, "a": "é"})).unwrap();
        assert_eq!(std::fs::read(&path).unwrap(), b"{\n  \"a\": \"\\u00e9\",\n  \"b\": 1\n}\n");
        assert_eq!(load_json_object(&path).unwrap()["b"], 1);
        assert_eq!(std::fs::read_dir(path.parent().unwrap()).unwrap().count(), 1);
    }

    fn read_only(path: &Path, contents: &[u8]) {
        std::fs::write(path, contents).unwrap();
        let mut permissions = std::fs::metadata(path).unwrap().permissions();
        permissions.set_readonly(true);
        std::fs::set_permissions(path, permissions).unwrap();
    }

    #[test]
    fn read_only_targets_are_replaced_and_removed() {
        let directory = tempfile::tempdir().unwrap();
        let (source, target) = (directory.path().join("new.exe"), directory.path().join("TriAevum.exe"));
        std::fs::write(&source, b"new").unwrap();
        read_only(&target, b"old");
        replace_file(&source, &target).unwrap();
        assert_eq!(std::fs::read(&target).unwrap(), b"new");
        assert!(!source.exists());

        read_only(&target, b"old");
        atomic_write_bytes(&target, b"written").unwrap();
        assert_eq!(std::fs::read(&target).unwrap(), b"written");

        read_only(&target, b"old");
        remove_file(&target).unwrap();
        assert!(!target.exists());
        remove_file(&target).unwrap();
        read_only(&target, b"old");
        try_remove_file(&target).unwrap();
        assert!(!target.exists());
        assert_eq!(std::fs::read_dir(directory.path()).unwrap().count(), 0);
    }

    #[test]
    fn other_failures_name_the_file_without_the_lock_advice() {
        let directory = tempfile::tempdir().unwrap();
        let missing = directory.path().join("missing");
        let error = replace_file(&missing, &directory.path().join("target")).unwrap_err();
        let forge_error = file_error("Cannot place", &missing, error);
        assert!(forge_error.message.starts_with(&format!("Cannot place {}: ", missing.display())));
        assert!(!forge_error.message.contains("try again"));
        assert_eq!(forge_error.paths, vec![missing]);
    }

    /// Holds `path` open without sharing, as a running program or a virus
    /// scanner does, until the returned sender is dropped or fired.
    #[cfg(windows)]
    fn hold(path: &Path) -> (std::sync::mpsc::Sender<()>, std::thread::JoinHandle<()>) {
        use std::os::windows::fs::OpenOptionsExt;
        let file = std::fs::OpenOptions::new().read(true).share_mode(0).open(path).unwrap();
        let (release, released) = std::sync::mpsc::channel::<()>();
        let holder = std::thread::spawn(move || {
            let _ = released.recv();
            drop(file);
        });
        (release, holder)
    }

    #[cfg(windows)]
    #[test]
    fn a_briefly_locked_target_is_replaced_once_released() {
        let directory = tempfile::tempdir().unwrap();
        let (source, target) = (directory.path().join("new"), directory.path().join("vcruntime140.dll"));
        std::fs::write(&source, b"new").unwrap();
        std::fs::write(&target, b"old").unwrap();
        let (release, holder) = hold(&target);
        let releaser = std::thread::spawn(move || {
            std::thread::sleep(std::time::Duration::from_millis(150));
            release.send(()).unwrap();
        });
        replace_file(&source, &target).unwrap();
        assert_eq!(std::fs::read(&target).unwrap(), b"new");
        releaser.join().unwrap();
        holder.join().unwrap();
    }

    #[cfg(windows)]
    #[test]
    fn a_held_target_is_reported_with_the_program_holding_it() {
        let directory = tempfile::tempdir().unwrap();
        let target = directory.path().join("TriAevum.exe");
        read_only(&target, b"old");
        let (release, holder) = hold(&target);
        let error = remove_file(&target).unwrap_err();
        assert!(is_locked(&error), "{error}");
        let forge_error = file_error("Cannot replace", &target, error);
        release.send(()).unwrap();
        holder.join().unwrap();
        assert_eq!(forge_error.kind, ErrorKind::PermissionDenied);
        let message = &forge_error.message;
        assert!(
            message.starts_with(&format!("Cannot replace {}: the file is in use by ", target.display())),
            "{message}"
        );
        assert!(message.contains(&format!("(process {})", std::process::id())), "{message}");
        assert!(message.ends_with("Close it, then try again."), "{message}");
        // The read-only attribute was cleared on the first refusal; once
        // released, the file goes.
        remove_file(&target).unwrap();
    }
}
