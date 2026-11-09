// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Public package inventories (UX plan WP-04B): the release manifest a
//! package carries (`docs/release-manifest.json` on Windows), read as the
//! list of files the publisher owns, and the replace/remove set between an
//! installed package and an update.
//!
//! The inventory decides ownership, not directory names: a file is replaced
//! or removed only when a manifest names it. Everything else in the
//! installation folder (`data/`, save states and logs beside the runtime,
//! private plugin generations, the placed runtime, unknown files) is kept.
//! New manifests may only own paths under the package's public layout
//! ([`OWNED_ROOTS`]); a member elsewhere is refused before anything is
//! written.
//!
//! The member checks mirror the public release audit
//! (`tools/triaevum_release/audit_release.py` and `release_policy.json`):
//! safe relative paths without traversal, drive, stream or device names,
//! no duplicates or case collisions, no private payloads.

use std::collections::{BTreeMap, HashSet};
use std::path::{Path, PathBuf};

use forge_base::hashing::is_sha256_hex;
use forge_base::{ErrorKind, ForgeError, Operation, Result, Subject};
use forge_catalog::paths::join_posix;
use serde_json::{Map, Value};

pub const MANIFEST_FORMAT: &str = "triaevum_public_release_manifest_v1";
/// Where a package keeps its manifest: the Windows root holds only
/// executables and README/LICENSE, so it is under docs/ there.
pub const MANIFEST_PATHS: [&str; 2] = ["release-manifest.json", "docs/release-manifest.json"];
pub const PRECOMPILED_MODEL: &str = "precompiled_title_rom_import_v1";
/// Bounds a staged package: files and expanded bytes.
pub const MAX_FILES: usize = 20_000;
pub const MAX_BYTES: u64 = 16 * 1024 * 1024 * 1024;
pub const MAX_MANIFEST_BYTES: u64 = 16 * 1024 * 1024;
/// The publisher signature's file name, in the manifest's folder
/// (`crate::release_signature`).
pub const SIGNATURE_NAME: &str = "release-signature.json";

/// The top-level names a package may own (UX plan WP-04B step 1).
pub const OWNED_ROOTS: [&str; 13] = [
    "runtime",
    "forge",
    "titles",
    "recipes",
    "resources",
    "docs",
    "LICENSES",
    "source",
    "config",
    "README.md",
    "LICENSE",
    "TriAevumForge.exe",
    "gamecontrollerdb.txt",
];

/// `release_policy.json` `forbidden_basenames` (kept equal by a test).
pub const FORBIDDEN_BASENAMES: [&str; 7] = [
    "AGENTS.md",
    "code.bin",
    "exheader.bin",
    "romfs.bin",
    "oot3d_native_game.exe",
    "oot3d_pica_default.o3ps",
    "atlas_overrides.o3tu",
];

/// `release_policy.json` `forbidden_extensions`.
pub const FORBIDDEN_EXTENSIONS: [&str; 38] = [
    ".3ds", ".7z", ".a", ".bcseq", ".bcwav", ".bin", ".bps", ".cci", ".cia", ".citra", ".cmb", ".csab", ".ctxb",
    ".cxi", ".dmp", ".gar", ".ips", ".mfpj", ".gz", ".lib", ".map", ".o", ".o3ps", ".o3ro", ".o3tu", ".obj", ".pdb", ".rar", ".sav",
    ".state", ".tam", ".tap", ".tar", ".xdelta", ".xdelta3", ".zar", ".zsi", ".zst",
];

/// `release_policy.json` `forbidden_extension_role_exceptions`.
pub const EXTENSION_ROLE_EXCEPTIONS: [(&str, &[&str]); 5] = [
    (".o3ps", &["portable_shader_corpus"]),
    (".lib", &["forge_link_library", "forge_tool_resource"]),
    (".ctxb", &["topscreen_singlescreen_asset", "restoration_original_member"]),
    (".zar", &["topscreen_singlescreen_asset"]),
    (".bps", &["restoration_patch"]),
];

/// `release_policy.json` `forbidden_path_components`.
pub const FORBIDDEN_COMPONENTS: [&str; 10] =
    ["captures", "evidence", "exefs", "game", "generated", "mod", "romfs", "savedata", "symbols", "whole_aot"];

/// One file a manifest owns.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FileRecord {
    pub path: String,
    pub role: String,
    pub bytes: u64,
    pub sha256: String,
}

/// A parsed release manifest and its exact bytes.
#[derive(Clone, Debug)]
pub struct ReleaseManifest {
    /// Where the manifest itself lives in its package.
    pub relative: String,
    pub bytes: Vec<u8>,
    pub release: Map<String, Value>,
    pub files: Vec<FileRecord>,
}

impl ReleaseManifest {
    pub fn version(&self) -> Option<&str> {
        self.release.get("version").and_then(Value::as_str)
    }

    pub fn target(&self) -> &str {
        self.release.get("target").and_then(Value::as_str).unwrap_or("x86_64-pc-windows-msvc")
    }

    pub fn file(&self, path: &str) -> Option<&FileRecord> {
        self.files.iter().find(|record| record.path == path)
    }

    pub fn total_bytes(&self) -> u64 {
        self.files.iter().map(|record| record.bytes).sum()
    }

    /// The signature's path beside the manifest.
    pub fn signature_relative(&self) -> String {
        signature_relative(&self.relative)
    }
}

/// `docs/release-signature.json` beside `docs/release-manifest.json`.
pub fn signature_relative(manifest_relative: &str) -> String {
    match manifest_relative.rsplit_once('/') {
        Some((folder, _)) => format!("{folder}/{SIGNATURE_NAME}"),
        None => SIGNATURE_NAME.to_string(),
    }
}

pub fn package_error(message: impl Into<String>) -> ForgeError {
    ForgeError::new(ErrorKind::ArtifactIntegrity, message).during(Operation::PackageVerify).about(Subject::Package)
}

/// A member path as a manifest or archive may name it: relative, `/`
/// separated, no empty, `.` or `..` component, no drive, stream (`:`) or
/// Windows-invalid character, no trailing dot or space, no device name.
pub fn check_member_path(path: &str) -> Result<()> {
    let unsafe_path = || package_error(format!("Unsafe path in the package: {path:?}"));
    if path.is_empty() || path.len() > 400 || path.starts_with('/') || path.contains('\\') {
        return Err(unsafe_path());
    }
    for part in path.split('/') {
        let invalid_character = part.chars().any(|character| character.is_control() || "<>:\"|?*".contains(character));
        if part.is_empty() || part == "." || part == ".." || invalid_character || part.ends_with(['.', ' ']) {
            return Err(unsafe_path());
        }
        let stem = part.split('.').next().unwrap_or_default().to_ascii_uppercase();
        let device = matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
            || (stem.len() == 4
                && (stem.starts_with("COM") || stem.starts_with("LPT"))
                && stem.as_bytes()[3].is_ascii_digit());
        if device {
            return Err(unsafe_path());
        }
    }
    Ok(())
}

/// The private-payload policy of the public audit, for one member.
pub fn check_public_member(path: &str, role: &str) -> Result<()> {
    let lowered = path.to_ascii_lowercase();
    let name = lowered.rsplit('/').next().unwrap_or_default();
    if FORBIDDEN_BASENAMES.iter().any(|forbidden| forbidden.eq_ignore_ascii_case(name)) {
        return Err(package_error(format!("The package contains a forbidden file: {path}")));
    }
    if let Some(dot) = name.rfind('.') {
        let extension = &name[dot..];
        let excepted =
            EXTENSION_ROLE_EXCEPTIONS.iter().any(|(candidate, roles)| *candidate == extension && roles.contains(&role));
        if FORBIDDEN_EXTENSIONS.contains(&extension) && !excepted {
            return Err(package_error(format!("The package contains a private file type: {path}")));
        }
    }
    if lowered.split('/').any(|part| FORBIDDEN_COMPONENTS.contains(&part)) {
        return Err(package_error(format!("The package contains a private folder: {path}")));
    }
    Ok(())
}

/// Parses manifest bytes found at `relative`.
pub fn parse_manifest(relative: &str, bytes: Vec<u8>) -> Result<ReleaseManifest> {
    let document = forge_base::legacy_json::load_object(&bytes)
        .map_err(|error| package_error(format!("The release manifest is not valid JSON: {error}")))?;
    if document.get("format").and_then(Value::as_str) != Some(MANIFEST_FORMAT) {
        return Err(package_error("The release manifest has an unsupported format"));
    }
    let release = match document.get("release") {
        Some(Value::Object(release)) => release.clone(),
        _ => return Err(package_error("The release manifest has no release metadata")),
    };
    let items = document
        .get("files")
        .and_then(Value::as_array)
        .ok_or_else(|| package_error("The release manifest has no file list"))?;
    if items.len() > MAX_FILES {
        return Err(package_error(format!("The release manifest lists more than {MAX_FILES} files")));
    }
    let mut files = Vec::with_capacity(items.len());
    let mut seen = HashSet::new();
    let mut total: u64 = 0;
    for item in items {
        let text = |key: &str| item.get(key).and_then(Value::as_str).unwrap_or_default().to_string();
        let (path, role, sha256) = (text("path"), text("role"), text("sha256").to_ascii_lowercase());
        check_member_path(&path)?;
        if MANIFEST_PATHS.contains(&path.as_str()) || path == signature_relative(relative) {
            return Err(package_error("The release manifest lists itself or its signature"));
        }
        if !seen.insert(path.to_ascii_lowercase()) {
            return Err(package_error(format!("The release manifest lists {path} twice (letter case ignored)")));
        }
        let bytes = item.get("bytes").and_then(Value::as_u64);
        let (Some(bytes), true, false) = (bytes, is_sha256_hex(&sha256), role.is_empty()) else {
            return Err(package_error(format!("The release manifest entry of {path} is malformed")));
        };
        total = total.saturating_add(bytes);
        files.push(FileRecord { path, role, bytes, sha256 });
    }
    if total > MAX_BYTES {
        return Err(package_error("The package expands to more than 16 GB"));
    }
    Ok(ReleaseManifest { relative: relative.to_string(), bytes, release, files })
}

/// The manifest of the package installed at `root`, when it has one.
pub fn installed_manifest(root: &Path) -> Result<Option<ReleaseManifest>> {
    for relative in MANIFEST_PATHS {
        let path = join_posix(root, relative);
        if path.is_file() {
            let bytes = std::fs::read(&path)
                .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
            return parse_manifest(relative, bytes).map(Some);
        }
    }
    Ok(None)
}

/// Checks a new package's manifest against the public layout and policy.
pub fn check_update_manifest(manifest: &ReleaseManifest, host_target: &str) -> Result<()> {
    if manifest.target() != host_target {
        return Err(ForgeError::new(
            ErrorKind::IncompatibleTarget,
            format!("This package is for {}, not for this system ({host_target})", manifest.target()),
        )
        .during(Operation::PackageVerify)
        .about(Subject::Package));
    }
    if manifest.release.get("distribution_model").and_then(Value::as_str) != Some(PRECOMPILED_MODEL) {
        return Err(package_error("This package is not a precompiled public release"));
    }
    for record in &manifest.files {
        let top = record.path.split('/').next().unwrap_or_default();
        if !OWNED_ROOTS.contains(&top) {
            return Err(package_error(format!("The package places a file outside its own folders: {}", record.path)));
        }
        check_public_member(&record.path, &record.role)?;
    }
    // The Forge title's own supported revisions (recipes/oot3d.json or recipes/mm3d.json):
    // an update package of the other title has no recipes this Forge can use.
    for required in ["recipes/precompiled-titles.json", forge_catalog::title::ACTIVE.packaged_recipes] {
        if manifest.file(required).is_none() {
            return Err(package_error(format!("The package has no {required}")));
        }
    }
    if !manifest.files.iter().any(|record| record.path.starts_with("titles/") && record.role == "precompiled_title") {
        return Err(package_error("The package has no precompiled title module (titles/ is missing)"));
    }
    Ok(())
}

/// What happens to one path.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    /// The new package has a different file (or the old one was changed locally).
    Replace,
    /// A new file the old package did not own.
    Add,
    /// The old package owned it and the new one does not.
    Remove,
}

impl Action {
    pub fn code(self) -> &'static str {
        match self {
            Self::Replace => "replace",
            Self::Add => "add",
            Self::Remove => "remove",
        }
    }
}

/// One change of the replace/remove set.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Change {
    pub path: String,
    pub action: Action,
    /// The old manifest's record of the path.
    pub old: Option<FileRecord>,
    pub new: Option<FileRecord>,
}

/// The replace/remove set between the installed and the new inventory.
#[derive(Clone, Debug, Default)]
pub struct ChangeSet {
    pub changes: Vec<Change>,
    /// Package files whose bytes on disk differ from the installed manifest
    /// (or a new member's path holding a file nobody owned): reported before
    /// they are replaced; their bytes are kept until the update commits.
    pub locally_modified: Vec<String>,
    pub unchanged: usize,
}

impl ChangeSet {
    /// Bytes the staged package needs.
    pub fn staged_bytes(&self) -> u64 {
        self.changes.iter().filter_map(|change| change.new.as_ref()).map(|record| record.bytes).sum()
    }

    /// Bytes the old files occupy when they must be copied aside (no hard links).
    pub fn rollback_bytes(&self) -> u64 {
        self.changes.iter().filter_map(|change| change.old.as_ref()).map(|record| record.bytes).sum()
    }
}

/// "Close TriAevum before updating": a package file another program holds,
/// with its holders when the system names them. Nothing is closed.
pub fn held_file(path: &Path) -> ForgeError {
    let holders = forge_base::file_holders::processes_using(path);
    let by = if holders.is_empty() { "another program".to_string() } else { holders.join(", ") };
    ForgeError::new(ErrorKind::Busy, format!("Close TriAevum before updating: {} is in use by {by}", path.display()))
        .with_path(path)
        .during(Operation::PackageReplace)
        .because(forge_base::Reason::VerifiedFileHolder)
}

/// The digest of a live package file; a file another program holds is
/// [`held_file`].
pub fn live_file_digest(path: &Path) -> Result<String> {
    if let Err(error) = std::fs::File::open(path) {
        if forge_base::fsutil::is_locked(&error) {
            return Err(held_file(path));
        }
    }
    crate::update_journal::digest(path)
}

/// Stats a live file: size and (only when the size matches) its digest.
fn live_digest(path: &Path, expected: &FileRecord) -> Result<Option<String>> {
    match std::fs::symlink_metadata(path) {
        Err(_) => Ok(None),
        Ok(metadata) if !metadata.is_file() => Err(package_error(format!("{} is not a regular file", path.display()))),
        Ok(metadata) if metadata.len() != expected.bytes => Ok(Some(String::new())),
        Ok(_) => Ok(Some(live_file_digest(path)?)),
    }
}

/// Derives the changes from the installed manifest (None: no verified
/// inventory, refused) to `new`, reading the live files under `root`.
pub fn derive_changes(root: &Path, old: &ReleaseManifest, new: &ReleaseManifest) -> Result<ChangeSet> {
    let old_files: BTreeMap<&str, &FileRecord> =
        old.files.iter().map(|record| (record.path.as_str(), record)).collect();
    let new_files: BTreeMap<&str, &FileRecord> =
        new.files.iter().map(|record| (record.path.as_str(), record)).collect();
    let platform = crate::layout::host_platform()?;
    let mut set = ChangeSet::default();
    for (path, record) in &new_files {
        let live = join_posix(root, path);
        match old_files.get(path) {
            Some(previous) => {
                let mut actual = live_digest(&live, previous)?;
                // Activation moved the packaged runtime to the root.
                if actual.is_none() && *path == platform.runtime {
                    let placed = root.join(platform.installed_runtime);
                    if live_digest(&placed, previous)?.as_deref() == Some(previous.sha256.as_str()) {
                        actual = Some(previous.sha256.clone());
                    }
                }
                let modified = actual.as_deref().is_some_and(|digest| digest != previous.sha256);
                if modified {
                    set.locally_modified.push(path.to_string());
                }
                if previous.sha256 == record.sha256 && previous.bytes == record.bytes && actual.is_some() && !modified {
                    set.unchanged += 1;
                    continue;
                }
                set.changes.push(Change {
                    path: path.to_string(),
                    action: Action::Replace,
                    old: Some((*previous).clone()),
                    new: Some((*record).clone()),
                });
            }
            None => {
                if std::fs::symlink_metadata(&live).is_ok() {
                    set.locally_modified.push(path.to_string());
                }
                set.changes.push(Change {
                    path: path.to_string(),
                    action: Action::Add,
                    old: None,
                    new: Some((*record).clone()),
                });
            }
        }
    }
    for (path, record) in &old_files {
        if new_files.contains_key(path) {
            continue;
        }
        let live = join_posix(root, path);
        let actual = live_digest(&live, record)?;
        if actual.is_none() {
            continue;
        }
        if actual.as_deref() != Some(record.sha256.as_str()) {
            set.locally_modified.push(path.to_string());
        }
        set.changes.push(Change {
            path: path.to_string(),
            action: Action::Remove,
            old: Some((*record).clone()),
            new: None,
        });
    }
    // The runtime first (the legacy entry point stops being startable), the
    // Forge window last (a crash before it leaves the old Forge in place).
    set.changes.sort_by_key(|change| {
        let rank = match change.path.as_str() {
            "runtime/TriAevum.exe" => 0,
            "TriAevumForge.exe" => 3,
            path if path.starts_with("runtime/") => 1,
            _ => 2,
        };
        (rank, change.path.clone())
    });
    set.locally_modified.sort();
    Ok(set)
}

/// The installation-relative paths an update never touches, whatever a
/// manifest says (the root runtime and module are activation outputs).
pub fn preserved(path: &str) -> bool {
    let top = path.split('/').next().unwrap_or_default();
    matches!(top, "data" | "savestates" | "logs" | "private-plugins" | ".forge-update")
        || top.starts_with(".update-")
        || top.starts_with(".forge-activation")
        || matches!(path, "triaevum_title_aot.dll")
        || [
            forge_catalog::platform::LEGACY_WINDOWS_RUNTIME,
            forge_catalog::platform::WINDOWS_PRODUCT_EXECUTABLE,
            forge_catalog::platform::LINUX_PRODUCT_EXECUTABLE,
            forge_catalog::platform::LEGACY_LAUNCH_PROFILE,
        ]
        .contains(&path)
        || forge_catalog::platform::launch_profile_name(crate::layout::WINDOWS.installed_runtime) == path
        || forge_catalog::platform::launch_profile_name(crate::layout::LINUX.installed_runtime) == path
}

/// The live path of a member under `root`.
pub fn live_path(root: &Path, path: &str) -> PathBuf {
    join_posix(root, path)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn unsafe_member_paths_are_refused() {
        for path in [
            "",
            "/abs",
            "C:/x",
            "a\\b",
            "../x",
            "a/../b",
            "a//b",
            "./a",
            "docs/file.txt:stream",
            "docs/CON",
            "docs/nul.txt",
            "docs/com1.log",
            "docs/trailing.",
            "docs/trailing ",
            "docs/a?b",
            "docs/\u{1}x",
        ] {
            assert!(check_member_path(path).is_err(), "{path:?}");
        }
        for path in ["runtime/TriAevum.exe", "docs/LICENSE_SCOPE.md", "forge/console.dll", "LICENSES/a.b.txt"] {
            check_member_path(path).unwrap();
        }
    }

    #[test]
    fn private_payloads_are_refused_with_the_audit_role_exceptions() {
        assert!(check_public_member("resources/code.bin", "title_neutral_resource").is_err());
        assert!(check_public_member("resources/x.tam", "title_neutral_resource").is_err());
        assert!(check_public_member("resources/savedata/x.json", "title_neutral_resource").is_err());
        assert!(check_public_member("forge/shader-corpus/portable.o3ps", "title_neutral_resource").is_err());
        check_public_member("forge/shader-corpus/portable.o3ps", "portable_shader_corpus").unwrap();
        check_public_member("resources/topscreen/singlescreen-3.0/custom_ui01.ctxb", "topscreen_singlescreen_asset")
            .unwrap();
    }

    /// The embedded lists are the release policy's.
    #[test]
    fn the_policy_lists_match_release_policy_json() {
        let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../triaevum_release/release_policy.json");
        let policy: Value = serde_json::from_slice(&std::fs::read(path).unwrap()).unwrap();
        let strings = |key: &str| -> Vec<String> {
            policy[key].as_array().unwrap().iter().map(|item| item.as_str().unwrap().to_string()).collect()
        };
        assert_eq!(strings("forbidden_basenames"), FORBIDDEN_BASENAMES);
        assert_eq!(strings("forbidden_extensions"), FORBIDDEN_EXTENSIONS);
        assert_eq!(strings("forbidden_path_components"), FORBIDDEN_COMPONENTS);
        let exceptions = policy["forbidden_extension_role_exceptions"].as_object().unwrap();
        assert_eq!(exceptions.len(), EXTENSION_ROLE_EXCEPTIONS.len());
        for (extension, roles) in EXTENSION_ROLE_EXCEPTIONS {
            let listed: Vec<&str> =
                exceptions[extension].as_array().unwrap().iter().map(|r| r.as_str().unwrap()).collect();
            assert_eq!(listed, roles);
        }
    }

    fn manifest(files: Value) -> Vec<u8> {
        json!({"format": MANIFEST_FORMAT, "release": {"version": "1", "target": "x86_64-pc-windows-msvc"}, "files": files})
            .to_string()
            .into_bytes()
    }

    #[test]
    fn malformed_manifests_are_refused() {
        let digest = "a".repeat(64);
        let entry = |path: &str| json!({"path": path, "role": "documentation", "bytes": 1, "sha256": digest});
        assert!(
            parse_manifest("docs/release-manifest.json", manifest(json!([entry("docs/a"), entry("DOCS/A")]))).is_err()
        );
        assert!(parse_manifest("docs/release-manifest.json", manifest(json!([entry("docs/release-signature.json")])))
            .is_err());
        assert!(parse_manifest(
            "docs/release-manifest.json",
            manifest(json!([{"path": "docs/a", "role": "", "bytes": 1, "sha256": digest}]))
        )
        .is_err());
        assert!(parse_manifest("docs/release-manifest.json", b"not json".to_vec()).is_err());
        let parsed = parse_manifest("docs/release-manifest.json", manifest(json!([entry("docs/a")]))).unwrap();
        assert_eq!(parsed.signature_relative(), "docs/release-signature.json");
        assert_eq!(signature_relative("release-manifest.json"), "release-signature.json");
        assert!(preserved("data/savedata/x") && preserved("TriAevum.exe") && preserved(".update-1-2/x"));
        let product = crate::layout::WINDOWS.installed_runtime;
        assert!(preserved(product) && preserved(&product.replace(".exe", ".launch.json")));
        assert!(!preserved("Moonfall_Curse.exe") || !preserved("Echoes_of_Time.exe"));
        assert!(!preserved("runtime/TriAevum.exe"));
        // Linux: the installed product binary and its profile, never the
        // packaged build output TriAevum or the other title's name.
        let linux = crate::layout::LINUX.installed_runtime;
        assert!(preserved(linux) && preserved(&format!("{linux}.launch.json")));
        assert!(!preserved("TriAevum") && !preserved("TriAevumForge"));
        assert!(!preserved("Moonfall_Curse") || !preserved("Echoes_of_Time"));
    }
}
