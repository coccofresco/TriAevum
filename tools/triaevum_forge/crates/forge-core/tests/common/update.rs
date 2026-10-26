// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! A package update over a synthetic installation (UX plan WP-04A): the
//! replaced package files, and sentinel user files that must survive.

#![allow(dead_code)]

use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::hashing::sha256_hex;
use serde_json::{json, Value};

use crate::common::{host, write, Fixture, RECIPE};

/// The package's release manifest (`state::package_version` reads it).
pub fn set_version(fixture: &Fixture, version: &str) {
    write(
        &fixture.package.join("release-manifest.json"),
        json!({"release": {"version": version}}).to_string().as_bytes(),
    );
}

/// Unpacks a new package over the installation: a new runtime where the
/// package brings it (Windows `runtime/`, Linux in place), a new title
/// plugin and the catalog that binds them.
pub fn replace_package(fixture: &Fixture, version: Option<&str>, runtime: &[u8], plugin: &[u8]) {
    let catalog_path = fixture.package.join("recipes/precompiled-titles.json");
    let mut catalog = Value::Object(load_json_object(&catalog_path).unwrap());
    let record = |relative: &str, bytes: &[u8]| json!({"path": relative, "bytes": bytes.len(), "sha256": sha256_hex(bytes)});
    write(&fixture.package.join(host().runtime), runtime);
    catalog["runtime"] = record(host().runtime, runtime);
    let plugin_path = format!("titles/{RECIPE}/{}", host().title_module);
    write(&fixture.package.join(&plugin_path), plugin);
    catalog["titles"][0]["plugin"] = record(&plugin_path, plugin);
    write(&catalog_path, catalog.to_string().as_bytes());
    if let Some(version) = version {
        set_version(fixture, version);
    }
}

/// A package without the installed revision's module.
pub fn drop_title(fixture: &Fixture) {
    let catalog_path = fixture.package.join("recipes/precompiled-titles.json");
    let mut catalog = Value::Object(load_json_object(&catalog_path).unwrap());
    catalog["titles"][0]["recipe"] = json!("another-revision");
    write(&catalog_path, catalog.to_string().as_bytes());
}

/// User files the update must keep byte for byte: saves, the HD pack and
/// other downloads, mods, save states, unknown root files.
pub fn sentinels(fixture: &Fixture) -> Vec<(PathBuf, Vec<u8>)> {
    let data = fixture.data();
    let files = [
        (data.join("savedata/00000000.sav"), b"save slot".to_vec()),
        (data.join("downloads/vanilla_ui_hud.zip"), b"PK hd pack".to_vec()),
        (data.join("mods/custom/romfs/file.bin"), b"mod payload".to_vec()),
        (fixture.package.join("savestates/slot1.state"), b"save state".to_vec()),
        (fixture.package.join("my notes.txt"), b"unknown root file".to_vec()),
    ];
    for (path, bytes) in &files {
        write(path, bytes);
    }
    files.to_vec()
}

pub fn assert_kept(files: &[(PathBuf, Vec<u8>)]) {
    for (path, bytes) in files {
        assert_eq!(&std::fs::read(path).unwrap(), bytes, "{} changed", path.display());
    }
}

/// Every file of `D/config` with its bytes.
pub fn config_files(data: &Path) -> Vec<(String, Vec<u8>)> {
    let mut files: Vec<(String, Vec<u8>)> = std::fs::read_dir(data.join("config"))
        .unwrap()
        .map(|entry| entry.unwrap())
        .filter(|entry| entry.file_type().unwrap().is_file())
        .map(|entry| (entry.file_name().to_string_lossy().into_owned(), std::fs::read(entry.path()).unwrap()))
        .collect();
    files.sort();
    files
}

/// The user's own TopScreen layout and graphics choices.
pub fn customize_settings(data: &Path) {
    let topscreen = data.join("config/topscreen_ui.json");
    let mut user = load_json_object(&topscreen).unwrap();
    user.insert("hud_margin_x".into(), json!(40));
    user.insert("dpad_size_percent".into(), json!(130));
    user.insert("camera_fov_percent".into(), json!(120));
    atomic_write_json(&topscreen, &Value::Object(user)).unwrap();
    let runtime = data.join("config/TriAevum.json");
    let mut config = load_json_object(&runtime).unwrap();
    config.insert("UserChoice".into(), json!({"Kept": true}));
    atomic_write_json(&runtime, &Value::Object(config)).unwrap();
}

/// The prepared title's runtime receipt.
pub fn receipt(fixture: &Fixture) -> serde_json::Map<String, Value> {
    let active = load_json_object(&fixture.data().join("active-title.json")).unwrap();
    let directory =
        forge_prepare::paths::resolve_reference(active["directory"].as_str().unwrap(), &fixture.data()).unwrap();
    load_json_object(&directory.join("forge-state.json")).unwrap()["runtime"].as_object().unwrap().clone()
}

/// The prepared title directory of the active title.
pub fn title_directory(fixture: &Fixture) -> PathBuf {
    let active = load_json_object(&fixture.data().join("active-title.json")).unwrap();
    forge_prepare::paths::resolve_reference(active["directory"].as_str().unwrap(), &fixture.data()).unwrap()
}

/// Copies a tree (files and folders).
pub fn copy_tree(source: &Path, destination: &Path) {
    std::fs::create_dir_all(destination).unwrap();
    for entry in std::fs::read_dir(source).unwrap() {
        let entry = entry.unwrap();
        let target = destination.join(entry.file_name());
        if entry.file_type().unwrap().is_dir() {
            copy_tree(&entry.path(), &target);
        } else {
            std::fs::copy(entry.path(), &target).unwrap();
        }
    }
}
