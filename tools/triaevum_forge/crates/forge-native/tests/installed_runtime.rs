// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Port of the validator cases of test_installed_runtime.py (publication
//! itself belongs to forge-core).

mod common;

use std::path::{Path, PathBuf};

use forge_base::fsutil::atomic_write_json;
use forge_base::hashing::sha256_hex;
use forge_catalog::{host_platform, ACTIVE_TITLE, LINUX, WINDOWS};
use forge_native::{validate_installed_runtime, validate_installed_runtime_for};
use serde_json::{json, Map, Value};

struct Fixture {
    _temp: tempfile::TempDir,
    root: PathBuf,
    exe: PathBuf,
    plugin: PathBuf,
    data: PathBuf,
    title: PathBuf,
    profile: PathBuf,
    receipt: Map<String, Value>,
}

fn sha(path: &Path) -> String {
    sha256_hex(&std::fs::read(path).unwrap())
}

impl Fixture {
    fn new() -> Self {
        let temp = tempfile::tempdir().unwrap();
        let root = forge_prepare::paths::resolve(temp.path()).unwrap();
        let platform = host_platform().unwrap();
        let exe = root.join(platform.installed_runtime);
        std::fs::write(&exe, b"host").unwrap();
        let plugin = root.join(platform.title_module);
        std::fs::write(&plugin, b"title").unwrap();
        let data = root.join("custom-data");
        let title = data.join("titles").join("fixture");
        std::fs::create_dir_all(&title).unwrap();
        let profile = root.join("TriAevum.launch.json");
        let mut arguments = Vec::new();
        for (option, path) in [
            ("--a32-process-manifest", title.join("process-manifest.json")),
            ("--config", data.join("config").join("TriAevum.json")),
            ("--topscreen-config", data.join("config").join("topscreen_ui.json")),
            ("--save-data", data.join("savedata")),
        ] {
            // The active title's fact: only a TopScreen title routes its file.
            if option == "--topscreen-config" && !ACTIVE_TITLE.topscreen_ui {
                continue;
            }
            if option != "--save-data" {
                atomic_write_json(&path, &json!({})).unwrap();
            }
            arguments.extend([json!(option), json!(path.to_string_lossy())]);
        }
        atomic_write_json(&profile, &json!({"format": "oot3d_native_game_launch_profile_v1", "arguments": arguments}))
            .unwrap();
        let receipt = json!({
            "status": "ready", "runtime_sha256": sha(&exe), "plugin_sha256": sha(&plugin),
            "launch_profile": profile.to_string_lossy(), "launch_profile_sha256": sha(&profile),
        })
        .as_object()
        .unwrap()
        .clone();
        Self { _temp: temp, root, exe, plugin, data, title, profile, receipt }
    }

    fn validate(&self) -> forge_base::Result<PathBuf> {
        validate_installed_runtime(&self.exe, &self.title, &self.data, &self.receipt)
    }

    fn add_arguments(&self, extra: &[String]) {
        let mut payload: Value = serde_json::from_slice(&std::fs::read(&self.profile).unwrap()).unwrap();
        for argument in extra {
            payload["arguments"].as_array_mut().unwrap().push(json!(argument));
        }
        atomic_write_json(&self.profile, &payload).unwrap();
    }

    fn error(&self) -> String {
        self.validate().unwrap_err().message
    }
}

#[test]
fn matching_installation_and_legacy_receipt() {
    let mut fixture = Fixture::new();
    assert_eq!(fixture.validate().unwrap(), fixture.profile);
    fixture.receipt.remove("launch_profile_sha256");
    assert_eq!(fixture.validate().unwrap(), fixture.profile);
}

#[test]
fn rejects_receipt_from_another_platform() {
    let mut fixture = Fixture::new();
    let other = if host_platform().unwrap() == LINUX { WINDOWS } else { LINUX };
    fixture.receipt.insert("target".into(), json!(other.target));
    assert!(fixture.error().contains("another platform"));
}

#[test]
fn rejects_changed_plugin_and_profile() {
    let fixture = Fixture::new();
    std::fs::write(&fixture.plugin, b"another title").unwrap();
    assert!(fixture.error().starts_with("Installed runtime identity mismatch: "));
    std::fs::write(&fixture.plugin, b"title").unwrap();
    atomic_write_json(&fixture.profile, &json!({})).unwrap();
    assert_eq!(fixture.error(), "The launch profile changed after activation; run Forge again");
}

#[test]
fn rejects_wrong_data_folder() {
    let fixture = Fixture::new();
    let error = validate_installed_runtime(&fixture.exe, &fixture.title, &fixture.root, &fixture.receipt).unwrap_err();
    assert_eq!(error.message, "Launch profile points to another installation: --config");
}

#[test]
fn routed_resources_are_hash_checked() {
    let mut fixture = Fixture::new();
    let pack = fixture.data.join("mods/topscreen/atlas_overrides.o3tu");
    std::fs::create_dir_all(pack.parent().unwrap()).unwrap();
    std::fs::write(&pack, b"O3TU fixture").unwrap();
    fixture.receipt.remove("launch_profile_sha256");
    fixture.receipt.insert("topscreen_textures".into(), json!({"path": pack.to_string_lossy(), "sha256": sha(&pack)}));
    assert_eq!(fixture.error(), "Installed TopScreen textures routing is missing or duplicated");
    fixture.add_arguments(&["--topscreen-texture-overrides".into(), pack.to_string_lossy().into_owned()]);
    fixture.validate().unwrap();
    std::fs::write(&pack, b"corrupt").unwrap();
    assert_eq!(fixture.error(), "Installed TopScreen textures changed or are missing; run Forge again");
}

#[test]
fn ui_pack_receipt_and_renderer_cache_routing() {
    let mut fixture = Fixture::new();
    fixture.receipt.remove("launch_profile_sha256");
    let ui = fixture.data.join("mods/ui");
    std::fs::create_dir_all(&ui).unwrap();
    std::fs::write(ui.join("source.json"), b"{}").unwrap();
    fixture.receipt.insert(
        "ui_texture_pack".into(),
        json!({"path": ui.to_string_lossy(), "receipt_sha256": sha(&ui.join("source.json"))}),
    );
    let cache = fixture.data.join("cache/renderer");
    fixture.receipt.insert("renderer_cache".into(), json!({"path": cache.to_string_lossy()}));
    fixture.add_arguments(&["--ui-texture-pack".into(), ui.to_string_lossy().into_owned()]);
    assert_eq!(fixture.error(), "Installed renderer cache routing is missing or duplicated");
    fixture.add_arguments(&[
        "--renderer-cache-directory".into(),
        fixture.root.join("other").to_string_lossy().into_owned(),
    ]);
    assert_eq!(fixture.error(), "Launch profile points to another renderer cache");
    let mut payload: Value = serde_json::from_slice(&std::fs::read(&fixture.profile).unwrap()).unwrap();
    let arguments = payload["arguments"].as_array_mut().unwrap();
    let last = arguments.len() - 1;
    arguments[last] = json!(cache.to_string_lossy());
    atomic_write_json(&fixture.profile, &payload).unwrap();
    // The mutable cache need not exist.
    fixture.validate().unwrap();
    std::fs::write(ui.join("source.json"), b"{\"changed\": 1}").unwrap();
    assert_eq!(fixture.error(), "Installed HD UI textures changed or are missing; run Forge again");
}

#[test]
fn immutable_generation_must_be_selected_by_profile() {
    let mut fixture = Fixture::new();
    let generation = fixture.root.join("private-plugins").join("hash").join("triaevum_title_aot.dll");
    std::fs::create_dir_all(generation.parent().unwrap()).unwrap();
    std::fs::write(&generation, b"title").unwrap();
    fixture.receipt.insert("plugin".into(), json!(generation.to_string_lossy()));
    fixture.receipt.remove("launch_profile_sha256");
    assert_eq!(fixture.error(), "Launch profile does not select the installed plugin generation");
    fixture.add_arguments(&["--title-plugin".into(), generation.to_string_lossy().into_owned()]);
    assert_eq!(fixture.validate().unwrap(), fixture.profile);
    fixture.add_arguments(&["--title-plugin".into(), fixture.plugin.to_string_lossy().into_owned()]);
    assert_eq!(fixture.error(), "Launch profile selects another title plugin");
}

#[test]
fn profile_relative_placeholders_are_expanded() {
    let mut fixture = Fixture::new();
    let mut payload: Value = serde_json::from_slice(&std::fs::read(&fixture.profile).unwrap()).unwrap();
    for argument in payload["arguments"].as_array_mut().unwrap() {
        let text = argument.as_str().unwrap().to_string();
        if let Ok(relative) = Path::new(&text).strip_prefix(&fixture.root) {
            *argument = json!(format!("${{profile_dir}}/{}", relative.to_string_lossy().replace('\\', "/")));
        }
    }
    atomic_write_json(&fixture.profile, &payload).unwrap();
    fixture.receipt.remove("launch_profile_sha256");
    assert_eq!(fixture.validate().unwrap(), fixture.profile);
}

/// Rewrites the fixture profile's arguments and re-pins its digest.
fn rewrite_profile(fixture: &mut Fixture, edit: impl FnOnce(&mut Vec<Value>)) {
    let mut payload: Value = serde_json::from_slice(&std::fs::read(&fixture.profile).unwrap()).unwrap();
    edit(payload["arguments"].as_array_mut().unwrap());
    atomic_write_json(&fixture.profile, &payload).unwrap();
    let digest = sha(&fixture.profile);
    fixture.receipt.insert("launch_profile_sha256".into(), json!(digest));
}

fn drop_option(arguments: &mut Vec<Value>, option: &str) {
    let index = arguments.iter().position(|argument| argument == option).unwrap();
    arguments.drain(index..index + 2);
}

fn validate_for(fixture: &Fixture, topscreen_ui: bool) -> forge_base::Result<PathBuf> {
    validate_installed_runtime_for(&fixture.exe, &fixture.title, &fixture.data, &fixture.receipt, topscreen_ui)
}

/// The fixture profile with (OoT3D shape) or without (MM3D shape) the
/// TopScreen route, whatever the active title of this build.
fn fixture_with_topscreen(topscreen: bool) -> Fixture {
    let mut fixture = Fixture::new();
    let file = fixture.data.join("config").join("topscreen_ui.json");
    rewrite_profile(&mut fixture, |arguments| {
        if arguments.iter().any(|argument| argument == "--topscreen-config") {
            drop_option(arguments, "--topscreen-config");
        }
        if topscreen {
            arguments.extend([json!("--topscreen-config"), json!(file.to_string_lossy())]);
        }
    });
    if topscreen {
        atomic_write_json(&file, &json!({})).unwrap();
    } else if file.exists() {
        std::fs::remove_file(&file).unwrap();
    }
    fixture
}

#[test]
fn active_title_fact_selects_the_topscreen_rule() {
    // G3-F3: the OoT3D Forge requires the route, the MM3D Forge forbids it.
    let fixture = fixture_with_topscreen(ACTIVE_TITLE.topscreen_ui);
    assert_eq!(fixture.validate().unwrap(), fixture.profile);
    let other = fixture_with_topscreen(!ACTIVE_TITLE.topscreen_ui);
    assert!(other.validate().is_err());
}

#[test]
fn topscreen_title_accepts_its_routed_topscreen_file() {
    let fixture = fixture_with_topscreen(true);
    assert_eq!(validate_for(&fixture, true).unwrap(), fixture.profile);
}

#[test]
fn topscreen_title_refuses_a_profile_without_the_route() {
    // Negative (OoT3D): a profile without --topscreen-config is refused even
    // though it contains no other TopScreen option.
    let fixture = fixture_with_topscreen(false);
    assert_eq!(
        validate_for(&fixture, true).unwrap_err().message,
        "Missing or duplicate installed profile option: --topscreen-config"
    );
}

#[test]
fn title_without_topscreen_accepts_a_profile_without_it() {
    let fixture = fixture_with_topscreen(false);
    assert_eq!(validate_for(&fixture, false).unwrap(), fixture.profile);
}

#[test]
fn title_without_topscreen_refuses_a_topscreen_route() {
    // Negative (MM3D): the TopScreen route or the profile selection is refused.
    let fixture = fixture_with_topscreen(true);
    assert_eq!(
        validate_for(&fixture, false).unwrap_err().message,
        "Launch profile selects the TopScreen UI, which this title does not have"
    );
    let mut selected = fixture_with_topscreen(false);
    rewrite_profile(&mut selected, |arguments| arguments.extend([json!("--ui-profile"), json!("topscreen")]));
    assert_eq!(
        validate_for(&selected, false).unwrap_err().message,
        "Launch profile selects the TopScreen UI, which this title does not have"
    );
}
