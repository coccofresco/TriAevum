// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `forge_gui.load_active_title`: the installed title the game would play,
//! or `None` when it is missing, unreadable or not ready (the same reading
//! as the desktop window's `active_title.rs`, for refresh and repair).

use std::path::{Path, PathBuf};

use forge_base::CancelToken;
use forge_prepare::content::load_prepared_content;
use forge_prepare::paths::{resolve, resolve_reference};
use serde_json::Value;

pub const ACTIVE_TITLE_FORMAT: &str = "triaevum_active_title_v1";
pub const ACTIVE_TITLE: &str = "active-title.json";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ActiveTitle {
    pub recipe_id: String,
    pub directory: PathBuf,
    pub module_sha256: String,
}

/// Python's `str(value)` for the fields read here.
fn text(value: Option<&Value>) -> String {
    match value {
        None => String::new(),
        Some(Value::String(text)) => text.clone(),
        Some(other) => other.to_string(),
    }
}

pub fn load_active_title(state_path: &Path) -> Option<ActiveTitle> {
    let state_path = resolve(state_path).ok()?;
    if !state_path.is_file() {
        return None;
    }
    let active = forge_base::fsutil::load_json_object(&state_path).ok()?;
    let directory = resolve_reference(&text(active.get("directory")), state_path.parent()?).ok()?;
    let prepared = load_prepared_content(&directory, &[], &CancelToken::new()).ok()?;
    let module = prepared.state.get("module").and_then(Value::as_object)?;
    let ready = active.get("format").and_then(Value::as_str) == Some(ACTIVE_TITLE_FORMAT)
        && module.get("status").and_then(Value::as_str) == Some("ready")
        && active.get("module_sha256") == module.get("sha256");
    ready.then(|| ActiveTitle {
        recipe_id: text(active.get("recipe")),
        directory,
        module_sha256: text(active.get("module_sha256")),
    })
}

/// The title directory `active-title.json` names, ready or not (repair
/// rebuilds a damaged one).
pub fn named_directory(state_path: &Path) -> Option<PathBuf> {
    let active = forge_base::fsutil::load_json_object(state_path).ok()?;
    let parent = state_path.parent()?;
    resolve_reference(active.get("directory")?.as_str()?, parent).ok()
}
