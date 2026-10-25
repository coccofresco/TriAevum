// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The original archive of a catalogued mod or texture pack: the contract a
//! revision recipe pins, discovery of a stored copy, and verified import of a
//! user-selected file (`romfs_mods.archive_contract`, `stored_archive`,
//! `import_archive`).

use std::fs::{self, File};
use std::path::{Path, PathBuf};

use forge_base::hashing::{copy_hashing, hex};
use forge_base::{CancelToken, ForgeError, Result};
use serde_json::Value;

use crate::catalog::CataloguedArchive;
use crate::util::{file_matches, io_error, object_field};

/// What a recipe pins about an archive: file name, size and SHA-256.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ArchiveContract {
    pub kind: String,
    pub name: String,
    pub bytes: u64,
    pub sha256: String,
}

fn contract_bytes(value: &Value) -> Option<u64> {
    match value {
        Value::Number(number) => number.as_u64().or_else(|| {
            let float = number.as_f64()?;
            (float >= 0.0 && float.fract() == 0.0 && float < 1.8e19).then_some(float as u64)
        }),
        Value::String(text) => text.trim().parse().ok(),
        _ => None,
    }
}

/// The archive the recipes (a recipes document, or one recipe) accept for
/// `archive`, or `None`. Recipes that disagree about it are an error.
pub fn archive_contract(recipes: &Value, archive: &dyn CataloguedArchive) -> Result<Option<ArchiveContract>> {
    let single = std::slice::from_ref(recipes);
    let items: &[Value] = match object_field(recipes, "recipes").and_then(Value::as_array) {
        Some(list) => list,
        None => single,
    };
    let mut found: Option<ArchiveContract> = None;
    for recipe in items {
        let Some(item) = object_field(recipe, "optional_inputs")
            .and_then(|optional| object_field(optional, archive.archive_kind()))
            .filter(|item| item.is_object())
        else {
            continue;
        };
        let field = |key: &str| {
            object_field(item, key)
                .ok_or_else(|| ForgeError::invalid(format!("The {} archive contract has no {key}", archive.title())))
        };
        let name = match field("name")? {
            Value::String(text) => text.clone(),
            other => other.to_string(),
        };
        let bytes = contract_bytes(field("bytes")?)
            .ok_or_else(|| ForgeError::invalid(format!("The {} archive size is not an integer", archive.title())))?;
        let sha256 = match field("sha256")? {
            Value::String(text) => text.to_lowercase(),
            other => other.to_string().to_lowercase(),
        };
        let contract = ArchiveContract { kind: archive.archive_kind().to_string(), name, bytes, sha256 };
        if found.as_ref().is_some_and(|previous| *previous != contract) {
            return Err(ForgeError::invalid(format!("Recipes disagree about the {} archive", archive.title())));
        }
        found = Some(contract);
    }
    Ok(found)
}

/// [`archive_contract`] over a recipes file.
pub fn archive_contract_from_file(path: &Path, archive: &dyn CataloguedArchive) -> Result<Option<ArchiveContract>> {
    let object = forge_base::fsutil::load_json_object(path)?;
    archive_contract(&Value::Object(object), archive)
}

pub fn downloads_directory(data_root: &Path) -> PathBuf {
    data_root.join("downloads")
}

/// `_verified`: a regular file (not a symbolic link) of the pinned size and hash.
pub fn archive_verified(path: &Path, contract: &ArchiveContract, cancel: &CancelToken) -> Result<bool> {
    file_matches(path, contract.bytes, &contract.sha256, cancel)
}

/// The original archive beside Forge, under `mods/` beside it, or in
/// `data/downloads`, found by size only (cheap; extraction verifies the hash).
pub fn stored_archive(root: &Path, data_root: &Path, contract: &ArchiveContract) -> Option<PathBuf> {
    [root.to_path_buf(), root.join("mods"), downloads_directory(data_root)].into_iter().find_map(|directory| {
        let path = directory.join(&contract.name);
        let metadata = fs::metadata(&path).ok()?;
        (metadata.is_file() && metadata.len() == contract.bytes).then_some(path)
    })
}

/// Moves a verified temporary download into data/downloads under its pinned name.
pub(crate) fn keep_archive(
    temporary: tempfile::TempPath,
    data_root: &Path,
    contract: &ArchiveContract,
) -> Result<PathBuf> {
    let destination = downloads_directory(data_root).join(&contract.name);
    temporary
        .persist(&destination)
        .map_err(|error| io_error(format!("Cannot store {}", destination.display()), error.error))?;
    Ok(destination)
}

/// A new temporary `.part` file in data/downloads.
pub(crate) fn part_file(data_root: &Path) -> Result<tempfile::NamedTempFile> {
    let directory = downloads_directory(data_root);
    fs::create_dir_all(&directory)
        .map_err(|error| io_error(format!("Cannot create {}", directory.display()), error))?;
    tempfile::Builder::new()
        .prefix(".")
        .suffix(".part")
        .tempfile_in(&directory)
        .map_err(|error| io_error(format!("Cannot write in {}", directory.display()), error))
}

/// Copies a user-selected archive into data/downloads after verifying it.
pub fn import_archive(
    source: &Path,
    data_root: &Path,
    archive: &dyn CataloguedArchive,
    contract: &ArchiveContract,
    cancel: &CancelToken,
) -> Result<PathBuf> {
    let source = fs::canonicalize(source).unwrap_or_else(|_| source.to_path_buf());
    if !source.is_file() {
        return Err(ForgeError::invalid(format!("Not a file: {}", source.display())));
    }
    if !archive_verified(&source, contract, cancel)? {
        return Err(ForgeError::integrity(format!(
            "This is not the original {} {} archive ({}): {}",
            archive.title(),
            archive.version(),
            contract.name,
            source.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default()
        )));
    }
    let destination = downloads_directory(data_root).join(&contract.name);
    fs::create_dir_all(downloads_directory(data_root))
        .map_err(|error| io_error("Cannot create the downloads folder", error))?;
    if fs::canonicalize(&destination).is_ok_and(|resolved| resolved == source) {
        return Ok(destination);
    }
    let mut part = part_file(data_root)?;
    let mut input =
        File::open(&source).map_err(|error| io_error(format!("Cannot read {}", source.display()), error))?;
    let (_, digest) = copy_hashing(&mut input, part.as_file_mut(), u64::MAX, cancel)?;
    part.as_file().sync_all().map_err(|error| io_error("Cannot write the copied archive", error))?;
    let copied = part.into_temp_path();
    // The copy is verified through the digest of the bytes written, plus the
    // size on disk, so a source that changed mid-copy is caught.
    let size = fs::metadata(&copied).map_err(|error| io_error("Cannot stat the copied archive", error))?.len();
    if size != contract.bytes || hex(&digest) != contract.sha256 {
        return Err(ForgeError::integrity("The copied archive changed while it was being copied"));
    }
    keep_archive(copied, data_root, contract)
}

/// Test helper: writes a file, creating its folder.
#[cfg(test)]
pub(crate) fn write_file(path: &Path, bytes: &[u8]) {
    use std::io::Write;
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).unwrap();
    }
    File::create(path).unwrap().write_all(bytes).unwrap();
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::RomFsMod;
    use serde_json::json;

    fn contract_for(payload: &[u8], name: &str) -> ArchiveContract {
        ArchiveContract {
            kind: "vanilla_plus_link_1_9_archive".into(),
            name: name.into(),
            bytes: payload.len() as u64,
            sha256: forge_base::hashing::sha256_hex(payload),
        }
    }

    #[test]
    fn contract_from_recipes() {
        let archive = RomFsMod::vanilla_plus_link();
        let item = json!({"name": "a.rar", "bytes": 5, "sha256": "AB"});
        let recipe = json!({"optional_inputs": {"vanilla_plus_link_1_9_archive": item}});
        let expected = ArchiveContract {
            kind: "vanilla_plus_link_1_9_archive".into(),
            name: "a.rar".into(),
            bytes: 5,
            sha256: "ab".into(),
        };
        assert_eq!(archive_contract(&recipe, &archive).unwrap(), Some(expected.clone()));
        let document = json!({"recipes": [recipe.clone(), {"optional_inputs": {}}, recipe.clone()]});
        assert_eq!(archive_contract(&document, &archive).unwrap(), Some(expected));
        assert_eq!(archive_contract(&json!({}), &archive).unwrap(), None);
        let other = json!({"optional_inputs": {"vanilla_plus_link_1_9_archive": {"name": "b.rar", "bytes": 5, "sha256": "ab"}}});
        let error = archive_contract(&json!({"recipes": [recipe, other]}), &archive).unwrap_err();
        assert!(error.message.contains("Recipes disagree about the"));
    }

    #[test]
    fn selected_archive_must_be_the_original() {
        let archive = RomFsMod::vanilla_plus_link();
        let directory = tempfile::tempdir().unwrap();
        let data = directory.path().join("data");
        let payload = b"original archive bytes";
        let contract = contract_for(payload, "fixture.rar");
        let picked = directory.path().join("picked.rar");
        write_file(&picked, b"original archive byteX");
        let cancel = CancelToken::new();
        let error = import_archive(&picked, &data, &archive, &contract, &cancel).unwrap_err();
        assert!(error.message.contains("not the original"));
        write_file(&picked, payload);
        let stored = import_archive(&picked, &data, &archive, &contract, &cancel).unwrap();
        assert_eq!(fs::read(&stored).unwrap(), payload);
        assert_eq!(fs::read_dir(data.join("downloads")).unwrap().count(), 1);
        // Importing the stored copy again is a no-op.
        assert_eq!(import_archive(&stored, &data, &archive, &contract, &cancel).unwrap(), stored);
        assert_eq!(fs::read_dir(data.join("downloads")).unwrap().count(), 1);
    }

    #[test]
    fn discovery_order_is_package_mods_then_downloads() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path().join("package");
        let data = directory.path().join("data");
        let contract = contract_for(b"abc", "x.rar");
        assert_eq!(stored_archive(&root, &data, &contract), None);
        write_file(&data.join("downloads/x.rar"), b"abc");
        assert_eq!(stored_archive(&root, &data, &contract), Some(data.join("downloads/x.rar")));
        write_file(&root.join("mods/x.rar"), b"abc");
        assert_eq!(stored_archive(&root, &data, &contract), Some(root.join("mods/x.rar")));
        write_file(&root.join("x.rar"), b"abc");
        assert_eq!(stored_archive(&root, &data, &contract), Some(root.join("x.rar")));
        // Size-only: a wrong size is not found.
        write_file(&root.join("x.rar"), b"abcd");
        assert_eq!(stored_archive(&root, &data, &contract), Some(root.join("mods/x.rar")));
    }
}
