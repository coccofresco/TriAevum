// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `common.py` equivalents: durable atomic writes, JSON object loading and
//! relative-path normalization.

use std::io::Write;
use std::path::Path;

use serde_json::{Map, Value};

use crate::error::{ForgeError, Result};
use crate::legacy_json;

/// Writes `payload` to a short hidden temporary file beside `path`, flushes
/// it to disk and renames it over `path`. The temporary name stays shorter
/// than content-addressed destinations near the Win32 path limit (issue 53).
pub fn atomic_write_bytes(path: &Path, payload: &[u8]) -> Result<()> {
    let parent = path.parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or(Path::new("."));
    std::fs::create_dir_all(parent)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    let mut temporary = tempfile::Builder::new()
        .prefix(".")
        .suffix(".tmp")
        .tempfile_in(parent)
        .map_err(|error| ForgeError::io(format!("Cannot write in {}", parent.display()), error))?;
    temporary
        .write_all(payload)
        .and_then(|()| temporary.as_file().sync_all())
        .map_err(|error| ForgeError::io(format!("Cannot write {}", path.display()), error))?;
    temporary
        .persist(path)
        .map_err(|error| ForgeError::io(format!("Cannot replace {}", path.display()), error.error))?;
    Ok(())
}

/// `common.atomic_write_json`: pretty sorted ASCII JSON plus LF.
pub fn atomic_write_json(path: &Path, value: &Value) -> Result<()> {
    atomic_write_bytes(path, &legacy_json::pretty_file_bytes(value))
}

/// `common.load_json_object`: UTF-8 with optional BOM, top level an object.
pub fn load_json_object(path: &Path) -> Result<Map<String, Value>> {
    let bytes =
        std::fs::read(path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    legacy_json::load_object(&bytes)
        .map_err(|error| ForgeError::invalid(format!("{} does not contain a JSON object: {error}", path.display())))
}

/// `common.normalize_relative_path`: backslashes become slashes; empty,
/// absolute, drive-qualified and `.`/`..`/empty components are rejected.
pub fn normalize_relative_path(value: &str) -> Result<String> {
    let normalized = value.replace('\\', "/");
    let unsafe_path = normalized.is_empty()
        || normalized.starts_with('/')
        || normalized.contains(':')
        || normalized.split('/').any(|part| part.is_empty() || part == "." || part == "..");
    if unsafe_path {
        return Err(ForgeError::invalid(format!("unsafe relative path: {value:?}")));
    }
    Ok(normalized)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn relative_paths() {
        assert_eq!(normalize_relative_path("a\\b/c.bin").unwrap(), "a/b/c.bin");
        for bad in ["", "/a", "C:/a", "a/../b", "a//b", "./a", "a/"] {
            assert!(normalize_relative_path(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn atomic_json_round_trip() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("deep/x.json");
        atomic_write_json(&path, &serde_json::json!({"b": 1, "a": "é"})).unwrap();
        assert_eq!(std::fs::read(&path).unwrap(), b"{\n  \"a\": \"\\u00e9\",\n  \"b\": 1\n}\n");
        assert_eq!(load_json_object(&path).unwrap()["b"], 1);
        assert_eq!(std::fs::read_dir(path.parent().unwrap()).unwrap().count(), 1);
    }
}
