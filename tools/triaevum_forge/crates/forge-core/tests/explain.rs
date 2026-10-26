// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The explanation catalogue against the shared event fixtures
//! (`tests/fixtures/explain_events.json`, also run through the desktop, CLI
//! and Android consumers): total over the 16 kinds and every stage, each
//! structured refinement reachable, details redacted, and the C ABI entry
//! byte-identical to the Rust one.

use std::collections::{HashMap, HashSet};

use forge_core::abi::{TriAevumForgeExplain, TriAevumForgeFreeBuffer};
use forge_core::explain::{explain_bytes, explain_value, REFINEMENTS};
use forge_core::ErrorKind;
use serde_json::Value;

const FIXTURES: &str = include_str!("fixtures/explain_events.json");

fn cases() -> Vec<Value> {
    let document: Value = serde_json::from_str(FIXTURES).unwrap();
    assert_eq!(document["format"], "triaevum_explain_fixtures_v1");
    document["cases"].as_array().unwrap().clone()
}

#[test]
fn every_fixture_has_its_expected_explanation() {
    for case in cases() {
        let explanation = explain_value(&case["input"]);
        let id = explanation.as_ref().map(|explanation| explanation.id);
        assert_eq!(id, case["id"].as_str(), "{}", case["name"]);
        if let Some(explanation) = explanation {
            for text in [explanation.title, explanation.cause, explanation.next_action] {
                assert!(!text.trim().is_empty(), "{}", case["name"]);
            }
        }
    }
}

#[test]
fn every_kind_and_stage_class_is_explained_distinctly() {
    let mut titles = HashMap::new();
    let mut next_actions = HashMap::new();
    let mut details: HashMap<String, HashSet<String>> = HashMap::new();
    for case in cases().iter().filter(|case| case["input"]["error"]["code"].is_string()) {
        let code = case["input"]["error"]["code"].as_str().unwrap();
        let Some(kind) = ErrorKind::from_code(code) else { continue };
        let structured = ["operation", "subject", "reason"].iter().any(|key| case["input"]["error"].get(key).is_some());
        if case["id"] != code || structured {
            continue;
        }
        let explanation = explain_value(&case["input"]).unwrap();
        titles.entry(kind).or_insert(explanation.title);
        assert_eq!(titles[&kind], explanation.title, "one title per kind for every stage");
        next_actions.entry(kind).or_insert(explanation.next_action);
        details.entry(code.to_string()).or_default().insert(explanation.details.clone());
        assert!(explanation.details.contains(r"C:\Users\<user>\Games"), "{}", explanation.details);
        assert!(!explanation.details.contains("Someone"));
    }
    assert_eq!(titles.len(), 16, "all 16 kinds have fixtures");
    assert_eq!(titles.values().collect::<HashSet<_>>().len(), 16, "titles are distinct");
    assert_eq!(next_actions.values().collect::<HashSet<_>>().len(), 16, "next actions are distinct");
    for (code, variants) in details {
        assert_eq!(variants.len(), 3, "{code}: known, null and unknown stages give distinct details");
    }
}

#[test]
fn every_refinement_has_a_fixture() {
    let covered: HashSet<String> = cases().iter().filter_map(|case| case["id"].as_str().map(str::to_string)).collect();
    for refinement in REFINEMENTS {
        assert!(covered.contains(refinement.id), "no fixture for {}", refinement.id);
    }
}

#[test]
fn the_c_abi_returns_the_same_bytes() {
    for case in cases() {
        let input = serde_json::to_vec(&case["input"]).unwrap();
        let buffer = unsafe { TriAevumForgeExplain(input.as_ptr(), input.len()) };
        let bytes = unsafe { std::slice::from_raw_parts(buffer.data, buffer.len) }.to_vec();
        unsafe { TriAevumForgeFreeBuffer(buffer) };
        assert_eq!(bytes, explain_bytes(&input), "{}", case["name"]);
        let value: Value = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(value["id"].as_str(), case["id"].as_str(), "{}", case["name"]);
    }
    let garbage = b"not json";
    let buffer = unsafe { TriAevumForgeExplain(garbage.as_ptr(), garbage.len()) };
    assert_eq!(unsafe { std::slice::from_raw_parts(buffer.data, buffer.len) }, b"null\n");
    unsafe { TriAevumForgeFreeBuffer(buffer) };
}
