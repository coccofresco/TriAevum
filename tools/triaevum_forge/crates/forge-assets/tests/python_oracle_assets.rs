// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Byte comparison with the Python Forge on generated fixtures: water
//! catalogs, scene-light catalogs, per-room emitters, the precomputed
//! scene-light table, font_coverage.zip, O3TU, the TopScreen folder and its
//! receipts, and the interior-sun receipt. Runs when TRIAEVUM_FORGE_PYTHON
//! names a Python 3.12+ interpreter.

use std::path::{Path, PathBuf};
use std::process::Command;

use forge_assets::interior_sun::{self, InteriorSunService, NativeRun, PrebuildCommand};
use forge_assets::{font_pack, precomputed_tables, scene_lights, texture_pack, topscreen, water};
use forge_base::legacy_json::{dumps, Options, Style};
use forge_base::{CancelToken, Result};
use serde_json::{Map, Value};

const COMPACT_UNSORTED: Options = Options { style: Style::Compact, sort_keys: false, ensure_ascii: true };

fn repository() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../..").canonicalize().unwrap()
}

struct Oracle {
    output: PathBuf,
    failures: Vec<String>,
}

impl Oracle {
    fn path(&self, case: &Value, key: &str) -> PathBuf {
        self.output.join(case[key].as_str().unwrap_or_else(|| panic!("case has no {key}: {case}")))
    }

    fn optional_path(&self, case: &Value, key: &str) -> Option<PathBuf> {
        case.get(key).and_then(Value::as_str).map(|relative| self.output.join(relative))
    }

    fn expect_bytes(&mut self, label: &str, actual: &[u8], expected: &Path) {
        let expected = std::fs::read(expected).unwrap();
        if actual != expected.as_slice() {
            let offset =
                actual.iter().zip(&expected).position(|(a, b)| a != b).unwrap_or(actual.len().min(expected.len()));
            let context = |bytes: &[u8]| {
                String::from_utf8_lossy(&bytes[offset.saturating_sub(40)..(offset + 80).min(bytes.len())]).into_owned()
            };
            self.failures.push(format!(
                "{label}: differs at byte {offset} (lengths {} / {})\n  rust:   {}\n  python: {}",
                actual.len(),
                expected.len(),
                context(actual),
                context(&expected)
            ));
        }
    }

    fn expect_value(&mut self, label: &str, actual: &Value, expected: &Value) {
        if actual != expected {
            self.failures.push(format!("{label}: {actual} != {expected}"));
        }
    }

    fn expect_error<T>(&mut self, label: &str, result: Result<T>, message: &str) {
        match result {
            Ok(_) => self.failures.push(format!("{label}: succeeded, Python failed with {message:?}")),
            Err(error) if error.message != message => {
                self.failures.push(format!("{label}: error {:?}, Python {message:?}", error.message))
            }
            Err(_) => {}
        }
    }

    /// Every file Python left under its data root, byte for byte, and no
    /// other file under ours.
    fn expect_tree(&mut self, label: &str, root: &Path, files: &Value) {
        let mut actual = Vec::new();
        collect(root, root, &mut actual);
        let expected: Vec<String> = files.as_object().unwrap().keys().cloned().collect();
        if actual != expected {
            self.failures.push(format!("{label}: files {actual:?} != {expected:?}"));
            return;
        }
        for (relative, source) in files.as_object().unwrap() {
            let bytes = std::fs::read(root.join(relative)).unwrap();
            self.expect_bytes(&format!("{label}: {relative}"), &bytes, &self.output.join(source.as_str().unwrap()));
        }
    }
}

fn collect(root: &Path, directory: &Path, out: &mut Vec<String>) {
    let Ok(entries) = std::fs::read_dir(directory) else { return };
    let mut entries: Vec<_> = entries.map(|entry| entry.unwrap().path()).collect();
    entries.sort();
    for path in entries {
        if path.is_dir() {
            collect(root, &path, out);
        } else {
            out.push(path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/"));
        }
    }
    out.sort();
}

struct FakeRuntime {
    sha256: String,
    shell: Vec<u8>,
    result: Value,
}

impl InteriorSunService for FakeRuntime {
    fn runtime_sha256(&self) -> Result<String> {
        Ok(self.sha256.clone())
    }

    fn prebuild(&self, command: &PrebuildCommand, _: &CancelToken) -> Result<NativeRun> {
        let shell = command.cache_directory.join("shadow-shells/0000000000000001.bin");
        std::fs::create_dir_all(shell.parent().unwrap()).unwrap();
        std::fs::write(shell, &self.shell).unwrap();
        std::fs::write(&command.manifest, self.result.to_string()).unwrap();
        Ok(NativeRun::default())
    }
}

fn known_rooms(table: &Path) -> Map<String, Value> {
    let mut table = precomputed_tables::read_scene_lights_table(table).unwrap();
    match table.remove("rooms") {
        Some(Value::Object(rooms)) => rooms,
        _ => unreachable!(),
    }
}

#[test]
fn derived_assets_match_python() {
    let Ok(python) = std::env::var("TRIAEVUM_FORGE_PYTHON") else {
        eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set");
        return;
    };
    let directory = tempfile::tempdir().unwrap();
    let output = std::path::absolute(directory.path()).unwrap().join("oracle");
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/asset_cases.py");
    let status =
        Command::new(python).arg("-I").arg(script).arg(repository()).arg(&output).arg("assets").status().unwrap();
    assert!(status.success(), "the Python fixture generator failed");
    let cases: Vec<Value> = serde_json::from_slice(&std::fs::read(output.join("cases.json")).unwrap()).unwrap();
    let mut oracle = Oracle { output: output.clone(), failures: Vec::new() };
    let cancel = CancelToken::new();
    let rust_data = directory.path().join("rust");
    for (index, case) in cases.iter().enumerate() {
        let label =
            format!("{} {}", case["kind"].as_str().unwrap(), case.get("name").and_then(Value::as_str).unwrap_or(""));
        let data_root = rust_data.join(index.to_string());
        match case["kind"].as_str().unwrap() {
            "water_catalog" => {
                let (payload, summary) = match case.get("profile") {
                    Some(profile) => water::build_catalog_with_profile(&oracle.path(case, "romfs"), profile, &cancel),
                    None => water::build_catalog(&oracle.path(case, "romfs"), &cancel),
                }
                .unwrap();
                oracle.expect_bytes(&label, &payload, &oracle.path(case, "expected"));
                oracle.expect_value(&label, &summary, &case["summary"]);
            }
            "prepare_water" => {
                water::prepare_water_textures(
                    &data_root,
                    &case["recipe"],
                    &oracle.path(case, "romfs"),
                    &mut |_, _| {},
                    &cancel,
                )
                .unwrap();
                oracle.expect_tree(&label, &data_root, &case["files"]);
            }
            "scene_catalog" => {
                let known = oracle.optional_path(case, "table").map(|table| known_rooms(&table));
                let (payload, summary) =
                    scene_lights::build_catalog(&oracle.path(case, "romfs"), known.as_ref(), &cancel).unwrap();
                oracle.expect_bytes(&label, &payload, &oracle.path(case, "expected"));
                oracle.expect_value(&label, &summary, &case["summary"]);
            }
            "room_emitters" => {
                let room = oracle.path(case, "room");
                let emitters = scene_lights::room_emitters(&std::fs::read(&room).unwrap()).unwrap();
                let payload = dumps(&Value::Array(emitters), COMPACT_UNSORTED);
                oracle.expect_bytes(
                    &format!("{label}{}", room.display()),
                    payload.as_bytes(),
                    &oracle.path(case, "expected"),
                );
            }
            "scene_table" => {
                let payload = precomputed_tables::scene_lights_table(
                    &oracle.path(case, "romfs"),
                    case["romfs_sha256"].as_str().unwrap(),
                    &cancel,
                )
                .unwrap();
                oracle.expect_bytes(&label, &payload, &oracle.path(case, "expected"));
            }
            "scene_table_error" => {
                let result = precomputed_tables::read_scene_lights_table(&oracle.path(case, "table"));
                oracle.expect_error(&label, result, case["message"].as_str().unwrap());
            }
            "prepare_scene" => {
                let table = oracle.optional_path(case, "table");
                scene_lights::prepare_scene_lights(
                    &data_root,
                    &case["recipe"],
                    &oracle.path(case, "romfs"),
                    table.as_deref(),
                    &mut |_, _| {},
                    &cancel,
                )
                .unwrap();
                oracle.expect_tree(&label, &data_root, &case["files"]);
            }
            "font_pack" => {
                let zip = data_root.join("font.zip");
                let result =
                    font_pack::prepare_font_pack(&oracle.path(case, "font"), &oracle.path(case, "romfs"), &zip);
                match case.get("message").and_then(Value::as_str) {
                    Some(message) => oracle.expect_error(&label, result, message),
                    None => {
                        result.unwrap();
                        oracle.expect_bytes(&label, &std::fs::read(&zip).unwrap(), &oracle.path(case, "expected"));
                    }
                }
            }
            "o3tu" => {
                let pack =
                    texture_pack::build_texture_pack(&std::fs::read(oracle.path(case, "atlas")).unwrap()).unwrap();
                oracle.expect_bytes(&label, &pack, &oracle.path(case, "expected"));
            }
            "topscreen" => {
                let hd = oracle.optional_path(case, "ui_atlas_hd");
                let result = topscreen::prepare_topscreen_assets(
                    &oracle.path(case, "root"),
                    &data_root,
                    &case["recipe"],
                    &oracle.path(case, "romfs"),
                    hd.as_deref(),
                    &mut |_, _| {},
                    &cancel,
                );
                match case.get("message").and_then(Value::as_str) {
                    Some(message) => oracle.expect_error(&label, result, message),
                    None => {
                        result.unwrap();
                        oracle.expect_tree(&label, &data_root, &case["files"]);
                    }
                }
            }
            "interior_sun" => {
                let runtime = FakeRuntime {
                    sha256: case["runtime_sha256"].as_str().unwrap().to_string(),
                    shell: case["shell"].as_array().unwrap().iter().map(|b| b.as_u64().unwrap() as u8).collect(),
                    result: case["result"].clone(),
                };
                let table = oracle.optional_path(case, "table");
                let mut messages = Vec::new();
                let returned = interior_sun::prepare_interior_sun(
                    &runtime,
                    &data_root,
                    &output.join("fixtures/sun/romfs.bin"),
                    &"a".repeat(64),
                    &oracle.path(case, "cache"),
                    table.as_deref(),
                    &mut |_, text| messages.push(Value::String(text.to_string())),
                    &cancel,
                )
                .unwrap();
                oracle.expect_value(&label, &Value::Array(messages), &case["messages"]);
                oracle.expect_value(&label, &Value::Object(returned), &case["returned"]);
                oracle.expect_tree(&label, &data_root, &case["files"]);
            }
            other => panic!("unknown case kind {other}"),
        }
    }
    assert!(oracle.failures.is_empty(), "{} mismatches:\n{}", oracle.failures.len(), oracle.failures.join("\n"));
    assert!(cases.len() > 80);
}
