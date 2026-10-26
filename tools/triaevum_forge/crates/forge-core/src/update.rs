// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Windows "Update from ZIP" (UX plan WP-04B), the Forge side: plan, stage
//! and hand off. The helper side is [`crate::update_helper`] and the
//! journal [`crate::update_journal`].
//!
//! 1. **Plan** without touching the installation: the ZIP must hold one
//!    package folder whose release manifest is signed by the publisher
//!    ([`crate::release_signature`]) and names exactly the archive's
//!    members, for this system, with a precompiled catalogue that supports
//!    the installed game revision. The replace/remove set comes from the
//!    installed and the new manifest ([`crate::package_inventory`]); a
//!    downgrade needs `allow_downgrade` (owner decision 10).
//! 2. **Stage** into `A/.update-<version>-<pid>/package` on the same volume,
//!    every file hashed while it is written, then the catalogue, title
//!    module and runtime bindings checked by the catalogue code; the
//!    package's own update helper is copied beside it.
//! 3. **Hand off**: an exclusive proposal file, the helper started with an
//!    inherited handle of this process, its readiness acknowledgement read,
//!    then the caller exits so the helper can replace Forge itself.
//!
//! Updating from beta.1.2 (Python Forge, no Update button): the new
//! package's Forge runs from wherever it was extracted with
//! `--update-target <old folder>`; the source is then that package folder.

use std::cell::RefCell;
use std::cmp::Ordering;
use std::collections::{BTreeMap, HashSet};
use std::ffi::OsString;
use std::fs::File;
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::time::Duration;

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::hashing::{copy_hashing, hex, sha256_hex};
use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result, Subject};
use forge_catalog::paths::join_posix;
use forge_prepare::paths::resolve;
use serde_json::{json, Map, Value};

use crate::layout::HostOs;
use crate::package_inventory::{
    check_member_path, check_update_manifest, derive_changes, installed_manifest, package_error, parse_manifest,
    Action, Change, ChangeSet, FileRecord, ReleaseManifest, MANIFEST_PATHS, MAX_MANIFEST_BYTES,
};
use crate::publish::HudLayout;
use crate::refresh::compare_versions;
use crate::release_signature::{verify, Authenticity, Trust, MAX_SIGNATURE_BYTES};
use crate::state::{inspect, InspectRequest, InstallationState};
use crate::update_journal::{Faults, STAGED_PACKAGE};

pub use crate::update_journal::PROPOSAL;
pub const STAGING_PREFIX: &str = ".update-";
pub const PROPOSAL_FORMAT: &str = "triaevum_update_proposal_v1";
/// The package members the update needs from the new package.
pub const HELPER_MEMBER: &str = "forge/triaevum-update-helper.exe";
pub const ENGINE_MEMBER: &str = "forge/triaevum-forge.exe";
/// The helper's copy in the staging folder (it never replaces itself).
pub const HELPER_COPY: &str = "triaevum-update-helper.exe";
/// Free space kept beyond the staged files and their rollback copies.
pub const SPACE_MARGIN: u64 = 256 * 1024 * 1024;
/// How long the parent waits for the helper's readiness acknowledgement.
pub const ACK_TIMEOUT: Duration = Duration::from_secs(60);

/// Where the new package comes from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum UpdateSource {
    /// The downloaded release ZIP.
    Archive(PathBuf),
    /// An extracted package folder (`--update-target`: the running Forge's own).
    Folder(PathBuf),
}

#[derive(Clone, Debug)]
pub struct UpdateRequest {
    pub source: UpdateSource,
    /// The installation to update (the Windows package folder).
    pub package_root: PathBuf,
    pub data_root: Option<PathBuf>,
    pub hud_layout: HudLayout,
    pub allow_downgrade: bool,
    /// The request's half of the developer override (the variable is the other).
    pub allow_unsigned: bool,
    /// Start the updated Forge window when the helper finishes.
    pub relaunch: bool,
}

impl UpdateRequest {
    pub fn new(source: UpdateSource, package_root: impl Into<PathBuf>) -> Self {
        Self {
            source,
            package_root: package_root.into(),
            data_root: None,
            hud_layout: HudLayout::Keep,
            allow_downgrade: false,
            allow_unsigned: false,
            relaunch: true,
        }
    }
}

/// What planning depends on besides the request.
#[derive(Clone, Debug)]
pub struct PlanOptions {
    pub trust: Trust,
    /// Free bytes on the installation's volume; None asks the system.
    pub available_space: Option<u64>,
}

impl PlanOptions {
    pub fn publisher(request: &UpdateRequest) -> Self {
        Self { trust: Trust::publisher(request.allow_unsigned), available_space: None }
    }
}

fn update_error(kind: ErrorKind, message: impl Into<String>) -> ForgeError {
    ForgeError::new(kind, message).during(Operation::Update)
}

/// The new package's members, read from the ZIP or the folder.
enum Members {
    /// Package-relative path -> archive index; the archive opened once.
    Archive {
        path: PathBuf,
        indices: BTreeMap<String, usize>,
        archive: RefCell<zip::ZipArchive<File>>,
    },
    Folder(PathBuf),
}

impl std::fmt::Debug for Members {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Archive { path, indices, .. } => {
                write!(formatter, "Archive({}, {} members)", path.display(), indices.len())
            }
            Self::Folder(path) => write!(formatter, "Folder({})", path.display()),
        }
    }
}

impl Members {
    /// Runs `read` over one member's bytes.
    fn read_with<T>(&self, relative: &str, read: impl FnOnce(&mut dyn Read) -> Result<T>) -> Result<T> {
        match self {
            Self::Folder(root) => {
                let path = join_posix(root, relative);
                let mut file = File::open(&path)
                    .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
                read(&mut file)
            }
            Self::Archive { indices, archive, .. } => {
                let index =
                    *indices.get(relative).ok_or_else(|| package_error(format!("The package has no {relative}")))?;
                let mut archive = archive.borrow_mut();
                let mut member = archive
                    .by_index(index)
                    .map_err(|_| package_error(format!("{relative} cannot be read from the ZIP file")))?;
                read(&mut member)
            }
        }
    }

    /// A small member's bytes (manifest, signature, catalogue), bounded.
    fn read_small(&self, relative: &str, limit: u64) -> Result<Vec<u8>> {
        let bytes = self.read_with(relative, |reader| {
            let mut bytes = Vec::new();
            reader
                .take(limit + 1)
                .read_to_end(&mut bytes)
                .map_err(|error| ForgeError::io(format!("Cannot read {relative} from the package"), error))?;
            Ok(bytes)
        })?;
        if bytes.len() as u64 > limit {
            return Err(package_error(format!("{relative} is too large")));
        }
        Ok(bytes)
    }

    fn contains(&self, relative: &str) -> bool {
        match self {
            Self::Folder(root) => join_posix(root, relative).is_file(),
            Self::Archive { indices, .. } => indices.contains_key(relative),
        }
    }
}

/// Lists a ZIP's members under its single package folder: every refusal of
/// the plan's step 2 that the archive itself can show.
fn archive_members(path: &Path) -> Result<Members> {
    let file = File::open(path).map_err(|error| ForgeError::io(format!("Cannot open {}", path.display()), error))?;
    let mut archive = zip::ZipArchive::new(file).map_err(|_| package_error("The update is not a valid ZIP file"))?;
    if archive.len() > crate::package_inventory::MAX_FILES + 1000 {
        return Err(package_error("The ZIP file has too many members"));
    }
    let mut prefix: Option<String> = None;
    let mut indices = BTreeMap::new();
    let mut folded = HashSet::new();
    for index in 0..archive.len() {
        let member = archive.by_index_raw(index).map_err(|_| package_error("The ZIP file cannot be read"))?;
        let name = member.name().map_err(|_| package_error("A ZIP member name is not valid text"))?.to_string();
        if member.encrypted() {
            return Err(package_error(format!("The ZIP member {name} is encrypted")));
        }
        if member.is_symlink() {
            return Err(package_error(format!("The ZIP member {name} is a link")));
        }
        let directory = member.is_dir();
        let trimmed = name.strip_suffix('/').unwrap_or(&name);
        check_member_path(trimmed)?;
        let (top, rest) = match trimmed.split_once('/') {
            Some((top, rest)) => (top, Some(rest)),
            None => (trimmed, None),
        };
        match &prefix {
            None => prefix = Some(top.to_string()),
            Some(existing) if existing != top => {
                return Err(package_error("The ZIP file must hold exactly one TriAevum package folder"));
            }
            Some(_) => {}
        }
        if !folded.insert(trimmed.to_ascii_lowercase()) {
            return Err(package_error(format!("The ZIP file names {trimmed} twice (letter case ignored)")));
        }
        match (rest, directory) {
            (None, true) => {}
            (None, false) => return Err(package_error("The ZIP file has files outside its package folder")),
            (Some(_), true) => {}
            (Some(rest), false) => {
                indices.insert(rest.to_string(), index);
            }
        }
    }
    if prefix.is_none() {
        return Err(package_error("The ZIP file is empty"));
    }
    Ok(Members::Archive { path: path.to_path_buf(), indices, archive: RefCell::new(archive) })
}

/// The new package's manifest, its signature and how it was authenticated.
fn read_new_manifest(members: &Members, trust: &Trust) -> Result<(ReleaseManifest, Option<Vec<u8>>, Authenticity)> {
    let relative = MANIFEST_PATHS
        .iter()
        .rev()
        .find(|relative| members.contains(relative))
        .ok_or_else(|| package_error("The package has no release manifest; it is not a TriAevum release"))?;
    let bytes = members.read_small(relative, MAX_MANIFEST_BYTES)?;
    let signature_relative = crate::package_inventory::signature_relative(relative);
    let signature = members
        .contains(&signature_relative)
        .then(|| members.read_small(&signature_relative, MAX_SIGNATURE_BYTES))
        .transpose()?;
    // Authenticity before anything else is trusted.
    let authenticity = verify(&bytes, signature.as_deref(), trust)?;
    let manifest = parse_manifest(relative, bytes)?;
    Ok((manifest, signature, authenticity))
}

/// The archive's members must be exactly the manifest's files, plus the
/// manifest and its signature, with the declared sizes.
fn check_archive_members(members: &Members, manifest: &ReleaseManifest, signature: bool) -> Result<()> {
    let Members::Archive { indices, archive, .. } = members else { return Ok(()) };
    let mut expected: HashSet<String> = manifest.files.iter().map(|record| record.path.clone()).collect();
    expected.insert(manifest.relative.clone());
    if signature {
        expected.insert(manifest.signature_relative());
    }
    for name in indices.keys() {
        if !expected.contains(name) {
            return Err(package_error(format!("The ZIP file holds a file its manifest does not list: {name}")));
        }
    }
    let mut archive = archive.borrow_mut();
    for record in &manifest.files {
        let Some(index) = indices.get(&record.path) else {
            return Err(package_error(format!("The ZIP file is missing {}", record.path)));
        };
        let member = archive.by_index_raw(*index).map_err(|_| package_error("The ZIP file cannot be read"))?;
        if member.size() != record.bytes {
            return Err(package_error(format!("{} has another size than its manifest says", record.path)));
        }
    }
    Ok(())
}

/// The recipes the new catalogue supports.
fn catalogue_recipes(members: &Members) -> Result<Vec<String>> {
    let bytes = members.read_small("recipes/precompiled-titles.json", MAX_MANIFEST_BYTES)?;
    let catalog = forge_base::legacy_json::load_object(&bytes)
        .map_err(|_| package_error("The package's precompiled catalogue is not valid"))?;
    let titles = catalog
        .get("titles")
        .and_then(Value::as_array)
        .ok_or_else(|| package_error("The package's precompiled catalogue lists no titles"))?;
    Ok(titles.iter().filter_map(|title| title.get("recipe")?.as_str().map(str::to_string)).collect())
}

/// A planned update, nothing written yet.
#[derive(Debug)]
pub struct Plan {
    pub activation: PathBuf,
    pub data_root: PathBuf,
    pub request: UpdateRequest,
    pub old: ReleaseManifest,
    pub new: ReleaseManifest,
    pub signature: Option<Vec<u8>>,
    pub authenticity: Authenticity,
    pub changes: ChangeSet,
    /// The installation state before the update (`InstallationState::code`).
    pub installed_state: String,
    pub recipe: Option<String>,
    members: Members,
}

/// `Plan` or an answer that needs no update.
#[derive(Debug)]
pub enum Planned {
    Ready(Box<Plan>),
    /// `{"status": "unchanged", "reason": "current"}` or
    /// `{"status": "confirmation_required", "reason": "downgrade", ...}`.
    Done(Value),
}

/// The manifest and the signature are package files too.
fn metadata_changes(
    root: &Path,
    old: &ReleaseManifest,
    new: &ReleaseManifest,
    signature: Option<&[u8]>,
) -> Result<Vec<Change>> {
    let mut changes = Vec::new();
    let record = |path: &str, role: &str, bytes: &[u8]| FileRecord {
        path: path.to_string(),
        role: role.to_string(),
        bytes: bytes.len() as u64,
        sha256: sha256_hex(bytes),
    };
    let current = |path: &str| -> Result<Option<FileRecord>> {
        let live = join_posix(root, path);
        if !live.is_file() {
            return Ok(None);
        }
        let bytes =
            std::fs::read(&live).map_err(|error| ForgeError::io(format!("Cannot read {}", live.display()), error))?;
        Ok(Some(record(path, "metadata", &bytes)))
    };
    let new_manifest = record(&new.relative, "manifest", &new.bytes);
    let old_manifest = current(&new.relative)?;
    changes.push(Change {
        path: new.relative.clone(),
        action: if old_manifest.is_some() { Action::Replace } else { Action::Add },
        old: old_manifest,
        new: Some(new_manifest),
    });
    if old.relative != new.relative {
        if let Some(previous) = current(&old.relative)? {
            changes.push(Change { path: old.relative.clone(), action: Action::Remove, old: Some(previous), new: None });
        }
    }
    let signature_path = new.signature_relative();
    let old_signature = current(&signature_path)?;
    match signature {
        Some(bytes) => changes.push(Change {
            path: signature_path.clone(),
            action: if old_signature.is_some() { Action::Replace } else { Action::Add },
            old: old_signature,
            new: Some(record(&signature_path, "signature", bytes)),
        }),
        None => {
            if let Some(previous) = old_signature {
                changes.push(Change { path: signature_path, action: Action::Remove, old: Some(previous), new: None });
            }
        }
    }
    Ok(changes)
}

fn installed_recipe(state: &InstallationState) -> Option<String> {
    match state {
        InstallationState::Ready { recipe, .. }
        | InstallationState::UpdateAvailable { recipe, .. }
        | InstallationState::NeedsGameInput { recipe, .. } => Some(recipe.clone()),
        _ => None,
    }
}

/// Step 1: everything decided before a byte is written.
pub fn plan(request: &UpdateRequest, options: &PlanOptions) -> Result<Planned> {
    let os = HostOs::current();
    if os != HostOs::Windows {
        return Err(update_error(
            ErrorKind::IncompatibleTarget,
            "Update from a ZIP is for the portable Windows package; replace the AppImage or reinstall the APK instead",
        ));
    }
    let activation = crate::install::package_layout(&request.package_root)?.activation;
    let data_root = crate::install::data_root(request.data_root.as_deref(), &activation)?;
    if crate::update_gate::held(&activation, os) {
        return Err(crate::update_gate::in_progress(
            &crate::update_gate::gate_path(&activation, os),
            Operation::Update,
        ));
    }
    if crate::update_gate::pending(&activation) {
        return Err(crate::update_gate::interrupted(&crate::update_gate::journal_path(&activation), Operation::Update));
    }
    if crate::transaction::journal_path(&activation, os).exists() {
        return Err(update_error(
            ErrorKind::RecoveryRequired,
            "An activation was interrupted; repair the installation before updating",
        ));
    }
    let Some(old) = installed_manifest(&activation)? else {
        return Err(update_error(
            ErrorKind::IncompatibleTarget,
            "This folder has no release inventory (docs/release-manifest.json), so Forge cannot tell which files \
             belong to the package. Extract the new version to a new folder instead.",
        )
        .about(Subject::InstalledData));
    };
    let members = match &request.source {
        UpdateSource::Archive(path) => archive_members(path)?,
        UpdateSource::Folder(path) => {
            let folder = resolve(path)?;
            if folder == activation {
                return Err(update_error(
                    ErrorKind::InvalidInput,
                    "Choose a downloaded package, not the installation itself",
                ));
            }
            Members::Folder(folder)
        }
    };
    let (new, signature, authenticity) = read_new_manifest(&members, &options.trust)?;
    check_update_manifest(&new, crate::layout::host_platform()?.target)?;
    check_archive_members(&members, &new, signature.is_some())?;
    for member in [HELPER_MEMBER, ENGINE_MEMBER] {
        if new.file(member).is_none() {
            return Err(package_error(format!(
                "This package has no {member}, so it cannot update an installation; extract it to a new folder"
            )));
        }
    }
    let (installed_version, offered_version) = (old.version().map(str::to_string), new.version().map(str::to_string));
    if installed_version == offered_version && old.bytes == new.bytes {
        return Ok(Planned::Done(json!({"status": "unchanged", "reason": "current", "version": offered_version})));
    }
    if let (Some(installed), Some(offered)) = (&installed_version, &offered_version) {
        if compare_versions(installed, offered) == Some(Ordering::Greater) && !request.allow_downgrade {
            return Ok(Planned::Done(json!({
                "status": "confirmation_required",
                "reason": "downgrade",
                "installed_version": installed,
                "present_version": offered,
            })));
        }
    }
    let state = inspect(&InspectRequest { data_root: Some(data_root.clone()), ..InspectRequest::package(&activation) });
    match &state {
        InstallationState::NotInstalled
        | InstallationState::Ready { .. }
        | InstallationState::UpdateAvailable { .. }
        | InstallationState::NeedsGameInput { .. }
        | InstallationState::Incompatible { .. } => {}
        InstallationState::Preparing => {
            return Err(update_error(ErrorKind::Busy, "Another Forge is preparing this installation")
                .because(Reason::ForgeRunning));
        }
        _ => {
            return Err(update_error(ErrorKind::RecoveryRequired, "Repair the installation before updating it"));
        }
    }
    let recipe = installed_recipe(&state);
    if let Some(recipe) = &recipe {
        if !catalogue_recipes(&members)?.contains(recipe) {
            return Err(ForgeError::new(
                ErrorKind::ArtifactIntegrity,
                format!("The new package does not support your installed game revision ({recipe})"),
            )
            .during(Operation::PackageVerify)
            .about(Subject::Package)
            .because(Reason::MissingModule));
        }
    }
    let activity = crate::session::activity(&activation, os);
    if let Some(error) = activity.busy_error(&activation, os, Operation::Update) {
        return Err(error);
    }
    crate::layout::ensure_runtime_closed(&activation, &crate::layout::host_platform()?)?;
    let mut changes = derive_changes(&activation, &old, &new)?;
    let mut metadata = metadata_changes(&activation, &old, &new, signature.as_deref())?;
    changes.changes.retain(|change| !metadata.iter().any(|other| other.path == change.path));
    changes.changes.append(&mut metadata);
    let required = changes.staged_bytes().saturating_add(changes.rollback_bytes()).saturating_add(SPACE_MARGIN);
    let available = options.available_space.or_else(|| forge_native::system::available_space(&activation));
    if let Some(available) = available {
        if available < required {
            use crate::preflight::format_bytes;
            return Err(ForgeError::new(
                ErrorKind::StorageFull,
                format!(
                    "Not enough free space to update: the new files and the copy of the old ones kept until the \
                     update finishes need about {}, and {} are free. Free at least {} on that drive.",
                    format_bytes(required),
                    format_bytes(available),
                    format_bytes(required - available)
                ),
            )
            .during(Operation::Update)
            .because(Reason::InsufficientSpace));
        }
    }
    Ok(Planned::Ready(Box::new(Plan {
        activation,
        data_root,
        request: request.clone(),
        old,
        new,
        signature,
        authenticity,
        changes,
        installed_state: state.code().to_string(),
        recipe,
        members,
    })))
}

impl Plan {
    /// What the window previews before the user confirms.
    pub fn summary(&self) -> Value {
        let count = |action: Action| self.changes.changes.iter().filter(|change| change.action == action).count();
        json!({
            "status": "ready",
            "previous_version": self.old.version(),
            "version": self.new.version(),
            "authenticity": self.authenticity.to_value(),
            "installed_state": self.installed_state,
            "recipe": self.recipe,
            "replace": count(Action::Replace),
            "add": count(Action::Add),
            "remove": count(Action::Remove),
            "unchanged": self.changes.unchanged,
            "locally_modified": self.changes.locally_modified,
            "staged_bytes": self.changes.staged_bytes(),
            "hud_layout": self.request.hud_layout.code(),
        })
    }
}

/// A program the helper runs, with its arguments.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Program {
    pub program: PathBuf,
    pub arguments: Vec<OsString>,
}

impl Program {
    fn to_value(&self) -> Value {
        json!({
            "program": self.program.to_string_lossy(),
            "arguments": self.arguments.iter().map(|argument| argument.to_string_lossy().into_owned()).collect::<Vec<_>>(),
        })
    }

    pub fn parse(value: &Value) -> Option<Self> {
        let program = PathBuf::from(value.get("program")?.as_str()?);
        let arguments = value
            .get("arguments")?
            .as_array()?
            .iter()
            .map(|item| item.as_str().map(OsString::from))
            .collect::<Option<Vec<_>>>()?;
        Some(Self { program, arguments })
    }
}

/// What the helper runs: the new engine's `refresh` and `status`, and the
/// updated Forge window.
#[derive(Clone, Debug)]
pub struct Commands {
    pub rebind: Program,
    pub validate: Program,
    pub relaunch: Option<Program>,
}

impl Commands {
    /// The installed new package's own tools.
    pub fn for_plan(plan: &Plan) -> Self {
        let activation = &plan.activation;
        let engine = join_posix(activation, ENGINE_MEMBER);
        let mut common: Vec<OsString> = vec!["--package-root".into(), activation.clone().into()];
        if plan.request.data_root.is_some() {
            common.extend(["--data-root".into(), plan.data_root.clone().into()]);
        }
        let mut rebind: Vec<OsString> = vec!["refresh".into()];
        rebind.extend(common.iter().cloned());
        if plan.request.hud_layout == HudLayout::Standard {
            rebind.push("--standard-hud-layout".into());
        }
        if plan.request.allow_downgrade {
            rebind.push("--allow-downgrade".into());
        }
        let mut validate: Vec<OsString> = vec!["status".into()];
        validate.extend(common);
        let relaunch = plan.request.relaunch.then(|| Program {
            program: activation
                .join(crate::layout::host_platform().map(|platform| platform.forge).unwrap_or("TriAevumForge.exe")),
            arguments: vec!["--update-result".into(), crate::update_journal::result_path(activation).into()],
        });
        Self {
            rebind: Program { program: engine.clone(), arguments: rebind },
            validate: Program { program: engine, arguments: validate },
            relaunch,
        }
    }
}

/// A staged update, ready for the helper.
#[derive(Debug)]
pub struct Staged {
    pub staging: PathBuf,
    pub proposal: Value,
}

impl Staged {
    pub fn helper(&self) -> PathBuf {
        self.staging.join(HELPER_COPY)
    }
}

fn version_label(version: Option<&str>) -> String {
    let label: String = version
        .unwrap_or("unknown")
        .chars()
        .map(|character| if character.is_ascii_alphanumeric() || character == '.' { character } else { '-' })
        .take(40)
        .collect();
    label
}

/// A fresh id: process, time and randomness from the hash of both.
fn new_id() -> String {
    let nanos =
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|time| time.as_nanos()).unwrap_or(0);
    sha256_hex(format!("{}-{nanos}", std::process::id()).as_bytes())[..24].to_string()
}

/// Streams a member to `target`, checking its size and digest.
fn extract(
    members: &Members,
    relative: &str,
    record_bytes: u64,
    sha256: &str,
    target: &Path,
    cancel: &CancelToken,
) -> Result<()> {
    if let Some(parent) = target.parent() {
        std::fs::create_dir_all(parent)
            .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    }
    let mut output =
        File::create(target).map_err(|error| forge_base::fsutil::file_error("Cannot write", target, error))?;
    let (bytes, digest) = members.read_with(relative, |input| {
        copy_hashing(&mut input.take(record_bytes + 1), &mut output, record_bytes + 1, cancel)
    })?;
    output.sync_all().map_err(|error| ForgeError::io(format!("Cannot write {}", target.display()), error))?;
    if bytes != record_bytes || hex(&digest) != sha256 {
        return Err(package_error(format!("{relative} does not match the package's manifest (damaged download?)")));
    }
    Ok(())
}

/// Step 2: the staging folder with the verified new files, the helper copy
/// and the proposal's content (not yet written).
pub fn stage(plan: &Plan, commands: &Commands, cancel: &CancelToken) -> Result<Staged> {
    let staging =
        plan.activation.join(format!("{STAGING_PREFIX}{}-{}", version_label(plan.new.version()), std::process::id()));
    std::fs::create_dir(&staging).map_err(|error| {
        ForgeError::io(format!("Cannot create {}", staging.display()), error).during(Operation::Update)
    })?;
    match stage_into(plan, commands, &staging, cancel) {
        Ok(staged) => Ok(staged),
        Err(error) => {
            let _ = std::fs::remove_dir_all(&staging);
            Err(error)
        }
    }
}

fn stage_into(plan: &Plan, commands: &Commands, staging: &Path, cancel: &CancelToken) -> Result<Staged> {
    let package = staging.join(STAGED_PACKAGE);
    // Every new file, also unchanged ones: the catalogue check below reads
    // a complete package. Unchanged files are not moved by the helper.
    for record in &plan.new.files {
        cancel.check()?;
        extract(
            &plan.members,
            &record.path,
            record.bytes,
            &record.sha256,
            &join_posix(&package, &record.path),
            cancel,
        )?;
    }
    forge_base::fsutil::atomic_write_bytes(&join_posix(&package, &plan.new.relative), &plan.new.bytes)?;
    if let Some(signature) = &plan.signature {
        forge_base::fsutil::atomic_write_bytes(&join_posix(&package, &plan.new.signature_relative()), signature)?;
    }
    // The catalogue, title module, native module and runtime bindings.
    let catalog =
        forge_catalog::precompiled::load_catalog(&package).map_err(|error| error.during(Operation::PackageVerify))?;
    if let Some(recipe) = &plan.recipe {
        let recipe = forge_catalog::recipes::load_recipe(&crate::install::recipes_path(&package), recipe)?;
        forge_catalog::precompiled::select_title(
            &package,
            &recipe,
            Some(&catalog),
            &forge_catalog::host_platform()?,
            cancel,
        )
        .map_err(|error| error.during(Operation::PackageVerify))?;
    }
    let helper = staging.join(HELPER_COPY);
    std::fs::copy(join_posix(&package, HELPER_MEMBER), &helper)
        .map_err(|error| ForgeError::io("Cannot copy the update helper", error))?;
    let changes: Vec<Value> = plan
        .changes
        .changes
        .iter()
        .map(|change| {
            json!({
                "path": change.path,
                "action": change.action.code(),
                "old_sha256": change.old.as_ref().map(|record| record.sha256.clone()),
                "old_bytes": change.old.as_ref().map(|record| record.bytes),
                "new_sha256": change.new.as_ref().map(|record| record.sha256.clone()),
                "new_bytes": change.new.as_ref().map(|record| record.bytes),
            })
        })
        .collect();
    let proposal = json!({
        "format": PROPOSAL_FORMAT,
        "id": new_id(),
        "activation": plan.activation.to_string_lossy(),
        "data_root": plan.data_root.to_string_lossy(),
        "staging": staging.to_string_lossy(),
        "previous_version": plan.old.version(),
        "version": plan.new.version(),
        "old_manifest_sha256": sha256_hex(&plan.old.bytes),
        "new_manifest_sha256": sha256_hex(&plan.new.bytes),
        "authenticity": plan.authenticity.to_value(),
        "installed_state": plan.installed_state,
        "recipe": plan.recipe,
        "hud_layout": plan.request.hud_layout.code(),
        "allow_downgrade": plan.request.allow_downgrade,
        "locally_modified": plan.changes.locally_modified,
        "changes": changes,
        "rebind": commands.rebind.to_value(),
        "validate": commands.validate.to_value(),
        "relaunch": commands.relaunch.as_ref().map(Program::to_value),
    });
    Ok(Staged { staging: staging.to_path_buf(), proposal })
}

/// Step 3: writes the exclusive proposal, starts `helper` (the staged copy
/// in production) with an inherited handle of this process and waits for
/// its acknowledgement. The caller must exit promptly afterwards: the
/// helper waits for this process to end before it changes anything.
pub fn hand_off(staged: &Staged, helper: &Program, faults: &Faults) -> Result<Value> {
    let path = staged.staging.join(PROPOSAL);
    let mut file = std::fs::OpenOptions::new().write(true).create_new(true).open(&path).map_err(|error| {
        ForgeError::io(format!("Cannot create {}", path.display()), error).during(Operation::Update)
    })?;
    file.write_all(&forge_base::legacy_json::pretty_file_bytes(&staged.proposal))
        .and_then(|()| file.sync_all())
        .map_err(|error| ForgeError::io(format!("Cannot write {}", path.display()), error))?;
    drop(file);
    faults.boundary("proposal written");
    let id = staged.proposal["id"].as_str().unwrap_or_default().to_string();
    let mut child = crate::update_helper::spawn_helper(helper, &path)?;
    faults.boundary("helper started");
    let stdout = child.stdout.take().ok_or_else(|| ForgeError::internal("The update helper has no output"))?;
    let (sender, receiver) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        for line in BufReader::new(stdout).lines() {
            let Ok(line) = line else { break };
            // The acknowledgement may follow other output on its line.
            if let Some((_, acknowledged)) = line.rsplit_once("ready ") {
                let _ = sender.send(acknowledged.trim().to_string());
                break;
            }
        }
    });
    match receiver.recv_timeout(ACK_TIMEOUT) {
        Ok(acknowledged) if acknowledged == id => {}
        _ => {
            // It changes nothing before this process exits: stopping it is safe.
            let _ = child.kill();
            let _ = child.wait();
            return Err(update_error(ErrorKind::Internal, "The update helper did not start; nothing was changed"));
        }
    }
    faults.boundary("helper acknowledged");
    Ok(json!({
        "status": "handed_off",
        "helper_pid": child.id(),
        "proposal": path.to_string_lossy(),
        "version": staged.proposal["version"],
    }))
}

/// Removes staging folders no pending journal refers to (a Forge that
/// stopped before or during the handoff, a finished update's helper copy).
pub fn cleanup_stale(activation: &Path) {
    let referenced = crate::update_journal::Journal::load(activation).ok().flatten().map(|journal| journal.staging());
    let Ok(entries) = std::fs::read_dir(activation) else { return };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        let path = entry.path();
        if name.starts_with(STAGING_PREFIX) && path.is_dir() && Some(&path) != referenced.as_ref() {
            let _ = std::fs::remove_dir_all(&path);
        }
    }
}

/// The result a finished helper left (`A/.forge-update/result.json`).
pub fn read_result(path: &Path) -> Option<Map<String, Value>> {
    let result = load_json_object(path).ok()?;
    (result.get("format").and_then(Value::as_str) == Some(crate::update_journal::RESULT_FORMAT)).then_some(result)
}

/// Reads and removes the result of `activation`'s last update.
pub fn take_result(activation: &Path) -> Option<Map<String, Value>> {
    let path = crate::update_journal::result_path(activation);
    let result = read_result(&path)?;
    let _ = std::fs::remove_file(&path);
    Some(result)
}

/// Plans, stages and hands off with the publisher's trust (the `update`
/// job and the CLI). The caller exits when the result is `handed_off`.
pub fn update(request: &UpdateRequest, report: crate::install::Report<'_>, cancel: &CancelToken) -> Result<Value> {
    report("preflight", "Checking the update package...");
    let plan = match plan(request, &PlanOptions::publisher(request))? {
        Planned::Done(answer) => return Ok(answer),
        Planned::Ready(plan) => plan,
    };
    report("verify", "Verifying the new files...");
    let commands = Commands::for_plan(&plan);
    let staged = stage(&plan, &commands, cancel)?;
    report("activate", "Handing over to the updater...");
    let helper = Program { program: staged.helper(), arguments: Vec::new() };
    let mut result = hand_off(&staged, &helper, &Faults::none())?;
    result["authenticity"] = plan.authenticity.to_value();
    result["previous_version"] = json!(plan.old.version());
    Ok(result)
}

/// Writes `value` as the result of `activation`'s update (the helper).
pub fn write_result(activation: &Path, value: &Value) -> Result<()> {
    atomic_write_json(&crate::update_journal::result_path(activation), value)
}

/// The update jobs (`jobs::start`): `update_plan` (preview, writes
/// nothing), `update` (plan, stage, hand off; the frontend exits on
/// `handed_off`) and `update_recover` (settles an interrupted update).
///
/// Fields: `source: {"path"}` (a ZIP, or a package folder), `package_root?`
/// (the installation; default this executable's), `data_root?`,
/// `hud_layout?`, `allow_downgrade?`, `allow_unsigned?` (with
/// `TRIAEVUM_FORGE_ALLOW_UNSIGNED_UPDATE`), `relaunch?` (default true).
pub fn start_job(name: &str, request: &Map<String, Value>) -> Result<crate::abi::Job> {
    let path = |key: &str| request.get(key).and_then(Value::as_str).filter(|text| !text.is_empty()).map(PathBuf::from);
    let flag = |key: &str, default: bool| match request.get(key) {
        None | Some(Value::Null) => Ok(default),
        Some(Value::Bool(flag)) => Ok(*flag),
        Some(_) => Err(ForgeError::invalid(format!("{key} must be a boolean"))),
    };
    let package_root = match path("package_root") {
        Some(root) => root,
        None => crate::layout::executable_package_root()?,
    };
    if name == "update_recover" {
        return Ok(crate::abi::Job::spawn(move |_, _| {
            let activation = crate::install::package_layout(&package_root)?.activation;
            let recovered = crate::update_journal::recover(&activation)?;
            cleanup_stale(&activation);
            Ok(json!({"recovered": recovered}))
        }));
    }
    let source = request
        .get("source")
        .and_then(|source| source.get("path"))
        .and_then(Value::as_str)
        .map(PathBuf::from)
        .ok_or_else(|| ForgeError::invalid("An update needs source.path (the downloaded ZIP or a package folder)"))?;
    let source = if source.is_dir() { UpdateSource::Folder(source) } else { UpdateSource::Archive(source) };
    let update_request = UpdateRequest {
        source,
        package_root,
        data_root: path("data_root"),
        hud_layout: HudLayout::parse(request.get("hud_layout"))?,
        allow_downgrade: flag("allow_downgrade", false)?,
        allow_unsigned: flag("allow_unsigned", false)?,
        relaunch: flag("relaunch", true)?,
    };
    let log = crate::install::data_root(update_request.data_root.as_deref(), &update_request.package_root)
        .ok()
        .map(|data_root| crate::log::ForgeLog::new(&data_root, name));
    if name == "update_plan" {
        return Ok(crate::abi::Job::spawn_logged(log, move |_, _| {
            Ok(match plan(&update_request, &PlanOptions::publisher(&update_request))? {
                Planned::Done(answer) => answer,
                Planned::Ready(plan) => plan.summary(),
            })
        }));
    }
    Ok(crate::abi::Job::spawn_logged(log, move |sink, cancel| {
        let mut report = |stage: &str, message: &str| sink.stage(stage, message);
        update(&update_request, &mut report, cancel)
    }))
}
