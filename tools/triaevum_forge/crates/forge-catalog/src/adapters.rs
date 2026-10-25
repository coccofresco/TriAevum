// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `input_adapters.py`: offline input normalization between ROM extraction
//! and title activation. An adapter is revision-owned and hash-bound on both
//! sides; the original inputs are never modified and the original and
//! execution identities stay distinct.

use std::path::Path;
use std::time::Instant;

use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::copy_adapter::{normalize_file, validate_program};
use crate::family::{self, InputPaths};
use crate::inputs::{InputFile, TitleInputs, KINDS};
use crate::paths::resolve;
use crate::precompiled::checked_file;
use crate::pyjson::{get, has_keys, index, int, is_hex_digest, is_int, py_eq};
use crate::region_assets::{normalize_romfs, ALGORITHM};

pub const FORMAT: &str = "triaevum_revision_input_adapter_v1";
pub const RECEIPT: &str = "input-adaptation.json";
pub const RECEIPT_FORMAT: &str = "triaevum_input_adaptation_receipt_v1";
const MAX_COPY_ARTIFACT_BYTES: i128 = 32 * 1024 * 1024;

fn unsupported() -> ForgeError {
    ForgeError::invalid("Unsupported revision input adapter")
}

/// `import_contract(recipe)`: the identities a ROM must have to select this
/// recipe (the adapter's source inputs when the recipe has one).
pub fn import_contract(recipe: &Map<String, Value>) -> Result<Value> {
    match get(recipe, "input_adapter") {
        Value::Null => Ok(recipe.get("inputs").cloned().unwrap_or_else(|| Value::Object(Map::new()))),
        Value::Object(adapter) if adapter.get("format").and_then(Value::as_str) == Some(FORMAT) => {
            Ok(adapter.get("source_inputs").cloned().unwrap_or_else(|| Value::Object(Map::new())))
        }
        _ => Err(unsupported()),
    }
}

fn valid_identity(record: &Value) -> bool {
    match record {
        Value::Object(record) if has_keys(record, &["bytes", "sha256"]) => {
            let size = &record["bytes"];
            is_int(size) && int(size).is_some_and(|n| n > 0) && is_hex_digest(&record["sha256"])
        }
        _ => false,
    }
}

/// `validate_adapter(root, recipe)`: the verified COPY program, or `None` for
/// a recipe without an adapter.
pub fn validate_adapter(root: &Path, recipe: &Map<String, Value>, cancel: &CancelToken) -> Result<Option<Value>> {
    let adapter = match get(recipe, "input_adapter") {
        Value::Null => return Ok(None),
        Value::Object(adapter)
            if has_keys(adapter, &["format", "source_inputs", "code_copies", "resource_algorithm"])
                && py_eq(&adapter["format"], &json!(FORMAT))
                && py_eq(&adapter["resource_algorithm"], &json!(ALGORITHM)) =>
        {
            adapter
        }
        _ => return Err(unsupported()),
    };
    for contracts in [&adapter["source_inputs"], get(recipe, "inputs")] {
        let Value::Object(contracts) = contracts else {
            return Err(ForgeError::invalid("Adapter requires all three input identities"));
        };
        if !has_keys(contracts, &KINDS) {
            return Err(ForgeError::invalid("Adapter requires all three input identities"));
        }
        if !contracts.values().all(valid_identity) {
            return Err(ForgeError::invalid("Invalid adapter input identity"));
        }
    }
    let inputs = &recipe["inputs"];
    if !py_eq(&adapter["source_inputs"]["exheader"], &inputs["exheader"]) {
        return Err(ForgeError::invalid("This adapter must preserve the original ExHeader"));
    }
    let record = &adapter["code_copies"];
    let valid_record = match record {
        Value::Object(fields) if has_keys(fields, &["path", "bytes", "sha256"]) => {
            is_int(&fields["bytes"]) && int(&fields["bytes"]).is_some_and(|n| 0 < n && n <= MAX_COPY_ARTIFACT_BYTES)
        }
        _ => false,
    };
    if !valid_record {
        return Err(ForgeError::invalid("Invalid code COPY artifact"));
    }
    let program = Value::Object(forge_base::fsutil::load_json_object(&checked_file(root, record, cancel)?)?);
    validate_program(&program)?;
    if !py_eq(&program["source"], &adapter["source_inputs"]["code"]) || !py_eq(&program["canonical"], &inputs["code"]) {
        return Err(ForgeError::invalid("Code COPY artifact does not match revision contracts"));
    }
    Ok(Some(program))
}

/// A completed adaptation: the new execution inputs and the receipt written
/// to `input-adaptation.json`.
#[derive(Clone, Debug)]
pub struct Adaptation {
    pub inputs: TitleInputs,
    pub receipt: Value,
    pub wall_seconds: f64,
}

impl Adaptation {
    /// The dict Python returned: the receipt plus `wall_seconds` (which the
    /// receipt file does not contain).
    pub fn to_value(&self) -> Value {
        let mut value = self.receipt.clone();
        value["wall_seconds"] = json!(self.wall_seconds);
        value
    }
}

fn identity_map(files: &[(&str, &InputFile)]) -> Value {
    Value::Object(files.iter().map(|(kind, file)| (kind.to_string(), file.identity())).collect())
}

/// `adapt_extracted_inputs`: builds new, verified inputs in the new directory
/// `output`. Returns `None` (inputs unchanged) for a recipe without adapter.
pub fn adapt_extracted_inputs(
    extracted: &TitleInputs,
    recipe: &Map<String, Value>,
    root: &Path,
    output: &Path,
    cancel: &CancelToken,
) -> Result<Option<Adaptation>> {
    let Some(program) = validate_adapter(root, recipe, cancel)? else {
        return Ok(None);
    };
    let started = Instant::now();
    let adapter = &recipe["input_adapter"];
    let mut original = Map::new();
    for (kind, item) in extracted.by_kind() {
        let metadata = std::fs::symlink_metadata(&item.path).ok().filter(|m| m.file_type().is_file());
        let Some(metadata) = metadata else {
            return Err(ForgeError::invalid(format!("Adapter {kind} source is not a regular file")));
        };
        let digest = hex(&sha256_file(&item.path, cancel)?.1);
        original.insert(kind.to_string(), json!({"bytes": metadata.len(), "sha256": digest}));
    }
    let original = Value::Object(original);
    if !py_eq(&original, &adapter["source_inputs"]) {
        if get(recipe, "data_compatibility").is_null() {
            return Err(ForgeError::invalid("Adapter source identity mismatch"));
        }
        family::verify(recipe, extracted.paths(), "source", cancel)?;
    }
    let output = resolve(output);
    if extracted.by_kind().iter().any(|(_, item)| resolve(&item.path).starts_with(&output)) {
        return Err(ForgeError::invalid("Adapter output must not contain the source inputs"));
    }
    if let Some(parent) = output.parent() {
        std::fs::create_dir_all(parent).map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    }
    std::fs::create_dir(&output).map_err(|error| ForgeError::io(format!("Cannot create {}", output.display()), error))?;
    match write_adapted(extracted, recipe, &program, &original, &output, cancel) {
        Ok((inputs, receipt)) => Ok(Some(Adaptation { inputs, receipt, wall_seconds: started.elapsed().as_secs_f64() })),
        Err(error) => {
            let _ = std::fs::remove_dir_all(&output);
            Err(error)
        }
    }
}

fn write_adapted(
    extracted: &TitleInputs,
    recipe: &Map<String, Value>,
    program: &Value,
    original: &Value,
    output: &Path,
    cancel: &CancelToken,
) -> Result<(TitleInputs, Value)> {
    normalize_file(&extracted.code.path, &output.join("code.bin"), program)?;
    std::fs::copy(&extracted.exheader.path, output.join("exheader.bin"))
        .map_err(|error| ForgeError::io(format!("Cannot copy {}", extracted.exheader.path.display()), error))?;
    let resources = normalize_romfs(&extracted.romfs.path, &output.join("romfs.bin"), cancel)?;
    let mut files = Vec::with_capacity(3);
    for kind in KINDS {
        let path = output.join(format!("{kind}.bin"));
        let (bytes, digest) = sha256_file(&path, cancel)?;
        files.push(InputFile { path, bytes, sha256: hex(&digest) });
    }
    let [code, exheader, romfs]: [InputFile; 3] = files.try_into().expect("three kinds");
    let inputs = TitleInputs { program_id: extracted.program_id, code, exheader, romfs };
    let actual = identity_map(&inputs.by_kind());
    if !py_eq(&actual, get(recipe, "inputs")) {
        if get(recipe, "data_compatibility").is_null() {
            return Err(ForgeError::invalid("Normalized input identity mismatch; title was not activated"));
        }
        let paths = InputPaths { code: &inputs.code.path, exheader: &inputs.exheader.path, romfs: &inputs.romfs.path };
        family::verify(recipe, paths, "execution", cancel)?;
    }
    let receipt = json!({
        "format": RECEIPT_FORMAT,
        "recipe": index(&Value::Object(recipe.clone()), "id")?,
        "program_id": format!("{:016X}", extracted.program_id),
        "source_inputs": original,
        "execution_inputs": actual,
        "adapter": recipe["input_adapter"],
        "resources": resources,
        "objects_compiled": 0,
    });
    forge_base::fsutil::atomic_write_json(&output.join(RECEIPT), &receipt)?;
    Ok((inputs, receipt))
}
