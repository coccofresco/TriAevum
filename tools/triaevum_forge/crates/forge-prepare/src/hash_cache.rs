// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `forge.HashCache`: titles/.hash-cache.json remembers the SHA-256 of large
//! inputs by resolved path, size and mtime so a re-import does not rehash
//! hundreds of megabytes. A malformed or unknown cache is ignored; `--rehash`
//! (`enabled = false`) neither reads nor writes it.

use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;

use serde_json::{json, Map, Value};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ForgeError, Result};

use crate::paths::{path_string, resolve};
use crate::pyvalue::py_eq;

pub const FORMAT: &str = "triaevum_forge_hash_cache_v1";

pub struct HashCache {
    pub path: PathBuf,
    pub enabled: bool,
    entries: Map<String, Value>,
}

impl HashCache {
    pub fn new(path: impl Into<PathBuf>, enabled: bool) -> Self {
        let path = path.into();
        let mut entries = Map::new();
        if enabled && path.is_file() {
            if let Ok(payload) = load_json_object(&path) {
                if payload.get("format").and_then(Value::as_str) == Some(FORMAT) {
                    if let Some(Value::Object(raw)) = payload.get("entries") {
                        entries = raw
                            .iter()
                            .filter(|(_, value)| value.is_object())
                            .map(|(k, v)| (k.clone(), v.clone()))
                            .collect();
                    }
                }
            }
        }
        Self { path, enabled, entries }
    }

    /// The file's SHA-256, from the cache when size and mtime still match.
    pub fn digest(&mut self, path: &Path, cancel: &CancelToken) -> Result<String> {
        let resolved = resolve(path)?;
        let (size, mtime_ns) = stat(&resolved)?;
        let key = path_string(&resolved)?;
        if self.enabled {
            if let Some(Value::Object(cached)) = self.entries.get(&key) {
                let fresh = cached.get("bytes").is_some_and(|value| py_eq(value, &json!(size)))
                    && cached.get("mtime_ns").is_some_and(|value| py_eq(value, &json!(mtime_ns)));
                if let (true, Some(Value::String(digest))) = (fresh, cached.get("sha256")) {
                    return Ok(digest.clone());
                }
            }
        }
        let digest = hex(&sha256_file(&resolved, cancel)?.1);
        if self.enabled {
            self.entries.insert(key, json!({"bytes": size, "mtime_ns": mtime_ns, "sha256": digest}));
        }
        Ok(digest)
    }

    /// Records a digest computed while Forge itself wrote the file.
    pub fn remember(&mut self, path: &Path, digest: &str) -> Result<()> {
        if !self.enabled {
            return Ok(());
        }
        let resolved = resolve(path)?;
        let (size, mtime_ns) = stat(&resolved)?;
        self.entries.insert(
            path_string(&resolved)?,
            json!({"bytes": size, "mtime_ns": mtime_ns, "sha256": digest.to_lowercase()}),
        );
        Ok(())
    }

    pub fn save(&self) -> Result<()> {
        if self.enabled {
            atomic_write_json(&self.path, &json!({"format": FORMAT, "entries": Value::Object(self.entries.clone())}))?;
        }
        Ok(())
    }
}

/// (st_size, st_mtime_ns) following symlinks, as `Path.stat()` reports them.
fn stat(path: &Path) -> Result<(u64, i64)> {
    let metadata =
        std::fs::metadata(path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    let modified =
        metadata.modified().map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    let nanos = match modified.duration_since(UNIX_EPOCH) {
        Ok(after) => i128::try_from(after.as_nanos()).unwrap_or(i128::MAX),
        Err(before) => -i128::try_from(before.duration().as_nanos()).unwrap_or(i128::MAX),
    };
    let nanos =
        i64::try_from(nanos).map_err(|_| ForgeError::invalid(format!("{} has an invalid mtime", path.display())))?;
    Ok((metadata.len(), nanos))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reuses_and_invalidates_entries() {
        let directory = tempfile::tempdir().unwrap();
        let input = directory.path().join("input.bin");
        std::fs::write(&input, b"abc").unwrap();
        let cache_path = directory.path().join(".hash-cache.json");
        let cancel = CancelToken::new();
        let mut cache = HashCache::new(&cache_path, true);
        let digest = cache.digest(&input, &cancel).unwrap();
        assert_eq!(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        cache.save().unwrap();

        // A cached digest is trusted while size and mtime match.
        let mut payload = load_json_object(&cache_path).unwrap();
        let key = path_string(&resolve(&input).unwrap()).unwrap();
        payload["entries"][&key]["sha256"] = json!("cached");
        atomic_write_json(&cache_path, &Value::Object(payload)).unwrap();
        assert_eq!(HashCache::new(&cache_path, true).digest(&input, &cancel).unwrap(), "cached");
        assert_eq!(HashCache::new(&cache_path, false).digest(&input, &cancel).unwrap(), digest);
        std::fs::write(&input, b"abcd").unwrap();
        assert_ne!(HashCache::new(&cache_path, true).digest(&input, &cancel).unwrap(), "cached");
    }

    #[test]
    fn ignores_malformed_caches_and_rehash_writes_nothing() {
        let directory = tempfile::tempdir().unwrap();
        let cache_path = directory.path().join(".hash-cache.json");
        std::fs::write(&cache_path, b"[1, 2]").unwrap();
        assert!(HashCache::new(&cache_path, true).entries.is_empty());
        std::fs::write(&cache_path, br#"{"format": "other", "entries": {"x": {}}}"#).unwrap();
        assert!(HashCache::new(&cache_path, true).entries.is_empty());
        std::fs::remove_file(&cache_path).unwrap();
        let input = directory.path().join("input.bin");
        std::fs::write(&input, b"abc").unwrap();
        let mut disabled = HashCache::new(&cache_path, false);
        disabled.digest(&input, &CancelToken::new()).unwrap();
        disabled.remember(&input, "AB").unwrap();
        disabled.save().unwrap();
        assert!(!cache_path.exists());
    }
}
