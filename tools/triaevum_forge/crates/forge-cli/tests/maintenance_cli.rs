// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `refresh` and `repair` (UX plan WP-04A) as the command line reports them:
//! the job result as JSON, a failure with its message. The engine itself is
//! tested in forge-core (`tests/refresh.rs`, `tests/repair.rs`).

use std::process::Command;

use serde_json::Value;

fn forge(arguments: &[&str]) -> (bool, Value) {
    let output = Command::new(env!("CARGO_BIN_EXE_triaevum-forge")).args(arguments).output().unwrap();
    let stdout = String::from_utf8(output.stdout).unwrap();
    (output.status.success(), serde_json::from_str(&stdout).unwrap_or_else(|_| panic!("not JSON: {stdout}")))
}

#[test]
fn refresh_and_repair_report_as_json() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().to_string_lossy().into_owned();
    // A folder without a publisher catalog is a developer installation.
    let (success, result) = forge(&["refresh", "--package-root", &root]);
    assert!(success, "{result}");
    assert_eq!(result, serde_json::json!({"status": "unchanged", "reason": "developer"}));
    let (success, result) = forge(&["repair", "--package-root", &root]);
    assert!(!success);
    assert_eq!(result["status"], "error");
    let (success, result) = forge(&["repair", "--package-root", &root, "--allow-downgrade"]);
    assert!(!success);
    assert_eq!(result["error"], "--allow-downgrade belongs to refresh");
}
