// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Python `Path.expanduser()` and non-strict `Path.resolve()` spellings, so
//! returned paths and messages match the Python Forge.

use std::path::{Component, Path, PathBuf};

/// Expands a leading `~` component to the user's home directory. `~user`
/// forms are left unchanged (the Forge never receives them from its UIs).
pub fn expand_user(path: &Path) -> PathBuf {
    let mut components = path.components();
    if components.next() != Some(Component::Normal("~".as_ref())) {
        return path.to_path_buf();
    }
    let home = if cfg!(windows) { std::env::var_os("USERPROFILE") } else { std::env::var_os("HOME") };
    match home {
        Some(home) if !home.is_empty() => PathBuf::from(home).join(components.as_path()),
        _ => path.to_path_buf(),
    }
}

/// Absolute path with symlinks resolved as far as the path exists and the
/// remaining components normalized lexically, like `os.path.realpath`.
pub fn resolve(path: &Path) -> PathBuf {
    let absolute = std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf());
    let mut existing = absolute.as_path();
    let mut tail = Vec::new();
    loop {
        if let Ok(canonical) = std::fs::canonicalize(existing) {
            let mut resolved = strip_verbatim(canonical);
            for component in tail.iter().rev() {
                match component {
                    Component::ParentDir => {
                        resolved.pop();
                    }
                    Component::CurDir => {}
                    other => resolved.push(other.as_os_str()),
                }
            }
            return resolved;
        }
        match (existing.parent(), existing.components().next_back()) {
            (Some(parent), Some(last)) => {
                tail.push(last);
                existing = parent;
            }
            _ => return absolute,
        }
    }
}

/// `\\?\C:\x` becomes `C:\x` and `\\?\UNC\host\share` becomes `\\host\share`,
/// as Python's realpath spells them.
fn strip_verbatim(path: PathBuf) -> PathBuf {
    if !cfg!(windows) {
        return path;
    }
    let text = path.to_string_lossy();
    if let Some(rest) = text.strip_prefix(r"\\?\UNC\") {
        return PathBuf::from(format!(r"\\{rest}"));
    }
    if let Some(rest) = text.strip_prefix(r"\\?\") {
        let bytes = rest.as_bytes();
        if bytes.len() >= 2 && bytes[0].is_ascii_alphabetic() && bytes[1] == b':' {
            return PathBuf::from(rest);
        }
    }
    path
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolves_missing_tail_lexically() {
        let directory = tempfile::tempdir().unwrap();
        let base = resolve(directory.path());
        let missing = directory.path().join("a").join("..").join("b").join(".").join("c");
        assert_eq!(resolve(&missing), base.join("b").join("c"));
        assert!(!base.to_string_lossy().starts_with(r"\\?\"));
    }

    #[test]
    fn expands_only_a_leading_tilde() {
        assert_eq!(expand_user(Path::new("a/~")), PathBuf::from("a/~"));
        let expanded = expand_user(Path::new("~/x"));
        assert!(expanded.ends_with("x"));
    }
}
