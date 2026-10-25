// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `precompiled_titles.py`: the revision-bound precompiled title catalog and
//! its hash-bound artifacts. There is no compiler fallback: a missing or
//! incompatible module is a package error.
//!
//! [`validate_title`] is target-independent (release audit, preparing data
//! for another device); [`select_title`] additionally requires the catalog to
//! bind the explicit execution target, which need not be the current host.

use std::path::{Path, PathBuf};

use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use serde_json::{Map, Value};

use crate::adapters::validate_adapter;
use crate::family;
use crate::paths::{join_posix, normalize_relative_path, resolve};
use crate::platform::{catalog_platform, installed_runtime_path, ReleasePlatform};
use crate::pyjson::{eq_int, eq_str, get, index, is_hex_digest, list, malformed, py_eq, py_str, type_name};

pub const FORMAT: &str = "triaevum_precompiled_titles_v1";
pub const MODEL: &str = "precompiled_title_rom_import_v1";
pub const CATALOG: &str = "recipes/precompiled-titles.json";
/// `shader_preparation.RENDERER_CONTRACT`.
pub const RENDERER_CONTRACT: &str = "triaevum_renderer_shader_compiler_v1";

fn integrity(message: String) -> ForgeError {
    ForgeError::integrity(message)
}

/// `checked_file(root, record)`: a regular, unlinked file inside `root` with
/// the record's exact size and SHA-256. Returns its path under resolved `root`.
pub fn checked_file(root: &Path, record: &Value, cancel: &CancelToken) -> Result<PathBuf> {
    let fields = record.as_object().ok_or_else(|| malformed(format!("artifact record is {}", type_name(record))))?;
    // `str(record.get("path", ""))`: missing is "", an explicit null "None".
    let relative = normalize_relative_path(&fields.get("path").map(py_str).unwrap_or_default())?;
    let root = resolve(root);
    let path = join_posix(&root, &relative);
    let parts: Vec<&str> = relative.split('/').filter(|part| *part != ".").collect();
    for depth in (1..=parts.len()).rev() {
        let component = join_posix(&root, &parts[..depth].join("/"));
        // On Windows `is_symlink` also covers junctions (name-surrogate
        // reparse points), which Python checked with `is_junction`.
        if std::fs::symlink_metadata(&component).is_ok_and(|m| m.file_type().is_symlink()) {
            return Err(integrity(format!("Linked precompiled artifact is forbidden: {relative}")));
        }
    }
    if !resolve(&path).starts_with(&root) || !path.is_file() {
        return Err(integrity(format!("Precompiled artifact is missing: {relative}")));
    }
    let size = std::fs::metadata(&path)
        .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?
        .len();
    if !eq_int(get(fields, "bytes"), size) || !eq_str(get(fields, "sha256"), &hex(&sha256_file(&path, cancel)?.1)) {
        return Err(integrity(format!("Precompiled artifact failed integrity validation: {relative}")));
    }
    Ok(path)
}

/// `load_catalog(root)`.
pub fn load_catalog(root: &Path) -> Result<Map<String, Value>> {
    let payload = forge_base::fsutil::load_json_object(&join_posix(root, CATALOG))?;
    if !eq_str(get(&payload, "format"), FORMAT) || !eq_str(get(&payload, "install_model"), MODEL) {
        return Err(integrity("Unsupported precompiled-title catalog".into()));
    }
    let platform = catalog_platform(&payload)?;
    for (field, expected) in [("runtime", platform.runtime), ("native_module", platform.native_module)] {
        let bound = matches!(payload.get(field), Some(Value::Object(record)) if eq_str(get(record, "path"), expected));
        if !bound {
            return Err(integrity(format!("Invalid catalog {field} binding")));
        }
    }
    let titles = match payload.get("titles") {
        Some(Value::Array(titles)) if !titles.is_empty() => titles,
        _ => {
            return Err(integrity("This release contains no precompiled titles; obtain a complete package".into()));
        }
    };
    let ids: Vec<&Value> = titles.iter().filter_map(Value::as_object).map(|item| get(item, "recipe")).collect();
    let mut seen = std::collections::HashSet::new();
    let valid = ids.len() == titles.len()
        && ids.iter().all(|id| id.as_str().is_some_and(|text| !text.is_empty()))
        && ids.iter().all(|id| seen.insert(id.as_str().unwrap_or_default()));
    if !valid {
        return Err(integrity("Invalid or duplicate precompiled-title revisions".into()));
    }
    Ok(payload)
}

/// `runtime_record`: the catalog's runtime record at its installed location.
pub fn runtime_record(root: &Path, catalog: &Map<String, Value>) -> Result<Value> {
    let platform = catalog_platform(catalog)?;
    let path = installed_runtime_path(root, &platform);
    let relative = path.strip_prefix(root).map_err(|_| integrity("Invalid catalog runtime binding".into()))?;
    let posix: Vec<String> =
        relative.components().map(|part| part.as_os_str().to_string_lossy().into_owned()).collect();
    let mut record = match catalog.get("runtime") {
        Some(Value::Object(record)) => record.clone(),
        other => return Err(malformed(format!("runtime is {}", other.map_or("missing", type_name)))),
    };
    record.insert("path".into(), Value::String(posix.join("/")));
    Ok(Value::Object(record))
}

fn catalog_or_load(root: &Path, catalog: Option<&Map<String, Value>>) -> Result<Map<String, Value>> {
    // `catalog or load_catalog(root)`: an empty dict loads the package catalog.
    match catalog {
        Some(catalog) if !catalog.is_empty() => Ok(catalog.clone()),
        _ => load_catalog(root),
    }
}

/// `validate_title`: catalog content for `recipe`, without executing binaries
/// and without requiring the catalog's target to be the current host.
pub fn validate_title(
    root: &Path,
    recipe: &Map<String, Value>,
    catalog: Option<&Map<String, Value>>,
    cancel: &CancelToken,
) -> Result<Map<String, Value>> {
    let catalog = catalog_or_load(root, catalog)?;
    let platform = catalog_platform(&catalog)?;
    let recipe_id = index_map(recipe, "id")?;
    let mut matches = Vec::new();
    for item in list(index_map(&catalog, "titles")?, "titles")? {
        if py_eq(index(item, "recipe")?, recipe_id) {
            matches.push(item);
        }
    }
    if matches.len() != 1 {
        return Err(ForgeError::new(
            ErrorKind::UnsupportedRevision,
            "No precompiled module for this ROM revision; obtain a compatible release",
        ));
    }
    let item_value = matches[0];
    let item = attr_object(item_value)?;
    if !py_eq(get(item, "inputs"), get(recipe, "inputs"))
        || !eq_int(get(item, "abi_version"), 2)
        || !eq_str(get(item, "target"), platform.target)
    {
        return Err(integrity("Precompiled title revision/ABI does not match the ROM recipe".into()));
    }
    if !py_eq(get(item, "input_adapter"), get(recipe, "input_adapter")) {
        return Err(integrity("Precompiled title input adapter differs from the revision recipe".into()));
    }
    if !py_eq(get(item, "data_compatibility"), get(recipe, "data_compatibility")) {
        return Err(integrity("Precompiled title content family differs from the recipe".into()));
    }
    if !get(recipe, "data_compatibility").is_null() {
        family::expected(recipe, "execution")?;
    }
    if !get(recipe, "input_adapter").is_null() {
        validate_adapter(root, recipe, cancel)?;
    }
    let identity = item.get("translator_identity_sha256").cloned().unwrap_or(Value::String(String::new()));
    if !is_hex_digest(&identity) {
        return Err(integrity("Precompiled title has no valid translator identity".into()));
    }
    checked_file(root, &runtime_record(root, &catalog)?, cancel)?;
    checked_file(root, index_map(&catalog, "native_module")?, cancel)?;
    checked_file(root, index_map(item, "plugin")?, cancel)?;
    let renderer = get(item, "renderer_shader_preparation");
    if !renderer.is_null() {
        let Some(renderer) = renderer.as_object().filter(|r| eq_str(get(r, "format"), RENDERER_CONTRACT)) else {
            return Err(integrity("Invalid renderer shader preparation binding".into()));
        };
        let empty = Value::Array(Vec::new());
        let dependencies = renderer.get("dependencies").unwrap_or(&empty);
        let (Some(compiler), Some(dependencies)) = (get(renderer, "compiler").as_object(), dependencies.as_array())
        else {
            return Err(integrity("Invalid renderer shader compiler artifacts".into()));
        };
        let compiler = Value::Object(compiler.clone());
        for record in std::iter::once(&compiler).chain(dependencies) {
            if !record.is_object() {
                return Err(integrity("Invalid renderer shader compiler artifact".into()));
            }
            checked_file(root, record, cancel)?;
        }
    }
    Ok(item.clone())
}

/// `select_title`: [`validate_title`] for a catalog that binds `target`, the
/// platform the title will execute on (Python: always the current host).
pub fn select_title(
    root: &Path,
    recipe: &Map<String, Value>,
    catalog: Option<&Map<String, Value>>,
    target: &ReleasePlatform,
    cancel: &CancelToken,
) -> Result<Map<String, Value>> {
    let catalog = catalog_or_load(root, catalog)?;
    let platform = catalog_platform(&catalog)?;
    if platform != *target {
        return Err(ForgeError::new(
            ErrorKind::IncompatibleTarget,
            format!("This package targets {}, not this host", platform.target),
        ));
    }
    validate_title(root, recipe, Some(&catalog), cancel)
}

fn index_map<'a>(map: &'a Map<String, Value>, key: &str) -> Result<&'a Value> {
    map.get(key).ok_or_else(|| malformed(format!("missing {key:?}")))
}

fn attr_object(value: &Value) -> Result<&Map<String, Value>> {
    value.as_object().ok_or_else(|| malformed(format!("title is {}", type_name(value))))
}
