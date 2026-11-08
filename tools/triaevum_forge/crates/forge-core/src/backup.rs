// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Backups of the user's files (UX plan WP-05B, section 3.3): one zip with a
//! manifest, made from the resolved inventory ([`crate::inventory`]).
//!
//! What a backup holds: every present inventory entry classed `include`
//! plus the `optional` ones the user chose (decision 2: mods, downloads and
//! the texture pack are never chosen by default), minus the descendants a
//! backup of a folder leaves out (excluded or unchosen nested entries:
//! crash dumps in the save data, the launch marker beside the settings).
//! Nested chosen entries travel inside their outermost chosen folder. The
//! Android touch preferences live in the app's shared preferences, which the
//! app serializes to JSON for the backup ([`TOUCH_PREFERENCES`]).
//!
//! Format `triaevum_backup_v1` (`manifest.json` plus `files/...`):
//!
//! ```json
//! {"format": "triaevum_backup_v1", "schema": 1, "created": "2026-10-26T12:00:00Z",
//!  "engine_version": "...", "package_version": "0.8.0" | null, "game": "oot3d",
//!  "recipe": "<installed revision>" | null, "platform": "windows_portable",
//!  "units": [{"name": "save_data", "category": "saves", "kind": "directory",
//!             "origin": "default", "managed": true, "source": "<resolved path>"}],
//!  "entries": [{"unit": "save_data", "path": "system/save.bin", "size": 10, "sha256": "..."}]}
//! ```
//!
//! A directory unit's files are `files/<unit>/<path>`; a file or JSON unit is
//! the single member `files/<unit>` with path `""`. Reading a backup
//! ([`open_backup`]) verifies everything before anything is written: the
//! manifest's schema and game, safe relative names (no `..`, roots, drive
//! letters, device names or case collisions), the member list exactly equal
//! to the manifest's, and every member's size and SHA-256.

use std::collections::{BTreeMap, HashSet};
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use forge_base::fsutil::{self, file_error, load_json_object};
use forge_base::hashing::{copy_hashing, hex, is_sha256_hex};
use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result, Subject};
use serde_json::{json, Map, Value};

use crate::inventory::{Backup as Class, Category, Entry, EntryKind, Inventory, Origin, Platform};
use crate::layout::HostOs;

pub const FORMAT: &str = "triaevum_backup_v1";
pub const SCHEMA: u64 = 1;
pub const MANIFEST: &str = "manifest.json";
pub const PLAN_FORMAT: &str = "triaevum_backup_plan_v1";
pub const RESULT_FORMAT: &str = "triaevum_backup_result_v1";
/// Legacy recipe catalogs and the original `open_backup` API are OoT-only.
/// New/multi-title catalogs declare `game` on every recipe; never infer it
/// from a display name, recipe id, texture override or a backup being opened.
pub const GAME: &str = "oot3d";
/// The logical name of the Android touch preferences, stored as JSON.
pub const TOUCH_PREFERENCES: &str = "touch_preferences";
const FILES: &str = "files/";
/// More than any real installation; bounds what a hostile manifest can ask.
const MAX_ENTRIES: usize = 200_000;
const MAX_PATH: usize = 1024;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UnitKind {
    File,
    Directory,
    /// Serialized by the host app (the Android touch preferences).
    Json,
}

impl UnitKind {
    pub fn code(self) -> &'static str {
        match self {
            Self::File => "file",
            Self::Directory => "directory",
            Self::Json => "json",
        }
    }

    fn from_code(code: &str) -> Option<Self> {
        [Self::File, Self::Directory, Self::Json].into_iter().find(|kind| kind.code() == code)
    }
}

pub(crate) fn platform_from_code(code: &str) -> Option<Platform> {
    [
        Platform::WindowsPortable,
        Platform::LinuxPortable,
        Platform::AppImage,
        Platform::Flatpak,
        Platform::Android,
        Platform::MacOsApp,
    ]
        .into_iter()
        .find(|platform| platform.code() == code)
}

/// The host family of an installation model.
pub fn os_of(platform: Platform) -> HostOs {
    match platform {
        Platform::WindowsPortable => HostOs::Windows,
        Platform::Android => HostOs::Android,
        Platform::MacOsApp => HostOs::MacOs,
        _ => HostOs::Linux,
    }
}

/// pathlib comparisons: case-insensitive on Windows.
pub(crate) fn same_path(left: &Path, right: &Path) -> bool {
    if cfg!(windows) {
        left.to_string_lossy().to_lowercase() == right.to_string_lossy().to_lowercase()
    } else {
        left == right
    }
}

pub(crate) fn is_within(path: &Path, base: &Path) -> bool {
    if cfg!(windows) {
        let lower = |path: &Path| PathBuf::from(path.to_string_lossy().to_lowercase());
        lower(path).starts_with(lower(base))
    } else {
        path.starts_with(base)
    }
}

/// `YYYYMMDD-HHMMSS` (UTC), for folder names.
pub(crate) fn stamp(time: std::time::SystemTime) -> String {
    let text = crate::log::timestamp(time);
    let digits: String = text.chars().filter(char::is_ascii_digit).collect();
    format!("{}-{}", &digits[..8], &digits[8..14])
}

/// `YYYYMMDD-HHMM` (UTC), for backup file names.
pub fn stamp_minutes(time: std::time::SystemTime) -> String {
    stamp(time)[..13].to_string()
}

/// The entries above `entry`, innermost first.
pub(crate) fn ancestors<'a>(inventory: &'a Inventory, entry: &Entry) -> Vec<&'a Entry> {
    let mut found = Vec::new();
    let mut parent = entry.parent;
    while let Some(name) = parent {
        let Some(above) = inventory.entry(name) else { break };
        found.push(above);
        parent = above.parent;
    }
    found
}

pub(crate) fn descendants<'a>(inventory: &'a Inventory, name: &str) -> Vec<&'a Entry> {
    inventory
        .entries
        .iter()
        .filter(|entry| ancestors(inventory, entry).iter().any(|above| above.logical_name == name))
        .collect()
}

/// A regular file of a unit.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FileItem {
    /// POSIX path relative to the unit's folder; `""` for a file unit.
    pub relative: String,
    pub path: PathBuf,
    pub size: u64,
}

/// Regular files below `root`, sorted, leaving out `skip` (files or
/// folders). Symbolic links are neither followed nor stored.
pub(crate) fn list_files(root: &Path, skip: &[PathBuf]) -> Result<Vec<FileItem>> {
    let mut files = Vec::new();
    let mut pending = vec![(root.to_path_buf(), String::new())];
    while let Some((directory, prefix)) = pending.pop() {
        let children = match std::fs::read_dir(&directory) {
            Ok(children) => children,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound && directory == root => return Ok(files),
            Err(error) => return Err(file_error("Cannot read the folder", &directory, error)),
        };
        for child in children {
            let child = child.map_err(|error| file_error("Cannot read the folder", &directory, error))?;
            let path = child.path();
            if skip.iter().any(|skipped| same_path(&path, skipped)) {
                continue;
            }
            let name = child.file_name();
            let Some(name) = name.to_str() else {
                return Err(ForgeError::invalid(format!(
                    "Cannot back up a file whose name is not Unicode: {}",
                    path.display()
                )));
            };
            let relative = if prefix.is_empty() { name.to_string() } else { format!("{prefix}/{name}") };
            let metadata = child.metadata().map_err(|error| file_error("Cannot inspect", &path, error))?;
            if metadata.is_dir() {
                pending.push((path, relative));
            } else if metadata.is_file() {
                files.push(FileItem { relative, size: metadata.len(), path });
            }
        }
    }
    files.sort_by(|left, right| left.relative.cmp(&right.relative));
    Ok(files)
}

/// One chosen inventory entry and its files.
#[derive(Clone, Debug)]
pub struct Unit {
    pub name: &'static str,
    pub category: Category,
    pub kind: UnitKind,
    pub path: PathBuf,
    pub origin: Origin,
    pub managed: bool,
    pub files: Vec<FileItem>,
    /// The descendants left out of a folder unit.
    pub skipped: Vec<PathBuf>,
}

impl Unit {
    pub fn bytes(&self) -> u64 {
        self.files.iter().map(|file| file.size).sum()
    }
}

/// What a backup would hold, and the offered and excluded locations.
#[derive(Clone, Debug)]
pub struct BackupPlan {
    pub game: String,
    pub platform: Platform,
    pub units: Vec<Unit>,
    /// Optional entries that can be added (present and not inside a unit).
    pub optional: Vec<(&'static str, Category, PathBuf, u64, bool)>,
    /// Present entries never backed up.
    pub excluded: Vec<(&'static str, Category, PathBuf, u64)>,
    /// The app must provide the touch preferences (Android without them).
    pub needs_touch_preferences: bool,
}

impl BackupPlan {
    pub fn bytes(&self) -> u64 {
        self.units.iter().map(Unit::bytes).sum()
    }

    pub fn files(&self) -> usize {
        self.units.iter().map(|unit| unit.files.len()).sum()
    }

    pub fn to_value(&self) -> Value {
        let path = |path: &Path| path.to_string_lossy().into_owned();
        json!({
            "format": PLAN_FORMAT,
            "game": self.game,
            "platform": self.platform.code(),
            "bytes": self.bytes(),
            "files": self.files(),
            "units": self.units.iter().map(|unit| json!({
                "name": unit.name, "category": unit.category.code(), "kind": unit.kind.code(),
                "path": path(&unit.path), "origin": unit.origin.code(), "managed": unit.managed,
                "files": unit.files.len(), "bytes": unit.bytes(),
            })).collect::<Vec<_>>(),
            "optional": self.optional.iter().map(|(name, category, location, bytes, chosen)| json!({
                "name": name, "category": category.code(), "path": path(location), "bytes": bytes, "chosen": chosen,
            })).collect::<Vec<_>>(),
            "excluded": self.excluded.iter().map(|(name, category, location, bytes)| json!({
                "name": name, "category": category.code(), "path": path(location), "bytes": bytes,
            })).collect::<Vec<_>>(),
            "needs_touch_preferences": self.needs_touch_preferences,
        })
    }
}

fn selected(entry: &Entry, chosen: &[String]) -> bool {
    entry.category != Category::State
        && match entry.backup {
            Class::Include => true,
            Class::Optional => chosen.iter().any(|name| name == entry.logical_name),
            Class::Exclude => false,
        }
}

/// The backup set of `inventory`: include + `chosen` optional entries −
/// what a folder unit leaves out. `touch` is the app's serialized touch
/// preferences (Android). Lists the files (reads folders, writes nothing).
pub fn backup_plan(inventory: &Inventory, chosen: &[String], touch: Option<&Map<String, Value>>) -> Result<BackupPlan> {
    let target = backup_target(inventory)?;
    let roots = &inventory.roots;
    let mut units = Vec::new();
    let mut optional = Vec::new();
    let mut excluded = Vec::new();
    let mut needs_touch_preferences = false;
    for entry in &inventory.entries {
        let inside_unit = ancestors(inventory, entry).iter().any(|above| above.present && selected(above, chosen));
        if entry.logical_name == TOUCH_PREFERENCES {
            match touch {
                Some(_) => units.push(Unit {
                    name: entry.logical_name,
                    category: entry.category,
                    kind: UnitKind::Json,
                    path: entry.resolved_path.clone(),
                    origin: entry.origin,
                    managed: entry.managed,
                    files: Vec::new(),
                    skipped: Vec::new(),
                }),
                None => needs_touch_preferences = true,
            }
            continue;
        }
        if !entry.present || inside_unit {
            continue;
        }
        if entry.backup == Class::Optional && entry.category != Category::State {
            let chosen = chosen.iter().any(|name| name == entry.logical_name);
            optional.push((entry.logical_name, entry.category, entry.resolved_path.clone(), entry.size, chosen));
        }
        if !selected(entry, chosen) {
            excluded.push((entry.logical_name, entry.category, entry.resolved_path.clone(), entry.size));
            continue;
        }
        let (kind, files, skipped) = match entry.kind {
            EntryKind::File => {
                let size = std::fs::metadata(&entry.resolved_path)
                    .map_err(|error| file_error("Cannot inspect", &entry.resolved_path, error))?
                    .len();
                let file = FileItem { relative: String::new(), path: entry.resolved_path.clone(), size };
                (UnitKind::File, vec![file], Vec::new())
            }
            EntryKind::Directory => {
                let holds_installation = [&roots.data, &roots.activation, &roots.package, &roots.state]
                    .iter()
                    .any(|root| is_within(root, &entry.resolved_path));
                if holds_installation {
                    return Err(ForgeError::new(
                        ErrorKind::InvalidSettings,
                        format!(
                            "{} holds the installation itself and cannot be backed up as one folder",
                            entry.resolved_path.display()
                        ),
                    )
                    .during(Operation::Backup)
                    .about(Subject::Settings));
                }
                let skipped: Vec<PathBuf> = descendants(inventory, entry.logical_name)
                    .into_iter()
                    .filter(|inner| !selected(inner, chosen))
                    .map(|inner| inner.resolved_path.clone())
                    .collect();
                (UnitKind::Directory, list_files(&entry.resolved_path, &skipped)?, skipped)
            }
        };
        units.push(Unit {
            name: entry.logical_name,
            category: entry.category,
            kind,
            path: entry.resolved_path.clone(),
            origin: entry.origin,
            managed: entry.managed,
            files,
            skipped,
        });
    }
    Ok(BackupPlan { game: target.game, platform: roots.platform, units, optional, excluded, needs_touch_preferences })
}

/// The zip member of a manifest entry.
pub fn member_name(unit: &str, relative: &str) -> String {
    if relative.is_empty() {
        format!("{FILES}{unit}")
    } else {
        format!("{FILES}{unit}/{relative}")
    }
}

/// The recipe the installation's active title names, if any.
pub(crate) fn installed_recipe(data_root: &Path) -> Option<String> {
    let active = load_json_object(&data_root.join("active-title.json")).ok()?;
    active.get("recipe")?.as_str().filter(|recipe| !recipe.is_empty()).map(str::to_string)
}

/// What the user asks a backup for.
#[derive(Clone, Debug, Default)]
pub struct BackupRequest {
    /// The zip to write (replaced when it exists: a save dialog asked).
    pub destination: PathBuf,
    /// Optional entries to add (`mods`, `downloads`, `texture_load`, ...).
    pub optional: Vec<String>,
    /// The Android app's touch preferences as a JSON object.
    pub touch_preferences: Option<Map<String, Value>>,
}

/// Progress of a long operation: stage, bytes done, bytes in all.
pub type Progress<'a> = &'a mut dyn FnMut(&str, u64, Option<u64>);

fn write_error<'a>(context: &str, path: &'a Path) -> impl Fn(zip::result::ZipError) -> ForgeError + 'a {
    let context = context.to_string();
    move |error| ForgeError::new(ErrorKind::PreparationFailed, format!("{context} {}: {error}", path.display()))
}

/// Writes the backup of `inventory` to `request.destination` while no game
/// and no other Forge runs: a temporary zip beside it, re-read and verified
/// member by member against its manifest, then renamed into place.
pub fn create_backup(
    inventory: &Inventory,
    request: &BackupRequest,
    progress: Progress<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    // Refuse unknown/ambiguous title identity before even creating a lock.
    backup_target(inventory).map_err(|error| error.during(Operation::Backup))?;
    let os = os_of(inventory.roots.platform);
    let _quiet = crate::session::quiesce(&inventory.roots.activation, os, Operation::Backup)?;
    let plan = backup_plan(inventory, &request.optional, request.touch_preferences.as_ref())
        .map_err(|error| error.during(Operation::Backup))?;
    let destination = &request.destination;
    let name = destination.file_name().ok_or_else(|| ForgeError::invalid("The backup needs a file name"))?;
    let partial = destination.with_file_name(format!(".{}.partial", name.to_string_lossy()));
    let outcome = write_archive(inventory, &plan, request, &partial, progress, cancel)
        .and_then(|manifest| {
            let opened = open_backup_for_game(&partial, &plan.game, None, cancel)
                .map_err(|error| ForgeError::integrity(format!("The new backup did not verify: {}", error.message)))?;
            if opened.manifest.entries.len() != manifest.entries.len() {
                return Err(ForgeError::integrity("The new backup did not verify: its entries differ"));
            }
            fsutil::replace_file(&partial, destination)
                .map_err(|error| file_error("Cannot save", destination, error))?;
            Ok(manifest)
        })
        .map_err(|error| error.during(Operation::Backup));
    let manifest = match outcome {
        Ok(manifest) => manifest,
        Err(error) => {
            let _ = std::fs::remove_file(&partial);
            return Err(error);
        }
    };
    let bytes = std::fs::metadata(destination).map(|metadata| metadata.len()).unwrap_or(0);
    Ok(json!({
        "format": RESULT_FORMAT,
        "game": manifest.game,
        "path": destination.to_string_lossy(),
        "bytes": bytes,
        "files": manifest.entries.len(),
        "content_bytes": manifest.entries.iter().map(|entry| entry.size).sum::<u64>(),
        "units": manifest.units.iter().map(|unit| unit.name.clone()).collect::<Vec<_>>(),
    }))
}

fn write_archive(
    inventory: &Inventory,
    plan: &BackupPlan,
    request: &BackupRequest,
    partial: &Path,
    progress: Progress<'_>,
    cancel: &CancelToken,
) -> Result<Manifest> {
    if let Some(parent) = partial.parent().filter(|parent| !parent.as_os_str().is_empty()) {
        if !parent.is_dir() {
            return Err(ForgeError::invalid(format!("The backup folder does not exist: {}", parent.display())));
        }
    }
    let file = File::create(partial).map_err(|error| file_error("Cannot create", partial, error))?;
    let mut writer = zip::ZipWriter::new(file);
    let failed = write_error("Cannot write the backup", partial);
    let total = plan.bytes();
    let mut done = 0u64;
    let mut entries = Vec::new();
    let mut units = Vec::new();
    for unit in &plan.units {
        units.push(ManifestUnit {
            name: unit.name.to_string(),
            category: unit.category.code().to_string(),
            kind: unit.kind,
            origin: unit.origin.code().to_string(),
            managed: unit.managed,
            source: unit.path.to_string_lossy().into_owned(),
        });
        if unit.kind == UnitKind::Json {
            let preferences = request.touch_preferences.clone().unwrap_or_default();
            let bytes = forge_base::legacy_json::pretty_file_bytes(&Value::Object(preferences));
            let options = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
            writer.start_file(member_name(unit.name, ""), options).map_err(&failed)?;
            let (size, digest) = copy_hashing(&mut bytes.as_slice(), &mut writer, u64::MAX, cancel)?;
            entries.push(ManifestEntry { unit: unit.name.into(), path: String::new(), size, sha256: hex(&digest) });
            continue;
        }
        for item in &unit.files {
            cancel.check()?;
            let options = zip::write::SimpleFileOptions::default()
                .compression_method(zip::CompressionMethod::Deflated)
                .large_file(item.size >= u64::from(u32::MAX));
            writer.start_file(member_name(unit.name, &item.relative), options).map_err(&failed)?;
            let mut input = File::open(&item.path).map_err(|error| file_error("Cannot read", &item.path, error))?;
            let (size, digest) = copy_hashing(&mut input, &mut writer, u64::MAX, cancel)?;
            done += size;
            progress("backup", done, Some(total));
            entries.push(ManifestEntry {
                unit: unit.name.into(),
                path: item.relative.clone(),
                size,
                sha256: hex(&digest),
            });
        }
    }
    let manifest = Manifest {
        created: crate::log::timestamp(std::time::SystemTime::now()),
        engine_version: env!("CARGO_PKG_VERSION").to_string(),
        package_version: crate::state::package_version(&inventory.roots.package),
        game: plan.game.clone(),
        recipe: installed_recipe(&inventory.roots.data),
        platform: inventory.roots.platform,
        units,
        entries,
    };
    let options = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
    writer.start_file(MANIFEST, options).map_err(&failed)?;
    std::io::Write::write_all(&mut writer, &forge_base::legacy_json::pretty_file_bytes(&manifest.to_value()))
        .map_err(|error| ForgeError::io("Cannot write the backup manifest", error))?;
    let file = writer.finish().map_err(&failed)?;
    file.sync_all().map_err(|error| ForgeError::io(format!("Cannot flush {}", partial.display()), error))?;
    Ok(manifest)
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ManifestUnit {
    pub name: String,
    pub category: String,
    pub kind: UnitKind,
    pub origin: String,
    pub managed: bool,
    pub source: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ManifestEntry {
    pub unit: String,
    pub path: String,
    pub size: u64,
    pub sha256: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Manifest {
    pub created: String,
    pub engine_version: String,
    pub package_version: Option<String>,
    pub game: String,
    pub recipe: Option<String>,
    pub platform: Platform,
    pub units: Vec<ManifestUnit>,
    pub entries: Vec<ManifestEntry>,
}

impl Manifest {
    pub fn to_value(&self) -> Value {
        json!({
            "format": FORMAT,
            "schema": SCHEMA,
            "created": self.created,
            "engine_version": self.engine_version,
            "package_version": self.package_version,
            "game": self.game,
            "recipe": self.recipe,
            "platform": self.platform.code(),
            "units": self.units.iter().map(|unit| json!({
                "name": unit.name, "category": unit.category, "kind": unit.kind.code(),
                "origin": unit.origin, "managed": unit.managed, "source": unit.source,
            })).collect::<Vec<_>>(),
            "entries": self.entries.iter().map(|entry| json!({
                "unit": entry.unit, "path": entry.path, "size": entry.size, "sha256": entry.sha256,
            })).collect::<Vec<_>>(),
        })
    }

    /// The summary a preview shows.
    pub fn summary(&self) -> Value {
        json!({
            "created": self.created,
            "engine_version": self.engine_version,
            "package_version": self.package_version,
            "game": self.game,
            "recipe": self.recipe,
            "platform": self.platform.code(),
            "files": self.entries.len(),
            "bytes": self.entries.iter().map(|entry| entry.size).sum::<u64>(),
            "units": self.units.iter().map(|unit| unit.name.clone()).collect::<Vec<_>>(),
        })
    }

    pub fn unit(&self, name: &str) -> Option<&ManifestUnit> {
        self.units.iter().find(|unit| unit.name == name)
    }

    pub fn entries_of<'a>(&'a self, unit: &'a str) -> impl Iterator<Item = &'a ManifestEntry> + 'a {
        self.entries.iter().filter(move |entry| entry.unit == unit)
    }
}

/// A backup whose every member was verified.
#[derive(Clone, Debug)]
pub struct OpenedBackup {
    pub path: PathBuf,
    pub manifest: Manifest,
}

fn refused(kind: ErrorKind, message: impl Into<String>) -> ForgeError {
    ForgeError::new(kind, message).during(Operation::Restore).about(Subject::SelectedInput)
}

fn damaged(message: impl Into<String>) -> ForgeError {
    refused(ErrorKind::ArtifactIntegrity, message).because(Reason::InvalidBackup)
}

fn not_a_backup(message: impl Into<String>) -> ForgeError {
    refused(ErrorKind::UnsupportedFormat, message).because(Reason::InvalidBackup)
}

/// Windows device names are not file names anywhere a backup may land.
fn reserved_name(component: &str) -> bool {
    let stem = component.split('.').next().unwrap_or("").to_ascii_uppercase();
    matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
        || (stem.len() == 4
            && (stem.starts_with("COM") || stem.starts_with("LPT"))
            && stem.as_bytes()[3].is_ascii_digit())
}

/// A relative POSIX path of plain components that stays in its folder on
/// every platform.
pub fn safe_relative(path: &str) -> bool {
    !path.is_empty()
        && path.len() <= MAX_PATH
        && path.split('/').all(|component| {
            !component.is_empty()
                && component.len() <= 255
                && component != "."
                && component != ".."
                && !component.ends_with(['.', ' '])
                && !reserved_name(component)
                && !component.chars().any(|c| c.is_control() || "\\:*?\"<>|".contains(c))
        })
}

fn unit_name_ok(name: &str) -> bool {
    !name.is_empty() && name.len() <= 64 && name.bytes().all(|byte| byte.is_ascii_lowercase() || byte == b'_')
}

fn text<'a>(map: &'a Map<String, Value>, key: &str) -> Result<&'a str> {
    map.get(key).and_then(Value::as_str).ok_or_else(|| damaged(format!("The backup manifest has no {key}")))
}

fn optional_text(map: &Map<String, Value>, key: &str) -> Result<Option<String>> {
    match map.get(key) {
        None | Some(Value::Null) => Ok(None),
        Some(Value::String(text)) => Ok(Some(text.clone())),
        Some(_) => Err(damaged(format!("The backup manifest's {key} is not text"))),
    }
}

fn parse_manifest(bytes: &[u8], expected_game: &str, supported_recipes: Option<&[String]>) -> Result<Manifest> {
    let manifest = forge_base::legacy_json::load_object(bytes)
        .map_err(|error| damaged(format!("The backup manifest is not readable: {error}")))?;
    if manifest.get("format").and_then(Value::as_str) != Some(FORMAT) {
        return Err(not_a_backup("This file is not a TriAevum backup"));
    }
    match manifest.get("schema").and_then(Value::as_u64) {
        Some(SCHEMA) => {}
        Some(schema) if schema > SCHEMA => {
            return Err(refused(
                ErrorKind::UnsupportedFormat,
                format!("This backup was made by a newer Forge (schema {schema})"),
            )
            .because(Reason::NewerSchema))
        }
        _ => return Err(damaged("The backup manifest has no valid schema")),
    }
    let game = text(&manifest, "game")?;
    if game != expected_game {
        return Err(refused(ErrorKind::IncompatibleTarget, format!("This backup belongs to another game ({game})"))
            .because(Reason::OtherGame));
    }
    let recipe = optional_text(&manifest, "recipe")?;
    if let (Some(recipe), Some(supported)) = (&recipe, supported_recipes) {
        if !supported.iter().any(|id| id == recipe) {
            return Err(refused(
                ErrorKind::IncompatibleTarget,
                format!("This backup belongs to a game revision this package does not support ({recipe})"),
            )
            .because(Reason::OtherGame));
        }
    }
    let platform = platform_from_code(text(&manifest, "platform")?)
        .ok_or_else(|| damaged("The backup manifest names an unknown platform"))?;
    let mut units = Vec::new();
    let mut unit_kinds = BTreeMap::new();
    for unit in manifest.get("units").and_then(Value::as_array).ok_or_else(|| damaged("The backup has no units"))? {
        let unit = unit.as_object().ok_or_else(|| damaged("A backup unit is not an object"))?;
        let name = text(unit, "name")?;
        let kind =
            UnitKind::from_code(text(unit, "kind")?).ok_or_else(|| damaged("A backup unit has an unknown kind"))?;
        if !unit_name_ok(name) || unit_kinds.insert(name.to_string(), kind).is_some() {
            return Err(damaged(format!("The backup names the unit {name:?} badly or twice")));
        }
        units.push(ManifestUnit {
            name: name.to_string(),
            category: text(unit, "category")?.to_string(),
            kind,
            origin: text(unit, "origin")?.to_string(),
            managed: unit.get("managed").and_then(Value::as_bool).unwrap_or(false),
            source: text(unit, "source")?.to_string(),
        });
    }
    let listed =
        manifest.get("entries").and_then(Value::as_array).ok_or_else(|| damaged("The backup has no entries"))?;
    if listed.len() > MAX_ENTRIES {
        return Err(damaged("The backup lists too many files"));
    }
    let mut entries = Vec::new();
    let mut seen = HashSet::new();
    for entry in listed {
        let entry = entry.as_object().ok_or_else(|| damaged("A backup entry is not an object"))?;
        let unit = text(entry, "unit")?;
        let path = text(entry, "path")?;
        let kind = unit_kinds.get(unit).ok_or_else(|| damaged(format!("A backup entry names no unit: {unit}")))?;
        let shaped = match kind {
            UnitKind::Directory => safe_relative(path),
            UnitKind::File | UnitKind::Json => path.is_empty(),
        };
        if !shaped {
            return Err(damaged(format!("The backup names an unsafe path: {unit}/{path}")));
        }
        // Case-insensitive file systems would merge two such names.
        if !seen.insert(member_name(unit, path).to_lowercase()) {
            return Err(damaged(format!("The backup lists a file twice: {unit}/{path}")));
        }
        let size = entry.get("size").and_then(Value::as_u64).ok_or_else(|| damaged("A backup entry has no size"))?;
        let sha256 = text(entry, "sha256")?;
        if !is_sha256_hex(sha256) {
            return Err(damaged("A backup entry has no valid SHA-256"));
        }
        entries.push(ManifestEntry { unit: unit.into(), path: path.into(), size, sha256: sha256.into() });
    }
    Ok(Manifest {
        created: text(&manifest, "created")?.to_string(),
        engine_version: text(&manifest, "engine_version")?.to_string(),
        package_version: optional_text(&manifest, "package_version")?,
        game: game.to_string(),
        recipe,
        platform,
        units,
        entries,
    })
}

type Archive = zip::ZipArchive<File>;

pub(crate) fn open_archive(path: &Path) -> Result<Archive> {
    let file = File::open(path).map_err(|error| ForgeError::io(format!("Cannot open {}", path.display()), error))?;
    zip::ZipArchive::new(file).map_err(|_| not_a_backup(format!("{} is not a zip backup", path.display())))
}

/// Streams one member into `writer` and checks it against its entry.
pub(crate) fn copy_member(
    archive: &mut Archive,
    entry: &ManifestEntry,
    writer: &mut dyn std::io::Write,
    cancel: &CancelToken,
) -> Result<()> {
    let name = member_name(&entry.unit, &entry.path);
    let mut member = archive.by_name(&name).map_err(|_| damaged(format!("The backup has no member {name}")))?;
    if member.is_dir() || member.is_symlink() {
        return Err(damaged(format!("The backup member {name} is not a regular file")));
    }
    // One byte past the declared size detects a member larger than listed.
    let mut limited = (&mut member).take(entry.size + 1);
    let (size, digest) = copy_hashing(&mut limited, &mut WriteAdapter(writer), entry.size + 1, cancel)?;
    if size != entry.size || hex(&digest) != entry.sha256 {
        return Err(damaged(format!("The backup member {name} does not match its manifest")));
    }
    Ok(())
}

struct WriteAdapter<'a>(&'a mut dyn std::io::Write);

impl std::io::Write for WriteAdapter<'_> {
    fn write(&mut self, buffer: &[u8]) -> std::io::Result<usize> {
        self.0.write(buffer)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.0.flush()
    }
}

/// Opens and verifies a backup: manifest, names, members, sizes and hashes.
/// `supported_recipes` (the package's revisions) refuses a backup of a
/// revision this package cannot play. Reads only.
pub fn open_backup(path: &Path, supported_recipes: Option<&[String]>, cancel: &CancelToken) -> Result<OpenedBackup> {
    open_backup_for_game(path, GAME, supported_recipes, cancel)
}

/// Verifies a backup against a game supplied by the destination's recipe
/// contract, never by the archive itself. The legacy API above stays OoT-only.
pub fn open_backup_for_game(
    path: &Path,
    expected_game: &str,
    supported_recipes: Option<&[String]>,
    cancel: &CancelToken,
) -> Result<OpenedBackup> {
    if !game_key_ok(expected_game) {
        return Err(ForgeError::invalid("The backup target has no valid game identity"));
    }
    let mut archive = open_archive(path)?;
    let mut names = HashSet::new();
    for index in 0..archive.len() {
        let name = archive
            .name_for_index(index)
            .ok_or_else(|| damaged("The backup's zip directory is damaged"))?
            .map_err(|_| damaged("The backup holds a file name that cannot be read"))?
            .into_owned();
        if !names.insert(name.clone()) {
            return Err(damaged(format!("The backup holds {name} twice")));
        }
    }
    if !names.contains(MANIFEST) {
        return Err(not_a_backup("This file is not a TriAevum backup (no manifest)"));
    }
    let mut bytes = Vec::new();
    {
        let manifest = archive.by_name(MANIFEST).map_err(|_| damaged("The backup manifest cannot be read"))?;
        manifest
            .take(64 << 20)
            .read_to_end(&mut bytes)
            .map_err(|error| damaged(format!("The backup manifest cannot be read: {error}")))?;
    }
    let manifest = parse_manifest(&bytes, expected_game, supported_recipes)?;
    let mut expected: HashSet<String> =
        manifest.entries.iter().map(|entry| member_name(&entry.unit, &entry.path)).collect();
    expected.insert(MANIFEST.to_string());
    if let Some(extra) = names.iter().find(|name| !expected.contains(*name)) {
        return Err(damaged(format!("The backup holds a file its manifest does not list: {extra}")));
    }
    if expected.len() != names.len() {
        return Err(damaged("The backup misses files its manifest lists"));
    }
    for entry in &manifest.entries {
        copy_member(&mut archive, entry, &mut std::io::sink(), cancel)?;
    }
    Ok(OpenedBackup { path: path.to_path_buf(), manifest })
}

/// The game and only its supported revisions, resolved independently of an
/// archive. One data root belongs to one active game. A mixed-title package
/// without an active revision is ambiguous, not permission to restore either.
pub(crate) struct BackupTarget {
    pub game: String,
    pub recipes: Vec<String>,
}

fn game_key_ok(game: &str) -> bool {
    !game.is_empty() && game.len() <= 64
        && game.bytes().all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
}

pub(crate) fn backup_target(inventory: &Inventory) -> Result<BackupTarget> {
    let invalid = |message: &str| ForgeError::new(ErrorKind::IncompatibleTarget, message)
        .because(Reason::OtherGame);
    let recipes = forge_catalog::recipes::load_recipes(&crate::install::recipes_path(&inventory.roots.package))?;
    // The old schema was exclusively OoT. Preserve that contract only for
    // entirely legacy catalogs; partial annotation in a new catalog is unsafe.
    let explicit = recipes.iter().any(|recipe| recipe.get("game").is_some());
    let mut games = BTreeMap::new();
    for recipe in &recipes {
        let id = recipe.get("id").and_then(Value::as_str).filter(|id| !id.is_empty())
            .ok_or_else(|| invalid("A backup recipe has no revision identity"))?;
        let game = if explicit {
            recipe.get("game").and_then(Value::as_str)
                .ok_or_else(|| invalid("Every recipe in a title-aware catalog must declare its game"))?
        } else {
            GAME
        };
        if !game_key_ok(game) || games.insert(id.to_string(), game.to_string()).is_some() {
            return Err(invalid("The backup recipe catalog has an invalid game or duplicate revision"));
        }
    }
    let active_path = inventory.roots.data.join("active-title.json");
    let game = if active_path.try_exists().map_err(|error| file_error("Cannot inspect", &active_path, error))? {
        let active = load_json_object(&active_path)?;
        if active.get("format").and_then(Value::as_str) != Some(crate::active_title::ACTIVE_TITLE_FORMAT) {
            return Err(invalid("The active title has no valid identity"));
        }
        let id = active.get("recipe").and_then(Value::as_str)
            .ok_or_else(|| invalid("The active title has no revision identity"))?;
        let game = games.get(id).ok_or_else(|| invalid("The active revision is absent from the package's recipes"))?;
        if active.get("game").is_some_and(|value| value.as_str() != Some(game.as_str())) {
            return Err(invalid("The active title and its recipe disagree about the game"));
        }
        game.clone()
    } else {
        let unique: HashSet<_> = games.values().collect();
        if unique.len() != 1 {
            return Err(invalid("Backup recovery needs an active revision or an unambiguous single-game package"));
        }
        (*unique.into_iter().next().expect("one game")).clone()
    };
    let recipes = games.into_iter().filter_map(|(id, owner)| (owner == game).then_some(id)).collect();
    Ok(BackupTarget { game, recipes })
}

/// Checks even a directly supplied restore plan against the installation.
pub(crate) fn check_backup_target(inventory: &Inventory, manifest: &Manifest) -> Result<()> {
    let target = backup_target(inventory)?;
    if manifest.game != target.game || manifest.recipe.as_ref().is_some_and(|id| !target.recipes.contains(id)) {
        return Err(refused(ErrorKind::IncompatibleTarget, "This backup belongs to another game or unsupported revision")
            .because(Reason::OtherGame));
    }
    Ok(())
}

pub(crate) fn open_installation_backup(inventory: &Inventory, path: &Path, cancel: &CancelToken) -> Result<OpenedBackup> {
    let target = backup_target(inventory)?;
    open_backup_for_game(path, &target.game, Some(&target.recipes), cancel)
}

/// The revisions the package at `package` supports.
pub fn supported_recipes(package: &Path) -> Option<Vec<String>> {
    let options = forge_catalog::recipes::load_recipe_options(&crate::install::recipes_path(package)).ok()?;
    Some(options.into_iter().map(|option| option.id).collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn relative_names_stay_in_their_folder() {
        for good in ["save.bin", "system/save00.bin", "a b/c.json", "\u{e9}t\u{e9}.bin"] {
            assert!(safe_relative(good), "{good}");
        }
        for bad in [
            "", "/abs", "../up", "a/../b", "a//b", "./a", "C:/x", "a\\b", "con", "nul.txt", "COM1", "lpt9.bin",
            "trail.", "trail ", "x\u{0}y", "a/\u{7}",
        ] {
            assert!(!safe_relative(bad), "{bad:?}");
        }
        assert!(safe_relative("console.json") && safe_relative("com10"));
    }

    #[test]
    fn stamps_are_sortable_folder_names() {
        let at = std::time::UNIX_EPOCH + std::time::Duration::from_secs(1_793_016_000);
        assert_eq!(stamp(at), "20261026-120000");
    }
}
