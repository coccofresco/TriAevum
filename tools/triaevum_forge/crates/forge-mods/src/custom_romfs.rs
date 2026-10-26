// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The custom RomFS folder (owner decision 6, UX plan WP-07): explicitly
//! unsupported, user-supplied files that replace files of the game's RomFS
//! through the same O3RO overlay as the catalogued mods. Off by default;
//! never packaged or redistributed; no support promise.
//!
//! Format (`docs/TRIAEVUM_ENHANCEMENTS.md`):
//!
//! - The folder is `D/mods/custom-romfs/` and mirrors RomFS paths:
//!   `actor/zelda_link_boy_new.zar` replaces `rom:/actor/zelda_link_boy_new.zar`.
//!   A folder holding a `romfs/` folder uses that as the root.
//! - An optional manifest `triaevum-custom-romfs.json`:
//!   `{"format": "triaevum_custom_romfs_v1", "name": "<up to 80 characters>"}`
//!   (other members are ignored). Hidden files, `desktop.ini` and
//!   `Thumbs.db` are ignored.
//! - The switch is `D/config/custom_romfs.json`:
//!   `{"format": "triaevum_custom_romfs_settings_v1", "enabled": true|false}`;
//!   without the file the folder is off.
//!
//! Validation rejects the whole folder (nothing of it is applied) for a
//! symbolic link, junction or special file, a name that is not printable
//! ASCII or contains `\` or `:`, an empty file, a file over 64 MiB (the
//! overlay's limit), more than 4096 files, more than 1 GiB in total, more
//! than 16 folder levels, or an invalid manifest. Files the base RomFS does
//! not have are flagged and left out (the overlay only replaces files), and
//! files a catalogued mod also replaces are reported as conflicts: the
//! custom folder's file wins.

use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use serde_json::{json, Value};

pub const FOLDER: &str = "custom-romfs";
pub const MANIFEST_NAME: &str = "triaevum-custom-romfs.json";
pub const MANIFEST_FORMAT: &str = "triaevum_custom_romfs_v1";
pub const SETTINGS_NAME: &str = "custom_romfs.json";
pub const SETTINGS_FORMAT: &str = "triaevum_custom_romfs_settings_v1";
pub const MAXIMUM_FILES: usize = 4096;
pub const MAXIMUM_FILE_BYTES: u64 = crate::overlay::MAXIMUM_MEMBER_BYTES;
pub const MAXIMUM_TOTAL_BYTES: u64 = 1024 * 1024 * 1024;
pub const MAXIMUM_DEPTH: usize = 16;
const MAXIMUM_NAME: usize = 80;
const IGNORED: [&str; 2] = ["desktop.ini", "thumbs.db"];

pub fn folder(data_root: &Path) -> PathBuf {
    data_root.join("mods").join(FOLDER)
}

pub fn settings_path(data_root: &Path) -> PathBuf {
    data_root.join("config").join(SETTINGS_NAME)
}

fn invalid_settings(path: &Path) -> ForgeError {
    ForgeError::new(ErrorKind::InvalidSettings, format!("Invalid custom RomFS settings: {}", path.display()))
}

/// Whether the user turned the folder on. A missing file is off; a
/// malformed file is an error, never a silent default.
pub fn enabled(data_root: &Path) -> Result<bool> {
    let path = settings_path(data_root);
    if !path.is_file() {
        return Ok(false);
    }
    let payload =
        load_json_object(&path).map_err(|error| ForgeError::new(ErrorKind::InvalidSettings, error.message))?;
    match (payload.get("format").and_then(Value::as_str), payload.get("enabled")) {
        (Some(SETTINGS_FORMAT), Some(Value::Bool(enabled))) => Ok(*enabled),
        _ => Err(invalid_settings(&path)),
    }
}

pub fn set_enabled(data_root: &Path, enabled: bool) -> Result<()> {
    atomic_write_json(&settings_path(data_root), &json!({"format": SETTINGS_FORMAT, "enabled": enabled}))
}

/// Why a file, or the whole folder, is not applied.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Issue {
    /// A symbolic link, junction or special file.
    Link(String),
    InvalidName(String),
    Empty(String),
    TooLarge { path: String, bytes: u64 },
    TooManyFiles(usize),
    TooLargeTotal(u64),
    TooDeep(String),
    Manifest(String),
    Unreadable { path: String, error: String },
    /// Flagged: the base RomFS has no such file, so nothing can replace it.
    NotInRomfs(String),
    /// Flagged: a catalogued mod replaces the same file; the custom one wins.
    ConflictsWithMod { path: String, title: String },
}

impl Issue {
    pub fn kind(&self) -> &'static str {
        match self {
            Self::Link(_) => "link",
            Self::InvalidName(_) => "invalid_name",
            Self::Empty(_) => "empty_file",
            Self::TooLarge { .. } => "file_too_large",
            Self::TooManyFiles(_) => "too_many_files",
            Self::TooLargeTotal(_) => "too_large_total",
            Self::TooDeep(_) => "too_deep",
            Self::Manifest(_) => "invalid_manifest",
            Self::Unreadable { .. } => "unreadable",
            Self::NotInRomfs(_) => "not_in_romfs",
            Self::ConflictsWithMod { .. } => "conflicts_with_mod",
        }
    }

    /// The English sentence reports and logs use.
    pub fn message(&self) -> String {
        match self {
            Self::Link(path) => format!("{path} is a link or a special file"),
            Self::InvalidName(path) => format!("{path} has a name the game's RomFS cannot have"),
            Self::Empty(path) => format!("{path} is empty"),
            Self::TooLarge { path, bytes } => {
                format!("{path} is {bytes} bytes (at most {MAXIMUM_FILE_BYTES} bytes per file)")
            }
            Self::TooManyFiles(count) => format!("the folder has {count} files (at most {MAXIMUM_FILES})"),
            Self::TooLargeTotal(bytes) => {
                format!("the folder holds {bytes} bytes (at most {MAXIMUM_TOTAL_BYTES} bytes)")
            }
            Self::TooDeep(path) => format!("{path} is more than {MAXIMUM_DEPTH} folders deep"),
            Self::Manifest(error) => format!("{MANIFEST_NAME} is invalid: {error}"),
            Self::Unreadable { path, error } => format!("{path} cannot be read: {error}"),
            Self::NotInRomfs(path) => format!("the game's RomFS has no {path}; it is left out"),
            Self::ConflictsWithMod { path, title } => format!("{path} is also replaced by {title}; this folder's file is used"),
        }
    }

    fn to_value(&self) -> Value {
        let path = match self {
            Self::Link(path)
            | Self::InvalidName(path)
            | Self::Empty(path)
            | Self::TooDeep(path)
            | Self::NotInRomfs(path)
            | Self::TooLarge { path, .. }
            | Self::Unreadable { path, .. }
            | Self::ConflictsWithMod { path, .. } => Value::from(path.as_str()),
            _ => Value::Null,
        };
        json!({"kind": self.kind(), "path": path, "message": self.message()})
    }
}

/// One file of the folder: its RomFS path, where it is and its size.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CustomFile {
    pub path: String,
    pub source: PathBuf,
    pub bytes: u64,
}

/// What the folder holds and whether it can be applied.
#[derive(Clone, Debug, Default)]
pub struct Scan {
    pub present: bool,
    pub name: Option<String>,
    pub files: Vec<CustomFile>,
    pub bytes: u64,
    /// Any of these keeps the whole folder out.
    pub rejected: Vec<Issue>,
    /// Files left out or overriding a mod; the rest still applies.
    pub flagged: Vec<Issue>,
}

impl Scan {
    /// Nothing rejected and at least one file to apply.
    pub fn applicable(&self) -> bool {
        self.rejected.is_empty() && !self.files.is_empty()
    }

    /// For status pages and logs: names and counts, never file contents.
    pub fn to_value(&self) -> Value {
        json!({
            "present": self.present,
            "name": self.name,
            "files": self.files.len(),
            "bytes": self.bytes,
            "paths": self.files.iter().map(|file| file.path.as_str()).collect::<Vec<_>>(),
            "applicable": self.applicable(),
            "rejected": self.rejected.iter().map(Issue::to_value).collect::<Vec<_>>(),
            "flagged": self.flagged.iter().map(Issue::to_value).collect::<Vec<_>>(),
        })
    }

    /// Flags the files `exists` says the base RomFS lacks and drops them.
    pub fn check_romfs(&mut self, exists: &dyn Fn(&str) -> bool) {
        let (kept, missing): (Vec<CustomFile>, Vec<CustomFile>) =
            std::mem::take(&mut self.files).into_iter().partition(|file| exists(&file.path));
        self.flagged.extend(missing.into_iter().map(|file| Issue::NotInRomfs(file.path)));
        self.files = kept;
    }

    /// Flags the files a catalogued mod (`path` -> mod title) replaces too.
    pub fn check_mods(&mut self, members: &BTreeMap<String, String>) {
        for file in &self.files {
            if let Some(title) = members.get(&file.path) {
                self.flagged.push(Issue::ConflictsWithMod { path: file.path.clone(), title: title.clone() });
            }
        }
    }
}

fn valid_component(name: &str) -> bool {
    !name.is_empty()
        && name != "."
        && name != ".."
        && name.chars().all(|c| (' '..='~').contains(&c) && c != '\\' && c != ':' && c != '/')
}

fn ignored(name: &str) -> bool {
    name.starts_with('.') || IGNORED.contains(&name.to_ascii_lowercase().as_str())
}

/// A hard link may name content outside the folder; only Unix can tell.
#[cfg(unix)]
fn shared_inode(metadata: &fs::Metadata) -> bool {
    use std::os::unix::fs::MetadataExt;
    metadata.nlink() > 1
}

#[cfg(not(unix))]
fn shared_inode(_: &fs::Metadata) -> bool {
    false
}

fn read_manifest(path: &Path) -> std::result::Result<Option<String>, String> {
    let document = load_json_object(path).map_err(|error| error.message)?;
    if document.get("format").and_then(Value::as_str) != Some(MANIFEST_FORMAT) {
        return Err(format!("format must be {MANIFEST_FORMAT}"));
    }
    match document.get("name") {
        None | Some(Value::Null) => Ok(None),
        Some(Value::String(name)) if !name.trim().is_empty() && name.chars().count() <= MAXIMUM_NAME => {
            Ok(Some(name.trim().to_string()))
        }
        Some(_) => Err(format!("name must be a text of 1 to {MAXIMUM_NAME} characters")),
    }
}

/// Reads the folder without following links. The result lists every
/// problem found; nothing is read beyond file metadata.
pub fn scan(data_root: &Path, cancel: &CancelToken) -> Result<Scan> {
    let base = folder(data_root);
    let mut scan = Scan::default();
    let Ok(metadata) = fs::symlink_metadata(&base) else { return Ok(scan) };
    scan.present = true;
    if !metadata.file_type().is_dir() {
        scan.rejected.push(Issue::Link(FOLDER.to_string()));
        return Ok(scan);
    }
    let manifest = base.join(MANIFEST_NAME);
    if fs::symlink_metadata(&manifest).is_ok() {
        match read_manifest(&manifest) {
            Ok(name) => scan.name = name,
            Err(error) => scan.rejected.push(Issue::Manifest(error)),
        }
    }
    let nested = base.join("romfs");
    let root = match fs::symlink_metadata(&nested) {
        Ok(metadata) if metadata.file_type().is_dir() => nested,
        _ => base.clone(),
    };
    let mut pending: Vec<(PathBuf, String, usize)> = vec![(root.clone(), String::new(), 0)];
    let mut counted = 0usize;
    while let Some((directory, prefix, depth)) = pending.pop() {
        cancel.check()?;
        let entries = match fs::read_dir(&directory) {
            Ok(entries) => entries,
            Err(error) => {
                let path = if prefix.is_empty() { FOLDER.to_string() } else { prefix.clone() };
                scan.rejected.push(Issue::Unreadable { path, error: error.to_string() });
                continue;
            }
        };
        let mut names: Vec<(String, PathBuf)> = Vec::new();
        for entry in entries {
            let entry = entry.map_err(|error| ForgeError::io(format!("Cannot read {}", directory.display()), error))?;
            names.push((entry.file_name().to_string_lossy().into_owned(), entry.path()));
        }
        names.sort();
        for (name, path) in names {
            let relative = if prefix.is_empty() { name.clone() } else { format!("{prefix}/{name}") };
            if depth == 0 && root == base && (name == MANIFEST_NAME || name == "romfs") {
                continue;
            }
            if ignored(&name) {
                continue;
            }
            if !valid_component(&name) || relative.len() > 255 {
                scan.rejected.push(Issue::InvalidName(relative));
                continue;
            }
            let metadata = match fs::symlink_metadata(&path) {
                Ok(metadata) => metadata,
                Err(error) => {
                    scan.rejected.push(Issue::Unreadable { path: relative, error: error.to_string() });
                    continue;
                }
            };
            let kind = metadata.file_type();
            if kind.is_dir() {
                if depth + 1 >= MAXIMUM_DEPTH {
                    scan.rejected.push(Issue::TooDeep(relative));
                } else {
                    pending.push((path, relative, depth + 1));
                }
                continue;
            }
            if !kind.is_file() || shared_inode(&metadata) {
                scan.rejected.push(Issue::Link(relative));
                continue;
            }
            counted += 1;
            let bytes = metadata.len();
            if bytes == 0 {
                scan.rejected.push(Issue::Empty(relative));
            } else if bytes > MAXIMUM_FILE_BYTES {
                scan.rejected.push(Issue::TooLarge { path: relative, bytes });
            } else {
                scan.bytes += bytes;
                scan.files.push(CustomFile { path: relative, source: path, bytes });
            }
        }
    }
    if counted > MAXIMUM_FILES {
        scan.rejected.push(Issue::TooManyFiles(counted));
    }
    if scan.bytes > MAXIMUM_TOTAL_BYTES {
        scan.rejected.push(Issue::TooLargeTotal(scan.bytes));
    }
    scan.files.sort_by(|left, right| left.path.cmp(&right.path));
    Ok(scan)
}

/// The catalogued members `path -> title` of the enabled mods whose files
/// are stored, for the conflict report.
pub fn mod_members(data_root: &Path, catalog: &crate::Catalog) -> Result<BTreeMap<String, String>> {
    let mut members = BTreeMap::new();
    for mod_id in crate::enabled_mods(data_root, catalog)? {
        if let Some(mod_) = catalog.get(&mod_id) {
            for member in &mod_.members {
                members.insert(member.path.clone(), mod_.title.clone());
            }
        }
    }
    Ok(members)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(path: &Path, bytes: &[u8]) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, bytes).unwrap();
    }

    #[test]
    fn off_by_default_and_settings_round_trip() {
        let data = tempfile::tempdir().unwrap();
        assert!(!enabled(data.path()).unwrap());
        set_enabled(data.path(), true).unwrap();
        assert!(enabled(data.path()).unwrap());
        set_enabled(data.path(), false).unwrap();
        assert!(!enabled(data.path()).unwrap());
        fs::write(settings_path(data.path()), r#"{"enabled": "yes"}"#).unwrap();
        assert_eq!(enabled(data.path()).unwrap_err().kind, ErrorKind::InvalidSettings);
    }

    #[test]
    fn a_valid_folder_lists_romfs_paths() {
        let data = tempfile::tempdir().unwrap();
        let base = folder(data.path());
        write(&base.join("actor/a.zar"), b"aa");
        write(&base.join("misc/menu.zar"), b"m");
        write(&base.join(".DS_Store"), b"x");
        write(&base.join("Thumbs.db"), b"x");
        write(&base.join(MANIFEST_NAME), br#"{"format": "triaevum_custom_romfs_v1", "name": " My test "}"#);
        let scan = scan(data.path(), &CancelToken::new()).unwrap();
        assert!(scan.applicable(), "{scan:?}");
        assert_eq!(scan.name.as_deref(), Some("My test"));
        assert_eq!(scan.files.iter().map(|file| file.path.as_str()).collect::<Vec<_>>(), ["actor/a.zar", "misc/menu.zar"]);
        assert_eq!(scan.bytes, 3);
        // A romfs/ folder is the root when present.
        let nested = tempfile::tempdir().unwrap();
        write(&folder(nested.path()).join("romfs/actor/b.zar"), b"b");
        let scan = super::scan(nested.path(), &CancelToken::new()).unwrap();
        assert_eq!(scan.files[0].path, "actor/b.zar");
    }

    #[test]
    fn rejections_keep_the_whole_folder_out() {
        let data = tempfile::tempdir().unwrap();
        let base = folder(data.path());
        write(&base.join("actor/empty.zar"), b"");
        write(&base.join("actor/ok.zar"), b"ok");
        write(&base.join("caf\u{e9}.bin"), b"x");
        write(&base.join(MANIFEST_NAME), br#"{"format": "other"}"#);
        let scan = scan(data.path(), &CancelToken::new()).unwrap();
        assert!(!scan.applicable());
        let kinds: Vec<&str> = scan.rejected.iter().map(Issue::kind).collect();
        for kind in ["empty_file", "invalid_manifest", "invalid_name"] {
            assert!(kinds.contains(&kind), "{kinds:?}");
        }
        // Too large for the overlay.
        let large = tempfile::tempdir().unwrap();
        let file = folder(large.path()).join("actor/big.zar");
        write(&file, b"x");
        fs::OpenOptions::new().write(true).open(&file).unwrap().set_len(MAXIMUM_FILE_BYTES + 1).unwrap();
        let scan = super::scan(large.path(), &CancelToken::new()).unwrap();
        assert_eq!(scan.rejected.iter().map(Issue::kind).collect::<Vec<_>>(), ["file_too_large"]);
        // Too deep.
        let deep = tempfile::tempdir().unwrap();
        let mut path = folder(deep.path());
        for level in 0..MAXIMUM_DEPTH {
            path = path.join(format!("d{level}"));
        }
        write(&path.join("f.bin"), b"x");
        let scan = super::scan(deep.path(), &CancelToken::new()).unwrap();
        assert_eq!(scan.rejected.iter().map(Issue::kind).collect::<Vec<_>>(), ["too_deep"]);
    }

    #[cfg(unix)]
    #[test]
    fn links_are_rejected() {
        let data = tempfile::tempdir().unwrap();
        let base = folder(data.path());
        write(&data.path().join("outside.bin"), b"secret");
        fs::create_dir_all(base.join("actor")).unwrap();
        std::os::unix::fs::symlink(data.path().join("outside.bin"), base.join("actor/a.zar")).unwrap();
        std::os::unix::fs::symlink(data.path(), base.join("misc")).unwrap();
        let scan = scan(data.path(), &CancelToken::new()).unwrap();
        assert_eq!(scan.rejected.iter().map(Issue::kind).collect::<Vec<_>>(), ["link", "link"]);
        assert!(scan.files.is_empty());
    }

    #[cfg(windows)]
    #[test]
    fn links_are_rejected() {
        let data = tempfile::tempdir().unwrap();
        let base = folder(data.path());
        write(&data.path().join("outside/secret.bin"), b"secret");
        fs::create_dir_all(&base).unwrap();
        // A junction needs no privilege on Windows.
        let status = std::process::Command::new("cmd")
            .args(["/C", "mklink", "/J"])
            .arg(base.join("misc"))
            .arg(data.path().join("outside"))
            .output()
            .unwrap();
        assert!(status.status.success(), "{status:?}");
        let scan = scan(data.path(), &CancelToken::new()).unwrap();
        assert_eq!(scan.rejected.iter().map(Issue::kind).collect::<Vec<_>>(), ["link"], "{scan:?}");
    }

    #[test]
    fn missing_files_and_mod_conflicts_are_flagged() {
        let data = tempfile::tempdir().unwrap();
        let base = folder(data.path());
        write(&base.join("actor/a.zar"), b"a");
        write(&base.join("actor/new.zar"), b"n");
        write(&base.join("misc/menu_link.zar"), b"m");
        let mut scan = scan(data.path(), &CancelToken::new()).unwrap();
        scan.check_romfs(&|path| path != "actor/new.zar");
        let members = BTreeMap::from([("misc/menu_link.zar".to_string(), "Vanilla+".to_string())]);
        scan.check_mods(&members);
        assert!(scan.applicable());
        assert_eq!(scan.files.len(), 2);
        assert_eq!(
            scan.flagged.iter().map(Issue::kind).collect::<Vec<_>>(),
            ["not_in_romfs", "conflicts_with_mod"]
        );
        let value = scan.to_value();
        assert_eq!(value["flagged"][0]["path"], "actor/new.zar");
        assert_eq!(value["files"], 2);
    }
}
