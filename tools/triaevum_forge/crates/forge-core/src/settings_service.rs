// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The validated shared settings service (UX plan WP-07): Forge changes a
//! setting the game owns only through here, never by ad hoc JSON edits.
//!
//! The custom texture switch belongs to the graphics settings owner
//! (`GraphicsSettingsRuntime`, `Graphics.TexturePacks.Azahar` in the runtime
//! configuration, `graphics_settings_persistence.cpp`). While the game runs
//! it is changed in the game (F1, the in-game menu); Forge changes it only
//! while no game and no other Forge uses the installation (the quiescence of
//! `session::quiesce`), and only in a configuration whose graphics schema it
//! knows: a newer schema is refused, never rewritten.

use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::{ErrorKind, ForgeError, Operation, Result};
use serde_json::{json, Map, Value};

use crate::layout::HostOs;
use crate::publish::ANDROID_CONFIG;

/// The newest graphics schema this Forge knows
/// (`kGraphicsSettingsSchemaVersion`).
pub const GRAPHICS_SCHEMA: u64 = 15;
/// The first schema with the texture folders (`LoadDirectory`).
const TEXTURE_FOLDERS_SCHEMA: u64 = 9;
/// The environment variable that names the load folder above every setting.
pub const LOAD_DIRECTORY_ENVIRONMENT: &str = "OOT3D_AZAHAR_LOAD_DIRECTORY";

/// The runtime configuration Forge publishes (`TriAevum.json`, Android
/// `TriAevum.android.json`).
pub fn runtime_config(data_root: &Path, android: bool) -> PathBuf {
    data_root.join("config").join(if android { ANDROID_CONFIG } else { "TriAevum.json" })
}

/// The user's custom texture settings as the runtime reads them.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TextureSettings {
    pub load_custom_textures: bool,
    /// An explicit folder chosen in F1; empty for the managed default.
    pub load_directory: String,
}

impl TextureSettings {
    pub fn to_value(&self) -> Value {
        json!({"load_custom_textures": self.load_custom_textures, "load_directory": self.load_directory})
    }
}

fn settings_error(path: &Path, message: impl std::fmt::Display) -> ForgeError {
    ForgeError::new(ErrorKind::InvalidSettings, format!("{}: {message}", path.display()))
        .during(Operation::SettingsSave)
}

fn graphics<'a>(document: &'a Map<String, Value>, path: &Path) -> Result<&'a Map<String, Value>> {
    let graphics = document
        .get("Graphics")
        .and_then(Value::as_object)
        .ok_or_else(|| settings_error(path, "the graphics settings are missing; start the game once"))?;
    let schema = graphics
        .get("SchemaVersion")
        .and_then(Value::as_u64)
        .ok_or_else(|| settings_error(path, "the graphics settings have no schema version"))?;
    if schema > GRAPHICS_SCHEMA {
        return Err(settings_error(
            path,
            "a newer TriAevum wrote these settings; change them in the game's menu",
        ));
    }
    if schema < TEXTURE_FOLDERS_SCHEMA {
        return Err(settings_error(path, "the graphics settings are from an older TriAevum; start the game once"));
    }
    Ok(graphics)
}

/// The texture settings of `config`; None when the file does not exist yet
/// (a fresh installation writes it on activation).
pub fn texture_settings(config: &Path) -> Result<Option<TextureSettings>> {
    if !config.is_file() {
        return Ok(None);
    }
    let document = load_json_object(config).map_err(|error| settings_error(config, error.message))?;
    let azahar = graphics(&document, config)?
        .get("TexturePacks")
        .and_then(|packs| packs.get("Azahar"))
        .and_then(Value::as_object);
    let field = |key: &str| azahar.and_then(|azahar| azahar.get(key));
    Ok(Some(TextureSettings {
        load_custom_textures: field("LoadCustomTextures").and_then(Value::as_bool).unwrap_or(false),
        load_directory: field("LoadDirectory").and_then(Value::as_str).unwrap_or_default().to_string(),
    }))
}

/// What a frontend asks to change.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TextureChange {
    pub load_custom_textures: Option<bool>,
    /// Forget an explicit F1 folder, so the managed folder loads.
    pub use_managed_folder: bool,
}

/// Writes `change` into `config`. The caller holds the quiescence; use
/// [`change_texture_settings`] otherwise.
pub fn apply_texture_change(config: &Path, change: TextureChange) -> Result<TextureSettings> {
    let mut document = load_json_object(config).map_err(|error| settings_error(config, error.message))?;
    graphics(&document, config)?;
    let graphics = document.get_mut("Graphics").and_then(Value::as_object_mut).expect("validated");
    let packs = graphics.entry("TexturePacks").or_insert_with(|| json!({}));
    if !packs.is_object() {
        return Err(settings_error(config, "Graphics.TexturePacks is not an object"));
    }
    let azahar = packs.as_object_mut().expect("checked").entry("Azahar").or_insert_with(|| {
        json!({"DumpTextures": false, "LoadCustomTextures": false, "LoadDirectory": "", "DumpDirectory": ""})
    });
    let Some(azahar) = azahar.as_object_mut() else {
        return Err(settings_error(config, "Graphics.TexturePacks.Azahar is not an object"));
    };
    if let Some(enabled) = change.load_custom_textures {
        azahar.insert("LoadCustomTextures".into(), Value::Bool(enabled));
    }
    if change.use_managed_folder {
        azahar.insert("LoadDirectory".into(), Value::from(""));
    }
    atomic_write_json(config, &Value::Object(document)).map_err(|error| error.during(Operation::SettingsSave))?;
    texture_settings(config)?.ok_or_else(|| settings_error(config, "the settings vanished while they were saved"))
}

/// Changes the texture settings while no game and no other Forge uses the
/// installation `activation`.
pub fn change_texture_settings(
    activation: &Path,
    os: HostOs,
    config: &Path,
    change: TextureChange,
) -> Result<TextureSettings> {
    let _quiet = crate::session::quiesce(activation, os, Operation::SettingsSave)?;
    apply_texture_change(config, change)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn config(schema: u64) -> (tempfile::TempDir, PathBuf) {
        let root = tempfile::tempdir().unwrap();
        let path = runtime_config(root.path(), false);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        let document = json!({"CVars": {"x": 1}, "Graphics": {"SchemaVersion": schema, "Preset": "Enhanced",
            "TexturePacks": {"Azahar": {"DumpTextures": false, "LoadCustomTextures": false,
                                        "LoadDirectory": "C:/elsewhere", "DumpDirectory": ""}}}});
        atomic_write_json(&path, &document).unwrap();
        (root, path)
    }

    #[test]
    fn the_switch_and_the_managed_folder_are_written_and_nothing_else() {
        let (root, path) = config(GRAPHICS_SCHEMA);
        let before = load_json_object(&path).unwrap();
        let change = TextureChange { load_custom_textures: Some(true), use_managed_folder: true };
        let settings = change_texture_settings(root.path(), HostOs::current(), &path, change).unwrap();
        assert_eq!(settings, TextureSettings { load_custom_textures: true, load_directory: String::new() });
        let after = load_json_object(&path).unwrap();
        assert_eq!(after["CVars"], before["CVars"]);
        assert_eq!(after["Graphics"]["Preset"], "Enhanced", "the preset stays (presets keep texture packs)");
        assert_eq!(after["Graphics"]["TexturePacks"]["Azahar"]["DumpTextures"], false);
        let off = apply_texture_change(&path, TextureChange { load_custom_textures: Some(false), ..Default::default() });
        assert!(!off.unwrap().load_custom_textures);
    }

    #[test]
    fn unknown_or_missing_schemas_are_refused_unchanged() {
        let (_root, path) = config(GRAPHICS_SCHEMA + 1);
        let bytes = std::fs::read(&path).unwrap();
        let change = TextureChange { load_custom_textures: Some(true), use_managed_folder: false };
        let error = apply_texture_change(&path, change).unwrap_err();
        assert_eq!(error.kind, ErrorKind::InvalidSettings);
        assert!(error.message.contains("newer TriAevum"), "{}", error.message);
        assert_eq!(std::fs::read(&path).unwrap(), bytes);
        let (_old, old) = config(7);
        assert!(apply_texture_change(&old, change).unwrap_err().message.contains("older"));
        let missing = tempfile::tempdir().unwrap();
        assert_eq!(texture_settings(&runtime_config(missing.path(), false)).unwrap(), None);
    }

    #[test]
    fn a_running_game_blocks_the_change() {
        let (root, path) = config(GRAPHICS_SCHEMA);
        let os = HostOs::current();
        let held = crate::session::hold_game_session(root.path(), os, Operation::Launch).unwrap();
        let change = TextureChange { load_custom_textures: Some(true), use_managed_folder: false };
        let error = change_texture_settings(root.path(), os, &path, change).unwrap_err();
        assert_eq!(error.kind, ErrorKind::Busy);
        drop(held);
        assert!(!texture_settings(&path).unwrap().unwrap().load_custom_textures);
    }
}
