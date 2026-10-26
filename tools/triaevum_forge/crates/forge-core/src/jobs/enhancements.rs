// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Enhancements page's jobs (UX plan WP-07, `crate::enhancements`).
//! Requests take the inventory request's `package_root?`, `data_root?` and
//! `android?` (`{native_library_dir, profile_root, cache_dir}` as for
//! `install`), plus:
//!
//! - `enhancements_status`: nothing -> the page's states;
//! - `apply_enhancements`: `hud_layout?` -> the repair's result with
//!   `status: "applied"` and `enhancements` (the states after);
//! - `texture_import`: `source: {path}`, `flat_confirmed?` (bool);
//! - `enhancement_settings`: `hd_interface_pack?`, `mod?: {id, enabled}`,
//!   `custom_textures?`, `use_managed_texture_folder?`, `custom_romfs?`
//!   (bools) -> the states after.

use std::path::PathBuf;

use serde_json::{json, Map, Value};

use super::{android_job, android_services, optional_path, package_root, AndroidJob, Source, StageEvents};
use crate::abi::Job;
use crate::error::{ForgeError, Result};
use crate::install::InstallTarget;
use crate::inventory::InventoryRequest;

pub(super) const JOBS: [&str; 4] = ["enhancements_status", "apply_enhancements", "texture_import", "enhancement_settings"];

fn flag(request: &Map<String, Value>, key: &str) -> Result<Option<bool>> {
    match request.get(key) {
        None | Some(Value::Null) => Ok(None),
        Some(Value::Bool(value)) => Ok(Some(*value)),
        Some(_) => Err(ForgeError::invalid(format!("{key} must be a boolean"))),
    }
}

fn inventory_request(package_root: PathBuf, data_root: Option<PathBuf>, android: Option<&AndroidJob>) -> InventoryRequest {
    InventoryRequest {
        package_root,
        data_root,
        android: android.map(|android| android.target.clone()),
        android_app: None,
        sizes: false,
    }
}

fn settings_change(request: &Map<String, Value>) -> Result<crate::enhancements::SettingsChange> {
    let mod_enabled = match request.get("mod") {
        None | Some(Value::Null) => None,
        Some(Value::Object(object)) => {
            let id = object.get("id").and_then(Value::as_str).filter(|id| !id.is_empty());
            let enabled = object.get("enabled").and_then(Value::as_bool);
            match (id, enabled) {
                (Some(id), Some(enabled)) => Some((id.to_string(), enabled)),
                _ => return Err(ForgeError::invalid("mod must be {\"id\": text, \"enabled\": boolean}")),
            }
        }
        Some(_) => return Err(ForgeError::invalid("mod must be {\"id\": text, \"enabled\": boolean}")),
    };
    Ok(crate::enhancements::SettingsChange {
        hd_interface_pack: flag(request, "hd_interface_pack")?,
        mod_enabled,
        custom_textures: flag(request, "custom_textures")?,
        use_managed_texture_folder: flag(request, "use_managed_texture_folder")?.unwrap_or(false),
        custom_romfs: flag(request, "custom_romfs")?,
    })
}

pub(super) fn start(name: &str, request: &Map<String, Value>) -> Result<Job> {
    let package_root = package_root(request)?;
    let data_root = optional_path(request, "data_root");
    let android = android_job(request)?;
    let target = inventory_request(package_root.clone(), data_root.clone(), android.as_ref());
    let log = super::job_log(name, data_root.as_deref(), &package_root, android.as_ref())
        .filter(|_| name != "enhancements_status");
    match name {
        "enhancements_status" => Ok(Job::spawn(move |_, cancel| crate::enhancements::status(&target, cancel))),
        "texture_import" => {
            let source = match Source::parse(request.get("source"))? {
                Source::Path(path) => path,
                #[cfg(unix)]
                Source::Descriptor(_) => return Err(ForgeError::invalid("texture_import needs a source path")),
            };
            let flat = flag(request, "flat_confirmed")?.unwrap_or(false);
            Ok(Job::spawn_logged(log, move |sink, cancel| {
                sink.stage("import", "Checking and copying your texture pack...");
                crate::enhancements::import_textures(&target, &source, flat, cancel)
            }))
        }
        "enhancement_settings" => {
            let change = settings_change(request)?;
            Ok(Job::spawn_logged(log, move |_, cancel| crate::enhancements::change_settings(&target, &change, cancel)))
        }
        "apply_enhancements" => {
            let hud_layout = crate::publish::HudLayout::parse(request.get("hud_layout"))?;
            Ok(Job::spawn_logged(log, move |sink, cancel| {
                let mut events = StageEvents { sink, current: String::new() };
                let mut report = |stage: &str, message: &str| events.report(stage, message);
                let (install_target, services): (InstallTarget, Box<dyn forge_native::NativeServices>) = match android {
                    None => (InstallTarget::Package, Box::new(forge_native::DesktopServices)),
                    Some(android) => {
                        let services = android_services(&android)?;
                        (InstallTarget::Android(android.target), Box::new(services))
                    }
                };
                let repair = crate::repair::RepairRequest { package_root, data_root, target: install_target, hud_layout };
                let mut result = crate::enhancements::apply(&repair, services.as_ref(), &mut report, cancel)?;
                result["enhancements"] = crate::enhancements::status(&target, cancel).unwrap_or(json!(null));
                Ok(result)
            }))
        }
        other => Err(ForgeError::invalid(format!("Unknown Forge job: {other}"))),
    }
}
