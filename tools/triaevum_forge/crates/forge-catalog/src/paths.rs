// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `pathlib` behaviors the catalog relies on: `Path.resolve()` (non-strict,
//! without the Windows verbatim prefix), `expanduser`, and
//! `common.normalize_relative_path` exactly as `PurePosixPath` parses it.

use std::path::{Component, Path, PathBuf};

use forge_base::{ForgeError, Result};

use crate::pyjson::repr_str;

/// `Path.resolve()` (strict=False): the canonical existing prefix plus the
/// remaining components, spelled like Python (`C:\...`, `\\server\share\...`).
pub fn resolve(path: &Path) -> PathBuf {
    if let Ok(canonical) = std::fs::canonicalize(path) {
        return strip_verbatim(canonical);
    }
    let absolute = std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf());
    let mut existing = absolute.as_path();
    let mut rest = Vec::new();
    while let Some(parent) = existing.parent() {
        if let Some(name) = existing.file_name() {
            rest.push(name.to_owned());
        }
        existing = parent;
        if let Ok(canonical) = std::fs::canonicalize(existing) {
            let mut resolved = strip_verbatim(canonical);
            for name in rest.iter().rev() {
                resolved.push(name);
            }
            return resolved;
        }
    }
    absolute
}

fn strip_verbatim(path: PathBuf) -> PathBuf {
    let Some(text) = path.to_str() else { return path };
    if let Some(unc) = text.strip_prefix(r"\\?\UNC\") {
        return PathBuf::from(format!(r"\\{unc}"));
    }
    match text.strip_prefix(r"\\?\") {
        Some(rest) if rest.as_bytes().get(1) == Some(&b':') => PathBuf::from(rest),
        _ => path,
    }
}

/// `Path.expanduser()` for `~` and `~/...` (the forms Forge documents).
pub fn expanduser(path: &Path) -> PathBuf {
    let mut components = path.components();
    match components.next() {
        Some(Component::Normal(first)) if first == "~" => {
            let home = std::env::var_os(if cfg!(windows) { "USERPROFILE" } else { "HOME" });
            match home {
                Some(home) => PathBuf::from(home).join(components.as_path()),
                None => path.to_path_buf(),
            }
        }
        _ => path.to_path_buf(),
    }
}

/// `common.normalize_relative_path` with `PurePosixPath` parsing: empty and
/// `.` components collapse (`a//b`, `./a`, `a/` are accepted and
/// normalized); absolute, drive (`:`) and `..` paths are rejected.
pub fn normalize_relative_path(value: &str) -> Result<String> {
    let normalized = value.replace('\\', "/");
    let parts: Vec<&str> = normalized.split('/').filter(|part| !part.is_empty() && *part != ".").collect();
    if normalized.is_empty() || normalized.starts_with('/') || normalized.contains(':') || parts.contains(&"..") {
        return Err(ForgeError::invalid(format!("unsafe relative path: {}", repr_str(value))));
    }
    Ok(if parts.is_empty() { ".".to_string() } else { parts.join("/") })
}

/// Joins a normalized POSIX relative path one component at a time, so the
/// native separator is used throughout (Python's `root / relative`).
pub fn join_posix(root: &Path, relative: &str) -> PathBuf {
    let mut path = root.to_path_buf();
    for part in relative.split('/').filter(|part| *part != ".") {
        path.push(part);
    }
    path
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn relative_paths_follow_pure_posix_path() {
        for (input, expected) in
            [("a//b", "a/b"), ("./a", "a"), ("a/", "a"), (".", "."), ("a/./b", "a/b"), ("a\\b/c.bin", "a/b/c.bin")]
        {
            assert_eq!(normalize_relative_path(input).unwrap(), expected, "{input}");
        }
        for bad in ["", "/a", "C:/a", "a/../b", "..", "\\x"] {
            assert!(normalize_relative_path(bad).is_err(), "{bad}");
        }
        assert_eq!(normalize_relative_path("../x").unwrap_err().message, "unsafe relative path: '../x'");
    }

    #[test]
    fn resolve_missing_tail() {
        let directory = tempfile::tempdir().unwrap();
        let resolved = resolve(&directory.path().join("missing").join("file.bin"));
        assert!(resolved.ends_with("missing/file.bin"));
        assert!(!resolved.to_string_lossy().starts_with(r"\\?\"));
    }
}
