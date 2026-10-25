// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! End-to-end oracle: the same ROM installed by the Python Forge and by this
//! engine, into the same package path (one after the other, the Python
//! output moved aside), must write the same files with the same bytes.
//!
//! Local only (real ROM, real runtime, real GPU helpers). Runs when
//! TRIAEVUM_FORGE_PYTHON (a Python 3 interpreter), TRIAEVUM_FORGE_E2E_PACKAGE
//! (a complete Windows package without `data/` or `private-plugins/`) and
//! TRIAEVUM_FORGE_E2E_ROM (a decrypted .cci/.3ds) are set. The package is
//! never modified: each run gets a hard-linked copy in
//! TRIAEVUM_FORGE_E2E_WORK (default: `<package>/../e2e-work`, same volume),
//! removed afterwards unless TRIAEVUM_FORGE_E2E_KEEP is set.
//!
//! Every difference is printed and classified: path/time context allowed by
//! FORGE_SPEC.md 3.2 (hash-cache mtimes, native timings, GPU driver caches)
//! or a real mismatch, which fails the test for the byte-identity set of
//! spec 3.1.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::Command;

use forge_base::CancelToken;
use forge_core::install::{install_private_title, InstallRequest, InstallSource, InstallTarget};
use forge_native::DesktopServices;
use serde_json::Value;

fn env_path(name: &str) -> Option<PathBuf> {
    std::env::var_os(name).filter(|value| !value.is_empty()).map(PathBuf::from)
}

fn repo_root() -> PathBuf {
    forge_prepare::paths::resolve(&Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../..")).unwrap()
}

/// Recreates `source` under `destination` with hard links (directories are
/// created; nothing is copied or written through).
fn link_tree(source: &Path, destination: &Path) {
    std::fs::create_dir_all(destination).unwrap();
    for entry in std::fs::read_dir(source).unwrap() {
        let entry = entry.unwrap();
        let target = destination.join(entry.file_name());
        if entry.file_type().unwrap().is_dir() {
            link_tree(&entry.path(), &target);
        } else {
            std::fs::hard_link(entry.path(), &target).unwrap();
        }
    }
}

fn files(root: &Path) -> BTreeMap<String, PathBuf> {
    fn walk(root: &Path, directory: &Path, out: &mut BTreeMap<String, PathBuf>) {
        let Ok(entries) = std::fs::read_dir(directory) else { return };
        for entry in entries.flatten() {
            let path = entry.path();
            if entry.file_type().unwrap().is_dir() {
                walk(root, &path, out);
            } else {
                let relative = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
                out.insert(relative, path);
            }
        }
    }
    let mut out = BTreeMap::new();
    walk(root, root, &mut out);
    out
}

/// Key paths where two JSON values differ.
fn json_diff(path: &str, left: &Value, right: &Value, out: &mut Vec<String>) {
    match (left, right) {
        (Value::Object(a), Value::Object(b)) => {
            for key in a.keys().chain(b.keys().filter(|key| !a.contains_key(*key))) {
                let child = format!("{path}.{key}");
                match (a.get(key), b.get(key)) {
                    (Some(x), Some(y)) => json_diff(&child, x, y, out),
                    _ => out.push(child),
                }
            }
        }
        (Value::Array(a), Value::Array(b)) if a.len() == b.len() => {
            for (index, (x, y)) in a.iter().zip(b).enumerate() {
                json_diff(&format!("{path}[{index}]"), x, y, out);
            }
        }
        _ if left != right => out.push(format!("{path}: python={left} rust={right}")),
        _ => {}
    }
}

/// Native timings and file times (spec 3.2), judged by the last key of a
/// difference (`.a.b.key: python=... rust=...`; keys may hold paths).
fn time_context(difference: &str) -> bool {
    let key_path = difference.split(": python=").next().unwrap_or_default();
    let key = key_path.rsplit('.').next().unwrap_or_default();
    key == "ms" || ["seconds", "_ms", "mtime_ns", "elapsed", "wall"].iter().any(|word| key.contains(word))
}

/// Files whose bytes legitimately depend on the GPU, the driver or the
/// clock: compared, reported, never failing.
fn informational(relative: &str) -> bool {
    relative.starts_with("data/cache/renderer/")
        || relative.starts_with("data/shader-seeds/device-preparation/")
        || relative.starts_with("data/shader-seeds/renderer-preparation/")
        || relative == "data/runtime-state.json"
}

fn run_python(python: &Path, package: &Path, rom: &Path, data: &Path, result: &Path) {
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/install_e2e.py");
    let status = Command::new(python)
        .arg("-I")
        .arg(script)
        .args([repo_root().as_path(), package, rom, data, result])
        .status()
        .unwrap();
    assert!(status.success(), "Python install failed: {status}");
}

/// Moves a run's outputs (private data and plugin generations) aside.
fn collect(package: &Path, into: &Path) {
    std::fs::create_dir_all(into).unwrap();
    for name in ["data", "private-plugins"] {
        if package.join(name).exists() {
            std::fs::rename(package.join(name), into.join(name)).unwrap();
        }
    }
}

#[test]
fn python_and_rust_install_the_same_files() {
    let (Some(python), Some(base), Some(rom)) =
        (env_path("TRIAEVUM_FORGE_PYTHON"), env_path("TRIAEVUM_FORGE_E2E_PACKAGE"), env_path("TRIAEVUM_FORGE_E2E_ROM"))
    else {
        return eprintln!(
            "skipped: TRIAEVUM_FORGE_PYTHON, TRIAEVUM_FORGE_E2E_PACKAGE and TRIAEVUM_FORGE_E2E_ROM are required"
        );
    };
    assert!(!base.join("data").exists() && !base.join("private-plugins").exists(), "the package must be clean");
    let work = env_path("TRIAEVUM_FORGE_E2E_WORK").unwrap_or_else(|| base.with_file_name("e2e-work"));
    assert!(!work.exists(), "{} exists; remove it first", work.display());
    let package = work.join("pkg");
    let data = package.join("data");

    link_tree(&base, &package);
    let before = files(&package);
    run_python(&python, &package, &rom, &data, &work.join("python-result.json"));
    collect(&package, &work.join("python"));
    let python_package = files(&package);
    std::fs::remove_dir_all(&package).unwrap();

    link_tree(&base, &package);
    let request = InstallRequest {
        source: InstallSource::Rom(rom),
        data_root: Some(data.clone()),
        package_root: package.clone(),
        target: InstallTarget::Package,
        game_language: None,
    };
    let mut report = |stage: &str, message: &str| eprintln!("[rust {stage}] {message}");
    let result = install_private_title(request, &DesktopServices, &mut report, &CancelToken::new()).unwrap();
    std::fs::write(work.join("rust-result.json"), serde_json::to_vec_pretty(&result).unwrap()).unwrap();
    collect(&package, &work.join("rust"));
    let rust_package = files(&package);

    let mut failures = Vec::new();
    let mut report_line = |class: &str, line: String| {
        println!("[{class}] {line}");
        if class == "MISMATCH" {
            failures.push(line);
        }
    };
    // The package files each run left (the runtime placement must agree).
    let keys = |map: &BTreeMap<String, PathBuf>| map.keys().cloned().collect::<Vec<_>>();
    if keys(&python_package) != keys(&rust_package) {
        report_line("MISMATCH", format!("package files differ after the runs (before: {})", before.len()));
    }

    let python_files = files(&work.join("python"));
    let rust_files = files(&work.join("rust"));
    for name in python_files.keys().filter(|name| !rust_files.contains_key(*name)) {
        report_line(if informational(name) { "context" } else { "MISMATCH" }, format!("only Python wrote {name}"));
    }
    for name in rust_files.keys().filter(|name| !python_files.contains_key(*name)) {
        report_line(if informational(name) { "context" } else { "MISMATCH" }, format!("only Rust wrote {name}"));
    }
    let mut identical = 0;
    for (name, python_path) in &python_files {
        let Some(rust_path) = rust_files.get(name) else { continue };
        let (left, right) = (std::fs::read(python_path).unwrap(), std::fs::read(rust_path).unwrap());
        if left == right {
            identical += 1;
            continue;
        }
        let parsed = (serde_json::from_slice::<Value>(&left), serde_json::from_slice::<Value>(&right));
        let mut differences = Vec::new();
        if let (Ok(a), Ok(b)) = &parsed {
            json_diff("", a, b, &mut differences);
        }
        let timing_only = !differences.is_empty() && differences.iter().all(|path| time_context(path));
        let class = if informational(name) || timing_only { "context" } else { "MISMATCH" };
        let detail = if differences.is_empty() {
            format!("{} vs {} bytes", left.len(), right.len())
        } else {
            differences.iter().take(12).cloned().collect::<Vec<_>>().join("; ")
        };
        report_line(class, format!("{name}: {detail}"));
    }
    println!("{identical} of {} files byte-identical", python_files.len());

    let python_result: Value =
        serde_json::from_slice(&std::fs::read(work.join("python-result.json")).unwrap()).unwrap();
    let mut differences = Vec::new();
    json_diff("result", &python_result, &result, &mut differences);
    // Native receipts may also differ in warm/cold counters; timing lines are
    // summarized per key.
    let mut timings: BTreeMap<String, usize> = BTreeMap::new();
    for difference in differences {
        let native = ["result.build.interior_sun", "result.build.renderer_shader", "result.build.device_pipeline"]
            .iter()
            .any(|prefix| difference.starts_with(prefix));
        if time_context(&difference) {
            let key = difference.split(": python=").next().unwrap_or_default();
            let key: String = key
                .split('[')
                .map(|part| part.split_once(']').map_or(part, |(_, rest)| rest))
                .collect::<Vec<_>>()
                .join("[]");
            *timings.entry(key).or_default() += 1;
        } else {
            report_line(if native { "context" } else { "MISMATCH" }, difference);
        }
    }
    for (key, count) in timings {
        report_line("context", format!("{key}: {count} timing value(s) differ"));
    }

    if std::env::var_os("TRIAEVUM_FORGE_E2E_KEEP").is_none() {
        std::fs::remove_dir_all(&work).unwrap();
    }
    assert!(failures.is_empty(), "{} real mismatches", failures.len());
}
