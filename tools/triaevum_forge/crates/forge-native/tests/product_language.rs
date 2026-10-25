// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Ports of test_product_contract.py, test_game_language.py and
//! test_native_process.py against the fake runtime.

mod common;

use std::ffi::OsString;
use std::path::Path;
use std::time::Instant;

use common::{install_fake, runs, set_plan};
use forge_native::process::{run_native, Environment, RunOptions};
use forge_native::{language, product, DesktopServices};
use serde_json::{json, Map, Value};

fn fixture_product() -> Map<String, Value> {
    json!({
        "format": "triaevum_product_info_v1",
        "runtime": "oot3d_native_game",
        "source_commit": "a".repeat(40),
        "whole_aot_plugin_abi": 2,
        "private_title_loaded": false,
        "capabilities": {"nri": true, "f1": true, "topscreen": true},
        "default_config": {"Graphics": {"Preset": "Enhanced", "FrameRate": {"Mode": "Interpolated2x"}}},
    })
    .as_object()
    .unwrap()
    .clone()
}

#[test]
fn rejects_old_host_missing_features_and_wrong_commit() {
    for (key, value) in [
        ("runtime", json!("triaevum_oot3d_module_host")),
        ("whole_aot_plugin_abi", json!(1)),
        ("capabilities", json!({})),
    ] {
        let mut info = fixture_product();
        info.insert(key.into(), value);
        assert!(product::validate_product_info(&info, "").is_err());
    }
    let error = product::validate_product_info(&fixture_product(), &"b".repeat(40)).unwrap_err();
    assert!(error.message.contains("commit"));
    let mut info = fixture_product();
    info["default_config"]["Graphics"]["Preset"] = json!("Custom");
    assert!(product::validate_product_info(&info, "").unwrap_err().message.contains("Enhanced"));
    let mut info = fixture_product();
    info["whole_aot_plugin_abi"] = json!(2.0);
    product::validate_product_info(&info, "").unwrap();
}

#[test]
fn initializes_x2_only_for_new_configuration() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("config.json");
    assert!(product::ensure_runtime_config(&path, &fixture_product()).unwrap());
    let written: Value = serde_json::from_slice(&std::fs::read(&path).unwrap()).unwrap();
    assert_eq!(written["Graphics"]["FrameRate"]["Mode"], "Interpolated2x");
    let custom = br#"{"Graphics":{"Preset":"Authentic"},"Other":"keep"}"#;
    std::fs::write(&path, custom).unwrap();
    assert!(!product::ensure_runtime_config(&path, &fixture_product()).unwrap());
    assert_eq!(std::fs::read(&path).unwrap(), custom);
    std::fs::write(&path, b"invalid").unwrap();
    let error = product::ensure_runtime_config(&path, &fixture_product()).unwrap_err();
    assert_eq!(error.message, "Expecting value: line 1 column 1 (char 0)");
    assert_eq!(std::fs::read(&path).unwrap(), b"invalid");
}

#[test]
fn query_hashes_the_queried_binary_and_skips_leading_lines() {
    let directory = tempfile::tempdir().unwrap();
    let document = serde_json::to_string_pretty(&Value::Object(fixture_product())).unwrap();
    let exe = install_fake(directory.path(), "TriAevum", &json!({"stdout": format!("overlay hooked\r\n{document}\n")}));
    let receipt = product::query_product(&DesktopServices, &exe, &"a".repeat(40), None).unwrap();
    assert_eq!(receipt.product, fixture_product());
    assert_eq!(receipt.runtime_sha256.len(), 64);
    let run = &runs(&exe)[0];
    assert_eq!(run["args"], json!(["--product-info"]));
    assert_eq!(Path::new(run["cwd"].as_str().unwrap()), forge_prepare::paths::resolve(directory.path()).unwrap());
}

#[test]
fn selected_plugin_query_requires_native_abi_success() {
    let directory = tempfile::tempdir().unwrap();
    let mut info = fixture_product();
    let exe = install_fake(directory.path(), "TriAevum", &json!({"stdout": Value::Object(info.clone()).to_string()}));
    let plugin = directory.path().join("private.dll");
    std::fs::write(&plugin, b"plugin").unwrap();
    let error = product::query_product(&DesktopServices, &exe, "", Some(&plugin)).unwrap_err();
    assert_eq!(error.message, "The selected private title failed its native ABI preflight");
    info.insert("private_title_loaded".into(), json!(true));
    set_plan(&exe, &json!({"stdout": Value::Object(info).to_string()}));
    product::query_product(&DesktopServices, &exe, "", Some(&plugin)).unwrap();
    let plugin = forge_prepare::paths::resolve(&plugin).unwrap();
    assert_eq!(runs(&exe)[1]["args"], json!(["--verify-title-plugin", plugin.to_string_lossy()]));
}

#[test]
fn windows_loader_failure_names_dependency_not_rom() {
    let directory = tempfile::tempdir().unwrap();
    let exe = install_fake(directory.path(), "TriAevum", &json!({"exit": -1073741511}));
    let error = product::query_product(&DesktopServices, &exe, "", None).unwrap_err();
    if cfg!(windows) {
        assert!(
            error.message.starts_with("Runtime preflight failed (0xC0000139: a required DLL export"),
            "{}",
            error.message
        );
        assert!(error.message.contains("before ROM processing"));
    }
    let log: Value =
        serde_json::from_slice(&std::fs::read(directory.path().join("TriAevum-preflight-error.json")).unwrap())
            .unwrap();
    if cfg!(windows) {
        assert_eq!(log["exit_status"], "0xC0000139");
    }
    assert_eq!(log["operation"], "product-info");
    assert_eq!(log["stdout"], "");
    assert_eq!(log["runtime_sha256"].as_str().unwrap().len(), 64);
}

#[test]
fn clean_exit_without_product_information_is_logged() {
    let directory = tempfile::tempdir().unwrap();
    let exe = install_fake(directory.path(), "TriAevum", &json!({"stderr": "loader note"}));
    let error = product::query_product(&DesktopServices, &exe, "", None).unwrap_err();
    assert!(error.message.starts_with(
        "Runtime preflight failed (exited normally without product information \
         (Expecting value: line 1 column 1 (char 0))): loader note\nDiagnostic log: "
    ));
    let log: Value =
        serde_json::from_slice(&std::fs::read(directory.path().join("TriAevum-preflight-error.json")).unwrap())
            .unwrap();
    assert_eq!(log["exit_status"], "0x00000000");
    assert_eq!(log["stderr"], "loader note");
}

#[test]
fn log_write_failure_does_not_hide_loader_error() {
    let directory = tempfile::tempdir().unwrap();
    let exe = install_fake(directory.path(), "TriAevum", &json!({"exit": 3}));
    std::fs::create_dir(directory.path().join("TriAevum-preflight-error.json")).unwrap();
    let error = product::query_product(&DesktopServices, &exe, "", None).unwrap_err();
    assert!(
        error.message.starts_with("Runtime preflight failed (3): \nCould not save diagnostic log at "),
        "{}",
        error.message
    );
}

#[test]
fn missing_plugin_cannot_be_queried() {
    let directory = tempfile::tempdir().unwrap();
    let exe = install_fake(directory.path(), "TriAevum", &json!({}));
    let error =
        product::query_product(&DesktopServices, &exe, "", Some(&directory.path().join("absent.dll"))).unwrap_err();
    assert!(error.message.starts_with("Cannot query the playable runtime: ["), "{}", error.message);
    assert!(runs(&exe).is_empty());
}

fn document(codes: &[&str]) -> Value {
    let available: Vec<Value> = codes
        .iter()
        .map(|code| json!({"code": code, "label": code, "system_id": if *code == "en" { 1 } else { 4 }}))
        .collect();
    json!({"format": language::FORMAT, "selected": codes[0], "available": available})
}

fn selected(root: &Path) -> Value {
    let written: Value = serde_json::from_slice(&std::fs::read(language::config_path(root)).unwrap()).unwrap();
    written["selected"].clone()
}

#[test]
fn shared_config_and_rom_switch() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    language::install(root, &document(&["en", "it"])).unwrap();
    language::select(root, "it").unwrap();
    language::install(root, &document(&["en", "it"])).unwrap();
    assert_eq!(selected(root), "it");
    assert!(language::select(root, "fr").is_err());
    assert_eq!(selected(root), "it");
    language::install(root, &document(&["en"])).unwrap();
    assert_eq!(selected(root), "en");
}

#[test]
fn a_preferred_language_wins_while_the_rom_offers_it() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    language::install_preferring(root, &document(&["en", "it", "fr"]), Some("fr")).unwrap();
    assert_eq!(selected(root), "fr");
    // An unoffered preference keeps the previous selection, then the default.
    language::install_preferring(root, &document(&["en", "fr"]), Some("de")).unwrap();
    assert_eq!(selected(root), "fr");
    language::install_preferring(root, &document(&["en", "it"]), Some("de")).unwrap();
    assert_eq!(selected(root), "en");
    language::install_preferring(root, &document(&["en", "it"]), None).unwrap();
    assert_eq!(selected(root), "en");
    let merged = language::merged(root, &document(&["en", "it"]), Some("it")).unwrap();
    assert_eq!(merged["selected"], "it");
    assert_eq!(selected(root), "en", "merged writes nothing");
    // A malformed file stays an error with a preference too.
    std::fs::write(language::config_path(root), "broken").unwrap();
    assert!(language::install_preferring(root, &document(&["en", "it"]), Some("it")).is_err());
    assert_eq!(std::fs::read(language::config_path(root)).unwrap(), b"broken");
}

#[test]
fn reads_a_cartridge_romfs_in_place() {
    let directory = tempfile::tempdir().unwrap();
    let runtime = install_fake(directory.path(), "runtime", &json!({"stdout": document(&["en"]).to_string()}));
    let rom = directory.path().join("game.cci");
    let query = forge_native::LanguageQuery { romfs: rom.clone(), extent: Some((0x4000, 0x8000)) };
    language::discover_query(&DesktopServices, &runtime, &query).unwrap();
    assert_eq!(
        runs(&runtime)[0]["args"],
        json!(["--game-language-info", rom.to_string_lossy(), "--romfs-offset", "16384", "--romfs-size", "32768"])
    );
}

#[test]
fn uses_native_detector() {
    let directory = tempfile::tempdir().unwrap();
    let runtime = install_fake(directory.path(), "runtime", &json!({"stdout": document(&["en", "it"]).to_string()}));
    let romfs = directory.path().join("romfs.bin");
    let detected = language::discover(&DesktopServices, &runtime, &romfs).unwrap();
    assert_eq!(Value::Object(detected), document(&["en", "it"]));
    assert_eq!(runs(&runtime)[0]["args"], json!(["--game-language-info", romfs.to_string_lossy()]));
    set_plan(&runtime, &json!({"exit": 2, "stderr": "truncated RomFS"}));
    let error = language::discover(&DesktopServices, &runtime, &romfs).unwrap_err();
    assert_eq!(error.message, "Cannot inspect ROM languages: truncated RomFS");
}

#[test]
fn rejects_invalid_metadata() {
    assert!(language::validate(&json!([])).is_err());
    for (key, value) in [("available", json!([])), ("selected", json!("fr")), ("format", json!("unknown"))] {
        let mut changed = document(&["en", "it"]);
        changed[key] = value;
        assert!(language::validate(&changed).is_err(), "{key}");
    }
    for entry in [
        json!({"code": "en", "label": "", "system_id": 1}),
        json!({"code": "en", "label": "en", "system_id": 12}),
        json!({"code": "en", "label": "en", "system_id": true}),
        json!({"code": "en", "label": "en", "system_id": 1.0}),
        json!({"code": 1, "label": "en", "system_id": 1}),
    ] {
        let invalid = json!({"format": language::FORMAT, "selected": "en", "available": [entry]});
        assert_eq!(language::validate(&invalid).unwrap_err().message, "Invalid game language entry");
    }
    let duplicate = json!({"format": language::FORMAT, "selected": "en", "available": [
        {"code": "en", "label": "a", "system_id": 1}, {"code": "en", "label": "b", "system_id": 2}]});
    assert!(language::validate(&duplicate).is_err());
}

#[test]
fn preserves_malformed_preferences() {
    let directory = tempfile::tempdir().unwrap();
    let path = language::config_path(directory.path());
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(&path, "broken").unwrap();
    assert!(language::install(directory.path(), &document(&["en", "it"])).is_err());
    assert_eq!(std::fs::read(&path).unwrap(), b"broken");
}

fn options(directory: &Path, timeout_seconds: f64) -> RunOptions {
    RunOptions { cwd: directory.to_path_buf(), environment: Environment::Inherit, timeout_seconds, creation_flags: 0 }
}

#[test]
fn native_run_captures_checks_and_times_out() {
    let directory = tempfile::tempdir().unwrap();
    let helper = install_fake(directory.path(), "helper", &json!({"stdout": "native\n", "stderr": "e", "exit": 9}));
    let command: Vec<OsString> = vec![helper.clone().into(), "--x".into()];
    let completed = run_native(&command, &options(directory.path(), 30.0), None).unwrap();
    assert_eq!(
        (completed.status, completed.stdout.as_slice(), completed.stderr.as_slice()),
        (9, &b"native\n"[..], &b"e"[..])
    );
    set_plan(&helper, &json!({"sleep_ms": 20000}));
    let started = Instant::now();
    let error = run_native(&command, &options(directory.path(), 0.2), None).unwrap_err();
    assert!(started.elapsed().as_secs() < 10);
    let expected = format!(
        "Command '[{}, '--x']' timed out after 0.2 seconds",
        forge_catalog::pyjson::repr_str(&helper.to_string_lossy())
    );
    assert_eq!(error.message, expected);
}

#[test]
fn cancellation_kills_the_helper() {
    let directory = tempfile::tempdir().unwrap();
    let helper = install_fake(directory.path(), "helper", &json!({"sleep_ms": 20000}));
    let cancel = forge_base::CancelToken::new();
    cancel.cancel();
    let error = run_native(&[helper.into()], &options(directory.path(), 30.0), Some(&cancel)).unwrap_err();
    assert_eq!(error.kind, forge_base::ErrorKind::Cancelled);
}

#[test]
fn missing_helper_is_an_os_error() {
    let directory = tempfile::tempdir().unwrap();
    let missing = directory.path().join("missing.exe");
    let error = run_native(&[missing.into()], &options(directory.path(), 5.0), None).unwrap_err();
    let prefix = if cfg!(windows) { "[WinError 2] " } else { "[Errno 2] " };
    assert!(error.message.starts_with(prefix), "{}", error.message);
}

#[cfg(target_os = "linux")]
#[test]
fn linux_helper_uses_verified_siblings() {
    let directory = tempfile::tempdir().unwrap();
    let helper = install_fake(directory.path(), "helper", &json!({}));
    let environment = forge_native::process::native_helper_environment(&helper).unwrap();
    let value =
        environment.iter().find(|(name, _)| name == "LD_LIBRARY_PATH").unwrap().1.to_string_lossy().into_owned();
    let sibling = forge_prepare::paths::resolve(directory.path()).unwrap();
    assert!(value == sibling.to_string_lossy() || value.starts_with(&format!("{}:", sibling.display())));
}
