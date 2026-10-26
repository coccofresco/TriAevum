// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Repair in place (UX plan WP-04A): bring a damaged installation back to
//! `Ready` from what it already has, never from a silent reimport of the ROM.
//!
//! 1. The game session lock is held throughout (the game cannot start); the
//!    activation transaction takes the installation lock itself.
//! 2. An interrupted activation is settled from its own journal: the files
//!    it names come back from their verified backups.
//! 3. The game data kept from the ROM is re-verified: the prepared title's
//!    `code`, `exheader` and `romfs` are rehashed against its index.
//!    Missing or changed data is `NeedsGameInput` (the explanation asks for
//!    the ROM); nothing is rebuilt from it.
//! 4. The precompiled installer runs again for the prepared title: every
//!    asset preparer reuses what its receipt proves and rebuilds what is
//!    missing or invalid (TopScreen, water, scene lights, interior sun, the
//!    shader seed and the renderer and device pipeline caches), then the
//!    plugin generation, launch profile, receipts and active title are
//!    published together.
//!
//! When the prepared title itself is missing or damaged (no or unreadable
//! `active-title.json`, a broken index), it is prepared again from the kept
//! ROM data (`sources/`), which the import verifies against the revision.
//! The TopScreen layout is kept unless `hud_layout` says otherwise, and every
//! change to `D/config` is reported.

use std::path::{Path, PathBuf};

use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result};
use forge_catalog::precompiled::{artifact_path, load_catalog};
use forge_catalog::recipes::load_recipe;
use forge_native::NativeServices;
use forge_prepare::content::load_prepared_content;
use serde_json::{json, Value};

use crate::active_title::{load_active_title, named_directory, ACTIVE_TITLE};
use crate::config_report::{changes_value, config_changes, ConfigSnapshot, Permitted};
use crate::install::{
    install_precompiled_title, install_private_title_with, installation_paths, package_layout, recipes_path,
    InstallOptions, InstallRequest, InstallSource, InstallTarget, PrecompiledRequest, Report,
};
use crate::layout::{self, HostOs};
use crate::publish::{AndroidRuntime, HudLayout};

const SOURCE_KINDS: [&str; 3] = ["code", "exheader", "romfs"];

pub struct RepairRequest {
    pub package_root: PathBuf,
    pub data_root: Option<PathBuf>,
    pub target: InstallTarget,
    pub hud_layout: HudLayout,
}

impl RepairRequest {
    pub fn package(package_root: impl Into<PathBuf>) -> Self {
        Self {
            package_root: package_root.into(),
            data_root: None,
            target: InstallTarget::Package,
            hud_layout: HudLayout::Keep,
        }
    }
}

/// The ROM is needed: the kept game data is missing or no longer verifies.
pub fn needs_game_input(details: impl Into<String>) -> ForgeError {
    ForgeError::new(ErrorKind::InvalidInput, details)
        .at_stage("verify")
        .during(Operation::Repair)
        .because(Reason::MissingRetainedSource)
}

/// A damaged prepared title (`titles/<revision>/<key>` whose index or state
/// no longer loads) would block its own preparation; it holds only generated
/// files, never saves or settings.
fn remove_damaged_titles(data_root: &Path, cancel: &CancelToken) -> Result<()> {
    let titles = data_root.join("titles");
    let Ok(revisions) = std::fs::read_dir(&titles) else { return Ok(()) };
    for revision in revisions.flatten().filter(|entry| entry.path().is_dir()) {
        let Ok(keys) = std::fs::read_dir(revision.path()) else { continue };
        for key in keys.flatten() {
            let (path, name) = (key.path(), key.file_name());
            if !path.is_dir() || name.to_string_lossy().starts_with('.') {
                continue;
            }
            cancel.check()?;
            if load_prepared_content(&path, &[], cancel).is_err_and(|error| error.kind != ErrorKind::Cancelled) {
                std::fs::remove_dir_all(&path).map_err(|error| {
                    forge_base::fsutil::file_error("Cannot remove", &path, error).during(Operation::Repair)
                })?;
            }
        }
    }
    Ok(())
}

/// How the title came back.
enum Route {
    /// The prepared title verified and was installed again.
    InPlace,
    /// The title was prepared again from the kept ROM data.
    KeptRomData,
}

/// Repairs the installation `request` names. Result: `{"status":
/// "repaired", "recipe", "route": "in_place" | "kept_rom_data",
/// "recovered_activation", "hud_layout", "config_changes", "build"}`.
pub fn repair_installation(
    request: &RepairRequest,
    services: &dyn NativeServices,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    let (package, data_root) =
        installation_paths(&request.package_root, request.data_root.as_deref(), &request.target)?;
    let android = match &request.target {
        InstallTarget::Package => None,
        InstallTarget::Android(android) => Some(android),
    };
    let activation = match android {
        None => package_layout(&package)?.activation,
        Some(android) => forge_prepare::paths::resolve(&android.profile_root)?,
    };
    let os = HostOs::current();
    let _session = crate::session::hold_game_session(&activation, os, Operation::Repair)?;
    report("preflight", "Checking the installation...");
    if android.is_none() {
        layout::ensure_runtime_closed(&package, &layout::host_platform()?)?;
    }
    // A package without its catalog or module is incompatible, never a build.
    load_catalog(&package).map_err(|error| error.during(Operation::PackageVerify))?;
    let before = ConfigSnapshot::take(&data_root);
    let recovered = crate::transaction::recover_interrupted(&activation, os, &[activation.clone(), data_root.clone()])
        .map_err(|error| error.during(Operation::Repair))?;
    if recovered {
        report("preflight", "An interrupted activation was undone.");
    }

    report("verify", "Checking the game data kept from your ROM...");
    let state_path = data_root.join(ACTIVE_TITLE);
    let directory =
        load_active_title(&state_path).map(|active| active.directory).or_else(|| named_directory(&state_path));
    let intact = directory.as_deref().and_then(|directory| load_prepared_content(directory, &[], cancel).ok());
    let (route, recipe, built) = match intact {
        Some(prepared) => {
            if let Err(error) = load_prepared_content(&prepared.directory, &SOURCE_KINDS, cancel) {
                if error.kind == ErrorKind::Cancelled {
                    return Err(error);
                }
                return Err(needs_game_input(format!(
                    "The game data kept from your ROM is missing or changed ({})",
                    error.message
                )));
            }
            let recipe_id = prepared.index.get("recipe").and_then(Value::as_str).unwrap_or_default().to_string();
            let recipe =
                load_recipe(&recipes_path(&package), &recipe_id).map_err(|error| error.during(Operation::Repair))?;
            report("activate", "Installing the verified precompiled title again...");
            let (runtime_plugin, launch_profile, android_runtime) = match android {
                None => (
                    crate::install::default_runtime_plugin_path(&package)?,
                    crate::install::default_runtime_launch_profile_path(&package)?,
                    None,
                ),
                Some(android) => (
                    android.title_file(),
                    android.launch_profile(),
                    Some(artifact_path(&package, layout::ANDROID.runtime)),
                ),
            };
            let android_runtime =
                android_runtime.as_deref().map(|runtime| AndroidRuntime { runtime, title_file: &runtime_plugin });
            let built = install_precompiled_title(
                &PrecompiledRequest {
                    prepared_directory: &prepared.directory,
                    root: &package,
                    recipe: &recipe,
                    data_root: &data_root,
                    runtime_plugin: &runtime_plugin,
                    launch_profile: &launch_profile,
                    active_title_state: &state_path,
                    android: android_runtime.as_ref(),
                    hud_layout: request.hud_layout,
                },
                services,
                report,
                cancel,
            )
            .map_err(|error| error.during(Operation::Repair))?;
            (Route::InPlace, recipe_id, built)
        }
        None => {
            let Some(source) = crate::data_actions::retained_source(&data_root) else {
                return Err(needs_game_input("The game data kept from your ROM is missing"));
            };
            remove_damaged_titles(&data_root, cancel)?;
            let install = InstallRequest {
                source: InstallSource::ExtractedDirectory(source),
                data_root: Some(data_root.clone()),
                package_root: package.clone(),
                target: request.target.clone(),
                game_language: None,
            };
            let options = InstallOptions { hud_layout: request.hud_layout };
            let result = install_private_title_with(install, options, services, report, cancel).map_err(|error| {
                // Kept data that no longer verifies needs the ROM again.
                if matches!(error.stage, Some("extract" | "verify" | "adapt")) && error.kind != ErrorKind::Cancelled {
                    needs_game_input(error.message)
                } else {
                    error.during(Operation::Repair)
                }
            })?;
            let recipe = result.get("recipe").and_then(Value::as_str).unwrap_or_default().to_string();
            (Route::KeptRomData, recipe, result)
        }
    };
    let reimported = matches!(route, Route::KeptRomData);
    let permitted = Permitted { hud_layout: request.hud_layout, language: reimported };
    let changes = config_changes(&before, &ConfigSnapshot::take(&data_root), permitted);
    report("ready", "The installation is repaired. Your saves and settings were kept.");
    Ok(json!({
        "status": "repaired",
        "recipe": recipe,
        "route": if reimported { "kept_rom_data" } else { "in_place" },
        "recovered_activation": recovered,
        "hud_layout": request.hud_layout.code(),
        "config_changes": changes_value(&changes),
        "build": built,
    }))
}
