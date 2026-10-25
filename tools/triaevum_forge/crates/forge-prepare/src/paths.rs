// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Path spelling as the Python Forge produced it. Absolute paths and
//! relative references are written into hashed metadata (process manifest,
//! content.tap, active-title.json, the hash cache keys), so `resolve`,
//! `relpath` and `is_relative_to` follow `pathlib`/`os.path` rather than
//! Rust's own conventions (`std::fs::canonicalize` returns `\\?\` paths and
//! fails for missing paths; `Path::starts_with` is case-sensitive on Windows).
//!
//! Also the port of `installation_context.py`.

use std::path::{Component, Path, PathBuf};

use forge_base::{ForgeError, Result};

/// UTF-8 spelling of a path, `str(path)` in Python. Non-UTF-8 paths are
/// refused: Python would have written surrogate escapes into the JSON.
pub fn path_string(path: &Path) -> Result<String> {
    path.to_str()
        .map(str::to_owned)
        .ok_or_else(|| ForgeError::invalid(format!("path is not valid Unicode: {}", path.display())))
}

/// `Path.expanduser()`: a leading `~` (or, on Windows, `~user`) component.
pub fn expanduser(path: &Path) -> PathBuf {
    let Some(text) = path.to_str() else { return path.to_path_buf() };
    if !text.starts_with('~') {
        return path.to_path_buf();
    }
    let end = text.find(|c| c == '/' || (cfg!(windows) && c == '\\')).unwrap_or(text.len());
    let Some(home) = home_directory() else { return path.to_path_buf() };
    let user = &text[1..end];
    let home = if user.is_empty() {
        home
    } else if cfg!(windows) {
        // ntpath.expanduser: another user's profile sits beside this one.
        if home.file_name().and_then(|name| name.to_str()) == Some(user) {
            home
        } else {
            home.parent().map(|parent| parent.join(user)).unwrap_or(home)
        }
    } else {
        // posixpath consults the password database; not modelled.
        return path.to_path_buf();
    };
    let rest = text[end..].trim_start_matches(['/', '\\']);
    if rest.is_empty() {
        home
    } else {
        home.join(rest)
    }
}

fn home_directory() -> Option<PathBuf> {
    if cfg!(windows) {
        if let Some(profile) = std::env::var_os("USERPROFILE") {
            return Some(PathBuf::from(profile));
        }
        let drive = std::env::var_os("HOMEDRIVE").unwrap_or_default();
        let home = std::env::var_os("HOMEPATH")?;
        let mut joined = drive;
        joined.push(home);
        Some(PathBuf::from(joined))
    } else {
        std::env::var_os("HOME").map(PathBuf::from)
    }
}

/// `Path.resolve()` (non-strict): absolute, symlinks resolved, on-disk
/// spelling for the existing prefix and the given spelling for the rest.
pub fn resolve(path: &Path) -> Result<PathBuf> {
    let absolute = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir().map_err(|error| ForgeError::io("Cannot read the working directory", error))?.join(path)
    };
    if cfg!(windows) {
        Ok(resolve_windows(&normalize(&absolute)))
    } else {
        resolve_posix(&absolute)
    }
}

/// ntpath.realpath: normalize lexically first, then ask the OS for the
/// final name of the longest existing prefix and append the missing tail.
fn resolve_windows(normalized: &Path) -> PathBuf {
    let mut existing = normalized.to_path_buf();
    let mut tail: Vec<std::ffi::OsString> = Vec::new();
    loop {
        if let Ok(canonical) = std::fs::canonicalize(&existing) {
            let mut result = strip_verbatim(&canonical);
            for name in tail.iter().rev() {
                result.push(name);
            }
            return result;
        }
        match (existing.parent(), existing.file_name()) {
            (Some(parent), Some(name)) => {
                tail.push(name.to_os_string());
                existing = parent.to_path_buf();
            }
            _ => return normalized.to_path_buf(),
        }
    }
}

fn strip_verbatim(path: &Path) -> PathBuf {
    let text = path.as_os_str().to_string_lossy();
    if let Some(rest) = text.strip_prefix(r"\\?\UNC\") {
        PathBuf::from(format!(r"\\{rest}"))
    } else if let Some(rest) = text.strip_prefix(r"\\?\") {
        PathBuf::from(rest)
    } else {
        path.to_path_buf()
    }
}

/// posixpath.realpath: components left to right, following symlinks while
/// the path exists and appending lexically once a component is missing.
fn resolve_posix(absolute: &Path) -> Result<PathBuf> {
    let mut resolved = PathBuf::new();
    let mut missing = false;
    for component in absolute.components() {
        match component {
            Component::Prefix(_) | Component::RootDir => resolved.push(component.as_os_str()),
            Component::CurDir => {}
            Component::ParentDir => {
                resolved.pop();
            }
            Component::Normal(name) => {
                resolved.push(name);
                if missing {
                    continue;
                }
                match std::fs::canonicalize(&resolved) {
                    Ok(canonical) => resolved = canonical,
                    Err(_) if std::fs::symlink_metadata(&resolved).is_err() => missing = true,
                    Err(error) => {
                        return Err(ForgeError::io(format!("Cannot resolve {}", resolved.display()), error));
                    }
                }
            }
        }
    }
    Ok(resolved)
}

/// Lexical `normpath`: drops `.`, folds `..` and rebuilds with the native
/// separator.
fn normalize(path: &Path) -> PathBuf {
    let mut result = PathBuf::new();
    let mut depth = 0usize;
    for component in path.components() {
        match component {
            Component::Prefix(_) | Component::RootDir => result.push(component.as_os_str()),
            Component::CurDir => {}
            Component::ParentDir => {
                if depth > 0 {
                    result.pop();
                    depth -= 1;
                }
            }
            Component::Normal(name) => {
                result.push(name);
                depth += 1;
            }
        }
    }
    result
}

fn component_key(component: &Component<'_>) -> String {
    let text = component.as_os_str().to_string_lossy();
    if cfg!(windows) {
        text.replace('/', "\\").to_lowercase()
    } else {
        text.into_owned()
    }
}

/// `PurePath.is_relative_to` on already resolved paths (case-insensitive on
/// Windows, like pathlib's normcase comparison).
pub fn is_relative_to(path: &Path, base: &Path) -> bool {
    let mut path_components = path.components();
    for base_component in base.components() {
        match path_components.next() {
            Some(component) if component_key(&component) == component_key(&base_component) => {}
            _ => return false,
        }
    }
    true
}

/// `Path(os.path.relpath(path, base)).as_posix()` for resolved paths on one
/// volume.
pub fn relpath_posix(path: &Path, base: &Path) -> Result<String> {
    let path_parts: Vec<Component<'_>> = path.components().collect();
    let base_parts: Vec<Component<'_>> = base.components().collect();
    let common = path_parts
        .iter()
        .zip(&base_parts)
        .take_while(|(left, right)| component_key(left) == component_key(right))
        .count();
    if common == 0 {
        return Err(ForgeError::invalid(format!(
            "path is on mount {}, start on mount {}",
            path.display(),
            base.display()
        )));
    }
    let mut parts: Vec<String> = vec!["..".to_string(); base_parts.len() - common];
    for component in &path_parts[common..] {
        parts.push(path_string(Path::new(component.as_os_str()))?);
    }
    if parts.is_empty() {
        return Ok(".".to_string());
    }
    Ok(parts.join("/"))
}

/// `installation_context.resolve_reference`: a relative reference is read
/// from `base`; an absolute one stays as written.
pub fn resolve_reference(value: &str, base: &Path) -> Result<PathBuf> {
    let path = Path::new(value);
    if path.is_absolute() {
        resolve(path)
    } else {
        resolve(&base.join(path))
    }
}

/// Persisted path scope: inside the installation root (written relative,
/// with `/`) or outside it (written as an explicit absolute path).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PathScope {
    Relative,
    External,
}

impl PathScope {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Relative => "relative",
            Self::External => "external",
        }
    }
}

/// `installation_context.InstallationContext`.
#[derive(Clone, Debug)]
pub struct InstallationContext {
    pub root: PathBuf,
}

impl InstallationContext {
    pub fn new(root: impl Into<PathBuf>) -> Self {
        Self { root: root.into() }
    }

    /// `reference(path, base)`: relative to `base` when both are inside the
    /// root, otherwise the resolved absolute path.
    pub fn reference(&self, path: &Path, base: &Path) -> Result<String> {
        let path = resolve(path)?;
        if self.scope(&path, Some(base))? == PathScope::Relative {
            return relpath_posix(&path, &resolve(base)?);
        }
        path_string(&path)
    }

    /// `scope(path, base=None)`.
    pub fn scope(&self, path: &Path, base: Option<&Path>) -> Result<PathScope> {
        let root = resolve(&self.root)?;
        let mut internal = is_relative_to(&resolve(path)?, &root);
        if let Some(base) = base {
            internal = internal && is_relative_to(&resolve(base)?, &root);
        }
        Ok(if internal { PathScope::Relative } else { PathScope::External })
    }

    /// `profile_argument`: package files named from `${runtime_dir}`, files
    /// inside the installation from `${profile_dir}`, the rest absolute.
    pub fn profile_argument(&self, path: &Path, profile: &Path, runtime_dir: Option<&Path>) -> Result<String> {
        if let Some(runtime_dir) = runtime_dir {
            let resolved = resolve(path)?;
            let runtime = resolve(runtime_dir)?;
            if is_relative_to(&resolved, &runtime) {
                let parts: Vec<String> = resolved
                    .components()
                    .skip(runtime.components().count())
                    .map(|component| path_string(Path::new(component.as_os_str())))
                    .collect::<Result<_>>()?;
                let relative = if parts.is_empty() { ".".to_string() } else { parts.join("/") };
                return Ok(format!("${{runtime_dir}}/{relative}"));
            }
        }
        let parent = profile.parent().unwrap_or(Path::new(""));
        let reference = self.reference(path, parent)?;
        if Path::new(&reference).is_absolute() {
            Ok(reference)
        } else {
            Ok(format!("${{profile_dir}}/{reference}"))
        }
    }
}

/// `expand_profile_argument`: the runtime's own placeholder expansion.
pub fn expand_profile_argument(value: &str, profile: &Path, runtime_dir: Option<&Path>) -> Result<String> {
    let mut value = value.to_string();
    if let Some(runtime_dir) = runtime_dir {
        value = value.replace("${runtime_dir}", &posix_spelling(&resolve(runtime_dir)?)?);
    }
    let profile_dir = resolve(profile)?;
    let profile_dir = profile_dir.parent().unwrap_or(Path::new(""));
    Ok(value.replace("${profile_dir}", &posix_spelling(profile_dir)?))
}

/// `PurePath.as_posix()`.
fn posix_spelling(path: &Path) -> Result<String> {
    let text = path_string(path)?;
    Ok(if cfg!(windows) { text.replace('\\', "/") } else { text })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_tail_keeps_its_spelling() {
        let directory = tempfile::tempdir().unwrap();
        let base = resolve(directory.path()).unwrap();
        let missing = resolve(&directory.path().join("A").join("..").join("b").join("C")).unwrap();
        assert_eq!(missing, base.join("b").join("C"));
        assert!(!path_string(&base).unwrap().starts_with(r"\\?\"));
    }

    // installation_context tests: profile paths follow a moved root.
    #[test]
    fn profile_paths_follow_new_root() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path().join("original");
        let moved = directory.path().join("moved installation");
        let context = InstallationContext::new(&root);
        let profile = root.join("TriAevum.launch.json");
        for suffix in ["private-plugins/hash/title.dll", "config/runtime.json", "saves"] {
            let argument = context.profile_argument(&root.join(suffix), &profile, None).unwrap();
            assert!(argument.starts_with("${profile_dir}/"), "{argument}");
            let expanded = expand_profile_argument(&argument, &moved.join("TriAevum.launch.json"), None).unwrap();
            assert_eq!(resolve(Path::new(&expanded)).unwrap(), resolve(&moved.join(suffix)).unwrap());
        }
    }

    #[test]
    fn package_files_follow_the_runtime_across_mounts() {
        let directory = tempfile::tempdir().unwrap();
        let data = directory.path().join("data").join("activation");
        let first = directory.path().join(".mount_first/usr/lib/triaevum");
        let second = directory.path().join(".mount_second/usr/lib/triaevum");
        let profile = data.join("TriAevum.launch.json");
        let argument =
            InstallationContext::new(&data).profile_argument(&first.join("resources"), &profile, Some(&first)).unwrap();
        assert_eq!(argument, "${runtime_dir}/resources");
        let expanded = expand_profile_argument(&argument, &profile, Some(&second)).unwrap();
        assert_eq!(resolve(Path::new(&expanded)).unwrap(), resolve(&second.join("resources")).unwrap());
    }

    #[test]
    fn external_inputs_remain_explicit() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path().join("installation");
        let external = directory.path().join("user ROM").join("code.bin");
        let context = InstallationContext::new(&root);
        assert_eq!(context.scope(&external, None).unwrap(), PathScope::External);
        let resolved = path_string(&resolve(&external).unwrap()).unwrap();
        assert_eq!(context.reference(&external, &root).unwrap(), resolved);
        assert_eq!(resolve_reference(&path_string(&external).unwrap(), &root).unwrap(), resolve(&external).unwrap());
    }

    #[test]
    fn external_base_cannot_make_internal_path_portable() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path().join("installation");
        let base = directory.path().join("external data");
        let context = InstallationContext::new(&root);
        let inside = root.join("game.dll");
        assert_eq!(context.reference(&inside, &base).unwrap(), path_string(&resolve(&inside).unwrap()).unwrap());
    }

    #[test]
    fn relative_references_climb_with_slashes() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path().join("data");
        let context = InstallationContext::new(&root);
        let title = root.join("titles/fixture/0123456789abcdef");
        let input = root.join("sources/abc/code.bin");
        assert_eq!(context.reference(&input, &title).unwrap(), "../../../sources/abc/code.bin");
        assert_eq!(context.scope(&input, Some(&title)).unwrap(), PathScope::Relative);
        assert_eq!(context.reference(&title, &title).unwrap(), ".");
    }

    #[cfg(windows)]
    #[test]
    fn windows_comparisons_ignore_case() {
        assert!(is_relative_to(Path::new(r"C:\Data\Titles\x"), Path::new(r"c:\data")));
        assert_eq!(relpath_posix(Path::new(r"C:\A\b\c"), Path::new(r"c:\a\D")).unwrap(), "../b/c");
        assert!(relpath_posix(Path::new(r"C:\A"), Path::new(r"D:\A")).is_err());
    }
}
