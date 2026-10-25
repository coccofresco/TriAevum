// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Runs the same extraction, staging, publication and BLZ jobs through the
//! Python Forge and this crate, in the same directory spelling one after the
//! other, and compares results, error messages and every output byte. Runs
//! when TRIAEVUM_FORGE_PYTHON names a Python 3 interpreter.

mod common;

use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use common::*;
use forge_base::hashing::hex;
use forge_base::CancelToken;
use forge_rom::*;
use serde_json::{json, Value};

struct Plan {
    root: PathBuf,
    jobs: Vec<Value>,
}

impl Plan {
    fn path(&self, relative: &str) -> String {
        self.root.join(relative).to_string_lossy().into_owned()
    }

    fn fixture(&self, name: &str, data: &[u8]) -> String {
        let path = self.root.join("fixtures").join(name);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(&path, data).unwrap();
        path.to_string_lossy().into_owned()
    }

    fn extract(&mut self, rom: &str, output: &str, require_suffix: bool, publish: Option<&str>) {
        let publish = publish.map(|relative| self.path(relative));
        let output = self.path(output);
        self.jobs.push(json!({"op": "extract", "rom": rom, "output": output, "require_suffix": require_suffix, "publish": publish}));
    }

    fn stage(&mut self, source: &str, output: &str, publish: Option<&str>) {
        let publish = publish.map(|relative| self.path(relative));
        let output = self.path(output);
        self.jobs.push(json!({"op": "stage", "source": source, "output": output, "publish": publish}));
    }

    fn write(&mut self, relative: &str, data: &[u8]) {
        let path = self.path(relative);
        self.jobs.push(json!({"op": "write", "path": path, "hex": hex(data)}));
    }

    fn blz(&mut self, name: &str, data: &[u8]) {
        let input = self.fixture(&format!("blz/{name}.bin"), data);
        let output = self.path(&format!("work/blz/{name}.out"));
        self.jobs.push(json!({"op": "blz", "input": input, "output": output}));
    }
}

fn describe(extracted: &ExtractedTitleInputs) -> Value {
    let mut files = serde_json::Map::new();
    for (kind, item) in extracted.by_kind() {
        files.insert(
            kind.into(),
            json!({"path": item.path.to_string_lossy(), "bytes": item.bytes, "sha256": item.sha256_hex()}),
        );
    }
    json!({
        "container_kind": extracted.container_kind.as_str(),
        "partition_index": extracted.partition_index,
        "program_id": extracted.program_id,
        "files": files,
    })
}

fn failure(step: &str, error: forge_base::ForgeError) -> Value {
    json!({"ok": false, "step": step, "error_type": format!("ForgeError:{}", error.kind.code()), "error": error.message})
}

fn run_rust(job: &Value) -> Value {
    let cancel = CancelToken::new();
    let text = |key: &str| PathBuf::from(job[key].as_str().unwrap());
    match job["op"].as_str().unwrap() {
        "build_rom" => json!({"ok": true}),
        "write" => {
            let path = text("path");
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            let data: Vec<u8> = (0..job["hex"].as_str().unwrap().len() / 2)
                .map(|index| u8::from_str_radix(&job["hex"].as_str().unwrap()[2 * index..2 * index + 2], 16).unwrap())
                .collect();
            fs::write(path, data).unwrap();
            json!({"ok": true})
        }
        "blz" => match decompress_exefs_code(&fs::read(text("input")).unwrap(), &cancel) {
            Ok(output) => {
                fs::write(text("output"), output).unwrap();
                json!({"ok": true})
            }
            Err(error) => failure("blz", error),
        },
        op => {
            let extracted = if op == "extract" {
                extract_decrypted_rom(&text("rom"), &text("output"), job["require_suffix"].as_bool().unwrap(), &cancel)
            } else {
                stage_directory(&text("source"), &text("output"), &cancel)
            };
            let extracted = match extracted {
                Ok(extracted) => extracted,
                Err(error) => return failure(op, error),
            };
            let mut result =
                json!({"ok": true, "extracted": describe(&extracted), "identity": input_identity(&extracted)});
            if let Some(publish) = job["publish"].as_str() {
                match publish_extracted_inputs(&extracted, Path::new(publish), &cancel) {
                    Ok(published) => result["published"] = describe(&published),
                    Err(error) => {
                        let mut failed = failure("publish", error);
                        failed["extracted"] = result["extracted"].take();
                        failed["identity"] = result["identity"].take();
                        return failed;
                    }
                }
            }
            result
        }
    }
}

fn tree(root: &Path) -> BTreeMap<String, Option<Vec<u8>>> {
    let mut entries = BTreeMap::new();
    let mut pending = vec![root.to_path_buf()];
    while let Some(directory) = pending.pop() {
        for entry in fs::read_dir(&directory).unwrap() {
            let path = entry.unwrap().path();
            let relative = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
            if path.is_dir() {
                entries.insert(relative, None);
                pending.push(path);
            } else {
                entries.insert(relative, Some(fs::read(&path).unwrap()));
            }
        }
    }
    entries
}

fn compare(job: &Value, python: &Value, rust: &Value) -> Result<(), String> {
    let context = || format!("job {job}\npython {python}\nrust   {rust}");
    if python["ok"] == rust["ok"] && python["ok"] == true {
        return if python == rust { Ok(()) } else { Err(context()) };
    }
    for key in ["ok", "step", "extracted", "identity"] {
        if python[key] != rust[key] {
            return Err(context());
        }
    }
    if python["error_type"] == "CtrRomError" && python["error"] != rust["error"] {
        return Err(context());
    }
    Ok(())
}

fn plan_jobs(plan: &mut Plan) {
    let release = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../triaevum_release");
    assert!(release.join("test_ctr_rom.py").is_file());
    for (name, declares) in [("built_by_python.cci", true), ("built_by_python_legacy.cci", false)] {
        let path = plan.path(&format!("fixtures/{name}"));
        plan.jobs.push(json!({"op": "build_rom", "path": path, "declares_decrypted": declares}));
    }

    let base = build_rom(true);
    let base_cci = plan.fixture("base.cci", &base);
    let base_3ds = plan.fixture("base.3ds", &base);
    let base_upper = plan.fixture("upper.CCI", &base);
    let base_bin = plan.fixture("base.bin", &base);
    let legacy = plan.fixture("legacy.cci", &build_rom(false));
    plan.extract(&base_cci, "work/x/base_cci", true, None);
    plan.extract(&base_3ds, "work/x/base_3ds", true, None);
    plan.extract(&base_upper, "work/x/upper", true, None);
    plan.extract(&legacy, "work/x/legacy", true, None);
    plan.extract(&base_bin, "work/x/bin_refused", true, None);
    plan.extract(&base_bin, "work/x/bin_allowed", false, None);
    let bare = ncch(CODE, false, true);
    let bare_cxi = plan.fixture("bare.cxi", &bare);
    let bare_3ds = plan.fixture("bare.3ds", &bare);
    plan.extract(&bare_cxi, "work/x/bare_cxi", false, None);
    plan.extract(&bare_3ds, "work/x/bare_3ds", true, None);

    let mut broken = ncch(CODE, false, true);
    broken[0x100] = b'X';
    let second = plan.fixture("second.cci", &ncsd(&[broken, ncch(CODE, false, true)]));
    plan.extract(&second, "work/x/second", true, None);
    let mut first = ncch(CODE, false, true);
    first[0x18D] = 0;
    let mut other = ncch(CODE, false, true);
    put_u32(&mut other, 0x1B0, 0);
    let all_broken = plan.fixture("all_broken.cci", &ncsd(&[first, other]));
    plan.extract(&all_broken, "work/x/all_broken", true, None);
    let mut empty = base.clone();
    empty[0x120..0x128].fill(0);
    let empty = plan.fixture("empty_table.cci", &empty);
    plan.extract(&empty, "work/x/empty_table", true, None);
    let mut magic = base.clone();
    magic[0x100..0x104].copy_from_slice(b"NCSE");
    let magic = plan.fixture("magic.cci", &magic);
    plan.extract(&magic, "work/x/magic", true, None);
    let short = plan.fixture("short.cci", &base[..0x1FF]);
    plan.extract(&short, "work/x/short", true, None);
    let mut nul_name = base.clone();
    nul_name[PARTITION_BASE + EXEFS + 5..PARTITION_BASE + EXEFS + 8].copy_from_slice(b"\0zz");
    let nul_name = plan.fixture("nul_name.cci", &nul_name);
    plan.extract(&nul_name, "work/x/nul_name", true, None);
    for case in partition_mutations() {
        let rom = plan.fixture(&format!("mutation_{}.cci", case.name), &case.image);
        plan.extract(&rom, &format!("work/x/mutation_{}", case.name), true, None);
    }

    let mut rng = Rng(20261025);
    for index in 0..8 {
        let length = 200 + rng.below(9000);
        let plain = compressible(&mut rng, length);
        let prefix = if index % 2 == 0 { rng.below(64) } else { 0 };
        let packed = blz_compress(&plain, prefix, index % 4, if index % 3 == 0 { 300 } else { 0x1002 }).unwrap();
        let rom = plan.fixture(&format!("compressed_{index}.cci"), &ncsd(&[ncch(&packed, true, true)]));
        plan.extract(&rom, &format!("work/x/compressed_{index}"), true, Some("work/compressed_sources"));
    }
    for index in 0..12 {
        let rom = plan.fixture(&format!("random_code_{index}.cci"), &ncsd(&[ncch(&blz_random(&mut rng), true, true)]));
        plan.extract(&rom, &format!("work/x/random_code_{index}"), true, None);
    }

    let missing = plan.path("fixtures/missing.3ds");
    plan.extract(&missing, "work/x/missing", true, None);
    let folder = plan.path("fixtures/folder.3ds");
    fs::create_dir_all(&folder).unwrap();
    plan.extract(&folder, "work/x/folder", true, None);
    plan.write("work/exists/keep", b"keep");
    plan.extract(&base_cci, "work/exists", true, None);
    plan.extract(&base_cci, "work/new_parent/a/b/output", true, None);

    plan.extract(&base_cci, "work/pub/s1", true, Some("work/sources"));
    plan.extract(&base_3ds, "work/pub/s2", true, Some("work/sources"));
    let identity = {
        let probe = tempfile::tempdir().unwrap();
        let rom = probe.path().join("probe.cci");
        fs::write(&rom, &base).unwrap();
        input_identity(&extract_decrypted_rom(&rom, &probe.path().join("out"), true, &CancelToken::new()).unwrap())
    };
    plan.write(&format!("work/sources/{identity}/romfs.bin"), b"IVFC tampered");
    plan.extract(&base_cci, "work/pub/s3", true, Some("work/sources"));
    plan.write(&format!("work/blocked/{identity}"), b"");
    plan.extract(&base_cci, "work/pub/s4", true, Some("work/blocked"));

    let mut romfs = b"IVFC".to_vec();
    romfs.resize(ROMFS_SIZE, b'R');
    let good = [("code.bin", CODE.to_vec()), ("exheader.bin", expected_exheader()), ("romfs.bin", romfs)];
    // (directory, changed file, its replacement; None removes the file)
    let variants: [(&str, &str, Option<&[u8]>); 7] = [
        ("good", "", None),
        ("empty_code", "code.bin", Some(b"")),
        ("bad_exheader", "exheader.bin", Some(b"bad")),
        ("encrypted_romfs", "romfs.bin", Some(b"encrypted")),
        ("short_romfs", "romfs.bin", Some(b"IV")),
        ("missing_code", "code.bin", None),
        ("missing_romfs", "romfs.bin", None),
    ];
    for (name, changed, replacement) in variants {
        for (file, data) in &good {
            let data = if *file == changed { replacement } else { Some(data.as_slice()) };
            if let Some(data) = data {
                plan.fixture(&format!("dirs/{name}/{file}"), data);
            }
        }
        let source = plan.path(&format!("fixtures/dirs/{name}"));
        plan.stage(&source, &format!("work/stage/{name}"), Some("work/stage_sources"));
    }
    let good_source = plan.path("fixtures/dirs/good");
    plan.stage(&good_source, "fixtures/dirs/good", None);
    plan.stage(&good_source, "fixtures/dirs/good/nested", None);
    plan.write("work/stage_exists/keep", b"keep");
    plan.stage(&good_source, "work/stage_exists", None);
    plan.stage(&plan.path("fixtures/dirs/good/code.bin"), "work/stage/file_source", None);
    plan.stage(&plan.path("fixtures/dirs/absent"), "work/stage/absent", None);
    plan.stage(&good_source, "work/stage/into_tampered", Some("work/sources"));

    let edges: [(&str, Vec<u8>); 9] = [
        ("too_small", vec![0; 7]),
        ("too_big", [b"abcd".as_slice(), &[8, 0, 0, 8], &((512u32 << 20) - 11).to_le_bytes()].concat()),
        ("footer_small", [b"abcd".as_slice(), &[8, 0, 0, 7], &[0; 4]].concat()),
        ("footer_large", [b"abcd".as_slice(), &[13, 0, 0, 13], &[0; 4]].concat()),
        ("encoded_small", [b"abcd".as_slice(), &[7, 0, 0, 8], &[0; 4]].concat()),
        ("ref_truncated", [[0x00, 0x80].as_slice(), &[10, 0, 0, 8], &[4, 0, 0, 0]].concat()),
        ("ref_out_of_range", [[0, 0, 0, 0x80].as_slice(), &[11, 0, 0, 8], &[4, 0, 0, 0]].concat()),
        ("underflow", [[0x00, 0xF0, 0x80].as_slice(), &[11, 0, 0, 8], &[0; 4]].concat()),
        (
            "output_full",
            [[0xEE, 0xEE, 0x00, 0xA0, b'c', b'b', b'a', 0x10].as_slice(), &[16, 0, 0, 8], &[0; 4]].concat(),
        ),
    ];
    for (name, data) in &edges {
        plan.blz(name, data);
    }
    for index in 0..40 {
        let length = 16 + rng.below(4000);
        let plain = compressible(&mut rng, length);
        let prefix = rng.below(48).min(length);
        if let Some(packed) = blz_compress(&plain, prefix, rng.below(4), 0x1002) {
            plan.blz(&format!("greedy_{index}"), &packed);
        }
    }
    for index in 0..300 {
        plan.blz(&format!("random_{index}"), &blz_random(&mut rng));
    }
}

#[test]
fn rom_import_matches_python() {
    let Ok(python) = std::env::var("TRIAEVUM_FORGE_PYTHON") else {
        eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set");
        return;
    };
    let directory = tempfile::tempdir().unwrap();
    let root = {
        let text = fs::canonicalize(directory.path()).unwrap().to_string_lossy().into_owned();
        PathBuf::from(text.strip_prefix(r"\\?\").unwrap_or(&text))
    };
    let mut plan = Plan { root: root.clone(), jobs: Vec::new() };
    plan_jobs(&mut plan);
    fs::create_dir_all(root.join("work/blz")).unwrap();

    let jobs_path = root.join("jobs.json");
    let results_path = root.join("results.json");
    fs::write(&jobs_path, serde_json::to_vec(&plan.jobs).unwrap()).unwrap();
    let release = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../triaevum_release");
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/ctr_rom_jobs.py");
    let status = Command::new(python)
        .arg("-I")
        .arg(script)
        .arg(fs::canonicalize(release).unwrap())
        .arg(&jobs_path)
        .arg(&results_path)
        .status()
        .unwrap();
    assert!(status.success());
    let python_results: Vec<Value> = serde_json::from_slice(&fs::read(&results_path).unwrap()).unwrap();
    assert_eq!(python_results.len(), plan.jobs.len());
    fs::rename(root.join("work"), root.join("work_python")).unwrap();

    for name in ["built_by_python.cci", "built_by_python_legacy.cci"] {
        let declares = name == "built_by_python.cci";
        assert_eq!(fs::read(root.join("fixtures").join(name)).unwrap(), build_rom(declares), "{name}");
    }

    fs::create_dir_all(root.join("work/blz")).unwrap();
    let mut mismatches = Vec::new();
    let mut python_only_errors = Vec::new();
    let (mut successes, mut failures) = (0, 0);
    let mut messages: BTreeMap<String, usize> = BTreeMap::new();
    for (job, python) in plan.jobs.iter().zip(&python_results) {
        let outcome = python["error"].as_str().map_or("success", |message| message.split(':').next().unwrap());
        *messages.entry(format!("{} {outcome}", job["op"].as_str().unwrap())).or_default() += 1;
        let rust = run_rust(job);
        if let Err(report) = compare(job, python, &rust) {
            mismatches.push(report);
        }
        match (python["ok"].as_bool(), python["error_type"].as_str()) {
            (Some(true), _) => successes += 1,
            (_, Some("CtrRomError")) => failures += 1,
            _ => python_only_errors.push(format!("{} | rust: {}", python["error"], rust["error"])),
        }
    }
    assert!(mismatches.is_empty(), "{} mismatches:\n{}", mismatches.len(), mismatches.join("\n\n"));
    for message in [
        "blz success",
        "blz compressed ExeFS .code is too small",
        "blz compressed ExeFS .code declares an invalid output size",
        "blz compressed ExeFS .code has an invalid footer",
        "blz compressed ExeFS .code back-reference is truncated",
        "blz compressed ExeFS .code back-reference is out of range",
        "blz compressed ExeFS .code output underflow",
        "extract success",
        "stage success",
    ] {
        assert!(messages.contains_key(message), "no oracle case for {message}");
    }
    eprintln!("Python outcomes by operation: {messages:#?}");

    let python_tree = tree(&root.join("work_python"));
    let rust_tree = tree(&root.join("work"));
    let python_keys: Vec<_> = python_tree.keys().collect();
    let rust_keys: Vec<_> = rust_tree.keys().collect();
    assert_eq!(python_keys, rust_keys, "output trees differ in their entries");
    for (path, bytes) in &python_tree {
        assert!(bytes == &rust_tree[path], "{path} differs");
    }
    eprintln!(
        "compared {} jobs: {successes} successes, {failures} CtrRomError rejections with identical messages, \
         {} other Python errors (both sides failed): {python_only_errors:?}; {} output entries byte-identical",
        plan.jobs.len(),
        python_only_errors.len(),
        python_tree.len()
    );
}
