// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Windows game is a GUI-subsystem program (owner decision 12, UX plan
//! WP-03). Forge still runs it the way it always did: the product query reads
//! its standard output through a pipe, and a failed run's error output and
//! exit code reach the caller. Set TRIAEVUM_GUI_RUNTIME to a built
//! TriAevum.exe to run this check; it is skipped otherwise.

use std::ffi::OsString;
use std::process::Stdio;

use forge_native::process::{popen_native, return_code, run_native, Environment, RunOptions};

#[test]
fn forge_reads_a_gui_runtime_through_pipes() {
    let Some(runtime) = std::env::var_os("TRIAEVUM_GUI_RUNTIME") else {
        eprintln!("skipped: TRIAEVUM_GUI_RUNTIME names no runtime");
        return;
    };
    let directory = tempfile::tempdir().unwrap();
    let options = RunOptions {
        cwd: directory.path().to_path_buf(),
        environment: Environment::Inherit,
        timeout_seconds: 120.0,
        creation_flags: 0,
    };
    let product = run_native(&[runtime.clone(), "--product-info".into()], &options, None).unwrap();
    assert_eq!(product.status, 0);
    let info: serde_json::Value = serde_json::from_slice(&product.stdout).unwrap();
    assert!(info.get("capabilities").is_some(), "{info}");

    // A run that fails before any window: Play's capture of the error output.
    let mut environment: Vec<(OsString, OsString)> = std::env::vars_os().collect();
    environment.push(("TRIAEVUM_NO_FATAL_DIALOG".into(), "1".into()));
    let options = RunOptions { environment: Environment::Replace(environment), ..options };
    let stderr = std::fs::File::create(directory.path().join("game-stderr.log")).unwrap();
    let program: Vec<OsString> = vec![
        runtime,
        "--a32-process-manifest".into(),
        directory.path().join("missing.json").into(),
        "--resource-root".into(),
        directory.path().into(),
    ];
    let mut child = popen_native(&program, &options, Stdio::null(), Stdio::from(stderr)).unwrap();
    let code = return_code(child.wait().unwrap());
    assert_eq!(code, 1);
    let captured = std::fs::read_to_string(directory.path().join("game-stderr.log")).unwrap();
    assert!(captured.lines().any(|line| line.starts_with("TRIAEVUM_FATAL ")), "{captured}");
}
