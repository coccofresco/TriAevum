// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The public import against a synthetic package and a fake native runtime:
//! stage order, written files, reuse, configuration migration, rollback.

mod common;

use std::cell::Cell;
use std::path::{Path, PathBuf};

use common::{code, host, FakeRuntime, Fixture, ANDROID, RECIPE};
use forge_base::fsutil::load_json_object;
use forge_base::hashing::sha256_hex;
use forge_base::ErrorKind;
use forge_core::publish::standard_topscreen;
use serde_json::{json, Value};

fn hidden_entries(directory: &Path) -> Vec<String> {
    std::fs::read_dir(directory)
        .unwrap()
        .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
        .filter(|name| name.starts_with(".rom-import-"))
        .collect()
}

#[test]
fn title_program_pack_is_bound_routed_and_rechecked_on_repair() {
    let fixture = Fixture::new();
    let path = fixture.package.join("recipes/precompiled-titles.json");
    let mut catalog: Value = serde_json::from_slice(&std::fs::read(&path).unwrap()).unwrap();
    let bytes = b"synthetic pack routing fixture";
    std::fs::write(fixture.package.join("title.spvpack"), bytes).unwrap();
    catalog["titles"][0]["title_program_pack"] = json!({"path": "title.spvpack", "bytes": bytes.len(), "sha256": sha256_hex(bytes)});
    std::fs::write(&path, serde_json::to_vec(&catalog).unwrap()).unwrap();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    let profile = load_json_object(&fixture.launch_profile()).unwrap();
    assert!(profile["arguments"].as_array().unwrap().iter().any(|v| v == "--title-program-pack"));
    let before = std::fs::read(fixture.launch_profile()).unwrap();
    std::fs::write(fixture.package.join("title.spvpack"), b"damaged").unwrap();
    assert!(fixture.install(&FakeRuntime::default(), &mut Vec::new()).is_err());
    assert_eq!(std::fs::read(fixture.launch_profile()).unwrap(), before);
}

#[test]
fn installs_the_precompiled_title_and_publishes_the_runtime() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime::default();
    let mut stages = Vec::new();
    let result = fixture.install(&runtime, &mut stages).unwrap();

    let mut names: Vec<&str> = stages.iter().map(|(stage, _)| stage.as_str()).collect();
    // A host without a Vulkan loader adds a warning in the preflight stage.
    // Stage order must not depend on the test machine's graphics installation.
    names.dedup();
    assert_eq!(&names[..5], ["preflight", "extract", "verify", "prepare", "activate"]);
    assert_eq!(stages.last().unwrap(), &("ready".to_string(), "The title is ready to play.".to_string()));
    assert!(names.contains(&"interior-sun"));
    assert_eq!(result["status"], "ready");
    assert_eq!(result["recipe"], RECIPE);
    assert_eq!(result["adaptation"], Value::Null);
    assert_eq!(result["prepared"]["status"], "prepared");
    assert_eq!(result["build"]["install_model"], "precompiled_title_rom_import_v1");
    assert_eq!(result["build"]["objects_compiled"], 0);
    assert_eq!(result["build"]["device_pipeline_preparation"], Value::Null);
    assert_eq!(result["build"]["interior_sun"]["receipt_reused"], false);
    assert!(result["install_wall_seconds"].as_f64().unwrap() >= 0.0);

    let data = fixture.data();
    assert!(hidden_entries(&data).is_empty(), "staging removed");
    let plugin_sha = sha256_hex(b"MZ fake title plugin");
    let plugin_relative = if host() == forge_catalog::MACOS {
        assert!(!fixture.package.join("private-plugins").exists());
        format!("titles/{RECIPE}/{}", host().title_module)
    } else {
        format!("private-plugins/{plugin_sha}/{}", host().title_module)
    };
    let generation = fixture.package.join(&plugin_relative);
    assert_eq!(std::fs::read(&generation).unwrap(), b"MZ fake title plugin");
    assert_eq!(*runtime.plugin_queries.borrow(), std::slice::from_ref(&generation));
    // The packaged runtime was placed in the installation root.
    assert!(fixture.package.join(host().installed_runtime).is_file());
    assert!(host().runtime == host().installed_runtime || !fixture.package.join(host().runtime).exists());

    let profile = load_json_object(&fixture.launch_profile()).unwrap();
    assert_eq!(profile["format"], "oot3d_native_game_launch_profile_v1");
    let arguments: Vec<&str> = profile["arguments"].as_array().unwrap().iter().map(|a| a.as_str().unwrap()).collect();
    assert_eq!(arguments[0..2], ["--title-plugin", &format!("${{runtime_dir}}/{plugin_relative}")]);
    assert_eq!(arguments[4..6], ["--resource-root", "${runtime_dir}/resources"]);
    // A plain package is its own activation: its data is a runtime_dir path.
    assert_eq!(arguments[10..12], ["--config", "${runtime_dir}/data/config/TriAevum.json"]);
    assert_eq!(arguments[24..26], ["--height", "720"]);
    let tail: Vec<&str> = arguments[26..].iter().step_by(2).copied().collect();
    assert_eq!(tail, ["--water-texture-catalog", "--scene-light-catalog", "--renderer-cache-directory"]);
    assert!(profile["path_scopes"].as_object().unwrap().values().all(|scope| scope == "relative"));

    let state =
        load_json_object(&PathBuf::from(result["prepared"]["directory"].as_str().unwrap()).join("forge-state.json"))
            .unwrap();
    let receipt = &state["runtime"];
    assert_eq!(receipt["backend"], "generated_cpp_whole_aot_plugin_v2");
    assert_eq!(receipt["target"], host().target);
    assert_eq!(receipt["runtime_sha256"], sha256_hex(b"fake runtime"));
    assert_eq!(receipt["plugin_sha256"], plugin_sha);
    let profile_reference =
        if cfg!(windows) { "../../../TriAevum.launch.json" } else { "../../../../TriAevum.launch.json" };
    assert_eq!(receipt["launch_profile"], profile_reference);
    assert_eq!(receipt["launch_profile_sha256"], sha256_hex(&std::fs::read(fixture.launch_profile()).unwrap()));
    assert_eq!(receipt["renderer_cache"]["path"], "../../../cache/renderer");
    // A runtime without managed_data_paths gets the Python Forge's profile.
    assert!(receipt.get("data_routing").is_none());
    assert_eq!(state["module"]["status"], "ready");

    let active = load_json_object(&data.join("active-title.json")).unwrap();
    assert_eq!(active["module_sha256"], state["module"]["sha256"]);
    assert_eq!(
        Value::Object(load_json_object(&data.join("config/topscreen_ui.json")).unwrap()),
        Value::Object(standard_topscreen().unwrap())
    );
    assert_eq!(load_json_object(&data.join("config/TriAevum.json")).unwrap()["Graphics"]["Preset"], "Enhanced");
    assert_eq!(load_json_object(&data.join("config/game_language.json")).unwrap()["selected"], "en");
    assert!(data.join("savedata").is_dir());
    assert!(!data.join(".forge-activation/journal.json").exists());
}

// UX plan WP-05C: a runtime advertising managed_data_paths gets the managed
// data tree's folders in D and the ROM's own title id (a USA ROM keeps
// 0004000000033500 though its code runs the EUR module); the receipt routes
// them and validation refuses a profile that points elsewhere.
#[test]
fn a_runtime_with_managed_data_paths_routes_the_data_tree() {
    let fixture = Fixture::build_with(false, Some(0x0004_0000_0003_3500));
    let runtime = FakeRuntime { managed_data_paths: true, ..FakeRuntime::default() };
    let result = fixture.install(&runtime, &mut Vec::new()).unwrap();
    let profile_path = fixture.launch_profile();
    let profile = load_json_object(&profile_path).unwrap();
    let arguments: Vec<&str> = profile["arguments"].as_array().unwrap().iter().map(|a| a.as_str().unwrap()).collect();
    let value = |option: &str| arguments[arguments.iter().position(|a| *a == option).unwrap() + 1];
    // The earlier arguments keep their places.
    assert_eq!(arguments[24..26], ["--height", "720"]);
    assert_eq!(value("--log-directory"), "${runtime_dir}/data/logs");
    assert_eq!(value("--crash-directory"), "${runtime_dir}/data/logs/crashes");
    assert_eq!(value("--savestate-directory"), "${runtime_dir}/data/savestates");
    assert_eq!(value("--texture-load-directory"), "${runtime_dir}/data/textures/load/0004000000033500");
    assert_eq!(value("--texture-dump-directory"), "${runtime_dir}/data/textures/dump/0004000000033500");
    assert_eq!(value("--title-id"), "0004000000033500");
    assert_eq!(profile["path_scopes"]["--savestate-directory"], "relative");
    assert!(profile["path_scopes"].get("--title-id").is_none());

    let title = PathBuf::from(result["prepared"]["directory"].as_str().unwrap());
    let state = load_json_object(&title.join("forge-state.json")).unwrap();
    let receipt = state["runtime"].as_object().unwrap().clone();
    let routing = &receipt["data_routing"];
    assert_eq!(routing["log_directory"], "../../../logs");
    assert_eq!(routing["crash_directory"], "../../../logs/crashes");
    assert_eq!(routing["savestate_directory"], "../../../savestates");
    assert_eq!(routing["texture_load_directory"], "../../../textures/load/0004000000033500");
    assert_eq!(routing["title_id"], "0004000000033500");
    let runtime_path = fixture.package.join(host().installed_runtime);
    let validate = |receipt: &serde_json::Map<String, Value>| {
        forge_native::validate_installed_runtime(&runtime_path, &title, &fixture.data(), receipt)
    };
    assert_eq!(validate(&receipt).unwrap(), profile_path);

    // A profile that routes the logs elsewhere (its hash aside) is refused.
    let mut edited = profile.clone();
    let index = arguments.iter().position(|a| *a == "--log-directory").unwrap() + 1;
    edited["arguments"][index] = json!("${runtime_dir}/elsewhere");
    forge_base::fsutil::atomic_write_json(&profile_path, &Value::Object(edited)).unwrap();
    let mut unhashed = receipt.clone();
    unhashed.remove("launch_profile_sha256");
    let error = validate(&unhashed).unwrap_err();
    assert_eq!(error.kind, ErrorKind::RecoveryRequired);
    assert!(error.message.contains("--log-directory"), "{}", error.message);
    let mut renamed = profile.clone();
    let index = arguments.iter().position(|a| *a == "--title-id").unwrap() + 1;
    renamed["arguments"][index] = json!("0004000000033600");
    forge_base::fsutil::atomic_write_json(&profile_path, &Value::Object(renamed)).unwrap();
    assert_eq!(validate(&unhashed).unwrap_err().kind, ErrorKind::RecoveryRequired);

    // A ROM whose ExHeader names no title: logs, dumps and states only.
    let unnamed = Fixture::new();
    unnamed.install(&runtime, &mut Vec::new()).unwrap();
    let profile = load_json_object(&unnamed.launch_profile()).unwrap();
    let arguments: Vec<&str> = profile["arguments"].as_array().unwrap().iter().map(|a| a.as_str().unwrap()).collect();
    assert!(arguments.contains(&"--log-directory") && arguments.contains(&"--savestate-directory"));
    assert!(!arguments.contains(&"--title-id") && !arguments.contains(&"--texture-load-directory"));
}

#[test]
fn a_second_install_reuses_the_title_and_keeps_user_state() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime::default();
    let first = fixture.install(&runtime, &mut Vec::new()).unwrap();
    let state_path = PathBuf::from(first["prepared"]["directory"].as_str().unwrap()).join("forge-state.json");
    let mut state = load_json_object(&state_path).unwrap();
    state.insert("translation".into(), json!({"status": "kept"}));
    forge_base::fsutil::atomic_write_json(&state_path, &Value::Object(state)).unwrap();
    let topscreen = fixture.data().join("config/topscreen_ui.json");
    let mut user = standard_topscreen().unwrap();
    user.insert("hud_scale".into(), json!(1.0));
    user.insert("hud_margin_x".into(), json!(40));
    user.insert("camera_fov_percent".into(), json!(120));
    forge_base::fsutil::atomic_write_json(&topscreen, &Value::Object(user)).unwrap();

    let second = fixture.install(&runtime, &mut Vec::new()).unwrap();
    assert_eq!(second["prepared"]["status"], "reused");
    assert_eq!(second["build"]["interior_sun"]["receipt_reused"], true);
    assert_eq!(second["build"]["package"]["status"], "reused");
    let state = load_json_object(&state_path).unwrap();
    assert_eq!(state["translation"]["status"], "kept");
    // Decision 4: a reinstall keeps the user's layout unless asked.
    let kept = load_json_object(&topscreen).unwrap();
    assert_eq!(kept["hud_margin_x"], 40);
    let request = forge_core::install::InstallRequest {
        source: forge_core::install::InstallSource::ExtractedDirectory(fixture.extracted.clone()),
        data_root: None,
        package_root: fixture.package.clone(),
        target: forge_core::install::InstallTarget::Package,
        game_language: None,
    };
    let options = forge_core::install::InstallOptions { hud_layout: forge_core::publish::HudLayout::Standard };
    let mut report = |_: &str, _: &str| {};
    forge_core::install::install_private_title_with(
        request,
        options,
        &runtime,
        &mut report,
        &forge_base::CancelToken::new(),
    )
    .unwrap();
    let migrated = load_json_object(&topscreen).unwrap();
    assert_eq!(migrated["hud_scale"], 1.0);
    assert_eq!(migrated["hud_margin_x"], 4);
    assert_eq!(migrated["camera_fov_percent"], 120);
}

#[test]
fn the_language_chosen_before_installing_is_selected_when_offered() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime::default();
    let selected = || load_json_object(&fixture.data().join("config/game_language.json")).unwrap()["selected"].clone();
    fixture.install_choosing(&runtime, &mut Vec::new(), Some("it")).unwrap();
    assert_eq!(selected(), "it");
    // Not offered: the previous selection stays.
    fixture.install_choosing(&runtime, &mut Vec::new(), Some("fr")).unwrap();
    assert_eq!(selected(), "it");
    fixture.install_choosing(&runtime, &mut Vec::new(), Some("en")).unwrap();
    assert_eq!(selected(), "en");
}

#[test]
fn a_rejected_plugin_rolls_the_publication_back() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime { reject_plugin: Cell::new(true), ..FakeRuntime::default() };
    let error = fixture.install(&runtime, &mut Vec::new()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::NativePreflight);
    assert_eq!(error.stage, Some("activate"));
    let data = fixture.data();
    for created in ["active-title.json", "TriAevum.launch.json", "config/TriAevum.json", "config/topscreen_ui.json"] {
        assert!(!data.join(created).exists(), "{created} rolled back");
    }
    assert!(!fixture.launch_profile().exists());
    let titles = data.join("titles").join(RECIPE);
    let title = std::fs::read_dir(&titles).unwrap().next().unwrap().unwrap().path();
    assert_eq!(load_json_object(&title.join("forge-state.json")).unwrap()["module"]["status"], "not_built");
    assert!(!data.join(".forge-activation/journal.json").exists());
    assert!(hidden_entries(&data).is_empty());
    // Languages are written only after a committed activation.
    assert!(!data.join("config/game_language.json").exists());
}

#[test]
fn an_unknown_revision_stops_at_verification() {
    let fixture = Fixture::new();
    let mut code = code();
    code[0] ^= 1;
    std::fs::write(fixture.extracted.join("code.bin"), code).unwrap();
    let error = fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedRevision);
    assert_eq!(error.stage, Some("verify"));
    assert!(error.message.starts_with("the decrypted ROM does not match any supported game revision"));
    assert!(hidden_entries(&fixture.data()).is_empty());
    assert!(!fixture.data().join("sources").exists());
}

// The only test of this binary that names a native library folder: the
// folder is fixed for the process lifetime.
#[test]
fn installs_for_the_android_application_without_copying_libraries() {
    let fixture = Fixture::android();
    let (native, profile_root) = fixture.android.clone().unwrap();
    let runtime = FakeRuntime::default();
    let mut stages = Vec::new();
    let result = fixture.install(&runtime, &mut stages).unwrap();
    assert_eq!(result["status"], "ready");
    assert_eq!(stages.last().unwrap().0, "ready");
    let plugin = native.join("libtriaevum_title_aot.so");
    let plugin_sha = sha256_hex(b"ELF fake title plugin");
    assert_eq!(result["build"]["package"]["status"], "library");
    // The title library is verified in place, never copied.
    assert_eq!(*runtime.plugin_queries.borrow(), std::slice::from_ref(&plugin));
    assert_eq!(std::fs::read_dir(&native).unwrap().count(), 2);
    assert!(!fixture.package.join("private-plugins").exists());

    let title = load_json_object(&profile_root.join("TriAevum.android.title.json")).unwrap();
    assert_eq!(title["format"], "triaevum_android_title_v1");
    assert_eq!(title["plugin_library"], "libtriaevum_title_aot.so");
    assert_eq!(title["plugin_sha256"], plugin_sha);

    let profile = load_json_object(&profile_root.join("TriAevum.android.launch.json")).unwrap();
    let arguments: Vec<&str> = profile["arguments"].as_array().unwrap().iter().map(|a| a.as_str().unwrap()).collect();
    assert!(!arguments.contains(&"--title-plugin"));
    let resources = forge_prepare::paths::path_string(&fixture.package.join("resources")).unwrap();
    let value = |option: &str| arguments[arguments.iter().position(|a| *a == option).unwrap() + 1];
    // The package is outside the profile folder and there is no runtime folder.
    assert_eq!(value("--resource-root"), resources);
    assert_eq!(profile["path_scopes"]["--resource-root"], "external");
    assert_eq!(value("--config"), "${profile_dir}/data/config/TriAevum.android.json");
    assert_eq!(value("--save-data"), "${profile_dir}/data/savedata");
    assert_eq!(value("--input-admission"), "auto");
    assert_eq!(value("--height"), "360");

    let data = fixture.data();
    let state =
        load_json_object(&PathBuf::from(result["prepared"]["directory"].as_str().unwrap()).join("forge-state.json"))
            .unwrap();
    assert_eq!(state["module"]["status"], "ready");
    assert_eq!(state["module"]["container"], Value::Null);
    assert_eq!(state["module"]["sha256"], plugin_sha);
    assert_eq!(state["module"]["target_triple"], ANDROID);
    assert_eq!(state["runtime"]["target"], ANDROID);
    assert_eq!(state["runtime"]["runtime_sha256"], sha256_hex(b"fake runtime"));
    let active = load_json_object(&data.join("active-title.json")).unwrap();
    assert_eq!(active["module_sha256"], plugin_sha);
    assert_eq!(load_json_object(&data.join("config/TriAevum.android.json")).unwrap()["Graphics"]["Preset"], "Enhanced");
    assert!(!data.join("config/TriAevum.json").exists());
    assert!(data.join("savedata").is_dir());
    assert!(!profile_root.join(".forge-activation/journal.json").exists());
}
