// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Restoring a backup ([`crate::backup`]) into an installation (UX plan
//! WP-05B, section 3.3), so that the installation always ends as one
//! coherent version: the full previous files or the full restore.
//!
//! Sequence (`restore`), all while no game and no other Forge runs:
//!
//! 1. The backup is verified completely ([`crate::backup::open_backup`]):
//!    nothing is written for a refused backup.
//! 2. The plan maps each backup unit to the destination's inventory entry of
//!    the same name. Units whose folders nest in the destination merge into
//!    the outermost one ("groups"). A group either replaces its folder (the
//!    folder becomes the backup's files plus the destination's own nested
//!    entries a backup leaves out, such as crash dumps) or, for settings
//!    from another platform, overlays only the portable settings (decision
//!    13). `savestates.json` keeps the destination's save-state folder and
//!    its `LastState` is remapped into it.
//! 3. A durable journal `D/backups/restore-<stamp>/journal.json` is written
//!    before the first write. Every file the restore replaces or removes is
//!    copied, verified, into `snapshot/<group>/` beside it.
//! 4. Each group's new folder is staged as a sibling (`<name>.restore-<stamp>.new`,
//!    same volume) from the verified backup.
//! 5. Category by category (saves, save states, settings, textures,
//!    enhancements) each group swaps by two renames (`dest` -> `.old`,
//!    `.new` -> `dest`); the journal records every step and each committed
//!    category.
//! 6. Committed: the `.old` siblings are removed; the snapshot stays.
//!
//! An interruption leaves the journal. [`recover_pending`] (run by every
//! frontend before Play is offered) rolls every group back from its `.old`
//! sibling (or, failing that, from the snapshot) unless all categories had
//! committed, in which case it only finishes the cleanup.

use std::collections::{BTreeMap, HashSet};
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::Duration;

use forge_base::fsutil::{atomic_write_json, file_error, load_json_object};
use forge_base::hashing::{copy_hashing, hex, sha256_bytes, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result};
use serde_json::{json, Map, Value};

use crate::backup::{
    copy_member, is_within, list_files, open_archive, open_backup, os_of, same_path, stamp, supported_recipes,
    ManifestEntry, OpenedBackup, Progress, UnitKind, TOUCH_PREFERENCES,
};
use crate::inventory::{Backup as Class, Category, Entry, EntryKind, Inventory};
use crate::layout::HostOs;

pub const PREVIEW_FORMAT: &str = "triaevum_restore_preview_v1";
pub const JOURNAL_FORMAT: &str = "triaevum_restore_journal_v1";
pub const RESULT_FORMAT: &str = "triaevum_restore_result_v1";
pub const BACKUPS: &str = "backups";
const JOURNAL: &str = "journal.json";
const SNAPSHOT: &str = "snapshot";
const MARGIN: u64 = 64 << 20;

/// The order categories are applied in.
pub const CATEGORY_ORDER: [Category; 5] =
    [Category::Saves, Category::SaveStates, Category::Settings, Category::Textures, Category::Enhancements];

/// Units that mean the same on every platform (decision 13). Settings that
/// hold device-specific values or literal paths (the runtime configuration
/// with display and texture folders, controls, the F1 window layout, the
/// Android host settings and touch preferences) are restored only on the
/// platform that made the backup.
const PORTABLE_UNITS: &[&str] = &[
    "save_data",
    "save_states",
    "save_states_default",
    "texture_load",
    "mods",
    "downloads",
    "topscreen_config",
    "game_language",
    "savestates_index",
    "environments",
];
/// The portable files of the settings folder, restored over another
/// platform's settings.
const PORTABLE_SETTINGS: &[&str] = &["topscreen_ui.json", "game_language.json", "savestates.json"];
const PORTABLE_SETTINGS_FOLDER: &str = "environments/";
const SAVESTATES: &str = "savestates.json";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    /// The folder becomes the backup's files (plus nested entries a backup leaves out).
    Replace,
    /// The backup's portable files are written over the current folder.
    Overlay,
}

impl Mode {
    pub fn code(self) -> &'static str {
        match self {
            Self::Replace => "replace",
            Self::Overlay => "overlay",
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct RestoreOptions {
    /// Restore only these units (default: every restorable one).
    pub units: Option<Vec<String>>,
    /// Fault injection for tests: exit with code 86, without unwinding, right
    /// after this many categories committed. Never set by a request.
    pub abort_after_category: Option<usize>,
}

/// What a file of a group's new folder comes from.
#[derive(Clone, Debug)]
enum Incoming {
    Member(ManifestEntry),
    /// Rewritten content (`savestates.json`).
    Bytes(Vec<u8>),
}

impl Incoming {
    fn size(&self) -> u64 {
        match self {
            Self::Member(entry) => entry.size,
            Self::Bytes(bytes) => bytes.len() as u64,
        }
    }

    fn sha256(&self) -> String {
        match self {
            Self::Member(entry) => entry.sha256.clone(),
            Self::Bytes(bytes) => hex(&sha256_bytes(bytes)),
        }
    }
}

/// One destination folder or file and what the restore puts there.
#[derive(Clone, Debug)]
pub struct Group {
    pub name: String,
    pub category: Category,
    pub kind: UnitKind,
    pub destination: PathBuf,
    pub mode: Mode,
    pub existed: bool,
    /// The backup units placed in this group (the first is the group's own).
    pub units: Vec<String>,
    incoming: BTreeMap<String, Incoming>,
    /// Destination files the restore may replace or remove (relative path, path, size).
    current: BTreeMap<String, (PathBuf, u64)>,
    /// Destination files kept as they are in the new folder.
    carry: BTreeMap<String, (PathBuf, u64)>,
    pub replaced: Vec<String>,
    pub removed: Vec<String>,
    pub added: Vec<String>,
    pub unchanged: Vec<String>,
}

impl Group {
    fn snapshot_files(&self) -> Vec<&String> {
        self.replaced.iter().chain(&self.removed).collect()
    }

    fn snapshot_bytes(&self) -> u64 {
        self.snapshot_files().iter().filter_map(|relative| self.current.get(*relative)).map(|(_, size)| size).sum()
    }

    /// The bytes of the staged new folder.
    fn staging_bytes(&self) -> u64 {
        let incoming: u64 = self.incoming.values().map(Incoming::size).sum();
        let kept: u64 = match self.mode {
            Mode::Replace => self.carry.values().map(|(_, size)| size).sum(),
            Mode::Overlay => self
                .current
                .iter()
                .filter(|(relative, _)| !self.incoming.contains_key(*relative))
                .map(|(_, (_, size))| size)
                .sum(),
        };
        incoming + kept
    }

    fn to_value(&self) -> Value {
        let examples = |list: &[String]| list.iter().take(8).cloned().collect::<Vec<_>>();
        json!({
            "name": self.name,
            "category": self.category.code(),
            "kind": self.kind.code(),
            "destination": self.destination.to_string_lossy(),
            "mode": self.mode.code(),
            "exists": self.existed,
            "units": self.units,
            "replaced": self.replaced.len(),
            "removed": self.removed.len(),
            "added": self.added.len(),
            "unchanged": self.unchanged.len(),
            "kept": self.carry.len(),
            "bytes": self.incoming.values().map(Incoming::size).sum::<u64>(),
            "examples": {"replaced": examples(&self.replaced), "removed": examples(&self.removed),
                         "added": examples(&self.added)},
        })
    }
}

/// A verified backup mapped onto an installation; nothing written yet.
#[derive(Clone, Debug)]
pub struct RestorePlan {
    pub backup: OpenedBackup,
    pub same_platform: bool,
    pub groups: Vec<Group>,
    /// (unit, reason) of the units not restored.
    pub skipped: Vec<(String, &'static str)>,
    pub warnings: Vec<Value>,
    /// Settings of another platform's backup that stay as they are here.
    pub kept_settings: Vec<String>,
    /// The touch preferences the app applies after the restore (Android).
    pub touch_preferences: Option<ManifestEntry>,
    pub data_root: PathBuf,
}

impl RestorePlan {
    pub fn snapshot_bytes(&self) -> u64 {
        self.groups.iter().map(Group::snapshot_bytes).sum()
    }

    pub fn required_bytes(&self) -> u64 {
        self.snapshot_bytes() + self.groups.iter().map(Group::staging_bytes).sum::<u64>()
    }

    pub fn to_value(&self) -> Value {
        let available = forge_native::system::available_space(&self.data_root);
        json!({
            "format": PREVIEW_FORMAT,
            "backup": self.backup.manifest.summary(),
            "same_platform": self.same_platform,
            "groups": self.groups.iter().map(Group::to_value).collect::<Vec<_>>(),
            "skipped": self.skipped.iter().map(|(unit, reason)| json!({"unit": unit, "reason": reason}))
                .collect::<Vec<_>>(),
            "warnings": self.warnings,
            "kept_settings": self.kept_settings,
            "touch_preferences": self.touch_preferences.is_some(),
            "snapshot_bytes": self.snapshot_bytes(),
            "required_bytes": self.required_bytes(),
            "available_bytes": available,
            "snapshot_folder": self.data_root.join(BACKUPS).to_string_lossy(),
        })
    }
}

fn posix_relative(path: &Path, base: &Path) -> Option<String> {
    let base_text = base.to_string_lossy().replace('\\', "/");
    let path_text = path.to_string_lossy().replace('\\', "/");
    let base_text = base_text.trim_end_matches('/');
    let rest = if cfg!(windows) {
        let lower = path_text.to_lowercase();
        lower
            .starts_with(&format!("{}/", base_text.to_lowercase()))
            .then(|| path_text[base_text.len() + 1..].to_string())
    } else {
        path_text.strip_prefix(&format!("{base_text}/")).map(str::to_string)
    };
    rest.filter(|rest| !rest.is_empty())
}

fn portable_setting(relative: &str) -> bool {
    PORTABLE_SETTINGS.contains(&relative) || relative.starts_with(PORTABLE_SETTINGS_FOLDER)
}

fn sha256_of(path: &Path, cancel: &CancelToken) -> Result<String> {
    Ok(hex(&sha256_file(path, cancel)?.1))
}

/// Reads one small member (verified) into memory.
fn member_bytes(backup: &OpenedBackup, entry: &ManifestEntry, cancel: &CancelToken) -> Result<Vec<u8>> {
    let mut archive = open_archive(&backup.path)?;
    let mut bytes = Vec::new();
    copy_member(&mut archive, entry, &mut bytes, cancel)?;
    Ok(bytes)
}

/// A path written by the backup's platform lies in `base` (both as text).
fn foreign_within(path: &str, base: &str, windows: bool) -> Option<String> {
    let normalize = |text: &str| {
        let text = text.replace('\\', "/");
        if windows {
            text.to_lowercase()
        } else {
            text
        }
    };
    let (path_key, base_key) = (normalize(path), normalize(base.trim_end_matches(['/', '\\'])));
    if base_key.is_empty() || !path_key.starts_with(&format!("{base_key}/")) {
        return None;
    }
    let rest = path.replace('\\', "/")[base_key.len() + 1..].to_string();
    crate::backup::safe_relative(&rest).then_some(rest)
}

struct Planner<'a> {
    inventory: &'a Inventory,
    backup: &'a OpenedBackup,
    cancel: &'a CancelToken,
    warnings: Vec<Value>,
}

impl Planner<'_> {
    /// `savestates.json` for this installation: the destination's
    /// save-state folder choice is kept, `LastState` follows the restored
    /// states into it.
    fn rewrite_savestates(&mut self, entry: &ManifestEntry) -> Result<Vec<u8>> {
        let bytes = member_bytes(self.backup, entry, self.cancel)?;
        let mut document = forge_base::legacy_json::load_object(&bytes).unwrap_or_default();
        let current = self
            .inventory
            .entry("savestates_index")
            .and_then(|index| load_json_object(&index.resolved_path).ok())
            .and_then(|index| index.get("Directory").and_then(Value::as_str).map(str::to_string))
            .unwrap_or_default();
        document.insert("Directory".into(), json!(current));
        let last = document.get("LastState").and_then(Value::as_str).unwrap_or_default().to_string();
        if !last.is_empty() {
            let windows = self.backup.manifest.platform == crate::inventory::Platform::WindowsPortable;
            let source = self.backup.manifest.unit("save_states").map(|unit| unit.source.clone());
            let remapped = source.as_deref().and_then(|source| foreign_within(&last, source, windows));
            match (remapped, self.inventory.entry("save_states")) {
                (Some(rest), Some(states)) => {
                    let target = rest.split('/').fold(states.resolved_path.clone(), |path, part| path.join(part));
                    document.insert("LastState".into(), json!(target.to_string_lossy()));
                }
                _ if !Path::new(&last).exists() => {
                    self.warnings.push(json!({"kind": "unavailable_reference", "setting": "LastState", "path": last}));
                }
                _ => {}
            }
        }
        Ok(forge_base::legacy_json::pretty_file_bytes(&Value::Object(document)))
    }

    /// Texture folders the restored runtime configuration names that do
    /// not exist here.
    fn check_texture_folders(&mut self, entry: &ManifestEntry) {
        let Ok(bytes) = member_bytes(self.backup, entry, self.cancel) else { return };
        let Ok(config) = forge_base::legacy_json::load_object(&bytes) else { return };
        let azahar = config
            .get("Graphics")
            .and_then(|graphics| graphics.get("TexturePacks"))
            .and_then(|packs| packs.get("Azahar"));
        for key in ["LoadDirectory", "DumpDirectory"] {
            let Some(path) = azahar.and_then(|azahar| azahar.get(key)).and_then(Value::as_str) else { continue };
            if !path.is_empty() && Path::new(path).is_absolute() && !Path::new(path).is_dir() {
                self.warnings.push(json!({"kind": "unavailable_reference", "setting": key, "path": path}));
            }
        }
    }
}

/// Whether a destination folder is unsafe to swap (it holds the installation).
fn unsafe_destination(inventory: &Inventory, path: &Path) -> bool {
    let roots = &inventory.roots;
    [&roots.data, &roots.activation, &roots.package, &roots.state].iter().any(|root| is_within(root, path))
}

/// Maps a verified backup onto `inventory` and classifies every file.
pub fn plan_restore(
    inventory: &Inventory,
    backup: &OpenedBackup,
    options: &RestoreOptions,
    cancel: &CancelToken,
) -> Result<RestorePlan> {
    let manifest = &backup.manifest;
    let same_platform = manifest.platform == inventory.roots.platform;
    let mut planner = Planner { inventory, backup, cancel, warnings: Vec::new() };
    if !same_platform {
        planner.warnings.push(json!({"kind": "other_platform", "from": manifest.platform.code(),
                                      "to": inventory.roots.platform.code()}));
    }
    let installed = crate::backup::installed_recipe(&inventory.roots.data);
    if let (Some(from), Some(here)) = (&manifest.recipe, &installed) {
        if from != here {
            planner.warnings.push(json!({"kind": "different_revision", "backup": from, "installed": here}));
        }
    }
    let mut skipped = Vec::new();
    let mut touch_preferences = None;
    // (unit, destination entry, mode)
    let mut candidates: Vec<(String, &Entry, Mode)> = Vec::new();
    for unit in &manifest.units {
        if options.units.as_ref().is_some_and(|chosen| !chosen.contains(&unit.name)) {
            skipped.push((unit.name.clone(), "not_selected"));
            continue;
        }
        let destination = inventory.entry(&unit.name);
        if unit.name == TOUCH_PREFERENCES {
            match (destination, same_platform) {
                (Some(_), true) => touch_preferences = manifest.entries_of(&unit.name).next().cloned(),
                (Some(_), false) => skipped.push((unit.name.clone(), "target_specific")),
                (None, _) => skipped.push((unit.name.clone(), "no_location")),
            }
            continue;
        }
        let Some(destination) = destination else {
            skipped.push((unit.name.clone(), "no_location"));
            continue;
        };
        let mode = if same_platform {
            Mode::Replace
        } else if unit.name == "config" {
            Mode::Overlay
        } else if PORTABLE_UNITS.contains(&unit.name.as_str()) {
            Mode::Replace
        } else {
            skipped.push((unit.name.clone(), "target_specific"));
            continue;
        };
        let kinds_match = matches!(
            (unit.kind, destination.kind),
            (UnitKind::Directory, EntryKind::Directory) | (UnitKind::File, EntryKind::File)
        );
        if !kinds_match || destination.category == Category::State {
            skipped.push((unit.name.clone(), "kind_mismatch"));
            continue;
        }
        if destination.kind == EntryKind::Directory && unsafe_destination(inventory, &destination.resolved_path) {
            skipped.push((unit.name.clone(), "unsafe_destination"));
            continue;
        }
        candidates.push((unit.name.clone(), destination, mode));
    }
    candidates.sort_by_key(|(_, entry, _)| entry.resolved_path.components().count());

    let mut groups: Vec<Group> = Vec::new();
    for (unit, destination, mode) in candidates {
        let path = &destination.resolved_path;
        if groups.iter().any(|group| same_path(&group.destination, path)) {
            skipped.push((unit, "duplicate_destination"));
            continue;
        }
        let outer =
            groups.iter_mut().find(|group| group.kind == UnitKind::Directory && is_within(path, &group.destination));
        let (group, prefix) = match outer {
            Some(group) => {
                let prefix = posix_relative(path, &group.destination).unwrap_or_default();
                (group, prefix)
            }
            None => {
                let kind = if destination.kind == EntryKind::Directory { UnitKind::Directory } else { UnitKind::File };
                let existed = match kind {
                    UnitKind::Directory => path.is_dir(),
                    _ => path.is_file(),
                };
                groups.push(Group {
                    name: unit.clone(),
                    category: destination.category,
                    kind,
                    destination: path.clone(),
                    mode,
                    existed,
                    units: Vec::new(),
                    incoming: BTreeMap::new(),
                    current: BTreeMap::new(),
                    carry: BTreeMap::new(),
                    replaced: Vec::new(),
                    removed: Vec::new(),
                    added: Vec::new(),
                    unchanged: Vec::new(),
                });
                (groups.last_mut().expect("just pushed"), String::new())
            }
        };
        group.units.push(unit.clone());
        let overlay = group.mode == Mode::Overlay && prefix.is_empty() && unit == "config";
        for entry in manifest.entries_of(&unit) {
            let relative = match (prefix.is_empty(), entry.path.is_empty()) {
                (true, _) => entry.path.clone(),
                (false, true) => prefix.clone(),
                (false, false) => format!("{prefix}/{}", entry.path),
            };
            if overlay && !portable_setting(&entry.path) {
                continue;
            }
            group.incoming.insert(relative, Incoming::Member(entry.clone()));
        }
    }

    // savestates.json and texture folders.
    let savestates_index = inventory.entry("savestates_index").map(|entry| entry.resolved_path.clone());
    let runtime_config = inventory.entry("runtime_config").map(|entry| entry.resolved_path.clone());
    for group in &mut groups {
        let keys: Vec<String> = group.incoming.keys().cloned().collect();
        for relative in keys {
            let target = if relative.is_empty() {
                group.destination.clone()
            } else {
                relative.split('/').fold(group.destination.clone(), |path, part| path.join(part))
            };
            let Some(Incoming::Member(entry)) = group.incoming.get(&relative).cloned() else { continue };
            if savestates_index.as_ref().is_some_and(|index| same_path(index, &target))
                || (relative.ends_with(SAVESTATES) && savestates_index.is_none())
            {
                let bytes = planner.rewrite_savestates(&entry)?;
                group.incoming.insert(relative, Incoming::Bytes(bytes));
            } else if runtime_config.as_ref().is_some_and(|config| same_path(config, &target)) {
                planner.check_texture_folders(&entry);
            }
        }
    }

    // What each group keeps, replaces, removes and adds.
    let group_names: HashSet<String> = groups.iter().flat_map(|group| group.units.clone()).collect();
    for group in &mut groups {
        if group.kind == UnitKind::File {
            if group.existed {
                let size = std::fs::metadata(&group.destination).map(|metadata| metadata.len()).unwrap_or(0);
                group.current.insert(String::new(), (group.destination.clone(), size));
            }
        } else {
            let nested: Vec<&Entry> = inventory
                .entries
                .iter()
                .filter(|entry| {
                    !group_names.contains(entry.logical_name)
                        && is_within(&entry.resolved_path, &group.destination)
                        && !same_path(&entry.resolved_path, &group.destination)
                        && (entry.backup != Class::Include || entry.category == Category::State)
                })
                .collect();
            let skip: Vec<PathBuf> = nested.iter().map(|entry| entry.resolved_path.clone()).collect();
            if group.mode == Mode::Replace {
                for entry in &nested {
                    let Some(prefix) = posix_relative(&entry.resolved_path, &group.destination) else { continue };
                    if entry.resolved_path.is_file() {
                        let size = std::fs::metadata(&entry.resolved_path).map(|m| m.len()).unwrap_or(0);
                        group.carry.insert(prefix, (entry.resolved_path.clone(), size));
                    } else {
                        for item in list_files(&entry.resolved_path, &[])? {
                            group.carry.insert(format!("{prefix}/{}", item.relative), (item.path, item.size));
                        }
                    }
                }
                // A backup file where a kept entry lives stays out.
                let kept: Vec<String> = group.carry.keys().cloned().collect();
                let skips: Vec<String> = nested
                    .iter()
                    .filter_map(|entry| posix_relative(&entry.resolved_path, &group.destination))
                    .collect();
                group.incoming.retain(|relative, _| {
                    let blocked =
                        skips.iter().any(|skip| relative == skip || relative.starts_with(&format!("{skip}/")))
                            || kept.contains(relative);
                    if blocked {
                        planner.warnings.push(json!({"kind": "kept_existing", "unit": group.name, "path": relative}));
                    }
                    !blocked
                });
            }
            let listing = list_files(&group.destination, if group.mode == Mode::Replace { &skip } else { &[] })?;
            for item in listing {
                group.current.insert(item.relative, (item.path, item.size));
            }
        }
        for (relative, incoming) in &group.incoming {
            match group.current.get(relative) {
                Some((path, size)) => {
                    if *size == incoming.size() && sha256_of(path, cancel)? == incoming.sha256() {
                        group.unchanged.push(relative.clone());
                    } else {
                        group.replaced.push(relative.clone());
                    }
                }
                None => group.added.push(relative.clone()),
            }
        }
        if group.mode == Mode::Replace {
            group.removed =
                group.current.keys().filter(|relative| !group.incoming.contains_key(*relative)).cloned().collect();
        }
    }
    let Planner { warnings, .. } = planner;
    let mut plan = RestorePlan {
        backup: backup.clone(),
        same_platform,
        groups,
        skipped,
        warnings,
        kept_settings: Vec::new(),
        touch_preferences,
        data_root: inventory.roots.data.clone(),
    };
    plan.kept_settings = manifest
        .entries_of("config")
        .filter(|entry| !same_platform && !portable_setting(&entry.path))
        .map(|entry| entry.path.clone())
        .collect();
    Ok(plan)
}

/// The preview of restoring `backup` into `inventory`: verification and
/// the plan, nothing written.
pub fn restore_preview(
    inventory: &Inventory,
    backup: &Path,
    options: &RestoreOptions,
    cancel: &CancelToken,
) -> Result<RestorePlan> {
    let supported = supported_recipes(&inventory.roots.package);
    let opened = open_backup(backup, supported.as_deref(), cancel)?;
    plan_restore(inventory, &opened, options, cancel).map_err(|error| error.during(Operation::Restore))
}

// ---- The journal ----

fn sync_directory(path: &Path) {
    #[cfg(unix)]
    if let Ok(directory) = File::open(path) {
        let _ = directory.sync_all();
    }
    #[cfg(not(unix))]
    let _ = path;
}

fn write_journal(path: &Path, journal: &Value) -> Result<()> {
    atomic_write_json(path, journal)?;
    if let Some(parent) = path.parent() {
        sync_directory(parent);
    }
    Ok(())
}

/// Renames, retrying briefly: a scanner may hold a file that was just written.
fn rename(from: &Path, to: &Path) -> Result<()> {
    let mut delays = [25u64, 50, 100, 200, 400, 800].into_iter();
    loop {
        match std::fs::rename(from, to) {
            Ok(()) => {
                if let Some(parent) = to.parent() {
                    sync_directory(parent);
                }
                return Ok(());
            }
            Err(error) => match delays.next() {
                Some(delay) if error.kind() != std::io::ErrorKind::NotFound => {
                    std::thread::sleep(Duration::from_millis(delay))
                }
                _ => return Err(ForgeError::io(format!("Cannot move {} to {}", from.display(), to.display()), error)),
            },
        }
    }
}

fn remove_path(path: &Path) -> Result<()> {
    let result = match std::fs::symlink_metadata(path) {
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(()),
        Err(error) => Err(error),
        Ok(metadata) if metadata.is_dir() => std::fs::remove_dir_all(path),
        Ok(_) => forge_base::fsutil::remove_file(path),
    };
    result.map_err(|error| file_error("Cannot remove", path, error))
}

fn sibling(path: &Path, stamp: &str, suffix: &str) -> PathBuf {
    let name = path.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
    path.with_file_name(format!("{name}.restore-{stamp}.{suffix}"))
}

fn join_relative(base: &Path, relative: &str) -> PathBuf {
    if relative.is_empty() {
        base.to_path_buf()
    } else {
        relative.split('/').fold(base.to_path_buf(), |path, part| path.join(part))
    }
}

/// Copies `source` to `target` (synced), then reads the copy back and
/// compares its digest with the source's.
fn copy_verified(source: &Path, target: &Path, cancel: &CancelToken) -> Result<(u64, String)> {
    if let Some(parent) = target.parent() {
        std::fs::create_dir_all(parent).map_err(|error| file_error("Cannot create", parent, error))?;
    }
    let mut input = File::open(source).map_err(|error| file_error("Cannot read", source, error))?;
    let mut output = File::create(target).map_err(|error| file_error("Cannot create", target, error))?;
    let (size, digest) = copy_hashing(&mut input, &mut output, u64::MAX, cancel)?;
    output.flush().and_then(|()| output.sync_all()).map_err(|error| file_error("Cannot write", target, error))?;
    drop(output);
    let copied = sha256_file(target, cancel)?;
    if copied.0 != size || copied.1 != digest {
        return Err(ForgeError::integrity(format!("The copy of {} did not verify", source.display())));
    }
    Ok((size, hex(&digest)))
}

fn write_synced(target: &Path, bytes: &[u8]) -> Result<()> {
    if let Some(parent) = target.parent() {
        std::fs::create_dir_all(parent).map_err(|error| file_error("Cannot create", parent, error))?;
    }
    let mut output = File::create(target).map_err(|error| file_error("Cannot create", target, error))?;
    output.write_all(bytes).and_then(|()| output.sync_all()).map_err(|error| file_error("Cannot write", target, error))
}

fn journal_group(group: &Group, stamp: &str) -> Value {
    json!({
        "name": group.name,
        "category": group.category.code(),
        "kind": group.kind.code(),
        "destination": group.destination.to_string_lossy(),
        "new": sibling(&group.destination, stamp, "new").to_string_lossy(),
        "old": sibling(&group.destination, stamp, "old").to_string_lossy(),
        "existed": group.existed,
        "status": "pending",
        "added": group.added,
        "snapshot": [],
    })
}

fn set(journal: &mut Value, pointer: &str, value: Value) {
    if let Some(slot) = journal.pointer_mut(pointer) {
        *slot = value;
    }
}

/// The journal of an unfinished restore under `data_root`, if any.
pub fn pending_restore(data_root: &Path) -> Option<PathBuf> {
    let mut pending: Vec<PathBuf> = std::fs::read_dir(data_root.join(BACKUPS))
        .ok()?
        .flatten()
        .filter(|child| child.file_name().to_string_lossy().starts_with("restore-"))
        .map(|child| child.path().join(JOURNAL))
        .filter(|journal| {
            journal.is_file()
                && load_json_object(journal).map_or(true, |journal| {
                    !matches!(journal.get("state").and_then(Value::as_str), Some("complete" | "rolled_back"))
                })
        })
        .collect();
    pending.sort();
    pending.pop()
}

fn recovery_failed(message: impl Into<String>) -> ForgeError {
    ForgeError::new(ErrorKind::RecoveryRequired, message).during(Operation::Restore).because(Reason::RollbackFailure)
}

fn group_text<'a>(group: &'a Map<String, Value>, key: &str) -> Result<&'a str> {
    group.get(key).and_then(Value::as_str).ok_or_else(|| recovery_failed(format!("The restore journal has no {key}")))
}

/// Puts one group back as it was before the restore.
fn roll_back_group(group: &Map<String, Value>, snapshot: &Path, cancel: &CancelToken) -> Result<()> {
    let destination = PathBuf::from(group_text(group, "destination")?);
    let new = PathBuf::from(group_text(group, "new")?);
    let old = PathBuf::from(group_text(group, "old")?);
    let existed = group.get("existed").and_then(Value::as_bool).unwrap_or(true);
    let status = group_text(group, "status")?;
    if matches!(status, "pending" | "staged") {
        return remove_path(&new);
    }
    if old.exists() {
        remove_path(&destination)?;
        rename(&old, &destination)?;
        return remove_path(&new);
    }
    if !existed {
        remove_path(&destination)?;
        return remove_path(&new);
    }
    if new.exists() {
        // `dest -> .old` never happened: the destination is the original.
        return remove_path(&new);
    }
    // The previous folder is gone: rebuild it from the restored one and the
    // snapshot (unchanged files are identical in both).
    let name = group_text(group, "name")?;
    let saved = snapshot.join(name);
    let kind = group_text(group, "kind")?;
    for relative in group.get("added").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_str) {
        remove_path(&join_relative(&destination, relative))?;
    }
    for item in group.get("snapshot").and_then(Value::as_array).into_iter().flatten() {
        let relative = item.get("path").and_then(Value::as_str).unwrap_or_default();
        let source = if kind == "file" { saved.clone() } else { join_relative(&saved, relative) };
        let (_, digest) = copy_verified(&source, &join_relative(&destination, relative), cancel)?;
        if Some(digest.as_str()) != item.get("sha256").and_then(Value::as_str) {
            return Err(recovery_failed(format!("The restore snapshot of {name} is damaged")));
        }
    }
    Ok(())
}

/// Finishes or undoes the restore whose journal is `journal_path`; the
/// caller holds the quiescence locks.
fn settle(journal_path: &Path, cancel: &CancelToken) -> Result<Value> {
    let mut journal = Value::Object(
        load_json_object(journal_path)
            .map_err(|error| recovery_failed(format!("Cannot read the restore journal: {}", error.message)))?,
    );
    if journal["format"] != JOURNAL_FORMAT {
        return Err(recovery_failed("The restore journal has an unknown format"));
    }
    let directory = journal_path.parent().unwrap_or(Path::new(".")).to_path_buf();
    let groups: Vec<Map<String, Value>> =
        journal["groups"].as_array().into_iter().flatten().filter_map(|group| group.as_object().cloned()).collect();
    let outcome = if journal["state"] == "committed" {
        for group in &groups {
            remove_path(Path::new(group_text(group, "old")?))?;
        }
        "complete"
    } else {
        for group in groups.iter().rev() {
            roll_back_group(group, &directory.join(SNAPSHOT), cancel)?;
        }
        "rolled_back"
    };
    set(&mut journal, "/state", json!(outcome));
    journal["settled"] = json!(crate::log::timestamp(std::time::SystemTime::now()));
    write_journal(journal_path, &journal)?;
    Ok(json!({"journal": journal_path.to_string_lossy(), "outcome": outcome,
              "snapshot": directory.join(SNAPSHOT).to_string_lossy()}))
}

/// Recovers an interrupted restore of the installation at activation root
/// `activation` (data folder `data_root`): every group is put back unless
/// all categories had committed. None when nothing was pending.
pub fn recover_pending(activation: &Path, os: HostOs, data_root: &Path, cancel: &CancelToken) -> Result<Option<Value>> {
    if pending_restore(data_root).is_none() {
        return Ok(None);
    }
    let _quiet = crate::session::quiesce(activation, os, Operation::Restore)?;
    let mut settled = Vec::new();
    while let Some(journal) = pending_restore(data_root) {
        settled.push(settle(&journal, cancel)?);
    }
    Ok(settled.pop())
}

// ---- Restore ----

/// Restores `backup` into `inventory`'s installation (see the module
/// documentation). Returns the restored groups, the snapshot folder and,
/// for the Android app, the touch preferences to apply.
pub fn restore(
    inventory: &Inventory,
    backup: &Path,
    options: &RestoreOptions,
    progress: Progress<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    let supported = supported_recipes(&inventory.roots.package);
    let opened = open_backup(backup, supported.as_deref(), cancel)?;
    let os = os_of(inventory.roots.platform);
    let _quiet = crate::session::quiesce(&inventory.roots.activation, os, Operation::Restore)?;
    let data_root = &inventory.roots.data;
    if let Some(journal) = pending_restore(data_root) {
        return Err(ForgeError::new(
            ErrorKind::RecoveryRequired,
            format!("An interrupted restore must be recovered first ({})", journal.display()),
        )
        .during(Operation::Restore)
        .because(Reason::InterruptedRestore));
    }
    let plan = plan_restore(inventory, &opened, options, cancel).map_err(|error| error.during(Operation::Restore))?;
    if plan.groups.is_empty() && plan.touch_preferences.is_none() {
        return Err(ForgeError::invalid("Nothing in this backup can be restored here").during(Operation::Restore));
    }
    if let Some(available) = forge_native::system::available_space(data_root) {
        let required = plan.required_bytes() + MARGIN;
        if available < required {
            return Err(ForgeError::new(
                ErrorKind::StorageFull,
                format!("The restore needs {required} bytes of free space; {available} are free"),
            )
            .during(Operation::Restore)
            .because(Reason::InsufficientSpace));
        }
    }
    let now = std::time::SystemTime::now();
    let mut name = format!("restore-{}", stamp(now));
    let mut suffix = 2;
    while data_root.join(BACKUPS).join(&name).exists() {
        name = format!("restore-{}-{suffix}", stamp(now));
        suffix += 1;
    }
    let stamp = name.trim_start_matches("restore-").to_string();
    let directory = data_root.join(BACKUPS).join(&name);
    std::fs::create_dir_all(&directory).map_err(|error| file_error("Cannot create", &directory, error))?;
    let journal_path = directory.join(JOURNAL);
    let mut journal = json!({
        "format": JOURNAL_FORMAT,
        "state": "prepared",
        "created": crate::log::timestamp(now),
        "backup_path": backup.to_string_lossy(),
        "backup": opened.manifest.summary(),
        "same_platform": plan.same_platform,
        "groups": plan.groups.iter().map(|group| journal_group(group, &stamp)).collect::<Vec<_>>(),
        "categories": CATEGORY_ORDER.iter()
            .filter(|category| plan.groups.iter().any(|group| group.category == **category))
            .map(|category| json!({"category": category.code(), "status": "pending"}))
            .collect::<Vec<_>>(),
    });
    write_journal(&journal_path, &journal)?;
    let applied = apply(&plan, &opened, &stamp, &directory, &journal_path, &mut journal, options, progress, cancel);
    if let Err(error) = applied {
        return Err(match settle(&journal_path, &CancelToken::new()) {
            Ok(_) => error.during(Operation::Restore),
            Err(rollback) => {
                recovery_failed(format!("{} The rollback also failed: {}", error.message, rollback.message))
            }
        });
    }
    let touch = match &plan.touch_preferences {
        Some(entry) => {
            let bytes = member_bytes(&opened, entry, cancel)?;
            Value::Object(forge_base::legacy_json::load_object(&bytes).unwrap_or_default())
        }
        None => Value::Null,
    };
    Ok(json!({
        "format": RESULT_FORMAT,
        "snapshot": directory.join(SNAPSHOT).to_string_lossy(),
        "journal": journal_path.to_string_lossy(),
        "groups": plan.groups.iter().map(Group::to_value).collect::<Vec<_>>(),
        "skipped": plan.skipped.iter().map(|(unit, reason)| json!({"unit": unit, "reason": reason})).collect::<Vec<_>>(),
        "warnings": plan.warnings,
        "kept_settings": plan.kept_settings,
        "touch_preferences": touch,
    }))
}

#[allow(clippy::too_many_arguments)]
fn apply(
    plan: &RestorePlan,
    backup: &OpenedBackup,
    stamp: &str,
    directory: &Path,
    journal_path: &Path,
    journal: &mut Value,
    options: &RestoreOptions,
    progress: Progress<'_>,
    cancel: &CancelToken,
) -> Result<()> {
    let total = plan.required_bytes();
    let mut done = 0u64;
    // Snapshot of every replaced or removed file, verified.
    for (index, group) in plan.groups.iter().enumerate() {
        let mut saved = Vec::new();
        for relative in group.snapshot_files() {
            let (source, _) = &group.current[relative];
            let target = if group.kind == UnitKind::File {
                directory.join(SNAPSHOT).join(&group.name)
            } else {
                join_relative(&directory.join(SNAPSHOT).join(&group.name), relative)
            };
            let (size, digest) = copy_verified(source, &target, cancel)?;
            done += size;
            progress("snapshot", done, Some(total));
            saved.push(json!({"path": relative, "size": size, "sha256": digest}));
        }
        set(journal, &format!("/groups/{index}/snapshot"), Value::Array(saved));
    }
    set(journal, "/state", json!("staging"));
    write_journal(journal_path, journal)?;

    // The new folders, beside their destinations.
    let mut archive = open_archive(&backup.path)?;
    for (index, group) in plan.groups.iter().enumerate() {
        let new = sibling(&group.destination, stamp, "new");
        remove_path(&new)?;
        let kept: Vec<(&String, &PathBuf)> = match group.mode {
            Mode::Replace => group.carry.iter().map(|(relative, (path, _))| (relative, path)).collect(),
            Mode::Overlay => group
                .current
                .iter()
                .filter(|(relative, _)| !group.incoming.contains_key(*relative))
                .map(|(relative, (path, _))| (relative, path))
                .collect(),
        };
        if group.kind == UnitKind::Directory {
            std::fs::create_dir_all(&new).map_err(|error| file_error("Cannot create", &new, error))?;
        } else if let Some(parent) = new.parent() {
            std::fs::create_dir_all(parent).map_err(|error| file_error("Cannot create", parent, error))?;
        }
        for (relative, source) in kept {
            let (size, _) = copy_verified(source, &join_relative(&new, relative), cancel)?;
            done += size;
            progress("staging", done, Some(total));
        }
        for (relative, incoming) in &group.incoming {
            let target = join_relative(&new, relative);
            match incoming {
                Incoming::Bytes(bytes) => write_synced(&target, bytes)?,
                Incoming::Member(entry) => {
                    if let Some(parent) = target.parent() {
                        std::fs::create_dir_all(parent).map_err(|error| file_error("Cannot create", parent, error))?;
                    }
                    let mut output =
                        File::create(&target).map_err(|error| file_error("Cannot create", &target, error))?;
                    copy_member(&mut archive, entry, &mut output, cancel)?;
                    output.sync_all().map_err(|error| file_error("Cannot write", &target, error))?;
                }
            }
            done += incoming.size();
            progress("staging", done, Some(total));
        }
        set(journal, &format!("/groups/{index}/status"), json!("staged"));
    }
    set(journal, "/state", json!("applying"));
    write_journal(journal_path, journal)?;

    // Category by category: two renames per group, each step journaled.
    let mut committed = 0usize;
    for (category_index, category) in CATEGORY_ORDER.iter().enumerate() {
        let members: Vec<usize> =
            (0..plan.groups.len()).filter(|index| plan.groups[*index].category == *category).collect();
        if members.is_empty() {
            continue;
        }
        for index in members {
            let group = &plan.groups[index];
            set(journal, &format!("/groups/{index}/status"), json!("swapping"));
            write_journal(journal_path, journal)?;
            let old = sibling(&group.destination, stamp, "old");
            if group.existed {
                rename(&group.destination, &old)?;
            } else if let Some(parent) = group.destination.parent() {
                std::fs::create_dir_all(parent).map_err(|error| file_error("Cannot create", parent, error))?;
            }
            rename(&sibling(&group.destination, stamp, "new"), &group.destination)?;
            set(journal, &format!("/groups/{index}/status"), json!("swapped"));
            write_journal(journal_path, journal)?;
        }
        let position = journal["categories"]
            .as_array()
            .and_then(|list| list.iter().position(|item| item["category"] == category.code()))
            .unwrap_or(category_index);
        set(journal, &format!("/categories/{position}/status"), json!("committed"));
        write_journal(journal_path, journal)?;
        committed += 1;
        if options.abort_after_category == Some(committed) {
            // Leave the journal and partial restore intact, without running
            // stack destructors or the error/rollback path. An ordinary exit
            // avoids OS crash reports for this deliberate interruption.
            std::process::exit(86);
        }
    }
    set(journal, "/state", json!("committed"));
    write_journal(journal_path, journal)?;
    for group in &plan.groups {
        let old = sibling(&group.destination, stamp, "old");
        let _ = remove_path(&old);
    }
    set(journal, "/state", json!("complete"));
    write_journal(journal_path, journal)
}
