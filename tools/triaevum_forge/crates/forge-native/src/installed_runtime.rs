// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `installed_runtime.py`: validates the installed runtime tuple (runtime,
//! title plugin generation, launch profile and routed resources) before
//! Forge launches native code. Mutable renderer caches are routed, never
//! hashed; saves are not immutable.
//!
//! Deliberate difference: a receipt resource without its `path` (or a
//! non-string one) crashed Python with KeyError/TypeError; it is reported as
//! an invalid receipt here.

use std::path::{Path, PathBuf};

use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_catalog::pyjson::{eq_str, get, py_str, truthy};
use forge_catalog::{for_target, host_platform};
use forge_prepare::paths::{expand_profile_argument, resolve, resolve_reference};
use serde_json::{Map, Value};

use crate::pyerr::{io_failure, load_json_object, open_error};

/// The managed data folders a receipt's `data_routing` names, by option.
const DATA_ROUTING_OPTIONS: [(&str, &str); 5] = [
    ("log_directory", "--log-directory"),
    ("crash_directory", "--crash-directory"),
    ("savestate_directory", "--savestate-directory"),
    ("texture_load_directory", "--texture-load-directory"),
    ("texture_dump_directory", "--texture-dump-directory"),
];

fn recovery(message: impl Into<String>) -> ForgeError {
    ForgeError::new(ErrorKind::RecoveryRequired, message)
}

fn file_sha256(path: &Path) -> Result<String> {
    if let Err(error) = std::fs::File::open(path) {
        let message = open_error(&error, path);
        return Err(io_failure(error, message));
    }
    Ok(hex(&sha256_file(path, &CancelToken::new())?.1))
}

/// pathlib equality of resolved paths: case-insensitive on Windows.
fn same_path(left: &Path, right: &Path) -> bool {
    if cfg!(windows) {
        left.to_string_lossy().to_lowercase() == right.to_string_lossy().to_lowercase()
    } else {
        left == right
    }
}

/// `Path(argument).resolve() == expected.resolve()`.
fn routes_to(argument: &str, expected: &Path) -> Result<bool> {
    Ok(same_path(&resolve(Path::new(argument))?, &resolve(expected)?))
}

/// The single value after `option`, or `None` when the option is missing,
/// duplicated or last.
fn single_value<'a>(arguments: &'a [String], option: &str) -> Option<Option<&'a str>> {
    if arguments.iter().filter(|argument| *argument == option).count() != 1 {
        return None;
    }
    let index = arguments.iter().position(|argument| argument == option)? + 1;
    Some(arguments.get(index).map(String::as_str))
}

fn resource_path<'a>(resource: &'a Value, field: &str) -> Result<&'a str> {
    resource
        .get("path")
        .and_then(Value::as_str)
        .ok_or_else(|| recovery(format!("Invalid installed runtime receipt: {field} has no path")))
}

/// `validate_installed_runtime(executable, title, data_root, runtime)`:
/// returns the validated launch profile.
pub fn validate_installed_runtime(
    executable: &Path,
    title: &Path,
    data_root: &Path,
    runtime: &Map<String, Value>,
) -> Result<PathBuf> {
    if !eq_str(get(runtime, "status"), "ready") {
        return Err(recovery("The active title has no ready playable runtime; run Forge again"));
    }
    let platform = match runtime.get("target") {
        None => host_platform()?,
        Some(Value::String(target)) => for_target(target)?,
        Some(other) => {
            return Err(ForgeError::new(
                ErrorKind::IncompatibleTarget,
                format!("Unsupported release target: {}", py_str(other)),
            ))
        }
    };
    if platform != host_platform()? {
        return Err(ForgeError::new(
            ErrorKind::IncompatibleTarget,
            "Installed runtime belongs to another platform; run this platform's Forge",
        ));
    }
    let runtime_dir = executable.parent().unwrap_or(Path::new(""));
    let default_plugin = runtime_dir.join(platform.title_module);
    let plugin_value = match get(runtime, "plugin") {
        value if truthy(value) => py_str(value),
        _ => default_plugin.to_string_lossy().into_owned(),
    };
    let plugin = resolve_reference(&plugin_value, title)?;
    for (path, key) in [(executable, "runtime_sha256"), (plugin.as_path(), "plugin_sha256")] {
        if !path.is_file() || !eq_str(get(runtime, key), &file_sha256(path)?) {
            return Err(recovery(format!("Installed runtime identity mismatch: {}; run Forge again", path.display())));
        }
    }
    let profile_value = match runtime.get("launch_profile") {
        Some(Value::String(value)) if !value.is_empty() => value,
        _ => return Err(recovery("The active title has no launch profile")),
    };
    let profile = resolve_reference(profile_value, title)?;
    let expected_hash = get(runtime, "launch_profile_sha256");
    if truthy(expected_hash) && !eq_str(expected_hash, &file_sha256(&profile)?) {
        return Err(recovery("The launch profile changed after activation; run Forge again"));
    }
    let payload = load_json_object(&profile)?;
    let arguments: Vec<String> = match payload.get("arguments") {
        Some(Value::Array(items)) if eq_str(get(&payload, "format"), "oot3d_native_game_launch_profile_v1") => {
            items.iter().map(|item| item.as_str().map(str::to_owned)).collect::<Option<_>>()
        }
        _ => None,
    }
    .ok_or_else(|| recovery("Invalid installed launch profile"))?;
    let arguments = arguments
        .iter()
        .map(|argument| expand_profile_argument(argument, &profile, Some(runtime_dir)))
        .collect::<Result<Vec<String>>>()?;
    if arguments.iter().any(|argument| argument == "--title-plugin") {
        let selected = match single_value(&arguments, "--title-plugin") {
            Some(Some(value)) => routes_to(value, &plugin)?,
            _ => false,
        };
        if !selected {
            return Err(recovery("Launch profile selects another title plugin"));
        }
    } else if !same_path(&resolve(&plugin)?, &resolve(&default_plugin)?) {
        return Err(recovery("Launch profile does not select the installed plugin generation"));
    }
    // Resolve profile-relative paths first. Reject ambiguous duplicate
    // routing arguments rather than validating one value and executing another.
    for (option, expected) in [
        ("--a32-process-manifest", title.join("process-manifest.json")),
        ("--config", data_root.join("config").join("TriAevum.json")),
        ("--topscreen-config", data_root.join("config").join("topscreen_ui.json")),
        ("--save-data", data_root.join("savedata")),
    ] {
        let Some(value) = single_value(&arguments, option) else {
            return Err(recovery(format!("Missing or duplicate installed profile option: {option}")));
        };
        if !matches!(value, Some(value) if routes_to(value, &expected)?) {
            return Err(recovery(format!("Launch profile points to another installation: {option}")));
        }
        if option != "--save-data" {
            load_json_object(&expected)?;
        }
    }
    for (field, option, label) in [
        ("topscreen_textures", "--topscreen-texture-overrides", "TopScreen textures"),
        ("water_textures", "--water-texture-catalog", "water texture catalog"),
        ("scene_lights", "--scene-light-catalog", "scene light catalog"),
        ("romfs_overlay", "--romfs-overlay", "RomFS mods"),
        ("pica_shader_pack", "--pica-aot-shader-pack", "PICA shader pack"),
        ("title_program_pack", "--title-program-pack", "title program pack"),
    ] {
        let resource = get(runtime, field);
        if resource.is_null() {
            continue;
        }
        let pack = resolve_reference(resource_path(resource, field)?, title)?;
        let Some(value) = single_value(&arguments, option) else {
            return Err(recovery(format!("Installed {label} routing is missing or duplicated")));
        };
        let intact = match value {
            Some(value) if routes_to(value, &pack)? && pack.is_file() => {
                resource.get("sha256").is_some_and(|digest| eq_str(digest, &file_sha256(&pack).unwrap_or_default()))
            }
            _ => false,
        };
        if !intact {
            return Err(recovery(format!("Installed {label} changed or are missing; run Forge again")));
        }
    }
    if let Some(resource) = runtime.get("ui_texture_pack") {
        let pack = resolve_reference(resource_path(resource, "ui_texture_pack")?, title)?;
        let Some(value) = single_value(&arguments, "--ui-texture-pack") else {
            return Err(recovery("Installed HD UI texture routing is missing or duplicated"));
        };
        let receipt = pack.join("source.json");
        let intact = match value {
            Some(value) if routes_to(value, &pack)? && receipt.is_file() => resource
                .get("receipt_sha256")
                .is_some_and(|digest| eq_str(digest, &file_sha256(&receipt).unwrap_or_default())),
            _ => false,
        };
        if !intact {
            return Err(recovery("Installed HD UI textures changed or are missing; run Forge again"));
        }
    }
    if let Some(resource) = runtime.get("renderer_cache") {
        let expected = resolve_reference(resource_path(resource, "renderer_cache")?, title)?;
        let Some(value) = single_value(&arguments, "--renderer-cache-directory") else {
            return Err(recovery("Installed renderer cache routing is missing or duplicated"));
        };
        // Mutable/disposable cache, not a release artifact. Missing or corrupt
        // entries are regenerated by the renderer; never hash or require them.
        if !matches!(value, Some(value) if same_path(&resolve(Path::new(value))?, &expected)) {
            return Err(recovery("Launch profile points to another renderer cache"));
        }
    }
    // The managed data tree (UX plan WP-05C, Rust addition): mutable folders,
    // routed like the renderer cache and never hashed. Receipts written
    // before it have no such field; their profiles have no such options.
    if let Some(routing) = runtime.get("data_routing") {
        let Value::Object(routing) = routing else {
            return Err(recovery("Invalid installed runtime receipt: data_routing"));
        };
        for (field, option) in DATA_ROUTING_OPTIONS {
            let Some(reference) = routing.get(field) else { continue };
            let reference = reference
                .as_str()
                .ok_or_else(|| recovery(format!("Invalid installed runtime receipt: data_routing.{field}")))?;
            let expected = resolve_reference(reference, title)?;
            let Some(value) = single_value(&arguments, option) else {
                return Err(recovery(format!("Installed data routing is missing or duplicated: {option}")));
            };
            if !matches!(value, Some(value) if same_path(&resolve(Path::new(value))?, &expected)) {
                return Err(recovery(format!("Launch profile points to another data folder: {option}")));
            }
        }
        if let Some(title_id) = routing.get("title_id") {
            if !matches!(single_value(&arguments, "--title-id"), Some(Some(value)) if eq_str(title_id, value)) {
                return Err(recovery("Launch profile names another installed title id"));
            }
        }
    }
    Ok(profile)
}
