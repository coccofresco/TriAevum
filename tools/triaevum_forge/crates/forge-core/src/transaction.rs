// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Recoverable publication (`activation_transaction.py`). Every target is
//! backed up and journaled before the body runs; a failure, a panic or a
//! crash leaves a journal from which this or the Python Forge restores the
//! previous files. The journal and backups have the Python format.

use std::fs::File;
use std::io::Write;
use std::panic::{catch_unwind, resume_unwind, AssertUnwindSafe};
use std::path::{Path, PathBuf};

use forge_base::fsutil::{self, atomic_write_json, file_error, load_json_object};
use forge_base::hashing::{copy_hashing, hex, sha256_file};
use forge_base::{CancelToken, ForgeError, Reason, Result};
use forge_prepare::paths::resolve;
use serde_json::{json, Value};

use crate::layout::{installation_state, HostOs};
use crate::lock::{self, KernelLock};

const JOURNAL_FORMAT: &str = "triaevum_activation_v1";

pub fn journal_path(installation: &Path, os: HostOs) -> PathBuf {
    installation_state(installation, os).join(".forge-activation").join("journal.json")
}

pub fn installation_lock(installation: &Path, os: HostOs) -> Result<KernelLock> {
    let state = installation_state(installation, os);
    std::fs::create_dir_all(&state)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", state.display()), error))?;
    lock::acquire(&state.join(".forge-activation.lock"))
}

fn file_digest(path: &Path) -> Result<String> {
    Ok(hex(&sha256_file(path, &CancelToken::new())?.1))
}

/// Streams `source` into a synced temporary beside `target`, then renames it.
fn copy_synced(source: &Path, target: &Path) -> Result<()> {
    let parent = target.parent().unwrap_or(Path::new("."));
    std::fs::create_dir_all(parent)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    let mut temporary = tempfile::Builder::new()
        .prefix(".activation-")
        .tempfile_in(parent)
        .map_err(|error| ForgeError::io(format!("Cannot write in {}", parent.display()), error))?;
    let mut input = File::open(source).map_err(|error| file_error("Cannot read", source, error))?;
    copy_hashing(&mut input, temporary.as_file_mut(), u64::MAX, &CancelToken::new())?;
    temporary
        .as_file_mut()
        .flush()
        .and_then(|()| temporary.as_file().sync_all())
        .map_err(|error| ForgeError::io(format!("Cannot write {}", target.display()), error))?;
    fsutil::persist(temporary, target)
}

fn path_text(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

/// Validates the whole recovery set before writing anything, then puts each
/// target back and removes the journal.
fn restore(journal: &Path, targets: &[PathBuf]) -> Result<()> {
    let payload = load_json_object(journal)?;
    let entries = match payload.get("entries") {
        Some(Value::Array(entries))
            if payload.get("format").and_then(Value::as_str) == Some(JOURNAL_FORMAT)
                && entries.len() == targets.len() =>
        {
            entries
        }
        _ => {
            return Err(ForgeError::new(
                forge_base::ErrorKind::RecoveryRequired,
                "Invalid Forge activation recovery journal",
            )
            .because(Reason::RollbackFailure))
        }
    };
    let directory = journal.parent().unwrap_or(Path::new("."));
    // A changed target set requires recovery with the original installation.
    for (index, (entry, target)) in entries.iter().zip(targets).enumerate() {
        if entry.get("target").and_then(Value::as_str) != Some(path_text(target).as_str()) {
            return Err(ForgeError::new(
                forge_base::ErrorKind::RecoveryRequired,
                "Pending activation targets differ; retry the original title installation",
            ));
        }
        if let Some(digest) = entry.get("sha256").and_then(Value::as_str) {
            if file_digest(&directory.join(format!("{index}.bak")))? != digest {
                return Err(ForgeError::new(
                    forge_base::ErrorKind::RecoveryRequired,
                    "Forge activation backup is damaged",
                )
                .because(Reason::RollbackFailure));
            }
        }
    }
    for (index, (entry, target)) in entries.iter().zip(targets).enumerate() {
        match entry.get("sha256").and_then(Value::as_str) {
            None => fsutil::remove_file(target).map_err(|error| file_error("Cannot remove", target, error))?,
            Some(digest) => {
                if !target.is_file() || file_digest(target)? != digest {
                    copy_synced(&directory.join(format!("{index}.bak")), target)?;
                }
            }
        }
    }
    std::fs::remove_file(journal).map_err(|error| ForgeError::io("Cannot remove the activation journal", error))
}

fn invalid_journal() -> ForgeError {
    ForgeError::new(forge_base::ErrorKind::RecoveryRequired, "Invalid Forge activation recovery journal")
        .because(Reason::RollbackFailure)
}

/// Repair's recovery of an interrupted activation: puts back the files the
/// journal names, from their verified backups, without knowing the original
/// request. Every journaled target must lie inside one of `roots` (the
/// installation and its data folder). Returns whether a journal was settled.
pub fn recover_interrupted(installation: &Path, os: HostOs, roots: &[PathBuf]) -> Result<bool> {
    let journal = journal_path(installation, os);
    let _lock = installation_lock(installation, os)?;
    if !journal.exists() {
        return Ok(false);
    }
    let payload = load_json_object(&journal).map_err(|_| invalid_journal())?;
    let Some(Value::Array(entries)) = payload.get("entries") else { return Err(invalid_journal()) };
    let roots = roots.iter().map(|root| resolve(root)).collect::<Result<Vec<PathBuf>>>()?;
    let mut targets = Vec::with_capacity(entries.len());
    for entry in entries {
        let target = entry.get("target").and_then(Value::as_str).ok_or_else(invalid_journal)?;
        let target = resolve(Path::new(target))?;
        if !roots.iter().any(|root| target.starts_with(root)) {
            return Err(ForgeError::new(
                forge_base::ErrorKind::RecoveryRequired,
                "Pending activation targets differ; retry the original title installation",
            )
            .with_path(target));
        }
        targets.push(target);
    }
    restore(&journal, &targets)?;
    let directory = journal.parent().expect("journal has a parent");
    for index in 0..targets.len() {
        let _ = std::fs::remove_file(directory.join(format!("{index}.bak")));
    }
    Ok(true)
}

/// Runs `body` with `targets` journaled. The body's failure (or panic) puts
/// every target back; success discards the backups. If the restore itself
/// fails, the journal and backups stay for the next Forge.
pub fn activation_transaction<T>(
    installation: &Path,
    targets: &[PathBuf],
    os: HostOs,
    body: impl FnOnce() -> Result<T>,
) -> Result<T> {
    let targets = targets.iter().map(|target| resolve(target)).collect::<Result<Vec<PathBuf>>>()?;
    for (index, target) in targets.iter().enumerate() {
        if targets[..index].contains(target) {
            return Err(ForgeError::invalid("Duplicate activation target"));
        }
    }
    let journal = journal_path(installation, os);
    let directory = journal.parent().expect("journal has a parent").to_path_buf();
    let _lock = installation_lock(installation, os)?;
    std::fs::create_dir_all(&directory)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", directory.display()), error))?;
    if journal.exists() {
        restore(&journal, &targets)?;
    }
    let mut entries = Vec::with_capacity(targets.len());
    for (index, target) in targets.iter().enumerate() {
        let digest = if target.exists() {
            let backup = directory.join(format!("{index}.bak"));
            // Stream potentially large private plugins; never hold them in RAM.
            copy_synced(target, &backup)?;
            Value::String(file_digest(&backup)?)
        } else {
            Value::Null
        };
        entries.push(json!({"target": path_text(target), "sha256": digest}));
    }
    atomic_write_json(&journal, &json!({"format": JOURNAL_FORMAT, "entries": entries}))?;
    let outcome = catch_unwind(AssertUnwindSafe(body));
    let committed = matches!(outcome, Ok(Ok(_)));
    let restore_result = if committed {
        std::fs::remove_file(&journal).map_err(|error| ForgeError::io("Cannot remove the activation journal", error))
    } else {
        restore(&journal, &targets)
    };
    if !journal.exists() {
        for index in 0..targets.len() {
            let _ = std::fs::remove_file(directory.join(format!("{index}.bak")));
        }
    }
    match outcome {
        Err(panic) => resume_unwind(panic),
        Ok(Err(error)) => Err(error),
        Ok(Ok(value)) => restore_result.map(|()| value),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn setup() -> (tempfile::TempDir, PathBuf, Vec<PathBuf>) {
        let directory = tempfile::tempdir().unwrap();
        let installation = directory.path().to_path_buf();
        let existing = installation.join("data/active-title.json");
        std::fs::create_dir_all(existing.parent().unwrap()).unwrap();
        std::fs::write(&existing, b"old").unwrap();
        let created = installation.join("data/TriAevum.launch.json");
        (directory, installation, vec![existing, created])
    }

    #[test]
    fn failure_restores_every_target() {
        let (_directory, installation, targets) = setup();
        let result: Result<()> = activation_transaction(&installation, &targets, HostOs::Windows, || {
            std::fs::write(&targets[0], b"new").unwrap();
            std::fs::write(&targets[1], b"new").unwrap();
            Err(ForgeError::invalid("fail"))
        });
        assert!(result.is_err());
        assert_eq!(std::fs::read(&targets[0]).unwrap(), b"old");
        assert!(!targets[1].exists());
        assert!(!journal_path(&installation, HostOs::Windows).exists());
        assert_eq!(std::fs::read_dir(installation.join("data/.forge-activation")).unwrap().count(), 0);
    }

    #[test]
    fn success_commits_and_a_left_journal_is_recovered_first() {
        let (_directory, installation, targets) = setup();
        activation_transaction(&installation, &targets, HostOs::Windows, || {
            std::fs::write(&targets[1], b"profile").map_err(|error| ForgeError::io("x", error))
        })
        .unwrap();
        assert_eq!(std::fs::read(&targets[1]).unwrap(), b"profile");
        // A crash mid-body leaves journal + backups: simulate one by hand.
        let journal = journal_path(&installation, HostOs::Windows);
        let resolved: Vec<_> = targets.iter().map(|target| resolve(target).unwrap()).collect();
        std::fs::copy(&targets[0], journal.parent().unwrap().join("0.bak")).unwrap();
        atomic_write_json(
            &journal,
            &json!({"format": JOURNAL_FORMAT, "entries": [
                {"target": path_text(&resolved[0]), "sha256": file_digest(&targets[0]).unwrap()},
                {"target": path_text(&resolved[1]), "sha256": null},
            ]}),
        )
        .unwrap();
        std::fs::write(&targets[0], b"half-written").unwrap();
        activation_transaction(&installation, &targets, HostOs::Windows, || Ok(())).unwrap();
        assert_eq!(std::fs::read(&targets[0]).unwrap(), b"old");
        assert!(!targets[1].exists());
    }

    #[test]
    fn a_different_target_set_is_refused() {
        let (_directory, installation, targets) = setup();
        let journal = journal_path(&installation, HostOs::Windows);
        std::fs::create_dir_all(journal.parent().unwrap()).unwrap();
        atomic_write_json(&journal, &json!({"format": JOURNAL_FORMAT, "entries": [{"target": "elsewhere", "sha256": null}, {"target": "x", "sha256": null}]})).unwrap();
        let error = activation_transaction(&installation, &targets, HostOs::Windows, || Ok(())).unwrap_err();
        assert_eq!(error.message, "Pending activation targets differ; retry the original title installation");
        assert!(journal.exists());
    }
}
