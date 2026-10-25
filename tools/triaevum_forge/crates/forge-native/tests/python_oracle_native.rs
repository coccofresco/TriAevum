// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Compares forge-native with the Python Forge on synthetic fixtures and the
//! fake helper executable (both sides run the same helper copy). Python runs
//! first; its data directory is moved aside and Rust runs at the same path,
//! so path-bearing receipts compare byte for byte. The only normalized
//! field is the random `.preparing-*` stage name inside failure messages.
//! Runs when TRIAEVUM_FORGE_PYTHON names a Python 3.13 interpreter.

mod common;

use std::path::{Path, PathBuf};

use common::{artifact, assert_same_tree, install_fake, oracle, outcome, python, record, synthetic_pack};
use forge_base::fsutil::atomic_write_json;
use forge_base::hashing::sha256_hex;
use forge_base::CancelToken;
use forge_native::device::{CACHE_FILENAME, FORMAT as DEVICE_FORMAT};
use forge_native::shader::{FORMAT, RENDERER_FORMAT};
use forge_native::{corpus, language, product, DesktopServices};
use serde_json::{json, Map, Value};

fn temp_root() -> (tempfile::TempDir, PathBuf) {
    let temp = tempfile::tempdir().unwrap();
    let root = forge_prepare::paths::resolve(temp.path()).unwrap();
    (temp, root)
}

/// Replaces random `.preparing-XXXX` stage names.
fn normalize(text: &str) -> String {
    let mut out = String::new();
    let mut rest = text;
    while let Some(index) = rest.find(".preparing-") {
        out.push_str(&rest[..index]);
        out.push_str(".preparing-*");
        rest = rest[index + 11..].trim_start_matches(|c: char| c.is_ascii_alphanumeric() || c == '_');
    }
    out.push_str(rest);
    out
}

fn normalize_value(value: &Value) -> Value {
    match value {
        Value::String(text) => Value::String(normalize(text)),
        Value::Array(items) => Value::Array(items.iter().map(normalize_value).collect()),
        Value::Object(map) => {
            Value::Object(map.iter().map(|(key, item)| (key.clone(), normalize_value(item))).collect())
        }
        other => other.clone(),
    }
}

/// Python's outcome without the exception class.
fn comparable(outcome: &Value) -> Value {
    let mut outcome = outcome.clone();
    if let Value::Object(map) = &mut outcome {
        map.remove("type");
    }
    normalize_value(&outcome)
}

fn assert_outcome(label: &str, rust: &Value, python: &Value) {
    assert_eq!(normalize_value(rust), comparable(python), "{label}");
}

/// Python then Rust at the same `data` path; returns both outcomes and
/// leaves Python's tree at `data-python`.
fn sequential(root: &Path, case: &str, arguments: &Value, rust: impl FnOnce() -> Value) -> (Value, Value) {
    let python = python().unwrap();
    let data = root.join("data");
    let python_outcome = oracle(&python, case, arguments);
    let aside = root.join("data-python");
    if data.exists() {
        std::fs::rename(&data, &aside).unwrap();
    } else {
        std::fs::create_dir_all(&aside).unwrap();
    }
    let rust_outcome = rust();
    std::fs::create_dir_all(&data).unwrap();
    (rust_outcome, python_outcome)
}

fn assert_same_tree_normalized(label: &str, rust: &Path, python: &Path) {
    for directory in [rust, python] {
        for (name, bytes) in common::tree(directory) {
            if let Ok(text) = String::from_utf8(bytes) {
                std::fs::write(directory.join(&name), normalize(&text)).unwrap();
            }
        }
    }
    assert_same_tree(label, rust, python);
}

fn reset(root: &Path) {
    for name in ["data", "data-python"] {
        let _ = std::fs::remove_dir_all(root.join(name));
    }
}

fn product_document() -> Value {
    json!({
        "format": "triaevum_product_info_v1", "runtime": "oot3d_native_game", "source_commit": "a".repeat(40),
        "whole_aot_plugin_abi": 2, "private_title_loaded": false,
        "capabilities": {"nri": true, "f1": true, "topscreen": true},
        "default_config": {"Graphics": {"Preset": "Enhanced", "FrameRate": {"Mode": "Interpolated2x"}}, "Audio": {"Volume": 0.75}},
    })
}

#[test]
fn json_decode_errors_match_python() {
    let Some(python) = python() else { return };
    let bs = '\\';
    let documents = [
        String::new(),
        "abc".into(),
        "{\"a\":1} x".into(),
        "{broken".into(),
        "[1 2]".into(),
        "{\"a\" 1}".into(),
        "{\"a\":".into(),
        "[1,]".into(),
        "{\"a\":1,}".into(),
        "\"abc".into(),
        "[\"a\nb\"]".into(),
        "tru".into(),
        "01".into(),
        "1.".into(),
        "-x".into(),
        "[nul]".into(),
        "\n\n  [1,]".into(),
        format!("\"a{bs}x\""),
        format!("\"ab{bs}u12\""),
        format!("[\"{bs}ud800{bs}u12\"]"),
        "{\"\u{e9}\u{1F600}\": x}".into(),
        "{ }x".into(),
        "[1, {\"a\": [true, false, null, -1.5e3]}, ]".into(),
    ];
    for document in documents {
        let expected = oracle(&python, "json_error", &json!({"text": document}));
        let actual = match forge_native::pyerr::json_loads(&document) {
            Ok(value) => json!({"ok": value}),
            Err(message) => json!({"error": message}),
        };
        assert_outcome(&format!("{document:?}"), &actual, &expected);
    }
}

#[test]
fn product_queries_match_python() {
    if python().is_none() {
        return;
    }
    let document = serde_json::to_string_pretty(&product_document()).unwrap();
    let cases = [
        ("leading lines", json!({"stdout": format!("overlay hooked\r\n{document}\n")}), false),
        ("loader failure", json!({"exit": -1073741511, "stdout": "partial", "stderr": "missing dep\r\n"}), false),
        ("unknown failure", json!({"exit": 7, "stderr": "x".repeat(3000)}), false),
        ("silent exit", json!({"stderr": "loader note"}), false),
        ("malformed", json!({"stdout": "{\"format\": tru}"}), false),
        ("not an object", json!({"stdout": "[1]"}), false),
        ("old host", json!({"stdout": "{\"format\": \"triaevum_product_info_v1\"}"}), false),
        ("plugin not loaded", json!({"stdout": document.clone()}), true),
    ];
    for (label, plan, with_plugin) in cases {
        let (_temp, root) = temp_root();
        let exe = install_fake(&root.join("runtime"), "TriAevum", &plan);
        let plugin = root.join("private.dll");
        std::fs::write(&plugin, b"plugin").unwrap();
        let log = root.join("runtime").join("TriAevum-preflight-error.json");
        let mut arguments = json!({"executable": exe.to_string_lossy(), "source_commit": "a".repeat(40)});
        if with_plugin {
            arguments["plugin"] = json!(plugin.to_string_lossy());
        }
        let expected = oracle(&python().unwrap(), "query_product", &arguments);
        let python_log = std::fs::read(&log).ok();
        let _ = std::fs::remove_file(&log);
        let plugin = with_plugin.then_some(plugin.as_path());
        let actual =
            outcome(product::query_product(&DesktopServices, &exe, &"a".repeat(40), plugin), |query| query.to_value());
        assert_outcome(label, &actual, &expected);
        let rust_log = std::fs::read(&log).ok();
        assert_eq!(
            rust_log.as_deref().map(String::from_utf8_lossy),
            python_log.as_deref().map(String::from_utf8_lossy),
            "{label}: preflight log"
        );
    }
}

#[test]
fn missing_plugin_and_runtime_config_match_python() {
    let Some(python) = python() else { return };
    let (_temp, root) = temp_root();
    let exe = install_fake(&root, "TriAevum", &json!({}));
    let missing = root.join("absent").join("private.dll");
    let expected = oracle(
        &python,
        "query_product",
        &json!({"executable": exe.to_string_lossy(), "plugin": missing.to_string_lossy()}),
    );
    let actual = outcome(product::query_product(&DesktopServices, &exe, "", Some(&missing)), |query| query.to_value());
    assert_outcome("missing plugin", &actual, &expected);

    let info = product_document();
    for (label, existing) in
        [("new", None), ("kept", Some(&b"{\"Graphics\":{}}"[..])), ("malformed", Some(&b"{\"a\": [1,]}"[..]))]
    {
        let path = root.join("data").join("config").join("TriAevum.json");
        let prepare = || {
            reset(&root);
            if let Some(bytes) = existing {
                std::fs::create_dir_all(path.parent().unwrap()).unwrap();
                std::fs::write(&path, bytes).unwrap();
            }
        };
        prepare();
        let expected =
            oracle(&python, "ensure_runtime_config", &json!({"path": path.to_string_lossy(), "product": info}));
        let python_file = std::fs::read(&path).ok();
        prepare();
        let actual =
            outcome(product::ensure_runtime_config(&path, info.as_object().unwrap()), |created| json!(created));
        assert_outcome(label, &actual, &expected);
        assert_eq!(std::fs::read(&path).ok(), python_file, "{label}: config bytes");
    }
}

/// ASCII labels, as the runtime prints them (`oot3d_game_language.cpp`):
/// Python decoded helper output with the locale code page, Rust with UTF-8.
fn language_document(codes: &[&str]) -> Value {
    let ids = |code: &str| match code {
        "en" => 1,
        "de" => 3,
        "fr" => 2,
        _ => 4,
    };
    let available: Vec<Value> = codes
        .iter()
        .map(|code| json!({"code": code, "label": format!("{code} label"), "system_id": ids(code)}))
        .collect();
    json!({"format": language::FORMAT, "selected": codes[0], "available": available})
}

#[test]
fn game_language_matches_python() {
    let Some(python) = python() else { return };
    let (_temp, root) = temp_root();
    let romfs = root.join("romfs.bin");
    for (label, plan) in [
        ("detected", json!({"stdout": language_document(&["en", "de", "fr"]).to_string()})),
        ("native failure", json!({"exit": 3, "stderr": "bad QM extents\n"})),
        ("bad document", json!({"stdout": "{\"format\": \"triaevum_game_language_v1\", \"available\": []}"})),
        ("not json", json!({"stdout": "language table:\n"})),
    ] {
        let runtime = install_fake(&root, "TriAevum", &plan);
        let expected = oracle(
            &python,
            "language_discover",
            &json!({"runtime": runtime.to_string_lossy(), "romfs": romfs.to_string_lossy()}),
        );
        let actual = outcome(language::discover(&DesktopServices, &runtime, &romfs), Value::Object);
        assert_outcome(label, &actual, &expected);
    }
    for (label, document) in [
        ("list", json!([])),
        ("format", json!({"format": "x", "available": [{}], "selected": "en"})),
        (
            "entry",
            json!({"format": language::FORMAT, "available": [{"code": "en", "label": "", "system_id": 1}], "selected": "en"}),
        ),
        (
            "bool id",
            json!({"format": language::FORMAT, "available": [{"code": "en", "label": "E", "system_id": false}], "selected": "en"}),
        ),
        (
            "selected",
            json!({"format": language::FORMAT, "available": [{"code": "en", "label": "E", "system_id": 0}], "selected": "it"}),
        ),
        ("valid", language_document(&["en", "it"])),
    ] {
        let expected = oracle(&python, "language_validate", &json!({"document": document}));
        assert_outcome(label, &outcome(language::validate(&document), Value::Object), &expected);
    }
    // One persisted preference across installs and ROM switches.
    let steps: Vec<(&str, Value)> = vec![
        ("language_install", json!({"detected": language_document(&["en", "it"])})),
        ("language_select", json!({"code": "it"})),
        ("language_install", json!({"detected": language_document(&["en", "it", "de"])})),
        ("language_select", json!({"code": "fr"})),
        ("language_install", json!({"detected": language_document(&["de", "en"])})),
    ];
    let python_root = root.join("python-language");
    let rust_root = root.join("rust-language");
    for (index, (case, arguments)) in steps.into_iter().enumerate() {
        let mut python_arguments = arguments.clone();
        python_arguments["data_root"] = json!(python_root.to_string_lossy());
        let expected = oracle(&python, case, &python_arguments);
        let actual = match case {
            "language_install" => outcome(language::install(&rust_root, &arguments["detected"]), |()| Value::Null),
            _ => outcome(language::select(&rust_root, arguments["code"].as_str().unwrap()), |()| Value::Null),
        };
        assert_outcome(&format!("step {index}"), &actual, &expected);
        assert_same_tree(&format!("step {index}"), &rust_root, &python_root);
    }
}

fn seed(root: &Path, title: &Value) -> Value {
    let mut events = Vec::new();
    let result = forge_native::prepare_shader_seed(
        &DesktopServices,
        root,
        &root.join("data"),
        title.as_object().unwrap(),
        &mut |stage, message| events.push(json!([stage, message])),
    );
    outcome(result, |path| json!({"result": path.map(|path| path.to_string_lossy().into_owned()), "events": events}))
}

fn compare_seed(label: &str, root: &Path, title: &Value) {
    reset(root);
    let arguments =
        json!({"root": root.to_string_lossy(), "data_root": root.join("data").to_string_lossy(), "title": title});
    let (actual, expected) = sequential(root, "prepare_shader_seed", &arguments, || seed(root, title));
    assert_outcome(label, &actual, &expected);
    assert_same_tree_normalized(label, &root.join("data"), &root.join("data-python"));
}

fn tool(root: &Path, name: &str, plan: &Value) -> Value {
    let path = install_fake(root, name, plan);
    record(root, &path.file_name().unwrap().to_string_lossy())
}

fn pack_writer(complete: bool) -> Value {
    json!({"write": [
        {"flag": "--output", "content": json!({"transferable_import": {"complete_import": complete, "shaders": 2}}).to_string()},
        {"flag": "--pack", "content": String::from_utf8(synthetic_pack(3)).unwrap()},
    ]})
}

#[test]
fn shader_seeds_match_python() {
    if python().is_none() {
        return;
    }
    let (_temp, root) = temp_root();
    let portable = json!({"recipe": "oot3d-eur \u{e9}", "shader_preparation": {
        "format": FORMAT, "mode": "portable_pack", "descriptor_schema_version": 3, "note": 1.25,
        "pack": artifact(&root, "forge/seed.o3ps", &synthetic_pack(3))}});
    compare_seed("portable", &root, &portable);
    let mut wrong_schema = portable.clone();
    wrong_schema["shader_preparation"]["descriptor_schema_version"] = json!(4);
    compare_seed("schema", &root, &wrong_schema);
    let mut escaped = portable.clone();
    escaped["shader_preparation"]["pack"]["path"] = json!("../seed.o3ps");
    compare_seed("escape", &root, &escaped);

    let compiler = tool(&root, "compiler", &pack_writer(true));
    let inventories = json!({"recipe": "test", "shader_preparation": {
        "format": FORMAT, "mode": "source_inventories", "descriptor_schema_version": 3, "compiler": compiler,
        "inventories": [artifact(&root, "native.json", b"native"), artifact(&root, "effects.json", b"effects")],
        "dependencies": [artifact(&root, "shaderc.so", b"shaderc")]}});
    compare_seed("inventories", &root, &inventories);

    let importer = tool(&root, "importer", &pack_writer(true));
    let citra = json!({"recipe": "test", "shader_preparation": {
        "format": FORMAT, "mode": "citra_transferable", "descriptor_schema_version": 3, "dialect": "azahar-v1",
        "caches": [artifact(&root, "cache.bin", b"cache")], "importer": importer, "compiler": compiler,
        "inventories": [artifact(&root, "native.json", b"native")]}});
    compare_seed("transferable", &root, &citra);
    let mut no_dialect = citra.clone();
    no_dialect["shader_preparation"].as_object_mut().unwrap().remove("dialect");
    compare_seed("dialect", &root, &no_dialect);

    let partial = tool(&root, "importer", &pack_writer(false));
    let mut incomplete = citra.clone();
    incomplete["shader_preparation"]["importer"] = partial;
    compare_seed("partial import", &root, &incomplete);

    let failing = tool(&root, "compiler", &json!({"exit": 2, "stdout": "out", "stderr": "shaderc: error\r\nline 2"}));
    let mut failed = inventories.clone();
    failed["shader_preparation"]["compiler"] = failing;
    compare_seed("compiler failure", &root, &failed);
}

fn renderer_result(cache: &str, modules: u32) -> String {
    json!({"format": RENDERER_FORMAT, "modules": modules, "hits": modules - 2, "builtin_hits": 1, "compiled": 2,
        "compile_failed": 0, "write_failed": 0, "writes": 2, "compile_ms": 12.345678, "cache_directory": cache,
        "game_booted": false})
    .to_string()
}

#[test]
fn renderer_passes_match_python() {
    if python().is_none() {
        return;
    }
    let (_temp, root) = temp_root();
    let cache = root.join("data").join("cache").join("renderer");
    let placeholder = "{flag:--prepare-renderer-cache}";
    let plans = [
        ("complete", json!({"write": [{"flag": "--manifest", "content": renderer_result(placeholder, 22)}]})),
        ("too many", json!({"write": [{"flag": "--manifest", "content": renderer_result(placeholder, 300)}]})),
        ("misdirected", json!({"write": [{"flag": "--manifest", "content": renderer_result("C:/elsewhere", 22)}]})),
        ("native failure", json!({"exit": 1, "stderr": "renderer shader cache could not persist every module"})),
        ("no manifest", json!({})),
        ("broken manifest", json!({"write": [{"flag": "--manifest", "content": "{\"format\": "}]})),
    ];
    for (label, plan) in plans {
        reset(&root);
        let compiler = tool(&root, "compiler", &plan);
        let title = json!({"renderer_shader_preparation": {"format": "triaevum_renderer_shader_compiler_v1",
            "compiler": compiler, "dependencies": [artifact(&root, "shaderc.dll", b"shaderc")]}});
        let arguments = json!({"root": root.to_string_lossy(), "data_root": root.join("data").to_string_lossy(),
            "title": title, "cache": cache.to_string_lossy()});
        let (actual, expected) = sequential(&root, "prepare_renderer", &arguments, || {
            let mut events = Vec::new();
            let result = forge_native::prepare_renderer_shader_cache(
                &DesktopServices,
                &root,
                &root.join("data"),
                title.as_object().unwrap(),
                &cache,
                &mut |stage, message| events.push(json!([stage, message])),
            );
            outcome(result, |result| json!({"result": result.map(Value::Object), "events": events}))
        });
        assert_outcome(label, &actual, &expected);
        assert_same_tree_normalized(label, &root.join("data"), &root.join("data-python"));
    }
}

const SEPARATOR: &str = if cfg!(windows) { "\\\\" } else { "/" };

fn device_plan(prewarm: &str, exit: i64, total: u64, prepared: u64) -> Value {
    let report = format!(
        "{{\"format\": \"{DEVICE_FORMAT}\", \"device_pipeline_prewarm\": \"{prewarm}\", \"total\": {total}, \
         \"prepared\": {prepared}, \"failed\": 0, \"error\": \"\", \"game_booted\": false, \"gpu\": \"Fake GPU \u{e9}\", \
         \"seconds\": 0.125, \"pipeline_cache_uuid\": \"00ff\", \"cache_path\": \"{{flag:--cache-dir}}{SEPARATOR}{CACHE_FILENAME}\"}}"
    );
    json!({"exit": exit, "progress": [json!({"event": "pipeline_progress", "attempted": prepared, "total": total}).to_string()],
        "sleep_ms": 150, "write": [
        {"flag": "--cache-dir", "join": CACHE_FILENAME, "content": "driver cache bytes"},
        {"flag": "--report", "content": report},
    ]})
}

#[test]
fn device_pipelines_match_python() {
    if python().is_none() {
        return;
    }
    let (_temp, root) = temp_root();
    let pack = root.join("data-pack.o3ps");
    std::fs::write(&pack, synthetic_pack(3)).unwrap();
    let cache = root.join("cache");
    let mut cancelled = device_plan("cancelled", 4, 10, 3);
    cancelled["wait_for"] = json!("--cancel-file");
    let cases = [
        ("complete", device_plan("complete", 0, 500, 500), json!({}), false),
        ("two manifests", device_plan("complete", 0, 7, 7), json!({"two": true}), false),
        ("startup programs", device_plan("complete", 0, 7, 7), json!({"startup_programs": true, "adapter": 2}), false),
        ("partial", device_plan("partial", 3, 500, 499), json!({}), false),
        ("complete status 3", device_plan("complete", 3, 5, 5), json!({}), false),
        ("cancelled", cancelled, json!({}), true),
        (
            "no report",
            json!({"exit": 1, "stderr": format!("NRI pipeline preparation: {}\n", "e".repeat(5000))}),
            json!({}),
            false,
        ),
        (
            "bad report",
            json!({"write": [{"flag": "--report", "content": "{\"format\": \"other\"}"}]}),
            json!({}),
            false,
        ),
    ];
    for (label, plan, options, cancel) in cases {
        reset(&root);
        let _ = std::fs::remove_dir_all(&cache);
        let helper = tool(&root, "helper", &plan);
        let mut contract = json!({"format": DEVICE_FORMAT, "helper": helper,
            "dependencies": [artifact(&root, "dep.dll", b"dependency")]});
        if options.get("two").is_some() {
            let native = artifact(&root, "native.json", b"native");
            contract["manifests"] = json!([native, artifact(&root, "effects.json", b"effects"), native]);
        } else {
            contract["manifest"] = artifact(&root, "native.json", b"native");
        }
        for key in ["startup_programs", "adapter"] {
            if let Some(value) = options.get(key) {
                contract[key] = value.clone();
            }
        }
        let title = json!({"device_pipeline_preparation": contract});
        let arguments = json!({"root": root.to_string_lossy(), "data_root": root.join("data").to_string_lossy(),
            "title": title, "pack": pack.to_string_lossy(), "cache": cache.to_string_lossy(), "cancel": cancel});
        let python_cache = root.join("cache-python");
        let (actual, expected) = sequential(&root, "prepare_device", &arguments, || {
            let _ = std::fs::remove_dir_all(&python_cache);
            if cache.exists() {
                std::fs::rename(&cache, &python_cache).unwrap();
            }
            let token = CancelToken::new();
            if cancel {
                token.cancel();
            }
            let mut events = Vec::new();
            let result = forge_native::prepare_device_pipelines(
                &DesktopServices,
                &root,
                &root.join("data"),
                title.as_object().unwrap(),
                Some(&pack),
                Some(&cache),
                &mut |stage, message| events.push(json!([stage, message])),
                &token,
            );
            outcome(result, |result| json!({"result": result.map(Value::Object), "events": events}))
        });
        assert_outcome(label, &actual, &expected);
        assert_same_tree_normalized(label, &root.join("data"), &root.join("data-python"));
        if python_cache.exists() {
            assert_same_tree(label, &cache, &python_cache);
        }
    }
}

#[test]
fn cache_adoption_matches_python() {
    let Some(python) = python() else { return };
    let (_temp, root) = temp_root();
    let source = root.join("previous");
    std::fs::create_dir_all(source.join("spirv-v2")).unwrap();
    for (name, size) in [
        (CACHE_FILENAME, 100usize),
        ("pipeline_cache.bin", 0),
        ("spirv-v2/ab_cd.spvc", 10),
        ("spirv-v2/0123_ff.spvc", 17 * 1024 * 1024),
        ("spirv-v2/ABCD_ef.spvc", 10),
        ("unknown.bin", 5),
    ] {
        std::fs::write(source.join(name), vec![7u8; size]).unwrap();
    }
    let python_destination = root.join("python-cache");
    let expected = oracle(
        &python,
        "adopt_existing_cache",
        &json!({"destination": python_destination.to_string_lossy(), "source": source.to_string_lossy()}),
    );
    let destination = root.join("rust-cache");
    let mut events = Vec::new();
    let copied = forge_native::adopt_existing_cache(&destination, Some(&source), &mut |stage, message| {
        events.push(json!([stage, message]))
    });
    assert_outcome("adoption", &json!({"ok": {"result": copied, "events": events}}), &expected);
    assert_same_tree("adoption", &destination, &python_destination);
}

struct Installed {
    root: PathBuf,
    exe: PathBuf,
    data: PathBuf,
    title: PathBuf,
    profile: PathBuf,
}

fn installed(root: &Path) -> (Installed, Map<String, Value>) {
    let platform = forge_catalog::host_platform().unwrap();
    let exe = root.join(platform.installed_runtime);
    std::fs::write(&exe, b"host").unwrap();
    let plugin = root.join(platform.title_module);
    std::fs::write(&plugin, b"title").unwrap();
    let data = root.join("custom-data");
    let title = data.join("titles").join("fixture");
    std::fs::create_dir_all(&title).unwrap();
    let profile = root.join("TriAevum.launch.json");
    let mut arguments = Vec::new();
    for (option, path) in [
        ("--a32-process-manifest", title.join("process-manifest.json")),
        ("--config", data.join("config").join("TriAevum.json")),
        ("--topscreen-config", data.join("config").join("topscreen_ui.json")),
        ("--save-data", data.join("savedata")),
    ] {
        if option != "--save-data" {
            atomic_write_json(&path, &json!({})).unwrap();
        }
        arguments.extend([json!(option), json!(path.to_string_lossy())]);
    }
    let water = data.join("cache/water/key/water_textures.txt");
    std::fs::create_dir_all(water.parent().unwrap()).unwrap();
    std::fs::write(&water, b"triaevum-water-textures 1\n").unwrap();
    arguments.extend([
        json!("--water-texture-catalog"),
        json!("${profile_dir}/custom-data/cache/water/key/water_textures.txt"),
    ]);
    atomic_write_json(&profile, &json!({"format": "oot3d_native_game_launch_profile_v1", "arguments": arguments}))
        .unwrap();
    let sha = |path: &Path| sha256_hex(&std::fs::read(path).unwrap());
    let receipt = json!({"status": "ready", "runtime_sha256": sha(&exe), "plugin_sha256": sha(&plugin),
        "launch_profile": profile.to_string_lossy(), "launch_profile_sha256": sha(&profile),
        "water_textures": {"path": water.to_string_lossy(), "sha256": sha(&water)}});
    (Installed { root: root.to_path_buf(), exe, data, title, profile }, receipt.as_object().unwrap().clone())
}

#[test]
fn installed_runtime_matches_python() {
    let Some(python) = python() else { return };
    let (_temp, root) = temp_root();
    let (fixture, receipt) = installed(&root);
    let other = if forge_catalog::host_platform().unwrap() == forge_catalog::LINUX {
        "x86_64-pc-windows-msvc"
    } else {
        "x86_64-unknown-linux-gnu"
    };
    let mut variants: Vec<(&str, Map<String, Value>, Option<PathBuf>)> = vec![("matching", receipt.clone(), None)];
    let mut legacy = receipt.clone();
    legacy.remove("launch_profile_sha256");
    variants.push(("legacy", legacy.clone(), None));
    variants.push(("wrong data root", receipt.clone(), Some(fixture.root.clone())));
    for (key, value) in [
        ("status", json!("pending")),
        ("target", json!(other)),
        ("target", json!("riscv")),
        ("plugin_sha256", json!("0".repeat(64))),
        ("launch_profile", json!("")),
        ("launch_profile_sha256", json!("f".repeat(64))),
        ("plugin", json!("private-plugins/missing.dll")),
        ("water_textures", json!({"path": "elsewhere.txt", "sha256": "0"})),
        ("scene_lights", json!({"path": fixture.data.join("lights.json").to_string_lossy(), "sha256": "0"})),
        ("renderer_cache", json!({"path": "cache/renderer"})),
    ] {
        let mut changed = legacy.clone();
        changed.insert(key.into(), value);
        variants.push((key, changed, None));
    }
    for (label, runtime, data_root) in variants {
        let data_root = data_root.unwrap_or_else(|| fixture.data.clone());
        let expected = oracle(
            &python,
            "validate_installed",
            &json!({"executable": fixture.exe.to_string_lossy(),
            "title": fixture.title.to_string_lossy(), "data_root": data_root.to_string_lossy(), "runtime": runtime}),
        );
        let actual = outcome(
            forge_native::validate_installed_runtime(&fixture.exe, &fixture.title, &data_root, &runtime),
            |profile| json!(profile.to_string_lossy()),
        );
        assert_outcome(label, &actual, &expected);
    }
    assert!(fixture.profile.is_file());
}

#[test]
fn shader_corpus_matches_python() {
    let Some(python) = python() else { return };
    let (_temp, root) = temp_root();
    let pack = root.join("input.o3ps");
    std::fs::write(
        &pack,
        common::encode_pack(3, &[((2, 1, 2, 4), vec![3, 2, 0x23, 7]), ((1, 9, 9, 9), vec![3, 2, 0x23, 7, 0, 0, 0, 1])]),
    )
    .unwrap();
    let manifest = root.join("input.json");
    atomic_write_json(
        &manifest,
        &json!({"format": "oot3d_pica_pipeline_manifest_v2", "schema_version": 2,
        "descriptor_schema_version": 3, "pipeline_count": 1, "pipelines": [{}]}),
    )
    .unwrap();
    let helper = root.join("helper");
    std::fs::write(&helper, b"test helper").unwrap();
    let dependency = root.join("libshaderc.so");
    std::fs::write(&dependency, b"library").unwrap();
    let sources = root.join("checkout");
    let runtime_input = "runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp";
    std::fs::create_dir_all(sources.join(runtime_input).parent().unwrap()).unwrap();
    std::fs::write(sources.join("generator.cpp"), "generator").unwrap();
    std::fs::write(sources.join(runtime_input), "runtime").unwrap();
    let qualification = root.join("qualification.json");
    let made = oracle(
        &python,
        "make_qualification",
        &json!({"sources": sources.to_string_lossy(),
        "generator": ["generator.cpp"], "runtime": [runtime_input], "pack": pack.to_string_lossy(),
        "manifests": [manifest.to_string_lossy()], "output": qualification.to_string_lossy()}),
    );
    assert_eq!(made, json!({"ok": null}));

    for (label, data) in [
        ("canonical", std::fs::read(&pack).unwrap()),
        ("trailing", [std::fs::read(&pack).unwrap(), b"tail".to_vec()].concat()),
        ("driver cache", b"not a portable pack; GPU cache fixture".to_vec()),
        ("header", synthetic_pack(3)),
    ] {
        let candidate = root.join("candidate.o3ps");
        std::fs::write(&candidate, &data).unwrap();
        let expected = oracle(&python, "read_portable_corpus", &json!({"pack": candidate.to_string_lossy()}));
        let actual =
            outcome(corpus::read_portable_corpus(&candidate), |(schema, modules)| json!([schema, modules.len()]));
        assert_outcome(label, &actual, &expected);
    }

    let catalog = |target: &str| {
        json!({"target": target, "format": "x", "titles": [
        {"recipe": "eur", "renderer_shader_preparation": {"compiler": {"path": "forge/c.exe"}, "dependencies": []},
         "shader_preparation": {"old": true}, "z": 1},
        {"recipe": "usa", "renderer_shader_preparation": {"compiler": {"path": "forge/c.exe"}}}]})
    };
    let bind = |catalog: &Value| {
        let arguments = json!({"catalog": catalog, "pack": pack.to_string_lossy(), "manifests": [manifest.to_string_lossy()],
            "helper": helper.to_string_lossy(), "dependencies": [dependency.to_string_lossy()],
            "qualification": qualification.to_string_lossy(), "source_root": sources.to_string_lossy()});
        let expected = oracle(&python, "bind_shader_corpus", &arguments);
        let actual = outcome(
            corpus::bind_shader_corpus(
                catalog.as_object().unwrap(),
                &pack,
                std::slice::from_ref(&manifest),
                &helper,
                std::slice::from_ref(&dependency),
                &qualification,
                &sources,
            ),
            |(catalog, items)| json!([catalog, items]),
        );
        (actual, expected)
    };
    for target in ["x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu", "aarch64-linux-android"] {
        let (actual, expected) = bind(&catalog(target));
        assert_outcome(target, &actual, &expected);
        // Key order of the bound catalog, as the publisher would serialize it unsorted.
        if let (Some(actual), Some(expected)) = (actual.get("ok"), expected.get("ok")) {
            assert_eq!(actual.to_string(), expected.to_string(), "{target}: key order");
        }
    }
    std::fs::write(sources.join("generator.cpp"), "generator v2").unwrap();
    let (actual, expected) = bind(&catalog("x86_64-pc-windows-msvc"));
    assert_outcome("stale generator", &actual, &expected);
    std::fs::write(sources.join("generator.cpp"), "generator").unwrap();
    let (actual, expected) = bind(&json!({"target": "x86_64-pc-windows-msvc", "titles": [{"recipe": "eur"}]}));
    assert_outcome("no renderer", &actual, &expected);
}

/// Local-only: the built runtime's `--product-info` (headless, no title
/// boot) through both implementations, when TRIAEVUM_TEST_RUNTIME names it.
#[test]
fn real_runtime_product_query_matches_python() {
    let Some(python) = python() else { return };
    let Some(runtime) = std::env::var_os("TRIAEVUM_TEST_RUNTIME").map(PathBuf::from) else {
        eprintln!("skipped: TRIAEVUM_TEST_RUNTIME is not set");
        return;
    };
    let expected = oracle(&python, "query_product", &json!({"executable": runtime.to_string_lossy()}));
    let actual = outcome(product::query_product(&DesktopServices, &runtime, "", None), |query| query.to_value());
    assert_outcome("real runtime", &actual, &expected);
    eprintln!("real runtime product query: {}", if actual.get("ok").is_some() { "ok" } else { "error" });
}
