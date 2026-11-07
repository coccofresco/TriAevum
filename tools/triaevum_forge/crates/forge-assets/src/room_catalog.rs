// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//! Publisher-computed room analysis, matched against the user's own content.
//! No geometry or textures in the table. Unsupported content is an error, not
//! a partial catalog or a foreign-title fallback. Title facts come from recipes.
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_base::hashing::{hex, is_sha256_hex, sha256_file, sha256_hex};
use forge_base::legacy_json::{dumps, Options, Style};
use serde_json::{json, Value};
use crate::{catalog_cache, model_catalog, romfs, Report};

fn bad() -> ForgeError { ForgeError::new(ErrorKind::ArtifactIntegrity, "Invalid or uncovered room analysis; regenerate with this title's publisher tools") }
fn hex16(v: &Value) -> bool { v.as_str().is_some_and(|s| s.len() == 16 && s.bytes().all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))) }
fn triple(v: &Value, colour: bool) -> bool { v.as_array().is_some_and(|a| a.len() == 3 && a.iter().all(|n| n.as_f64().is_some_and(|n| n.is_finite() && (!colour || (0.0..=4.0).contains(&n))))) }
fn indexed_name(path: &str, directory: &str, suffix: &str, separator: &str) -> bool {
    path.strip_prefix(&format!("{directory}/")).and_then(|s| s.strip_suffix(suffix))
        .and_then(|s| s.rsplit_once(separator)).is_some_and(|(stem, n)| !stem.is_empty() && !n.is_empty() && n.bytes().all(|b| b.is_ascii_digit()))
}

pub fn prepare(data_root: &Path, recipe: &Value, romfs_path: &Path, report: Report<'_>, cancel: &CancelToken) -> Result<PathBuf> {
    let profile = &recipe["room_catalog"];
    let title = profile["title"].as_str().filter(|s| !s.is_empty() && s.len() <= 32 && s.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')).ok_or_else(bad)?;
    let directory = profile["directory"].as_str().filter(|s| !s.is_empty() && s.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')).ok_or_else(bad)?;
    let token = |key: &str| profile[key].as_str().filter(|s| !s.is_empty() && s.len() <= 64 && s.bytes().all(|b| b.is_ascii_alphanumeric() || b"_.-".contains(&b))).ok_or_else(bad);
    let suffix = token("filename_suffix")?;
    let separator = token("index_separator")?;
    let room_name = |path: &str, directory: &str| indexed_name(path, directory, suffix, separator);
    let source = recipe["inputs"]["romfs"]["sha256"].as_str().filter(|s| is_sha256_hex(s)).ok_or_else(bad)?;
    let generator = profile["generator_sha256"].as_str().filter(|s| is_sha256_hex(s)).ok_or_else(bad)?;
    // Preserve the publisher's decimal spelling (CPython and Rust may choose
    // different shortest strings for the same binary64 halfway value).
    let payload = profile["catalog_json"].as_str().filter(|s| s.is_ascii() && s.len() <= 16 * 1024 * 1024).ok_or_else(bad)?;
    let catalog: Value = serde_json::from_str(payload).map_err(|_| bad())?;
    if profile["version"] != 1 || catalog["schema"] != "triaevum.scene-lights" || catalog["version"] != 1 || catalog["title"] != title
        || !catalog["rejected"].as_array().is_some_and(|a| a.is_empty()) { return Err(bad()); }
    let rows = catalog["rooms"].as_array().filter(|a| !a.is_empty() && a.len() <= 4096).ok_or_else(bad)?;
    let mut expected = BTreeMap::new();
    let mut keys = BTreeSet::new();
    let mut emitters = 0usize;
    for row in rows {
        let path = row["source"].as_str().filter(|p| room_name(p, directory) && !p.contains("..") && !p.contains('\\')).ok_or_else(bad)?;
        let digest = row["sourceSha256"].as_str().filter(|s| is_sha256_hex(s)).ok_or_else(bad)?;
        let scene = row["scene"].as_u64().filter(|n| *n <= 65535).ok_or_else(bad)?;
        let room = row["room"].as_i64().filter(|n| (-1..=127).contains(n)).ok_or_else(bad)?;
        if !hex16(&row["fingerprint"]) || !keys.insert((scene, room, row["fingerprint"].as_str().unwrap())) || expected.insert(path, digest).is_some() { return Err(bad()); }
        let lights = row["emitters"].as_array().filter(|a| a.len() <= 256).ok_or_else(bad)?;
        emitters += lights.len();
        for light in lights {
            if !light["category"].as_str().is_some_and(crate::scene_lights::is_category)
                || !triple(&light["position"], false) || !triple(&light["colour"], true)
                || !light["radius"].as_f64().is_some_and(|n| n.is_finite() && n > 0.0)
                || !light["gain"].as_f64().is_some_and(|n| n.is_finite() && (0.0..=4.0).contains(&n))
                || !light["surfaces"].as_array().is_some_and(|a| !a.is_empty() && a.len() <= 2 && a.iter().all(hex16))
                || !light["independentEmission"].is_boolean() { return Err(bad()); }
        }
    }
    if let Some(excluded) = profile.get("excluded_rooms") {
        let excluded = excluded.as_array().filter(|a| a.len() <= 4096).ok_or_else(bad)?;
        for row in excluded {
            let path = row["source"].as_str().filter(|p| room_name(p, directory) && !p.contains("..") && !p.contains('\\')).ok_or_else(bad)?;
            let digest = row["sourceSha256"].as_str().filter(|s| is_sha256_hex(s)).ok_or_else(bad)?;
            if !row["reason"].as_str().is_some_and(|s| !s.is_empty() && s.len() <= 256) || expected.insert(path, digest).is_some() { return Err(bad()); }
        }
    }
    // Do not reuse a receipt under a caller-supplied, stale ROM identity.
    if romfs_path.is_file() && hex(&sha256_file(romfs_path, cancel)?.1) != source { return Err(bad()); }
    let mut found = BTreeSet::new();
    let mut visit = |path: &str, packed: &[u8]| -> Result<()> {
        if !room_name(path, directory) { return Ok(()); }
        let digest = expected.get(path).ok_or_else(bad)?;
        let data = model_catalog::unpack_lzs(packed)?;
        if sha256_hex(&data) != *digest || !found.insert(path.to_owned()) { return Err(bad()); }
        Ok(())
    };
    if romfs_path.is_dir() {
        for (name, path) in romfs::sorted_entries(&romfs_path.join(directory)) {
            cancel.check()?;
            let relative = format!("{directory}/{name}");
            if room_name(&relative, directory) {
                let bytes = std::fs::read(&path).map_err(|e| ForgeError::io("Cannot read room", e))?;
                visit(&relative, &bytes)?;
            }
        }
    } else {
        romfs::image_files(romfs_path, &[directory], cancel, &mut |name, data| visit(&name, &data))?;
    }
    if found.len() != expected.len() { return Err(bad()); }
    let options = Options { style: Style::Compact, sort_keys: true, ensure_ascii: true };
    let identity = json!({"catalog_version": 1, "room_table_revision": 1, "romfs_sha256": source,
        "generator_sha256": generator, "profile_sha256": sha256_hex(dumps(profile, options).as_bytes())});
    catalog_cache::prepare_repairable(data_root, "scene-lights", "scene_lights.json", identity.as_object().unwrap().clone(), || {
        report("scene_lights", "Matching the title's computed room analysis to verified local content...");
        Ok((payload.as_bytes().to_vec(), json!({"rooms": rows.len(), "emitters": emitters, "content_verified": found.len()})))
    })
}
