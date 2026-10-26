// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Data & backups actions besides backup and restore (UX plan WP-05B):
//! clearing the renderer cache, removing the game data prepared from the
//! ROM, rebuilding it from the kept ROM data, and the uninstall help; plus
//! the jobs of the page (`data_plan`, `backup`, `restore_preview`,
//! `restore`, `restore_recover`, `clear_renderer_cache`,
//! `remove_prepared_game`; `rebuild_prepared_assets` is in `jobs.rs`, which
//! owns the native services).
//!
//! Deletion rules: only entries that are inside the installation's own
//! folders (`managed`) and not chosen by the user (`origin != explicit`)
//! are ever deleted, always after a preview of the same plan, and never the
//! user's saves, settings, mods or downloads (decision 2).

use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_json, file_error, load_json_object};
use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result};
use forge_native::NativeServices;
use forge_prepare::content::load_prepared_content;
use forge_prepare::paths::resolve_reference;
use serde_json::{json, Map, Value};

use crate::abi::Job;
use crate::backup::{ancestors, backup_plan, create_backup, installed_recipe, os_of, BackupRequest};
use crate::install::{install_private_title, InstallRequest, InstallSource, InstallTarget};
use crate::inventory::{inventory_of, Category, Entry, Inventory, InventoryRequest, Origin, Platform};
use crate::layout::HostOs;
use crate::log::ForgeLog;
use crate::restore::{pending_restore, recover_pending, restore, restore_preview, RestoreOptions};

pub const PLAN_FORMAT: &str = "triaevum_data_actions_v1";
/// Written by "Remove prepared game": the installation then needs the ROM.
pub const REMOVED_RECORD: &str = "prepared-game-removed.json";
pub const REMOVED_FORMAT: &str = "triaevum_prepared_game_removed_v1";
const SOURCE_FILES: [&str; 3] = ["code.bin", "exheader.bin", "romfs.bin"];

/// Locations an action would delete, and those it leaves alone.
#[derive(Clone, Debug, Default)]
pub struct Deletion {
    /// (logical name, path, bytes, files)
    pub targets: Vec<(&'static str, PathBuf, u64, u64)>,
    /// (logical name, path, reason) present locations not deleted.
    pub kept: Vec<(&'static str, PathBuf, &'static str)>,
}

impl Deletion {
    pub fn bytes(&self) -> u64 {
        self.targets.iter().map(|(_, _, bytes, _)| bytes).sum()
    }

    pub fn to_value(&self) -> Value {
        json!({
            "bytes": self.bytes(),
            "targets": self.targets.iter().map(|(name, path, bytes, files)| json!({
                "name": name, "path": path.to_string_lossy(), "bytes": bytes, "files": files,
            })).collect::<Vec<_>>(),
            "kept": self.kept.iter().map(|(name, path, reason)| json!({
                "name": name, "path": path.to_string_lossy(), "reason": reason,
            })).collect::<Vec<_>>(),
        })
    }
}

fn consider(deletion: &mut Deletion, entry: &Entry) {
    if !entry.present {
        return;
    }
    if !entry.managed {
        deletion.kept.push((entry.logical_name, entry.resolved_path.clone(), "outside_installation"));
    } else if entry.origin == Origin::Explicit {
        deletion.kept.push((entry.logical_name, entry.resolved_path.clone(), "chosen_by_user"));
    } else {
        deletion.targets.push((entry.logical_name, entry.resolved_path.clone(), entry.size, entry.files));
    }
}

/// What "Clear renderer cache" deletes.
pub fn renderer_cache_plan(inventory: &Inventory) -> Deletion {
    let mut deletion = Deletion::default();
    if let Some(entry) = inventory.entry("renderer_cache") {
        consider(&mut deletion, entry);
    }
    deletion
}

/// What "Remove prepared game" deletes: the outermost generated folders and
/// the active-title record.
pub fn prepared_game_plan(inventory: &Inventory) -> Deletion {
    let mut deletion = Deletion::default();
    for entry in &inventory.entries {
        let generated = entry.category == Category::Generated;
        let nested = ancestors(inventory, entry).iter().any(|above| above.category == Category::Generated);
        if (generated && !nested) || entry.logical_name == "active_title" {
            consider(&mut deletion, entry);
        }
    }
    deletion
}

fn delete(deletion: &Deletion) -> Result<()> {
    for (_, path, _, _) in &deletion.targets {
        let result = if path.is_dir() { std::fs::remove_dir_all(path) } else { forge_base::fsutil::remove_file(path) };
        match result {
            Ok(()) => {}
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
            Err(error) => return Err(file_error("Cannot remove", path, error).during(Operation::Cleanup)),
        }
    }
    Ok(())
}

/// Deletes the renderer cache (disposable: the runtime rebuilds it).
pub fn clear_renderer_cache(inventory: &Inventory) -> Result<Value> {
    let _quiet =
        crate::session::quiesce(&inventory.roots.activation, os_of(inventory.roots.platform), Operation::Cleanup)?;
    let deletion = renderer_cache_plan(inventory);
    delete(&deletion)?;
    Ok(json!({"cleared": deletion.to_value()}))
}

/// Deletes the game data prepared from the ROM; saves, settings, mods and
/// downloads stay. The installation then needs the ROM again
/// (`NeedsGameInput`).
pub fn remove_prepared_game(inventory: &Inventory) -> Result<Value> {
    let _quiet =
        crate::session::quiesce(&inventory.roots.activation, os_of(inventory.roots.platform), Operation::Cleanup)?;
    let deletion = prepared_game_plan(inventory);
    let data = &inventory.roots.data;
    let recipe = installed_recipe(data);
    if recipe.is_some() {
        let record = json!({
            "format": REMOVED_FORMAT,
            "recipe": recipe,
            "removed": crate::log::timestamp(std::time::SystemTime::now()),
            "bytes": deletion.bytes(),
        });
        atomic_write_json(&data.join(REMOVED_RECORD), &record).map_err(|error| error.during(Operation::Cleanup))?;
    }
    // The active title first: from here on Play is not offered.
    let (title, rest): (Vec<_>, Vec<_>) =
        deletion.targets.iter().cloned().partition(|(name, _, _, _)| *name == "active_title");
    delete(&Deletion { targets: title, kept: Vec::new() })?;
    delete(&Deletion { targets: rest, kept: Vec::new() })?;
    Ok(json!({"removed": deletion.to_value(), "recipe": recipe}))
}

/// The recipe of a removed prepared game, when its record is present.
pub fn removed_recipe(data_root: &Path) -> Option<String> {
    let record = load_json_object(&data_root.join(REMOVED_RECORD)).ok()?;
    if record.get("format").and_then(Value::as_str) != Some(REMOVED_FORMAT) {
        return None;
    }
    record.get("recipe")?.as_str().map(str::to_string)
}

/// The folder of the ROM data kept for repairs (`sources/<identity>`): the
/// active title's, else the most recent complete one.
pub fn retained_source(data_root: &Path) -> Option<PathBuf> {
    let complete = |directory: &Path| SOURCE_FILES.iter().all(|name| directory.join(name).is_file());
    let from_title = (|| {
        let active = load_json_object(&data_root.join("active-title.json")).ok()?;
        let directory = resolve_reference(active.get("directory")?.as_str()?, data_root).ok()?;
        let prepared = load_prepared_content(&directory, &[], &CancelToken::new()).ok()?;
        let code = prepared.index.get("inputs")?.get("code")?.get("path")?.as_str()?;
        let code = resolve_reference(code, &prepared.directory).ok()?;
        code.parent().map(Path::to_path_buf)
    })();
    if let Some(directory) = from_title.filter(|directory| complete(directory)) {
        return Some(directory);
    }
    let mut found: Vec<(std::time::SystemTime, PathBuf)> = std::fs::read_dir(data_root.join("sources"))
        .ok()?
        .flatten()
        .map(|child| child.path())
        .filter(|path| path.is_dir() && complete(path))
        .map(|path| (std::fs::metadata(&path).and_then(|m| m.modified()).unwrap_or(std::time::UNIX_EPOCH), path))
        .collect();
    found.sort();
    found.pop().map(|(_, path)| path)
}

fn source_bytes(directory: &Path) -> u64 {
    SOURCE_FILES.iter().filter_map(|name| std::fs::metadata(directory.join(name)).ok()).map(|m| m.len()).sum()
}

/// "Rebuild prepared assets": the install pipeline run again from the kept
/// ROM data (`sources/`), so the title, assets and receipts are rebuilt and
/// activated together. Until UX plan WP-04A lands its repair, this is the
/// repair. The game session lock is held throughout; the install takes the
/// installation lock itself.
pub fn rebuild_prepared_assets(
    package_root: &Path,
    data_root: Option<&Path>,
    target: InstallTarget,
    services: &dyn NativeServices,
    report: &mut dyn FnMut(&str, &str),
    cancel: &CancelToken,
) -> Result<Value> {
    let (package_root, data) = crate::install::installation_paths(package_root, data_root, &target)?;
    let activation = match &target {
        InstallTarget::Package => crate::install::package_layout(&package_root)?.activation,
        InstallTarget::Android(android) => android.profile_root.clone(),
    };
    let lock = crate::session::session_lock_path(&activation, HostOs::current());
    if let Some(parent) = lock.parent() {
        std::fs::create_dir_all(parent).map_err(|error| file_error("Cannot create", parent, error))?;
    }
    let _game = crate::lock::acquire(&lock).map_err(|error| match error.kind {
        ErrorKind::Busy => {
            ForgeError::new(ErrorKind::Busy, format!("TriAevum is running from this installation ({})", lock.display()))
                .with_path(&lock)
                .because(Reason::GameRunning)
        }
        _ => error,
    })?;
    let Some(source) = retained_source(&data) else {
        return Err(ForgeError::invalid("The game data kept from your ROM is missing")
            .during(Operation::Repair)
            .because(Reason::MissingRetainedSource));
    };
    let request = InstallRequest {
        source: InstallSource::ExtractedDirectory(source),
        data_root: Some(data),
        package_root,
        target,
        game_language: None,
    };
    install_private_title(request, services, report, cancel).map_err(|error| error.during(Operation::Repair))
}

/// The uninstall story of section 3.2, from the inventory: what to delete,
/// what Forge never deletes (the user's own locations outside it) and what
/// to keep (mods and downloads, decision 2).
pub fn uninstall_help(inventory: &Inventory) -> Value {
    let roots = &inventory.roots;
    let external: Vec<Value> = inventory
        .entries
        .iter()
        .filter(|entry| entry.present && !entry.managed && entry.category != Category::Generated)
        .map(|entry| json!({"name": entry.logical_name, "path": entry.resolved_path.to_string_lossy()}))
        .collect();
    let keep: Vec<Value> = ["mods", "downloads"]
        .iter()
        .filter_map(|name| inventory.entry(name))
        .filter(|entry| entry.present)
        .map(|entry| json!({"name": entry.logical_name, "path": entry.resolved_path.to_string_lossy()}))
        .collect();
    let mut help = json!({
        "platform": roots.platform.code(),
        "package": roots.package.to_string_lossy(),
        "activation": roots.activation.to_string_lossy(),
        "data": roots.data.to_string_lossy(),
        "external": external,
        "keep": keep,
    });
    if roots.platform == Platform::Flatpak {
        help["flatpak_id"] = json!(crate::layout::FLATPAK_ID);
    }
    help
}

/// Everything the page previews: the backup set (with `chosen` optional
/// entries), the deletions, the rebuild source and the uninstall help.
pub fn data_actions(inventory: &Inventory, chosen: &[String]) -> Result<Value> {
    let data = &inventory.roots.data;
    let source = retained_source(data);
    let mut removal = prepared_game_plan(inventory).to_value();
    removal["recipe"] = json!(installed_recipe(data));
    Ok(json!({
        "format": PLAN_FORMAT,
        "backup": backup_plan(inventory, chosen, None)?.to_value(),
        "renderer_cache": renderer_cache_plan(inventory).to_value(),
        "prepared_game": removal,
        "rebuild": {
            "source": source.as_ref().map(|path| path.to_string_lossy().into_owned()),
            "bytes": source.as_deref().map(source_bytes).unwrap_or(0),
        },
        "uninstall": uninstall_help(inventory),
        "pending_restore": pending_restore(data).map(|path| path.to_string_lossy().into_owned()),
    }))
}

// ---- Jobs ----

fn inventory_request(request: &Map<String, Value>, sizes: bool) -> Result<InventoryRequest> {
    let mut fields = request.clone();
    fields.insert("format".into(), json!(crate::inventory::REQUEST_FORMAT));
    fields.insert("sizes".into(), json!(sizes));
    InventoryRequest::parse(&fields)
}

fn path_field(request: &Map<String, Value>, key: &str) -> Result<PathBuf> {
    request
        .get(key)
        .and_then(Value::as_str)
        .filter(|text| !text.is_empty())
        .map(PathBuf::from)
        .ok_or_else(|| ForgeError::invalid(format!("{key} must be a path")))
}

fn names_field(request: &Map<String, Value>, key: &str) -> Result<Option<Vec<String>>> {
    match request.get(key) {
        None | Some(Value::Null) => Ok(None),
        Some(Value::Array(items)) => items
            .iter()
            .map(|item| {
                item.as_str().map(str::to_string).ok_or_else(|| ForgeError::invalid(format!("{key} must list names")))
            })
            .collect::<Result<Vec<_>>>()
            .map(Some),
        Some(_) => Err(ForgeError::invalid(format!("{key} must list names"))),
    }
}

/// The Data & backups jobs. Requests take the inventory request's fields
/// (`package_root?`, `data_root?`, `android?`) plus:
///
/// - `data_plan`: `optional?` -> [`data_actions`];
/// - `backup`: `destination`, `optional?`, `touch_preferences?` (object);
/// - `restore_preview`, `restore`: `backup`, `units?`;
/// - `restore_recover`, `clear_renderer_cache`, `remove_prepared_game`.
pub(crate) fn start(job: &str, request: &Map<String, Value>) -> Result<Option<Job>> {
    let sizes = matches!(job, "data_plan" | "clear_renderer_cache" | "remove_prepared_game");
    if !matches!(
        job,
        "data_plan"
            | "backup"
            | "restore_preview"
            | "restore"
            | "restore_recover"
            | "clear_renderer_cache"
            | "remove_prepared_game"
    ) {
        return Ok(None);
    }
    let target = inventory_request(request, sizes)?;
    let chosen = names_field(request, "optional")?.unwrap_or_default();
    let units = names_field(request, "units")?;
    let backup = match job {
        "restore_preview" | "restore" => Some(path_field(request, "backup")?),
        _ => None,
    };
    let backup_request = match job {
        "backup" => Some(BackupRequest {
            destination: path_field(request, "destination")?,
            optional: chosen.clone(),
            touch_preferences: match request.get("touch_preferences") {
                None | Some(Value::Null) => None,
                Some(Value::Object(object)) => Some(object.clone()),
                Some(_) => return Err(ForgeError::invalid("touch_preferences must be an object")),
            },
        }),
        _ => None,
    };
    let job = job.to_string();
    let log = inventory_of(&inventory_request(request, false)?)
        .ok()
        .map(|inventory| ForgeLog::new(&inventory.roots.data, &job));
    Ok(Some(Job::spawn_logged(log, move |sink, cancel| {
        let operation = match job.as_str() {
            "backup" => Operation::Backup,
            "restore_preview" | "restore" | "restore_recover" => Operation::Restore,
            "data_plan" => Operation::Inspect,
            _ => Operation::Cleanup,
        };
        let inventory = inventory_of(&target).map_err(|error| error.during(operation))?;
        let mut progress = |stage: &str, done: u64, total: Option<u64>| sink.progress(stage, done, total);
        let options = RestoreOptions { units, abort_after_category: None };
        match job.as_str() {
            "data_plan" => data_actions(&inventory, &chosen),
            "backup" => {
                sink.stage("backup", "Backing up your files...");
                create_backup(&inventory, backup_request.as_ref().expect("parsed"), &mut progress, cancel)
            }
            "restore_preview" => {
                sink.stage("verify", "Checking the backup...");
                restore_preview(&inventory, backup.as_deref().expect("parsed"), &options, cancel)
                    .map(|plan| plan.to_value())
            }
            "restore" => {
                sink.stage("restore", "Restoring your files...");
                restore(&inventory, backup.as_deref().expect("parsed"), &options, &mut progress, cancel)
            }
            "restore_recover" => {
                let os = os_of(inventory.roots.platform);
                let settled = recover_pending(&inventory.roots.activation, os, &inventory.roots.data, cancel)?;
                Ok(json!({"recovered": settled}))
            }
            "clear_renderer_cache" => clear_renderer_cache(&inventory),
            _ => remove_prepared_game(&inventory),
        }
    })))
}
