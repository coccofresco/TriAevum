// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The RomFS mod settings and the verified member store: enabled mods
//! (`config/romfs_mods.json`), extraction of the catalogued members from the
//! original archive or an extracted folder, and their status
//! (`romfs_mods.enabled_mods` .. `mod_status`).

use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use serde_json::{json, Value};

use crate::archive::{archive_verified, stored_archive, ArchiveContract};
use crate::catalog::{Catalog, RomFsMod};
use crate::extract::ArchiveExtractor;
use crate::util::{io_error, join_slash, member_matches, publish_directory, staging_dir};

pub const SETTINGS_FORMAT: &str = "triaevum_romfs_mods_v1";
pub const SETTINGS_NAME: &str = "romfs_mods.json";
pub const SOURCE_RECEIPT: &str = "source.json";

/// Verified extracted members: RomFS path to the stored file.
pub type StoredMembers = BTreeMap<String, PathBuf>;

pub fn settings_path(data_root: &Path) -> PathBuf {
    data_root.join("config").join(SETTINGS_NAME)
}

fn invalid_settings(path: &Path) -> ForgeError {
    ForgeError::new(ErrorKind::InvalidSettings, format!("Invalid RomFS mod settings: {}", path.display()))
}

/// The enabled catalogued mods in catalogue order. A missing file is none; a
/// malformed file is an error, never a silent default; unknown ids are
/// ignored.
pub fn enabled_mods(data_root: &Path, catalog: &Catalog) -> Result<Vec<String>> {
    let path = settings_path(data_root);
    if !path.is_file() {
        return Ok(Vec::new());
    }
    let payload =
        load_json_object(&path).map_err(|error| ForgeError::new(ErrorKind::InvalidSettings, error.message))?;
    let Some(Value::Array(enabled)) = payload.get("enabled") else {
        return Err(invalid_settings(&path));
    };
    let ids: Option<Vec<&str>> = enabled.iter().map(Value::as_str).collect();
    let (Some(ids), Some(Value::String(format))) = (ids, payload.get("format")) else {
        return Err(invalid_settings(&path));
    };
    if format != SETTINGS_FORMAT {
        return Err(invalid_settings(&path));
    }
    Ok(catalog.mods.iter().filter(|item| ids.contains(&item.mod_id.as_str())).map(|item| item.mod_id.clone()).collect())
}

/// Enables or disables a catalogued mod and writes the catalogue-ordered list.
pub fn set_mod_enabled(data_root: &Path, catalog: &Catalog, mod_id: &str, enabled: bool) -> Result<Vec<String>> {
    if catalog.get(mod_id).is_none() {
        return Err(ForgeError::invalid(format!("Unknown RomFS mod: {mod_id}")));
    }
    let mut current = enabled_mods(data_root, catalog)?;
    current.retain(|item| item != mod_id);
    if enabled {
        current.push(mod_id.to_string());
    }
    let ordered: Vec<String> =
        catalog.mods.iter().filter(|item| current.contains(&item.mod_id)).map(|item| item.mod_id.clone()).collect();
    atomic_write_json(&settings_path(data_root), &json!({"format": SETTINGS_FORMAT, "enabled": ordered}))?;
    Ok(ordered)
}

pub fn source_directory(data_root: &Path, mod_: &RomFsMod) -> PathBuf {
    data_root.join("mods").join("romfs-sources").join(format!("{}-{}", mod_.mod_id, mod_.version))
}

/// The verified extracted members in the private data folder, or `None`.
pub fn stored_members(data_root: &Path, mod_: &RomFsMod, cancel: &CancelToken) -> Result<Option<StoredMembers>> {
    let directory = source_directory(data_root, mod_);
    let receipt = directory.join(SOURCE_RECEIPT);
    if !receipt.is_file() {
        return Ok(None);
    }
    let Ok(payload) = load_json_object(&receipt) else {
        return Ok(None);
    };
    if payload.get("mod").and_then(Value::as_str) != Some(&mod_.mod_id)
        || payload.get("version").and_then(Value::as_str) != Some(&mod_.version)
    {
        return Ok(None);
    }
    let mut members = StoredMembers::new();
    for member in &mod_.members {
        members.insert(member.path.clone(), join_slash(&directory, &member.path));
    }
    for member in &mod_.members {
        if !member_matches(&members[&member.path], member, cancel)? {
            return Ok(None);
        }
    }
    Ok(Some(members))
}

fn publish_members(
    staging: tempfile::TempDir,
    data_root: &Path,
    mod_: &RomFsMod,
    origin: &str,
    cancel: &CancelToken,
) -> Result<StoredMembers> {
    for member in &mod_.members {
        if !member_matches(&join_slash(staging.path(), &member.path), member, cancel)? {
            return Err(ForgeError::integrity(format!(
                "{}: {} is missing or differs from version {}",
                mod_.title, member.path, mod_.version
            )));
        }
    }
    atomic_write_json(
        &staging.path().join(SOURCE_RECEIPT),
        &json!({
            "mod": mod_.mod_id, "version": mod_.version, "origin": origin,
            "attribution": mod_.attribution(), "private_local_artifact": true}),
    )?;
    publish_directory(staging, &source_directory(data_root, mod_))?;
    stored_members(data_root, mod_, cancel)?.ok_or_else(|| {
        ForgeError::integrity(format!("{}: the extracted files changed while they were stored", mod_.title))
    })
}

/// Extracts exactly the catalogued members of the verified original archive.
pub fn extract_archive(
    archive: &Path,
    data_root: &Path,
    mod_: &RomFsMod,
    contract: &ArchiveContract,
    extractor: &dyn ArchiveExtractor,
    cancel: &CancelToken,
) -> Result<StoredMembers> {
    if !archive_verified(archive, contract, cancel)? {
        return Err(ForgeError::integrity(format!(
            "{} archive failed verification: {}",
            mod_.title,
            archive.display()
        )));
    }
    let parent = source_directory(data_root, mod_).parent().map(Path::to_path_buf).unwrap_or_default();
    let staging = staging_dir(&parent, ".extract-")?;
    let names: Vec<&str> = mod_.members.iter().map(|member| member.path.as_str()).collect();
    extractor.extract(archive, staging.path(), &names, &mod_.title, cancel)?;
    publish_members(staging, data_root, mod_, &format!("archive:{}", contract.sha256), cancel)
}

/// Verifies and keeps members from a folder the user extracted with any tool:
/// the archive root (actor/, misc/), or a folder whose romfs/ is that root.
pub fn import_extracted(
    folder: &Path,
    data_root: &Path,
    mod_: &RomFsMod,
    cancel: &CancelToken,
) -> Result<StoredMembers> {
    let folder = fs::canonicalize(folder).unwrap_or_else(|_| folder.to_path_buf());
    let root = [folder.clone(), folder.join("romfs")]
        .into_iter()
        .find(|item| mod_.members.iter().all(|member| join_slash(item, &member.path).is_file()));
    let Some(root) = root else {
        let first = mod_.members.first().map(|member| member.path.as_str()).unwrap_or_default();
        return Err(ForgeError::invalid(format!(
            "This folder does not contain the extracted {} files (expected {first} and the others)",
            mod_.title
        )));
    };
    let parent = source_directory(data_root, mod_).parent().map(Path::to_path_buf).unwrap_or_default();
    let staging = staging_dir(&parent, ".import-")?;
    for member in &mod_.members {
        cancel.check()?;
        let source = join_slash(&root, &member.path);
        let is_link = fs::symlink_metadata(&source).is_ok_and(|metadata| metadata.file_type().is_symlink());
        if is_link || !member_matches(&source, member, cancel)? {
            return Err(ForgeError::integrity(format!("{} differs from {} {}", member.path, mod_.title, mod_.version)));
        }
        let target = join_slash(staging.path(), &member.path);
        if let Some(directory) = target.parent() {
            fs::create_dir_all(directory)
                .map_err(|error| io_error(format!("Cannot create {}", directory.display()), error))?;
        }
        fs::copy(&source, &target).map_err(|error| io_error(format!("Cannot copy {}", source.display()), error))?;
    }
    publish_members(staging, data_root, mod_, "extracted_folder", cancel)
}

/// Where a mod stands: verified files stored, archive stored but not yet
/// extracted, or nothing.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Ready,
    Archive,
    Missing,
}

impl Status {
    /// The Python status string.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Ready => "ready",
            Self::Archive => "archive",
            Self::Missing => "missing",
        }
    }
}

pub fn mod_status(
    root: &Path,
    data_root: &Path,
    mod_: &RomFsMod,
    contract: Option<&ArchiveContract>,
    cancel: &CancelToken,
) -> Result<Status> {
    if stored_members(data_root, mod_, cancel)?.is_some() {
        return Ok(Status::Ready);
    }
    if contract.is_some_and(|contract| stored_archive(root, data_root, contract).is_some()) {
        return Ok(Status::Archive);
    }
    Ok(Status::Missing)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::Member;
    use forge_base::hashing::sha256_hex;

    fn small_mod(members: &[(&str, &[u8])]) -> RomFsMod {
        RomFsMod {
            mod_id: "fixture".into(),
            archive_kind: "fixture_archive".into(),
            version: "0.1".into(),
            downloads: vec!["https://example.invalid/primary".into(), "https://example.invalid/mirror".into()],
            members: members
                .iter()
                .map(|(name, data)| Member::new(*name, data.len() as u64, sha256_hex(data)))
                .collect(),
            ..RomFsMod::vanilla_plus_link()
        }
    }

    #[test]
    fn settings_default_off_round_trip_and_reject_malformed() {
        let directory = tempfile::tempdir().unwrap();
        let data = directory.path();
        let catalog = Catalog::builtin();
        assert!(enabled_mods(data, &catalog).unwrap().is_empty());
        assert_eq!(set_mod_enabled(data, &catalog, "vanilla_plus_link", true).unwrap(), ["vanilla_plus_link"]);
        assert_eq!(
            fs::read_to_string(settings_path(data)).unwrap(),
            "{\n  \"enabled\": [\n    \"vanilla_plus_link\"\n  ],\n  \"format\": \"triaevum_romfs_mods_v1\"\n}\n"
        );
        assert_eq!(enabled_mods(data, &catalog).unwrap(), ["vanilla_plus_link"]);
        assert!(set_mod_enabled(data, &catalog, "vanilla_plus_link", false).unwrap().is_empty());
        assert!(set_mod_enabled(data, &catalog, "unknown", true).is_err());
        fs::write(settings_path(data), r#"{"enabled": "yes"}"#).unwrap();
        let error = enabled_mods(data, &catalog).unwrap_err();
        assert!(error.message.starts_with("Invalid RomFS mod settings"));
        assert_eq!(error.kind, ErrorKind::InvalidSettings);
        // Unknown strings are filtered, order follows the catalogue.
        fs::write(settings_path(data), r#"{"format":"triaevum_romfs_mods_v1","enabled":["x","vanilla_plus_link"]}"#)
            .unwrap();
        assert_eq!(enabled_mods(data, &catalog).unwrap(), ["vanilla_plus_link"]);
        fs::write(settings_path(data), "{").unwrap();
        assert_eq!(enabled_mods(data, &catalog).unwrap_err().kind, ErrorKind::InvalidSettings);
    }

    #[test]
    fn extracted_folder_is_verified_member_by_member() {
        let directory = tempfile::tempdir().unwrap();
        let data = directory.path().join("data");
        let members: [(&str, &[u8]); 2] =
            [("actor/a.zar", b"new A payload, longer than before"), ("misc/menu.zar", b"m")];
        let mod_ = small_mod(&members);
        let folder = directory.path().join("extracted/romfs");
        for (name, bytes) in members {
            let path = join_slash(&folder, name);
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::write(path, bytes).unwrap();
        }
        let cancel = CancelToken::new();
        fs::write(folder.join("misc/menu.zar"), b"x").unwrap();
        let error = import_extracted(folder.parent().unwrap(), &data, &mod_, &cancel).unwrap_err();
        assert!(error.message.contains("differs"));
        assert!(stored_members(&data, &mod_, &cancel).unwrap().is_none());
        fs::write(folder.join("misc/menu.zar"), b"m").unwrap();
        let stored = import_extracted(folder.parent().unwrap(), &data, &mod_, &cancel).unwrap();
        for (name, bytes) in members {
            assert_eq!(fs::read(&stored[name]).unwrap(), bytes);
        }
        let receipt = fs::read_to_string(source_directory(&data, &mod_).join(SOURCE_RECEIPT)).unwrap();
        assert!(receipt.contains("\"origin\": \"extracted_folder\""));
        fs::write(&stored["actor/a.zar"], b"tampered").unwrap();
        assert!(stored_members(&data, &mod_, &cancel).unwrap().is_none());
        let error = import_extracted(directory.path(), &data, &mod_, &cancel).unwrap_err();
        assert!(error.message.contains("does not contain the extracted"));
        assert_eq!(mod_status(directory.path(), &data, &mod_, None, &cancel).unwrap(), Status::Missing);
    }
}
