// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Forge language the user picked on About, kept in Forge's own
//! settings `data/config/forge.json` (UX plan section 3.1), never in the
//! game's configuration. The window still follows decision 8: a kept
//! language the ROM in use does not provide is skipped (not forgotten), and
//! the window falls back to the system language, then English.
//!
//! The same file keeps the HD interface pack choice (`hd_interface_pack`,
//! owner decision 5, UX plan WP-07), which the engine reads for every
//! preparation (`forge_core::enhancements`); each writer keeps the other's
//! members.

use std::path::{Path, PathBuf};

use serde_json::{Map, Value};

pub const FORMAT: &str = forge_core::enhancements::PREFERENCES_FORMAT;
const KEY: &str = "ui_language";

pub fn path(data_root: &Path) -> PathBuf {
    data_root.join("config").join("forge.json")
}

/// A code Forge can keep: two lowercase ASCII letters.
fn valid(code: &str) -> bool {
    code.len() == 2 && code.bytes().all(|byte| byte.is_ascii_lowercase())
}

/// The kept language, None when there is none or the file is unreadable
/// (the window then starts as if nothing was chosen).
pub fn load(data_root: &Path) -> Option<String> {
    let path = path(data_root);
    if !path.is_file() {
        return None;
    }
    let document = forge_base::fsutil::load_json_object(&path).ok()?;
    document.get(KEY)?.as_str().filter(|code| valid(code)).map(str::to_string)
}

/// Keeps `code` as the Forge language; the file's other members stay. The
/// pseudo-locale (a developer switch) is never kept.
pub fn save(data_root: &Path, code: &str) -> Result<(), String> {
    if !valid(code) {
        return Ok(());
    }
    let path = path(data_root);
    let mut document = if path.is_file() {
        forge_base::fsutil::load_json_object(&path).unwrap_or_else(|_| Map::new())
    } else {
        Map::new()
    };
    document.insert("format".to_string(), Value::from(FORMAT));
    document.insert(KEY.to_string(), Value::from(code));
    forge_base::fsutil::atomic_write_json(&path, &Value::Object(document)).map_err(|error| error.message)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_choice_is_kept_in_forge_settings() {
        let root = tempfile::tempdir().unwrap();
        assert_eq!(load(root.path()), None);
        save(root.path(), "it").unwrap();
        assert_eq!(load(root.path()).as_deref(), Some("it"));
        let document = forge_base::fsutil::load_json_object(&path(root.path())).unwrap();
        assert_eq!(document["format"], FORMAT);
        // Not the game's configuration.
        assert!(!forge_native::language::config_path(root.path()).exists());
        save(root.path(), "de").unwrap();
        assert_eq!(load(root.path()).as_deref(), Some("de"));
    }

    #[test]
    fn other_members_stay_and_bad_values_are_ignored() {
        let root = tempfile::tempdir().unwrap();
        let file = path(root.path());
        std::fs::create_dir_all(file.parent().unwrap()).unwrap();
        std::fs::write(&file, r#"{"other": 3, "ui_language": "ITALIAN"}"#).unwrap();
        assert_eq!(load(root.path()), None);
        save(root.path(), "pseudo").unwrap();
        assert_eq!(load(root.path()), None, "the pseudo-locale is not kept");
        save(root.path(), "fr").unwrap();
        let document = forge_base::fsutil::load_json_object(&file).unwrap();
        assert_eq!(document["other"], 3);
        assert_eq!(document["ui_language"], "fr");
        // The HD interface pack choice shares the file (WP-07).
        forge_core::enhancements::set_hd_pack_enabled(root.path(), false).unwrap();
        save(root.path(), "it").unwrap();
        assert!(!forge_core::enhancements::hd_pack_enabled(root.path()));
        assert_eq!(load(root.path()).as_deref(), Some("it"));
        std::fs::write(&file, "not json").unwrap();
        assert_eq!(load(root.path()), None);
        save(root.path(), "es").unwrap();
        assert_eq!(load(root.path()).as_deref(), Some("es"));
    }
}
