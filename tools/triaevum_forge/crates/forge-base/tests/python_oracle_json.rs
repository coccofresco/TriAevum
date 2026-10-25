// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Compares the legacy JSON serializers with Python's json.dumps on random
//! values. Runs when TRIAEVUM_FORGE_PYTHON names a Python 3 interpreter.

use std::path::Path;
use std::process::Command;

use forge_base::legacy_json::{dumps, Options, Style};
use serde_json::Value;

#[test]
fn legacy_json_matches_python() {
    let Ok(python) = std::env::var("TRIAEVUM_FORGE_PYTHON") else {
        eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set");
        return;
    };
    let directory = tempfile::tempdir().unwrap();
    let cases_path = directory.path().join("cases.json");
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/json_cases.py");
    let status = Command::new(python).arg("-I").arg(script).arg(&cases_path).status().unwrap();
    assert!(status.success());
    let cases: Vec<Value> = serde_json::from_slice(&std::fs::read(cases_path).unwrap()).unwrap();
    let unicode = Options { style: Style::Pretty, sort_keys: true, ensure_ascii: false };
    for case in &cases {
        let input: Value = serde_json::from_str(case["input"].as_str().unwrap()).unwrap();
        for (key, options) in [
            ("pretty", Options::PRETTY),
            ("compact", Options::COMPACT),
            ("spaced", Options::SPACED),
            ("pretty_unicode", unicode),
        ] {
            assert_eq!(dumps(&input, options), case[key].as_str().unwrap(), "{key} of {}", case["input"]);
        }
    }
    assert!(cases.len() >= 1000);
}
