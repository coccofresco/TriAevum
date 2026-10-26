// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The command line as a consumer of the shared explanation fixtures and of
//! the installation state: `explain` and `status` print what the engine (and
//! so the C ABI and the Android installer) answers.

use std::path::Path;
use std::process::Command;

use serde_json::{json, Value};

const FIXTURES: &str = include_str!("../../forge-core/tests/fixtures/explain_events.json");

fn forge(arguments: &[&str]) -> (bool, Value) {
    let output = Command::new(env!("CARGO_BIN_EXE_triaevum-forge")).args(arguments).output().unwrap();
    let stdout = String::from_utf8(output.stdout).unwrap();
    (output.status.success(), serde_json::from_str(&stdout).unwrap_or_else(|_| panic!("not JSON: {stdout}")))
}

#[test]
fn explain_prints_the_engine_explanation_of_every_fixture() {
    let directory = tempfile::tempdir().unwrap();
    let document: Value = serde_json::from_str(FIXTURES).unwrap();
    for (index, case) in document["cases"].as_array().unwrap().iter().enumerate() {
        let input = directory.path().join(format!("{index}.json"));
        std::fs::write(&input, serde_json::to_vec(&case["input"]).unwrap()).unwrap();
        let (success, printed) = forge(&["explain", &input.to_string_lossy()]);
        let reference: Value =
            serde_json::from_slice(&forge_core::explain::explain_bytes(&serde_json::to_vec(&case["input"]).unwrap()))
                .unwrap();
        assert_eq!(printed, reference, "{}", case["name"]);
        assert_eq!(printed["id"].as_str(), case["id"].as_str(), "{}", case["name"]);
        assert_eq!(success, !case["id"].is_null(), "{}", case["name"]);
    }
}

/// A package with a catalog for this host and its runtime, nothing installed.
fn package(root: &Path) {
    let platform = forge_core::layout::host_platform().unwrap();
    let write = |relative: &str, bytes: &[u8]| {
        let path = root.join(relative);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, bytes).unwrap();
    };
    write("recipes/oot3d.json", b"{\"format\": \"triaevum_supported_revisions_v1\", \"recipes\": []}");
    let catalog = json!({
        "format": "triaevum_precompiled_titles_v1",
        "install_model": "precompiled_title_rom_import_v1",
        "target": platform.target,
        "runtime": {"path": platform.runtime, "bytes": 1, "sha256": "0".repeat(64)},
        "native_module": {"path": platform.native_module, "bytes": 1, "sha256": "0".repeat(64)},
        "titles": [{"recipe": "fixture"}],
    });
    write("recipes/precompiled-titles.json", catalog.to_string().as_bytes());
    write(platform.runtime, b"r");
}

#[test]
fn status_prints_the_installation_state() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let (success, printed) = forge(&["status", "--package-root", &root.to_string_lossy()]);
    assert!(success);
    assert_eq!(printed["state"], "incompatible");
    assert_eq!(printed["reason"]["kind"], "missing_catalogue");
    assert_eq!(printed["explanation"]["id"], "package.component_missing");

    package(root);
    let (_, printed) = forge(&["status", "--package-root", &root.to_string_lossy()]);
    let reference = forge_core::state::inspect(&forge_core::state::InspectRequest::package(root)).to_value();
    assert_eq!(printed, reference);
    assert_eq!(printed, json!({"format": "triaevum_installation_state_v1", "state": "not_installed"}));

    let journal = forge_core::transaction::journal_path(root, forge_core::layout::HostOs::current());
    std::fs::create_dir_all(journal.parent().unwrap()).unwrap();
    std::fs::write(&journal, b"{}").unwrap();
    let (_, printed) = forge(&["status", "--package-root", &root.to_string_lossy()]);
    assert_eq!(printed["state"], "repair_needed");
    assert_eq!(printed["reason"]["kind"], "interrupted_activation");
}

#[test]
fn inventory_prints_the_engine_inventory_and_a_running_game() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    package(root);
    std::fs::create_dir_all(root.join("data").join("savedata")).unwrap();
    std::fs::write(root.join("data").join("savedata").join("save.bin"), b"12345").unwrap();
    let (success, printed) = forge(&["inventory", "--package-root", &root.to_string_lossy()]);
    assert!(success);
    let reference = forge_core::inventory::inventory(root).unwrap().to_value();
    assert_eq!(printed, reference);
    assert_eq!(printed["format"], "triaevum_inventory_v1");
    assert_eq!(printed["activity"], json!({"game_running": false, "forge_running": false}));
    let save_data = printed["entries"].as_array().unwrap().iter().find(|entry| entry["logical_name"] == "save_data");
    assert_eq!(save_data.unwrap()["size"], 5);

    let os = forge_core::layout::HostOs::current();
    let lock = forge_core::session::session_lock_path(root, os);
    std::fs::create_dir_all(lock.parent().unwrap()).unwrap();
    let game = std::fs::File::create(&lock).unwrap();
    game.lock().unwrap();
    let (success, printed) = forge(&["inventory", "--no-sizes", "--package-root", &root.to_string_lossy()]);
    assert!(success);
    assert_eq!(printed["activity"]["game_running"], true);
    assert_eq!(printed["activity"]["explanation"]["id"], "session.game_running");
}

#[test]
fn diagnostics_prints_the_report_and_saves_the_bundle() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    package(root);
    let bundle = root.join("out").join("bundle.zip");
    let (success, printed) =
        forge(&["diagnostics", "--package-root", &root.to_string_lossy(), "--bundle", &bundle.to_string_lossy()]);
    assert!(success, "{printed}");
    assert_eq!(printed["format"], "triaevum_diagnostics_v1");
    assert!(printed["text"].as_str().unwrap().starts_with("TriAevum diagnostics, "));
    assert!(printed["text"].as_str().unwrap().contains("Installation: not_installed"));
    assert!(bundle.is_file());
    assert_eq!(printed["bundle"]["members"][0], "diagnostics.txt");
}
