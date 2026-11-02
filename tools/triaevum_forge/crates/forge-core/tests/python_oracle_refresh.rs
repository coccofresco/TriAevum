// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Oracle for Update in place (UX plan WP-04A): the same synthetic
//! installation, refreshed after a package update by the Python Forge
//! (`package_update.refresh_packaged_runtime` as `launch_runtime` ran it)
//! and by this engine, at the same path one after the other, must leave the
//! same files with the same bytes: plugin generations, launch profile,
//! receipts, active title, settings and the placed runtime.
//!
//! Deliberate differences (the plan's fixes): with the default
//! `hud_layout: keep` the user's TopScreen file keeps its bytes (Python
//! always rewrote the layout; `standard` matches Python), the package
//! version record `installed-package.json` is new, and the refresh holds
//! the game session lock (its lock file). Failures carry Python's messages.
//!
//! Runs when TRIAEVUM_FORGE_PYTHON names a Python 3 interpreter.

mod common;
#[path = "common/update.rs"]
mod update;

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::Command;

use common::{FakeRuntime, Fixture};
use forge_base::CancelToken;
use forge_core::publish::HudLayout;
use forge_core::refresh::{refresh_installation, RefreshRequest};
use serde_json::Value;
use update::{copy_tree, customize_settings, replace_package, set_version};

const NEW_RUNTIME: &[u8] = b"fake runtime 2";
const NEW_PLUGIN: &[u8] = b"MZ fake title plugin 2";

fn python() -> Option<String> {
    std::env::var("TRIAEVUM_FORGE_PYTHON").ok().filter(|value| !value.is_empty())
}

fn repo_root() -> PathBuf {
    forge_prepare::paths::resolve(&Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../..")).unwrap()
}

fn run_python(python: &str, mode: &str, fixture: &Fixture) -> Value {
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/refresh_e2e.py");
    let result = fixture.package.with_file_name("python-result.json");
    let output = Command::new(python)
        .arg("-I")
        .arg(script)
        .arg(repo_root())
        .arg(mode)
        .args([&fixture.package, &fixture.data(), &result])
        .output()
        .unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    serde_json::from_slice(&std::fs::read(result).unwrap()).unwrap()
}

fn rust_refresh(fixture: &Fixture, hud_layout: HudLayout) -> forge_base::Result<Value> {
    let request = RefreshRequest { hud_layout, ..RefreshRequest::package(&fixture.package) };
    refresh_installation(&request, &FakeRuntime::default(), &mut |_, _| {}, &CancelToken::new())
}

fn files(root: &Path) -> BTreeMap<String, Vec<u8>> {
    fn walk(root: &Path, directory: &Path, out: &mut BTreeMap<String, Vec<u8>>) {
        for entry in std::fs::read_dir(directory).unwrap().flatten() {
            let path = entry.path();
            if entry.file_type().unwrap().is_dir() {
                walk(root, &path, out);
            } else {
                let relative = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
                out.insert(relative, std::fs::read(&path).unwrap());
            }
        }
    }
    let mut out = BTreeMap::new();
    walk(root, root, &mut out);
    out
}

/// Files only this engine writes, by design.
fn rust_only(relative: &str) -> bool {
    relative.ends_with("/installed-package.json") || relative.ends_with(".forge-activation/game-session.lock")
}

/// Every difference between the Python and Rust trees, except `skip`.
fn differences(python: &BTreeMap<String, Vec<u8>>, rust: &BTreeMap<String, Vec<u8>>, skip: &str) -> Vec<String> {
    let mut out = Vec::new();
    for (name, bytes) in python {
        match rust.get(name) {
            None => out.push(format!("only Python wrote {name}")),
            // Python does not update the Rust-only record it inherited.
            Some(other) if other != bytes && name != skip && !rust_only(name) => out.push(format!("{name} differs")),
            _ => {}
        }
    }
    for name in rust.keys().filter(|name| !python.contains_key(*name) && !rust_only(name)) {
        out.push(format!("only Rust wrote {name}"));
    }
    out
}

/// Puts the installation back to its state before the refresh.
fn restore(fixture: &Fixture, snapshot: &Path) {
    if fixture.package.exists() {
        std::fs::remove_dir_all(&fixture.package).unwrap();
    }
    copy_tree(snapshot, &fixture.package);
}

#[test]
fn python_and_rust_rebind_the_same_receipts() {
    let Some(python) = python() else { return eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set") };
    let fixture = Fixture::new();
    set_version(&fixture, "0.8.0");
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    customize_settings(&fixture.data());
    replace_package(&fixture, Some("0.8.1"), NEW_RUNTIME, NEW_PLUGIN);
    let snapshot = fixture.package.with_file_name("snapshot");
    copy_tree(&fixture.package, &snapshot);
    let topscreen = "data/config/topscreen_ui.json";
    let before = files(&snapshot);

    assert_eq!(run_python(&python, "launch", &fixture), serde_json::json!({"refreshed": true}));
    let python_tree = files(&fixture.package);
    assert_ne!(python_tree[topscreen], before[topscreen], "Python rewrites the layout");

    restore(&fixture, &snapshot);
    let standard = rust_refresh(&fixture, HudLayout::Standard).unwrap();
    assert_eq!(standard["status"], "updated");
    let rust_tree = files(&fixture.package);
    let mismatches = differences(&python_tree, &rust_tree, "");
    assert!(mismatches.is_empty(), "{mismatches:#?}");
    println!("standard layout: {} files byte-identical", python_tree.len());

    // Decision 4: the default keeps the user's layout; everything else is Python's.
    restore(&fixture, &snapshot);
    assert_eq!(rust_refresh(&fixture, HudLayout::Keep).unwrap()["config_changes"], serde_json::json!([]));
    let kept = files(&fixture.package);
    let mismatches = differences(&python_tree, &kept, topscreen);
    assert!(mismatches.is_empty(), "{mismatches:#?}");
    assert_eq!(kept[topscreen], before[topscreen]);

    // Both see the refreshed installation as current.
    assert_eq!(run_python(&python, "launch", &fixture), serde_json::json!({"refreshed": false}));
    assert_eq!(rust_refresh(&fixture, HudLayout::Keep).unwrap()["status"], "unchanged");
}

#[test]
fn python_and_rust_refuse_the_same_installations() {
    let Some(python) = python() else { return eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set") };
    let fixture = Fixture::new();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    replace_package(&fixture, None, NEW_RUNTIME, NEW_PLUGIN);
    let snapshot = fixture.package.with_file_name("snapshot");
    copy_tree(&fixture.package, &snapshot);
    let old_generation = || {
        if common::host() == forge_catalog::MACOS {
            return fixture.package.join("titles").join(common::RECIPE).join(common::host().title_module);
        }
        fixture
            .package
            .join("private-plugins")
            .join(forge_base::hashing::sha256_hex(b"MZ fake title plugin"))
            .join(common::host().title_module)
    };
    let journal = || forge_core::transaction::journal_path(&fixture.package, forge_core::layout::HostOs::current());
    type Damage<'a> = Box<dyn Fn() + 'a>;
    let cases: [(&str, &str, Damage); 4] = [
        ("launch", "damaged plugin generation", Box::new(|| std::fs::write(old_generation(), b"damaged").unwrap())),
        ("launch", "changed launch profile", Box::new(|| std::fs::write(fixture.launch_profile(), b"{}").unwrap())),
        ("launch", "revision not in the package", Box::new(|| update::drop_title(&fixture))),
        ("refresh", "interrupted activation", Box::new(|| std::fs::write(journal(), b"{}").unwrap())),
    ];
    for (mode, name, damage) in cases {
        restore(&fixture, &snapshot);
        damage();
        let expected = run_python(&python, mode, &fixture);
        restore(&fixture, &snapshot);
        damage();
        let error = rust_refresh(&fixture, HudLayout::Keep).unwrap_err();
        assert_eq!(expected["error"].as_str(), Some(error.message.as_str()), "{name}");
    }
}
