// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The JNI bridge's byte functions (what `ForgeNative.inspect` and
//! `ForgeNative.explain` return) against the shared explanation fixtures.
//! The JVM array marshalling itself is exercised on the device.

use serde_json::Value;

const FIXTURES: &str = include_str!("../../forge-core/tests/fixtures/explain_events.json");

#[test]
fn explain_returns_the_engine_explanation_of_every_fixture() {
    let document: Value = serde_json::from_str(FIXTURES).unwrap();
    for case in document["cases"].as_array().unwrap() {
        let input = serde_json::to_vec(&case["input"]).unwrap();
        let bytes = triaevum_forge::explain_json(&input);
        assert_eq!(bytes, forge_core::explain::explain_bytes(&input), "{}", case["name"]);
        let explanation: Value = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(explanation["id"].as_str(), case["id"].as_str(), "{}", case["name"]);
    }
}

#[test]
fn inspect_answers_with_a_state_for_any_request() {
    let directory = tempfile::tempdir().unwrap();
    let request = serde_json::json!({
        "format": "triaevum_forge_inspect_v1",
        "package_root": directory.path().to_string_lossy(),
    });
    let bytes = triaevum_forge::inspect_json(request.to_string().as_bytes());
    let state: Value = serde_json::from_slice(&bytes).unwrap();
    assert_eq!(state["format"], "triaevum_installation_state_v1");
    assert_eq!(state["state"], "incompatible");
    let refused: Value = serde_json::from_slice(&triaevum_forge::inspect_json(b"[]")).unwrap();
    assert_eq!(refused["state"], "failed");
    assert_eq!(refused["explanation"]["id"], "request.invalid");
}

#[test]
fn inventory_resolves_the_android_installation() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let request = serde_json::json!({
        "format": "triaevum_forge_inventory_v1",
        "package_root": root.join("files/forge-package").to_string_lossy(),
        "android": {
            "native_library_dir": root.join("lib").to_string_lossy(),
            "profile_root": root.join("external").to_string_lossy(),
            "app_data_dir": root.join("app").to_string_lossy(),
            "package_name": "org.triaevum.android",
        },
    });
    let bytes = triaevum_forge::inventory_json(request.to_string().as_bytes());
    assert_eq!(bytes, forge_core::inventory::inventory_bytes(request.to_string().as_bytes()));
    let inventory: Value = serde_json::from_slice(&bytes).unwrap();
    assert_eq!(inventory["status"], "ok", "{inventory}");
    assert_eq!(inventory["platform"], "android");
    let names: Vec<&str> =
        inventory["entries"].as_array().unwrap().iter().filter_map(|entry| entry["logical_name"].as_str()).collect();
    assert!(names.contains(&"touch_preferences") && names.contains(&"save_data"), "{names:?}");
    let refused: Value = serde_json::from_slice(&triaevum_forge::inventory_json(b"[]")).unwrap();
    assert_eq!(refused["status"], "failed");
    assert_eq!(refused["explanation"]["id"], "request.invalid");
}

#[test]
fn diagnostics_write_the_bundle_the_installer_shares() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let external = root.join("external");
    // The game writes its logs to the data folder (D/logs) since WP-05C.
    let logs = external.join("data").join("logs");
    std::fs::create_dir_all(&logs).unwrap();
    std::fs::write(logs.join("native-stderr.log"), b"TRIAEVUM_FATAL no device\n").unwrap();
    let bundle = root.join("cache").join("share").join("TriAevum-diagnostics.zip");
    let request = serde_json::json!({
        "format": "triaevum_forge_diagnostics_v1",
        "package_root": root.join("files/forge-package").to_string_lossy(),
        "android": {
            "native_library_dir": root.join("lib").to_string_lossy(),
            "profile_root": external.to_string_lossy(),
            "app_data_dir": root.join("app").to_string_lossy(),
            "package_name": "org.triaevum.android",
        },
        "bundle": bundle.to_string_lossy(),
    });
    let answer: Value =
        serde_json::from_slice(&triaevum_forge::diagnostics_json(request.to_string().as_bytes())).unwrap();
    assert_eq!(answer["status"], "ok", "{answer}");
    assert!(answer["text"].as_str().unwrap().contains("Last error reported by the game: no device"));
    assert!(bundle.is_file());
    assert!(answer["bundle"]["members"].as_array().unwrap().iter().any(|name| name == "game-logs/native-stderr.log"));
    let refused: Value = serde_json::from_slice(&triaevum_forge::diagnostics_json(b"[]")).unwrap();
    assert_eq!(refused["status"], "failed");
}
