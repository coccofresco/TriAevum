// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Shared fixtures: the scriptable fake helper (`examples/fake_native_helper.rs`),
//! catalog artifact records, synthetic O3PS packs and the Python oracle runner.

#![allow(dead_code)]

use std::path::{Path, PathBuf};
use std::process::Command;

use forge_base::hashing::sha256_hex;
use serde_json::{json, Value};

/// The fake helper `cargo test` builds next to the test binaries.
pub fn fake_helper_binary() -> PathBuf {
    let current = std::env::current_exe().unwrap();
    let profile = current.parent().unwrap().parent().unwrap();
    profile.join("examples").join(format!("fake_native_helper{}", std::env::consts::EXE_SUFFIX))
}

/// Copies the fake helper to `directory/name` (the executable suffix is
/// added) with `plan`; returns its path.
pub fn install_fake(directory: &Path, name: &str, plan: &Value) -> PathBuf {
    std::fs::create_dir_all(directory).unwrap();
    let path = directory.join(format!("{name}{}", std::env::consts::EXE_SUFFIX));
    copy_executable(&fake_helper_binary(), &path);
    set_plan(&path, plan);
    path
}

/// On Linux a child that another test thread forks while this process holds
/// the new file open for writing keeps the descriptor until it execs, and
/// running the file meanwhile fails with ETXTBSY: let `cp` write it.
fn copy_executable(source: &Path, destination: &Path) {
    if cfg!(unix) {
        let status = Command::new("cp").arg(source).arg(destination).status().unwrap();
        assert!(status.success(), "cp {} failed", source.display());
    } else {
        std::fs::copy(source, destination).unwrap();
    }
}

pub fn set_plan(helper: &Path, plan: &Value) {
    std::fs::write(sidecar(helper, ".plan.json"), serde_json::to_vec(plan).unwrap()).unwrap();
}

fn sidecar(helper: &Path, suffix: &str) -> PathBuf {
    let mut path = helper.as_os_str().to_os_string();
    path.push(suffix);
    PathBuf::from(path)
}

/// One record per run of the fake helper: `{args, cwd, ld_library_path}`.
pub fn runs(helper: &Path) -> Vec<Value> {
    std::fs::read_to_string(sidecar(helper, ".args.json"))
        .unwrap_or_default()
        .lines()
        .map(|line| serde_json::from_str(line).unwrap())
        .collect()
}

pub fn clear_runs(helper: &Path) {
    let _ = std::fs::remove_file(sidecar(helper, ".args.json"));
}

/// Writes `data` at `root/name` and returns its catalog record.
pub fn artifact(root: &Path, name: &str, data: &[u8]) -> Value {
    let path = root.join(name);
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(&path, data).unwrap();
    json!({"path": name, "bytes": data.len(), "sha256": sha256_hex(data)})
}

/// The catalog record of an existing file below `root`.
pub fn record(root: &Path, name: &str) -> Value {
    let data = std::fs::read(root.join(name)).unwrap();
    json!({"path": name, "bytes": data.len(), "sha256": sha256_hex(&data)})
}

/// `test_shader_preparation.synthetic_pack`: a header-only valid pack.
pub fn synthetic_pack(schema: u32) -> Vec<u8> {
    let mut data = Vec::new();
    data.extend_from_slice(b"O3PSAOT\0");
    for value in [1u32, schema, 1, 0] {
        data.extend_from_slice(&value.to_le_bytes());
    }
    data.extend(std::iter::repeat_n(0u8, 56 + 20));
    data
}

fn binary_hash(data: &[u8]) -> u64 {
    let mut hash = forge_native::o3ps::BinaryHash::new();
    hash.update(data);
    hash.finish()
}

/// `merge_shader_packs.encode(schema, modules)`.
/// `((stage, primary, secondary, size), spirv)`.
pub type PackModule = ((u32, u64, u64, u64), Vec<u8>);

pub fn encode_pack(schema: u32, modules: &[PackModule]) -> Vec<u8> {
    let mut modules = modules.to_vec();
    modules.sort_by_key(|(key, _)| *key);
    let mut offset = 24 + modules.len() as u64 * 56;
    let mut table = Vec::new();
    let mut payloads = Vec::new();
    for ((stage, primary, secondary, size), payload) in &modules {
        table.extend_from_slice(&stage.to_le_bytes());
        table.extend_from_slice(&0u32.to_le_bytes());
        for value in [*primary, *secondary, *size] {
            table.extend_from_slice(&value.to_le_bytes());
        }
        table.extend_from_slice(&((payload.len() / 4) as u32).to_le_bytes());
        table.extend_from_slice(&0u32.to_le_bytes());
        table.extend_from_slice(&offset.to_le_bytes());
        table.extend_from_slice(&binary_hash(payload).to_le_bytes());
        payloads.extend_from_slice(payload);
        offset += payload.len() as u64;
    }
    let mut data = Vec::new();
    data.extend_from_slice(b"O3PSAOT\0");
    for value in [1u32, schema, modules.len() as u32, 0] {
        data.extend_from_slice(&value.to_le_bytes());
    }
    data.extend(table);
    data.extend(payloads);
    data
}

/// The Python interpreter for oracle tests, when configured.
pub fn python() -> Option<String> {
    match std::env::var("TRIAEVUM_FORGE_PYTHON") {
        Ok(python) if !python.is_empty() => Some(python),
        _ => {
            eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set");
            None
        }
    }
}

pub fn release_tools() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../triaevum_release").canonicalize().unwrap()
}

/// Runs one case of `tests/oracle/native_cases.py` and returns its
/// `{"ok": ...}` or `{"error": ..., "type": ...}` outcome.
pub fn oracle(python: &str, case: &str, arguments: &Value) -> Value {
    let directory = tempfile::tempdir().unwrap();
    let input = directory.path().join("arguments.json");
    let output = directory.path().join("outcome.json");
    std::fs::write(&input, serde_json::to_vec(arguments).unwrap()).unwrap();
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/native_cases.py");
    let result = Command::new(python)
        .arg("-I")
        .arg(script)
        .arg(release_tools())
        .arg(case)
        .arg(&input)
        .arg(&output)
        .output()
        .unwrap();
    assert!(result.status.success(), "{case}: {}", String::from_utf8_lossy(&result.stderr));
    serde_json::from_slice(&std::fs::read(output).unwrap()).unwrap()
}

/// `{"ok": value}` or `{"error": message}` of a Rust result, for comparison
/// with [`oracle`] outcomes.
pub fn outcome<T>(result: forge_base::Result<T>, ok: impl FnOnce(T) -> Value) -> Value {
    match result {
        Ok(value) => json!({"ok": ok(value)}),
        Err(error) => json!({"error": error.message}),
    }
}

/// Every file below `root` as relative POSIX path -> bytes.
pub fn tree(root: &Path) -> Vec<(String, Vec<u8>)> {
    fn walk(root: &Path, directory: &Path, files: &mut Vec<(String, Vec<u8>)>) {
        let Ok(entries) = std::fs::read_dir(directory) else { return };
        for entry in entries {
            let path = entry.unwrap().path();
            if path.is_dir() {
                walk(root, &path, files);
            } else {
                let relative = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
                files.push((relative, std::fs::read(&path).unwrap()));
            }
        }
    }
    let mut files = Vec::new();
    walk(root, root, &mut files);
    files.sort();
    files
}

/// Asserts two trees are byte-identical, reporting the first difference.
pub fn assert_same_tree(label: &str, rust: &Path, python: &Path) {
    let (rust_files, python_files) = (tree(rust), tree(python));
    let names = |files: &[(String, Vec<u8>)]| files.iter().map(|(name, _)| name.clone()).collect::<Vec<_>>();
    assert_eq!(names(&rust_files), names(&python_files), "{label}: file sets differ");
    for ((name, rust_bytes), (_, python_bytes)) in rust_files.iter().zip(&python_files) {
        assert!(
            rust_bytes == python_bytes,
            "{label}: {name} differs\n  rust:   {}\n  python: {}",
            String::from_utf8_lossy(rust_bytes),
            String::from_utf8_lossy(python_bytes)
        );
    }
}
