// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! "What state is the installation in": one read-only answer for the
//! desktop window, the CLI (`status`) and the Android installer (through
//! `TriAevumForgeInspect`).
//!
//! [`inspect`] never writes: it does not place the runtime, rewrite
//! receipts, take the activation lock or create folders. It reads the
//! package (catalog, runtime), the activation journal and lock, the active
//! title with its receipts and settings, and the kept game sources. It
//! hashes the runtime and the title plugin; the kept sources are checked by
//! presence and size unless `verify_sources` asks for their hashes (the
//! RomFS is hundreds of megabytes).

use std::path::{Path, PathBuf};

use forge_base::fsutil::load_json_object;
use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result, Subject};
use forge_catalog::paths::join_posix;
use forge_catalog::platform::{catalog_platform, installed_runtime_path};
use forge_catalog::precompiled::{artifact_path, load_catalog, set_native_library_directory};
use forge_catalog::ReleasePlatform;
use forge_prepare::content::{load_prepared_content, PreparedContent};
use forge_prepare::paths::{expanduser, resolve, resolve_reference};
use serde_json::{json, Map, Value};

use crate::explain::{explain, Explanation};
use crate::install::{recipes_path, AndroidTarget, InstallTarget, ANDROID_TITLE_FILE};
use crate::layout::{self, installation_state, HostOs};
use crate::transaction::journal_path;

pub const FORMAT: &str = "triaevum_installation_state_v1";
pub const REQUEST_FORMAT: &str = "triaevum_forge_inspect_v1";
const ACTIVE_TITLE_FORMAT: &str = "triaevum_active_title_v1";
const ACTIVE_TITLE: &str = "active-title.json";
const SOURCE_KINDS: [&str; 3] = ["code", "exheader", "romfs"];

pub struct InspectRequest {
    pub package_root: PathBuf,
    /// The private data folder; the package's (or the Android profile's) by default.
    pub data_root: Option<PathBuf>,
    pub target: InstallTarget,
    /// Rehash the kept game sources instead of checking presence and size.
    pub verify_sources: bool,
}

impl InspectRequest {
    pub fn package(package_root: impl Into<PathBuf>) -> Self {
        Self {
            package_root: package_root.into(),
            data_root: None,
            target: InstallTarget::Package,
            verify_sources: false,
        }
    }

    /// `{"format": "triaevum_forge_inspect_v1", "package_root"?, "data_root"?,
    /// "android"?: {"native_library_dir", "profile_root"}, "verify_sources"?}`.
    pub fn parse(request: &Map<String, Value>) -> Result<Self> {
        if request.get("format").and_then(Value::as_str) != Some(REQUEST_FORMAT) {
            return Err(ForgeError::invalid(format!("Inspection request format must be {REQUEST_FORMAT}")));
        }
        let path =
            |key: &str| request.get(key).and_then(Value::as_str).filter(|text| !text.is_empty()).map(PathBuf::from);
        let package_root = match path("package_root") {
            Some(root) => root,
            None => layout::executable_package_root()?,
        };
        let target = match request.get("android") {
            None | Some(Value::Null) => InstallTarget::Package,
            Some(Value::Object(android)) => {
                let field = |key: &str| {
                    android
                        .get(key)
                        .and_then(Value::as_str)
                        .filter(|text| !text.is_empty())
                        .map(PathBuf::from)
                        .ok_or_else(|| ForgeError::invalid(format!("android.{key} must be a path")))
                };
                InstallTarget::Android(AndroidTarget {
                    native_library_dir: field("native_library_dir")?,
                    profile_root: field("profile_root")?,
                })
            }
            Some(_) => return Err(ForgeError::invalid("android must be an object")),
        };
        let verify_sources = match request.get("verify_sources") {
            None | Some(Value::Null) => false,
            Some(Value::Bool(flag)) => *flag,
            Some(_) => return Err(ForgeError::invalid("verify_sources must be a boolean")),
        };
        Ok(Self { package_root, data_root: path("data_root"), target, verify_sources })
    }
}

/// A package as an installation sees it: its runtime digest and version.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PackageIdentity {
    pub runtime_sha256: String,
    pub version: Option<String>,
}

impl PackageIdentity {
    fn to_value(&self) -> Value {
        json!({"runtime_sha256": self.runtime_sha256, "version": self.version})
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RepairReason {
    /// An activation journal is present.
    InterruptedActivation,
    RuntimeMissing,
    /// A runtime, plugin, profile or asset digest differs from its receipt.
    ReceiptMismatch,
    /// `file` is relative to the data folder (or the Android profile folder).
    ConfigUnreadable {
        file: String,
    },
}

impl RepairReason {
    fn to_value(&self) -> Value {
        match self {
            Self::InterruptedActivation => json!({"kind": "interrupted_activation"}),
            Self::RuntimeMissing => json!({"kind": "runtime_missing"}),
            Self::ReceiptMismatch => json!({"kind": "receipt_mismatch"}),
            Self::ConfigUnreadable { file } => json!({"kind": "config_unreadable", "file": file}),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IncompatibleReason {
    /// The package (or the installed runtime) targets another platform.
    TargetMismatch,
    /// The precompiled catalog or the supported revisions are missing or invalid.
    MissingCatalogue,
    /// The package has no precompiled module for the installed revision.
    MissingTitleModule,
    MissingRuntime,
    /// The package's runtime or module differs from its own catalog.
    PackageIntegrity,
}

impl IncompatibleReason {
    pub fn code(self) -> &'static str {
        match self {
            Self::TargetMismatch => "target_mismatch",
            Self::MissingCatalogue => "missing_catalogue",
            Self::MissingTitleModule => "missing_title_module",
            Self::MissingRuntime => "missing_runtime",
            Self::PackageIntegrity => "package_integrity",
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum InstallationState {
    NotInstalled,
    /// Another Forge holds the activation lock.
    Preparing,
    Ready {
        recipe: String,
        package_version: Option<String>,
        language: Option<String>,
    },
    /// A different verified package is present than the one installed (a
    /// replaced AppImage or APK, a ZIP unpacked over the installation).
    UpdateAvailable {
        recipe: String,
        installed_package_identity: PackageIdentity,
        present_package_identity: PackageIdentity,
    },
    RepairNeeded {
        reason: RepairReason,
        details: String,
    },
    /// The installation is coherent but the kept game sources are missing
    /// or changed: the ROM is needed again.
    NeedsGameInput {
        recipe: String,
        details: String,
    },
    Incompatible {
        reason: IncompatibleReason,
        details: String,
    },
    /// Inspection itself failed.
    Failed {
        explanation: Explanation,
    },
}

impl InstallationState {
    pub fn code(&self) -> &'static str {
        match self {
            Self::NotInstalled => "not_installed",
            Self::Preparing => "preparing",
            Self::Ready { .. } => "ready",
            Self::UpdateAvailable { .. } => "update_available",
            Self::RepairNeeded { .. } => "repair_needed",
            Self::NeedsGameInput { .. } => "needs_game_input",
            Self::Incompatible { .. } => "incompatible",
            Self::Failed { .. } => "failed",
        }
    }

    /// The explanation of a state that needs the user (repair, game input,
    /// incompatible, failed); None for the others.
    pub fn explanation(&self) -> Option<Explanation> {
        let error = match self {
            Self::RepairNeeded { reason: RepairReason::ConfigUnreadable { .. }, details } => {
                ForgeError::new(ErrorKind::InvalidSettings, details.clone())
                    .during(Operation::SettingsLoad)
                    .because(Reason::MalformedSettings)
            }
            Self::RepairNeeded { details, .. } => {
                ForgeError::new(ErrorKind::RecoveryRequired, details.clone()).during(Operation::Inspect)
            }
            Self::NeedsGameInput { details, .. } => ForgeError::new(ErrorKind::InvalidInput, details.clone())
                .during(Operation::Inspect)
                .because(Reason::MissingRetainedSource),
            Self::Incompatible { reason: IncompatibleReason::TargetMismatch, details } => {
                ForgeError::new(ErrorKind::IncompatibleTarget, details.clone()).during(Operation::PackageVerify)
            }
            Self::Incompatible { reason, details } => ForgeError::new(ErrorKind::ArtifactIntegrity, details.clone())
                .during(Operation::PackageVerify)
                .about(Subject::Package)
                .because(match reason {
                    IncompatibleReason::MissingCatalogue => Reason::MissingCatalogue,
                    IncompatibleReason::MissingRuntime => Reason::MissingRuntime,
                    _ => Reason::MissingModule,
                }),
            Self::Failed { explanation } => return Some(explanation.clone()),
            _ => return None,
        };
        Some(explain(&error))
    }

    /// Tagged JSON: `{"format", "state", ...fields, "explanation"?}`.
    pub fn to_value(&self) -> Value {
        let redact = crate::redact::redact;
        let mut value = json!({"format": FORMAT, "state": self.code()});
        match self {
            Self::NotInstalled | Self::Preparing | Self::Failed { .. } => {}
            Self::Ready { recipe, package_version, language } => {
                value["recipe"] = json!(recipe);
                value["package_version"] = json!(package_version);
                value["language"] = json!(language);
            }
            Self::UpdateAvailable { recipe, installed_package_identity, present_package_identity } => {
                value["recipe"] = json!(recipe);
                value["installed_package_identity"] = installed_package_identity.to_value();
                value["present_package_identity"] = present_package_identity.to_value();
            }
            Self::RepairNeeded { reason, details } => {
                value["reason"] = reason.to_value();
                value["details"] = json!(redact(details));
            }
            Self::NeedsGameInput { recipe, details } => {
                value["recipe"] = json!(recipe);
                value["details"] = json!(redact(details));
            }
            Self::Incompatible { reason, details } => {
                value["reason"] = json!({"kind": reason.code()});
                value["details"] = json!(redact(details));
            }
        }
        if let Some(explanation) = self.explanation() {
            value["explanation"] = explanation.to_value();
        }
        value
    }
}

/// `release.version` of the package's release manifest, when it has one.
pub fn package_version(package: &Path) -> Option<String> {
    ["release-manifest.json", "docs/release-manifest.json"].iter().find_map(|relative| {
        let manifest = load_json_object(&join_posix(package, relative)).ok()?;
        manifest.get("release")?.get("version")?.as_str().map(str::to_string)
    })
}

/// The state of the installation `request` names. Never fails: a failure
/// of the inspection itself is [`InstallationState::Failed`].
pub fn inspect(request: &InspectRequest) -> InstallationState {
    match Context::new(request).and_then(|context| context.inspect()) {
        Ok(state) => state,
        Err(error) => InstallationState::Failed { explanation: explain(&error.during(Operation::Inspect)) },
    }
}

/// [`inspect`] for a JSON request (the ABI and JNI entry), as JSON bytes.
pub fn inspect_bytes(request: &[u8]) -> Vec<u8> {
    let parsed = crate::legacy_json::load_object(request)
        .map_err(|error| ForgeError::invalid(format!("Invalid inspection request: {error}")))
        .and_then(|request| InspectRequest::parse(&request));
    let state = match parsed {
        Ok(request) => inspect(&request),
        Err(error) => InstallationState::Failed { explanation: explain(&error.during(Operation::RequestValidation)) },
    };
    crate::legacy_json::compact_line_bytes(&state.to_value())
}

fn repair(reason: RepairReason, details: impl Into<String>) -> InstallationState {
    InstallationState::RepairNeeded { reason, details: details.into() }
}

fn incompatible(reason: IncompatibleReason, details: impl Into<String>) -> InstallationState {
    InstallationState::Incompatible { reason, details: details.into() }
}

fn file_sha256(path: &Path) -> Result<String> {
    Ok(hex(&sha256_file(path, &CancelToken::new())?.1))
}

fn text<'a>(map: &'a Map<String, Value>, key: &str) -> &'a str {
    map.get(key).and_then(Value::as_str).unwrap_or_default()
}

/// What the runtime check concluded.
enum Verdict {
    Coherent { update: bool },
    State(InstallationState),
}

struct Context<'a> {
    package: PathBuf,
    data_root: PathBuf,
    /// Where the activation journal and lock live (the runtime plugin's folder).
    installation: PathBuf,
    platform: ReleasePlatform,
    android: Option<&'a AndroidTarget>,
    verify_sources: bool,
}

impl<'a> Context<'a> {
    fn new(request: &'a InspectRequest) -> Result<Self> {
        let package = resolve(&expanduser(&request.package_root))?;
        let explicit = |default: PathBuf| -> Result<PathBuf> {
            match &request.data_root {
                Some(path) => resolve(&expanduser(path)),
                None => resolve(&default),
            }
        };
        match &request.target {
            InstallTarget::Package => {
                let layout = layout::for_package(&package, &layout::process_env, HostOs::current())?;
                Ok(Self {
                    data_root: explicit(layout.data())?,
                    installation: layout.activation,
                    package,
                    platform: forge_catalog::host_platform()?,
                    android: None,
                    verify_sources: request.verify_sources,
                })
            }
            InstallTarget::Android(android) => {
                set_native_library_directory(&android.native_library_dir)?;
                Ok(Self {
                    data_root: explicit(android.data_root())?,
                    installation: resolve(&android.profile_root)?,
                    package,
                    platform: forge_catalog::ANDROID,
                    android: Some(android),
                    verify_sources: request.verify_sources,
                })
            }
        }
    }

    fn runtime(&self) -> PathBuf {
        match self.android {
            Some(_) => artifact_path(&self.package, self.platform.runtime),
            None => installed_runtime_path(&self.package, &self.platform),
        }
    }

    fn inspect(&self) -> Result<InstallationState> {
        let catalog = match load_catalog(&self.package).and_then(|catalog| Ok((catalog_platform(&catalog)?, catalog))) {
            Ok(loaded) => loaded,
            Err(error) => return Ok(incompatible(IncompatibleReason::MissingCatalogue, error.message)),
        };
        let (platform, catalog) = catalog;
        if !recipes_path(&self.package).is_file() {
            let details = "The package has no supported game revisions (recipes/oot3d.json)";
            return Ok(incompatible(IncompatibleReason::MissingCatalogue, details));
        }
        if platform != self.platform {
            let details = format!("This package targets {}, not {}", platform.target, self.platform.target);
            return Ok(incompatible(IncompatibleReason::TargetMismatch, details));
        }
        let state = installation_state(&self.installation, HostOs::current());
        if crate::lock::is_held(&state.join(".forge-activation.lock")) {
            return Ok(InstallationState::Preparing);
        }
        if journal_path(&self.installation, HostOs::current()).exists() {
            return Ok(repair(RepairReason::InterruptedActivation, "An activation was interrupted"));
        }
        let runtime = self.runtime();
        let active_path = self.data_root.join(ACTIVE_TITLE);
        if !active_path.exists() {
            if !runtime.is_file() {
                return Ok(incompatible(IncompatibleReason::MissingRuntime, "The package has no game runtime"));
            }
            return Ok(InstallationState::NotInstalled);
        }
        let unreadable =
            |details: String| repair(RepairReason::ConfigUnreadable { file: ACTIVE_TITLE.into() }, details);
        let active = match load_json_object(&active_path) {
            Ok(active) if text(&active, "format") == ACTIVE_TITLE_FORMAT => active,
            Ok(_) => return Ok(unreadable(format!("{ACTIVE_TITLE} has an unsupported format"))),
            Err(error) => return Ok(unreadable(error.message)),
        };
        let directory = resolve_reference(text(&active, "directory"), &self.data_root)?;
        let prepared = match load_prepared_content(&directory, &[], &CancelToken::new()) {
            Ok(prepared) => prepared,
            Err(error) => return Ok(repair(RepairReason::ReceiptMismatch, error.message)),
        };
        let module_ready = prepared.state.get("module").and_then(Value::as_object).is_some_and(|module| {
            text(module, "status") == "ready" && module.get("sha256") == active.get("module_sha256")
        });
        if !module_ready {
            return Ok(repair(RepairReason::ReceiptMismatch, "The installed title module is not ready"));
        }
        let recipe = text(&active, "recipe").to_string();
        let Some(item) = self.catalog_title(&catalog, &recipe) else {
            let details = format!("The package has no precompiled module for the installed revision {recipe}");
            return Ok(incompatible(IncompatibleReason::MissingTitleModule, details));
        };
        let plugin = item.get("plugin").and_then(Value::as_object).map(|plugin| text(plugin, "path")).unwrap_or("");
        if plugin.is_empty() || !artifact_path(&self.package, plugin).is_file() {
            let details = format!("The package's title module is missing: {plugin}");
            return Ok(incompatible(IncompatibleReason::MissingTitleModule, details));
        }
        if let Some(state) = self.unreadable_config() {
            return Ok(state);
        }
        let Some(Value::Object(receipt)) = prepared.state.get("runtime") else {
            return Ok(repair(RepairReason::ReceiptMismatch, "The active title has no playable runtime receipt"));
        };
        if !runtime.is_file() {
            return Ok(repair(RepairReason::RuntimeMissing, "The installed game runtime is missing"));
        }
        let present_sha = file_sha256(&runtime)?;
        let catalog_sha = catalog.get("runtime").and_then(Value::as_object).map(|record| text(record, "sha256"));
        if catalog_sha != Some(present_sha.as_str()) {
            let details = "The package's game runtime does not match its catalog";
            return Ok(incompatible(IncompatibleReason::PackageIntegrity, details));
        }
        let verdict = match self.android {
            None => self.desktop_runtime(&runtime, &present_sha, receipt, &prepared.directory)?,
            Some(android) => self.android_runtime(android, item, &present_sha, receipt)?,
        };
        let update = match verdict {
            Verdict::State(state) => return Ok(state),
            Verdict::Coherent { update } => update,
        };
        let version = self.package_version();
        if update {
            return Ok(InstallationState::UpdateAvailable {
                recipe,
                installed_package_identity: PackageIdentity {
                    runtime_sha256: text(receipt, "runtime_sha256").to_string(),
                    version: None,
                },
                present_package_identity: PackageIdentity { runtime_sha256: present_sha, version },
            });
        }
        if let Some(details) = self.missing_sources(&prepared) {
            return Ok(InstallationState::NeedsGameInput { recipe, details });
        }
        Ok(InstallationState::Ready { recipe, package_version: version, language: self.language() })
    }

    fn catalog_title<'c>(&self, catalog: &'c Map<String, Value>, recipe: &str) -> Option<&'c Map<String, Value>> {
        catalog
            .get("titles")?
            .as_array()?
            .iter()
            .filter_map(Value::as_object)
            .find(|item| text(item, "recipe") == recipe && text(item, "target") == self.platform.target)
    }

    /// The first settings file that exists but cannot be read as JSON.
    fn unreadable_config(&self) -> Option<InstallationState> {
        let runtime_config = if self.android.is_some() { crate::publish::ANDROID_CONFIG } else { "TriAevum.json" };
        for name in [runtime_config, "topscreen_ui.json", "game_language.json", "romfs_mods.json"] {
            let path = self.data_root.join("config").join(name);
            if !path.exists() {
                continue;
            }
            if let Err(error) = load_json_object(&path) {
                return Some(repair(RepairReason::ConfigUnreadable { file: format!("config/{name}") }, error.message));
            }
        }
        None
    }

    /// The installed tuple of a desktop package (`validate_installed_runtime`).
    /// A new package unpacked over the installation brings `runtime/` (Windows)
    /// or replaces the runtime in place (Linux, AppImage): that is an update
    /// when the rest of the installation is coherent.
    fn desktop_runtime(
        &self,
        present: &Path,
        present_sha: &str,
        receipt: &Map<String, Value>,
        title: &Path,
    ) -> Result<Verdict> {
        let installed = self.package.join(self.platform.installed_runtime);
        let packaged = join_posix(&self.package, self.platform.runtime);
        let in_place = self.platform.runtime == self.platform.installed_runtime;
        let update = text(receipt, "runtime_sha256") != present_sha && (in_place || packaged.is_file());
        let executable = if installed.is_file() { installed } else { present.to_path_buf() };
        let mut receipt = receipt.clone();
        if update && in_place {
            receipt.insert("runtime_sha256".into(), json!(present_sha));
        }
        Ok(match forge_native::validate_installed_runtime(&executable, title, &self.data_root, &receipt) {
            Ok(_) => Verdict::Coherent { update },
            Err(error) if error.kind == ErrorKind::IncompatibleTarget => {
                Verdict::State(incompatible(IncompatibleReason::TargetMismatch, error.message))
            }
            Err(error) => Verdict::State(repair(RepairReason::ReceiptMismatch, error.message)),
        })
    }

    /// The Android tuple: the APK's runtime and title library (an APK update
    /// replaces both and the catalog), the title file and the launch profile.
    fn android_runtime(
        &self,
        android: &AndroidTarget,
        item: &Map<String, Value>,
        present_sha: &str,
        receipt: &Map<String, Value>,
    ) -> Result<Verdict> {
        let title = match load_json_object(&android.title_file()) {
            Ok(title) => title,
            Err(error) => {
                let file = ANDROID_TITLE_FILE.to_string();
                return Ok(Verdict::State(repair(RepairReason::ConfigUnreadable { file }, error.message)));
            }
        };
        let catalog_plugin = item.get("plugin").and_then(Value::as_object).map(|plugin| text(plugin, "sha256"));
        let library = android.native_library_dir.join(text(&title, "plugin_library"));
        let library_sha = if library.is_file() { Some(file_sha256(&library)?) } else { None };
        if library_sha.as_deref() != catalog_plugin {
            let details = "The installed title library does not match the package catalog";
            return Ok(Verdict::State(incompatible(IncompatibleReason::PackageIntegrity, details)));
        }
        let update =
            text(receipt, "runtime_sha256") != present_sha || catalog_plugin != Some(text(receipt, "plugin_sha256"));
        if !update && text(&title, "plugin_sha256") != text(receipt, "plugin_sha256") {
            let details = "The Android title file names another title library than the receipt";
            return Ok(Verdict::State(repair(RepairReason::ReceiptMismatch, details)));
        }
        let profile = android.launch_profile();
        let expected = text(receipt, "launch_profile_sha256");
        if !profile.is_file() || (!expected.is_empty() && file_sha256(&profile)? != expected) {
            let details = "The launch profile changed after activation or is missing";
            return Ok(Verdict::State(repair(RepairReason::ReceiptMismatch, details)));
        }
        Ok(Verdict::Coherent { update })
    }

    /// Why the kept game sources cannot serve a repair, if they cannot.
    fn missing_sources(&self, prepared: &PreparedContent) -> Option<String> {
        let inputs = prepared.index.get("inputs").and_then(Value::as_object)?;
        for kind in SOURCE_KINDS {
            let intact = inputs.get(kind).and_then(Value::as_object).is_some_and(|descriptor| {
                let Ok(path) = resolve_reference(text(descriptor, "path"), &prepared.directory) else { return false };
                let size = std::fs::metadata(&path).ok().filter(|metadata| metadata.is_file()).map(|m| m.len());
                size.is_some() && size == descriptor.get("bytes").and_then(Value::as_u64)
            });
            if !intact {
                return Some(format!("The {kind} data kept from your ROM is missing or changed"));
            }
        }
        if self.verify_sources {
            if let Err(error) = load_prepared_content(&prepared.directory, &SOURCE_KINDS, &CancelToken::new()) {
                return Some(error.message);
            }
        }
        None
    }

    fn package_version(&self) -> Option<String> {
        package_version(&self.package)
    }

    fn language(&self) -> Option<String> {
        let settings = load_json_object(&forge_native::language::config_path(&self.data_root)).ok()?;
        settings.get("selected")?.as_str().map(str::to_string)
    }
}
