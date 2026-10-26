// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `update` (UX plan WP-04B) as the command line reports it. The planning,
//! staging, helper and kill matrix are tested in forge-core
//! (`tests/update.rs`) and forge-update-helper.

#![cfg(windows)]

use std::process::Command;

use serde_json::{json, Value};

fn forge(arguments: &[&str]) -> (bool, Value) {
    let output = Command::new(env!("CARGO_BIN_EXE_triaevum-forge"))
        .args(arguments)
        .env_remove("TRIAEVUM_FORGE_ALLOW_UNSIGNED_UPDATE")
        .output()
        .unwrap();
    let stdout = String::from_utf8(output.stdout).unwrap();
    (output.status.success(), serde_json::from_str(&stdout).unwrap_or_else(|_| panic!("not JSON: {stdout}")))
}

#[test]
fn update_reports_as_json() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("TriAevum");
    std::fs::create_dir_all(&root).unwrap();
    let root = root.to_string_lossy().into_owned();
    assert_eq!(forge(&["update", "--package-root", &root, "--recover"]), (true, json!({"recovered": null})));
    assert_eq!(forge(&["update", "--package-root", &root, "--result"]), (true, json!({"result": null})));
    let (success, result) = forge(&["update"]);
    assert!(!success);
    assert!(result["error"].as_str().unwrap().starts_with("Choose the downloaded TriAevum ZIP"), "{result}");

    // A folder that is no TriAevum installation cannot be updated.
    let zip = directory.path().join("download.zip");
    std::fs::write(&zip, b"not a zip").unwrap();
    let zip = zip.to_string_lossy().into_owned();
    let (success, result) = forge(&["update", &zip, "--package-root", &root, "--plan"]);
    assert!(!success);
    assert!(result["error"].as_str().unwrap().contains("release inventory"), "{result}");
    assert!(std::fs::read_dir(&root).unwrap().next().is_none(), "nothing was written");
}
