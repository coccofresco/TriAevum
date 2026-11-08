// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Repair in place (UX plan WP-04A) on a synthetic installation: an
//! interrupted activation, missing assets and a missing title come back to
//! `Ready` from the kept ROM data; missing or changed kept data needs the
//! ROM and is never silently reimported.

mod common;
#[path = "common/update.rs"]
mod update;

use std::path::PathBuf;

use common::{host, FakeRuntime, Fixture};
use forge_base::fsutil::atomic_write_json;
use forge_base::hashing::sha256_hex;
use forge_base::{CancelToken, ErrorKind, Reason, Result};
use forge_core::layout::HostOs;
use forge_core::repair::{repair_installation, RepairRequest};
use forge_core::state::{inspect, InspectRequest, InstallationState, RepairReason};
use forge_core::transaction::journal_path;
use serde_json::{json, Value};
use update::{assert_kept, config_files, customize_settings, sentinels, title_directory};

fn state(fixture: &Fixture) -> InstallationState {
    inspect(&InspectRequest::package(&fixture.package))
}

fn repair(fixture: &Fixture) -> Result<Value> {
    repair_installation(
        &RepairRequest::package(&fixture.package),
        &FakeRuntime::default(),
        &mut |_, _| {},
        &CancelToken::new(),
    )
}

fn installed() -> (Fixture, Vec<(PathBuf, Vec<u8>)>) {
    let fixture = Fixture::new();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    customize_settings(&fixture.data());
    let kept = sentinels(&fixture);
    (fixture, kept)
}

fn assert_ready(fixture: &Fixture) {
    assert!(matches!(state(fixture), InstallationState::Ready { .. }), "{:?}", state(fixture));
}

/// The journal and backups a Forge killed in the middle of publication
/// leaves, with its targets half written.
fn interrupt_activation(fixture: &Fixture) {
    let data = fixture.data();
    let title = title_directory(fixture);
    let targets = [
        fixture.package.join(host().title_module),
        fixture.launch_profile(),
        data.join("active-title.json"),
        title.join("forge-state.json"),
        data.join("config/TriAevum.json"),
        data.join("config/topscreen_ui.json"),
    ];
    let journal = journal_path(&fixture.package, HostOs::current());
    let directory = journal.parent().unwrap();
    let mut entries = Vec::new();
    for (index, target) in targets.iter().enumerate() {
        let target = forge_prepare::paths::resolve(target).unwrap();
        let digest = if target.is_file() {
            std::fs::copy(&target, directory.join(format!("{index}.bak"))).unwrap();
            json!(sha256_hex(&std::fs::read(&target).unwrap()))
        } else {
            Value::Null
        };
        entries.push(json!({"target": target.to_string_lossy(), "sha256": digest}));
    }
    atomic_write_json(&journal, &json!({"format": "triaevum_activation_v1", "entries": entries})).unwrap();
    for target in &targets[1..4] {
        std::fs::write(target, b"half-written").unwrap();
    }
}

#[test]
fn an_interrupted_activation_is_repaired_in_one_click() {
    let (fixture, kept) = installed();
    let settings = config_files(&fixture.data());
    interrupt_activation(&fixture);
    assert!(matches!(
        state(&fixture),
        InstallationState::RepairNeeded { reason: RepairReason::InterruptedActivation, .. }
    ));
    // The ROM is not needed.
    std::fs::remove_dir_all(&fixture.extracted).unwrap();
    let result = repair(&fixture).unwrap();
    assert_eq!(result["status"], "repaired");
    assert_eq!(result["route"], "in_place");
    assert_eq!(result["recovered_activation"], true);
    assert_eq!(result["config_changes"], json!([]));
    assert_ready(&fixture);
    assert_kept(&kept);
    assert_eq!(config_files(&fixture.data()), settings);
    assert!(!journal_path(&fixture.package, HostOs::current()).exists());
}

#[test]
fn missing_assets_and_preparation_caches_are_rebuilt() {
    let (fixture, kept) = installed();
    let data = fixture.data();
    let water = data.join("cache/water");
    assert!(water.is_dir(), "the fixture prepares water textures");
    std::fs::remove_dir_all(&water).unwrap();
    // The interior-sun cache is invalid too: its receipt no longer proves it
    // (only a title with an offline interior sun has one).
    let interior_sun = forge_catalog::title::ACTIVE.offline_interior_sun;
    let shells = data.join("cache/renderer/shadow-shells");
    if interior_sun {
        std::fs::remove_dir_all(&shells).unwrap();
    }
    assert!(matches!(state(&fixture), InstallationState::RepairNeeded { reason: RepairReason::ReceiptMismatch, .. }));

    let result = repair(&fixture).unwrap();
    assert_eq!(result["route"], "in_place");
    assert_eq!(result["recovered_activation"], false);
    common::assert_interior_sun(&result["build"], false, "rebuilt, not trusted");
    assert!(water.is_dir() && (shells.is_dir() || !interior_sun));
    assert_ready(&fixture);
    assert_kept(&kept);
}

#[test]
fn missing_kept_rom_data_needs_the_rom_and_changes_nothing() {
    let (fixture, kept) = installed();
    std::fs::remove_dir_all(fixture.data().join("sources")).unwrap();
    assert!(matches!(state(&fixture), InstallationState::NeedsGameInput { .. }));
    let error = repair(&fixture).unwrap_err();
    assert_eq!((error.kind, error.reason), (ErrorKind::InvalidInput, Some(Reason::MissingRetainedSource)));
    let explanation = forge_core::explain::explain(&error);
    assert_eq!(explanation.id, "source.needed");
    assert_eq!(explanation.primary_action.code(), "select_rom");
    // A damaged asset is not rebuilt from nothing either.
    let water = fixture.data().join("cache/water");
    std::fs::remove_dir_all(&water).unwrap();
    assert_eq!(repair(&fixture).unwrap_err().reason, Some(Reason::MissingRetainedSource));
    assert!(!water.exists(), "no silent rebuild");
    assert_kept(&kept);
}

#[test]
fn changed_kept_rom_data_needs_the_rom() {
    let (fixture, _) = installed();
    let title = title_directory(&fixture);
    let index = forge_base::fsutil::load_json_object(&title.join("content.tap")).unwrap();
    let romfs =
        forge_prepare::paths::resolve_reference(index["inputs"]["romfs"]["path"].as_str().unwrap(), &title).unwrap();
    let mut bytes = std::fs::read(&romfs).unwrap();
    bytes[0] ^= 0xFF;
    std::fs::write(&romfs, bytes).unwrap();
    let error = repair(&fixture).unwrap_err();
    assert_eq!(error.reason, Some(Reason::MissingRetainedSource));
    assert!(error.message.contains("romfs input changed after content preparation"), "{}", error.message);
}

#[test]
fn a_missing_title_is_prepared_again_from_the_kept_rom_data() {
    let (fixture, kept) = installed();
    let settings = config_files(&fixture.data());
    let title = title_directory(&fixture);
    std::fs::remove_file(fixture.data().join("active-title.json")).unwrap();
    std::fs::write(title.join("content.tap"), b"damaged").unwrap();
    std::fs::remove_dir_all(&fixture.extracted).unwrap();

    let result = repair(&fixture).unwrap();
    assert_eq!(result["route"], "kept_rom_data");
    assert_eq!(result["recipe"], common::RECIPE);
    assert_ready(&fixture);
    assert_kept(&kept);
    let changes: Vec<Value> = result["config_changes"].as_array().unwrap().clone();
    assert!(changes.iter().all(|change| change["change"] == "language_selected"), "{changes:?}");
    let topscreen = |files: &[(String, Vec<u8>)]| files.iter().find(|(name, _)| name == "topscreen_ui.json").cloned();
    assert_eq!(topscreen(&config_files(&fixture.data())), topscreen(&settings), "the layout is kept");
}

#[test]
fn a_running_game_blocks_the_repair() {
    let (fixture, _) = installed();
    let lock = forge_core::session::session_lock_path(&fixture.package, HostOs::current());
    let game = std::fs::File::create(&lock).unwrap();
    game.lock().unwrap();
    let error = repair(&fixture).unwrap_err();
    assert_eq!((error.kind, error.reason), (ErrorKind::Busy, Some(Reason::GameRunning)));
    assert_eq!(forge_core::explain::explain(&error).id, "session.game_running");
}

#[test]
fn repair_after_a_package_update_binds_the_new_package() {
    let (fixture, kept) = installed();
    update::replace_package(&fixture, Some("0.9.0"), b"fake runtime 3", b"MZ fake title plugin 3");
    assert!(matches!(state(&fixture), InstallationState::UpdateAvailable { .. }));
    let result = repair(&fixture).unwrap();
    assert_eq!(result["route"], "in_place");
    assert_ready(&fixture);
    assert_eq!(update::receipt(&fixture)["runtime_sha256"], sha256_hex(b"fake runtime 3"));
    assert_kept(&kept);
}
