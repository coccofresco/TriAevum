// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `data_compatibility.py`: publisher-bound content families. No inference
//! from region or filename; a family can never relax the compiled code.

use std::io::Read;
use std::path::Path;

use forge_base::hashing::{hex, sha256_bytes, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::pyjson::{attr_get, get, has_keys, int, is_hex_digest, is_int, py_eq};
use crate::romfs_identity::fingerprint;

pub const FORMAT: &str = "triaevum_content_family_v1";
const EXHEADER_BYTES: usize = 2048;

/// The three title inputs by kind.
#[derive(Clone, Copy, Debug)]
pub struct InputPaths<'a> {
    pub code: &'a Path,
    pub exheader: &'a Path,
    pub romfs: &'a Path,
}

/// ExHeader bytes that define process data: the ExeFS compression and SD
/// storage flags (low two bits of 0x0d) and the signature/key block
/// 0x400..0x600 are cleared; every other byte stays checked.
pub fn normalized_exheader_digest(header: &[u8]) -> Result<String> {
    if header.len() != EXHEADER_BYTES {
        return Err(ForgeError::invalid("Content family requires a complete ExHeader"));
    }
    let mut header = header.to_vec();
    header[0x0d] &= !3;
    header[0x400..0x600].fill(0);
    Ok(hex(&sha256_bytes(&header)))
}

/// `identity(paths)`: `{"code": {bytes, sha256}, "exheader": hex, "romfs": fingerprint}`.
pub fn identity(paths: InputPaths<'_>, cancel: &CancelToken) -> Result<Value> {
    let read_error = |path: &Path, error| ForgeError::io(format!("Cannot read {}", path.display()), error);
    let mut header = Vec::with_capacity(EXHEADER_BYTES + 1);
    std::fs::File::open(paths.exheader)
        .and_then(|file| file.take(EXHEADER_BYTES as u64 + 1).read_to_end(&mut header))
        .map_err(|error| read_error(paths.exheader, error))?;
    let exheader = normalized_exheader_digest(&header)?;
    let (code_bytes, code_digest) = sha256_file(paths.code, cancel)?;
    Ok(json!({
        "code": {"bytes": code_bytes, "sha256": hex(&code_digest)},
        "exheader": exheader,
        "romfs": fingerprint(paths.romfs, cancel)?.to_value(),
    }))
}

fn unsupported() -> ForgeError {
    ForgeError::invalid("Unsupported content-family contract")
}

/// `expected(recipe, phase)`: the validated family for `phase` ("source" or
/// "execution"), or `None` when the recipe declares no family.
pub fn expected(recipe: &Map<String, Value>, phase: &str) -> Result<Option<Value>> {
    let contract = match get(recipe, "data_compatibility") {
        Value::Null => return Ok(None),
        Value::Object(contract) => contract,
        _ => return Err(unsupported()),
    };
    if !has_keys(contract, &["format", "source", "execution"])
        || contract["format"].as_str() != Some(FORMAT)
        || !matches!(phase, "source" | "execution")
    {
        return Err(unsupported());
    }
    let empty = Value::Object(Map::new());
    for member in ["source", "execution"] {
        let family = match &contract[member] {
            Value::Object(family) if has_keys(family, &["code", "exheader", "romfs"]) => family,
            _ => return Err(ForgeError::invalid("Invalid content-family identity")),
        };
        let (code, romfs) = match (&family["code"], &family["romfs"]) {
            (Value::Object(code), Value::Object(romfs))
                if has_keys(code, &["bytes", "sha256"])
                    && has_keys(romfs, &["sha256", "files", "directories", "payload_bytes"]) =>
            {
                (code, romfs)
            }
            _ => return Err(ForgeError::invalid("Invalid content-family record")),
        };
        if ![&family["exheader"], &code["sha256"], &romfs["sha256"]].into_iter().all(is_hex_digest) {
            return Err(ForgeError::invalid("Invalid content-family digest"));
        }
        let sizes = [&code["bytes"], &romfs["files"], &romfs["directories"], &romfs["payload_bytes"]];
        if !sizes.into_iter().all(|value| is_int(value) && int(value).is_some_and(|n| n > 0)) {
            return Err(ForgeError::invalid("Invalid content-family size"));
        }
        let recipe_inputs = recipe.get("inputs").unwrap_or(&empty);
        let inputs = if member == "source" {
            let adapter = recipe.get("input_adapter").unwrap_or(&empty);
            match attr_get(adapter, "source_inputs")? {
                Value::Null if !adapter.as_object().is_some_and(|a| a.contains_key("source_inputs")) => recipe_inputs,
                found => found,
            }
        } else {
            recipe_inputs
        };
        if !py_eq(&family["code"], attr_get(inputs, "code")?) {
            return Err(ForgeError::invalid("Content family cannot change the compiled code identity"));
        }
    }
    Ok(Some(contract[phase].clone()))
}

/// `verify(recipe, paths, phase)`: the actual identity, which must equal
/// the declared family.
pub fn verify(recipe: &Map<String, Value>, paths: InputPaths<'_>, phase: &str, cancel: &CancelToken) -> Result<Value> {
    let Some(contract) = expected(recipe, phase)? else {
        return Err(ForgeError::new(ErrorKind::UnsupportedRevision, "No content-family contract for this title"));
    };
    let actual = identity(paths, cancel)?;
    if !py_eq(&actual, &contract) {
        let different: Vec<&str> =
            ["code", "exheader", "romfs"].into_iter().filter(|kind| !py_eq(&actual[*kind], &contract[*kind])).collect();
        return Err(ForgeError::new(
            ErrorKind::UnsupportedRevision,
            format!("Incompatible {phase} data: {}; no title was activated", different.join(", ")),
        ));
    }
    Ok(actual)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn family_recipe() -> Map<String, Value> {
        let family = json!({"code": {"bytes": 3, "sha256": "a".repeat(64)}, "exheader": "b".repeat(64),
                            "romfs": {"sha256": "c".repeat(64), "files": 1, "directories": 1, "payload_bytes": 1}});
        json!({"id": "x", "inputs": {"code": {"bytes": 3, "sha256": "a".repeat(64)}},
               "data_compatibility": {"format": FORMAT, "source": family, "execution": family}})
        .as_object()
        .unwrap()
        .clone()
    }

    #[test]
    fn contract_rejections() {
        let recipe = family_recipe();
        assert!(expected(&recipe, "execution").unwrap().is_some());
        assert!(expected(&Map::new(), "source").unwrap().is_none());
        assert_eq!(expected(&recipe, "other").unwrap_err().message, "Unsupported content-family contract");
        let cases: [(&str, Value, &str); 5] = [
            (
                "/data_compatibility/source/code/sha256",
                json!("d".repeat(64)),
                "Content family cannot change the compiled code identity",
            ),
            ("/data_compatibility/source/exheader", json!("B".repeat(64)), "Invalid content-family digest"),
            ("/data_compatibility/source/romfs/files", json!(true), "Invalid content-family size"),
            ("/data_compatibility/execution/romfs", json!({"sha256": "c".repeat(64)}), "Invalid content-family record"),
            ("/data_compatibility/format", json!("unbound"), "Unsupported content-family contract"),
        ];
        for (pointer, value, message) in cases {
            let mut changed = Value::Object(recipe.clone());
            *changed.pointer_mut(pointer).unwrap() = value;
            assert_eq!(expected(changed.as_object().unwrap(), "source").unwrap_err().message, message, "{pointer}");
        }
        let mut adapter_null = recipe.clone();
        adapter_null.insert("input_adapter".into(), Value::Null);
        assert!(expected(&adapter_null, "source").is_err());
    }

    #[test]
    fn exheader_normalization() {
        let mut header = vec![0u8; 2048];
        let digest = normalized_exheader_digest(&header).unwrap();
        header[0x0d] = 3;
        header[0x400..0x600].fill(b'S');
        assert_eq!(normalized_exheader_digest(&header).unwrap(), digest);
        header[0x28] = 1;
        assert_ne!(normalized_exheader_digest(&header).unwrap(), digest);
        assert!(normalized_exheader_digest(&header[..2047]).is_err());
    }
}
