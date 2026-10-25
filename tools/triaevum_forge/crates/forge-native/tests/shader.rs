// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Ports of test_shader_preparation.py and test_renderer_shader_preparation.py;
//! the importer and compilers are fake helper executables.

mod common;

use std::path::{Path, PathBuf};

use common::{artifact, install_fake, runs, set_plan, synthetic_pack};
use forge_native::shader::{FORMAT, RENDERER_CONTRACT, RENDERER_FORMAT};
use forge_native::{prepare_renderer_shader_cache, prepare_shader_seed, DesktopServices};
use serde_json::{json, Map, Value};

struct Fixture {
    _temp: tempfile::TempDir,
    root: PathBuf,
    data: PathBuf,
}

impl Fixture {
    fn new() -> Self {
        let temp = tempfile::tempdir().unwrap();
        let root = forge_prepare::paths::resolve(temp.path()).unwrap();
        let data = root.join("data");
        Self { _temp: temp, root, data }
    }

    /// A fake tool inside the package root and its catalog record.
    fn tool(&self, name: &str, plan: &Value) -> (PathBuf, Value) {
        let path = install_fake(&self.root, name, plan);
        let relative = path.file_name().unwrap().to_string_lossy().into_owned();
        (path, common::record(&self.root, &relative))
    }

    fn seed(&self, title: &Value) -> forge_base::Result<Option<PathBuf>> {
        prepare_shader_seed(&DesktopServices, &self.root, &self.data, title.as_object().unwrap(), &mut |_, _| {})
    }

    fn renderer(&self, title: &Value) -> forge_base::Result<Option<Map<String, Value>>> {
        let cache = self.root.join("data/cache/renderer");
        prepare_renderer_shader_cache(
            &DesktopServices,
            &self.root,
            &self.data,
            title.as_object().unwrap(),
            &cache,
            &mut |_, _| {},
        )
    }
}

fn pack_text() -> String {
    String::from_utf8(synthetic_pack(3)).unwrap()
}

/// The fake importer/compiler of `test_shader_preparation.fake_run`.
fn compiler_plan(complete: bool) -> Value {
    json!({"write": [
        {"flag": "--output", "content": json!({"transferable_import": {"complete_import": complete}}).to_string()},
        {"flag": "--pack", "content": pack_text()},
    ]})
}

fn portable_title(fixture: &Fixture) -> Value {
    json!({"recipe": "test", "shader_preparation": {
        "format": FORMAT, "mode": "portable_pack", "descriptor_schema_version": 3,
        "pack": artifact(&fixture.root, "seed.o3ps", &synthetic_pack(3))}})
}

#[test]
fn optional_off_never_runs_a_tool() {
    let fixture = Fixture::new();
    assert_eq!(fixture.seed(&json!({})).unwrap(), None);
    assert_eq!(fixture.renderer(&json!({})).unwrap(), None);
}

#[test]
fn portable_pack_copy_reuse_and_corruption_repair() {
    let fixture = Fixture::new();
    let title = portable_title(&fixture);
    let pack = fixture.seed(&title).unwrap().unwrap();
    assert_eq!(std::fs::read(&pack).unwrap(), synthetic_pack(3));
    let before = std::fs::metadata(&pack).unwrap().modified().unwrap();
    let mut messages = Vec::new();
    let again = prepare_shader_seed(
        &DesktopServices,
        &fixture.root,
        &fixture.data,
        title.as_object().unwrap(),
        &mut |stage, message| messages.push(format!("{stage}: {message}")),
    )
    .unwrap();
    assert_eq!(again.as_ref(), Some(&pack));
    assert_eq!(messages, ["shaders: Reusing prepared portable shaders..."]);
    assert_eq!(std::fs::metadata(&pack).unwrap().modified().unwrap(), before);
    std::fs::write(&pack, b"damaged").unwrap();
    assert_eq!(std::fs::read(fixture.seed(&title).unwrap().unwrap()).unwrap(), synthetic_pack(3));
    let receipt: Value =
        serde_json::from_slice(&std::fs::read(pack.parent().unwrap().join("preparation.json")).unwrap()).unwrap();
    assert_eq!(receipt["device_pipeline_prewarm"], "not_performed");
    assert_eq!(receipt["game_coverage_proven"], false);
    assert_eq!(std::fs::read_dir(pack.parent().unwrap()).unwrap().count(), 2);
}

#[test]
fn bad_schema_integrity_and_path_are_rejected() {
    let fixture = Fixture::new();
    let original = portable_title(&fixture);
    for (pointer, value) in [
        ("/shader_preparation/descriptor_schema_version", json!(4)),
        ("/shader_preparation/descriptor_schema_version", json!(true)),
        ("/shader_preparation/pack/sha256", json!("0".repeat(64))),
        ("/shader_preparation/pack/path", json!("../outside")),
        ("/shader_preparation/mode", json!("guess")),
        ("/shader_preparation/format", json!("other")),
    ] {
        let mut title = original.clone();
        *title.pointer_mut(pointer).unwrap() = value;
        assert!(fixture.seed(&title).is_err(), "{pointer}");
    }
}

fn transferable(fixture: &Fixture, plan: &Value) -> (PathBuf, Value) {
    let (importer, importer_record) = fixture.tool("importer", plan);
    let (_, compiler_record) = fixture.tool("compiler", plan);
    let title = json!({"recipe": "test", "shader_preparation": {
        "format": FORMAT, "mode": "citra_transferable", "descriptor_schema_version": 3,
        "dialect": "citra-legacy-v1", "caches": [artifact(&fixture.root, "cache.bin", b"fixture")],
        "importer": importer_record, "compiler": compiler_record,
        "dependencies": [artifact(&fixture.root, "shaderc.so", b"fixture")], "inventories": []}});
    (importer, title)
}

#[test]
fn translates_once_without_title_build_and_invalidates_on_dependency() {
    let fixture = Fixture::new();
    let (importer, mut title) = transferable(&fixture, &compiler_plan(true));
    let compiler = fixture.root.join(format!("compiler{}", std::env::consts::EXE_SUFFIX));
    let first = fixture.seed(&title).unwrap().unwrap();
    assert_eq!((runs(&importer).len(), runs(&compiler).len()), (1, 1));
    let import = &runs(&importer)[0]["args"];
    assert_eq!(import[0], "--dialect");
    assert_eq!(import[1], "citra-legacy-v1");
    assert_eq!(import[4], "--input");
    let compile = &runs(&compiler)[0];
    assert_eq!(compile["args"].as_array().unwrap().iter().filter(|value| *value == "--inventory").count(), 1);
    assert_eq!(Path::new(compile["cwd"].as_str().unwrap()), fixture.root);
    assert_eq!(fixture.seed(&title).unwrap().unwrap(), first);
    assert_eq!(runs(&compiler).len(), 1);
    title["shader_preparation"]["dependencies"] = json!([artifact(&fixture.root, "shaderc.so", b"new")]);
    assert_ne!(fixture.seed(&title).unwrap().unwrap(), first);
    assert_eq!(runs(&compiler).len(), 2);
}

#[test]
fn partial_import_is_not_activated() {
    let fixture = Fixture::new();
    let (importer, title) = transferable(&fixture, &compiler_plan(false));
    let error = fixture.seed(&title).unwrap_err();
    assert_eq!(error.message, "Transferable shader import was incomplete");
    assert_eq!(runs(&importer).len(), 1);
    assert!(!common::tree(&fixture.data).iter().any(|(name, _)| name.ends_with("portable.o3ps")));
}

#[test]
fn dialect_must_be_explicit() {
    let fixture = Fixture::new();
    let (_, mut title) = transferable(&fixture, &compiler_plan(true));
    title["shader_preparation"].as_object_mut().unwrap().remove("dialect");
    assert!(fixture.seed(&title).unwrap_err().message.contains("dialect"));
}

#[test]
fn known_native_and_extension_sources_compile_together_and_reuse() {
    let fixture = Fixture::new();
    let (compiler, compiler_record) = fixture.tool("compiler", &compiler_plan(true));
    let mut title = json!({"recipe": "test", "shader_preparation": {
        "format": FORMAT, "mode": "source_inventories", "descriptor_schema_version": 3,
        "compiler": compiler_record,
        "inventories": [artifact(&fixture.root, "native.json", b"fixture"), artifact(&fixture.root, "effects.json", b"fixture")],
        "dependencies": [artifact(&fixture.root, "shaderc.so", b"fixture")]}});
    let first = fixture.seed(&title).unwrap().unwrap();
    assert_eq!(fixture.seed(&title).unwrap().unwrap(), first);
    assert_eq!(runs(&compiler).len(), 1);
    let command: Vec<String> =
        runs(&compiler)[0]["args"].as_array().unwrap().iter().map(|value| value.as_str().unwrap().to_owned()).collect();
    assert_eq!(command.iter().filter(|value| *value == "--inventory").count(), 2);
    assert!(!command.contains(&"--input".to_string()) && !command.contains(&"--title-plugin".to_string()));
    title["shader_preparation"]["inventories"][1] = artifact(&fixture.root, "effects.json", b"new variant");
    assert_ne!(fixture.seed(&title).unwrap().unwrap(), first);
    assert!(first.is_file());
    assert_eq!(runs(&compiler).len(), 2);
}

#[test]
fn inventory_preparation_requires_verified_inputs() {
    let fixture = Fixture::new();
    let (compiler, compiler_record) = fixture.tool("compiler", &compiler_plan(true));
    let mut title = json!({"recipe": "test", "shader_preparation": {
        "format": FORMAT, "mode": "source_inventories", "descriptor_schema_version": 3,
        "compiler": compiler_record, "inventories": []}});
    assert!(fixture.seed(&title).unwrap_err().message.contains("no source inventories"));
    title["shader_preparation"]["inventories"] = json!([artifact(&fixture.root, "native.json", b"fixture")]);
    std::fs::write(fixture.root.join("native.json"), b"changed").unwrap();
    assert!(fixture.seed(&title).unwrap_err().message.contains("integrity"));
    assert!(runs(&compiler).is_empty());
}

#[test]
fn compiler_failure_reports_its_diagnostics() {
    let fixture = Fixture::new();
    let (_, compiler_record) = fixture.tool("compiler", &json!({"exit": 1, "stderr": "boom\r\n"}));
    let title = json!({"recipe": "test", "shader_preparation": {
        "format": FORMAT, "mode": "source_inventories", "descriptor_schema_version": 3,
        "compiler": compiler_record, "inventories": [artifact(&fixture.root, "native.json", b"fixture")]}});
    assert_eq!(fixture.seed(&title).unwrap_err().message, "Shader preparation failed: boom\n");
}

fn renderer_plan(modules: u32, hits: u32, cache: Option<&str>) -> Value {
    let cache = cache.map_or("{flag:--prepare-renderer-cache}".to_string(), |cache| cache.replace('\\', "\\\\"));
    let result = format!(
        "{{\"format\": \"{RENDERER_FORMAT}\", \"modules\": {modules}, \"hits\": {hits}, \"builtin_hits\": {hits}, \
         \"compiled\": 0, \"compile_failed\": 0, \"write_failed\": 0, \"writes\": 0, \"compile_ms\": 1.5, \
         \"cache_directory\": \"{cache}\", \"game_booted\": false}}"
    );
    json!({"write": [{"flag": "--manifest", "content": result}]})
}

fn renderer_title(fixture: &Fixture, plan: &Value) -> (PathBuf, Value) {
    let (compiler, record) = fixture.tool("compiler", plan);
    (compiler, json!({"shader_preparation": {"format": FORMAT, "compiler": record}}))
}

#[test]
fn prepare_before_launch_rechecks_cache_without_title_or_gpu_boot() {
    let fixture = Fixture::new();
    let (compiler, title) = renderer_title(&fixture, &renderer_plan(22, 22, None));
    let result = fixture.renderer(&title).unwrap().unwrap();
    assert_eq!(result["renderer_shader_preparation"], "complete");
    assert_eq!(fixture.renderer(&title).unwrap().unwrap()["hits"], 22);
    let run = &runs(&compiler)[1]["args"];
    let cache = fixture.root.join("data").join("cache").join("renderer");
    assert_eq!(run[0], "--prepare-renderer-cache");
    assert_eq!(run[1], json!(cache.to_string_lossy()));
    assert_eq!(run[2], "--manifest");
    let receipt = fixture.data.join("shader-seeds/renderer-preparation/latest.json");
    let written: Value = serde_json::from_slice(&std::fs::read(receipt).unwrap()).unwrap();
    assert_eq!(written["compiler_sha256"].as_str().unwrap().len(), 64);
    assert_eq!(written["dependency_sha256"], json!([]));
}

#[test]
fn public_renderer_contract_without_private_seed() {
    let fixture = Fixture::new();
    let (compiler, mut title) = renderer_title(&fixture, &renderer_plan(22, 22, None));
    let mut seed = title.as_object_mut().unwrap().remove("shader_preparation").unwrap();
    seed["format"] = json!(RENDERER_CONTRACT);
    title["renderer_shader_preparation"] = seed;
    assert_eq!(fixture.seed(&title).unwrap(), None);
    assert_eq!(fixture.renderer(&title).unwrap().unwrap()["renderer_shader_preparation"], "complete");
    assert_eq!(runs(&compiler).len(), 1);
    title["shader_preparation"] = json!({"format": "not-a-renderer-compiler"});
    assert_eq!(fixture.renderer(&title).unwrap().unwrap()["renderer_shader_preparation"], "complete");
}

#[test]
fn invalid_artifact_fails_before_tool() {
    let fixture = Fixture::new();
    let (compiler, title) = renderer_title(&fixture, &renderer_plan(22, 22, None));
    let mut changed = title.clone();
    changed["shader_preparation"]["compiler"]["sha256"] = json!("0".repeat(64));
    assert!(fixture.renderer(&changed).is_err());
    assert!(runs(&compiler).is_empty());
}

#[test]
fn failed_or_misdirected_cache_is_not_reported_complete() {
    let fixture = Fixture::new();
    let (compiler, title) = renderer_title(&fixture, &json!({"exit": 1, "stderr": "old tool"}));
    let result = fixture.renderer(&title).unwrap().unwrap();
    assert_eq!(result["renderer_shader_preparation"], "failed");
    assert_eq!(result["error"], "Shader preparation failed: old tool");
    let wrong = fixture.root.join("wrong-cache");
    set_plan(&compiler, &renderer_plan(22, 22, Some(&wrong.to_string_lossy())));
    let result = fixture.renderer(&title).unwrap().unwrap();
    assert_eq!(result["error"], "Renderer shader preparation did not persist the requested cache");
    set_plan(&compiler, &renderer_plan(300, 300, None));
    assert_eq!(fixture.renderer(&title).unwrap().unwrap()["renderer_shader_preparation"], "failed");
    set_plan(&compiler, &json!({}));
    let result = fixture.renderer(&title).unwrap().unwrap();
    assert!(
        result["error"].as_str().unwrap().starts_with("[Errno 2] No such file or directory: "),
        "{}",
        result["error"]
    );
}
