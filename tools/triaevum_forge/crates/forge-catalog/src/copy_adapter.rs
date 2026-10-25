// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `input_copy_adapter.py`: hash-bound COPY-only normalization of small title
//! inputs. A program holds only `[offset, count]` copies of source bytes, never
//! literal replacement bytes.

use std::collections::HashMap;
use std::path::Path;

use forge_base::hashing::{hex, sha256_bytes};
use forge_base::{ForgeError, Result};
use serde_json::{json, Value};

use crate::paths::resolve;
use crate::pyjson::{has_keys, int, is_hex_digest, is_int, py_eq};

pub const FORMAT: &str = "triaevum_input_copy_adapter_v1";
pub const MAX_BYTES: u64 = 16 * 1024 * 1024;
pub const MAX_OPERATIONS: usize = 500_000;

/// `{"bytes": len, "sha256": hex}`.
pub fn identity(data: &[u8]) -> Value {
    json!({"bytes": data.len(), "sha256": hex(&sha256_bytes(data))})
}

/// Publisher side: the COPY program that rebuilds `canonical` from `source`.
pub fn build_program(source: &[u8], canonical: &[u8]) -> Result<Value> {
    if source.is_empty() || canonical.is_empty() || source.len().max(canonical.len()) as u64 > MAX_BYTES {
        return Err(ForgeError::invalid("normalization input size is outside limits"));
    }
    let widths = [16usize, 4, 1];
    let indexes: Vec<HashMap<&[u8], usize>> = widths
        .iter()
        .map(|&width| {
            let stride = if width == 1 { 1 } else { 4 };
            let mut table = HashMap::new();
            if source.len() >= width {
                for offset in (0..=source.len() - width).step_by(stride) {
                    table.entry(&source[offset..offset + width]).or_insert(offset);
                }
            }
            table
        })
        .collect();
    let mut operations: Vec<[usize; 2]> = Vec::new();
    let mut output = 0usize;
    while output < canonical.len() {
        let found = widths.iter().zip(&indexes).find_map(|(&width, table)| {
            if output + width > canonical.len() {
                return None;
            }
            table.get(&canonical[output..output + width]).map(|&origin| (width, origin))
        });
        let Some((width, origin)) = found else {
            return Err(ForgeError::invalid("canonical bytes cannot be copied from the source"));
        };
        let maximum = (source.len() - origin).min(canonical.len() - output);
        let mut length = width;
        while length + 4096 <= maximum
            && source[origin + length..origin + length + 4096] == canonical[output + length..output + length + 4096]
        {
            length += 4096;
        }
        while length < maximum && source[origin + length] == canonical[output + length] {
            length += 1;
        }
        match operations.last_mut() {
            Some(last) if last[0] + last[1] == origin => last[1] += length,
            _ => operations.push([origin, length]),
        }
        output += length;
    }
    if operations.len() > MAX_OPERATIONS {
        return Err(ForgeError::invalid("normalization recipe has too many operations"));
    }
    Ok(json!({"format": FORMAT, "source": identity(source), "canonical": identity(canonical), "copies": operations}))
}

/// The complete COPY-only schema, also used by the release audit.
pub fn validate_program(program: &Value) -> Result<()> {
    let map = match program {
        Value::Object(map)
            if has_keys(map, &["format", "source", "canonical", "copies"])
                && map.get("format").and_then(Value::as_str) == Some(FORMAT) =>
        {
            map
        }
        _ => return Err(ForgeError::invalid("invalid COPY-only normalization format")),
    };
    for field in ["source", "canonical"] {
        let record = match &map[field] {
            Value::Object(record) if has_keys(record, &["bytes", "sha256"]) => record,
            _ => return Err(ForgeError::invalid("invalid normalization identity")),
        };
        let size = &record["bytes"];
        let valid_size = is_int(size) && int(size).is_some_and(|n| 0 < n && n <= i128::from(MAX_BYTES));
        if !valid_size || !is_hex_digest(&record["sha256"]) {
            return Err(ForgeError::invalid("invalid normalization identity"));
        }
    }
    let size = int(&map["canonical"]["bytes"]).unwrap_or_default();
    let source_size = int(&map["source"]["bytes"]).unwrap_or_default();
    let copies = match &map["copies"] {
        Value::Array(copies) if !copies.is_empty() && copies.len() <= MAX_OPERATIONS => copies,
        _ => return Err(ForgeError::invalid("invalid normalization operation list")),
    };
    let mut written = 0i128;
    for operation in copies {
        let pair = match operation {
            Value::Array(pair) if pair.len() == 2 && pair.iter().all(is_int) => pair,
            _ => return Err(ForgeError::invalid("invalid COPY operation")),
        };
        let (offset, count) = (int(&pair[0]).unwrap_or_default(), int(&pair[1]).unwrap_or_default());
        if offset < 0 || count <= 0 || offset + count > source_size || written + count > size {
            return Err(ForgeError::invalid("COPY operation exceeds input or output bounds"));
        }
        written += count;
    }
    if written != size {
        return Err(ForgeError::invalid("incomplete normalization output"));
    }
    Ok(())
}

pub fn apply_program(source: &[u8], program: &Value) -> Result<Vec<u8>> {
    validate_program(program)?;
    if !py_eq(&program["source"], &identity(source)) {
        return Err(ForgeError::invalid("normalization source identity mismatch"));
    }
    let mut output = Vec::with_capacity(int(&program["canonical"]["bytes"]).unwrap_or_default() as usize);
    for operation in program["copies"].as_array().into_iter().flatten() {
        // Validated: 0 <= offset, offset + count <= the verified source size.
        let offset = int(&operation[0]).unwrap_or_default() as usize;
        let count = int(&operation[1]).unwrap_or_default() as usize;
        output.extend_from_slice(&source[offset..offset + count]);
    }
    if !py_eq(&identity(&output), &program["canonical"]) {
        return Err(ForgeError::invalid("canonical output identity mismatch"));
    }
    Ok(output)
}

/// Applies `program` to `source` and writes a new `destination` atomically.
pub fn normalize_file(source: &Path, destination: &Path, program: &Value) -> Result<Value> {
    let metadata = std::fs::symlink_metadata(source).ok();
    let regular = metadata.as_ref().is_some_and(|m| m.file_type().is_file() && m.len() <= MAX_BYTES);
    if !regular {
        return Err(ForgeError::invalid("invalid normalization source file"));
    }
    if resolve(source) == resolve(destination) || destination.exists() {
        return Err(ForgeError::invalid("normalization requires a new output path"));
    }
    let bytes =
        std::fs::read(source).map_err(|error| ForgeError::io(format!("Cannot read {}", source.display()), error))?;
    let result = apply_program(&bytes, program)?;
    forge_base::fsutil::atomic_write_bytes(destination, &result)?;
    Ok(identity(&result))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture() -> (Vec<u8>, Vec<u8>, Value) {
        let source: Vec<u8> = (0..=255u8).cycle().take(256 * 8).collect();
        let mut target = source[100..700].to_vec();
        target.extend_from_slice(b"abc");
        target.extend_from_slice(&source[..80]);
        let program = build_program(&source, &target).unwrap();
        (source, target, program)
    }

    #[test]
    fn round_trip_reorder_and_reuse() {
        let (source, target, program) = fixture();
        assert_eq!(apply_program(&source, &program).unwrap(), target);
    }

    #[test]
    fn wrong_source_and_changed_program_rejected() {
        let (mut source, _, program) = fixture();
        let mut changed = program.clone();
        changed["copies"][0][0] = json!(changed["copies"][0][0].as_u64().unwrap() + 1);
        assert_eq!(apply_program(&source, &changed).unwrap_err().message, "canonical output identity mismatch");
        source.push(b'1');
        assert_eq!(apply_program(&source, &program).unwrap_err().message, "normalization source identity mismatch");
    }

    #[test]
    fn invalid_operations_rejected() {
        let (source, _, program) = fixture();
        for operation in [
            json!([-1, 10]),
            json!([0, 0]),
            json!([2048, 1]),
            json!([0, 100000]),
            json!([true, 1]),
            json!(["0", 1]),
            json!([0.0, 1]),
        ] {
            let mut changed = program.clone();
            changed["copies"] = json!([operation]);
            assert!(apply_program(&source, &changed).is_err(), "{operation}");
        }
        let mut literal = program.clone();
        literal["literal"] = json!("not allowed");
        assert_eq!(validate_program(&literal).unwrap_err().message, "invalid COPY-only normalization format");
        let mut float_size = program;
        float_size["source"]["bytes"] = json!(2048.0);
        assert_eq!(validate_program(&float_size).unwrap_err().message, "invalid normalization identity");
    }

    #[test]
    fn absent_byte_not_synthesized() {
        let error = build_program(&[b'a'; 32], b"b").unwrap_err();
        assert_eq!(error.message, "canonical bytes cannot be copied from the source");
        assert!(build_program(b"", b"b").is_err());
    }

    #[test]
    fn file_source_and_previous_output_preserved() {
        let (source, target, program) = fixture();
        let directory = tempfile::tempdir().unwrap();
        let (input, output) = (directory.path().join("source"), directory.path().join("output"));
        std::fs::write(&input, &source).unwrap();
        normalize_file(&input, &output, &program).unwrap();
        assert_eq!(std::fs::read(&input).unwrap(), source);
        assert_eq!(std::fs::read(&output).unwrap(), target);
        assert!(normalize_file(&input, &output, &program).is_err());
        assert!(normalize_file(&input, &input, &program).is_err());
        assert!(normalize_file(&directory.path().join("missing"), &directory.path().join("x"), &program).is_err());
    }
}
