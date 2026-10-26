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
