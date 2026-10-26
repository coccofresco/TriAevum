// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The real `triaevum-update-helper` executable: Forge's handoff (inherited
//! process handle, readiness acknowledgement, waiting for Forge to exit),
//! a complete update of a synthetic package, and the recovery it performs
//! when started on its own after a crash. The Forge and new-engine roles
//! re-execute this test binary (`*_entry` tests, inert unless selected).

#![cfg(windows)]

use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant};

use forge_core::update::{hand_off, Program, Staged};
use forge_core::update_gate::DELEGATION_VARIABLE;
use forge_core::update_journal::{self, Faults};
use serde_json::{json, Value};

const HELPER: &str = env!("CARGO_BIN_EXE_triaevum-update-helper");
const ROLE_FORGE: &str = "HELPER_TEST_FORGE";
const STAGING: &str = "HELPER_TEST_STAGING";

fn sha(bytes: &[u8]) -> String {
    forge_core::hashing::sha256_hex(bytes)
}

fn write(path: &Path, bytes: &[u8]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, bytes).unwrap();
}

fn entry(name: &str) -> Value {
    json!({"program": std::env::current_exe().unwrap().to_string_lossy(),
           "arguments": [name, "--exact", "--nocapture", "--test-threads=1"]})
}

/// An installation with no prepared game and a staged update of two files.
fn staged_site(root: &Path) -> (PathBuf, PathBuf, Value) {
    let activation = root.join("TriAevum");
    let staging = activation.join(".update-0.8.1-1");
    write(&activation.join("docs/a.md"), b"old a");
    write(&activation.join("docs/gone.md"), b"old gone");
    write(&activation.join("data/savedata/save.bin"), b"save");
    write(&staging.join("package/docs/a.md"), b"new a");
    write(&staging.join("package/docs/b.md"), b"new b");
    let change = |path: &str, old: Option<&[u8]>, new: Option<&[u8]>| {
        json!({"path": path, "action": "replace",
               "old_sha256": old.map(sha), "old_bytes": old.map(<[u8]>::len),
               "new_sha256": new.map(sha), "new_bytes": new.map(<[u8]>::len)})
    };
    let proposal = json!({
        "format": "triaevum_update_proposal_v1",
        "id": "handoff-test",
        "activation": activation.to_string_lossy(),
        "data_root": activation.join("data").to_string_lossy(),
        "staging": staging.to_string_lossy(),
        "previous_version": "0.8.0",
        "version": "0.8.1",
        "authenticity": {"status": "signed", "key_id": "0000000000000000"},
        "installed_state": "not_installed",
        "recipe": null,
        "hud_layout": "keep",
        "allow_downgrade": false,
        "locally_modified": [],
        "changes": [
            change("docs/a.md", Some(b"old a"), Some(b"new a")),
            change("docs/b.md", None, Some(b"new b")),
            change("docs/gone.md", Some(b"old gone"), None),
        ],
        "rebind": entry("engine_entry"),
        "validate": entry("status_entry"),
        "relaunch": null,
    });
    (activation, staging, proposal)
}

/// Forge: hands off to the real helper and exits.
#[test]
fn forge_entry() {
    let (Ok(_), Ok(staging)) = (std::env::var(ROLE_FORGE), std::env::var(STAGING)) else { return };
    let staging = PathBuf::from(staging);
    let proposal: Value =
        serde_json::from_slice(&std::fs::read(staging.join("proposal.source.json")).unwrap()).unwrap();
    let staged = Staged { staging, proposal };
    let helper = Program { program: HELPER.into(), arguments: Vec::new() };
    let result = hand_off(&staged, &helper, &Faults::none()).unwrap();
    assert_eq!(result["status"], "handed_off");
    std::process::exit(0);
}

/// The new engine's refresh: nothing installed to bind.
#[test]
fn engine_entry() {
    if std::env::var(DELEGATION_VARIABLE).is_err() {
        return;
    }
    println!("{}", json!({"status": "unchanged", "reason": "not_installed"}));
    std::process::exit(0);
}

/// The new engine's status.
#[test]
fn status_entry() {
    if std::env::var(DELEGATION_VARIABLE).is_err() {
        return;
    }
    println!("{}", json!({"format": "triaevum_installation_state_v1", "state": "not_installed"}));
    std::process::exit(0);
}

#[test]
fn the_real_helper_updates_after_forge_exits() {
    let directory = tempfile::tempdir().unwrap();
    let (activation, staging, proposal) = staged_site(directory.path());
    std::fs::write(staging.join("proposal.source.json"), proposal.to_string()).unwrap();
    let status = Command::new(std::env::current_exe().unwrap())
        .args(["forge_entry", "--exact", "--nocapture", "--test-threads=1"])
        .env(ROLE_FORGE, "1")
        .env(STAGING, &staging)
        .status()
        .unwrap();
    assert!(status.success(), "the Forge role could not hand off: {status:?}");
    // The helper runs on after Forge exited; its result marks the end.
    let result_path = update_journal::result_path(&activation);
    let started = Instant::now();
    while !result_path.is_file() || forge_core::update_gate::held(&activation, forge_core::layout::HostOs::Windows) {
        assert!(started.elapsed() < Duration::from_secs(60), "the helper did not finish");
        std::thread::sleep(Duration::from_millis(100));
    }
    let result: Value = serde_json::from_slice(&std::fs::read(&result_path).unwrap()).unwrap();
    assert_eq!(result["status"], "updated", "{result}");
    assert_eq!(std::fs::read(activation.join("docs/a.md")).unwrap(), b"new a");
    assert_eq!(std::fs::read(activation.join("docs/b.md")).unwrap(), b"new b");
    assert!(!activation.join("docs/gone.md").exists());
    assert_eq!(std::fs::read(activation.join("data/savedata/save.bin")).unwrap(), b"save");
    assert!(!forge_core::update_gate::pending(&activation));
}

#[test]
fn started_alone_it_rolls_back_an_interrupted_update() {
    let directory = tempfile::tempdir().unwrap();
    let (activation, staging, proposal) = staged_site(directory.path());
    // A crash right after the first file was replaced: journal, old bytes
    // kept, new file in place.
    let mut journal = proposal.as_object().unwrap().clone();
    journal.insert("format".into(), json!("triaevum_update_journal_v1"));
    journal.insert("state".into(), json!("replacing"));
    journal.insert("root_runtime".into(), json!({"path": "TriAevum.exe", "sha256": null}));
    write(&update_journal::old_path(&activation, "docs/a.md"), b"old a");
    std::fs::rename(staging.join("package/docs/a.md"), activation.join("docs/a.md")).unwrap();
    write(&forge_core::update_gate::journal_path(&activation), Value::Object(journal).to_string().as_bytes());
    let helper = staging.join("triaevum-update-helper.exe");
    std::fs::copy(HELPER, &helper).unwrap();

    let status = Command::new(&helper).env_remove("TRIAEVUM_UPDATE_PROPOSAL").status().unwrap();
    assert_eq!(status.code(), Some(forge_core::update_helper::EXIT_ROLLED_BACK));
    assert_eq!(std::fs::read(activation.join("docs/a.md")).unwrap(), b"old a");
    assert_eq!(std::fs::read(activation.join("docs/gone.md")).unwrap(), b"old gone");
    assert!(!activation.join("docs/b.md").exists());
    assert!(!forge_core::update_gate::pending(&activation));
    let result: Value =
        serde_json::from_slice(&std::fs::read(update_journal::result_path(&activation)).unwrap()).unwrap();
    assert_eq!(result["status"], "rolled_back");
    // Nothing pending: it says so and changes nothing.
    let again = Command::new(&helper).status().unwrap();
    assert_eq!(again.code(), Some(0));
}
