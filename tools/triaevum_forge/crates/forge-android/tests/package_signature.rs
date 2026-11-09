// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Signature and hash verification of the Android Forge package, host-side.

use std::path::Path;

use forge_base::CancelToken;
use forge_core::release_signature::{generate_test_key, sign, Trust};
use serde_json::json;
use triaevum_forge::package_signature::{verify_package, verify_package_json, INVENTORY, SIGNATURE};

fn sha(bytes: &[u8]) -> String {
    forge_base::hashing::sha256_hex(bytes)
}

/// A package with one data file, signed by a fresh test key.
fn package(root: &Path) -> (Trust, Vec<u8>) {
    let data = b"recipe bytes";
    std::fs::create_dir_all(root.join("recipes")).unwrap();
    std::fs::write(root.join("recipes/a.json"), data).unwrap();
    let inventory = serde_json::to_vec(&json!({
        "format": "triaevum_android_forge_package_v1",
        "title": "mm3d",
        "files": [{"path": "recipes/a.json", "sha256": sha(data), "bytes": data.len()}],
    }))
    .unwrap();
    std::fs::write(root.join(INVENTORY), &inventory).unwrap();
    let (pkcs8, key) = generate_test_key();
    let signature = sign(&inventory, &pkcs8).unwrap();
    std::fs::write(root.join(SIGNATURE), serde_json::to_vec(&signature).unwrap()).unwrap();
    (Trust { keys: vec![key], allow_unsigned: false }, inventory)
}

#[test]
fn a_signed_package_verifies() {
    let directory = tempfile::tempdir().unwrap();
    let (trust, _) = package(directory.path());
    let answer = verify_package(directory.path(), &trust, &CancelToken::new()).unwrap();
    assert_eq!(answer["status"], "signed");
    assert_eq!(answer["files"], 1);
    assert_eq!(answer["title"], "mm3d");
}

#[test]
fn a_tampered_inventory_is_refused() {
    let directory = tempfile::tempdir().unwrap();
    let (trust, mut inventory) = package(directory.path());
    inventory.push(b' ');
    std::fs::write(directory.path().join(INVENTORY), &inventory).unwrap();
    assert!(verify_package(directory.path(), &trust, &CancelToken::new()).is_err());
}

#[test]
fn a_changed_package_file_is_refused() {
    let directory = tempfile::tempdir().unwrap();
    let (trust, _) = package(directory.path());
    std::fs::write(directory.path().join("recipes/a.json"), b"recipe bytez").unwrap();
    let error = verify_package(directory.path(), &trust, &CancelToken::new()).unwrap_err();
    assert!(error.to_string().contains("does not match"), "{error}");
}

#[test]
fn an_unsigned_or_foreign_package_is_refused() {
    let directory = tempfile::tempdir().unwrap();
    let (trust, _) = package(directory.path());
    let (_, other) = generate_test_key();
    let foreign = Trust { keys: vec![other], allow_unsigned: false };
    assert!(verify_package(directory.path(), &foreign, &CancelToken::new()).is_err());
    std::fs::remove_file(directory.path().join(SIGNATURE)).unwrap();
    assert!(verify_package(directory.path(), &trust, &CancelToken::new()).is_err());
}

#[test]
fn an_unsafe_path_is_refused() {
    let directory = tempfile::tempdir().unwrap();
    let inventory = serde_json::to_vec(&json!({
        "format": "triaevum_android_forge_package_v1",
        "files": [{"path": "../x", "sha256": sha(b""), "bytes": 0}],
    }))
    .unwrap();
    std::fs::write(directory.path().join(INVENTORY), &inventory).unwrap();
    let (pkcs8, key) = generate_test_key();
    std::fs::write(directory.path().join(SIGNATURE), serde_json::to_vec(&sign(&inventory, &pkcs8).unwrap()).unwrap())
        .unwrap();
    let trust = Trust { keys: vec![key], allow_unsigned: false };
    let error = verify_package(directory.path(), &trust, &CancelToken::new()).unwrap_err();
    assert!(error.to_string().contains("unsafe"), "{error}");
}

#[test]
fn the_android_trust_refuses_a_test_signed_package() {
    // The embedded publisher keys do not include a test key; no override.
    let directory = tempfile::tempdir().unwrap();
    package(directory.path());
    let answer: serde_json::Value = serde_json::from_slice(&verify_package_json(directory.path())).unwrap();
    assert_eq!(answer["status"], "refused");
}
