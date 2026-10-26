// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The release tooling (Python, `release_signature.py`) and Forge (Rust,
//! `release_signature`) agree on publisher signatures both ways: what the
//! publisher signs, Forge verifies, and a tampered manifest fails in both.
//! Runs when TRIAEVUM_FORGE_PYTHON names a Python 3 interpreter with the
//! `cryptography` package.

#![cfg(not(target_os = "android"))]

use std::path::{Path, PathBuf};
use std::process::Command;

use forge_base::hashing::hex;
use forge_core::release_signature::{generate_test_key, sign, verify, Authenticity, Trust, TrustedKey};
use serde_json::Value;

fn python() -> Option<String> {
    std::env::var("TRIAEVUM_FORGE_PYTHON").ok()
}

fn release_tools() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../triaevum_release")
}

fn job(python: &str, args: &[&str]) -> Value {
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/release_signature_jobs.py");
    let output = Command::new(python).arg("-I").arg(script).arg(release_tools()).args(args).output().unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    serde_json::from_slice(&output.stdout).unwrap()
}

#[test]
fn python_signatures_verify_in_forge_and_forge_signatures_in_python() {
    let Some(python) = python() else { return eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set") };
    let directory = tempfile::tempdir().unwrap();
    let manifest = directory.path().join("release-manifest.json");
    let bytes = br#"{"format": "triaevum_public_release_manifest_v1", "release": {"version": "0.8.1"}, "files": []}"#;
    std::fs::write(&manifest, bytes).unwrap();

    // The publisher's tooling signs; Forge verifies with the published key.
    let key = directory.path().join("release.pem");
    let signed = job(&python, &["sign", manifest.to_str().unwrap(), key.to_str().unwrap()]);
    let public_hex = signed["entry"]["public_key"].as_str().unwrap();
    let mut public = [0u8; 32];
    for (index, byte) in public.iter_mut().enumerate() {
        *byte = u8::from_str_radix(&public_hex[index * 2..index * 2 + 2], 16).unwrap();
    }
    let trusted = TrustedKey::new(public);
    assert_eq!(signed["entry"]["key_id"], trusted.key_id);
    let signature = serde_json::to_vec(&signed["signature"]).unwrap();
    let trust = Trust { keys: vec![trusted.clone()], allow_unsigned: false };
    assert_eq!(verify(bytes, Some(&signature), &trust).unwrap(), Authenticity::Signed { key_id: trusted.key_id });
    assert!(verify(b"{}", Some(&signature), &trust).is_err());

    // Forge's signature verifies in the tooling (same message and format).
    let (private, forge_key) = generate_test_key();
    let document = serde_json::to_vec(&sign(bytes, &private).unwrap()).unwrap();
    let path = directory.path().join("forge-signature.json");
    std::fs::write(&path, &document).unwrap();
    let public_hex = hex(&forge_key.public_key);
    let arguments = ["verify", manifest.to_str().unwrap(), path.to_str().unwrap(), &forge_key.key_id, &public_hex];
    assert_eq!(job(&python, &arguments)["errors"], serde_json::json!([]));
    std::fs::write(&manifest, b"{}").unwrap();
    let errors = job(&python, &arguments)["errors"].as_array().unwrap().clone();
    assert_eq!(errors, [serde_json::json!("release signature belongs to another manifest")]);
}
