// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `precomputed_tables.py`, Forge side: the release's interior-sun mask
//! table and scene-light table readers, the scene-light table builder and
//! the title's table lookup. The tables hold TriAevum's own analysis keyed
//! by room content, never game data; a table only decides whether a room is
//! computed again, never what is computed.
//!
//! The publisher steps (`generate`, `bind_precomputed_tables`) and the
//! release audit (`validate_precomputed_tables`) stay in the Python release
//! tooling and are not ported.

use std::fs::File;
use std::io::{BufReader, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use forge_base::fsutil::load_json_object;
use forge_base::hashing::sha256_hex;
use forge_base::legacy_json::dumps;
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::fnv::fold;
use crate::interior_sun::CRITERION_VERSION;
use crate::pyvalue::py_eq;
use crate::scene_lights::{self, is_category, parse_room_file, room_emitters, scene_id, MAX_SURFACES};

pub const FORMAT: &str = "triaevum_precomputed_tables_v1";
pub const MASKS_PATH: &str = "forge/precomputed/interior-sun.masks";
pub const LIGHTS_PATH: &str = "forge/precomputed/scene-lights.json";
pub const LIGHTS_SCHEMA: &str = "triaevum.scene-lights-rooms";
const MASKS_MAGIC: &[u8; 4] = b"TISP";
const MASKS_FORMAT: u32 = 1;
const MAX_MASKS_BYTES: u64 = 256 * 1024 * 1024;
const MAX_LIGHTS_BYTES: u64 = 16 * 1024 * 1024;
const MAX_TRIANGLES: u32 = 500_000;
const EMITTER_KEYS: [&str; 7] = ["category", "position", "radius", "gain", "colour", "texture", "surfaces"];
const REQUIRED_EMITTER_KEYS: [&str; 5] = ["category", "position", "radius", "gain", "colour"];
const CHECKSUM_BASIS: u64 = 1_469_598_103_934_665_603;

fn table_error(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::ArtifactIntegrity, message)
}

/// `path.is_symlink() or not path.is_file()`, with the file size.
fn regular_file_size(path: &Path) -> Option<u64> {
    let link = std::fs::symlink_metadata(path).ok()?;
    if link.file_type().is_symlink() {
        return None;
    }
    let metadata = std::fs::metadata(path).ok()?;
    metadata.is_file().then_some(metadata.len())
}

/// Summary of a valid mask table.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct MaskTableSummary {
    pub entries: u32,
    pub masks: u64,
    pub bound_fallbacks: u64,
}

/// `read_interior_sun_masks`: `TISP` + `<3I>` version 1, criterion 3,
/// count; sorted `<QIIQ>` entries (fingerprint, triangles, kind 0 masks /
/// 1 fallback, first mask); contiguous u64 masks; then the renderer-FNV
/// checksum of everything before it. Streamed, never loaded whole.
pub fn read_interior_sun_masks(path: &Path, cancel: &CancelToken) -> Result<MaskTableSummary> {
    let size = regular_file_size(path)
        .filter(|size| (24..=MAX_MASKS_BYTES).contains(size))
        .ok_or_else(|| table_error("Invalid interior-sun mask table file"))?;
    let io = |error| ForgeError::io(format!("Cannot read {}", path.display()), error);
    let mut stream = BufReader::new(File::open(path).map_err(io)?);
    let mut header = [0u8; 16];
    stream.read_exact(&mut header).map_err(io)?;
    let field = |index: usize| u32::from_le_bytes(header[index * 4..index * 4 + 4].try_into().expect("four bytes"));
    let (version, criterion, count) = (field(1), field(2), field(3));
    if &header[..4] != MASKS_MAGIC || version != MASKS_FORMAT || criterion != CRITERION_VERSION {
        return Err(table_error("Incompatible interior-sun mask table"));
    }
    let body = size - 8;
    let mut checksum = fold(CHECKSUM_BASIS, &header);
    let mut remaining = body - 16;
    let mut buffer = vec![0u8; 1 << 20];
    while remaining > 0 {
        cancel.check()?;
        let take = remaining.min(buffer.len() as u64) as usize;
        stream.read_exact(&mut buffer[..take]).map_err(io)?;
        checksum = fold(checksum, &buffer[..take]);
        remaining -= take as u64;
    }
    let mut stored = [0u8; 8];
    stream.read_exact(&mut stored).map_err(io)?;
    if u64::from_le_bytes(stored) != checksum {
        return Err(table_error("Interior-sun mask table checksum differs"));
    }
    let masks_at = 16 + u64::from(count) * 24;
    if masks_at > body || (body - masks_at) % 8 != 0 {
        return Err(table_error("Invalid interior-sun mask table extent"));
    }
    stream.seek(SeekFrom::Start(16)).map_err(io)?;
    let (mut previous, mut following, mut fallbacks) = (None::<u64>, 0u64, 0u64);
    let mut entry = [0u8; 24];
    for _ in 0..count {
        stream.read_exact(&mut entry).map_err(io)?;
        let fingerprint = u64::from_le_bytes(entry[..8].try_into().expect("eight bytes"));
        let triangles = u32::from_le_bytes(entry[8..12].try_into().expect("four bytes"));
        let kind = u32::from_le_bytes(entry[12..16].try_into().expect("four bytes"));
        let first = u64::from_le_bytes(entry[16..].try_into().expect("eight bytes"));
        if previous.is_some_and(|previous| fingerprint <= previous)
            || kind > 1
            || first != following
            || (kind == 1 && triangles != 0)
            || triangles > MAX_TRIANGLES
        {
            return Err(table_error("Invalid interior-sun mask table entry"));
        }
        previous = Some(fingerprint);
        following += u64::from(triangles);
        fallbacks += u64::from(kind);
    }
    if following != (body - masks_at) / 8 {
        return Err(table_error("Invalid interior-sun mask table mask count"));
    }
    Ok(MaskTableSummary { entries: count, masks: following, bound_fallbacks: fallbacks })
}

fn is_hex(text: &str, length: usize) -> bool {
    text.len() == length && text.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}

/// `type(value) in (int, float) and math.isfinite(value)`.
fn finite_number(value: &Value) -> bool {
    value.as_f64().is_some_and(f64::is_finite)
}

fn valid_emitter(emitter: &Value) -> bool {
    let Some(emitter) = emitter.as_object() else { return false };
    if emitter.keys().any(|key| !EMITTER_KEYS.contains(&key.as_str()))
        || REQUIRED_EMITTER_KEYS.iter().any(|key| !emitter.contains_key(*key))
    {
        return false;
    }
    let triple = |key: &str| emitter[key].as_array().filter(|items| items.len() == 3);
    let (Some(position), Some(colour)) = (triple("position"), triple("colour")) else { return false };
    let texture_ok = emitter.get("texture").is_none_or(|texture| {
        texture.as_str().is_some_and(|name| {
            (1..=64).contains(&name.len())
                && name.bytes().all(|b| b.is_ascii_alphanumeric() || matches!(b, b'_' | b'.' | b'-'))
        })
    });
    let surfaces_ok = emitter.get("surfaces").is_none_or(|surfaces| {
        surfaces.as_array().is_some_and(|items| {
            items.len() <= MAX_SURFACES && items.iter().all(|item| item.as_str().is_some_and(|s| is_hex(s, 16)))
        })
    });
    emitter["category"].as_str().is_some_and(is_category)
        && position.iter().chain(colour).chain([&emitter["radius"], &emitter["gain"]]).all(finite_number)
        && texture_ok
        && surfaces_ok
}

/// `read_scene_lights_table`: the validated table; emitters may hold only
/// the catalog's own fields.
pub fn read_scene_lights_table(path: &Path) -> Result<Map<String, Value>> {
    regular_file_size(path)
        .filter(|size| *size <= MAX_LIGHTS_BYTES)
        .ok_or_else(|| table_error("Invalid scene-light table file"))?;
    let table = load_json_object(path)?;
    let expected_keys = ["schema", "version", "catalog_version", "generator_revision", "romfs_sha256", "rooms"];
    let compatible = table.len() == expected_keys.len()
        && expected_keys.iter().all(|key| table.contains_key(*key))
        && table["schema"] == LIGHTS_SCHEMA
        && py_eq(&table["version"], &json!(1))
        && py_eq(&table["catalog_version"], &json!(scene_lights::CATALOG_VERSION))
        && py_eq(&table["generator_revision"], &json!(scene_lights::GENERATOR_REVISION))
        && table["romfs_sha256"].as_str().is_some_and(|digest| is_hex(digest, 64))
        && table["rooms"].as_object().is_some_and(|rooms| !rooms.is_empty());
    if !compatible {
        return Err(table_error("Incompatible scene-light table"));
    }
    for (digest, emitters) in table["rooms"].as_object().expect("checked above") {
        let Some(emitters) = emitters.as_array().filter(|_| is_hex(digest, 64)) else {
            return Err(table_error("Invalid scene-light table room"));
        };
        if !emitters.iter().all(valid_emitter) {
            return Err(table_error("Invalid scene-light table emitter"));
        }
    }
    Ok(table)
}

/// `scene_lights_table`: the publisher's table of every catalogued room's
/// emitters, keyed by room file SHA-256 (compact JSON, no final LF).
pub fn scene_lights_table(romfs: &Path, romfs_sha256: &str, cancel: &CancelToken) -> Result<Vec<u8>> {
    let mut rooms = std::collections::BTreeMap::new();
    scene_lights::romfs_room_files(romfs, cancel, &mut |path, data| {
        if parse_room_file(path).is_some_and(|file| scene_id(&file.stem).is_some()) {
            rooms.insert(sha256_hex(data), Value::Array(room_emitters(data)?));
        }
        Ok(())
    })?;
    let table = json!({
        "schema": LIGHTS_SCHEMA,
        "version": 1,
        "catalog_version": scene_lights::CATALOG_VERSION,
        "generator_revision": scene_lights::GENERATOR_REVISION,
        "romfs_sha256": romfs_sha256,
        "rooms": rooms,
    });
    Ok(dumps(&table, scene_lights::COMPACT_UNSORTED).into_bytes())
}

/// The title's integrity-checked tables.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TitleTables {
    pub interior_sun_masks: Option<PathBuf>,
    pub scene_lights: Option<PathBuf>,
}

/// `title_tables`: the title's tables, each verified by `checked_file`
/// (`precompiled_titles.checked_file(root, record)`, owned by the catalog
/// crate), or none when the title names none.
pub fn title_tables(title: &Value, checked_file: &mut dyn FnMut(&Value) -> Result<PathBuf>) -> Result<TitleTables> {
    let Some(tables) = title.get("precomputed_tables").filter(|tables| crate::pyvalue::truthy(tables)) else {
        return Ok(TitleTables::default());
    };
    if tables.get("format").and_then(Value::as_str) != Some(FORMAT) {
        return Err(table_error("Incompatible precomputed tables"));
    }
    let mut record = |key: &str| -> Result<PathBuf> {
        let value = tables.get(key).ok_or_else(|| table_error("Incompatible precomputed tables"))?;
        checked_file(value)
    };
    Ok(TitleTables { interior_sun_masks: Some(record("interior_sun_masks")?), scene_lights: Some(record("scene_lights")?) })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn masks_table(entries: &[(u64, u32, u32, u64)], masks: &[u64]) -> Vec<u8> {
        let mut data = b"TISP".to_vec();
        for value in [1u32, 3, entries.len() as u32] {
            data.extend(value.to_le_bytes());
        }
        for (fingerprint, triangles, kind, first) in entries {
            data.extend(fingerprint.to_le_bytes());
            data.extend(triangles.to_le_bytes());
            data.extend(kind.to_le_bytes());
            data.extend(first.to_le_bytes());
        }
        for mask in masks {
            data.extend(mask.to_le_bytes());
        }
        let checksum = fold(CHECKSUM_BASIS, &data);
        data.extend(checksum.to_le_bytes());
        data
    }

    #[test]
    fn mask_table_holds_only_fingerprints_counts_and_masks() {
        let directory = tempfile::tempdir().unwrap();
        let write = |name: &str, data: &[u8]| {
            let path = directory.path().join(name);
            std::fs::write(&path, data).unwrap();
            path
        };
        let cancel = CancelToken::new();
        let good = masks_table(&[(1, 2, 0, 0), (5, 0, 1, 2), (9, 1, 0, 2)], &[3, 0, 1 << 63]);
        assert_eq!(
            read_interior_sun_masks(&write("good.masks", &good), &cancel).unwrap(),
            MaskTableSummary { entries: 3, masks: 3, bound_fallbacks: 1 }
        );
        let mut damaged = good.clone();
        damaged[16] ^= 1;
        let mut criterion = good.clone();
        criterion[8..12].copy_from_slice(&2u32.to_le_bytes());
        for (name, data) in [
            ("damaged", damaged),
            ("unordered", masks_table(&[(9, 1, 0, 0), (1, 1, 0, 1)], &[0, 0])),
            ("gap", masks_table(&[(1, 1, 0, 1)], &[0, 0])),
            ("fallback-masks", masks_table(&[(1, 1, 1, 0)], &[0])),
            ("criterion", criterion),
        ] {
            assert!(read_interior_sun_masks(&write(name, &data), &cancel).is_err(), "{name}");
        }
    }

    #[test]
    fn title_without_tables_has_none() {
        let mut never = |_: &Value| -> Result<PathBuf> { unreachable!() };
        assert_eq!(title_tables(&json!({}), &mut never).unwrap(), TitleTables::default());
        assert!(title_tables(&json!({"precomputed_tables": {"format": "x"}}), &mut never).is_err());
    }
}
