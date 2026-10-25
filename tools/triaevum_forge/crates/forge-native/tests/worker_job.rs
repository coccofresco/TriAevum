// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Port of test_worker_job.py: the worker exits when its owner dies, and on
//! Windows killing the worker kills its descendants (Job Object).

mod common;

use std::io::{BufRead, BufReader, Read};
use std::process::{Command, Stdio};
use std::sync::mpsc;
use std::time::{Duration, Instant};

use common::install_fake;
use serde_json::json;

#[cfg(any(windows, target_os = "linux"))]
#[test]
fn owner_exit_terminates_worker_without_direct_kill() {
    let directory = tempfile::tempdir().unwrap();
    let owner_exe = install_fake(directory.path(), "owner", &json!({"sleep_ms": 60000}));
    let mut owner = Command::new(&owner_exe).spawn().unwrap();
    let worker_exe = install_fake(directory.path(), "worker", &json!({"worker_owner": owner.id()}));
    let mut worker = Command::new(&worker_exe).stdout(Stdio::piped()).spawn().unwrap();
    let mut lines = BufReader::new(worker.stdout.take().unwrap()).lines();
    assert_eq!(lines.next().unwrap().unwrap(), "watching");
    owner.kill().unwrap();
    owner.wait().unwrap();
    let deadline = Instant::now() + Duration::from_secs(10);
    let status = loop {
        if let Some(status) = worker.try_wait().unwrap() {
            break status;
        }
        if Instant::now() > deadline {
            worker.kill().unwrap();
            panic!("worker survived its owner");
        }
        std::thread::sleep(Duration::from_millis(20));
    };
    assert_eq!(status.code(), Some(forge_native::worker_job::OWNER_EXIT_STATUS));
}

#[cfg(windows)]
#[test]
fn killing_worker_closes_descendant_inherited_pipe() {
    let directory = tempfile::tempdir().unwrap();
    let worker_exe = install_fake(directory.path(), "worker", &json!({"spawn_sleeper": true}));
    let mut worker = Command::new(&worker_exe).stdout(Stdio::piped()).spawn().unwrap();
    let mut stdout = BufReader::new(worker.stdout.take().unwrap());
    let mut line = String::new();
    stdout.read_line(&mut line).unwrap();
    assert_eq!(line.trim(), "child-started");
    worker.kill().unwrap();
    worker.wait().unwrap();
    // The sleeper inherits stdout: EOF is impossible while it survives.
    let (sender, receiver) = mpsc::channel();
    std::thread::spawn(move || {
        let mut rest = Vec::new();
        let _ = stdout.read_to_end(&mut rest);
        let _ = sender.send(());
    });
    receiver.recv_timeout(Duration::from_secs(10)).expect("descendant kept the pipe open");
}

#[test]
fn invalid_owner_is_rejected() {
    for pid in [0, -1, i64::from(std::process::id())] {
        let error = forge_native::watch_owner(pid).unwrap_err();
        assert_eq!(error.message, "Invalid worker owner PID");
    }
}
