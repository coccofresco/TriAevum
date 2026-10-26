// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `product_contract.py`: query the actual runtime and initialize only absent
//! user configuration.

use std::path::{Path, PathBuf};

use forge_base::fsutil::atomic_write_json;
use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result};
use forge_catalog::pyjson::{eq_str, get, py_eq};
use serde_json::{json, Map, Value};

use crate::process::describe_exit_status;
use crate::pyerr::{io_failure, json_loads, load_json_object, os_error, tail};
use crate::services::NativeServices;

pub const FORMAT: &str = "triaevum_product_info_v1";
pub const PREFLIGHT_LOG: &str = "TriAevum-preflight-error.json";

fn preflight(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::NativePreflight, message)
}

/// `validate_product_info(info, source_commit)`.
///
/// Deliberate difference: a `FrameRate` that is present but not an object
/// crashed Python with AttributeError; it is the interpolation error here.
pub fn validate_product_info(info: &Map<String, Value>, source_commit: &str) -> Result<()> {
    if !eq_str(get(info, "format"), FORMAT)
        || !eq_str(get(info, "runtime"), "oot3d_native_game")
        || !py_eq(get(info, "whole_aot_plugin_abi"), &json!(2))
    {
        return Err(preflight("This is not the playable TriAevum ABI-v2 runtime"));
    }
    let capabilities = match info.get("capabilities") {
        None => Some(Map::new()),
        Some(Value::Object(map)) => Some(map.clone()),
        Some(_) => None,
    };
    let supported = capabilities.is_some_and(|capabilities| {
        ["nri", "f1", "topscreen"].iter().all(|name| capabilities.get(*name) == Some(&Value::Bool(true)))
    });
    if !supported {
        return Err(preflight("TriAevum is missing NRI, F1 or TopScreen support"));
    }
    if !source_commit.is_empty() && !eq_str(get(info, "source_commit"), source_commit) {
        return Err(preflight("Runtime source commit does not match the release"));
    }
    let graphics = match info.get("default_config") {
        Some(Value::Object(defaults)) => match defaults.get("Graphics") {
            Some(Value::Object(graphics)) => graphics,
            _ => return Err(preflight("Runtime did not publish its graphics defaults")),
        },
        _ => return Err(preflight("Runtime did not publish its graphics defaults")),
    };
    if !eq_str(get(graphics, "Preset"), "Enhanced") {
        return Err(preflight("Runtime defaults do not select the Enhanced preset"));
    }
    let mode = match graphics.get("FrameRate") {
        None => &Value::Null,
        Some(Value::Object(frame_rate)) => get(frame_rate, "Mode"),
        Some(_) => return Err(preflight("Runtime defaults do not enable visual interpolation x2")),
    };
    if !eq_str(mode, "Interpolated2x") {
        return Err(preflight("Runtime defaults do not enable visual interpolation x2"));
    }
    Ok(())
}

/// `str.splitlines(keepends=True)`.
fn split_lines_keep_ends(text: &str) -> Vec<&str> {
    let mut lines = Vec::new();
    let mut start = 0;
    let mut characters = text.char_indices().peekable();
    while let Some((index, character)) = characters.next() {
        let breaks = matches!(
            character,
            '\n' | '\r' | '\u{0b}' | '\u{0c}' | '\u{1c}' | '\u{1d}' | '\u{1e}' | '\u{85}' | '\u{2028}' | '\u{2029}'
        );
        if !breaks {
            continue;
        }
        let mut end = index + character.len_utf8();
        if character == '\r' && characters.peek().is_some_and(|(_, next)| *next == '\n') {
            characters.next();
            end += 1;
        }
        lines.push(&text[start..end]);
        start = end;
    }
    if start < text.len() {
        lines.push(&text[start..]);
    }
    lines
}

/// `_product_document(stdout)`: the runtime's JSON document; lines other
/// software printed before it are skipped.
fn product_document(stdout: &str) -> std::result::Result<Value, String> {
    let lines = split_lines_keep_ends(stdout);
    match lines.iter().position(|line| line.starts_with('{')) {
        Some(index) => json_loads(&lines[index..].concat()),
        None => json_loads(stdout),
    }
}

/// `Path.resolve(strict=True)`.
fn resolve_strict(path: &Path) -> Result<PathBuf> {
    if let Err(error) = std::fs::metadata(path) {
        let mut spelled = std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf());
        // POSIX realpath(strict=True) names the first component it cannot
        // find; Windows names the whole path.
        if cfg!(unix) {
            if let Some(missing) = spelled.ancestors().collect::<Vec<_>>().into_iter().rev().find(|prefix| {
                std::fs::symlink_metadata(prefix).is_err_and(|e| e.kind() == std::io::ErrorKind::NotFound)
            }) {
                spelled = missing.to_path_buf();
            }
        }
        let message = os_error(&error, Some(&spelled));
        return Err(io_failure(error, message));
    }
    forge_prepare::paths::resolve(path)
}

fn file_sha256(path: &Path) -> Result<String> {
    Ok(hex(&sha256_file(path, &CancelToken::new())?.1))
}

/// The receipt of a successful query: the queried binary's SHA-256 and the
/// product document.
#[derive(Clone, Debug, PartialEq)]
pub struct ProductQuery {
    pub runtime_sha256: String,
    pub product: Map<String, Value>,
}

impl ProductQuery {
    /// Python's `{"runtime_sha256": ..., "product": ...}`.
    pub fn to_value(&self) -> Value {
        json!({"runtime_sha256": self.runtime_sha256, "product": Value::Object(self.product.clone())})
    }
}

/// `query_product(executable, source_commit, plugin=None)`: product
/// information of the runtime, or with `plugin` the native ABI preflight of
/// that title plugin. A failed or silent query writes
/// `TriAevum-preflight-error.json` beside the executable.
pub fn query_product(
    services: &dyn NativeServices,
    executable: &Path,
    source_commit: &str,
    plugin: Option<&Path>,
) -> Result<ProductQuery> {
    query_product_at(services, executable, source_commit, plugin, None)
}

/// The loader cause a Windows exit status names (`describe_exit_status`).
fn exit_reason(status: i64) -> Option<Reason> {
    match (status as u64) & 0xFFFF_FFFF {
        0xC000_0139 | 0xC000_0135 | 0xC000_007B => Some(Reason::MissingLoaderDependency),
        0xC000_001D => Some(Reason::UnsupportedCpu),
        _ => None,
    }
}

/// [`query_product`] whose diagnostic file goes to `diagnostic` (an
/// installation's `D/logs/preflight-error.json`) instead of beside the
/// executable. Errors name the runtime query and keep the cause of a failed
/// start (the OS error, the loader status).
pub fn query_product_at(
    services: &dyn NativeServices,
    executable: &Path,
    source_commit: &str,
    plugin: Option<&Path>,
    diagnostic: Option<&Path>,
) -> Result<ProductQuery> {
    let executable = resolve_strict(executable)
        .map_err(|error| error.because(Reason::MissingRuntime).during(Operation::PackageVerify))?;
    query_resolved(services, &executable, source_commit, plugin, diagnostic)
        .map_err(|error| error.during(Operation::RuntimeQuery))
}

fn query_resolved(
    services: &dyn NativeServices,
    executable: &Path,
    source_commit: &str,
    plugin: Option<&Path>,
    diagnostic: Option<&Path>,
) -> Result<ProductQuery> {
    let cannot_query = |error: ForgeError| {
        preflight(&format!("Cannot query the playable runtime: {}", error.message)).caused_by(&error)
    };
    let plugin = plugin.map(resolve_strict).transpose().map_err(cannot_query)?;
    let output = services.product_info(executable, plugin.as_deref()).map_err(cannot_query)?;
    let parsed = if output.status == 0 { Some(product_document(&output.stdout)) } else { None };
    let info = match parsed {
        Some(Ok(info)) => info,
        _ => {
            let status = match &parsed {
                Some(Err(decode_error)) => format!("exited normally without product information ({decode_error})"),
                _ => describe_exit_status(output.status),
            };
            let log = match diagnostic {
                Some(path) => path.to_path_buf(),
                None => executable.parent().unwrap_or(Path::new("")).join(PREFLIGHT_LOG),
            };
            let report = json!({
                "format": "triaevum_preflight_error_v1",
                "platform": crate::pyplatform::platform(),
                "runtime_sha256": file_sha256(executable).map_err(cannot_query)?,
                "operation": if plugin.is_some() { "verify-title-plugin" } else { "product-info" },
                "exit_status": format!("0x{:08X}", (output.status as u64) & 0xFFFF_FFFF),
                "description": status,
                "stdout": tail(&output.stdout, 8000),
                "stderr": tail(&output.stderr, 8000),
            });
            let logging = match atomic_write_json(&log, &report) {
                Ok(()) => format!("Diagnostic log: {}. Review personal paths before sharing.", log.display()),
                Err(error) => format!("Could not save diagnostic log at {}: {}", log.display(), error.message),
            };
            let streams = if !output.stderr.is_empty() { &output.stderr } else { &output.stdout };
            let failure =
                preflight(&format!("Runtime preflight failed ({status}): {}\n{logging}", tail(streams, 2000)));
            return Err(match exit_reason(output.status) {
                Some(reason) if parsed.is_none() => failure.because(reason),
                _ => failure,
            });
        }
    };
    let Value::Object(info) = info else {
        return Err(preflight("Runtime product information is not an object"));
    };
    validate_product_info(&info, source_commit)?;
    if plugin.is_some() && info.get("private_title_loaded") != Some(&Value::Bool(true)) {
        return Err(preflight("The selected private title failed its native ABI preflight"));
    }
    Ok(ProductQuery { runtime_sha256: file_sha256(executable)?, product: info })
}

/// `ensure_runtime_config(path, product)`: writes the runtime's published
/// defaults only when no configuration exists. Every existing choice,
/// including legacy settings, is kept; a malformed file is an error and is
/// never replaced.
pub fn ensure_runtime_config(path: &Path, product: &Map<String, Value>) -> Result<bool> {
    validate_product_info(product, "")?;
    if path.exists() {
        load_json_object(path)?;
        return Ok(false);
    }
    atomic_write_json(path, &product["default_config"])?;
    Ok(true)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splitlines_matches_python() {
        assert_eq!(split_lines_keep_ends("a\nb\r\nc\x0bd"), ["a\n", "b\r\n", "c\x0b", "d"]);
        assert_eq!(split_lines_keep_ends(""), Vec::<&str>::new());
    }

    #[test]
    fn leading_lines_are_skipped() {
        assert_eq!(product_document("hooked\n{\"a\": 1}\n").unwrap(), json!({"a": 1}));
        assert_eq!(product_document("").unwrap_err(), "Expecting value: line 1 column 1 (char 0)");
    }
}
