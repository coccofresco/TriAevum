// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `backup` and `restore` on a synthetic portable installation: the zip,
//! the preview, the journaled restore and the recovery, as JSON.

use std::path::Path;
use std::process::Command;

use serde_json::Value;

fn forge(arguments: &[&str]) -> (bool, Value) {
    let output = Command::new(env!("CARGO_BIN_EXE_triaevum-forge")).args(arguments).output().unwrap();
    let stdout = String::from_utf8(output.stdout).unwrap();
    (output.status.success(), serde_json::from_str(&stdout).unwrap_or_else(|_| panic!("not JSON: {stdout}")))
}

fn write(path: &Path, bytes: &[u8]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, bytes).unwrap();
}

#[test]
fn backup_preview_restore_and_recover() {
    let directory = tempfile::tempdir().unwrap();
    let package = directory.path().join("TriAevum");
    let data = package.join("data");
    write(&data.join("savedata/system/save00.bin"), b"first");
    write(&data.join("config/TriAevum.json"), b"{}");
    write(&package.join("recipes/oot3d.json"),
        br#"{"format":"triaevum_supported_revisions_v1","recipes":[{"id":"fixture","game":"oot3d"}]}"#);
    let zip = directory.path().join("backup.zip");
    let root = package.to_string_lossy().into_owned();
    let zip_text = zip.to_string_lossy().into_owned();

    let (success, result) = forge(&["backup", &zip_text, "--package-root", &root]);
    assert!(success, "{result}");
    assert_eq!(result["format"], "triaevum_backup_result_v1");
    assert!(result["units"].as_array().unwrap().iter().any(|unit| unit == "save_data"));

    write(&data.join("savedata/system/save00.bin"), b"second");
    let (success, preview) = forge(&["restore", &zip_text, "--preview", "--package-root", &root]);
    assert!(success, "{preview}");
    assert_eq!(preview["format"], "triaevum_restore_preview_v1");
    let saves = preview["groups"].as_array().unwrap().iter().find(|group| group["name"] == "save_data").unwrap();
    assert_eq!(saves["replaced"], 1);
    assert_eq!(std::fs::read(data.join("savedata/system/save00.bin")).unwrap(), b"second");

    let (success, restored) = forge(&["restore", &zip_text, "--package-root", &root]);
    assert!(success, "{restored}");
    assert_eq!(std::fs::read(data.join("savedata/system/save00.bin")).unwrap(), b"first");
    let snapshot = Path::new(restored["snapshot"].as_str().unwrap());
    assert_eq!(std::fs::read(snapshot.join("save_data/system/save00.bin")).unwrap(), b"second");

    let (success, recovered) = forge(&["restore", "--recover", "--package-root", &root]);
    assert!(success && recovered["recovered"].is_null(), "{recovered}");

    write(&directory.path().join("not-a-backup.zip"), b"text");
    let bad = directory.path().join("not-a-backup.zip").to_string_lossy().into_owned();
    let (success, refused) = forge(&["restore", &bad, "--package-root", &root]);
    assert!(!success);
    assert_eq!(refused["status"], "error");
}
