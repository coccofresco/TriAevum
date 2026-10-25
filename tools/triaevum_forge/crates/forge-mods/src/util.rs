// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Small helpers shared by the archive, mod and texture-pack modules.

use std::fs;
use std::path::{Path, PathBuf};

use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ForgeError, Result};
use serde_json::Value;
use tempfile::TempDir;

use crate::catalog::Member;

pub(crate) fn io_error(context: impl std::fmt::Display, error: std::io::Error) -> ForgeError {
    ForgeError::io(context, error)
}

/// `_member_ok`: a regular file (never a symbolic link) of the exact size and
/// SHA-256. A missing or different file is `false`; a read failure is an error.
pub(crate) fn file_matches(path: &Path, bytes: u64, sha256: &str, cancel: &CancelToken) -> Result<bool> {
    let Ok(metadata) = fs::symlink_metadata(path) else {
        return Ok(false);
    };
    if !metadata.file_type().is_file() || metadata.len() != bytes {
        return Ok(false);
    }
    Ok(hex(&sha256_file(path, cancel)?.1) == sha256)
}

pub(crate) fn member_matches(path: &Path, member: &Member, cancel: &CancelToken) -> Result<bool> {
    file_matches(path, member.bytes, &member.sha256, cancel)
}

/// `tempfile.mkdtemp(prefix=..., dir=parent)` after creating `parent`.
pub(crate) fn staging_dir(parent: &Path, prefix: &str) -> Result<TempDir> {
    fs::create_dir_all(parent).map_err(|error| io_error(format!("Cannot create {}", parent.display()), error))?;
    tempfile::Builder::new()
        .prefix(prefix)
        .tempdir_in(parent)
        .map_err(|error| io_error(format!("Cannot create a staging folder in {}", parent.display()), error))
}

/// Replaces `destination` by the finished staging folder (`rmtree` then
/// `os.replace`). The staging folder is deleted if anything fails first.
pub(crate) fn publish_directory(staging: TempDir, destination: &Path) -> Result<()> {
    if destination.exists() {
        fs::remove_dir_all(destination)
            .map_err(|error| io_error(format!("Cannot replace {}", destination.display()), error))?;
    }
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent).map_err(|error| io_error(format!("Cannot create {}", parent.display()), error))?;
    }
    fs::rename(staging.path(), destination)
        .map_err(|error| io_error(format!("Cannot store {}", destination.display()), error))?;
    // The folder now lives at its destination; drop must not remove it.
    let _ = staging.keep();
    Ok(())
}

/// Joins a `/`-separated catalogue path onto a directory.
pub(crate) fn join_slash(base: &Path, relative: &str) -> PathBuf {
    relative.split('/').fold(base.to_path_buf(), |path, part| path.join(part))
}

/// A recipe or settings object field as Python's `dict.get` would see it.
pub(crate) fn object_field<'a>(value: &'a Value, key: &str) -> Option<&'a Value> {
    value.as_object().and_then(|object| object.get(key))
}

/// Python `repr` of a `str`, for the legacy identity keys. Identity values are
/// ASCII by construction; anything else fails like `.encode("ascii")` does.
fn python_str_repr(text: &str) -> Result<String> {
    let quote = if text.contains('\'') && !text.contains('"') { '"' } else { '\'' };
    let mut out = String::with_capacity(text.len() + 2);
    out.push(quote);
    for character in text.chars() {
        match character {
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c == quote => {
                out.push('\\');
                out.push(c);
            }
            c if (' '..='~').contains(&c) => out.push(c),
            c if (c as u32) < 0x80 => out.push_str(&format!("\\x{:02x}", c as u32)),
            _ => return Err(ForgeError::invalid("A cache identity must be ASCII")),
        }
    }
    out.push(quote);
    Ok(out)
}

fn python_value_repr(value: &Value) -> Result<String> {
    Ok(match value {
        Value::Null => "None".into(),
        Value::Bool(true) => "True".into(),
        Value::Bool(false) => "False".into(),
        Value::Number(number) => match number.as_f64() {
            Some(float) if number.is_f64() => forge_base::legacy_json::python_float_repr(float),
            _ => number.to_string(),
        },
        Value::String(text) => python_str_repr(text)?,
        Value::Array(items) => {
            let parts = items.iter().map(python_value_repr).collect::<Result<Vec<_>>>()?;
            format!("[{}]", parts.join(", "))
        }
        Value::Object(map) => {
            let parts = map
                .iter()
                .map(|(key, item)| Ok(format!("{}: {}", python_str_repr(key)?, python_value_repr(item)?)))
                .collect::<Result<Vec<_>>>()?;
            format!("{{{}}}", parts.join(", "))
        }
    })
}

/// `hashlib.sha256(repr(sorted(identity.items())).encode("ascii")).hexdigest()`,
/// the legacy cache-folder key.
pub(crate) fn identity_key(identity: &serde_json::Map<String, Value>) -> Result<String> {
    let mut items: Vec<(&String, &Value)> = identity.iter().collect();
    items.sort_by(|a, b| a.0.cmp(b.0));
    let parts = items
        .into_iter()
        .map(|(key, value)| Ok(format!("({}, {})", python_str_repr(key)?, python_value_repr(value)?)))
        .collect::<Result<Vec<_>>>()?;
    Ok(forge_base::hashing::sha256_hex(format!("[{}]", parts.join(", ")).as_bytes()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn identity_repr_spelling() {
        let identity = json!({"b": [["x", "1.9", "ff"]], "a": 1, "c": true, "d": null, "e": "it's"});
        let map = identity.as_object().unwrap();
        let mut items: Vec<_> = map.iter().collect();
        items.sort_by(|a, b| a.0.cmp(b.0));
        let text = items
            .iter()
            .map(|(k, v)| format!("({}, {})", python_str_repr(k).unwrap(), python_value_repr(v).unwrap()))
            .collect::<Vec<_>>()
            .join(", ");
        assert_eq!(text, "('a', 1), ('b', [['x', '1.9', 'ff']]), ('c', True), ('d', None), ('e', \"it's\")");
        assert!(python_str_repr("é").is_err());
    }
}
