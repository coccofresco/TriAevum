// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Enhancements page's engine (UX plan WP-07): what each enhancement is
//! (downloaded or imported), what the user wants (enabled), and what the
//! installed title routes (applied); applying the changes from the game data
//! kept from the ROM; importing the user's texture pack; and the explicitly
//! unsupported custom RomFS folder (owner decision 6).
//!
//! - Applying is the repair in place (`repair::repair_installation`): the kept
//!   ROM data (`sources/`) is rehashed, every asset whose receipt still proves
//!   it is reused, and the HD interface pack, the enabled catalogued mods and
//!   the custom RomFS folder are prepared again and activated together. The
//!   ROM is needed only when the kept data no longer verifies
//!   (`source.needed`). The TopScreen layout is kept unless the frontend asks
//!   for the standard one (decision 4).
//! - The HD interface pack follows the user's choice `hd_interface_pack` in
//!   Forge's preferences `D/config/forge.json` (on by default, decision 5); a
//!   stored pack is applied only while it is on.
//! - The user's texture pack lives in `D/textures/load/<title id>`; the game
//!   reads it when it starts, so it needs no Apply: "applied" means the game
//!   will load it (switch on, folder routed and not shadowed).

use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::{CancelToken, ForgeError, Operation, Result, Subject};
use forge_mods::{custom_romfs, user_textures, Catalog, Status, UiTexturePack};
use forge_prepare::content::{load_prepared_content, PreparedContent};
use forge_prepare::paths::resolve_reference;
use serde_json::{json, Map, Value};

use crate::active_title::{load_active_title, ACTIVE_TITLE};
use crate::backup::os_of;
use crate::inventory::{inventory_of, Inventory, InventoryRequest, Origin, Platform};
use crate::settings_service::{self, TextureChange};

pub const STATUS_FORMAT: &str = "triaevum_enhancements_v1";
/// Forge's own preferences (`forge-desktop/src/texts/preference.rs`).
pub const PREFERENCES_FORMAT: &str = "triaevum_forge_preferences_v1";
pub const HD_PREFERENCE: &str = "hd_interface_pack";
/// Counting the user's texture pack stops here (the import's own limit).
const COUNT_LIMIT: u64 = user_textures::MAXIMUM_FILES + 1;

pub fn preferences_path(data_root: &Path) -> PathBuf {
    data_root.join("config").join("forge.json")
}

/// The user's choice for the HD interface pack: on unless Forge's
/// preferences say `false` (decision 5: on by default on the desktop; on
/// Android the pack exists only once the user downloaded it).
pub fn hd_pack_enabled(data_root: &Path) -> bool {
    let path = preferences_path(data_root);
    !path.is_file()
        || load_json_object(&path).ok().and_then(|document| document.get(HD_PREFERENCE)?.as_bool()).unwrap_or(true)
}

/// Keeps the choice; the file's other members (the Forge language) stay.
pub fn set_hd_pack_enabled(data_root: &Path, enabled: bool) -> Result<()> {
    let path = preferences_path(data_root);
    let mut document = if path.is_file() { load_json_object(&path).unwrap_or_default() } else { Map::new() };
    document.insert("format".into(), json!(PREFERENCES_FORMAT));
    document.insert(HD_PREFERENCE.into(), json!(enabled));
    atomic_write_json(&path, &Value::Object(document)).map_err(|error| error.during(Operation::SettingsSave))
}

/// What the installed title routes, from its runtime receipt.
#[derive(Clone, Debug, Default)]
struct Applied {
    installed: bool,
    title_id: Option<String>,
    recipe: Option<String>,
    hd_pack: bool,
    mods: Vec<String>,
    custom_romfs: Option<Value>,
    prepared: Option<PreparedContent>,
}

fn applied(data_root: &Path) -> Applied {
    let Some(active) = load_active_title(&data_root.join(ACTIVE_TITLE)) else { return Applied::default() };
    let Ok(prepared) = load_prepared_content(&active.directory, &[], &CancelToken::new()) else {
        return Applied::default();
    };
    let runtime = prepared.state.get("runtime").and_then(Value::as_object).cloned().unwrap_or_default();
    let receipt = runtime
        .get("romfs_overlay")
        .and_then(|overlay| overlay.get("path"))
        .and_then(Value::as_str)
        .and_then(|path| resolve_reference(path, &prepared.directory).ok())
        .and_then(|overlay| load_json_object(&overlay.parent()?.join("import.json")).ok());
    let mods = receipt
        .as_ref()
        .and_then(|receipt| receipt.get("mods")?.as_array().cloned())
        .unwrap_or_default()
        .iter()
        .filter_map(|credit| credit.get("id")?.as_str().map(str::to_string))
        .collect();
    Applied {
        installed: true,
        title_id: crate::publish::prepared_title_id(&prepared).map(crate::publish::title_id_text),
        recipe: Some(active.recipe_id),
        hd_pack: runtime.contains_key("ui_texture_pack"),
        mods,
        custom_romfs: receipt.and_then(|receipt| receipt.get("custom_romfs").cloned()),
        prepared: Some(prepared),
    }
}

/// The installed RomFS's file names (its service view), for the custom
/// folder's check; None when the title or its data cannot be read.
fn romfs_paths(prepared: &PreparedContent) -> Option<std::collections::HashSet<String>> {
    let path = prepared.index.get("inputs")?.get("romfs")?.get("path")?.as_str()?;
    let romfs = resolve_reference(path, &prepared.directory).ok()?;
    let (offset, size) = forge_mods::romfs_service_view(&prepared.directory.join("process-manifest.json")).ok()?;
    let mut file = std::fs::File::open(romfs).ok()?;
    let records = forge_mods::romfs_file_records(&mut file, offset, size).ok()?;
    Some(records.files.into_keys().collect())
}

fn recipes(package: &Path) -> Value {
    load_json_object(&crate::install::recipes_path(package)).map(Value::Object).unwrap_or(Value::Null)
}

fn status_code(status: std::result::Result<Status, ForgeError>) -> Value {
    match status {
        Ok(status) => json!(status.as_str()),
        Err(error) => json!({"error": error.message}),
    }
}

fn android(inventory: &Inventory) -> bool {
    inventory.roots.platform == Platform::Android
}

/// The user's texture pack: where it is, whether the game will load it.
fn custom_textures(inventory: &Inventory, title_id: Option<&str>) -> Value {
    let data = &inventory.roots.data;
    let config = settings_service::runtime_config(data, android(inventory));
    let (settings, settings_error) = match settings_service::texture_settings(&config) {
        Ok(settings) => (settings, None),
        Err(error) => (None, Some(error.message)),
    };
    let managed = title_id.map(|title| data.join("textures").join("load").join(title));
    let files = managed.as_deref().map_or(0, |folder| user_textures::count_files(folder, COUNT_LIMIT));
    let effective = inventory.entry("texture_load");
    let routed = match (effective, &managed) {
        (Some(entry), Some(managed)) => same_folder(&entry.resolved_path, managed),
        _ => false,
    };
    let shadowed_by = match effective.map(|entry| entry.origin) {
        _ if routed => None,
        Some(Origin::Explicit) if std::env::var_os(settings_service::LOAD_DIRECTORY_ENVIRONMENT).is_some() => {
            Some("environment")
        }
        Some(Origin::Explicit) => Some("settings"),
        Some(Origin::Legacy) if effective.is_some_and(|entry| entry.resolved_path.is_dir()) => Some("azahar_folder"),
        Some(Origin::Legacy) => Some("older_profile"),
        _ => None,
    };
    // A pack in Azahar's folder: still loading (legacy) or left behind.
    let legacy = inventory
        .entry("texture_load_legacy")
        .map(|entry| entry.resolved_path.clone())
        .or_else(|| {
            effective
                .filter(|entry| entry.origin == Origin::Legacy && crate::inventory::holds_texture_files(&entry.resolved_path))
                .map(|entry| entry.resolved_path.clone())
        });
    let enabled = settings.as_ref().is_some_and(|settings| settings.load_custom_textures);
    json!({
        "folder": managed.as_ref().map(|path| path.to_string_lossy().into_owned()),
        "files": files,
        "enabled": enabled,
        "settings": settings.as_ref().map(settings_service::TextureSettings::to_value),
        "settings_error": settings_error,
        "effective_folder": effective.map(|entry| entry.resolved_path.to_string_lossy().into_owned()),
        "routed": routed,
        "shadowed_by": shadowed_by,
        "legacy_folder": legacy.map(|path| path.to_string_lossy().into_owned()),
        "applied": enabled && routed && files > 0,
    })
}

fn same_folder(left: &Path, right: &Path) -> bool {
    let normal = |path: &Path| {
        let text = forge_prepare::paths::resolve(path).unwrap_or_else(|_| path.to_path_buf()).to_string_lossy().into_owned();
        if cfg!(windows) {
            text.to_lowercase()
        } else {
            text
        }
    };
    normal(left) == normal(right)
}

/// The custom RomFS folder: validation, the installed RomFS check, the mod
/// conflicts, and what the installed overlay holds. Names and counts only.
fn custom_romfs_status(data: &Path, applied: &Applied, catalog: &Catalog, cancel: &CancelToken) -> Value {
    let (enabled, settings_error) = match custom_romfs::enabled(data) {
        Ok(enabled) => (enabled, None),
        Err(error) => (false, Some(error.message)),
    };
    let mut scan = custom_romfs::scan(data, cancel).unwrap_or_default();
    let checked = match applied.prepared.as_ref().and_then(romfs_paths) {
        Some(paths) => {
            scan.check_romfs(&|path| paths.contains(path));
            true
        }
        None => false,
    };
    if let Ok(members) = custom_romfs::mod_members(data, catalog) {
        scan.check_mods(&members);
    }
    let applied_paths: Vec<String> = applied
        .custom_romfs
        .as_ref()
        .and_then(|receipt| receipt.get("paths")?.as_array().cloned())
        .unwrap_or_default()
        .iter()
        .filter_map(|path| path.as_str().map(str::to_string))
        .collect();
    let current: Vec<String> = scan.files.iter().map(|file| file.path.clone()).collect();
    let mut value = scan.to_value();
    value["folder"] = json!(custom_romfs::folder(data).to_string_lossy());
    value["enabled"] = json!(enabled);
    value["settings_error"] = json!(settings_error);
    value["checked_against_romfs"] = json!(checked);
    value["applied"] = json!(applied.custom_romfs.is_some());
    value["applied_files"] = json!(applied_paths.len());
    value["applied_current"] = json!(applied.custom_romfs.is_some() && applied_paths == current);
    value
}

/// Everything the Enhancements page shows (`enhancements_status`).
pub fn status(request: &InventoryRequest, cancel: &CancelToken) -> Result<Value> {
    let inventory = inventory_of(&InventoryRequest { sizes: false, ..request.clone() })
        .map_err(|error| error.during(Operation::Inspect))?;
    let (package, data) = (&inventory.roots.package, &inventory.roots.data);
    let applied = applied(data);
    let recipes = recipes(package);
    let pack = UiTexturePack::vanilla_ui_hud();
    let hd_contract = forge_mods::archive_contract(&recipes, &pack).ok().flatten();
    let hd_enabled = hd_pack_enabled(data);
    let hd_stored = forge_mods::pack_status(package, data, hd_contract.as_ref(), &pack, cancel);
    let hd_present = matches!(hd_stored, Ok(Status::Ready | Status::Archive));
    let catalog = Catalog::builtin();
    let enabled_mods = forge_mods::enabled_mods(data, &catalog);
    let mut apply_needed = applied.installed
        && hd_contract.is_some()
        && ((hd_enabled && hd_present && !applied.hd_pack) || (!hd_enabled && applied.hd_pack));
    let mut mods = Vec::new();
    for mod_ in &catalog.mods {
        let contract = forge_mods::archive_contract(&recipes, mod_).ok().flatten();
        let stored = forge_mods::mod_status(package, data, mod_, contract.as_ref(), cancel);
        let enabled = enabled_mods.as_ref().is_ok_and(|ids| ids.contains(&mod_.mod_id));
        let on = applied.mods.contains(&mod_.mod_id);
        let present = matches!(stored, Ok(Status::Ready | Status::Archive));
        apply_needed |= applied.installed && contract.is_some() && ((enabled && present && !on) || (!enabled && on));
        mods.push(json!({
            "id": mod_.mod_id, "title": mod_.title, "version": mod_.version,
            "available": contract.is_some(), "stored": status_code(stored), "enabled": enabled, "applied": on,
        }));
    }
    let custom = custom_romfs_status(data, &applied, &catalog, cancel);
    let custom_enabled = custom["enabled"] == true;
    apply_needed |= applied.installed
        && ((custom_enabled && custom["applicable"] == true && custom["applied_current"] != true)
            || (!custom_enabled && custom["applied"] == true));
    let layout = crate::config_report::hud_layout_preview(data).unwrap_or_default();
    Ok(json!({
        "format": STATUS_FORMAT,
        "installed": applied.installed,
        "recipe": applied.recipe,
        "title_id": applied.title_id,
        "retained_sources": crate::data_actions::retained_source(data).is_some(),
        "game_running": inventory.activity.game_running,
        "hd_pack": {
            "title": pack.title, "version": pack.version,
            "available": hd_contract.is_some(),
            "bytes": hd_contract.as_ref().map(|contract| contract.bytes),
            "stored": status_code(hd_stored),
            "enabled": hd_enabled,
            "applied": applied.hd_pack,
        },
        "mods": mods,
        "mod_settings_error": enabled_mods.err().map(|error| error.message),
        "custom_textures": custom_textures(&inventory, applied.title_id.as_deref()),
        "custom_romfs": custom,
        "hud_layout": {
            "policy": "keep",
            "differs": !layout.is_empty(),
            "fields": layout.iter().map(|field| field.name.clone()).collect::<Vec<_>>(),
        },
        "apply_needed": apply_needed,
    }))
}

/// "Apply changes": the repair in place with the current choices. Result:
/// the repair's, with `status: "applied"`.
pub fn apply(
    request: &crate::repair::RepairRequest,
    services: &dyn forge_native::NativeServices,
    report: crate::install::Report<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    report("preflight", "Applying your enhancement choices from the game data kept from your ROM...");
    let mut result = crate::repair::repair_installation(request, services, report, cancel)?;
    result["status"] = json!("applied");
    Ok(result)
}

fn import_error(error: ForgeError) -> ForgeError {
    error.during(Operation::OptionalImport).about(Subject::OptionalEnhancement)
}

/// Imports the user's texture pack (a folder or a zip) into the managed
/// folder of the installed title and turns custom textures on. A flat pack
/// first returns `{status: "confirmation_required", reason: "flat"}`.
pub fn import_textures(request: &InventoryRequest, source: &Path, flat_confirmed: bool, cancel: &CancelToken) -> Result<Value> {
    let inventory = inventory_of(&InventoryRequest { sizes: false, ..request.clone() }).map_err(import_error)?;
    let data = inventory.roots.data.clone();
    let Some(title_id) = applied(&data).title_id else {
        return Err(import_error(ForgeError::invalid(
            "Prepare the game first: the texture folder is named after the installed game",
        )));
    };
    let destination = data.join("textures").join("load").join(&title_id);
    let os = os_of(inventory.roots.platform);
    let _quiet = crate::session::quiesce(&inventory.roots.activation, os, Operation::OptionalImport)?;
    let mut opened = user_textures::Source::open(source, cancel).map_err(import_error)?;
    if opened.layout(&title_id).map_err(import_error)? == user_textures::Layout::Flat && !flat_confirmed {
        return Ok(json!({"status": "confirmation_required", "reason": "flat", "title_id": title_id}));
    }
    let import = user_textures::ImportRequest { destination: &destination, title_id: &title_id, flat_confirmed };
    let summary = user_textures::import(&mut opened, &import, cancel).map_err(import_error)?;
    let config = settings_service::runtime_config(&data, android(&inventory));
    let change = TextureChange { load_custom_textures: Some(true), use_managed_folder: false };
    let settings_error = if config.is_file() {
        settings_service::apply_texture_change(&config, change).err().map(|error| error.message)
    } else {
        None
    };
    drop(_quiet);
    let after = inventory_of(&InventoryRequest { sizes: false, ..request.clone() }).ok();
    let textures = after.as_ref().map(|inventory| custom_textures(inventory, Some(&title_id)));
    Ok(json!({
        "status": "imported",
        "title_id": title_id,
        "import": summary.to_value(),
        "settings_error": settings_error,
        "custom_textures": textures,
    }))
}

/// The page's switches (`enhancement_settings`). Custom textures belong to
/// the game's graphics settings and change only while the game is closed.
#[derive(Clone, Debug, Default)]
pub struct SettingsChange {
    pub hd_interface_pack: Option<bool>,
    pub mod_enabled: Option<(String, bool)>,
    pub custom_textures: Option<bool>,
    pub use_managed_texture_folder: bool,
    pub custom_romfs: Option<bool>,
}

pub fn change_settings(request: &InventoryRequest, change: &SettingsChange, cancel: &CancelToken) -> Result<Value> {
    let inventory = inventory_of(&InventoryRequest { sizes: false, ..request.clone() })
        .map_err(|error| error.during(Operation::SettingsSave))?;
    let data = &inventory.roots.data;
    if let Some(enabled) = change.hd_interface_pack {
        set_hd_pack_enabled(data, enabled)?;
    }
    if let Some((id, enabled)) = &change.mod_enabled {
        forge_mods::set_mod_enabled(data, &Catalog::builtin(), id, *enabled)
            .map_err(|error| error.during(Operation::SettingsSave))?;
    }
    if let Some(enabled) = change.custom_romfs {
        custom_romfs::set_enabled(data, enabled).map_err(|error| error.during(Operation::SettingsSave))?;
    }
    if change.custom_textures.is_some() || change.use_managed_texture_folder {
        let config = settings_service::runtime_config(data, android(&inventory));
        let texture_change = TextureChange {
            load_custom_textures: change.custom_textures,
            use_managed_folder: change.use_managed_texture_folder,
        };
        settings_service::change_texture_settings(
            &inventory.roots.activation,
            os_of(inventory.roots.platform),
            &config,
            texture_change,
        )?;
    }
    status(request, cancel)
}

/// One line for the diagnostics report and the support bundle: what is
/// applied, names and counts only (never file contents or the user's paths).
pub fn diagnostics_line(data_root: &Path) -> String {
    let applied = applied(data_root);
    if !applied.installed {
        return "Enhancements: no installed game".to_string();
    }
    let mods = if applied.mods.is_empty() { "none".to_string() } else { applied.mods.join(", ") };
    let textures = settings_service::texture_settings(&settings_service::runtime_config(data_root, false))
        .ok()
        .flatten()
        .or_else(|| settings_service::texture_settings(&settings_service::runtime_config(data_root, true)).ok().flatten())
        .map(|settings| settings.load_custom_textures);
    let files = applied
        .title_id
        .as_deref()
        .map_or(0, |title| user_textures::count_files(&data_root.join("textures").join("load").join(title), COUNT_LIMIT));
    let custom = match (&applied.custom_romfs, custom_romfs::enabled(data_root)) {
        (Some(receipt), _) => {
            let paths: Vec<&str> = receipt["paths"].as_array().map(|paths| paths.iter().filter_map(Value::as_str).take(10).collect()).unwrap_or_default();
            format!("ACTIVE (unsupported), {} files: {}", receipt["files"], paths.join(", "))
        }
        (None, Ok(true)) => "on, not applied".to_string(),
        (None, Ok(false)) => "off".to_string(),
        (None, Err(_)) => "settings unreadable".to_string(),
    };
    format!(
        "Enhancements: HD interface pack {}; mods {mods}; custom textures {} ({files} files in the managed folder); custom RomFS folder {custom}",
        if applied.hd_pack { "applied" } else { "not applied" },
        match textures {
            Some(true) => "on",
            Some(false) => "off",
            None => "unknown",
        },
    )
}
