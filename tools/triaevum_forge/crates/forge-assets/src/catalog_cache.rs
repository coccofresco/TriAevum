// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The receipt-checked cache the water and scene-light catalogs share:
//! `<data_root>/cache/<kind>/<identity key>/{<catalog>, receipt.json}`,
//! reused while the receipt names the identity and the catalog's SHA-256.
//! A malformed receipt is an error, as in Python.

use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_bytes, atomic_write_json, load_json_object};
use forge_base::hashing::sha256_hex;
use forge_base::{ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::pyvalue::{identity_key, py_eq};

pub fn prepare(
    data_root: &Path,
    kind: &str,
    file_name: &str,
    identity: Map<String, Value>,
    build: impl FnOnce() -> Result<(Vec<u8>, Value)>,
) -> Result<PathBuf> {
    let directory = data_root.join("cache").join(kind).join(identity_key(&identity)?);
    let identity = Value::Object(identity);
    let catalog = directory.join(file_name);
    let receipt_path = directory.join("receipt.json");
    if catalog.is_file() && receipt_path.is_file() {
        let receipt = load_json_object(&receipt_path)?;
        if receipt.get("source").is_some_and(|source| py_eq(source, &identity)) {
            let bytes = std::fs::read(&catalog)
                .map_err(|error| ForgeError::io(format!("Cannot read {}", catalog.display()), error))?;
            if receipt.get("catalog_sha256").and_then(Value::as_str) == Some(sha256_hex(&bytes).as_str()) {
                return Ok(catalog);
            }
        }
    }
    let (payload, summary) = build()?;
    std::fs::create_dir_all(&directory)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", directory.display()), error))?;
    atomic_write_bytes(&catalog, &payload)?;
    atomic_write_json(
        &receipt_path,
        &json!({"source": identity, "summary": summary, "catalog_sha256": sha256_hex(&payload)}),
    )?;
    Ok(catalog)
}
