// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `game_language.py`: one persisted game-language preference shared by
//! Forge and the runtime, `data/config/game_language.json`. The native
//! runtime decides which languages a ROM supports.

use std::collections::HashSet;
use std::path::{Path, PathBuf};

use forge_base::fsutil::atomic_write_json;
use forge_base::{ErrorKind, ForgeError, Result};
use serde_json::{Map, Value};

use crate::pyerr::{json_loads, load_json_object, tail};
use crate::services::{LanguageQuery, NativeServices};

pub const FORMAT: &str = "triaevum_game_language_v1";

fn settings(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::InvalidSettings, message)
}

fn system_id_valid(value: Option<&Value>) -> bool {
    matches!(value, Some(Value::Number(number)) if !number.is_f64() && number.as_u64().is_some_and(|id| id <= 11))
}

/// `validate(document)`: a nonempty list of `{code, label, system_id}`
/// entries with unique string codes, nonempty labels and system IDs 0..11,
/// and a selected code that is available.
///
/// Deliberate difference: an unhashable `selected` (list/object) raised
/// TypeError in Python; it is "unavailable" here.
pub fn validate(document: &Value) -> Result<Map<String, Value>> {
    let Value::Object(document) = document else {
        return Err(settings("Invalid game language document"));
    };
    let entries = match (document.get("format"), document.get("available")) {
        (Some(Value::String(format)), Some(Value::Array(entries))) if format == FORMAT && !entries.is_empty() => {
            entries
        }
        _ => return Err(settings("Invalid game language information")),
    };
    let mut codes = HashSet::new();
    for entry in entries {
        let valid = match entry {
            Value::Object(entry) => {
                let code = entry.get("code").and_then(Value::as_str);
                let label = entry.get("label").and_then(Value::as_str);
                code.is_some_and(|code| !codes.contains(code))
                    && label.is_some_and(|label| !label.is_empty())
                    && system_id_valid(entry.get("system_id"))
            }
            _ => false,
        };
        if !valid {
            return Err(settings("Invalid game language entry"));
        }
        codes.insert(entry["code"].as_str().unwrap_or_default().to_string());
    }
    if !document.get("selected").and_then(Value::as_str).is_some_and(|selected| codes.contains(selected)) {
        return Err(settings("Selected game language is unavailable"));
    }
    Ok(document.clone())
}

/// `discover(runtime, romfs)`: the runtime's `--game-language-info` report
/// for the execution RomFS.
pub fn discover(services: &dyn NativeServices, runtime: &Path, romfs: &Path) -> Result<Map<String, Value>> {
    discover_query(services, runtime, &LanguageQuery::romfs(romfs))
}

/// Rust addition: [`discover`] for any [`LanguageQuery`], such as the RomFS
/// extent of a cartridge image recognized before extraction.
pub fn discover_query(
    services: &dyn NativeServices,
    runtime: &Path,
    query: &LanguageQuery,
) -> Result<Map<String, Value>> {
    let output = services.game_language_info(runtime, query)?;
    if output.status != 0 {
        return Err(ForgeError::new(
            ErrorKind::NativePreflight,
            format!("Cannot inspect ROM languages: {}", tail(&output.stderr, 2000)),
        ));
    }
    let document = json_loads(&output.stdout).map_err(|error| ForgeError::new(ErrorKind::NativePreflight, error))?;
    validate(&document)
}

/// `config_path(data_root)`.
pub fn config_path(data_root: &Path) -> PathBuf {
    data_root.join("config").join("game_language.json")
}

/// `install(data_root, detected)`: writes the detected languages, keeping
/// the previous selection while the new ROM still offers it. A malformed
/// existing file is an error and stays untouched.
pub fn install(data_root: &Path, detected: &Value) -> Result<()> {
    install_preferring(data_root, detected, None)
}

/// Rust addition: [`install`] honouring the language the user chose before
/// installing (the recognized-ROM card). Without a preference, or when the
/// detected languages do not offer it, this is `install`.
pub fn install_preferring(data_root: &Path, detected: &Value, preferred: Option<&str>) -> Result<()> {
    atomic_write_json(&config_path(data_root), &Value::Object(merged(data_root, detected, preferred)?))
}

/// The document [`install_preferring`] writes: the detected languages with
/// `preferred` selected when offered, else the previous selection while
/// offered, else the runtime's default. A malformed existing file is an
/// error, also with a preference.
pub fn merged(data_root: &Path, detected: &Value, preferred: Option<&str>) -> Result<Map<String, Value>> {
    let mut document = validate(detected)?;
    let path = config_path(data_root);
    let previous = match path.exists() {
        true => Some(validate(&Value::Object(load_json_object(&path)?))?["selected"].clone()),
        false => None,
    };
    let offered = |code: &Value| {
        document["available"]
            .as_array()
            .is_some_and(|entries| entries.iter().any(|entry| entry.get("code") == Some(code)))
    };
    let preferred = preferred.map(|code| Value::String(code.to_string()));
    if let Some(selected) = [preferred, previous].into_iter().flatten().find(offered) {
        document.insert("selected".to_string(), selected);
    }
    Ok(document)
}

/// `select(data_root, code)`: persists a new selection; an unavailable code
/// leaves the file unchanged.
pub fn select(data_root: &Path, code: &str) -> Result<()> {
    let path = config_path(data_root);
    let mut document = load_json_object(&path)?;
    document.insert("selected".to_string(), Value::String(code.to_string()));
    atomic_write_json(&path, &Value::Object(validate(&Value::Object(document))?))
}
