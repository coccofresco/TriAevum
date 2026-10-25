// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `topscreen_assets.py`: the SingleScreen assets TriAevum ships (the 3.0
//! low-resolution UI atlas and Link animations, the 3.1 HD Latin font), never
//! executable patches, and the HD UI atlas from the user's prepared HD UI
//! texture pack when there is one.

use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_bytes, atomic_write_json, load_json_object};
use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::font_pack::{prepare_font_pack, ATTRIBUTION as FONT_ATTRIBUTION};
use crate::pyvalue::{identity_key, py_eq};
use crate::texture_pack::build_texture_pack;
use crate::Report;

/// 6: the HD UI atlas comes from the HD UI texture pack.
pub const IMPORT_VERSION: i64 = 6;
/// 4: the HD font comes from the package.
pub const FONT_IMPORT_VERSION: i64 = 4;
pub const TITLE: &str = "The Legend of Zelda: Ocarina of Time 3D";
pub const UI_ATLAS: &str = "custom_ui01.ctxb";
pub const LINK_ANIMATIONS: &str = "zelda_link_ext.zar";
pub const HD_FONT: &str = "ltn16.qbf";
pub const UI_ATLAS_HD: &str = "ui_atlas_hd.png";
pub const MOD_PAGE: &str = "https://gamebanana.com/mods/695893";
/// Package directories of the shipped assets.
pub const BUNDLED_DIRECTORY: [&str; 3] = ["resources", "topscreen", "singlescreen-3.0"];
pub const BUNDLED_FONT_DIRECTORY: [&str; 3] = ["resources", "topscreen", "singlescreen-3.1"];
pub const UI_ATLAS_IDENTITY: (u64, &str) =
    (131_144, "20290e417c56973cd19daa1f61a6c3963f075d61776db3d996954ee3296d07d8");
pub const LINK_ANIMATIONS_IDENTITY: (u64, &str) =
    (186_404, "06d60e05c493e86677497eb935b49951d613249c43f896816ae898bb8b005902");
pub const HD_FONT_IDENTITY: (u64, &str) = (427_592, "8b83e081f721c6e1842b0a9cd90b71a3fb2849674d2b1040f3dc5c3221594169");

/// The package's verified SingleScreen assets.
#[derive(Clone, Debug)]
pub struct BundledAssets {
    pub ui_atlas: PathBuf,
    pub link_animations: PathBuf,
    pub hd_font: PathBuf,
}

fn sha256_hex_file(path: &Path, cancel: &CancelToken) -> Result<String> {
    Ok(hex(&sha256_file(path, cancel)?.1))
}

fn bundled(
    root: &Path,
    directory: [&str; 3],
    name: &str,
    expected: (u64, &str),
    cancel: &CancelToken,
) -> Result<PathBuf> {
    // `str(Path(...))`: the platform separator, as the Python message shows.
    let relative: PathBuf = directory.iter().copied().chain([name]).collect();
    let path = root.join(&relative);
    let valid = path.is_file()
        && std::fs::metadata(&path).is_ok_and(|metadata| metadata.len() == expected.0)
        && sha256_hex_file(&path, cancel)? == expected.1;
    if !valid {
        return Err(ForgeError::integrity(format!(
            "The TriAevum package is incomplete or modified: {}. Extract the release again.",
            relative.display()
        )));
    }
    Ok(path)
}

/// `bundled_assets`: the package's SingleScreen assets, verified.
pub fn bundled_assets(root: &Path, cancel: &CancelToken) -> Result<BundledAssets> {
    Ok(BundledAssets {
        ui_atlas: bundled(root, BUNDLED_DIRECTORY, UI_ATLAS, UI_ATLAS_IDENTITY, cancel)?,
        link_animations: bundled(root, BUNDLED_DIRECTORY, LINK_ANIMATIONS, LINK_ANIMATIONS_IDENTITY, cancel)?,
        hd_font: bundled(root, BUNDLED_FONT_DIRECTORY, HD_FONT, HD_FONT_IDENTITY, cancel)?,
    })
}

/// `checked_ui_atlas_hd`: a PNG with the native atlas's 2:1 aspect.
pub fn checked_ui_atlas_hd(path: &Path) -> Result<Vec<u8>> {
    let data = std::fs::read(path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    if data.get(..8) != Some(b"\x89PNG\r\n\x1a\n") || data.get(12..16) != Some(b"IHDR") {
        return Err(ForgeError::invalid(format!("The HD UI atlas is not a PNG: {}", path.display())));
    }
    let Some(size) = data.get(16..24) else {
        // Python's struct.error for a header cut inside IHDR.
        return Err(ForgeError::invalid(format!("The HD UI atlas is not a PNG: {}", path.display())));
    };
    let width = u32::from_be_bytes(size[..4].try_into().expect("four bytes"));
    let height = u32::from_be_bytes(size[4..].try_into().expect("four bytes"));
    if width < 512 || width % 512 != 0 || u64::from(width) != u64::from(height) * 2 {
        return Err(ForgeError::invalid(format!("The HD UI atlas has unexpected dimensions {width}x{height}")));
    }
    Ok(data)
}

/// The imported HD UI atlas beside a prepared texture pack.
pub fn topscreen_ui_atlas_hd(pack: Option<&Path>) -> Option<PathBuf> {
    beside(pack, UI_ATLAS_HD)
}

/// The installed `zelda_link_ext.zar` beside a prepared texture pack.
pub fn topscreen_link_animations(pack: Option<&Path>) -> Option<PathBuf> {
    beside(pack, LINK_ANIMATIONS)
}

fn beside(pack: Option<&Path>, name: &str) -> Option<PathBuf> {
    let candidate = pack?.parent()?.join(name);
    candidate.is_file().then_some(candidate)
}

fn recipe_romfs_sha256(recipe: &Value) -> Result<&Value> {
    recipe
        .get("inputs")
        .and_then(|inputs| inputs.get("romfs"))
        .and_then(|romfs| romfs.get("sha256"))
        .ok_or_else(|| ForgeError::invalid("The recipe has no RomFS identity"))
}

fn receipt_matches(path: &Path, identity: &Value, pack: &Path, cancel: &CancelToken) -> Result<bool> {
    let receipt = load_json_object(path)?;
    if !receipt.get("source").is_some_and(|source| py_eq(source, identity)) {
        return Ok(false);
    }
    let digest = sha256_hex_file(pack, cancel)?;
    Ok(receipt.get("pack_sha256").and_then(Value::as_str) == Some(digest.as_str()))
}

/// `prepare_topscreen_assets`: the TopScreen texture overrides with the Link
/// animations, the HD font coverage and, from the prepared HD UI texture
/// pack, the HD UI atlas beside them. Other titles get `None`.
///
/// `romfs` is the retained RomFS image (the font pack reads the native fonts
/// from it). The RomFS identity is the recipe's, as in Python.
pub fn prepare_topscreen_assets(
    root: &Path,
    data_root: &Path,
    recipe: &Value,
    romfs: &Path,
    ui_atlas_hd: Option<&Path>,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Option<PathBuf>> {
    if recipe.get("title").and_then(Value::as_str) != Some(TITLE) {
        return Ok(None);
    }
    let assets = bundled_assets(root, cancel)?;
    let hd_atlas_sha256 = ui_atlas_hd.map(|path| sha256_hex_file(path, cancel)).transpose()?;
    let romfs_sha256 = recipe_romfs_sha256(recipe)?.clone();
    let mut identity = Map::new();
    identity.insert("import_version".into(), json!(IMPORT_VERSION));
    identity.insert("ui_atlas_sha256".into(), json!(UI_ATLAS_IDENTITY.1));
    identity.insert("link_animations_sha256".into(), json!(LINK_ANIMATIONS_IDENTITY.1));
    identity.insert("ui_atlas_hd_sha256".into(), json!(hd_atlas_sha256));
    identity.insert("romfs_sha256".into(), romfs_sha256.clone());
    let directory = data_root.join("mods").join("topscreen").join(identity_key(&identity)?);
    let identity = Value::Object(identity);
    let pack = directory.join("atlas_overrides.o3tu");
    let animations = directory.join(LINK_ANIMATIONS);
    let atlas_hd = directory.join(UI_ATLAS_HD);
    let font_pack = directory.join("font_coverage.zip");
    let font_receipt_path = directory.join("fonts_import.json");
    let receipt_path = directory.join("import.json");

    let textures_valid = pack.is_file()
        && receipt_path.is_file()
        && receipt_matches(&receipt_path, &identity, &pack, cancel)?
        && animations.is_file()
        && sha256_hex_file(&animations, cancel)? == LINK_ANIMATIONS_IDENTITY.1
        && match &hd_atlas_sha256 {
            None => true,
            Some(expected) => atlas_hd.is_file() && sha256_hex_file(&atlas_hd, cancel)? == *expected,
        };
    let font_identity = json!({
        "font_import_version": FONT_IMPORT_VERSION,
        "font_sha256": HD_FONT_IDENTITY.1,
        "romfs_sha256": romfs_sha256,
    });
    let fonts_valid = font_pack.is_file()
        && font_receipt_path.is_file()
        && receipt_matches(&font_receipt_path, &font_identity, &font_pack, cancel)?;
    if textures_valid && fonts_valid {
        return Ok(Some(pack));
    }

    std::fs::create_dir_all(&directory)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", directory.display()), error))?;
    if !textures_valid {
        report("topscreen", "Installing the SingleScreen UI textures and Link animations...");
        let atlas = std::fs::read(&assets.ui_atlas)
            .map_err(|error| ForgeError::io(format!("Cannot read {}", assets.ui_atlas.display()), error))?;
        atomic_write_bytes(&pack, &build_texture_pack(&atlas)?)?;
        std::fs::copy(&assets.link_animations, &animations)
            .map_err(|error| ForgeError::io(format!("Cannot write {}", animations.display()), error))?;
        let mut receipt = Map::new();
        receipt.insert("source".into(), identity.clone());
        receipt.insert("pack_sha256".into(), json!(sha256_hex_file(&pack, cancel)?));
        receipt.insert("attribution".into(), json!("TopScreen / Single Screen Experience by M-1"));
        receipt.insert("source_url".into(), json!(MOD_PAGE));
        if let Some(source) = ui_atlas_hd {
            report("topscreen", "Installing the HD UI atlas from the HD UI texture pack...");
            atomic_write_bytes(&atlas_hd, &checked_ui_atlas_hd(source)?)?;
            receipt.insert("ui_atlas_hd_attribution".into(), json!("chubbycubby, OoT3D Vanilla UI/HUD"));
        } else if let Err(error) = std::fs::remove_file(&atlas_hd) {
            if error.kind() != std::io::ErrorKind::NotFound {
                return Err(ForgeError::io(format!("Cannot remove {}", atlas_hd.display()), error));
            }
        }
        atomic_write_json(&receipt_path, &Value::Object(receipt))?;
    }
    if !fonts_valid {
        report("topscreen", "Preparing the SingleScreen HD font...");
        let manifest = prepare_font_pack(&assets.hd_font, romfs, &font_pack)?;
        let font_count = manifest.get("fonts").and_then(Value::as_array).map_or(0, Vec::len);
        atomic_write_json(
            &font_receipt_path,
            &json!({
                "source": font_identity,
                "pack_sha256": sha256_hex_file(&font_pack, cancel)?,
                "runtime_enabled": true,
                "minimum_output_height": 480,
                "status": "native_atlas_coverage_ready",
                "font_count": font_count,
                "attribution": FONT_ATTRIBUTION,
                "source_url": MOD_PAGE,
            }),
        )?;
    }
    Ok(Some(pack))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn png_header(width: u32, height: u32) -> Vec<u8> {
        let mut data = b"\x89PNG\r\n\x1a\n".to_vec();
        data.extend(13u32.to_be_bytes());
        data.extend(b"IHDR");
        data.extend(width.to_be_bytes());
        data.extend(height.to_be_bytes());
        data.extend([8, 6, 0, 0, 0]);
        data.extend(b"fixture");
        data
    }

    #[test]
    fn hd_ui_atlas_must_be_a_two_to_one_png() {
        let directory = tempfile::tempdir().unwrap();
        for (name, atlas) in [
            ("square", png_header(2048, 2048)),
            ("odd", png_header(4000, 2000)),
            ("text", b"not a png at all, padded to the IHDR length".to_vec()),
        ] {
            let path = directory.path().join(format!("{name}.png"));
            std::fs::write(&path, atlas).unwrap();
            assert!(checked_ui_atlas_hd(&path).is_err(), "{name}");
        }
        let path = directory.path().join("native.png");
        std::fs::write(&path, png_header(512, 256)).unwrap();
        assert_eq!(checked_ui_atlas_hd(&path).unwrap(), png_header(512, 256));
    }

    #[test]
    fn other_title_does_not_install_oot3d_assets() {
        let directory = tempfile::tempdir().unwrap();
        let data = directory.path().join("data");
        let result = prepare_topscreen_assets(
            directory.path(),
            &data,
            &json!({"title": "Another title"}),
            &directory.path().join("other.bin"),
            None,
            &mut |_, _| {},
            &CancelToken::new(),
        )
        .unwrap();
        assert!(result.is_none());
        assert!(!data.exists());
    }

    #[test]
    fn missing_shipped_asset_is_a_package_error() {
        let directory = tempfile::tempdir().unwrap();
        let error = bundled_assets(directory.path(), &CancelToken::new()).unwrap_err();
        assert!(error.message.contains("package is incomplete or modified"));
        assert!(error.message.contains(UI_ATLAS));
    }
}
