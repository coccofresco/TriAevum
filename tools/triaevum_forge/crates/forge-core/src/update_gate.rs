// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The update gate (UX plan WP-04B): one exclusion contract between the
//! "Update from ZIP" helper and every Forge operation that changes an
//! installation (install, refresh, repair, restore, migration, cleanup).
//!
//! - `<state>/.forge-activation/update-gate.lock` (beside the game session
//!   lock) is a
//!   kernel lock ([`crate::lock`]). Every mutator holds it shared for its
//!   whole run (mutators keep excluding each other through their own locks);
//!   the update helper takes it exclusively before it creates its journal
//!   and keeps it until the update is committed or rolled back. A crash
//!   releases it.
//! - `A/.forge-update/journal.json` is the helper's journal. While it exists
//!   the installation is between two package versions: mutators refuse
//!   (`recovery_required`, reason `interrupted_update`) and so do Play and
//!   the runtime's own bootstrap ("An update is in progress; open Forge").
//!
//! The helper's own child (the new package's `refresh` and `status`) runs
//! while the helper holds the gate: it receives the journal's id in
//! [`DELEGATION_VARIABLE`] and passes only while that journal is the
//! pending one in its rebinding phase. This is coordination between
//! cooperating Forge processes, not a security boundary.

use std::path::{Path, PathBuf};

use forge_base::fsutil::load_json_object;
use forge_base::{ErrorKind, ForgeError, Operation, Reason, Result};
use serde_json::Value;

use crate::layout::{installation_state, HostOs};
use crate::lock::{self, KernelLock};

/// `A/.forge-update`: the journal, the old package generation and results.
pub const UPDATE_DIRECTORY: &str = ".forge-update";
pub const JOURNAL: &str = "journal.json";
pub const GATE_LOCK: &str = "update-gate.lock";
/// Set by the update helper for the engine it runs during an update.
pub const DELEGATION_VARIABLE: &str = "TRIAEVUM_FORGE_UPDATE_JOURNAL";
/// The journal states in which the helper's child may pass the gate.
const DELEGATED_STATES: [&str; 2] = ["rebinding", "validating"];

/// `A/.forge-update`.
pub fn update_directory(activation: &Path) -> PathBuf {
    activation.join(UPDATE_DIRECTORY)
}

/// `A/.forge-update/journal.json`.
pub fn journal_path(activation: &Path) -> PathBuf {
    update_directory(activation).join(JOURNAL)
}

/// `<state>/.forge-activation/update-gate.lock`.
pub fn gate_path(activation: &Path, os: HostOs) -> PathBuf {
    installation_state(activation, os).join(crate::session::ACTIVATION_DIRECTORY).join(GATE_LOCK)
}

/// Whether an update journal is pending (read-only).
pub fn pending(activation: &Path) -> bool {
    journal_path(activation).exists()
}

/// Whether an update helper holds the gate now (read-only).
pub fn held(activation: &Path, os: HostOs) -> bool {
    lock::is_held_exclusively(&gate_path(activation, os))
}

pub fn in_progress(path: &Path, operation: Operation) -> ForgeError {
    ForgeError::new(ErrorKind::Busy, "An update is in progress; wait until it finishes and Forge opens again")
        .with_path(path)
        .during(operation)
        .because(Reason::UpdateInProgress)
}

pub fn interrupted(path: &Path, operation: Operation) -> ForgeError {
    ForgeError::new(
        ErrorKind::RecoveryRequired,
        "An update was interrupted; open Forge to finish or undo it before changing this installation",
    )
    .with_path(path)
    .during(operation)
    .because(Reason::InterruptedUpdate)
}

/// Held by a mutator for its run; empty for the update helper's own child.
#[derive(Debug)]
pub struct Gate {
    _lock: Option<KernelLock>,
}

/// Whether this process is the update helper's delegate (its `refresh` or
/// `status`) for the journal pending in `activation`.
pub fn delegated(activation: &Path) -> bool {
    let Ok(token) = std::env::var(DELEGATION_VARIABLE) else { return false };
    let Ok(journal) = load_json_object(&journal_path(activation)) else { return false };
    let text = |key: &str| journal.get(key).and_then(Value::as_str).unwrap_or_default().to_string();
    !token.is_empty() && text("id") == token && DELEGATED_STATES.contains(&text("state").as_str())
}

/// Enters the gate for `operation`: `busy` while an update helper holds it,
/// `recovery_required` while an update journal is pending, else the gate
/// lock until the returned value is dropped.
pub fn enter(activation: &Path, os: HostOs, operation: Operation) -> Result<Gate> {
    if delegated(activation) {
        return Ok(Gate { _lock: None });
    }
    let path = gate_path(activation, os);
    let journal = journal_path(activation);
    if lock::is_held_exclusively(&path) {
        return Err(in_progress(&path, operation));
    }
    if journal.exists() {
        return Err(interrupted(&journal, operation));
    }
    let folder = path.parent().expect("the gate lock has a folder");
    std::fs::create_dir_all(folder)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", folder.display()), error).during(operation))?;
    let held = lock::acquire_shared(&path).map_err(|error| match error.kind {
        ErrorKind::Busy => in_progress(&path, operation),
        _ => error.during(operation),
    })?;
    // The helper may have died after creating its journal and before we
    // took the lock.
    if journal.exists() {
        return Err(interrupted(&journal, operation));
    }
    Ok(Gate { _lock: Some(held) })
}

/// The gate for the update helper itself: the lock, exclusively (it then
/// creates or recovers the journal). `busy` while any mutator holds it.
pub fn take_for_update(activation: &Path, os: HostOs) -> Result<KernelLock> {
    let path = gate_path(activation, os);
    let folder = path.parent().expect("the gate lock has a folder");
    std::fs::create_dir_all(folder)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", folder.display()), error))?;
    lock::acquire(&path).map_err(|error| match error.kind {
        ErrorKind::Busy => ForgeError::new(
            ErrorKind::Busy,
            "Another Forge operation is changing this installation; close it, then update again",
        )
        .with_path(&path)
        .during(Operation::Update)
        .because(Reason::ForgeRunning),
        _ => error.during(Operation::Update),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mutators_exclude_each_other_and_a_pending_journal_refuses() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        let os = HostOs::Windows;
        assert!(!pending(root) && !held(root, os));
        let first = enter(root, os, Operation::Install).unwrap();
        let second = enter(root, os, Operation::Restore).unwrap();
        assert!(!held(root, os), "mutators hold the gate shared");
        let error = take_for_update(root, os).unwrap_err();
        assert_eq!((error.kind, error.reason), (ErrorKind::Busy, Some(Reason::ForgeRunning)));
        drop((first, second));
        let helper = take_for_update(root, os).unwrap();
        assert!(held(root, os));
        let error = enter(root, os, Operation::Restore).unwrap_err();
        assert_eq!((error.kind, error.reason), (ErrorKind::Busy, Some(Reason::UpdateInProgress)));
        drop(helper);
        assert_eq!(gate_path(root, os), root.join("data").join(".forge-activation").join(GATE_LOCK));

        std::fs::create_dir_all(update_directory(root)).unwrap();
        std::fs::write(journal_path(root), br#"{"id": "j1", "state": "replacing"}"#).unwrap();
        let error = enter(root, os, Operation::Update).unwrap_err();
        assert_eq!((error.kind, error.reason), (ErrorKind::RecoveryRequired, Some(Reason::InterruptedUpdate)));
        let helper = take_for_update(root, os).unwrap();
        let error = enter(root, os, Operation::Cleanup).unwrap_err();
        assert_eq!(error.reason, Some(Reason::UpdateInProgress));
        drop(helper);
    }
}
