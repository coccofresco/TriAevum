// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The user's settings across an update, a repair or an installation (UX
//! plan WP-04A, owner decision 4): `D/config` keeps its bytes except for the
//! migrations publication is allowed to perform, and each of those is
//! reported ("Update changed: ...").
//!
//! The permitted migrations, all of them:
//!
//! - `created`: a missing settings file is created with its defaults (the
//!   standard TopScreen profile, the runtime's default configuration, the
//!   game language the ROM offers);
//! - `standard_hud_layout`: the TopScreen layout fields are reset to the
//!   standard profile, only when the user chose "Use standard";
//! - `language_selected`: an installation from a ROM selects the language the
//!   user chose for it.
//!
//! Any other difference is reported as `unexpected` (or `removed`): it is
//! never silent, and the tests require that none occurs.

use std::collections::BTreeMap;
use std::path::Path;

use forge_base::fsutil::load_json_object;
use forge_base::Result;
use forge_prepare::pyvalue::py_eq;
use serde_json::{json, Map, Value};

use crate::publish::{standard_topscreen, standard_topscreen_layout, HudLayout};

const TOPSCREEN: &str = "topscreen_ui.json";
const GAME_LANGUAGE: &str = "game_language.json";
/// Settings files are small; a larger file is compared by digest only.
const KEPT_BYTES: u64 = 1 << 20;

#[derive(Clone, Debug, PartialEq, Eq)]
enum Contents {
    Bytes(Vec<u8>),
    Digest(String),
}

/// The files of `D/config` (recursively) at one moment.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ConfigSnapshot {
    files: BTreeMap<String, Contents>,
}

impl ConfigSnapshot {
    /// Reads `data_root/config`; a missing folder is an empty snapshot and an
    /// unreadable file is recorded as such (it then shows as changed).
    pub fn take(data_root: &Path) -> Self {
        let mut snapshot = Self::default();
        snapshot.walk(&data_root.join("config"), "");
        snapshot
    }

    fn walk(&mut self, directory: &Path, prefix: &str) {
        let Ok(entries) = std::fs::read_dir(directory) else { return };
        for entry in entries.flatten() {
            let name = format!("{prefix}{}", entry.file_name().to_string_lossy());
            let Ok(kind) = entry.file_type() else { continue };
            if kind.is_dir() {
                self.walk(&entry.path(), &format!("{name}/"));
            } else if kind.is_file() {
                let contents = match entry.metadata() {
                    Ok(metadata) if metadata.len() > KEPT_BYTES => {
                        forge_base::hashing::sha256_file(&entry.path(), &forge_base::CancelToken::new())
                            .map(|(_, digest)| Contents::Digest(forge_base::hashing::hex(&digest)))
                            .unwrap_or_else(|error| Contents::Digest(format!("unreadable: {}", error.message)))
                    }
                    _ => match std::fs::read(entry.path()) {
                        Ok(bytes) => Contents::Bytes(bytes),
                        Err(error) => Contents::Digest(format!("unreadable: {error}")),
                    },
                };
                self.files.insert(name, contents);
            }
        }
    }

    fn json(&self, name: &str) -> Option<Map<String, Value>> {
        match self.files.get(name)? {
            Contents::Bytes(bytes) => forge_base::legacy_json::load_object(bytes).ok(),
            Contents::Digest(_) => None,
        }
    }
}

/// What happened to one settings file.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ChangeKind {
    Created,
    StandardHudLayout { fields: Vec<String> },
    LanguageSelected,
    Removed,
    Unexpected,
}

impl ChangeKind {
    pub fn code(&self) -> &'static str {
        match self {
            Self::Created => "created",
            Self::StandardHudLayout { .. } => "standard_hud_layout",
            Self::LanguageSelected => "language_selected",
            Self::Removed => "removed",
            Self::Unexpected => "unexpected",
        }
    }

    /// One of the enumerated migrations (not `removed` or `unexpected`).
    pub fn permitted(&self) -> bool {
        !matches!(self, Self::Removed | Self::Unexpected)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ConfigChange {
    /// Relative to the data folder, `/`-separated (`config/topscreen_ui.json`).
    pub file: String,
    pub kind: ChangeKind,
}

impl ConfigChange {
    pub fn to_value(&self) -> Value {
        let mut value = json!({"file": self.file, "change": self.kind.code()});
        if let ChangeKind::StandardHudLayout { fields } = &self.kind {
            value["fields"] = json!(fields);
        }
        value
    }
}

/// What the operation was allowed to change besides creating missing files.
#[derive(Clone, Copy, Debug, Default)]
pub struct Permitted {
    pub hud_layout: HudLayout,
    /// An installation from a ROM selects the chosen game language.
    pub language: bool,
}

/// The top-level keys whose values differ (Python equality: `1 == 1.0`).
fn changed_fields(before: &Map<String, Value>, after: &Map<String, Value>) -> Vec<String> {
    let mut fields: Vec<String> = before
        .keys()
        .chain(after.keys().filter(|key| !before.contains_key(*key)))
        .filter(|key| match (before.get(*key), after.get(*key)) {
            (Some(left), Some(right)) => !py_eq(left, right),
            _ => true,
        })
        .cloned()
        .collect();
    fields.sort();
    fields
}

/// Every difference between two snapshots, classified.
pub fn config_changes(before: &ConfigSnapshot, after: &ConfigSnapshot, permitted: Permitted) -> Vec<ConfigChange> {
    let names: std::collections::BTreeSet<&String> = before.files.keys().chain(after.files.keys()).collect();
    let mut changes = Vec::new();
    for name in names {
        let kind = match (before.files.get(name), after.files.get(name)) {
            (None, Some(_)) => ChangeKind::Created,
            (Some(_), None) => ChangeKind::Removed,
            (Some(old), Some(new)) if old == new => continue,
            _ if name == TOPSCREEN && permitted.hud_layout == HudLayout::Standard => {
                match (before.json(name), after.json(name)) {
                    (Some(old), Some(new)) => ChangeKind::StandardHudLayout { fields: changed_fields(&old, &new) },
                    _ => ChangeKind::Unexpected,
                }
            }
            _ if name == GAME_LANGUAGE && permitted.language => ChangeKind::LanguageSelected,
            _ => ChangeKind::Unexpected,
        };
        changes.push(ConfigChange { file: format!("config/{name}"), kind });
    }
    changes
}

/// The changes as the job results carry them.
pub fn changes_value(changes: &[ConfigChange]) -> Value {
    Value::Array(changes.iter().map(ConfigChange::to_value).collect())
}

/// One layout field "Use standard" would change.
#[derive(Clone, Debug, PartialEq)]
pub struct LayoutField {
    pub name: String,
    pub yours: Value,
    pub standard: Value,
}

/// The decision-4 preview: what "Use standard" would change in the user's
/// TopScreen file. Nothing when the file is missing (it is created from the
/// standard profile either way), unreadable or of another schema.
pub fn hud_layout_preview(data_root: &Path) -> Result<Vec<LayoutField>> {
    let path = data_root.join("config").join(TOPSCREEN);
    if !path.is_file() {
        return Ok(Vec::new());
    }
    let Ok(user) = load_json_object(&path) else { return Ok(Vec::new()) };
    let standard = standard_topscreen()?;
    let migrated = standard_topscreen_layout(&user, &standard)?;
    Ok(changed_fields(&user, &migrated)
        .into_iter()
        .map(|name| LayoutField {
            yours: user.get(&name).cloned().unwrap_or(Value::Null),
            standard: migrated.get(&name).cloned().unwrap_or(Value::Null),
            name,
        })
        .collect())
}

/// `{"differs", "fields": [{"name", "yours", "standard"}]}`.
pub fn preview_value(fields: &[LayoutField]) -> Value {
    json!({
        "differs": !fields.is_empty(),
        "fields": fields.iter().map(|field| json!({
            "name": field.name, "yours": field.yours, "standard": field.standard,
        })).collect::<Vec<_>>(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(root: &Path, name: &str, value: &Value) {
        let path = root.join("config").join(name);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        forge_base::fsutil::atomic_write_json(&path, value).unwrap();
    }

    #[test]
    fn every_difference_is_classified() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        let standard = standard_topscreen().unwrap();
        let mut user = standard.clone();
        user.insert("hud_margin_x".into(), json!(40));
        write(root, TOPSCREEN, &Value::Object(user.clone()));
        write(root, "TriAevum.json", &json!({"Graphics": {"Preset": "Enhanced"}}));
        write(root, GAME_LANGUAGE, &json!({"selected": "en"}));
        let before = ConfigSnapshot::take(root);
        assert!(config_changes(&before, &ConfigSnapshot::take(root), Permitted::default()).is_empty());

        let migrated = standard_topscreen_layout(&user, &standard).unwrap();
        write(root, TOPSCREEN, &Value::Object(migrated));
        write(root, GAME_LANGUAGE, &json!({"selected": "it"}));
        write(root, "romfs_mods.json", &json!({"enabled": []}));
        std::fs::remove_file(root.join("config/TriAevum.json")).unwrap();
        let after = ConfigSnapshot::take(root);

        let kept = config_changes(&before, &after, Permitted::default());
        let codes: Vec<(&str, &str)> = kept.iter().map(|change| (change.file.as_str(), change.kind.code())).collect();
        assert_eq!(
            codes,
            [
                ("config/TriAevum.json", "removed"),
                ("config/game_language.json", "unexpected"),
                ("config/romfs_mods.json", "created"),
                ("config/topscreen_ui.json", "unexpected"),
            ]
        );
        let chosen = config_changes(&before, &after, Permitted { hud_layout: HudLayout::Standard, language: true });
        assert_eq!(chosen[1].kind, ChangeKind::LanguageSelected);
        assert_eq!(chosen[3].kind, ChangeKind::StandardHudLayout { fields: vec!["hud_margin_x".into()] });
        assert_eq!(chosen[3].to_value()["fields"], json!(["hud_margin_x"]));
        assert!(!chosen[0].kind.permitted() && chosen[3].kind.permitted());
    }

    #[test]
    fn the_preview_lists_what_use_standard_would_change() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        assert!(hud_layout_preview(root).unwrap().is_empty(), "no file: nothing to choose");
        let standard = standard_topscreen().unwrap();
        write(root, TOPSCREEN, &Value::Object(standard.clone()));
        assert!(hud_layout_preview(root).unwrap().is_empty());
        let mut user = standard.clone();
        user.insert("hud_margin_y".into(), json!(25));
        user.insert("hud_scale".into(), json!(0.85));
        user.insert("camera_fov_percent".into(), json!(120));
        write(root, TOPSCREEN, &Value::Object(user));
        let fields = hud_layout_preview(root).unwrap();
        let names: Vec<&str> = fields.iter().map(|field| field.name.as_str()).collect();
        assert_eq!(names, ["hud_margin_y", "hud_scale"], "camera choices are not layout");
        assert_eq!(fields[0].yours, json!(25));
        assert_eq!(fields[0].standard, standard["hud_margin_y"]);
        assert_eq!(preview_value(&fields)["differs"], true);
    }
}
