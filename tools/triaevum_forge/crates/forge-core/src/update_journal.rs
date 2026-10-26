// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The update journal (UX plan WP-04B steps 3 to 6): how the update helper
//! replaces package files and how any Forge (or the helper again) puts a
//! validated tuple back after a crash.
//!
//! `A/.forge-update/journal.json` is created once, under the update gate,
//! with every change it will make (path, recorded old digest, new digest),
//! before any file moves. Its `state` then advances:
//!
//! `replacing` -> `replaced` -> `rebinding` -> `validating` -> `committed`
//!
//! Per-file progress is not journaled: it is read back from the files.
//! A changed file keeps its old bytes in `A/.forge-update/old/<path>` (a
//! hard link made before the staged file is renamed over the live one, a
//! synced copy where the volume has no hard links); a removed file is
//! renamed there; the installed runtime in the root is renamed to
//! `old/.root/` first, so a legacy runtime that knows nothing of the
//! journal cannot start while files are mixed. Before the rebind, the
//! activation targets (title module, launch profile, active title, receipts,
//! settings) are copied to `A/.forge-update/activation/`: the inner
//! activation transaction deletes its own backups when it commits, and the
//! update must still be able to undo it.
//!
//! Recovery is deterministic: before `committed` everything is rolled back
//! (an interrupted inner activation first, then the activation targets, the
//! root runtime and every file, each verified against its recorded digest)
//! and the old tuple is validated; from `committed` on, only the cleanup is
//! finished. If rollback material is missing or damaged the journal stays,
//! in state `recovery_failed`, and Forge reports `recovery_required`.

use std::cell::Cell;
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};

use forge_base::fsutil::{self, atomic_write_json, file_error, load_json_object};
use forge_base::hashing::{copy_hashing, hex, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result};
use serde_json::{json, Map, Value};

use crate::layout::HostOs;
use crate::package_inventory::live_path;
use crate::update_gate::{journal_path, update_directory};

pub const JOURNAL_FORMAT: &str = "triaevum_update_journal_v1";
pub const RESULT_FORMAT: &str = "triaevum_update_result_v1";
/// `A/.forge-update/old`: the old generation of every changed file.
pub const OLD_DIRECTORY: &str = "old";
/// `old/.root`: the installed runtime of the root, moved aside.
pub const ROOT_ASIDE: &str = ".root";
pub const ACTIVATION_BACKUPS: &str = "activation";
/// `A/.forge-update/result.json`: what the relaunched Forge shows.
pub const RESULT: &str = "result.json";
/// `staging/package`: the verified new files.
pub const STAGED_PACKAGE: &str = "package";
/// `staging/proposal.json`: what Forge asked the helper to do.
pub const PROPOSAL: &str = "proposal.json";

pub fn digest(path: &Path) -> Result<String> {
    Ok(hex(&sha256_file(path, &CancelToken::new())?.1))
}

/// Size first, then the digest.
fn matches(path: &Path, sha256: &str, bytes: Option<u64>) -> bool {
    let Ok(metadata) = std::fs::symlink_metadata(path) else { return false };
    metadata.is_file() && bytes.is_none_or(|bytes| bytes == metadata.len()) && digest(path).is_ok_and(|d| d == sha256)
}

pub fn old_path(activation: &Path, relative: &str) -> PathBuf {
    live_path(&update_directory(activation).join(OLD_DIRECTORY), relative)
}

pub fn result_path(activation: &Path) -> PathBuf {
    update_directory(activation).join(RESULT)
}

fn damaged(message: impl Into<String>) -> ForgeError {
    ForgeError::new(ErrorKind::RecoveryRequired, message).during(Operation::Update).because(Reason::RollbackFailure)
}

/// Fault injection for the kill-matrix tests: abort the process at the
/// n-th boundary (journal write, rename, publication, cleanup step). The
/// production entry points pass [`Faults::none`]; no request can set it.
#[derive(Debug, Default)]
pub struct Faults {
    pub abort_at: Option<usize>,
    passed: Cell<usize>,
}

impl Faults {
    pub fn none() -> Self {
        Self::default()
    }

    pub fn at(boundary: usize) -> Self {
        Self { abort_at: Some(boundary), passed: Cell::new(0) }
    }

    pub fn boundary(&self, label: &str) {
        let passed = self.passed.get() + 1;
        self.passed.set(passed);
        if self.abort_at == Some(passed) {
            eprintln!("update fault injection: abort at boundary {passed} ({label})");
            std::process::abort();
        }
    }

    pub fn passed(&self) -> usize {
        self.passed.get()
    }
}

/// One journaled change. `old_sha256` is what was on disk when the journal
/// was created (None: the path did not exist); `new_sha256` None removes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Entry {
    pub path: String,
    pub old_sha256: Option<String>,
    pub old_bytes: Option<u64>,
    pub new_sha256: Option<String>,
    pub new_bytes: Option<u64>,
}

impl Entry {
    pub fn to_value(&self) -> Value {
        json!({
            "path": self.path,
            "old_sha256": self.old_sha256,
            "old_bytes": self.old_bytes,
            "new_sha256": self.new_sha256,
            "new_bytes": self.new_bytes,
        })
    }

    fn parse(value: &Value) -> Option<Self> {
        let text = |key: &str| value.get(key).and_then(Value::as_str).map(str::to_string);
        let number = |key: &str| value.get(key).and_then(Value::as_u64);
        let path = text("path")?;
        crate::package_inventory::check_member_path(&path).ok()?;
        Some(Self {
            path,
            old_sha256: text("old_sha256"),
            old_bytes: number("old_bytes"),
            new_sha256: text("new_sha256"),
            new_bytes: number("new_bytes"),
        })
    }
}

/// The journal on disk.
#[derive(Clone, Debug)]
pub struct Journal {
    pub path: PathBuf,
    pub value: Map<String, Value>,
}

impl Journal {
    /// The pending journal of `activation`, or None. A journal that cannot
    /// be read is `recovery_required`.
    pub fn load(activation: &Path) -> Result<Option<Self>> {
        let path = journal_path(activation);
        if !path.exists() {
            return Ok(None);
        }
        let value = load_json_object(&path).map_err(|_| damaged("The update journal cannot be read"))?;
        if value.get("format").and_then(Value::as_str) != Some(JOURNAL_FORMAT) {
            return Err(damaged("The update journal has an unsupported format"));
        }
        Ok(Some(Self { path, value }))
    }

    /// Creates the journal; fails when one exists (the caller holds the gate).
    pub fn create(activation: &Path, value: Map<String, Value>) -> Result<Self> {
        let path = journal_path(activation);
        if path.exists() {
            return Err(damaged("Another update is pending; finish or undo it first"));
        }
        let journal = Self { path, value };
        journal.write()?;
        Ok(journal)
    }

    fn write(&self) -> Result<()> {
        atomic_write_json(&self.path, &Value::Object(self.value.clone()))
    }

    pub fn text(&self, key: &str) -> &str {
        self.value.get(key).and_then(Value::as_str).unwrap_or_default()
    }

    pub fn id(&self) -> &str {
        self.text("id")
    }

    pub fn state(&self) -> &str {
        self.text("state")
    }

    pub fn activation(&self) -> PathBuf {
        PathBuf::from(self.text("activation"))
    }

    pub fn staging(&self) -> PathBuf {
        PathBuf::from(self.text("staging"))
    }

    pub fn set(&mut self, key: &str, value: Value) {
        self.value.insert(key.to_string(), value);
    }

    pub fn advance(&mut self, state: &str) -> Result<()> {
        self.set("state", json!(state));
        self.write()
    }

    pub fn entries(&self) -> Result<Vec<Entry>> {
        let items = self
            .value
            .get("changes")
            .and_then(Value::as_array)
            .ok_or_else(|| damaged("The update journal lists no changes"))?;
        items
            .iter()
            .map(|item| Entry::parse(item).ok_or_else(|| damaged("The update journal has an invalid change")))
            .collect()
    }

    fn root_runtime(&self) -> Option<(String, Option<String>)> {
        let record = self.value.get("root_runtime")?.as_object()?;
        let path = record.get("path")?.as_str()?.to_string();
        Some((path, record.get("sha256").and_then(Value::as_str).map(str::to_string)))
    }

    fn activation_targets(&self) -> Vec<(PathBuf, Option<String>)> {
        let items = self.value.get("activation_targets").and_then(Value::as_array).cloned().unwrap_or_default();
        items
            .iter()
            .filter_map(|item| {
                let target = PathBuf::from(item.get("target")?.as_str()?);
                Some((target, item.get("sha256").and_then(Value::as_str).map(str::to_string)))
            })
            .collect()
    }
}

/// Streams `source` into a synced temporary beside `target`, then renames it.
fn copy_synced(source: &Path, target: &Path) -> Result<()> {
    let parent = target.parent().unwrap_or(Path::new("."));
    std::fs::create_dir_all(parent)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    let mut temporary = tempfile::Builder::new()
        .prefix(".update-")
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

fn create_parent(path: &Path) -> Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)
            .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    }
    Ok(())
}

/// Keeps the old bytes of `live` at `backup`: a hard link (instant, no
/// space) or, where the volume has none, a synced copy.
fn keep_old(live: &Path, backup: &Path) -> Result<()> {
    create_parent(backup)?;
    fsutil::remove_file(backup).map_err(|error| file_error("Cannot replace", backup, error))?;
    if std::fs::hard_link(live, backup).is_ok() {
        return Ok(());
    }
    copy_synced(live, backup)
}

fn rename(source: &Path, target: &Path) -> Result<()> {
    create_parent(target)?;
    fsutil::replace_file(source, target).map_err(|error| file_error("Cannot replace", target, error))
}

/// Removes empty folders from `relative`'s parent up to (not including) `root`.
fn prune_empty_parents(root: &Path, relative: &str) {
    let mut current = live_path(root, relative);
    while let Some(parent) = current.parent() {
        if parent == root || !parent.starts_with(root) || std::fs::remove_dir(parent).is_err() {
            break;
        }
        current = parent.to_path_buf();
    }
}

/// Step 4: the installed runtime of the root goes aside first.
pub fn move_root_runtime_aside(journal: &Journal, faults: &Faults) -> Result<()> {
    let activation = journal.activation();
    if let Some((name, Some(_))) = journal.root_runtime() {
        let live = activation.join(&name);
        if live.exists() {
            rename(&live, &old_path(&activation, &format!("{ROOT_ASIDE}/{name}")))?;
            faults.boundary("root runtime aside");
        }
    }
    Ok(())
}

/// Step 3: every journaled change, old bytes kept first.
pub fn replace_files(journal: &Journal, faults: &Faults) -> Result<()> {
    let activation = journal.activation();
    let staged_root = journal.staging().join(STAGED_PACKAGE);
    for entry in journal.entries()? {
        let live = live_path(&activation, &entry.path);
        let backup = old_path(&activation, &entry.path);
        if entry.old_sha256.is_some() {
            if entry.new_sha256.is_none() {
                rename(&live, &backup)?;
                prune_empty_parents(&activation, &entry.path);
                faults.boundary("removed");
                continue;
            }
            keep_old(&live, &backup)?;
            faults.boundary("old bytes kept");
        }
        rename(&live_path(&staged_root, &entry.path), &live)?;
        faults.boundary("replaced");
    }
    Ok(())
}

/// The files the activation transaction rewrites (`install.rs`), plus the
/// installed-package record written after it commits.
pub fn activation_targets(activation: &Path, data_root: &Path) -> Result<Vec<PathBuf>> {
    let platform = crate::layout::host_platform()?;
    let active = data_root.join(crate::active_title::ACTIVE_TITLE);
    let mut targets = vec![
        activation.join(platform.title_module),
        // Both profile places: a legacy root profile and the data/ one.
        activation.join("TriAevum.launch.json"),
        crate::layout::installation_state(activation, HostOs::current()).join("TriAevum.launch.json"),
        active.clone(),
        data_root.join("config").join("TriAevum.json"),
        data_root.join("config").join("topscreen_ui.json"),
    ];
    if let Some(directory) = crate::active_title::named_directory(&active) {
        targets.push(directory.join("forge-state.json"));
        targets.push(directory.join(crate::refresh::PACKAGE_RECORD));
    }
    targets.dedup();
    Ok(targets)
}

/// Step 5 preparation: copies the activation targets aside and records them
/// with state `rebinding` (one journal write).
pub fn back_up_activation(journal: &mut Journal, targets: &[PathBuf], faults: &Faults) -> Result<()> {
    let directory = update_directory(&journal.activation()).join(ACTIVATION_BACKUPS);
    std::fs::create_dir_all(&directory)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", directory.display()), error))?;
    let mut records = Vec::with_capacity(targets.len());
    for (index, target) in targets.iter().enumerate() {
        let sha256 = if target.is_file() {
            let backup = directory.join(format!("{index}.bak"));
            copy_synced(target, &backup)?;
            Value::String(digest(&backup)?)
        } else {
            Value::Null
        };
        records.push(json!({"target": target.to_string_lossy(), "sha256": sha256}));
    }
    journal.set("activation_targets", Value::Array(records));
    journal.advance("rebinding")?;
    faults.boundary("rebinding");
    Ok(())
}

/// Undoes the update: the old tuple, validated. The journal is removed and
/// the result written; on failure the journal stays as `recovery_failed`.
pub fn rollback(journal: &mut Journal, cause: &str) -> Result<Value> {
    match restore_old_tuple(journal) {
        Ok(()) => {
            let result = result_value(journal, "rolled_back", Some(cause));
            discard(journal, &result)?;
            Ok(result)
        }
        Err(error) => {
            journal.set("error", json!(format!("{cause}; rollback: {}", error.message)));
            let _ = journal.advance("recovery_failed");
            let result = result_value(journal, "recovery_required", Some(&error.message));
            let _ = atomic_write_json(&result_path(&journal.activation()), &result);
            Err(error)
        }
    }
}

/// An inner activation the new engine's refresh did not finish is put back
/// from its own journal first (it takes the installation lock itself).
pub fn settle_inner_activation(journal: &Journal) -> Result<()> {
    let activation = journal.activation();
    let os = HostOs::current();
    if crate::transaction::journal_path(&activation, os).exists() {
        let data_root = PathBuf::from(journal.text("data_root"));
        crate::transaction::recover_interrupted(&activation, os, &[activation.clone(), data_root])?;
    }
    Ok(())
}

fn restore_old_tuple(journal: &Journal) -> Result<()> {
    let activation = journal.activation();
    settle_inner_activation(journal)?;
    let backups = update_directory(&activation).join(ACTIVATION_BACKUPS);
    let targets = journal.activation_targets();
    for (index, (_, sha256)) in targets.iter().enumerate() {
        if let Some(sha256) = sha256 {
            if !matches(&backups.join(format!("{index}.bak")), sha256, None) {
                return Err(damaged("A backup of the previous activation is missing or damaged"));
            }
        }
    }
    for (index, (target, sha256)) in targets.iter().enumerate() {
        match sha256 {
            None => fsutil::remove_file(target).map_err(|error| file_error("Cannot remove", target, error))?,
            Some(sha256) if !matches(target, sha256, None) => {
                copy_synced(&backups.join(format!("{index}.bak")), target)?
            }
            Some(_) => {}
        }
    }
    if let Some((name, sha256)) = journal.root_runtime() {
        let live = activation.join(&name);
        let aside = old_path(&activation, &format!("{ROOT_ASIDE}/{name}"));
        match sha256 {
            None => fsutil::remove_file(&live).map_err(|error| file_error("Cannot remove", &live, error))?,
            Some(sha256) => {
                if matches(&aside, &sha256, None) {
                    rename(&aside, &live)?;
                } else if !matches(&live, &sha256, None) {
                    return Err(damaged(format!("The previous {name} is missing or damaged")));
                }
            }
        }
    }
    let entries = journal.entries()?;
    for entry in entries.iter().rev() {
        let live = live_path(&activation, &entry.path);
        let backup = old_path(&activation, &entry.path);
        match &entry.old_sha256 {
            None => {
                fsutil::remove_file(&live).map_err(|error| file_error("Cannot remove", &live, error))?;
                prune_empty_parents(&activation, &entry.path);
            }
            Some(sha256) => {
                if matches(&backup, sha256, entry.old_bytes) {
                    rename(&backup, &live)?;
                } else if !matches(&live, sha256, entry.old_bytes) {
                    return Err(damaged(format!("The previous version of {} is missing or damaged", entry.path)));
                }
            }
        }
    }
    validate_tuple(journal, Generation::Old)
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Generation {
    Old,
    New,
}

/// Every journaled path holds the bytes of `generation` (sizes, then
/// digests): the tuple is that generation's, not merely present files.
pub fn validate_tuple(journal: &Journal, generation: Generation) -> Result<()> {
    let activation = journal.activation();
    let platform = crate::layout::host_platform()?;
    for entry in journal.entries()? {
        let live = live_path(&activation, &entry.path);
        let (sha256, bytes) = match generation {
            Generation::Old => (&entry.old_sha256, entry.old_bytes),
            Generation::New => (&entry.new_sha256, entry.new_bytes),
        };
        // Activation moves the packaged runtime to the root.
        let placed = |sha256: &str| {
            entry.path == platform.runtime
                && std::fs::symlink_metadata(&live).is_err()
                && matches(&activation.join(platform.installed_runtime), sha256, bytes)
        };
        let valid = match sha256 {
            None => std::fs::symlink_metadata(&live).is_err(),
            Some(sha256) => matches(&live, sha256, bytes) || placed(sha256),
        };
        if !valid {
            return Err(damaged(format!("{} does not hold the expected version", entry.path)));
        }
    }
    if generation == Generation::Old {
        if let Some((name, Some(sha256))) = journal.root_runtime() {
            if !matches(&activation.join(&name), &sha256, None) {
                return Err(damaged(format!("{name} does not hold the previous version")));
            }
        }
    }
    Ok(())
}

/// The result document the relaunched Forge shows.
pub fn result_value(journal: &Journal, status: &str, error: Option<&str>) -> Value {
    let field = |key: &str| journal.value.get(key).cloned().unwrap_or(Value::Null);
    json!({
        "format": RESULT_FORMAT,
        "id": journal.id(),
        "status": status,
        "previous_version": field("previous_version"),
        "version": field("version"),
        "hud_layout": field("hud_layout"),
        "authenticity": field("authenticity"),
        "locally_modified": field("locally_modified"),
        "config_changes": field("config_changes"),
        "error": error,
    })
}

/// Removes the rollback material and the staged files, writes the result
/// and removes the journal (each a fault boundary).
fn discard(journal: &Journal, result: &Value) -> Result<()> {
    discard_with(journal, result, &Faults::none())
}

fn discard_with(journal: &Journal, result: &Value, faults: &Faults) -> Result<()> {
    let activation = journal.activation();
    let directory = update_directory(&activation);
    for (path, label) in [
        (directory.join(OLD_DIRECTORY), "old generation removed"),
        (directory.join(ACTIVATION_BACKUPS), "activation backups removed"),
        (journal.staging().join(STAGED_PACKAGE), "staged files removed"),
    ] {
        remove_tree(&path)?;
        faults.boundary(label);
    }
    let _ = std::fs::remove_file(journal.staging().join(PROPOSAL));
    atomic_write_json(&result_path(&activation), result)?;
    faults.boundary("result written");
    std::fs::remove_file(&journal.path).map_err(|error| ForgeError::io("Cannot remove the update journal", error))?;
    faults.boundary("journal removed");
    Ok(())
}

fn remove_tree(path: &Path) -> Result<()> {
    match std::fs::remove_dir_all(path) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(file_error("Cannot remove", path, error)),
    }
}

/// Step 6 after `committed`: the old generation goes, the result stays.
pub fn finish(journal: &Journal, faults: &Faults) -> Result<Value> {
    let result = result_value(journal, "updated", None);
    discard_with(journal, &result, faults)?;
    Ok(result)
}

/// Settles a pending journal of `activation` (Forge at startup, the
/// helper run without a proposal): rolls back before `committed`, finishes
/// the cleanup after. Takes the update gate and the installation lock.
/// None when no journal is pending.
pub fn recover(activation: &Path) -> Result<Option<Value>> {
    let os = HostOs::current();
    if Journal::load(activation)?.is_none() {
        return Ok(None);
    }
    let _gate = crate::update_gate::take_for_update(activation, os).map_err(|_| {
        crate::update_gate::in_progress(&crate::update_gate::gate_path(activation, os), Operation::Update)
    })?;
    let Some(mut journal) = Journal::load(activation)? else { return Ok(None) };
    let same = |left: &Path, right: &Path| {
        forge_prepare::paths::resolve(left)
            .ok()
            .zip(forge_prepare::paths::resolve(right).ok())
            .is_some_and(|(a, b)| a == b)
    };
    if !same(&journal.activation(), activation) {
        return Err(damaged("The update journal belongs to another folder (the installation was moved)"));
    }
    let result = match journal.state() {
        "committed" => finish(&journal, &Faults::none())?,
        _ => {
            settle_inner_activation(&journal)?;
            let _quiet = crate::session::quiesce_for_update(activation, os)?;
            rollback(&mut journal, "The update was interrupted; the previous version was restored")?
        }
    };
    Ok(Some(result))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(path: &Path, bytes: &[u8]) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, bytes).unwrap();
    }

    fn sha(bytes: &[u8]) -> String {
        forge_base::hashing::sha256_hex(bytes)
    }

    /// A journal over two replaced files, one added and one removed.
    fn fixture(root: &Path) -> Journal {
        let activation = root.join("A");
        let staging = activation.join(".update-2-1");
        write(&activation.join("runtime/a.dll"), b"old a");
        write(&activation.join("docs/gone.md"), b"old gone");
        write(&activation.join("TriAevum.exe"), b"old root");
        write(&staging.join("package/runtime/a.dll"), b"new a");
        write(&staging.join("package/docs/new.md"), b"new file");
        let changes = [
            Entry {
                path: "runtime/a.dll".into(),
                old_sha256: Some(sha(b"old a")),
                old_bytes: Some(5),
                new_sha256: Some(sha(b"new a")),
                new_bytes: Some(5),
            },
            Entry {
                path: "docs/new.md".into(),
                old_sha256: None,
                old_bytes: None,
                new_sha256: Some(sha(b"new file")),
                new_bytes: Some(8),
            },
            Entry {
                path: "docs/gone.md".into(),
                old_sha256: Some(sha(b"old gone")),
                old_bytes: Some(8),
                new_sha256: None,
                new_bytes: None,
            },
        ];
        let mut value = Map::new();
        value.insert("format".into(), json!(JOURNAL_FORMAT));
        value.insert("id".into(), json!("j"));
        value.insert("state".into(), json!("replacing"));
        value.insert("activation".into(), json!(activation.to_string_lossy()));
        value.insert("staging".into(), json!(staging.to_string_lossy()));
        value.insert("data_root".into(), json!(activation.join("data").to_string_lossy()));
        value.insert("changes".into(), Value::Array(changes.iter().map(Entry::to_value).collect()));
        value.insert("root_runtime".into(), json!({"path": "TriAevum.exe", "sha256": sha(b"old root")}));
        Journal::create(&activation, value).unwrap()
    }

    #[test]
    fn replaced_files_roll_back_to_the_validated_old_tuple() {
        let directory = tempfile::tempdir().unwrap();
        let mut journal = fixture(directory.path());
        let activation = journal.activation();
        move_root_runtime_aside(&journal, &Faults::none()).unwrap();
        replace_files(&journal, &Faults::none()).unwrap();
        validate_tuple(&journal, Generation::New).unwrap();
        assert!(!activation.join("TriAevum.exe").exists());
        assert_eq!(std::fs::read(old_path(&activation, "runtime/a.dll")).unwrap(), b"old a");
        let result = rollback(&mut journal, "test").unwrap();
        assert_eq!(result["status"], "rolled_back");
        assert_eq!(std::fs::read(activation.join("runtime/a.dll")).unwrap(), b"old a");
        assert_eq!(std::fs::read(activation.join("docs/gone.md")).unwrap(), b"old gone");
        assert_eq!(std::fs::read(activation.join("TriAevum.exe")).unwrap(), b"old root");
        assert!(!activation.join("docs/new.md").exists());
        assert!(!journal_path(&activation).exists());
        assert!(!update_directory(&activation).join(OLD_DIRECTORY).exists());
    }

    #[test]
    fn damaged_rollback_material_keeps_the_journal() {
        let directory = tempfile::tempdir().unwrap();
        let mut journal = fixture(directory.path());
        let activation = journal.activation();
        replace_files(&journal, &Faults::none()).unwrap();
        std::fs::write(old_path(&activation, "runtime/a.dll"), b"evil").unwrap();
        let error = rollback(&mut journal, "test").unwrap_err();
        assert_eq!((error.kind, error.reason), (ErrorKind::RecoveryRequired, Some(Reason::RollbackFailure)));
        let kept = Journal::load(&activation).unwrap().unwrap();
        assert_eq!(kept.state(), "recovery_failed");
        assert_eq!(load_json_object(&result_path(&activation)).unwrap()["status"], "recovery_required");
    }

    #[test]
    fn a_committed_journal_finishes_forward() {
        let directory = tempfile::tempdir().unwrap();
        let mut journal = fixture(directory.path());
        let activation = journal.activation();
        replace_files(&journal, &Faults::none()).unwrap();
        journal.advance("committed").unwrap();
        let result = recover(&activation).unwrap().unwrap();
        assert_eq!(result["status"], "updated");
        assert_eq!(std::fs::read(activation.join("runtime/a.dll")).unwrap(), b"new a");
        assert!(!journal_path(&activation).exists());
        assert!(recover(&activation).unwrap().is_none());
    }
}
