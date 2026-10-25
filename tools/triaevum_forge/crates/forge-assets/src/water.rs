// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `water_textures.py`: the local water texture catalog, from the user's
//! RomFS. Texture identities (the renderer's base-level content hash and
//! size) and roles are read from scene room ZSIs and scene/actor ZARs and
//! classified by texture name. Format (ASCII):
//!
//! ```text
//! triaevum-water-textures 1
//! <content hash of the base level, 16 hex> <width> <height> <role mask>
//! ```
//!
//! Roles: 1 surface, 2 river, 4 waterfall, 8 underwater.

use std::collections::{BTreeMap, HashSet};
use std::path::{Path, PathBuf};

use forge_base::{CancelToken, ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::fnv::content_hash;
use crate::pyvalue::recipe_romfs_source;
use crate::romfs::{image_files, sorted_entries};
use crate::{catalog_cache, Report};

pub const CATALOG_VERSION: i64 = 1;
pub const CATALOG_HEADER: &str = "triaevum-water-textures 1";
pub const ROLE_SURFACE: u8 = 1;
pub const ROLE_RIVER: u8 = 2;
pub const ROLE_WATERFALL: u8 = 4;
pub const ROLE_UNDERWATER: u8 = 8;

/// Bits per pixel by CMB (PICA format, data type).
pub(crate) fn bits_per_pixel(format: u16, kind: u16) -> Option<usize> {
    Some(match (format, kind) {
        (0x6752, 0x1401) => 32,
        (0x6754, 0x1401) => 24,
        (0x6752, 0x8034) | (0x6752, 0x8033) | (0x6754, 0x8363) | (0x6758, 0x1401) => 16,
        (0x6758, 0x6760) | (0x6757, 0x1401) | (0x6756, 0x1401) | (0x675B, 0x0000) => 8,
        (0x6757, 0x6761) | (0x6756, 0x6761) | (0x675A, 0x0000) => 4,
        _ => return None,
    })
}

/// The base level as stored (8x8 tiles), in bytes.
pub(crate) fn base_level_bytes(width: u16, height: u16, bits: usize) -> usize {
    let tiled = |extent: u16| (usize::from(extent.max(8)) + 7) & !7;
    tiled(width) * tiled(height) * bits / 8
}

/// A CMB texture name: up to the first NUL, each non-ASCII byte U+FFFD
/// (`decode("ascii", "replace")`).
pub(crate) fn texture_name(field: &[u8]) -> String {
    let end = field.iter().position(|byte| *byte == 0).unwrap_or(field.len());
    field[..end].iter().map(|&byte| if byte < 0x80 { char::from(byte) } else { '\u{FFFD}' }).collect()
}

const WATER_STEMS: &[&str] = &[
    "water",
    "riv",
    "kawa",
    "taki",
    "idomizu",
    "mizusoko",
    "bmizu",
    "hole_mizu",
    "niwa_mizu",
    "drag_mizu",
    "rm_mizu",
    "ydan_mizu",
    "wsea",
    "hwat",
    "sea0",
];
const EXCLUDED_STEMS: &[&str] = &[
    "kabe",
    "yuka",
    "hasira",
    "_ten",
    "iwa",
    "emblem",
    "futi",
    "hari",
    "toge",
    "tunagi",
    "_obj",
    "ranma",
    "_dg_",
    "_bs_",
    "kasumi",
    "nawa",
    "angel",
    "hikari",
    "entran",
    "mizukusa",
    "mizuzou",
    "sumi",
    "huchi",
    "fallmaster",
];

/// `WATER_NAME.search` (case-insensitive): a stem anywhere, or `wate` at the
/// end (`$` also matches before a final newline).
fn is_water_name(lower: &str) -> bool {
    WATER_STEMS.iter().any(|stem| lower.contains(stem)) || lower.ends_with("wate") || lower.ends_with("wate\n")
}

/// `EXCLUDED_NAME.search`: a stem anywhere, or `awa_` not after a letter.
fn is_excluded_name(lower: &str) -> bool {
    EXCLUDED_STEMS.iter().any(|stem| lower.contains(stem))
        || lower.match_indices("awa_").any(|(at, _)| !lower[..at].ends_with(|c: char| c.is_ascii_lowercase()))
}

pub fn role_of(name: &str) -> u8 {
    let name = name.to_lowercase();
    if name.contains("taki") {
        ROLE_WATERFALL
    } else if name.contains("uvwater") {
        ROLE_UNDERWATER
    } else if name.contains("riv") || name.contains("kawa") {
        ROLE_RIVER
    } else {
        ROLE_SURFACE
    }
}

pub fn water_role(name: &str) -> u8 {
    // Names are ASCII plus U+FFFD, so ASCII case folding is re.I here.
    let lower = name.to_ascii_lowercase();
    if !is_water_name(&lower) || is_excluded_name(&lower) {
        0
    } else {
        role_of(name)
    }
}

/// One texture's name, size and base-level bytes.
pub type ModelTexture<'a> = (String, u16, u16, &'a [u8]);

fn truncated() -> ForgeError {
    ForgeError::invalid("Model file is truncated")
}

fn u16_at(data: &[u8], offset: usize) -> Result<u16> {
    let end = offset.checked_add(2).ok_or_else(truncated)?;
    Ok(u16::from_le_bytes(data.get(offset..end).ok_or_else(truncated)?.try_into().expect("two bytes")))
}

fn u32_at(data: &[u8], offset: usize) -> Result<u32> {
    let end = offset.checked_add(4).ok_or_else(truncated)?;
    Ok(u32::from_le_bytes(data.get(offset..end).ok_or_else(truncated)?.try_into().expect("four bytes")))
}

fn magic_at(data: &[u8], offset: usize, magic: &[u8]) -> bool {
    offset.checked_add(magic.len()).and_then(|end| data.get(offset..end)) == Some(magic)
}

/// `cmb_textures`: (name, width, height, base level) of a version-6 CMB's
/// textures; anything else yields nothing.
pub fn cmb_textures<'a>(data: &'a [u8], offset: usize, out: &mut Vec<ModelTexture<'a>>) -> Result<()> {
    if !magic_at(data, offset, b"cmb ") || offset + 0x44 > data.len() {
        return Ok(());
    }
    let size = u32_at(data, offset + 4)? as usize;
    let version = u32_at(data, offset + 8)?;
    if version != 6 || size < 0x44 || offset + size > data.len() {
        return Ok(());
    }
    let texture_chunk = u32_at(data, offset + 0x2C)? as usize;
    let texture_data = u32_at(data, offset + 0x40)? as usize;
    let chunk = offset + texture_chunk;
    if !magic_at(data, chunk, b"tex ") {
        return Err(ForgeError::invalid("CMB texture chunk is missing"));
    }
    let count = u32_at(data, chunk + 8)? as usize;
    for index in 0..count {
        let entry = chunk + 0x0C + index * 0x24;
        if entry + 20 > data.len() {
            return Err(truncated());
        }
        let data_size = u32_at(data, entry)? as usize;
        let (width, height) = (u16_at(data, entry + 8)?, u16_at(data, entry + 10)?);
        let (format, kind) = (u16_at(data, entry + 12)?, u16_at(data, entry + 14)?);
        let relative = u32_at(data, entry + 16)? as usize;
        let name = texture_name(&data[(entry + 0x14).min(data.len())..(entry + 0x24).min(data.len())]);
        let Some(bits) = bits_per_pixel(format, kind) else { continue };
        let base = base_level_bytes(width, height, bits);
        let start = offset + texture_data + relative;
        if base > data_size || start + base > data.len() {
            continue;
        }
        out.push((name, width, height, &data[start..start + base]));
    }
    Ok(())
}

/// Offsets of every `cmb ` occurrence, overlapping matches included.
pub(crate) fn cmb_offsets(data: &[u8]) -> impl Iterator<Item = usize> + '_ {
    data.windows(4).enumerate().filter(|(_, window)| *window == b"cmb ").map(|(offset, _)| offset)
}

/// Textures of the CMB models embedded in a room's ZSI.
pub fn zsi_textures(data: &[u8]) -> Result<Vec<ModelTexture<'_>>> {
    let mut textures = Vec::new();
    for offset in cmb_offsets(data) {
        cmb_textures(data, offset, &mut textures)?;
    }
    Ok(textures)
}

/// Textures of the CMB models in a ZAR archive.
pub fn zar_textures(data: &[u8]) -> Result<Vec<ModelTexture<'_>>> {
    let mut textures = Vec::new();
    if data.get(..4) != Some(b"ZAR\x01") {
        return Ok(textures);
    }
    let type_count = usize::from(u16_at(data, 8)?);
    let file_count = usize::from(u16_at(data, 10)?);
    let type_section = u32_at(data, 12)? as usize;
    let meta_section = u32_at(data, 16)? as usize;
    let data_section = u32_at(data, 20)? as usize;
    let c_string = |offset: usize| -> Result<String> {
        let end = data
            .get(offset..)
            .and_then(|rest| rest.iter().position(|byte| *byte == 0))
            .ok_or_else(|| ForgeError::invalid("ZAR name is not terminated"))?;
        Ok(texture_name(&data[offset..offset + end]))
    };
    let mut cmb_files = HashSet::new();
    for type_index in 0..type_count {
        let record = type_section + type_index * 0x10;
        let typed_count = u32_at(data, record)? as usize;
        let list_offset = u32_at(data, record + 4)? as usize;
        let name_offset = u32_at(data, record + 8)? as usize;
        if c_string(name_offset)? == "cmb" {
            if typed_count
                .checked_mul(4)
                .and_then(|bytes| list_offset.checked_add(bytes))
                .is_none_or(|end| end > data.len())
            {
                return Err(truncated());
            }
            for item in 0..typed_count {
                cmb_files.insert(u32_at(data, list_offset + item * 4)? as usize);
            }
        }
    }
    for index in 0..file_count {
        let size = u32_at(data, meta_section + index * 8)? as usize;
        let name_offset = u32_at(data, meta_section + index * 8 + 4)? as usize;
        if !cmb_files.contains(&index) && !c_string(name_offset)?.ends_with(".cmb") {
            continue;
        }
        let start = (u32_at(data, data_section + index * 4)? as usize).min(data.len());
        let end = start.saturating_add(size).min(data.len());
        cmb_textures(&data[start..end], 0, &mut textures)?;
    }
    Ok(textures)
}

fn wanted_model(path: &str) -> bool {
    path.ends_with(".zar") || (path.starts_with("scene/") && path.ends_with("_info.zsi"))
}

/// `romfs_model_files`: scene room ZSIs and scene/actor ZARs, from a RomFS
/// image or an extracted RomFS directory.
pub fn romfs_model_files(
    romfs: &Path,
    cancel: &CancelToken,
    visit: &mut dyn FnMut(&str, &[u8]) -> Result<()>,
) -> Result<()> {
    if romfs.is_dir() {
        for directory in ["scene", "actor"] {
            for (name, path) in sorted_entries(&romfs.join(directory)) {
                cancel.check()?;
                let relative = format!("{directory}/{name}");
                if path.is_file() && wanted_model(&relative) {
                    let data = std::fs::read(&path)
                        .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
                    visit(&relative, &data)?;
                }
            }
        }
        return Ok(());
    }
    image_files(romfs, &["scene", "actor"], cancel, &mut |relative, data| {
        if wanted_model(&relative) {
            visit(&relative, &data)?;
        }
        Ok(())
    })
}

/// `build_catalog`: the catalog bytes and the receipt summary.
pub fn build_catalog(romfs: &Path, cancel: &CancelToken) -> Result<(Vec<u8>, Value)> {
    let mut roles: BTreeMap<(u64, u16, u16), u8> = BTreeMap::new();
    let (mut textures, mut models) = (0u64, 0u64);
    romfs_model_files(romfs, cancel, &mut |path, data| {
        models += 1;
        let found = if path.ends_with(".zsi") { zsi_textures(data)? } else { zar_textures(data)? };
        for (name, width, height, base) in found {
            textures += 1;
            let role = water_role(&name);
            if role != 0 {
                *roles.entry((content_hash(base), width, height)).or_insert(0) |= role;
            }
        }
        Ok(())
    })?;
    let mut text = String::from(CATALOG_HEADER);
    text.push('\n');
    for ((hash, width, height), role) in &roles {
        text.push_str(&format!("{hash:016x} {width} {height} {role}\n"));
    }
    let summary = json!({"model_files": models, "textures": textures, "water_textures": roles.len()});
    Ok((text.into_bytes(), summary))
}

/// `prepare_water_textures`: the catalog for this RomFS, generated once and
/// reused while its receipt matches; `None` when the recipe names no RomFS.
pub fn prepare_water_textures(
    data_root: &Path,
    recipe: &Value,
    romfs: &Path,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Option<PathBuf>> {
    let Some(source) = recipe_romfs_source(recipe) else { return Ok(None) };
    let mut identity = Map::new();
    identity.insert("catalog_version".into(), json!(CATALOG_VERSION));
    identity.insert("romfs_sha256".into(), source.clone());
    catalog_cache::prepare(data_root, "water", "water_textures.txt", identity, || {
        report("water", "Cataloguing water textures...");
        build_catalog(romfs, cancel)
    })
    .map(Some)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_classify_water_and_its_roles() {
        assert_eq!(water_role("hylia_water_01"), ROLE_SURFACE);
        assert_eq!(water_role("spot_taki"), ROLE_WATERFALL);
        assert_eq!(water_role("kawa_02"), ROLE_RIVER);
        assert_eq!(water_role("uvwater_01"), ROLE_UNDERWATER);
        assert_eq!(water_role("s04_kawa_01"), ROLE_RIVER);
        assert_eq!(water_role("WATE"), ROLE_SURFACE);
        assert_eq!(water_role("wate\n"), ROLE_SURFACE);
        assert_eq!(water_role("wate_x"), 0);
        assert_eq!(water_role("awa_water"), 0);
        assert_eq!(water_role("_awa_water"), 0);
        assert_eq!(water_role("kawa_water"), ROLE_RIVER);
        for name in ["wtr_kabe_01", "izumi_kabe", "kasumi_water", "mizukusa_01", "labo_awa_01", "grass_01"] {
            assert_eq!(water_role(name), 0, "{name}");
        }
    }

    #[test]
    fn base_level_is_tiled() {
        assert_eq!(base_level_bytes(4, 4, 4), 32);
        assert_eq!(base_level_bytes(32, 32, 8), 1024);
        assert_eq!(base_level_bytes(9, 8, 16), 256);
        assert_eq!(texture_name(b"ab\xffc\0rest"), "ab\u{FFFD}c");
    }
}
