// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Byte comparison with the Python Forge (process-manifest.json,
//! content.tap, forge-state.json, .hash-cache.json, the private .tam and
//! active-title.json) on synthetic inputs. Each case runs Python first in a
//! temporary root, snapshots its outputs, removes them and replays the Rust
//! port at the same paths, so path-bearing bytes are compared raw.
//! Runs when TRIAEVUM_FORGE_PYTHON names a Python 3 interpreter.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::Command;

use forge_base::hashing::sha256_hex;
use forge_base::legacy_json::pretty_file_bytes;
use forge_base::{CancelToken, ForgeError};
use forge_prepare::paths::{path_string, resolve};
use forge_prepare::{
    activate_prepared_title, build_manifest, load_prepared_content, package_private_module, prepare_content,
    verify_sources, write_tam_file, HashCache, SourcePaths, TamRequest, VerifiedInput,
};
use serde_json::{json, Map, Value};

fn python() -> Option<String> {
    let python = std::env::var("TRIAEVUM_FORGE_PYTHON").ok();
    if python.is_none() {
        eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set");
    }
    python
}

fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).ancestors().nth(4).unwrap().to_path_buf()
}

/// Runs one operation of tests/oracle/prepare_oracle.py: Ok(result) or
/// Err(message).
fn oracle(python: &str, scratch: &Path, request: Value) -> Result<Value, String> {
    let request_path = scratch.join("request.json");
    let response_path = scratch.join("response.json");
    std::fs::write(&request_path, serde_json::to_vec(&request).unwrap()).unwrap();
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/prepare_oracle.py");
    let status = Command::new(python)
        .arg("-I")
        .arg(script)
        .arg(repo_root())
        .arg(&request_path)
        .arg(&response_path)
        .status()
        .unwrap();
    assert!(status.success(), "oracle failed for {request}");
    let mut response: Map<String, Value> = serde_json::from_slice(&std::fs::read(&response_path).unwrap()).unwrap();
    match response.remove("ok") {
        Some(result) => Ok(result),
        None => Err(response["error"].as_str().unwrap().to_string()),
    }
}

fn text(path: &Path) -> String {
    path_string(path).unwrap()
}

/// Every file under `directory` by relative path.
fn snapshot(directory: &Path) -> BTreeMap<String, Vec<u8>> {
    let mut files = BTreeMap::new();
    if directory.exists() {
        collect(directory, directory, &mut files);
    }
    files
}

fn collect(root: &Path, directory: &Path, files: &mut BTreeMap<String, Vec<u8>>) {
    for entry in std::fs::read_dir(directory).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            collect(root, &path, files);
        } else {
            let relative = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
            files.insert(relative, std::fs::read(&path).unwrap());
        }
    }
}

fn assert_same_files(python: &BTreeMap<String, Vec<u8>>, rust: &BTreeMap<String, Vec<u8>>, case: &str) {
    assert_eq!(python.keys().collect::<Vec<_>>(), rust.keys().collect::<Vec<_>>(), "{case}: file sets differ");
    for (name, expected) in python {
        let actual = &rust[name];
        if expected != actual {
            let offset =
                expected.iter().zip(actual).position(|(a, b)| a != b).unwrap_or(expected.len().min(actual.len()));
            let window = |bytes: &[u8]| {
                String::from_utf8_lossy(&bytes[offset.saturating_sub(40)..(offset + 80).min(bytes.len())]).into_owned()
            };
            panic!(
                "{case}: {name} differs at byte {offset}\npython: {:?}\nrust:   {:?}",
                window(expected),
                window(actual)
            );
        }
    }
}

fn remove(path: &Path) {
    if path.is_dir() {
        std::fs::remove_dir_all(path).unwrap();
    } else if path.exists() {
        std::fs::remove_file(path).unwrap();
    }
}

// Synthetic CTR inputs (process_manifest.rs fixtures, repeated for the
// integration test crate).

fn exheader(memory_mode: u8, priority: u8, name: &[u8]) -> Vec<u8> {
    let mut bytes = vec![0u8; 0x400];
    bytes[..name.len()].copy_from_slice(name);
    let words: [(usize, u32); 11] = [
        (0x10, 0x0010_0000),
        (0x14, 2),
        (0x18, 0x0000_1800),
        (0x1C, 0x0000_4000),
        (0x20, 0x0010_2000),
        (0x24, 1),
        (0x28, 0x0000_0400),
        (0x30, 0x0010_3000),
        (0x34, 1),
        (0x38, 0x0000_0200),
        (0x3C, 0x0000_1234),
    ];
    for (offset, value) in words {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    bytes[0x20D] = memory_mode;
    bytes[0x20F] = priority;
    bytes
}

fn code() -> Vec<u8> {
    (0..0x4000u32).map(|index| (index * 13 % 251) as u8).collect()
}

fn raw_romfs() -> Vec<u8> {
    let mut bytes = b"IVFC".to_vec();
    bytes.resize(0x1000, 0);
    bytes.extend_from_slice(&0x28u32.to_le_bytes());
    bytes.extend_from_slice(&[0x5A; 0x7C]);
    bytes
}

struct Inputs {
    code: PathBuf,
    exheader: PathBuf,
    romfs: PathBuf,
    recipe: Value,
}

fn write_inputs(directory: &Path, exheader_bytes: &[u8], romfs_bytes: &[u8]) -> Inputs {
    std::fs::create_dir_all(directory).unwrap();
    let code_bytes = code();
    let mut contracts = Map::new();
    let mut paths = Vec::new();
    for (kind, name, bytes) in [
        ("code", "code.bin", &code_bytes[..]),
        ("exheader", "exheader.bin", exheader_bytes),
        ("romfs", "romfs.bin", romfs_bytes),
    ] {
        let path = directory.join(name);
        std::fs::write(&path, bytes).unwrap();
        contracts.insert(kind.to_string(), json!({"bytes": bytes.len(), "sha256": sha256_hex(bytes)}));
        paths.push(path);
    }
    let [code, exheader, romfs]: [PathBuf; 3] = paths.try_into().unwrap();
    let recipe = json!({"id": "fixture-eur", "inputs": contracts, "process": {"entrypoint": 0x0010_0000}});
    Inputs { code, exheader, romfs, recipe }
}

fn synthetic_manifest(exheader: &Path, code: &Path, romfs: &Path, _: &CancelToken) -> forge_base::Result<Value> {
    let digest = |path: &Path| sha256_hex(&std::fs::read(path).unwrap());
    Ok(json!({
        "format": "oot3d_native_process_manifest_v1",
        "source": {
            "exheader_path": text(&resolve(exheader)?),
            "exheader_sha256": digest(exheader),
            "code_bin_path": text(&resolve(code)?),
            "code_bin_sha256": digest(code),
            "romfs_image_path": text(&resolve(romfs)?),
        },
        "process": {
            "entrypoint": 0x0010_0000,
            "linear_heap": {"base_address": 0x1400_0000, "size": 0x0400_0000},
            "system_regions": [{"name": "ctr_vram", "address": 0x1F00_0000, "mapped_size": 0x0060_0000}],
        },
    }))
}

struct PrepareCase<'a> {
    inputs: &'a Inputs,
    output_root: &'a Path,
    rehash: bool,
    synthetic: bool,
    ui_mod: Option<&'a Path>,
}

impl PrepareCase<'_> {
    fn python_request(&self) -> Value {
        json!({
            "op": "prepare",
            "recipe": self.inputs.recipe,
            "code": text(&self.inputs.code),
            "exheader": text(&self.inputs.exheader),
            "romfs": text(&self.inputs.romfs),
            "output_root": text(self.output_root),
            "rehash": self.rehash,
            "builder": if self.synthetic { "synthetic" } else { "real" },
            "mod": self.ui_mod.map(|path| json!({"kind": "ui_texture_archive", "path": text(path)})),
        })
    }

    /// The Rust equivalent of `forge.py prepare` (and the oracle's mod
    /// handling): verify with the hash cache, then prepare_content.
    fn rust(&self) -> Result<Value, String> {
        let cancel = CancelToken::new();
        let run = || -> Result<Value, ForgeError> {
            let output_root = resolve(self.output_root)?;
            let mut cache = HashCache::new(output_root.join(".hash-cache.json"), !self.rehash);
            let paths =
                SourcePaths { code: &self.inputs.code, exheader: &self.inputs.exheader, romfs: &self.inputs.romfs };
            let recipe = self.inputs.recipe.as_object().unwrap();
            let (verified, _) = verify_sources(recipe, &paths, &mut cache, None, None, &cancel)?;
            let verified_mod = match self.ui_mod {
                Some(path) => {
                    let path = resolve(path)?;
                    let bytes = std::fs::metadata(&path).unwrap().len();
                    let sha256 = cache.digest(&path, &cancel)?;
                    Some(VerifiedInput { kind: "ui_texture_archive".into(), path, bytes, sha256 })
                }
                None => None,
            };
            let builder: Option<forge_prepare::ProcessManifestBuilder<'_>> =
                if self.synthetic { Some(&synthetic_manifest) } else { None };
            prepare_content(recipe, &verified, &output_root, verified_mod.as_ref(), builder, &cancel)?.as_json()
        };
        run().map_err(|error| error.message)
    }
}

/// Python then Rust into the same output root; returns both results.
fn compare_prepare(python: &str, scratch: &Path, case: &PrepareCase<'_>, label: &str) -> Value {
    let expected = oracle(python, scratch, case.python_request());
    let python_files = snapshot(case.output_root);
    remove(case.output_root);
    let actual = case.rust();
    assert_eq!(actual, expected, "{label}: results differ");
    assert_same_files(&python_files, &snapshot(case.output_root), label);
    actual.unwrap_or(Value::Null)
}

#[test]
fn process_manifest_matches_python() {
    let Some(python) = python() else { return };
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let mut count = 0;
    for memory_mode in [0u8, 2, 3, 4, 5, 6, 7, 0x30] {
        for (romfs_name, romfs) in [("raw", raw_romfs()), ("level3", raw_romfs()[0x1000..].to_vec())] {
            let inputs = write_inputs(
                &root.join(format!("m{memory_mode}-{romfs_name}")),
                &exheader(memory_mode, 0x31, b"OOT3D\0x"),
                &romfs,
            );
            let output = root.join("python.json");
            oracle(
                &python,
                &root,
                json!({
                    "op": "manifest", "exheader": text(&inputs.exheader), "code": text(&inputs.code),
                    "romfs": text(&inputs.romfs), "output": text(&output),
                }),
            )
            .unwrap();
            let manifest = build_manifest(&inputs.exheader, &inputs.code, &inputs.romfs, &CancelToken::new()).unwrap();
            assert_eq!(
                pretty_file_bytes(&manifest),
                std::fs::read(&output).unwrap(),
                "memory mode {memory_mode} {romfs_name}"
            );
            count += 1;
        }
    }
    assert_eq!(count, 16);

    // Rejections: the ValueError text Python reports.
    type Case = (&'static str, Vec<u8>, Vec<u8>, Vec<u8>);
    let mut cases: Vec<Case> = Vec::new();
    let good = exheader(0, 0x30, b"fixture");
    cases.push(("truncated", good[..0x3FF].to_vec(), code(), raw_romfs()));
    cases.push(("empty romfs", good.clone(), code(), Vec::new()));
    cases.push(("short ivfc", good.clone(), code(), raw_romfs()[..0x1000].to_vec()));
    let mut no_level3 = raw_romfs();
    no_level3[0x1000] = 0x29;
    cases.push(("no level3", good.clone(), code(), no_level3));
    cases.push(("unknown romfs", good.clone(), code(), b"ABCD1234".to_vec()));
    cases.push(("tiny romfs", good.clone(), code(), b"(".to_vec()));
    cases.push(("code size", good.clone(), code()[..0x3000].to_vec(), raw_romfs()));
    cases.push(("unused mode", exheader(1, 0, b"x"), code(), raw_romfs()));
    cases.push(("bad mode", exheader(8, 0, b"x"), code(), raw_romfs()));
    cases.push(("ascii", exheader(0, 0, b"ab\xe9"), code(), raw_romfs()));
    let mut misaligned = good.clone();
    misaligned[0x31] = 0x31;
    cases.push(("misaligned", misaligned, code(), raw_romfs()));
    let mut no_pages = good.clone();
    no_pages[0x24] = 0;
    cases.push(("no pages", no_pages, code()[..0x3000].to_vec(), raw_romfs()));
    let mut oversize = good.clone();
    oversize[0x39] = 0x20;
    cases.push(("oversize", oversize, code(), raw_romfs()));
    let mut overlap = good.clone();
    overlap[0x21] = 0x10;
    overlap[0x22] = 0x10;
    overlap[0x31] = 0x20;
    cases.push(("ro overlap", overlap, code(), raw_romfs()));
    let mut stack = good.clone();
    stack[0x1C..0x20].copy_from_slice(&0x0FF0_0000u32.to_le_bytes());
    cases.push(("collision", stack, code(), raw_romfs()));
    let mut zero_stack = good.clone();
    zero_stack[0x1C..0x20].copy_from_slice(&0u32.to_le_bytes());
    cases.push(("zero stack", zero_stack, code(), raw_romfs()));
    for (label, exheader_bytes, code_bytes, romfs_bytes) in cases {
        let case_root = root.join(label.replace(' ', "-"));
        let inputs = write_inputs(&case_root, &exheader_bytes, &romfs_bytes);
        std::fs::write(&inputs.code, &code_bytes).unwrap();
        let expected = oracle(
            &python,
            &root,
            json!({
                "op": "manifest", "exheader": text(&inputs.exheader), "code": text(&inputs.code),
                "romfs": text(&inputs.romfs), "output": text(&case_root.join("out.json")),
            }),
        )
        .unwrap_err();
        let actual =
            build_manifest(&inputs.exheader, &inputs.code, &inputs.romfs, &CancelToken::new()).unwrap_err().message;
        assert_eq!(actual, expected, "{label}");
    }
    let inputs = write_inputs(&root.join("missing"), &good, &raw_romfs());
    let missing = root.join("missing").join("nope.bin");
    let expected = oracle(
        &python,
        &root,
        json!({
            "op": "manifest", "exheader": text(&inputs.exheader), "code": text(&inputs.code),
            "romfs": text(&missing), "output": text(&root.join("missing-out.json")),
        }),
    )
    .unwrap_err();
    assert_eq!(
        build_manifest(&inputs.exheader, &inputs.code, &missing, &CancelToken::new()).unwrap_err().message,
        expected
    );
}

#[test]
fn prepare_matches_python_for_internal_and_external_inputs() {
    let Some(python) = python() else { return };
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let data = root.join("TriAevum").join("data");
    let output_root = data.join("titles");

    // Retained sources inside the installation: relative references.
    let internal =
        write_inputs(&data.join("sources").join("a".repeat(64)), &exheader(0, 0x30, b"fixture"), &raw_romfs());
    let case =
        PrepareCase { inputs: &internal, output_root: &output_root, rehash: false, synthetic: false, ui_mod: None };
    let result = compare_prepare(&python, &root, &case, "internal real manifest");
    assert_eq!(result["status"], "prepared");
    // Each implementation reuses the other's output.
    assert_eq!(oracle(&python, &root, case.python_request()).unwrap()["status"], "reused");
    assert_eq!(case.rust().unwrap()["status"], "reused");
    let title = PathBuf::from(result["directory"].as_str().unwrap());
    let loaded = oracle(
        &python,
        &root,
        json!({"op": "load", "prepared": text(&title), "required": ["code", "exheader", "romfs"]}),
    )
    .unwrap();
    let prepared = load_prepared_content(&title, &["code", "exheader", "romfs"], &CancelToken::new()).unwrap();
    assert_eq!(loaded["directory"], text(&prepared.directory));
    for input in &prepared.inputs {
        assert_eq!(loaded["inputs"][&input.kind], input.as_json().unwrap());
    }
    remove(&output_root);

    // Inputs outside the installation, a non-ASCII path and an optional
    // archive: external absolute references with \u escapes.
    let external =
        write_inputs(&root.join("user ROM \u{e8}\u{4e2d}"), &exheader(6, 0x18, b"zelda"), &raw_romfs()[0x1000..]);
    let archive = data.join("downloads").join("pack.zip");
    std::fs::create_dir_all(archive.parent().unwrap()).unwrap();
    std::fs::write(&archive, b"synthetic archive").unwrap();
    let case = PrepareCase {
        inputs: &external,
        output_root: &output_root,
        rehash: false,
        synthetic: false,
        ui_mod: Some(&archive),
    };
    compare_prepare(&python, &root, &case, "external real manifest with mod");
    remove(&output_root);

    // The synthetic manifest of test_forge, and --rehash (no cache file).
    let case =
        PrepareCase { inputs: &internal, output_root: &output_root, rehash: true, synthetic: true, ui_mod: None };
    compare_prepare(&python, &root, &case, "synthetic manifest with rehash");
    assert!(!output_root.join(".hash-cache.json").exists());
    remove(&output_root);

    // An existing cache keeps unrelated and unknown entries.
    std::fs::create_dir_all(&output_root).unwrap();
    let cache = json!({"format": "triaevum_forge_hash_cache_v1", "entries": {
        "Z:\\elsewhere\\file.bin": {"bytes": 3, "mtime_ns": 1, "sha256": "ab", "note": "\u{e9}"},
        "dropped": 7,
    }});
    let cache_bytes = pretty_file_bytes(&cache);
    std::fs::write(output_root.join(".hash-cache.json"), &cache_bytes).unwrap();
    let case =
        PrepareCase { inputs: &internal, output_root: &output_root, rehash: false, synthetic: false, ui_mod: None };
    let expected = oracle(&python, &root, case.python_request());
    let python_files = snapshot(&output_root);
    remove(&output_root);
    std::fs::create_dir_all(&output_root).unwrap();
    std::fs::write(output_root.join(".hash-cache.json"), &cache_bytes).unwrap();
    assert_eq!(case.rust(), expected);
    assert_same_files(&python_files, &snapshot(&output_root), "existing hash cache");
}

#[test]
fn prepare_rejections_match_python() {
    let Some(python) = python() else { return };
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let output_root = root.join("data").join("titles");
    let mut inputs = write_inputs(&root.join("data").join("sources"), &exheader(0, 0x30, b"fixture"), &raw_romfs());
    let compare = |inputs: &Inputs, label: &str| {
        let case = PrepareCase { inputs, output_root: &output_root, rehash: false, synthetic: false, ui_mod: None };
        let expected = oracle(&python, &root, case.python_request());
        assert!(expected.is_err(), "{label}: Python accepted");
        remove(&output_root);
        assert_eq!(case.rust(), expected, "{label}");
        remove(&output_root);
    };
    let original = inputs.recipe.clone();
    inputs.recipe["inputs"]["code"]["bytes"] = json!(1);
    compare(&inputs, "size mismatch");
    inputs.recipe = original.clone();
    inputs.recipe["inputs"]["romfs"]["sha256"] = json!("F".repeat(64));
    compare(&inputs, "digest mismatch");
    inputs.recipe = original.clone();
    inputs.recipe["process"]["entrypoint"] = json!(0x0020_0000);
    compare(&inputs, "entrypoint mismatch");
    inputs.recipe = original.clone();
    inputs.recipe["inputs"].as_object_mut().unwrap().remove("exheader");
    compare(&inputs, "missing contract");
    inputs.recipe = original;
    std::fs::write(&inputs.exheader, &exheader(0, 0x30, b"fixture")[..0x3FF]).unwrap();
    inputs.recipe["inputs"]["exheader"] =
        json!({"bytes": 0x3FF, "sha256": sha256_hex(&std::fs::read(&inputs.exheader).unwrap())});
    compare(&inputs, "manifest derivation");

    let mut long = String::from("/x/");
    long.push_str(&"d".repeat(150));
    let recipes = ["oot3d-eur-project-baseline-16a6b0aa"];
    let expected = oracle(
        &python,
        &root,
        json!({"op": "title_path_length", "output_root": format!("{long}/data/titles"), "recipes": recipes}),
    )
    .unwrap_err();
    let actual = forge_prepare::check_title_path_length(Path::new(&format!("{long}/data/titles")), &recipes, true)
        .unwrap_err()
        .message;
    assert_eq!(actual, expected);
}

#[test]
fn legacy_folders_are_reused_by_both() {
    let Some(python) = python() else { return };
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let output_root = root.join("data").join("titles");
    let inputs = write_inputs(&root.join("data").join("sources"), &exheader(0, 0x30, b"fixture"), &raw_romfs());
    let case =
        PrepareCase { inputs: &inputs, output_root: &output_root, rehash: false, synthetic: false, ui_mod: None };
    let prepared = oracle(&python, &root, case.python_request()).unwrap();
    let title = PathBuf::from(prepared["directory"].as_str().unwrap());

    // A pre-0.8.0-beta.1.2 full-key folder is still selected.
    let key = std::fs::read_to_string(title.join("forge-state.json")).unwrap();
    let key = serde_json::from_str::<Value>(&key).unwrap()["content_key"].as_str().unwrap().to_string();
    let full = title.parent().unwrap().join(&key);
    std::fs::rename(&title, &full).unwrap();
    let expected = oracle(&python, &root, case.python_request());
    assert_eq!(case.rust(), expected);
    assert_eq!(expected.unwrap()["directory"], text(&full));

    // Absolute-path (pre manifest_relative_v1) folders are compared in their
    // own spelling and never rewritten.
    oracle(&python, &root, json!({"op": "make_legacy", "title": text(&full)})).unwrap();
    let before = snapshot(&full);
    let expected = oracle(&python, &root, case.python_request());
    assert_eq!(expected.as_ref().unwrap()["status"], "reused");
    assert_eq!(case.rust(), expected);
    assert_eq!(snapshot(&full), before, "a legacy folder is never rewritten");

    // A legacy folder whose index disagrees is refused the same way.
    let index_path = full.join("content.tap");
    let mut index: Value = serde_json::from_slice(&std::fs::read(&index_path).unwrap()).unwrap();
    index["inputs"]["code"]["bytes"] = json!(code().len() as f64);
    std::fs::write(&index_path, pretty_file_bytes(&index)).unwrap();
    let expected = oracle(&python, &root, case.python_request());
    assert_eq!(expected.as_ref().unwrap()["status"], "reused", "16384.0 == 16384 in Python");
    assert_eq!(case.rust(), expected);
    index["inputs"]["code"]["path"] = json!("elsewhere");
    std::fs::write(&index_path, pretty_file_bytes(&index)).unwrap();
    let expected = oracle(&python, &root, case.python_request());
    assert!(expected.as_ref().unwrap_err().starts_with("existing Forge output does not match inputs"));
    assert_eq!(case.rust(), expected);
}

#[test]
fn packaging_and_activation_match_python() {
    let Some(python) = python() else { return };
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let data = root.join("data");
    let output_root = data.join("titles");
    let inputs = write_inputs(&data.join("sources").join("b".repeat(64)), &exheader(0, 0x30, b"fixture"), &raw_romfs());
    let native = root.join("package").join("oot3d_game_module.dll");
    let title_aot = root.join("package").join("triaevum_title_aot.dll");
    std::fs::create_dir_all(native.parent().unwrap()).unwrap();
    std::fs::write(&native, b"MZ generic runtime module, 33 bytes").unwrap();
    // Odd lengths exercise the 16-byte section padding.
    std::fs::write(&title_aot, (0..70_001u32).map(|i| (i % 253) as u8).collect::<Vec<u8>>()).unwrap();
    let translator = "C".repeat(64);
    let cancel = CancelToken::new();

    for (label, with_aot, active) in [
        ("title AOT, active inside data", true, data.join("active-title.json")),
        ("native only, active outside data", false, root.join("elsewhere").join("active-title.json")),
    ] {
        let case =
            PrepareCase { inputs: &inputs, output_root: &output_root, rehash: false, synthetic: false, ui_mod: None };
        let prepared = oracle(&python, &root, case.python_request()).unwrap();
        let title = PathBuf::from(prepared["directory"].as_str().unwrap());
        let request = json!({
            "op": "package", "prepared": text(&title), "native": text(&native),
            "title_aot": with_aot.then(|| text(&title_aot)), "target": "x86_64-pc-windows-msvc",
            "translator": translator, "active": text(&active),
        });
        let expected = oracle(&python, &root, request.clone()).unwrap();
        let python_titles = snapshot(&output_root);
        let python_active = std::fs::read(&active).unwrap();
        remove(&output_root);
        remove(&active);

        case.rust().unwrap();
        let aot = with_aot.then_some(title_aot.as_path());
        let packaged =
            package_private_module(&title, &native, aot, "x86_64-pc-windows-msvc", &translator, Some(&active), &cancel)
                .unwrap();
        assert_eq!(packaged.as_json().unwrap(), expected, "{label}: package result");
        assert_same_files(&python_titles, &snapshot(&output_root), label);
        assert_eq!(std::fs::read(&active).unwrap(), python_active, "{label}: active-title.json");

        // Reuse across implementations, and a separate activation.
        let reused = oracle(&python, &root, request).unwrap();
        assert_eq!(reused["status"], "reused");
        let other = root.join("other-active.json");
        let expected =
            oracle(&python, &root, json!({"op": "activate", "prepared": text(&title), "active": text(&other)}))
                .unwrap();
        let python_other = std::fs::read(&other).unwrap();
        remove(&other);
        assert_eq!(activate_prepared_title(&title, &other, &cancel).unwrap().as_json().unwrap(), expected);
        assert_eq!(std::fs::read(&other).unwrap(), python_other);
        remove(&output_root);
        remove(&active);
        remove(&other);
    }
}

#[test]
fn tam_builder_matches_python() {
    let Some(python) = python() else { return };
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let native = root.join("module.dll");
    let title_aot = root.join("title.dll");
    std::fs::write(&native, b"MZ native").unwrap();
    std::fs::write(&title_aot, vec![0xC3u8; 4097]).unwrap();
    let source = "A".repeat(64);
    let translator = "0123456789abcdef".repeat(4);
    let regions_sets: [&[(i128, i128, i128)]; 3] = [
        &[],
        &[(0x2000_0000, 0x1400_0000, 0x0800_0000), (0x1800_0000, 0x1F00_0000, 0x0060_0000)],
        &[(0, 0, 1), (0xFFFF_FFFF, 0xFFFF_FFFF, 1)],
    ];
    let mut index = 0;
    for regions in regions_sets {
        for services in [&[][..], &[0x4143_4950, 0x5359_5346, 0x1][..]] {
            for with_aot in [false, true] {
                index += 1;
                let output = root.join(format!("out{index}")).join("game.tam");
                let mut argv = vec![
                    "--native-image".to_string(),
                    text(&native),
                    "--output".into(),
                    text(&output),
                    "--recipe".into(),
                    "fixture \u{e9}".into(),
                    "--target-triple".into(),
                    "aarch64-unknown-linux-gnu".into(),
                    "--source-identity".into(),
                    source.clone(),
                    "--translator-identity".into(),
                    translator.clone(),
                ];
                if with_aot {
                    argv.extend(["--title-aot-image".into(), text(&title_aot)]);
                }
                for service in services {
                    argv.extend(["--required-service".into(), format!("{service:#x}")]);
                }
                for (physical, guest, bytes) in regions {
                    argv.extend(["--physical-memory-region".into(), format!("{physical:#x}:{guest:#x}:{bytes}")]);
                }
                let expected = oracle(&python, &root, json!({"op": "tam", "argv": argv})).unwrap();
                let python_bytes = std::fs::read(&output).ok();
                remove(&output);
                let request = TamRequest {
                    native_image: &native,
                    title_aot_image: with_aot.then_some(title_aot.as_path()),
                    recipe: "fixture \u{e9}",
                    target_triple: "aarch64-unknown-linux-gnu",
                    source_identity: &source,
                    translator_identity: &translator,
                    required_services: services,
                    physical_memory_regions: regions,
                    runtime_abi: 1,
                };
                let actual = write_tam_file(&output, &request, &CancelToken::new());
                let label = format!("case {index}");
                match (&expected["stdout"]["status"], actual) {
                    (status, Ok(file)) if status == "built" => {
                        assert_eq!(Some(std::fs::read(&output).unwrap()), python_bytes, "{label}");
                        assert_eq!(expected["stdout"]["sha256"], file.sha256);
                        assert_eq!(expected["stdout"]["metadata"], file.metadata);
                        assert_eq!(expected["stdout"]["output"], text(&file.output));
                    }
                    (_, Err(error)) => assert_eq!(expected["stdout"]["error"], error.message, "{label}"),
                    (_, Ok(_)) => panic!("{label}: Rust accepted what Python refused: {expected}"),
                }
            }
        }
    }

    // Refusals: existing output, stale temporary, bad identities and images.
    let output = root.join("out1").join("game.tam");
    std::fs::create_dir_all(output.parent().unwrap()).unwrap();
    std::fs::write(&output, b"x").unwrap();
    let stale = root.join("stale").join("game.tam");
    std::fs::create_dir_all(stale.parent().unwrap()).unwrap();
    std::fs::write(stale.with_file_name(".game.tam.tmp"), b"").unwrap();
    let empty = root.join("empty.dll");
    std::fs::write(&empty, b"").unwrap();
    let fresh = root.join("fresh").join("game.tam");
    let base = |output: &Path| -> Vec<String> {
        vec![
            "--native-image".into(),
            text(&native),
            "--output".into(),
            text(output),
            "--recipe".into(),
            "fixture".into(),
            "--target-triple".into(),
            "x86_64-pc-windows-msvc".into(),
            "--source-identity".into(),
            source.clone(),
            "--translator-identity".into(),
            translator.clone(),
        ]
    };
    let mut refusals: Vec<(Vec<String>, TamRequest<'_>)> = Vec::new();
    let request = |output_native: &'static str| TamRequest {
        native_image: if output_native == "empty" { &empty } else { &native },
        title_aot_image: None,
        recipe: "fixture",
        target_triple: "x86_64-pc-windows-msvc",
        source_identity: &source,
        translator_identity: &translator,
        required_services: &[],
        physical_memory_regions: &[],
        runtime_abi: 1,
    };
    refusals.push((base(&output), request("native")));
    refusals.push((base(&stale), request("native")));
    let mut argv = base(&fresh);
    argv[1] = text(&empty);
    refusals.push((argv, request("empty")));
    let mut argv = base(&fresh);
    argv[9] = "abc".into();
    refusals.push((argv, TamRequest { source_identity: "abc", ..request("native") }));
    let mut argv = base(&fresh);
    argv.extend(["--required-service".into(), "0x41434950".into()]);
    refusals.push((argv, TamRequest { required_services: &[0x4143_4950], ..request("native") }));
    let mut argv = base(&fresh);
    argv.extend([
        "--physical-memory-region".into(),
        "0:0:0x2000".into(),
        "--physical-memory-region".into(),
        "0x1000:0x8000:1".into(),
    ]);
    refusals.push((
        argv,
        TamRequest { physical_memory_regions: &[(0, 0, 0x2000), (0x1000, 0x8000, 1)], ..request("native") },
    ));
    let target = |output: &Path| {
        let mut argv = base(output);
        argv[7] = "aarch64-linux-android".into();
        argv
    };
    refusals.push((target(&fresh), TamRequest { target_triple: "aarch64-linux-android", ..request("native") }));
    for (index, (argv, request)) in refusals.into_iter().enumerate() {
        let output = PathBuf::from(&argv[3]);
        let expected = oracle(&python, &root, json!({"op": "tam", "argv": argv})).unwrap();
        assert_eq!(expected["status"], 1, "refusal {index}");
        let actual = write_tam_file(&output, &request, &CancelToken::new()).unwrap_err().message;
        assert_eq!(expected["stdout"]["error"], actual, "refusal {index}");
    }
}

/// Local-only comparison on real extracted inputs (never part of the repo):
/// TRIAEVUM_FORGE_REAL_INPUTS names a directory with code.bin, exheader.bin
/// and romfs.bin; TRIAEVUM_FORGE_REAL_WORK a scratch directory on the same
/// volume (inputs are hard-linked into it for the installation-relative
/// case). Prints the SHA-256 of the compared artifacts.
#[test]
fn real_inputs_match_python() {
    let Some(python) = python() else { return };
    let (Ok(inputs_directory), Ok(work)) =
        (std::env::var("TRIAEVUM_FORGE_REAL_INPUTS"), std::env::var("TRIAEVUM_FORGE_REAL_WORK"))
    else {
        eprintln!("skipped: TRIAEVUM_FORGE_REAL_INPUTS/TRIAEVUM_FORGE_REAL_WORK are not set");
        return;
    };
    let inputs_directory = resolve(Path::new(&inputs_directory)).unwrap();
    let directory = tempfile::Builder::new().prefix("a3-real").tempdir_in(&work).unwrap();
    let root = resolve(directory.path()).unwrap();
    let cancel = CancelToken::new();
    let original = ["code.bin", "exheader.bin", "romfs.bin"].map(|name| inputs_directory.join(name));
    let mut contracts = Map::new();
    for (kind, path) in ["code", "exheader", "romfs"].iter().zip(&original) {
        let (bytes, digest) = forge_base::hashing::sha256_file(path, &cancel).unwrap();
        contracts.insert(kind.to_string(), json!({"bytes": bytes, "sha256": forge_base::hashing::hex(&digest)}));
    }
    let catalog: Value = serde_json::from_slice(
        &std::fs::read(repo_root().join("tools/triaevum_release/supported_revisions.json")).unwrap(),
    )
    .unwrap();
    let matches = |recipe: &&Value| {
        recipe["inputs"].as_object().is_some_and(|inputs| {
            ["code", "exheader", "romfs"].iter().all(|kind| inputs[*kind]["sha256"] == contracts[*kind]["sha256"])
        })
    };
    let recipe = catalog["recipes"].as_array().unwrap().iter().find(matches).cloned().unwrap_or_else(
        || json!({"id": "local-unlisted-revision", "inputs": contracts, "process": {"entrypoint": 0x0010_0000}}),
    );
    eprintln!("recipe {}", recipe["id"]);

    let data = root.join("data");
    let sources = data.join("sources").join("real");
    std::fs::create_dir_all(&sources).unwrap();
    for path in &original {
        std::fs::hard_link(path, sources.join(path.file_name().unwrap())).unwrap();
    }
    let linked = Inputs {
        code: sources.join("code.bin"),
        exheader: sources.join("exheader.bin"),
        romfs: sources.join("romfs.bin"),
        recipe: recipe.clone(),
    };
    let external =
        Inputs { code: original[0].clone(), exheader: original[1].clone(), romfs: original[2].clone(), recipe };
    let native = root.join("oot3d_game_module.dll");
    std::fs::write(&native, b"MZ synthetic native module for the real-input comparison").unwrap();
    let title_aot = root.join("triaevum_title_aot.dll");
    std::fs::write(&title_aot, (0..1_000_003u32).map(|i| (i * 31 % 255) as u8).collect::<Vec<u8>>()).unwrap();
    let output_root = data.join("titles");
    let active = data.join("active-title.json");
    let translator = "d".repeat(64);

    for (label, inputs) in [("installation-relative", &linked), ("external", &external)] {
        let case = PrepareCase { inputs, output_root: &output_root, rehash: false, synthetic: false, ui_mod: None };
        let prepared = oracle(&python, &root, case.python_request()).unwrap();
        let title = PathBuf::from(prepared["directory"].as_str().unwrap());
        let request = json!({
            "op": "package", "prepared": text(&title), "native": text(&native), "title_aot": text(&title_aot),
            "target": "x86_64-pc-windows-msvc", "translator": translator, "active": text(&active),
        });
        let expected = oracle(&python, &root, request).unwrap();
        let python_titles = snapshot(&output_root);
        let python_active = std::fs::read(&active).unwrap();
        remove(&output_root);
        remove(&active);

        assert_eq!(case.rust().unwrap(), prepared, "{label}: prepare result");
        let packaged = package_private_module(
            &title,
            &native,
            Some(&title_aot),
            "x86_64-pc-windows-msvc",
            &translator,
            Some(&active),
            &cancel,
        )
        .unwrap();
        assert_eq!(packaged.as_json().unwrap(), expected, "{label}: package result");
        let rust_titles = snapshot(&output_root);
        assert_same_files(&python_titles, &rust_titles, label);
        assert_eq!(std::fs::read(&active).unwrap(), python_active, "{label}: active-title.json");
        for (name, bytes) in &rust_titles {
            eprintln!("{label}: {name} {} bytes sha256 {}", bytes.len(), sha256_hex(bytes));
        }
        eprintln!("{label}: active-title.json sha256 {}", sha256_hex(&python_active));
        remove(&output_root);
        remove(&active);
    }
}
