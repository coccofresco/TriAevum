// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `prepare_qbf_font_pack.prepare_font_pack`: the shipped HD Latin font
//! paired with the ROM's own native fonts in `font_coverage.zip`.
//!
//! The archive is stored, members sorted (`coverage/<sha>.qbf`,
//! `native/<sha>.qbf`) then `manifest.json` (indent-2 sorted JSON without a
//! final LF), with fixed timestamps, so it is byte-reproducible.

use std::collections::BTreeMap;
use std::path::Path;

use forge_base::hashing::sha256_hex;
use forge_base::legacy_json::{dumps, Options};
use forge_base::{ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::qbf::{pair_font_coverage, parse_qbf, MAX_FONT_BYTES};
use crate::romfs::read_level3_file;
use crate::zip::StoredZip;

/// SingleScreen 3.1's HD Latin font replaces this font in both regions'
/// RomFS; a ROM need not contain the other region's.
pub const NATIVE_FONTS: [&str; 2] = ["message/eu/ltn16.qbf", "message/us/ltn16.qbf"];
pub const ATTRIBUTION: &str = "SingleScreen (TopScreen / Single Screen Experience) by M-1 / rlgcarrot";

/// Pairs the RomFS image's Latin fonts with `font` and publishes `output`
/// only after every font validated. Returns the manifest.
pub fn prepare_font_pack(font: &Path, romfs: &Path, output: &Path) -> Result<Map<String, Value>> {
    let read_error = |error| ForgeError::io(format!("Cannot read {}", font.display()), error);
    let size = std::fs::metadata(font).map_err(read_error)?.len();
    if size > MAX_FONT_BYTES as u64 {
        return Err(ForgeError::invalid(format!("oversized QBF: {}", font.display())));
    }
    let font_bytes = std::fs::read(font).map_err(read_error)?;
    let replacement = parse_qbf(&font_bytes)?;
    let mut assets: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    let mut fonts = Vec::new();
    for native_path in NATIVE_FONTS {
        let Some(native_bytes) = read_level3_file(romfs, native_path)? else { continue };
        let native = parse_qbf(&native_bytes)?;
        let contract = pair_font_coverage(&native, &replacement)?;
        let asset = format!("coverage/{}.qbf", replacement.sha256());
        let original_asset = format!("native/{}.qbf", native.sha256());
        assets.insert(asset.clone(), replacement.data.clone());
        assets.insert(original_asset.clone(), native.data);
        let mut entry = Map::new();
        entry.insert("romfs_path".into(), json!(native_path));
        entry.insert("coverage_file".into(), json!(asset));
        entry.insert("native_file".into(), json!(original_asset));
        entry.extend(contract);
        fonts.push(Value::Object(entry));
    }
    if fonts.is_empty() {
        return Err(ForgeError::invalid("ROM contains no supported Latin QBF font"));
    }
    let manifest = json!({
        "format": "oot3d_qbf_coverage_v1",
        "runtime_enabled": true,
        "minimum_output_height": 480,
        "status": "native_atlas_coverage_ready",
        "fonts": fonts,
        "source_font_sha256": sha256_hex(&font_bytes),
        "attribution": ATTRIBUTION,
    });
    let mut archive = StoredZip::new();
    for (name, payload) in &assets {
        archive.add(name, payload);
    }
    archive.add("manifest.json", dumps(&manifest, Options::PRETTY).as_bytes());
    publish(output, &archive.finish())?;
    let Value::Object(manifest) = manifest else { unreachable!() };
    Ok(manifest)
}

/// Writes `<output>.partial` and renames it over `output`, removing the
/// partial file on failure, as the Python publisher does.
fn publish(output: &Path, bytes: &[u8]) -> Result<()> {
    if let Some(parent) = output.parent().filter(|parent| !parent.as_os_str().is_empty()) {
        std::fs::create_dir_all(parent)
            .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    }
    let mut name = output.file_name().unwrap_or_default().to_os_string();
    name.push(".partial");
    let temporary = output.with_file_name(name);
    let result = std::fs::write(&temporary, bytes)
        .and_then(|()| std::fs::rename(&temporary, output))
        .map_err(|error| ForgeError::io(format!("Cannot write {}", output.display()), error));
    if result.is_err() {
        let _ = std::fs::remove_file(&temporary);
    }
    result
}
