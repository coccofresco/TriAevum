// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `forge_gui.load_active_title`: the installed title the game would play,
//! or `None` when it is missing, unreadable or not ready. Belongs in
//! forge-core next to the activation code; kept here until the lead moves it.

use std::path::{Path, PathBuf};

use forge_base::CancelToken;
use forge_prepare::content::load_prepared_content;
use forge_prepare::paths::{resolve, resolve_reference};
use serde_json::Value;

pub const ACTIVE_TITLE_FORMAT: &str = "triaevum_active_title_v1";

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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_malformed_or_unprepared_states_are_not_playable() {
        let directory = tempfile::tempdir().unwrap();
        let state = directory.path().join("active-title.json");
        assert_eq!(load_active_title(&state), None);
        std::fs::write(&state, b"not json").unwrap();
        assert_eq!(load_active_title(&state), None);
        std::fs::create_dir(directory.path().join("title")).unwrap();
        std::fs::write(
            &state,
            br#"{"format": "triaevum_active_title_v1", "directory": "title", "recipe": "r", "module_sha256": "x"}"#,
        )
        .unwrap();
        assert_eq!(load_active_title(&state), None);
    }
}
