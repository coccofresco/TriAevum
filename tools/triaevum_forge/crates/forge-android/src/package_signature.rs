// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Publisher signature of the Android Forge package (APK assets).
//!
//! `tools/triaevum_release/android_forge_package.py` writes the package
//! inventory `forge-package.json` inside the package folder (`forge-package/`
//! in the APK assets, copied to app storage by `ForgePackage.prepare`); the
//! publisher signs its exact bytes with the release key (`release_signature.py`,
//! the desktop scheme and domain) and ships `release-signature.json` beside it.
//! Inventory paths are relative to that folder. Before the installer reads
//! any package file, it verifies the signature under the keys built into
//! this Forge and then the size and SHA-256 of every file the inventory
//! names. An unsigned package, an unknown key, a signature over other bytes
//! or a changed file is refused. There is no developer override on Android:
//! a release APK has no environment the user controls.

use std::path::{Component, Path};

use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ForgeError, Result};
use forge_core::release_signature::{verify, Authenticity, Trust, MAX_SIGNATURE_BYTES};
use serde_json::{json, Value};

pub const INVENTORY: &str = "forge-package.json";
pub const SIGNATURE: &str = "release-signature.json";
pub const INVENTORY_FORMAT: &str = "triaevum_android_forge_package_v1";
const MAX_INVENTORY_BYTES: u64 = 16 * 1024 * 1024;

fn read_bounded(path: &Path, limit: u64) -> Result<Option<Vec<u8>>> {
    match std::fs::metadata(path) {
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(ForgeError::io(format!("Cannot read {}", path.display()), error)),
        Ok(meta) if meta.len() > limit => {
            return Err(ForgeError::invalid(format!("{} is larger than {limit} bytes", path.display())))
        }
        Ok(_) => {}
    }
    std::fs::read(path).map(Some).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))
}

fn safe_relative(path: &str) -> bool {
    !path.is_empty()
        && !path.contains('\\')
        && Path::new(path).components().all(|component| matches!(component, Component::Normal(_)))
}

/// Verifies the package folder `package_root` (it holds `forge-package.json`,
/// `release-signature.json` and the files the inventory names).
pub fn verify_package(package_root: &Path, trust: &Trust, cancel: &CancelToken) -> Result<Value> {
    let inventory = read_bounded(&package_root.join(INVENTORY), MAX_INVENTORY_BYTES)?
        .ok_or_else(|| ForgeError::invalid("The Forge package has no inventory"))?;
    let signature = read_bounded(&package_root.join(SIGNATURE), MAX_SIGNATURE_BYTES)?;
    let authenticity = verify(&inventory, signature.as_deref(), trust)?;
    let document: Value = serde_json::from_slice(&inventory)
        .map_err(|error| ForgeError::invalid(format!("The Forge package inventory is not JSON: {error}")))?;
    if document.get("format").and_then(Value::as_str) != Some(INVENTORY_FORMAT) {
        return Err(ForgeError::invalid("The Forge package inventory has an unsupported format"));
    }
    let files = document
        .get("files")
        .and_then(Value::as_array)
        .ok_or_else(|| ForgeError::invalid("The Forge package inventory has no file list"))?;
    for record in files {
        let path = record.get("path").and_then(Value::as_str).unwrap_or_default();
        let digest = record.get("sha256").and_then(Value::as_str).unwrap_or_default();
        let bytes = record.get("bytes").and_then(Value::as_u64);
        if !safe_relative(path) {
            return Err(ForgeError::invalid(format!("The Forge package names an unsafe path: {path:?}")));
        }
        let (size, actual) = sha256_file(&package_root.join(path), cancel)?;
        if Some(size) != bytes || hex(&actual) != digest {
            return Err(ForgeError::invalid(format!("Forge package file {path} does not match its signed inventory")));
        }
    }
    let key = match &authenticity {
        Authenticity::Signed { key_id } => key_id.clone(),
        Authenticity::DeveloperOverride { .. } => unreachable!("Android never enables the override"),
    };
    Ok(json!({"status": "signed", "key_id": key, "files": files.len(), "title": document.get("title")}))
}

/// The Android trust: the embedded publisher keys, never the override.
pub fn android_trust() -> Trust {
    Trust::publisher(false)
}

/// The JSON bytes `ForgeNative.verifyPackage` returns.
pub fn verify_package_json(package_root: &Path) -> Vec<u8> {
    let answer = match verify_package(package_root, &android_trust(), &CancelToken::new()) {
        Ok(value) => value,
        Err(error) => json!({"status": "refused", "message": error.to_string()}),
    };
    serde_json::to_vec(&answer).unwrap_or_default()
}
