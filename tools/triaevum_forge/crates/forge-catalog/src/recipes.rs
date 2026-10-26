// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Supported revisions (`triaevum_supported_revisions_v1`): `forge.py`
//! DEFAULT_RECIPES/load_recipe/verify_input and the GUI's recipe options and
//! byte-based recipe matching (`forge_gui.match_extracted_recipe`).

use std::path::{Path, PathBuf};

use forge_base::hashing::{hex, sha256_bytes, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Reason, Result};
use serde_json::{json, Map, Value};

use crate::adapters::import_contract;
use crate::family;
use crate::inputs::TitleInputs;
use crate::paths::{expanduser, join_posix, resolve};
use crate::pyjson::{eq_int, get, py_eq, py_str, py_strip, repr_str, truthy, type_name};

pub const FORMAT: &str = "triaevum_supported_revisions_v1";
/// The package's own supported inputs, beside Forge.
pub const PACKAGED_RECIPES: &str = "recipes/oot3d.json";
/// The source-tree fallback: not the complete list of a qualified package.
pub const EMBEDDED_RECIPES: &str = "tools/triaevum_release/supported_revisions.json";

/// `forge.DEFAULT_RECIPES`: the packaged recipes when present, otherwise the
/// distribution's embedded definitions.
pub fn default_recipes(installation_root: &Path, distribution_root: &Path) -> PathBuf {
    let packaged = join_posix(installation_root, PACKAGED_RECIPES);
    if packaged.is_file() {
        packaged
    } else {
        join_posix(distribution_root, EMBEDDED_RECIPES)
    }
}

/// The recipe list of a supported-revisions file (format and list checked).
pub fn load_recipes(path: &Path) -> Result<Vec<Value>> {
    let mut payload = forge_base::fsutil::load_json_object(path)?;
    if get(&payload, "format").as_str() != Some(FORMAT) {
        return Err(ForgeError::integrity("unsupported supported-revision recipe format"));
    }
    match payload.remove("recipes") {
        Some(Value::Array(recipes)) => Ok(recipes),
        _ => Err(ForgeError::integrity("supported-revision recipe list is malformed")),
    }
}

/// `forge.load_recipe`: exactly one recipe object with this id.
pub fn load_recipe(path: &Path, recipe_id: &str) -> Result<Map<String, Value>> {
    let mut matches: Vec<Map<String, Value>> = load_recipes(path)?
        .into_iter()
        .filter_map(|item| match item {
            Value::Object(map) if get(&map, "id").as_str() == Some(recipe_id) => Some(map),
            _ => None,
        })
        .collect();
    if matches.len() != 1 {
        return Err(ForgeError::new(
            ErrorKind::UnsupportedRevision,
            format!("unknown or duplicate revision recipe: {recipe_id}"),
        ));
    }
    Ok(matches.remove(0))
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RecipeOption {
    pub id: String,
    pub label: String,
}

/// The "Title (Region)" label of a recipe with this id.
pub fn recipe_label(recipe: &Map<String, Value>, id: &str) -> String {
    let title = get(recipe, "title");
    let title = if truthy(title) { py_str(title) } else { id.to_string() };
    let region = get(recipe, "region");
    let region = if truthy(region) { py_str(region) } else { String::new() };
    let region = py_strip(&region);
    if region.is_empty() {
        title
    } else {
        format!("{title} ({region})")
    }
}

/// `forge_gui.load_recipe_options`: "Title (Region)" labels.
pub fn load_recipe_options(path: &Path) -> Result<Vec<RecipeOption>> {
    let mut options = Vec::new();
    for item in load_recipes(path)? {
        let Some(map) = item.as_object() else { continue };
        let Some(id) = get(map, "id").as_str() else { continue };
        options.push(RecipeOption { id: id.to_string(), label: recipe_label(map, id) });
    }
    if options.is_empty() {
        return Err(ForgeError::integrity("no supported title revisions are available"));
    }
    Ok(options)
}

/// `str(contract.get("sha256", "")).lower()`.
fn contract_digest(contract: &Map<String, Value>) -> String {
    contract.get("sha256").map(py_str).unwrap_or_default().to_lowercase()
}

fn unsupported_revision(program_id: u64) -> ForgeError {
    ForgeError::new(
        ErrorKind::UnsupportedRevision,
        format!("the decrypted ROM does not match any supported game revision (program ID {program_id:016X})"),
    )
    .because(Reason::IdentityMismatch)
}

/// Rust addition: the recipes a ROM's executable identifies before its
/// RomFS is read (Forge's recognized-ROM card), in recipe order: the import
/// identity's code and ExHeader, or else a declared source content family's
/// code and normalized ExHeader. `match_extracted_recipe` still decides the
/// installation with all three inputs.
pub fn recognize_recipes(
    program_id: u64,
    code: &[u8],
    exheader: &[u8],
    path: &Path,
) -> Result<Vec<Map<String, Value>>> {
    let recipes = load_recipes(path)?;
    let code_digest = hex(&sha256_bytes(code));
    let exheader_digest = hex(&sha256_bytes(exheader));
    let same = |contract: Option<&Value>, bytes: usize, digest: &str| match contract {
        Some(Value::Object(contract)) => {
            eq_int(get(contract, "bytes"), bytes as u64) && contract_digest(contract) == digest
        }
        _ => false,
    };
    let mut matches = Vec::new();
    for recipe in recipes.iter().filter_map(Value::as_object) {
        let Value::Object(contracts) = import_contract(recipe)? else { continue };
        if same(contracts.get("code"), code.len(), &code_digest)
            && same(contracts.get("exheader"), exheader.len(), &exheader_digest)
        {
            matches.push(recipe.clone());
        }
    }
    if matches.is_empty() {
        let code_identity = json!({"bytes": code.len(), "sha256": code_digest});
        let normalized = family::normalized_exheader_digest(exheader).ok();
        for recipe in recipes.iter().filter_map(Value::as_object) {
            if let Some(source) = family::expected(recipe, "source")? {
                if py_eq(&source["code"], &code_identity) && source["exheader"].as_str() == normalized.as_deref() {
                    matches.push(recipe.clone());
                }
            }
        }
    }
    if matches.is_empty() {
        return Err(unsupported_revision(program_id));
    }
    Ok(matches)
}

/// `forge_gui.match_extracted_recipe`: the one recipe whose import identity
/// (source inputs for adapted revisions) equals the extracted bytes, or else
/// whose declared source content family equals them.
pub fn match_extracted_recipe(
    extracted: &TitleInputs,
    path: &Path,
    cancel: &CancelToken,
) -> Result<Map<String, Value>> {
    let recipes = load_recipes(path)?;
    let mut matches: Vec<&Map<String, Value>> = Vec::new();
    for recipe in recipes.iter().filter_map(Value::as_object) {
        let Value::Object(contracts) = import_contract(recipe)? else { continue };
        let exact = extracted.by_kind().iter().all(|(kind, item)| match contracts.get(*kind) {
            Some(Value::Object(contract)) => {
                eq_int(get(contract, "bytes"), item.bytes) && contract_digest(contract) == item.sha256
            }
            _ => false,
        });
        if exact {
            matches.push(recipe);
        }
    }
    if matches.is_empty() {
        let code = extracted.code.identity();
        let mut candidates = Vec::new();
        for recipe in recipes.iter().filter_map(Value::as_object) {
            if let Some(source) = family::expected(recipe, "source")? {
                if py_eq(&source["code"], &code) {
                    candidates.push((recipe, source));
                }
            }
        }
        if !candidates.is_empty() {
            let actual = family::identity(extracted.paths(), cancel)?;
            matches = candidates.into_iter().filter(|(_, source)| py_eq(source, &actual)).map(|(r, _)| r).collect();
        }
    }
    match matches.as_slice() {
        [] => Err(unsupported_revision(extracted.program_id)),
        [recipe] => Ok((*recipe).clone()),
        _ => Err(ForgeError::integrity("the decrypted ROM matches multiple revision recipes")),
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VerifiedInput {
    pub kind: String,
    pub path: PathBuf,
    pub bytes: u64,
    pub sha256: String,
}

impl VerifiedInput {
    /// `VerifiedInput.as_json()`.
    pub fn as_json(&self) -> Value {
        json!({"path": self.path.to_string_lossy(), "bytes": self.bytes, "sha256": self.sha256})
    }
}

/// How `verify_input` obtains a file digest. The hash cache (forge-prepare)
/// implements this; [`FreshDigest`] always rehashes.
pub trait InputDigest {
    /// Lowercase hex SHA-256 of the (resolved) file.
    fn digest(&mut self, path: &Path) -> Result<String>;
}

pub struct FreshDigest<'a>(pub &'a CancelToken);

impl InputDigest for FreshDigest<'_> {
    fn digest(&mut self, path: &Path) -> Result<String> {
        Ok(hex(&sha256_file(path, self.0)?.1))
    }
}

/// `int(value)` as `verify_input` applied it to a contract size.
fn py_int(value: &Value) -> Result<i128> {
    let type_error = || {
        ForgeError::invalid(format!(
            "int() argument must be a string, a bytes-like object or a real number, not '{}'",
            type_name(value)
        ))
    };
    match value {
        Value::Bool(flag) => Ok(i128::from(*flag)),
        Value::Number(number) => match crate::pyjson::int(value) {
            Some(n) => Ok(n),
            None => {
                let float = number.as_f64().unwrap_or(f64::NAN);
                if !float.is_finite() {
                    return Err(ForgeError::invalid("cannot convert float infinity or NaN to integer"));
                }
                Ok(float.trunc().clamp(-1.0e38, 1.0e38) as i128)
            }
        },
        Value::String(text) => parse_python_int(text)
            .ok_or_else(|| ForgeError::invalid(format!("invalid literal for int() with base 10: {}", repr_str(text)))),
        _ => Err(type_error()),
    }
}

/// Decimal `int(str)`: surrounding whitespace, a sign and single underscores
/// between digits.
fn parse_python_int(text: &str) -> Option<i128> {
    let body = py_strip(text);
    let (negative, digits) = match body.as_bytes().first() {
        Some(b'-') => (true, &body[1..]),
        Some(b'+') => (false, &body[1..]),
        _ => (false, body),
    };
    let valid = !digits.is_empty()
        && !digits.starts_with('_')
        && !digits.ends_with('_')
        && !digits.contains("__")
        && digits.bytes().all(|b| b.is_ascii_digit() || b == b'_');
    if !valid {
        return None;
    }
    let value: i128 = digits.replace('_', "").parse().ok()?;
    Some(if negative { -value } else { value })
}

/// `forge.verify_input`: a regular file with the contract's size and SHA-256.
pub fn verify_input(
    kind: &str,
    path: &Path,
    contract: &Map<String, Value>,
    digest: &mut dyn InputDigest,
) -> Result<VerifiedInput> {
    let resolved = resolve(&expanduser(path));
    let regular = std::fs::symlink_metadata(&resolved).is_ok_and(|m| m.file_type().is_file());
    if !regular {
        return Err(ForgeError::invalid(format!("{kind} input is not a regular file: {}", resolved.display())));
    }
    let size = std::fs::metadata(&resolved)
        .map_err(|error| ForgeError::io(format!("Cannot read {}", resolved.display()), error))?
        .len();
    let expected_size = contract.get("bytes").map_or(Ok(-1), py_int)?;
    if i128::from(size) != expected_size {
        let shown = contract.get("bytes").map_or_else(|| "None".to_string(), py_str);
        return Err(ForgeError::invalid(format!("{kind} size mismatch: expected {shown}, got {size}")));
    }
    let actual = digest.digest(&resolved)?;
    let expected = contract_digest(contract);
    if actual != expected {
        return Err(ForgeError::invalid(format!("{kind} SHA-256 mismatch: expected {expected}, got {actual}")));
    }
    Ok(VerifiedInput { kind: kind.to_string(), path: resolved, bytes: size, sha256: actual })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn python_int_literals() {
        for (text, expected) in [("12", Some(12)), (" -7 ", Some(-7)), ("1_000", Some(1000)), ("+3", Some(3))] {
            assert_eq!(parse_python_int(text), expected, "{text}");
        }
        for text in ["", "1.0", "_1", "1__0", "0x10", "-"] {
            assert_eq!(parse_python_int(text), None, "{text}");
        }
    }

    fn recipes_file(directory: &Path, recipes: Value) -> PathBuf {
        let path = directory.join("recipes.json");
        std::fs::write(&path, json!({"format": FORMAT, "recipes": recipes}).to_string()).unwrap();
        path
    }

    fn identity(bytes: &[u8]) -> Value {
        json!({"bytes": bytes.len(), "sha256": forge_base::hashing::sha256_hex(bytes)})
    }

    #[test]
    fn recognition_reads_the_executable_identity() {
        let directory = tempfile::tempdir().unwrap();
        let (code, exheader) = (b"code".to_vec(), vec![7u8; 2048]);
        let mut other = exheader.clone();
        other[0] = 1;
        let romfs = json!({"bytes": 9, "sha256": "e".repeat(64)});
        let path = recipes_file(
            directory.path(),
            json!([
                {"id": "other", "title": "T", "region": "EUR",
                 "inputs": {"code": identity(&code), "exheader": identity(&other), "romfs": romfs}},
                {"id": "eur", "title": "T", "region": " EUR ",
                 "inputs": {"code": identity(&code), "exheader": identity(&exheader), "romfs": romfs}},
            ]),
        );
        let found = recognize_recipes(0x42, &code, &exheader, &path).unwrap();
        assert_eq!(found.len(), 1);
        assert_eq!(found[0]["id"], "eur");
        assert_eq!(recipe_label(&found[0], "eur"), "T (EUR)");
        let error = recognize_recipes(0x42, b"else", &exheader, &path).unwrap_err();
        assert_eq!(error.kind, ErrorKind::UnsupportedRevision);
        assert_eq!(
            error.message,
            "the decrypted ROM does not match any supported game revision (program ID 0000000000000042)"
        );
    }

    #[test]
    fn recognition_falls_back_to_the_source_family() {
        let directory = tempfile::tempdir().unwrap();
        let (code, exheader) = (b"code".to_vec(), vec![7u8; 2048]);
        let mut dumped = exheader.clone();
        dumped[0x0d] |= 1;
        dumped[0x400] = 9;
        let member = json!({"code": identity(&code),
            "exheader": family::normalized_exheader_digest(&exheader).unwrap(),
            "romfs": {"sha256": "c".repeat(64), "files": 1, "directories": 1, "payload_bytes": 1}});
        let path = recipes_file(
            directory.path(),
            json!([{"id": "fam", "inputs": {"code": identity(&code), "exheader": identity(&exheader),
                "romfs": {"bytes": 9, "sha256": "e".repeat(64)}},
                "data_compatibility": {"format": family::FORMAT, "source": member, "execution": member}}]),
        );
        let found = recognize_recipes(1, &code, &dumped, &path).unwrap();
        assert_eq!(found[0]["id"], "fam");
        assert_eq!(recipe_label(&found[0], "fam"), "fam");
    }

    #[test]
    fn verify_input_rejections() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("code.bin");
        std::fs::write(&path, b"abc").unwrap();
        let digest = forge_base::hashing::sha256_hex(b"abc");
        let cancel = CancelToken::new();
        let contract = |value: Value| value.as_object().unwrap().clone();
        let ok = verify_input(
            "code",
            &path,
            &contract(json!({"bytes": "3", "sha256": digest.to_uppercase()})),
            &mut FreshDigest(&cancel),
        )
        .unwrap();
        assert_eq!(ok.sha256, digest);
        let error =
            verify_input("code", &path, &contract(json!({"sha256": digest})), &mut FreshDigest(&cancel)).unwrap_err();
        assert_eq!(error.message, "code size mismatch: expected None, got 3");
        let error = verify_input("code", &path, &contract(json!({"bytes": 3})), &mut FreshDigest(&cancel)).unwrap_err();
        assert_eq!(error.message, format!("code SHA-256 mismatch: expected , got {digest}"));
        let error =
            verify_input("code", directory.path(), &contract(json!({})), &mut FreshDigest(&cancel)).unwrap_err();
        assert!(error.message.starts_with("code input is not a regular file: "), "{}", error.message);
    }
}
