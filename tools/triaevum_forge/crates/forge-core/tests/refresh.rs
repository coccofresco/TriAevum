// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Update in place (UX plan WP-04A) on a synthetic installation: a replaced
//! package is bound to the prepared game without the ROM, user files keep
//! their bytes, settings change only by the reported permitted migrations.

mod common;
#[path = "common/update.rs"]
mod update;

use common::{host, FakeRuntime, Fixture};
use forge_base::fsutil::load_json_object;
use forge_base::hashing::sha256_hex;
use forge_base::{CancelToken, ErrorKind, Reason, Result};
use forge_core::publish::HudLayout;
use forge_core::refresh::{refresh_installation, RefreshRequest};
use forge_core::state::{inspect, IncompatibleReason, InspectRequest, InstallationState, RepairReason};
use serde_json::{json, Value};
use update::{assert_kept, config_files, customize_settings, receipt, replace_package, sentinels, set_version};

const NEW_RUNTIME: &[u8] = b"fake runtime 2";
const NEW_PLUGIN: &[u8] = b"MZ fake title plugin 2";

fn state(fixture: &Fixture) -> InstallationState {
    inspect(&InspectRequest::package(&fixture.package))
}

fn refresh_with(fixture: &Fixture, configure: impl FnOnce(&mut RefreshRequest)) -> Result<Value> {
    let mut request = RefreshRequest::package(&fixture.package);
    configure(&mut request);
    refresh_installation(&request, &FakeRuntime::default(), &mut |_, _| {}, &CancelToken::new())
}

fn refresh(fixture: &Fixture) -> Result<Value> {
    refresh_with(fixture, |_| {})
}

/// An installation of package 0.8.0 with the user's own files and settings.
fn installed() -> (Fixture, Vec<(std::path::PathBuf, Vec<u8>)>) {
    let fixture = Fixture::new();
    set_version(&fixture, "0.8.0");
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    customize_settings(&fixture.data());
    let kept = sentinels(&fixture);
    (fixture, kept)
}

#[test]
fn a_replaced_package_is_bound_without_the_rom_and_keeps_everything() {
    let (fixture, kept) = installed();
    let settings = config_files(&fixture.data());
    let old_receipt = receipt(&fixture);
    replace_package(&fixture, Some("0.8.1"), NEW_RUNTIME, NEW_PLUGIN);

    let InstallationState::UpdateAvailable { installed_package_identity, present_package_identity, .. } =
        state(&fixture)
    else {
        panic!("{:?}", state(&fixture))
    };
    assert_eq!(installed_package_identity.version.as_deref(), Some("0.8.0"), "recorded at installation");
    assert_eq!(present_package_identity.version.as_deref(), Some("0.8.1"));

    // The extracted ROM data is gone: the update must not need it.
    std::fs::remove_dir_all(&fixture.extracted).unwrap();
    let mut stages = Vec::new();
    let result = refresh_installation(
        &RefreshRequest::package(&fixture.package),
        &FakeRuntime::default(),
        &mut |stage: &str, message: &str| stages.push((stage.to_string(), message.to_string())),
        &CancelToken::new(),
    )
    .unwrap();
    assert_eq!(result["status"], "updated");
    assert_eq!(result["previous"]["version"], "0.8.0");
    assert_eq!(result["present"]["version"], "0.8.1");
    assert_eq!(result["present"]["runtime_sha256"], sha256_hex(NEW_RUNTIME));
    assert_eq!(result["hud_layout"], "keep");
    assert_eq!(result["config_changes"], json!([]), "settings keep their bytes");
    assert_eq!(stages.first().unwrap().0, "preflight");
    assert_eq!(stages.last().unwrap().0, "ready");

    assert!(matches!(state(&fixture), InstallationState::Ready { .. }), "{:?}", state(&fixture));
    assert_kept(&kept);
    assert_eq!(config_files(&fixture.data()), settings);
    let new_receipt = receipt(&fixture);
    assert_eq!(new_receipt["runtime_sha256"], sha256_hex(NEW_RUNTIME));
    assert_eq!(new_receipt["plugin_sha256"], sha256_hex(NEW_PLUGIN));
    assert_ne!(new_receipt["launch_profile_sha256"], old_receipt["launch_profile_sha256"]);
    // Immutable plugin generations: the new one is added, the old one stays.
    let generations = fixture.package.join("private-plugins");
    for plugin in [b"MZ fake title plugin".as_slice(), NEW_PLUGIN] {
        let generation = generations.join(sha256_hex(plugin)).join(host().title_module);
        assert_eq!(std::fs::read(&generation).unwrap(), plugin);
    }
    // The packaged runtime was placed in the installation root.
    assert_eq!(std::fs::read(fixture.package.join(host().installed_runtime)).unwrap(), NEW_RUNTIME);

    assert_eq!(refresh(&fixture).unwrap(), json!({"status": "unchanged", "reason": "current"}));
}

#[test]
fn use_standard_resets_only_the_layout_and_says_so() {
    let (fixture, _) = installed();
    let before = load_json_object(&fixture.data().join("config/topscreen_ui.json")).unwrap();
    let runtime_config = std::fs::read(fixture.data().join("config/TriAevum.json")).unwrap();
    replace_package(&fixture, Some("0.8.1"), NEW_RUNTIME, NEW_PLUGIN);
    let result = refresh_with(&fixture, |request| request.hud_layout = HudLayout::Standard).unwrap();
    assert_eq!(result["hud_layout"], "standard");
    assert_eq!(
        result["config_changes"],
        json!([{"file": "config/topscreen_ui.json", "change": "standard_hud_layout",
                "fields": ["dpad_size_percent", "hud_margin_x"]}])
    );
    let after = load_json_object(&fixture.data().join("config/topscreen_ui.json")).unwrap();
    assert_eq!(after["hud_margin_x"], forge_core::publish::standard_topscreen().unwrap()["hud_margin_x"]);
    assert_eq!(after["camera_fov_percent"], before["camera_fov_percent"], "not a layout field");
    assert_eq!(std::fs::read(fixture.data().join("config/TriAevum.json")).unwrap(), runtime_config);
}

#[test]
fn a_downgrade_needs_a_confirmation() {
    let (fixture, _) = installed();
    replace_package(&fixture, Some("0.8.1"), NEW_RUNTIME, NEW_PLUGIN);
    refresh(&fixture).unwrap();
    replace_package(&fixture, Some("0.7.9"), b"fake runtime old", b"MZ fake title plugin old");
    let asked = refresh(&fixture).unwrap();
    assert_eq!(
        asked,
        json!({"status": "confirmation_required", "reason": "downgrade",
               "installed_version": "0.8.1", "present_version": "0.7.9"})
    );
    assert_eq!(receipt(&fixture)["runtime_sha256"], sha256_hex(NEW_RUNTIME), "nothing changed");
    let confirmed = refresh_with(&fixture, |request| request.allow_downgrade = true).unwrap();
    assert_eq!(confirmed["status"], "updated");
    assert_eq!(confirmed["present"]["version"], "0.7.9");
}

#[test]
fn old_plugin_and_profile_damage_is_not_laundered() {
    let (fixture, _) = installed();
    replace_package(&fixture, None, NEW_RUNTIME, NEW_PLUGIN);
    let generation =
        fixture.package.join("private-plugins").join(sha256_hex(b"MZ fake title plugin")).join(host().title_module);
    std::fs::write(&generation, b"damaged").unwrap();
    let error = refresh(&fixture).unwrap_err();
    assert!(error.message.starts_with("Installed runtime identity mismatch"), "{}", error.message);
    std::fs::write(&generation, b"MZ fake title plugin").unwrap();
    let profile = fixture.launch_profile();
    let original = std::fs::read(&profile).unwrap();
    std::fs::write(&profile, b"{}").unwrap();
    let error = refresh(&fixture).unwrap_err();
    assert_eq!(error.message, "The launch profile changed after activation; run Forge again");
    assert_eq!(error.kind, ErrorKind::RecoveryRequired);
    // Nothing was installed or placed.
    std::fs::write(&profile, original).unwrap();
    assert_eq!(receipt(&fixture)["plugin_sha256"], sha256_hex(b"MZ fake title plugin"));
    assert!(!fixture.package.join("private-plugins").join(sha256_hex(NEW_PLUGIN)).exists());
    assert_eq!(refresh(&fixture).unwrap()["status"], "updated");
}

#[test]
fn an_interrupted_activation_is_refused() {
    let (fixture, _) = installed();
    replace_package(&fixture, None, NEW_RUNTIME, NEW_PLUGIN);
    let journal = forge_core::transaction::journal_path(&fixture.package, forge_core::layout::HostOs::current());
    std::fs::write(&journal, b"{}").unwrap();
    let error = refresh(&fixture).unwrap_err();
    assert_eq!(error.message, "An activation was interrupted; run Forge before updating");
    assert_eq!(error.kind, ErrorKind::RecoveryRequired);
    assert!(matches!(
        state(&fixture),
        InstallationState::RepairNeeded { reason: RepairReason::InterruptedActivation, .. }
    ));
}

#[test]
fn a_package_without_the_installed_revision_is_incompatible_never_a_build() {
    let (fixture, _) = installed();
    replace_package(&fixture, None, NEW_RUNTIME, NEW_PLUGIN);
    update::drop_title(&fixture);
    let error = refresh(&fixture).unwrap_err();
    assert_eq!(error.message, "The updated package does not support the installed title");
    assert_eq!((error.kind, error.reason), (ErrorKind::ArtifactIntegrity, Some(Reason::MissingModule)));
    assert_eq!(forge_core::explain::explain(&error).id, "package.component_missing");
    assert!(matches!(
        state(&fixture),
        InstallationState::Incompatible { reason: IncompatibleReason::MissingTitleModule, .. }
    ));
    assert_eq!(receipt(&fixture)["runtime_sha256"], sha256_hex(b"fake runtime"));
}

#[test]
fn developer_packages_and_empty_installations_are_left_alone() {
    let fixture = Fixture::new();
    assert_eq!(refresh(&fixture).unwrap(), json!({"status": "unchanged", "reason": "not_installed"}));
    std::fs::remove_file(fixture.package.join("recipes/precompiled-titles.json")).unwrap();
    assert_eq!(refresh(&fixture).unwrap(), json!({"status": "unchanged", "reason": "developer"}));
}

#[test]
fn a_running_game_blocks_the_update() {
    let (fixture, _) = installed();
    replace_package(&fixture, None, NEW_RUNTIME, NEW_PLUGIN);
    let lock = forge_core::session::session_lock_path(&fixture.package, forge_core::layout::HostOs::current());
    let game = std::fs::File::create(&lock).unwrap();
    game.lock().unwrap();
    let error = refresh(&fixture).unwrap_err();
    assert_eq!((error.kind, error.reason), (ErrorKind::Busy, Some(Reason::GameRunning)));
    drop(game);
    assert_eq!(refresh(&fixture).unwrap()["status"], "updated");
}

/// Issue #110: a library the old package left in the root is loaded (Forge
/// loads the CRT beside it) and cannot be deleted; the update renames it
/// aside and succeeds.
#[cfg(windows)]
#[test]
fn a_loaded_library_in_the_root_does_not_stop_the_update() {
    use std::os::windows::ffi::OsStrExt;
    #[link(name = "kernel32")]
    extern "system" {
        fn LoadLibraryW(name: *const u16) -> *mut std::ffi::c_void;
        fn FreeLibrary(module: *mut std::ffi::c_void) -> i32;
    }
    let system = std::path::PathBuf::from(std::env::var_os("SystemRoot").unwrap()).join("System32");
    let (fixture, kept) = installed();
    replace_package(&fixture, Some("0.8.1"), NEW_RUNTIME, NEW_PLUGIN);
    let held = fixture.package.join("vcruntime140.dll");
    std::fs::copy(system.join("version.dll"), &held).unwrap();
    std::fs::write(fixture.package.join("runtime/vcruntime140.dll"), b"new crt").unwrap();
    let name: Vec<u16> = held.as_os_str().encode_wide().chain(Some(0)).collect();
    // SAFETY: a NUL-terminated path; the module is released below.
    let module = unsafe { LoadLibraryW(name.as_ptr()) };
    assert!(!module.is_null());
    assert!(forge_base::fsutil::remove_file(&held).is_err(), "the loaded library is held");

    let result = refresh(&fixture).unwrap();
    // SAFETY: the handle LoadLibraryW returned.
    unsafe { FreeLibrary(module) };
    assert_eq!(result["status"], "updated");
    assert!(!held.exists(), "renamed aside");
    let stale: Vec<String> = std::fs::read_dir(&fixture.package)
        .unwrap()
        .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
        .filter(|name| name.starts_with(".vcruntime140.dll.") && name.ends_with(".stale"))
        .collect();
    assert_eq!(stale.len(), 1, "{stale:?}");
    assert!(matches!(state(&fixture), InstallationState::Ready { .. }));
    assert_kept(&kept);
}
