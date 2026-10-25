// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Python and Rust Forges exclude each other through the activation lock and
//! recover each other's interrupted activations. Runs when
//! TRIAEVUM_FORGE_PYTHON names a Python 3 interpreter.

use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

use forge_core::layout::HostOs;
use forge_core::transaction::{activation_transaction, journal_path};
use forge_core::{lock, ErrorKind, ForgeError};

const CRASH_HELPER: &str = "TRIAEVUM_FORGE_CRASH_HELPER";

fn python() -> Option<String> {
    std::env::var("TRIAEVUM_FORGE_PYTHON").ok()
}

fn release_tools() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../triaevum_release")
}

fn script() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/transaction_jobs.py")
}

fn run_python(python: &str, args: &[&str]) -> std::process::Output {
    Command::new(python).arg("-I").arg(script()).arg(release_tools()).args(args).output().unwrap()
}

fn layout(root: &Path) -> (PathBuf, Vec<PathBuf>) {
    let installation = root.to_path_buf();
    let existing = installation.join("data/active-title.json");
    std::fs::create_dir_all(existing.parent().unwrap()).unwrap();
    std::fs::write(&existing, b"previous title").unwrap();
    (installation.clone(), vec![existing, installation.join("data/TriAevum.launch.json")])
}

fn os() -> HostOs {
    HostOs::current()
}

#[test]
fn locks_exclude_each_other() {
    let Some(python) = python() else { return eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set") };
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join(".forge-activation.lock");
    let held = lock::acquire(&path).unwrap();
    let output = run_python(&python, &["try", path.to_str().unwrap()]);
    assert_eq!(String::from_utf8_lossy(&output.stdout).trim(), "busy");
    drop(held);

    let mut child = Command::new(&python)
        .arg("-I")
        .arg(script())
        .arg(release_tools())
        .args(["hold", path.to_str().unwrap()])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    let mut line = String::new();
    BufReader::new(child.stdout.as_mut().unwrap()).read_line(&mut line).unwrap();
    assert_eq!(line.trim(), "held");
    assert_eq!(lock::acquire(&path).unwrap_err().kind, ErrorKind::Busy);
    drop(child.stdin.take());
    child.wait().unwrap();
    assert!(lock::acquire(&path).is_ok());
}

#[test]
fn rust_recovers_a_python_crash() {
    let Some(python) = python() else { return eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set") };
    let directory = tempfile::tempdir().unwrap();
    let (installation, targets) = layout(directory.path());
    let mut args = vec!["crash", installation.to_str().unwrap()];
    args.extend(targets.iter().map(|target| target.to_str().unwrap()));
    let output = run_python(&python, &args);
    assert_eq!(output.status.code(), Some(3));
    assert!(journal_path(&installation, os()).exists());
    activation_transaction(&installation, &targets, os(), || Ok(())).unwrap();
    assert_eq!(std::fs::read(&targets[0]).unwrap(), b"previous title");
    assert!(!targets[1].exists());
    assert!(!journal_path(&installation, os()).exists());
}

/// Child process body for `python_recovers_a_rust_crash`.
#[test]
fn crash_helper() {
    let Ok(root) = std::env::var(CRASH_HELPER) else { return };
    let (installation, targets) = layout(Path::new(&root));
    let _: Result<(), ForgeError> = activation_transaction(&installation, &targets, os(), || {
        for target in &targets {
            std::fs::write(target, b"half-written by rust").unwrap();
        }
        std::process::exit(3)
    });
}

#[test]
fn python_recovers_a_rust_crash() {
    let Some(python) = python() else { return eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set") };
    let directory = tempfile::tempdir().unwrap();
    let status = Command::new(std::env::current_exe().unwrap())
        .args(["--exact", "crash_helper", "--nocapture", "--test-threads=1"])
        .env(CRASH_HELPER, directory.path())
        .status()
        .unwrap();
    assert_eq!(status.code(), Some(3));
    let installation = directory.path().to_path_buf();
    let targets = [installation.join("data/active-title.json"), installation.join("data/TriAevum.launch.json")];
    assert!(journal_path(&installation, os()).exists());
    let mut args = vec!["commit", installation.to_str().unwrap()];
    args.extend(targets.iter().map(|target| target.to_str().unwrap()));
    let output = run_python(&python, &args);
    assert_eq!(String::from_utf8_lossy(&output.stdout).trim(), "committed", "{}", String::from_utf8_lossy(&output.stderr));
    assert_eq!(std::fs::read(&targets[0]).unwrap(), b"previous title");
    assert!(!targets[1].exists());
}
