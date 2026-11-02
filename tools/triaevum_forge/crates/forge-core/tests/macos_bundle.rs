// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Synthetic import through the real engine: a sealed app stays byte-identical.
#![cfg(target_os = "macos")]

mod common;

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

fn inventory(root: &Path) -> BTreeMap<PathBuf, Vec<u8>> {
    fn walk(root: &Path, directory: &Path, files: &mut BTreeMap<PathBuf, Vec<u8>>) {
        for entry in std::fs::read_dir(directory).unwrap() {
            let path = entry.unwrap().path();
            if path.is_dir() {
                files.insert(path.strip_prefix(root).unwrap().to_path_buf(), Vec::new());
                walk(root, &path, files);
            } else {
                files.insert(path.strip_prefix(root).unwrap().to_path_buf(), std::fs::read(path).unwrap());
            }
        }
    }
    let mut files = BTreeMap::new();
    walk(root, root, &mut files);
    files
}

#[test]
fn bundle_import_uses_external_activation() {
    // Isolate the process environment from every other test, and from the
    // developer's normal activation. Nothing may reach ~/Library in this test.
    if std::env::var_os("TRIAEVUM_TEST_BUNDLE_CHILD").is_none() {
        let directory = tempfile::tempdir().unwrap();
        let result = std::process::Command::new(std::env::current_exe().unwrap())
            .args(["--exact", "bundle_import_uses_external_activation", "--nocapture"])
            .env("TRIAEVUM_TEST_BUNDLE_CHILD", "1")
            .env("TRIAEVUM_MACOS_DATA_ROOT", directory.path().join("activation"))
            .output()
            .unwrap();
        assert!(
            result.status.success(),
            "{}\n{}",
            String::from_utf8_lossy(&result.stdout),
            String::from_utf8_lossy(&result.stderr)
        );
        return;
    }
    let mut fixture = common::Fixture::new();
    let app = fixture.package.with_file_name("TriAevum.app");
    let package = app.join("Contents/Resources");
    std::fs::create_dir_all(package.parent().unwrap()).unwrap();
    std::fs::rename(&fixture.package, &package).unwrap();
    fixture.package = package;
    let before = inventory(&app);
    let activation = PathBuf::from(std::env::var_os("TRIAEVUM_MACOS_DATA_ROOT").unwrap());
    let runtime = common::FakeRuntime::default();
    fixture.install(&runtime, &mut Vec::new()).unwrap();
    assert_eq!(inventory(&app), before);
    let profile = activation.join("TriAevum.launch.json");
    assert!(profile.is_file());
    let data = activation.join("data");
    assert!(data.join("active-title.json").is_file());
    let plugin = fixture.package.join("titles").join(common::RECIPE).join(common::host().title_module);
    assert_eq!(*runtime.plugin_queries.borrow(), vec![plugin]);
    let save = data.join("savedata/sentinel.sav");
    std::fs::write(&save, b"keep my save").unwrap();
    let config = data.join("config/TriAevum.json");
    let settings = std::fs::read(&config).unwrap();
    fixture.install(&runtime, &mut Vec::new()).unwrap();
    assert_eq!(inventory(&app), before);
    assert_eq!(std::fs::read(save).unwrap(), b"keep my save");
    assert_eq!(std::fs::read(config).unwrap(), settings);
}
