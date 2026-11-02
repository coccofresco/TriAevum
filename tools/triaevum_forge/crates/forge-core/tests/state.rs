// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Installation state matrix: synthetic installations (made by the real
//! import with a fake runtime) for every state, each inspected without a
//! single change to the fixture directory.

mod common;

use std::path::{Path, PathBuf};

use common::{host, write, FakeRuntime, Fixture, RECIPE};
use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::hashing::sha256_hex;
use forge_core::abi::{TriAevumForgeFreeBuffer, TriAevumForgeInspect};
use forge_core::install::{AndroidTarget, InstallTarget};
use forge_core::layout::HostOs;
use forge_core::state::{inspect, IncompatibleReason, InspectRequest, InstallationState, RepairReason};
use forge_core::transaction::{installation_lock, journal_path};
use serde_json::{json, Value};

/// Every file under `root` with its digest, and every folder.
fn tree(root: &Path) -> Vec<(String, String)> {
    fn walk(root: &Path, directory: &Path, out: &mut Vec<(String, String)>) {
        for entry in std::fs::read_dir(directory).unwrap() {
            let path = entry.unwrap().path();
            let relative = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
            if path.is_dir() {
                out.push((relative, "dir".into()));
                walk(root, &path, out);
            } else {
                // A held Windows lock makes its own file unreadable.
                let digest = std::fs::read(&path).map_or_else(|_| "locked".to_string(), |bytes| sha256_hex(&bytes));
                out.push((relative, digest));
            }
        }
    }
    let mut out = Vec::new();
    walk(root, root, &mut out);
    out.sort();
    out
}

fn root_of(fixture: &Fixture) -> PathBuf {
    fixture.package.parent().unwrap().to_path_buf()
}

fn request(fixture: &Fixture) -> InspectRequest {
    match &fixture.android {
        None => InspectRequest::package(&fixture.package),
        Some((native, profile)) => InspectRequest {
            package_root: fixture.package.clone(),
            data_root: None,
            target: InstallTarget::Android(AndroidTarget {
                native_library_dir: native.clone(),
                profile_root: profile.clone(),
            }),
            verify_sources: true,
        },
    }
}

/// Inspects and proves that nothing under the fixture changed.
fn inspected(fixture: &Fixture) -> InstallationState {
    let before = tree(&root_of(fixture));
    let state = inspect(&request(fixture));
    assert_eq!(tree(&root_of(fixture)), before, "inspection wrote to the installation");
    state
}

fn installed() -> Fixture {
    let fixture = Fixture::new();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    fixture
}

fn catalog_path(fixture: &Fixture) -> PathBuf {
    fixture.package.join("recipes/precompiled-titles.json")
}

fn edit_json(path: &Path, edit: impl FnOnce(&mut Value)) {
    let mut value = Value::Object(load_json_object(path).unwrap());
    edit(&mut value);
    std::fs::write(path, value.to_string()).unwrap();
}

fn title_state(fixture: &Fixture) -> PathBuf {
    let active = load_json_object(&fixture.data().join("active-title.json")).unwrap();
    forge_prepare::paths::resolve_reference(active["directory"].as_str().unwrap(), &fixture.data())
        .unwrap()
        .join("forge-state.json")
}

#[test]
fn a_fresh_package_is_not_installed() {
    let fixture = Fixture::new();
    assert_eq!(inspected(&fixture), InstallationState::NotInstalled);
}

#[test]
fn an_installed_title_is_ready_with_its_language_and_version() {
    let fixture = installed();
    let state = inspected(&fixture);
    assert_eq!(
        state,
        InstallationState::Ready { recipe: RECIPE.into(), package_version: None, language: Some("en".into()) }
    );
    write(
        &fixture.package.join("release-manifest.json"),
        json!({"format": "triaevum_public_release_manifest_v1", "release": {"version": "0.8.0-test"}})
            .to_string()
            .as_bytes(),
    );
    let InstallationState::Ready { package_version, .. } = inspected(&fixture) else { panic!("not ready") };
    assert_eq!(package_version.as_deref(), Some("0.8.0-test"));
    assert_eq!(state.to_value()["state"], "ready");
    assert!(state.explanation().is_none());
}

#[test]
fn a_left_journal_is_an_interrupted_activation() {
    let fixture = installed();
    write(&journal_path(&fixture.package, HostOs::current()), b"{}");
    let state = inspected(&fixture);
    assert!(matches!(state, InstallationState::RepairNeeded { reason: RepairReason::InterruptedActivation, .. }));
    assert_eq!(state.explanation().unwrap().id, "recovery_required");
}

#[test]
fn a_held_activation_lock_is_preparing() {
    let fixture = installed();
    let lock = installation_lock(&fixture.package, HostOs::current()).unwrap();
    assert_eq!(inspected(&fixture), InstallationState::Preparing);
    drop(lock);
    assert!(matches!(inspected(&fixture), InstallationState::Ready { .. }));
}

#[test]
fn a_removed_runtime_needs_repair() {
    let fixture = installed();
    std::fs::remove_file(fixture.package.join(host().installed_runtime)).unwrap();
    let state = inspected(&fixture);
    assert!(matches!(state, InstallationState::RepairNeeded { reason: RepairReason::RuntimeMissing, .. }), "{state:?}");
}

#[test]
fn a_changed_receipt_hash_is_a_receipt_mismatch() {
    let fixture = installed();
    edit_json(&title_state(&fixture), |state| state["runtime"]["plugin_sha256"] = json!("0".repeat(64)));
    let state = inspected(&fixture);
    let InstallationState::RepairNeeded { reason: RepairReason::ReceiptMismatch, details } = &state else {
        panic!("{state:?}")
    };
    if host() == forge_catalog::MACOS {
        assert_eq!(details, "The installed title receipt changed; run Forge again");
    } else {
        assert!(details.starts_with("Installed runtime identity mismatch"), "{details}");
    }
    assert_eq!(state.to_value()["reason"]["kind"], "receipt_mismatch");
}

#[test]
fn a_truncated_setting_is_unreadable() {
    let fixture = installed();
    let config = fixture.data().join("config/TriAevum.json");
    let bytes = std::fs::read(&config).unwrap();
    std::fs::write(&config, &bytes[..bytes.len() / 2]).unwrap();
    let state = inspected(&fixture);
    assert!(
        matches!(&state, InstallationState::RepairNeeded { reason: RepairReason::ConfigUnreadable { .. }, .. }),
        "{state:?}"
    );
    let value = state.to_value();
    assert_eq!(value["reason"], json!({"kind": "config_unreadable", "file": "config/TriAevum.json"}));
    assert_eq!(value["explanation"]["id"], "settings_load.malformed");
}

#[test]
fn removed_sources_need_the_game_again() {
    let fixture = installed();
    std::fs::remove_dir_all(fixture.data().join("sources")).unwrap();
    let state = inspected(&fixture);
    assert!(matches!(&state, InstallationState::NeedsGameInput { recipe, .. } if recipe == RECIPE), "{state:?}");
    assert_eq!(state.explanation().unwrap().id, "source.needed");
}

/// The other desktop target, with consistent runtime and module bindings.
#[test]
fn a_package_for_another_target_is_incompatible() {
    let fixture = installed();
    let other =
        if host().target == forge_catalog::WINDOWS.target { forge_catalog::LINUX } else { forge_catalog::WINDOWS };
    edit_json(&catalog_path(&fixture), |catalog| {
        catalog["target"] = json!(other.target);
        catalog["runtime"]["path"] = json!(other.runtime);
        catalog["native_module"]["path"] = json!(other.native_module);
        catalog["titles"][0]["target"] = json!(other.target);
    });
    let state = inspected(&fixture);
    assert!(
        matches!(state, InstallationState::Incompatible { reason: IncompatibleReason::TargetMismatch, .. }),
        "{state:?}"
    );
    assert_eq!(state.explanation().unwrap().id, "incompatible_target");
}

#[test]
fn a_package_with_another_runtime_offers_an_update() {
    let fixture = installed();
    let runtime = b"fake runtime, next release";
    write(&fixture.package.join(host().runtime), runtime);
    edit_json(&catalog_path(&fixture), |catalog| {
        catalog["runtime"]["sha256"] = json!(sha256_hex(runtime));
        catalog["runtime"]["bytes"] = json!(runtime.len());
    });
    let state = inspected(&fixture);
    let InstallationState::UpdateAvailable { recipe, installed_package_identity, present_package_identity } = &state
    else {
        panic!("{state:?}")
    };
    assert_eq!(recipe, RECIPE);
    assert_eq!(installed_package_identity.runtime_sha256, sha256_hex(b"fake runtime"));
    assert_eq!(present_package_identity.runtime_sha256, sha256_hex(runtime));
}

#[test]
fn missing_catalogue_and_title_module_are_incompatible_not_uninstalled() {
    let fresh = Fixture::new();
    std::fs::remove_file(catalog_path(&fresh)).unwrap();
    let state = inspected(&fresh);
    assert!(
        matches!(state, InstallationState::Incompatible { reason: IncompatibleReason::MissingCatalogue, .. }),
        "{state:?}"
    );
    assert_eq!(state.explanation().unwrap().id, "package.component_missing");

    let fixture = installed();
    std::fs::remove_file(fixture.package.join(format!("titles/{RECIPE}/{}", host().title_module))).unwrap();
    let state = inspected(&fixture);
    assert!(
        matches!(state, InstallationState::Incompatible { reason: IncompatibleReason::MissingTitleModule, .. }),
        "{state:?}"
    );
}

#[test]
fn json_requests_through_the_c_abi() {
    let fixture = installed();
    let request = json!({"format": "triaevum_forge_inspect_v1", "package_root": fixture.package.to_string_lossy()});
    let bytes = request.to_string().into_bytes();
    let before = tree(&root_of(&fixture));
    let buffer = unsafe { TriAevumForgeInspect(bytes.as_ptr(), bytes.len()) };
    let answer: Value = serde_json::from_slice(unsafe { std::slice::from_raw_parts(buffer.data, buffer.len) }).unwrap();
    unsafe { TriAevumForgeFreeBuffer(buffer) };
    assert_eq!(tree(&root_of(&fixture)), before);
    assert_eq!(answer, inspect(&InspectRequest::package(&fixture.package)).to_value());
    assert_eq!(answer["format"], "triaevum_installation_state_v1");
    assert_eq!(answer["state"], "ready");
    assert_eq!(answer["can_play"], true);

    let refused = forge_core::state::inspect_bytes(b"{\"format\": \"other\"}");
    let refused: Value = serde_json::from_slice(&refused).unwrap();
    assert_eq!(refused["state"], "failed");
    assert_eq!(refused["explanation"]["id"], "request.invalid");
}

// The only test of this binary that names a native library folder: the
// folder is fixed for the process lifetime.
#[test]
fn android_installations_are_ready_then_updated_by_a_new_apk() {
    let fixture = Fixture::android();
    assert_eq!(inspected(&fixture), InstallationState::NotInstalled);
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    let state = inspected(&fixture);
    assert!(matches!(&state, InstallationState::Ready { recipe, .. } if recipe == RECIPE), "{state:?}");

    let (native, _) = fixture.android.clone().unwrap();
    let runtime = b"fake runtime from the next APK";
    std::fs::write(native.join("libTriAevum.so"), runtime).unwrap();
    edit_json(&catalog_path(&fixture), |catalog| {
        catalog["runtime"]["sha256"] = json!(sha256_hex(runtime));
        catalog["runtime"]["bytes"] = json!(runtime.len());
    });
    let state = inspected(&fixture);
    assert!(matches!(state, InstallationState::UpdateAvailable { .. }), "{state:?}");

    atomic_write_json(&fixture.data().join("config/TriAevum.android.json"), &json!({"Graphics": {}})).unwrap();
    std::fs::write(fixture.data().join("config/topscreen_ui.json"), b"{").unwrap();
    let value = inspected(&fixture).to_value();
    assert_eq!(value["reason"]["file"], "config/topscreen_ui.json");
}
