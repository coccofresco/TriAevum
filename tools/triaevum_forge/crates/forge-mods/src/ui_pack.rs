// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The optional HD UI texture pack ("OoT3D Vanilla UI/HUD" 3.2 by chubbycubby),
//! prepared on the user's machine only (`ui_texture_pack.py`).
//!
//! The prepared pack sorts the catalogued textures for the runtime's
//! `--ui-texture-pack`: the pack's language folders in `languages/<folder>/`
//! and every other texture in `common/`, with a `source.json` receipt. The
//! archive is read as a ZIP (stored/deflate only); a folder the user extracted
//! with any tool is verified the same way. Nothing is converted or recompressed.

use std::fs::{self, File};
use std::io::BufReader;
use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::hashing::copy_hashing;
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use serde_json::{json, Value};

use crate::archive::{archive_contract, archive_verified, stored_archive, ArchiveContract};
use crate::catalog::{UiTexturePack, LANGUAGE_FOLDERS, TEXTURE_FOLDER, UI_ATLAS_HD_TEXTURE};
use crate::util::{io_error, join_slash, member_matches, publish_directory, staging_dir};

pub const SOURCE_RECEIPT: &str = "source.json";
pub const COMMON_DIRECTORY: &str = "common";
pub const LANGUAGES_DIRECTORY: &str = "languages";

/// Where a member goes in the prepared pack (a `/`-separated relative path): a
/// language folder's texture in `languages/<folder>/`, any other in `common/`
/// keeping its path.
pub fn prepared_path(member: &str) -> String {
    let parts: Vec<&str> = member.split('/').filter(|part| !part.is_empty()).collect();
    if parts.len() == 3 && parts[0] == "UI" && LANGUAGE_FOLDERS.contains(&parts[1]) {
        return format!("{LANGUAGES_DIRECTORY}/{}/{}", parts[1], parts[2]);
    }
    format!("{COMMON_DIRECTORY}/{}", parts.join("/"))
}

/// The archive the recipes (a recipes document, or one recipe) accept, or `None`.
pub fn pack_contract(recipes: &Value, pack: &UiTexturePack) -> Result<Option<ArchiveContract>> {
    archive_contract(recipes, pack)
}

pub fn pack_directory(data_root: &Path, pack: &UiTexturePack) -> PathBuf {
    data_root.join("mods").join("ui-textures").join(format!("{}-{}", pack.mod_id, pack.version))
}

/// The verified prepared pack in the private data folder, or `None`.
pub fn prepared_pack(data_root: &Path, pack: &UiTexturePack, cancel: &CancelToken) -> Result<Option<PathBuf>> {
    let directory = pack_directory(data_root, pack);
    let receipt = directory.join(SOURCE_RECEIPT);
    if !receipt.is_file() {
        return Ok(None);
    }
    let Ok(payload) = load_json_object(&receipt) else {
        return Ok(None);
    };
    if payload.get("mod").and_then(Value::as_str) != Some(&pack.mod_id)
        || payload.get("version").and_then(Value::as_str) != Some(&pack.version)
    {
        return Ok(None);
    }
    for member in &pack.members {
        if !member_matches(&join_slash(&directory, &prepared_path(&member.path)), member, cancel)? {
            return Ok(None);
        }
    }
    Ok(Some(directory))
}

/// The pack's HD SingleScreen UI atlas in a prepared pack.
pub fn ui_atlas_hd(directory: Option<&Path>) -> Option<PathBuf> {
    let candidate = join_slash(directory?, &prepared_path(UI_ATLAS_HD_TEXTURE));
    candidate.is_file().then_some(candidate)
}

fn publish(
    staging: tempfile::TempDir,
    data_root: &Path,
    pack: &UiTexturePack,
    origin: &str,
    cancel: &CancelToken,
) -> Result<PathBuf> {
    for member in &pack.members {
        if !member_matches(&join_slash(staging.path(), &prepared_path(&member.path)), member, cancel)? {
            return Err(ForgeError::integrity(format!(
                "{}: {} is missing or differs from version {}",
                pack.title, member.path, pack.version
            )));
        }
    }
    atomic_write_json(
        &staging.path().join(SOURCE_RECEIPT),
        &json!({
            "mod": pack.mod_id, "version": pack.version, "origin": origin,
            "attribution": pack.attribution(), "license": pack.license_name,
            "license_url": pack.license_url, "source_url": pack.page,
            "private_local_artifact": true, "redistributable": false}),
    )?;
    publish_directory(staging, &pack_directory(data_root, pack))?;
    prepared_pack(data_root, pack, cancel)?.ok_or_else(|| {
        ForgeError::integrity(format!("{}: the extracted files changed while they were stored", pack.title))
    })
}

fn staging(data_root: &Path, pack: &UiTexturePack, prefix: &str) -> Result<tempfile::TempDir> {
    let directory = pack_directory(data_root, pack);
    staging_dir(directory.parent().unwrap_or(Path::new(".")), prefix)
}

fn zip_error(pack: &UiTexturePack, error: zip::result::ZipError) -> ForgeError {
    match error {
        zip::result::ZipError::Io(error) => io_error(format!("Cannot read the {} archive", pack.title), error),
        unsupported @ (zip::result::ZipError::UnsupportedArchive(_)
        | zip::result::ZipError::CompressionMethodNotSupported(_)
        | zip::result::ZipError::InvalidPassword) => ForgeError::new(
            ErrorKind::UnsupportedFormat,
            format!(
                "The {} archive uses a ZIP feature this Forge cannot read ({unsupported}). Extract it with any tool and choose \"Select extracted folder...\".",
                pack.title
            ),
        ),
        other => ForgeError::invalid(format!("The {} archive is not a valid ZIP file: {other}", pack.title)),
    }
}

/// Extracts exactly the catalogued textures of the verified original archive.
pub fn extract_archive(
    archive: &Path,
    data_root: &Path,
    contract: &ArchiveContract,
    pack: &UiTexturePack,
    cancel: &CancelToken,
) -> Result<PathBuf> {
    if !archive_verified(archive, contract, cancel)? {
        return Err(ForgeError::integrity(format!(
            "{} archive failed verification: {}",
            pack.title,
            archive.display()
        )));
    }
    let staging = staging(data_root, pack, ".extract-")?;
    let file = File::open(archive).map_err(|error| io_error(format!("Cannot read {}", archive.display()), error))?;
    let mut source = zip::ZipArchive::new(BufReader::new(file)).map_err(|error| zip_error(pack, error))?;
    for member in &pack.members {
        cancel.check()?;
        let entry = format!("{}/{TEXTURE_FOLDER}/{}", pack.archive_root, member.path);
        let mut info = match source.by_name(&entry) {
            Ok(info) => info,
            Err(zip::result::ZipError::FileNotFound) => {
                return Err(ForgeError::invalid(format!("The {} archive has no {}", pack.title, member.path)));
            }
            Err(error) => return Err(zip_error(pack, error)),
        };
        if info.size() != member.bytes {
            return Err(ForgeError::integrity(format!("{} differs from {} {}", member.path, pack.title, pack.version)));
        }
        let target = join_slash(staging.path(), &prepared_path(&member.path));
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent)
                .map_err(|error| io_error(format!("Cannot create {}", parent.display()), error))?;
        }
        let mut output =
            File::create(&target).map_err(|error| io_error(format!("Cannot write {}", target.display()), error))?;
        copy_hashing(&mut info, &mut output, u64::MAX, cancel)?;
    }
    publish(staging, data_root, pack, &format!("archive:{}", contract.sha256), cancel)
}

/// Candidate roots of a folder the user extracted: the pack's folder, its
/// `load/` folder, the title texture folder, or a folder containing the pack.
fn import_candidates(folder: &Path) -> Vec<PathBuf> {
    let title_name = TEXTURE_FOLDER.rsplit('/').next().unwrap_or_default();
    let below_load = TEXTURE_FOLDER.strip_prefix("load/").unwrap_or(TEXTURE_FOLDER);
    let mut candidates = vec![
        folder.to_path_buf(),
        join_slash(folder, below_load),
        folder.join(title_name),
        join_slash(folder, TEXTURE_FOLDER),
    ];
    if let Ok(entries) = fs::read_dir(folder) {
        let mut children: Vec<PathBuf> =
            entries.filter_map(|entry| entry.ok()).map(|entry| entry.path()).filter(|path| path.is_dir()).collect();
        // Python sorts Paths: case-insensitively on Windows.
        children.sort_by_key(|path| {
            let name = path.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
            if cfg!(windows) {
                name.to_lowercase()
            } else {
                name
            }
        });
        candidates.extend(children.iter().map(|child| join_slash(child, TEXTURE_FOLDER)));
    }
    candidates
}

/// Verifies and keeps the textures of a folder the user extracted with any
/// tool: the pack's folder, its `load/` folder, or the title texture folder.
pub fn import_extracted(
    folder: &Path,
    data_root: &Path,
    pack: &UiTexturePack,
    cancel: &CancelToken,
) -> Result<PathBuf> {
    let folder = fs::canonicalize(folder).unwrap_or_else(|_| folder.to_path_buf());
    let root = import_candidates(&folder)
        .into_iter()
        .find(|item| pack.members.iter().all(|member| join_slash(item, &member.path).is_file()));
    let Some(root) = root else {
        return Err(ForgeError::invalid(format!(
            "This folder does not contain the extracted {} {} textures (expected {TEXTURE_FOLDER}/{UI_ATLAS_HD_TEXTURE} and the others)",
            pack.title, pack.version
        )));
    };
    let staging = staging(data_root, pack, ".import-")?;
    for member in &pack.members {
        cancel.check()?;
        let source = join_slash(&root, &member.path);
        let is_link = fs::symlink_metadata(&source).is_ok_and(|metadata| metadata.file_type().is_symlink());
        if is_link || !member_matches(&source, member, cancel)? {
            return Err(ForgeError::integrity(format!("{} differs from {} {}", member.path, pack.title, pack.version)));
        }
        let target = join_slash(staging.path(), &prepared_path(&member.path));
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent)
                .map_err(|error| io_error(format!("Cannot create {}", parent.display()), error))?;
        }
        fs::copy(&source, &target).map_err(|error| io_error(format!("Cannot copy {}", source.display()), error))?;
    }
    publish(staging, data_root, pack, "extracted_folder", cancel)
}

/// The pack's state: verified textures prepared, archive stored but not yet
/// prepared, or missing.
pub use crate::romfs_mods::Status;

pub fn pack_status(
    root: &Path,
    data_root: &Path,
    contract: Option<&ArchiveContract>,
    pack: &UiTexturePack,
    cancel: &CancelToken,
) -> Result<Status> {
    if prepared_pack(data_root, pack, cancel)?.is_some() {
        return Ok(Status::Ready);
    }
    if contract.is_some_and(|contract| stored_archive(root, data_root, contract).is_some()) {
        return Ok(Status::Archive);
    }
    Ok(Status::Missing)
}

/// The prepared pack, or `None` when the revision does not accept it or the
/// user has not obtained it. A stored archive that fails is reported through
/// `report` (stage, message) and left out: the title is still prepared, with
/// the native textures.
pub fn prepare_ui_texture_pack(
    root: &Path,
    data_root: &Path,
    recipe: &Value,
    pack: &UiTexturePack,
    report: &mut dyn FnMut(&str, &str),
    cancel: &CancelToken,
) -> Result<Option<PathBuf>> {
    let Some(contract) = pack_contract(recipe, pack)? else {
        return Ok(None);
    };
    if let Some(prepared) = prepared_pack(data_root, pack, cancel)? {
        return Ok(Some(prepared));
    }
    let Some(archive) = stored_archive(root, data_root, &contract) else {
        return Ok(None);
    };
    report("ui_textures", &format!("Preparing {} {} from the original archive...", pack.title, pack.version));
    match extract_archive(&archive, data_root, &contract, pack, cancel) {
        Ok(prepared) => Ok(Some(prepared)),
        Err(error) if error.kind == ErrorKind::Cancelled => Err(error),
        Err(error) => {
            report("ui_textures", &format!("{} The game uses the native UI textures.", error.message));
            Ok(None)
        }
    }
}
