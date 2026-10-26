// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Update in place (UX plan WP-04A): `package_update.refresh_packaged_runtime`.
//! A newer verified package in the package folder (a replaced AppImage or
//! APK, a ZIP unpacked over the Windows folder) is bound to the prepared
//! game without the ROM, a reimport or a compiler.
//!
//! As in Python: a package without a publisher catalog is a developer
//! installation (no refresh); an interrupted activation is refused; the
//! same runtime and plugin are a no-op. Otherwise the installed revision
//! must be in the new catalog and its artifacts verified, every previous
//! binding (title plugin generation, launch profile, settings, routed
//! assets) must still validate with only the runtime identity substituted,
//! and then the precompiled installer runs **after** the inspection lock is
//! released: it takes the non-reentrant installation lock for its own
//! activation transaction (immutable plugin generation, receipts recomputed
//! together, the runtime placed inside the transaction body).
//!
//! Deliberate differences from Python:
//!
//! - Python's Play placed the new runtime before refreshing. Here nothing is
//!   placed before validation: the new runtime is verified against its
//!   catalog (`select_title`) and the previous tuple is validated with the
//!   executable in the installation root, whose own identity is substituted
//!   (it is the runtime being replaced). Placement happens in the
//!   publication. Only a Windows package whose root runtime is missing
//!   places the verified runtime first, as Python did.
//! - The whole refresh holds the game session lock, so the game cannot
//!   start halfway (the installation lock is still taken only for the
//!   inspection and by the publication).
//! - An existing TopScreen layout is kept unless the request says
//!   `hud_layout: "standard"` (owner decision 4); every change to
//!   `D/config` is reported (`config_report`).
//! - The installed package version is recorded beside the receipts
//!   (`installed-package.json`), so a downgrade is detected and needs
//!   `allow_downgrade` (owner decision 10; the confirmation is the
//!   frontend's).
//! - Android: the APK replaced both libraries, so the previous tuple is the
//!   title file, the launch profile and the settings.

use std::cmp::Ordering;
use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result, Subject};
use forge_catalog::precompiled::{artifact_path, load_catalog, select_title};
use forge_catalog::recipes::load_recipe;
use forge_catalog::ReleasePlatform;
use forge_native::NativeServices;
use forge_prepare::content::load_prepared_content;
use serde_json::{json, Map, Value};

use crate::active_title::{load_active_title, ACTIVE_TITLE};
use crate::config_report::{changes_value, config_changes, ConfigSnapshot, Permitted};
use crate::install::{
    install_precompiled_title, installation_paths, package_layout, recipes_path, AndroidTarget, InstallTarget,
    PrecompiledRequest, Report,
};
use crate::layout::{self, HostOs};
use crate::publish::{AndroidRuntime, HudLayout, ANDROID_CONFIG};
use crate::state::{package_version, PackageIdentity};
use crate::transaction::{installation_lock, journal_path};

/// The publisher catalog; a package without one is a developer installation.
pub const CATALOG: &str = "recipes/precompiled-titles.json";
/// The installed package's version, beside the receipts it belongs to.
pub const PACKAGE_RECORD: &str = "installed-package.json";
pub const PACKAGE_RECORD_FORMAT: &str = "triaevum_installed_package_v1";

pub struct RefreshRequest {
    pub package_root: PathBuf,
    pub data_root: Option<PathBuf>,
    pub target: InstallTarget,
    pub hud_layout: HudLayout,
    /// The user confirmed installing an older package over a newer one.
    pub allow_downgrade: bool,
}

impl RefreshRequest {
    pub fn package(package_root: impl Into<PathBuf>) -> Self {
        Self {
            package_root: package_root.into(),
            data_root: None,
            target: InstallTarget::Package,
            hud_layout: HudLayout::Keep,
            allow_downgrade: false,
        }
    }
}

fn text<'a>(map: &'a Map<String, Value>, key: &str) -> &'a str {
    map.get(key).and_then(Value::as_str).unwrap_or_default()
}

fn file_sha256(path: &Path) -> Result<String> {
    Ok(hex(&sha256_file(path, &CancelToken::new())?.1))
}

/// Writes the package record of a committed publication. Best effort: the
/// record only names a version, and a stale one is ignored because it is
/// bound to the receipt's runtime and plugin digests.
pub fn record_installed_package(package_root: &Path, prepared_directory: &Path) {
    let Ok(prepared) = load_prepared_content(prepared_directory, &[], &CancelToken::new()) else { return };
    let Some(Value::Object(runtime)) = prepared.state.get("runtime") else { return };
    let record = json!({
        "format": PACKAGE_RECORD_FORMAT,
        "version": package_version(package_root),
        "runtime_sha256": runtime.get("runtime_sha256").cloned().unwrap_or(Value::Null),
        "plugin_sha256": runtime.get("plugin_sha256").cloned().unwrap_or(Value::Null),
    });
    let _ = atomic_write_json(&prepared.directory.join(PACKAGE_RECORD), &record);
}

/// The installed package as its receipt and record describe it; the version
/// is unknown without a record for exactly this runtime and plugin.
pub fn installed_package(prepared_directory: &Path, receipt: &Map<String, Value>) -> PackageIdentity {
    let version = load_json_object(&prepared_directory.join(PACKAGE_RECORD)).ok().and_then(|record| {
        let bound = text(&record, "format") == PACKAGE_RECORD_FORMAT
            && record.get("runtime_sha256") == receipt.get("runtime_sha256")
            && record.get("plugin_sha256") == receipt.get("plugin_sha256");
        bound.then(|| record.get("version").and_then(Value::as_str).map(str::to_string)).flatten()
    });
    PackageIdentity { runtime_sha256: text(receipt, "runtime_sha256").to_string(), version }
}

/// Semantic-version precedence of two package versions (`0.8.0-beta.1.2`):
/// numeric parts numerically, a pre-release before its release, build
/// metadata ignored. None when either is not a version.
pub fn compare_versions(left: &str, right: &str) -> Option<Ordering> {
    fn parse(text: &str) -> Option<(Vec<u64>, Vec<&str>)> {
        let text = text.trim().trim_start_matches('v');
        let text = text.split_once('+').map_or(text, |(version, _)| version);
        let (core, pre) = text.split_once('-').map_or((text, None), |(core, pre)| (core, Some(pre)));
        let numbers = core.split('.').map(|part| part.parse().ok()).collect::<Option<Vec<u64>>>()?;
        let pre: Vec<&str> = pre.map(|pre| pre.split('.').collect()).unwrap_or_default();
        (!numbers.is_empty() && pre.iter().all(|part| !part.is_empty())).then_some((numbers, pre))
    }
    let ((left_core, left_pre), (right_core, right_pre)) = (parse(left)?, parse(right)?);
    let width = left_core.len().max(right_core.len());
    let padded = |core: &[u64]| (0..width).map(|index| core.get(index).copied().unwrap_or(0)).collect::<Vec<_>>();
    let core = padded(&left_core).cmp(&padded(&right_core));
    if core != Ordering::Equal {
        return Some(core);
    }
    Some(match (left_pre.is_empty(), right_pre.is_empty()) {
        (true, true) => Ordering::Equal,
        (true, false) => Ordering::Greater,
        (false, true) => Ordering::Less,
        (false, false) => {
            for (a, b) in left_pre.iter().zip(&right_pre) {
                let order = match (a.parse::<u64>(), b.parse::<u64>()) {
                    (Ok(a), Ok(b)) => a.cmp(&b),
                    (Ok(_), Err(_)) => Ordering::Less,
                    (Err(_), Ok(_)) => Ordering::Greater,
                    (Err(_), Err(_)) => a.cmp(b),
                };
                if order != Ordering::Equal {
                    return Some(order);
                }
            }
            left_pre.len().cmp(&right_pre.len())
        }
    })
}

fn unchanged(reason: &str) -> Value {
    json!({"status": "unchanged", "reason": reason})
}

fn update_error(kind: ErrorKind, message: impl Into<String>) -> ForgeError {
    ForgeError::new(kind, message).during(Operation::Update)
}

/// What the inspection under the installation lock decided.
enum Plan {
    Done(Value),
    Install(Box<Install>),
}

struct Install {
    title: PathBuf,
    recipe: Map<String, Value>,
    profile: PathBuf,
    previous: PackageIdentity,
    previous_plugin: String,
    present: PackageIdentity,
    present_plugin: String,
}

struct Context<'a> {
    package: PathBuf,
    data_root: PathBuf,
    activation: PathBuf,
    platform: ReleasePlatform,
    android: Option<&'a AndroidTarget>,
}

/// `refresh_packaged_runtime`. Results:
///
/// - `{"status": "unchanged", "reason": "developer" | "not_installed" |
///   "not_ready" | "current"}` (Python: `False`);
/// - `{"status": "confirmation_required", "reason": "downgrade",
///   "installed_version", "present_version"}`: nothing changed;
/// - `{"status": "updated", "recipe", "previous", "present", "hud_layout",
///   "config_changes", "build"}` (Python: `True`).
pub fn refresh_installation(
    request: &RefreshRequest,
    services: &dyn NativeServices,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    let (package, data_root) =
        installation_paths(&request.package_root, request.data_root.as_deref(), &request.target)?;
    if !package.join(CATALOG).is_file() {
        return Ok(unchanged("developer"));
    }
    let android = match &request.target {
        InstallTarget::Package => None,
        InstallTarget::Android(android) => Some(android),
    };
    let activation = match android {
        None => package_layout(&package)?.activation,
        Some(android) => forge_prepare::paths::resolve(&android.profile_root)?,
    };
    let context = Context { package, data_root, activation, platform: request.target.platform()?, android };
    let os = HostOs::current();
    let _session = crate::session::hold_game_session(&context.activation, os, Operation::Update)?;
    report("preflight", "Checking the updated package...");
    if android.is_none() {
        layout::ensure_runtime_closed(&context.package, &layout::host_platform()?)?;
    }
    let install = match context.inspect(request, cancel)? {
        Plan::Done(result) => return Ok(result),
        Plan::Install(install) => install,
    };

    // The installer takes the installation lock itself and revalidates the
    // prepared inputs: never nest its transaction under the inspection lock.
    report("activate", "Installing the updated package for your prepared game...");
    let before = ConfigSnapshot::take(&context.data_root);
    let (runtime_plugin, android_runtime) = match android {
        None => (context.activation.join(context.platform.title_module), None),
        Some(android) => (android.title_file(), Some(artifact_path(&context.package, context.platform.runtime))),
    };
    let android_runtime =
        android_runtime.as_deref().map(|runtime| AndroidRuntime { runtime, title_file: &runtime_plugin });
    let built = install_precompiled_title(
        &PrecompiledRequest {
            prepared_directory: &install.title,
            root: &context.package,
            recipe: &install.recipe,
            data_root: &context.data_root,
            runtime_plugin: &runtime_plugin,
            launch_profile: &install.profile,
            active_title_state: &context.data_root.join(ACTIVE_TITLE),
            android: android_runtime.as_ref(),
            hud_layout: request.hud_layout,
        },
        services,
        report,
        cancel,
    )
    .map_err(|error| error.during(Operation::Update))?;
    let permitted = Permitted { hud_layout: request.hud_layout, language: false };
    let changes = config_changes(&before, &ConfigSnapshot::take(&context.data_root), permitted);
    report("ready", "The update is installed. Your saves and settings were kept.");
    let identity = |identity: &PackageIdentity, plugin: &str| json!({"runtime_sha256": identity.runtime_sha256, "plugin_sha256": plugin, "version": identity.version});
    Ok(json!({
        "status": "updated",
        "recipe": text(&install.recipe, "id"),
        "previous": identity(&install.previous, &install.previous_plugin),
        "present": identity(&install.present, &install.present_plugin),
        "hud_layout": request.hud_layout.code(),
        "config_changes": changes_value(&changes),
        "build": built,
    }))
}

impl Context<'_> {
    fn inspect(&self, request: &RefreshRequest, cancel: &CancelToken) -> Result<Plan> {
        let os = HostOs::current();
        let _lock = installation_lock(&self.activation, os).map_err(|error| error.during(Operation::Update))?;
        if journal_path(&self.activation, os).exists() {
            return Err(update_error(
                ErrorKind::RecoveryRequired,
                "An activation was interrupted; run Forge before updating",
            ));
        }
        let Some(active) = load_active_title(&self.data_root.join(ACTIVE_TITLE)) else {
            return Ok(Plan::Done(unchanged("not_installed")));
        };
        let prepared = load_prepared_content(&active.directory, &[], cancel)?;
        let runtime = match prepared.state.get("runtime") {
            Some(Value::Object(runtime)) if text(runtime, "status") == "ready" => runtime.clone(),
            _ => return Ok(Plan::Done(unchanged("not_ready"))),
        };
        let catalog = load_catalog(&self.package).map_err(|error| error.during(Operation::PackageVerify))?;
        let titles = catalog.get("titles").and_then(Value::as_array).map(Vec::as_slice).unwrap_or_default();
        let matches: Vec<&Map<String, Value>> = titles
            .iter()
            .filter_map(Value::as_object)
            .filter(|item| text(item, "recipe") == active.recipe_id)
            .collect();
        let [candidate] = matches[..] else {
            return Err(ForgeError::new(
                ErrorKind::ArtifactIntegrity,
                "The updated package does not support the installed title",
            )
            .during(Operation::PackageVerify)
            .about(Subject::Package)
            .because(Reason::MissingModule));
        };
        let catalog_runtime =
            catalog.get("runtime").and_then(Value::as_object).map(|record| text(record, "sha256")).unwrap_or("");
        let candidate_plugin =
            candidate.get("plugin").and_then(Value::as_object).map(|plugin| text(plugin, "sha256")).unwrap_or("");
        if text(&runtime, "runtime_sha256") == catalog_runtime && text(&runtime, "plugin_sha256") == candidate_plugin {
            return Ok(Plan::Done(unchanged("current")));
        }
        let previous = installed_package(&prepared.directory, &runtime);
        let present =
            PackageIdentity { runtime_sha256: catalog_runtime.to_string(), version: package_version(&self.package) };
        if let (Some(installed), Some(offered)) = (&previous.version, &present.version) {
            if compare_versions(installed, offered) == Some(Ordering::Greater) && !request.allow_downgrade {
                return Ok(Plan::Done(json!({
                    "status": "confirmation_required",
                    "reason": "downgrade",
                    "installed_version": installed,
                    "present_version": offered,
                })));
            }
        }
        let recipe = load_recipe(&recipes_path(&self.package), &active.recipe_id)?;
        select_title(&self.package, &recipe, Some(&catalog), &self.platform, cancel)
            .map_err(|error| error.during(Operation::PackageVerify))?;
        // The old runtime was replaced by the package manager. Validate every
        // other old binding, and the new runtime against its catalog, before
        // the installer may publish a replacement tuple.
        let profile = match self.android {
            None => self.validate_desktop(&prepared.directory, &runtime, catalog_runtime)?,
            Some(android) => self.validate_android(android, &runtime)?,
        };
        Ok(Plan::Install(Box::new(Install {
            title: prepared.directory,
            recipe,
            profile,
            previous_plugin: text(&runtime, "plugin_sha256").to_string(),
            previous,
            present,
            present_plugin: candidate_plugin.to_string(),
        })))
    }

    /// `validate_installed_runtime(executable, title, data_root, previous)`
    /// with the runtime identity substituted: the catalog's when the new
    /// runtime is already in place, else that of the root runtime this
    /// update replaces (the profile's `${runtime_dir}` is the root).
    fn validate_desktop(&self, title: &Path, runtime: &Map<String, Value>, catalog_runtime: &str) -> Result<PathBuf> {
        let present = forge_catalog::platform::installed_runtime_path(&self.package, &self.platform);
        let installed = self.package.join(self.platform.installed_runtime);
        let runtime_sha256 = if present == installed {
            catalog_runtime.to_string()
        } else if installed.is_file() {
            file_sha256(&installed)?
        } else {
            // Nothing to validate the profile with: place the verified
            // runtime first, as the Python Forge always did.
            layout::place_installed_runtime(&self.package, &layout::host_platform()?)?;
            catalog_runtime.to_string()
        };
        let mut previous = runtime.clone();
        previous.insert("runtime_sha256".into(), json!(runtime_sha256));
        forge_native::validate_installed_runtime(&installed, title, &self.data_root, &previous)
            .map_err(|error| error.during(Operation::Update))
    }

    /// The Android tuple the APK update left: the title file, the launch
    /// profile the receipt pins, and the settings.
    fn validate_android(&self, android: &AndroidTarget, runtime: &Map<String, Value>) -> Result<PathBuf> {
        let invalid = |message: String| update_error(ErrorKind::RecoveryRequired, message);
        let title = load_json_object(&android.title_file())?;
        if text(&title, "format") != crate::publish::ANDROID_TITLE_FORMAT {
            return Err(invalid("The Android title file has an unsupported format; run Forge again".into()));
        }
        let profile = android.launch_profile();
        let expected = text(runtime, "launch_profile_sha256");
        if !profile.is_file() || (!expected.is_empty() && file_sha256(&profile)? != expected) {
            return Err(invalid("The launch profile changed after activation; run Forge again".into()));
        }
        for name in [ANDROID_CONFIG, "topscreen_ui.json"] {
            let path = self.data_root.join("config").join(name);
            if path.exists() {
                load_json_object(&path)?;
            }
        }
        Ok(profile)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions_follow_semantic_precedence() {
        let order = |left, right| compare_versions(left, right).unwrap();
        assert_eq!(order("0.8.0-beta.1.2", "0.8.0-beta.1.3"), Ordering::Less);
        assert_eq!(order("0.8.0-beta.1.10", "0.8.0-beta.1.9"), Ordering::Greater);
        assert_eq!(order("0.8.0-beta.1.2", "0.8.0"), Ordering::Less);
        assert_eq!(order("0.8.1", "0.8.0-beta.2"), Ordering::Greater);
        assert_eq!(order("0.8", "0.8.0"), Ordering::Equal);
        assert_eq!(order("0.8.0-beta", "0.8.0-beta.1"), Ordering::Less);
        assert_eq!(order("0.8.0-1", "0.8.0-alpha"), Ordering::Less);
        assert_eq!(order("v1.0.0+build.7", "1.0.0"), Ordering::Equal);
        assert_eq!(compare_versions("unknown", "0.8.0"), None);
        assert_eq!(compare_versions("0.8.0-", "0.8.0"), None);
    }
}
