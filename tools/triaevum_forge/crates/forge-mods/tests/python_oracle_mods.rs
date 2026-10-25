// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Compares the Rust port with the Python Forge on synthetic fixtures: the
//! trees of prepared UI packs, RomFS mod stores, O3RO overlays and receipts
//! (raw bytes), error messages, reports, and the download retry behavior.
//! Runs when TRIAEVUM_FORGE_PYTHON names a Python 3 interpreter.

mod common;

use std::collections::VecDeque;
use std::fs;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Mutex;
use std::time::Duration;

use common::*;
use forge_base::hashing::{hex, sha256_hex};
use forge_base::CancelToken;
use forge_mods::download::{download_archive, download_verified, VerifiedDownload};
use forge_mods::{ArchiveContract, Downloader, HttpError, HttpRequest, HttpResponse, RomFsMod};
use serde_json::{json, Map, Value};

fn python() -> Option<String> {
    let python = std::env::var("TRIAEVUM_FORGE_PYTHON").ok();
    if python.is_none() {
        eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set");
    }
    python
}

fn oracle(script: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle").join(script)
}

fn run_python(python: &str, script: &str, args: &[&Path]) {
    let status = Command::new(python).arg("-I").arg(oracle(script)).args(args).status().unwrap();
    assert!(status.success(), "{script} failed");
}

#[test]
fn prepared_trees_receipts_and_overlays_match_python() {
    let Some(python) = python() else { return };
    let directory = tempfile::tempdir().unwrap();
    let fixtures = Fixtures::create(&directory.path().join("fixtures"));
    let py_out = directory.path().join("py");
    let rs_out = directory.path().join("rs");
    let spec_path = directory.path().join("spec.json");
    fs::write(&spec_path, serde_json::to_vec(&fixtures.spec(&py_out)).unwrap()).unwrap();
    run_python(&python, "mods_cases.py", &[&spec_path]);
    let rust = run_rust(&fixtures, &rs_out);
    let python_result: Value = serde_json::from_slice(&fs::read(py_out.join("results.json")).unwrap()).unwrap();

    // Results, reports and error messages.
    let (py_norm, rs_norm) = (normalize(&python_result, &py_out), normalize(&rust, &rs_out));
    let mut keys: Vec<&String> =
        py_norm["results"].as_object().unwrap().keys().chain(rs_norm["results"].as_object().unwrap().keys()).collect();
    keys.sort();
    keys.dedup();
    for key in keys {
        assert_eq!(py_norm["results"][key], rs_norm["results"][key], "result {key}");
    }
    let mut names: Vec<&String> =
        py_norm["errors"].as_object().unwrap().keys().chain(rs_norm["errors"].as_object().unwrap().keys()).collect();
    names.sort();
    names.dedup();
    for name in names {
        // JSON parser diagnostics differ between Python's json and serde_json; the
        // failure itself must still happen on both sides.
        if name == "settings_not_json" {
            assert!(py_norm["errors"][name].is_string() && rs_norm["errors"][name].is_string());
            continue;
        }
        assert_eq!(py_norm["errors"][name], rs_norm["errors"][name], "error {name}");
    }
    assert!(py_norm["errors"].as_object().unwrap().len() >= 17);

    // Every produced file, byte for byte (SHA-256), and the folder layout.
    let (py_tree, rs_tree) = (tree(&py_out), tree(&rs_out));
    assert_eq!(py_tree.keys().collect::<Vec<_>>(), rs_tree.keys().collect::<Vec<_>>());
    for (path, digest) in &py_tree {
        assert_eq!(digest, &rs_tree[path], "{path}");
    }
    eprintln!(
        "compared {} tree entries, {} results, {} error messages; bsdtar route: {}",
        py_tree.len(),
        py_norm["results"].as_object().unwrap().len(),
        py_norm["errors"].as_object().unwrap().len(),
        rust["results"]["bsdtar"]["available"]
    );
    assert!(py_tree.keys().any(|path| path.ends_with("romfs_overlay.o3ro")));
    assert!(py_tree.keys().any(|path| path.ends_with("import.json")));
    assert!(py_tree.keys().any(|path| path.ends_with("source.json")));
    assert!(py_tree.keys().any(|path| path.ends_with("config/romfs_mods.json")));
    assert_eq!(
        py_tree.keys().filter(|path| path.ends_with(".o3ro")).count(),
        if rust["results"]["bsdtar"]["available"] == true { 5 } else { 4 }
    );
}

#[test]
fn level3_fixture_builder_matches_python() {
    let Some(python) = python() else { return };
    let directory = tempfile::tempdir().unwrap();
    let fixtures = Fixtures::create(&directory.path().join("fixtures"));
    let script = directory.path().join("level3.py");
    fs::write(
        &script,
        "import sys, json\nfrom pathlib import Path\nsys.path.insert(0, 'I:/TriAevum-devwarp/tools/triaevum_release')\n\
         import test_romfs_mods as t\nfiles = {k: bytes.fromhex(v) for k, v in json.loads(Path(sys.argv[1]).read_text()).items()}\n\
         Path(sys.argv[2]).write_bytes(t.build_level3(files))\n",
    )
    .unwrap();
    let files: Map<String, Value> = fixtures.base_files.iter().map(|(k, v)| (k.clone(), json!(hex(v)))).collect();
    let input = directory.path().join("files.json");
    fs::write(&input, serde_json::to_vec(&files).unwrap()).unwrap();
    let output = directory.path().join("level3.bin");
    let status = Command::new(&python).arg("-I").arg(&script).arg(&input).arg(&output).status().unwrap();
    assert!(status.success());
    assert_eq!(fs::read(&output).unwrap(), build_level3(&fixtures.base_files));
}

// --- Downloads ------------------------------------------------------------------

enum Scripted {
    Body(Vec<u8>, String),
    Fail(HttpError),
}

struct Scripts {
    queue: Mutex<VecDeque<Scripted>>,
    requested: Mutex<Vec<String>>,
}

impl Scripts {
    fn new(queue: Vec<Scripted>) -> Self {
        Self { queue: Mutex::new(queue.into()), requested: Mutex::new(Vec::new()) }
    }
}

impl Downloader for Scripts {
    fn open(&self, request: &HttpRequest<'_>) -> Result<HttpResponse, HttpError> {
        self.requested.lock().unwrap().push(request.url.to_string());
        match self.queue.lock().unwrap().pop_front().expect("scripted response") {
            Scripted::Body(bytes, final_url) => Ok(HttpResponse { final_url, body: Box::new(Cursor::new(bytes)) }),
            Scripted::Fail(error) => Err(error),
        }
    }
}

const URL: &str = "https://example.test/archive";

fn ok(bytes: &[u8]) -> Scripted {
    Scripted::Body(bytes.to_vec(), URL.into())
}

fn status(code: u16) -> Scripted {
    Scripted::Fail(HttpError::Status { code, message: "CDN error".into() })
}

fn transport(text: &str) -> Scripted {
    Scripted::Fail(HttpError::Transport(text.into()))
}

struct Case<'a> {
    url: &'a str,
    size: u64,
    sha: String,
    attempts: u32,
    max_seconds: f64,
    existing: Option<&'a [u8]>,
}

fn run_verified(work: &Path, name: &str, script: Vec<Scripted>, case: Case<'_>) -> Value {
    let folder = work.join(name);
    fs::create_dir_all(&folder).unwrap();
    let output = folder.join("archive.zip");
    if let Some(existing) = case.existing {
        fs::write(&output, existing).unwrap();
    }
    let scripts = Scripts::new(script);
    let mut job = VerifiedDownload::new(case.url, &output, case.size, &case.sha);
    job.attempts = case.attempts;
    job.max_seconds = case.max_seconds;
    let mut retries = Vec::new();
    let result = download_verified(
        &job,
        &scripts,
        &mut |_, _| {},
        &mut |attempt, _| retries.push(attempt),
        &|_: Duration| {},
        &CancelToken::new(),
    );
    let mut leftover: Vec<String> = fs::read_dir(&folder)
        .unwrap()
        .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
        .filter(|name| name != "archive.zip")
        .collect();
    leftover.sort();
    let calls = scripts.requested.lock().unwrap().len();
    json!({
        "error": result.err().map(|error| error.message), "calls": calls, "retries": retries,
        "file": output.is_file().then(|| hex(&fs::read(&output).unwrap())), "leftover": leftover})
}

fn run_archive(work: &Path, name: &str, bodies: Vec<&[u8]>) -> Value {
    let folder = work.join(name);
    let payload = b"original archive bytes";
    let contract = ArchiveContract {
        kind: "vanilla_plus_link_1_9_archive".into(),
        name: "fixture.rar".into(),
        bytes: payload.len() as u64,
        sha256: sha256_hex(payload),
    };
    let mut mod_ = RomFsMod::vanilla_plus_link();
    mod_.downloads = vec!["https://example.invalid/primary".into(), "https://example.invalid/mirror".into()];
    let scripts = Scripts::new(bodies.into_iter().map(ok).collect());
    let (mut reports, mut progress) = (Vec::new(), Vec::new());
    let result = download_archive(
        &folder,
        &mod_,
        &contract,
        None,
        &scripts,
        &mut |done, total| progress.push(json!([done, total])),
        &mut |message| reports.push(message.to_string()),
        &CancelToken::new(),
    );
    let downloads = folder.join("downloads");
    let mut leftover: Vec<String> = fs::read_dir(&downloads)
        .map(|entries| entries.map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned()).collect())
        .unwrap_or_default();
    leftover.sort();
    let requested = scripts.requested.lock().unwrap().clone();
    let (file, error) = match result {
        Ok(path) => (Some(hex(&fs::read(path).unwrap())), None),
        Err(error) => (None, Some(error.message)),
    };
    json!({"error": error, "requested": requested, "reports": reports, "progress": progress, "file": file, "leftover": leftover})
}

fn rust_downloads(work: &Path) -> Value {
    let data = b"expected archive".to_vec();
    let sha = sha256_hex(&data);
    let case = |sha: &str| Case {
        url: URL,
        size: data.len() as u64,
        sha: sha.to_string(),
        attempts: 3,
        max_seconds: 900.0,
        existing: None,
    };
    let mut results = Map::new();
    let mut put = |name: &str, value: Value| {
        results.insert(name.to_string(), value);
    };
    put(
        "retry_then_success",
        run_verified(work, "a", vec![transport("slow CDN"), ok(b"short"), ok(&data)], case(&sha)),
    );
    put(
        "integrity",
        run_verified(work, "b", vec![ok(&vec![b'x'; data.len()])], Case { existing: Some(b"old cache"), ..case(&sha) }),
    );
    for code in [503u16, 429, 403, 404, 500] {
        put(
            &format!("status_{code}"),
            run_verified(work, &format!("c{code}"), vec![status(code), status(code), status(code)], case(&sha)),
        );
    }
    put(
        "insecure_redirect",
        run_verified(work, "d", vec![Scripted::Body(data.clone(), "http://example.test".into())], case(&sha)),
    );
    put(
        "transport_fails",
        run_verified(work, "e", vec![transport("slow"), transport("slow"), transport("slow")], case(&sha)),
    );
    put("one_attempt", run_verified(work, "f", vec![transport("slow")], Case { attempts: 1, ..case(&sha) }));
    let mut longer = data.clone();
    longer.extend(b"extra");
    put("too_long", run_verified(work, "g", vec![ok(&longer)], case(&sha)));
    put("incomplete", run_verified(work, "h", vec![ok(b"short"), ok(b"short"), ok(b"short")], case(&sha)));
    put(
        "bad_contract_http",
        run_verified(work, "i", vec![], Case { url: "http://example.test/archive", ..case(&sha) }),
    );
    put("bad_contract_size", run_verified(work, "j", vec![], Case { size: 0, ..case(&sha) }));
    put("bad_contract_sha", run_verified(work, "k", vec![], case(&sha.to_uppercase())));
    put("bad_contract_attempts", run_verified(work, "l", vec![], Case { attempts: 4, ..case(&sha) }));
    put("bad_contract_seconds", run_verified(work, "m", vec![], Case { max_seconds: 0.0, ..case(&sha) }));
    put("deadline", run_verified(work, "n", vec![], Case { attempts: 1, max_seconds: 1e-9, ..case(&sha) }));
    let payload: &[u8] = b"original archive bytes";
    put("archive_fallback", run_archive(work, "p", vec![b"<html>challenge</html>", payload]));
    put("archive_first_ok", run_archive(work, "q", vec![payload]));
    let newer: Vec<u8> = [payload, b"newer"].concat();
    put("archive_all_fail", run_archive(work, "r", vec![&newer, b"short"]));
    put("archive_both_ok_second_unused", run_archive(work, "s", vec![payload]));
    Value::Object(results)
}

#[test]
fn download_retry_and_failure_behavior_matches_python() {
    let Some(python) = python() else { return };
    let directory = tempfile::tempdir().unwrap();
    let py_work = directory.path().join("py");
    let py_result = directory.path().join("py.json");
    run_python(&python, "download_cases.py", &[&py_work, &py_result]);
    let python_value: Value = serde_json::from_slice(&fs::read(&py_result).unwrap()).unwrap();
    let rust_value = rust_downloads(&directory.path().join("rs"));
    for (name, expected) in python_value.as_object().unwrap() {
        assert_eq!(expected, &rust_value[name], "download scenario {name}");
    }
    assert_eq!(python_value.as_object().unwrap().len(), rust_value.as_object().unwrap().len());
}
