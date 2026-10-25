// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `scene_lights.py`: the local scene light catalog, from the user's RomFS.
//!
//! Each room's emitting materials (lava and fire, windows and lamps, light
//! shafts, magic glows, white doorway volumes) become point lights named by
//! the content hashes of their surfaces. The release supplies the emitters
//! it computed for the supported rooms, keyed by room file SHA-256
//! (precomputed_tables); only rooms it does not cover are analysed here.
//! Format: compact ASCII JSON in insertion order, no final LF:
//!
//! ```text
//! {"schema":"triaevum.scene-lights","version":1,"rooms":[{"scene":10,"room":1,
//!  "masterQuest":false,"fingerprint":"<16 hex>","emitters":[...]}]}
//! ```
//!
//! The arithmetic reproduces CPython's (Neumaier `sum`, `math.hypot`,
//! round-half-even, float `//` and `%`, libm `cos`/`sin`/`atan2`/`pow`), so
//! the float spellings match on the same platform C library.

mod emitters;
mod model;
mod texels;

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use forge_base::hashing::sha256_hex;
use forge_base::legacy_json::{dumps, Options, Style};
use forge_base::{CancelToken, ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::pyvalue::{py_eq, recipe_romfs_source};
use crate::romfs::{image_files, sorted_entries};
use crate::{catalog_cache, precomputed_tables, Report};

pub use emitters::{category, is_category, room_emitters, room_fingerprint, MAX_SURFACES};
pub use texels::{is_halo, texture_colour};

pub const CATALOG_SCHEMA: &str = "triaevum.scene-lights";
pub const CATALOG_VERSION: i64 = 1;
/// Bumped with any change to the emitters' placement.
pub const GENERATOR_REVISION: i64 = 4;

/// Compact JSON in insertion order, as `json.dumps(..., separators=(",", ":"))`.
pub(crate) const COMPACT_UNSORTED: Options = Options { style: Style::Compact, sort_keys: false, ensure_ascii: true };

/// Scene file stems by scene id (the title's scene table order).
const SCENE_NAMES: [(i64, &str); 102] = [
    (0, "ydan"), (1, "ddan"), (2, "bdan"), (3, "bmori1"), (4, "hidan"), (5, "mizusin"), (6, "jyasinzou"),
    (7, "hakadan"), (8, "hakadan_ch"), (9, "ice_doukutu"), (10, "ganon"), (11, "men"), (12, "gerudoway"),
    (13, "ganontika"), (14, "ganon_sonogo"), (15, "ganontikasonogo"), (16, "takaraya"), (17, "ydan_boss"),
    (18, "ddan_boss"), (19, "bdan_boss"), (20, "moriboss"), (21, "fire_bs"), (22, "mizusin_boss"),
    (23, "jyasinzou_boss"), (24, "hakadan_boss"), (25, "ganon_boss"), (26, "ganon_final"), (27, "entra_day"),
    (28, "entra_night"), (29, "entra_ruins"), (30, "market_alley"), (31, "market_alley_n"), (32, "market_day"),
    (33, "market_night"), (34, "market_ruins"), (35, "shrine"), (36, "shrine_n"), (37, "shrine_r"), (38, "k_home"),
    (39, "k_home3"), (40, "k_home4"), (41, "k_home5"), (42, "kakariko"), (43, "kakariko_home3"), (44, "shop"),
    (45, "kokiri"), (46, "shop_golon"), (47, "zoora"), (48, "shop_drag"), (49, "shop_alley"), (50, "shop_night"),
    (51, "shop_face"), (52, "link"), (53, "kakariko_impa"), (54, "stable"), (55, "labo"), (56, "hylia_labo"),
    (57, "tent"), (58, "hut"), (59, "daiyousei_izumi"), (60, "yousei_izumi_tate"), (61, "yousei_izumi_yoko"),
    (62, "kakusiana"), (63, "hakaana"), (64, "hakaana2"), (65, "hakaana_ouke"), (66, "syatekijyou"),
    (67, "tokinoma"), (68, "kenjyanoma"), (69, "hairal_niwa"), (70, "hairal_niwa_n"), (71, "hiral_demo"),
    (72, "hakasitarelay"), (73, "turibori"), (74, "nakaniwa"), (75, "bowling"), (76, "souko"), (77, "miharigoya"),
    (78, "mahouya"), (79, "ganon_demo"), (80, "kinsuta"), (81, "spot00"), (82, "spot01"), (83, "spot02"),
    (84, "spot03"), (85, "spot04"), (86, "spot05"), (87, "spot06"), (88, "spot07"), (89, "spot08"), (90, "spot09"),
    (91, "spot10"), (92, "spot11"), (93, "spot12"), (94, "spot13"), (95, "spot15"), (96, "spot16"), (97, "spot17"),
    (98, "spot18"), (99, "spot20"), (100, "ganon_tou"), (107, "spot99"),
];

pub fn scene_id(name: &str) -> Option<i64> {
    SCENE_NAMES.iter().find(|entry| entry.1 == name).map(|entry| entry.0)
}

/// A room file path, `ROOM_FILE.fullmatch` of
/// `scene/(.+?)_(\d+)(_dd)?_info\.zsi`: (scene stem, room, Master Quest).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RoomFile {
    pub stem: String,
    pub room: i64,
    pub master_quest: bool,
}

/// The first (shortest-stem) way the lazy pattern matches the whole path.
/// `\d` is taken as ASCII digits; RomFS names hold no other digits.
pub fn parse_room_file(path: &str) -> Option<RoomFile> {
    let rest = path.strip_prefix("scene/")?;
    for (index, character) in rest.char_indices() {
        if character == '\n' {
            return None;
        }
        let stem_end = index + character.len_utf8();
        let Some(after) = rest[stem_end..].strip_prefix('_') else { continue };
        let digits = after.bytes().take_while(u8::is_ascii_digit).count();
        if digits == 0 {
            continue;
        }
        let master_quest = match &after[digits..] {
            "_dd_info.zsi" => true,
            "_info.zsi" => false,
            _ => continue,
        };
        // Python's int() of an arbitrarily long digit run.
        let room = after[..digits].parse().ok()?;
        return Some(RoomFile { stem: rest[..stem_end].to_string(), room, master_quest });
    }
    None
}

/// `romfs_room_files`: the scenes' room files, from a RomFS image or an
/// extracted RomFS directory, in Python's order.
pub fn romfs_room_files(
    romfs: &Path,
    cancel: &CancelToken,
    visit: &mut dyn FnMut(&str, &[u8]) -> Result<()>,
) -> Result<()> {
    if romfs.is_dir() {
        for (name, path) in sorted_entries(&romfs.join("scene")) {
            cancel.check()?;
            let relative = format!("scene/{name}");
            if glob_info_zsi(&name) && parse_room_file(&relative).is_some() {
                let data =
                    std::fs::read(&path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
                visit(&relative, &data)?;
            }
        }
        return Ok(());
    }
    image_files(romfs, &["scene"], cancel, &mut |relative, data| {
        if parse_room_file(&relative).is_some() {
            visit(&relative, &data)?;
        }
        Ok(())
    })
}

/// `glob("*_info.zsi")`: case-insensitive on Windows, like pathlib there.
fn glob_info_zsi(name: &str) -> bool {
    if cfg!(windows) {
        name.to_lowercase().ends_with("_info.zsi")
    } else {
        name.ends_with("_info.zsi")
    }
}

/// `build_catalog`: the catalog bytes and receipt summary. `known` holds the
/// release's emitters by room file SHA-256, used for exactly those rooms.
pub fn build_catalog(
    romfs: &Path,
    known: Option<&Map<String, Value>>,
    cancel: &CancelToken,
) -> Result<(Vec<u8>, Value)> {
    let known = known.filter(|rooms| !rooms.is_empty());
    let mut rooms: Vec<(i64, i64, bool, String, Vec<Value>)> = Vec::new();
    let mut reused = 0u64;
    romfs_room_files(romfs, cancel, &mut |path, data| {
        let Some(file) = parse_room_file(path) else { return Ok(()) };
        let Some(scene) = scene_id(&file.stem) else { return Ok(()) };
        let precomputed = known.and_then(|rooms| rooms.get(&sha256_hex(data)));
        let emitters = match precomputed {
            Some(emitters) => {
                reused += 1;
                emitters.as_array().cloned().unwrap_or_default()
            }
            None => room_emitters(data)?,
        };
        if !emitters.is_empty() {
            rooms.push((scene, file.room, file.master_quest, room_fingerprint(data), emitters));
        }
        Ok(())
    })?;
    rooms.sort_by_key(|room| (room.0, room.1, room.2));
    // A standard and a Master Quest room whose command lists begin alike
    // cannot be told apart at run time: one entry, the standard room's.
    let mut unique: Vec<(i64, i64, bool, String, Vec<Value>)> = Vec::new();
    let mut ambiguous = 0u64;
    for room in rooms {
        match unique.iter().find(|kept| kept.0 == room.0 && kept.1 == room.1 && kept.3 == room.3) {
            Some(kept) => {
                ambiguous += u64::from(!py_eq(&Value::Array(kept.4.clone()), &Value::Array(room.4.clone())));
            }
            None => unique.push(room),
        }
    }
    let mut counts: BTreeMap<String, u64> = BTreeMap::new();
    let mut entries = Vec::with_capacity(unique.len());
    for (scene, room, master_quest, fingerprint, emitters) in unique {
        for emitter in &emitters {
            let category = emitter.get("category").and_then(Value::as_str).unwrap_or_default();
            *counts.entry(category.to_string()).or_insert(0) += 1;
        }
        entries.push(json!({
            "scene": scene,
            "room": room,
            "masterQuest": master_quest,
            "fingerprint": fingerprint,
            "emitters": emitters,
        }));
    }
    let mut summary = Map::new();
    summary.insert("rooms".into(), json!(entries.len()));
    summary.insert("emitters".into(), json!(counts));
    if known.is_some() {
        summary.insert("precomputed_rooms".into(), json!(reused));
    }
    if ambiguous > 0 {
        summary.insert("ambiguous_rooms".into(), json!(ambiguous));
    }
    let catalog = json!({"schema": CATALOG_SCHEMA, "version": CATALOG_VERSION, "rooms": entries});
    Ok((dumps(&catalog, COMPACT_UNSORTED).into_bytes(), Value::Object(summary)))
}

/// `prepare_scene_lights`: the catalog for this RomFS, generated once and
/// reused while its receipt matches. A release table (integrity-checked by
/// the caller) supplies the rooms it covers. `None` when the recipe names
/// no RomFS.
pub fn prepare_scene_lights(
    data_root: &Path,
    recipe: &Value,
    romfs: &Path,
    table: Option<&Path>,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Option<PathBuf>> {
    let Some(source) = recipe_romfs_source(recipe) else { return Ok(None) };
    let mut identity = Map::new();
    identity.insert("catalog_version".into(), json!(CATALOG_VERSION));
    identity.insert("generator_revision".into(), json!(GENERATOR_REVISION));
    identity.insert("romfs_sha256".into(), source.clone());
    let mut known = None;
    if let Some(table) = table {
        let mut loaded = precomputed_tables::read_scene_lights_table(table)?;
        known = match loaded.remove("rooms") {
            Some(Value::Object(rooms)) => Some(rooms),
            _ => None,
        };
        let bytes =
            std::fs::read(table).map_err(|error| ForgeError::io(format!("Cannot read {}", table.display()), error))?;
        identity.insert("table_sha256".into(), json!(sha256_hex(&bytes)));
    }
    catalog_cache::prepare(data_root, "scene-lights", "scene_lights.json", identity, || {
        report("scene_lights", "Cataloguing the rooms' light sources...");
        build_catalog(romfs, known.as_ref(), cancel)
    })
    .map(Some)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn room_file_names() {
        let parse = |path: &str| parse_room_file(path).map(|file| (file.stem, file.room, file.master_quest));
        assert_eq!(parse("scene/ganon_1_info.zsi"), Some(("ganon".into(), 1, false)));
        assert_eq!(parse("scene/ganon_1_dd_info.zsi"), Some(("ganon".into(), 1, true)));
        assert_eq!(parse("scene/ydan_boss_12_info.zsi"), Some(("ydan_boss".into(), 12, false)));
        assert_eq!(parse("scene/spot04_info.zsi"), None);
        assert_eq!(parse("scene/ganon_info.zsi"), None);
        assert_eq!(parse("scene/a_1_2_info.zsi"), Some(("a_1".into(), 2, false)));
        assert_eq!(parse("scene/a_1_dd_dd_info.zsi"), None);
        assert_eq!(parse("actor/ganon_1_info.zsi"), None);
        assert_eq!(scene_id("spot99"), Some(107));
    }
}
