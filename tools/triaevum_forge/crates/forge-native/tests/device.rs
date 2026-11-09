// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Port of test_device_pipeline_preparation.py with a fake pipeline helper.
//! No GPU work runs here.

mod common;

use std::cell::RefCell;
use std::ffi::OsString;
use std::path::{Path, PathBuf};

use common::{artifact, install_fake, runs, set_plan};
use forge_base::CancelToken;
use forge_native::desktop::run_device_helper;
use forge_native::device::{prepare_title_device_pipelines, title_cache_filename, CACHE_FILENAME, FORMAT};
use forge_native::services::*;
use forge_native::{adopt_existing_cache, prepare_device_pipelines, renderer_cache_directory, DesktopServices};
use serde_json::{json, Map, Value};

/// Desktop services that also remember each device job.
#[derive(Default)]
struct Recording {
    jobs: RefCell<Vec<DeviceJob>>,
}

impl NativeServices for Recording {
    fn product_info(&self, runtime: &Path, plugin: Option<&Path>) -> forge_base::Result<NativeOutput> {
        DesktopServices.product_info(runtime, plugin)
    }
    fn game_language_info(&self, runtime: &Path, query: &LanguageQuery) -> forge_base::Result<NativeOutput> {
        DesktopServices.game_language_info(runtime, query)
    }
    fn prebuild_interior_sun(
        &self,
        request: &InteriorSunRequest,
        cancel: &CancelToken,
    ) -> forge_base::Result<NativeOutput> {
        DesktopServices.prebuild_interior_sun(request, cancel)
    }
    fn prepare_renderer_passes(&self, request: &RendererPassRequest) -> forge_base::Result<NativeOutput> {
        DesktopServices.prepare_renderer_passes(request)
    }
    fn import_transferable_shaders(&self, request: &TransferableImport) -> forge_base::Result<NativeOutput> {
        DesktopServices.import_transferable_shaders(request)
    }
    fn compile_portable_shaders(&self, request: &PortableCompile) -> forge_base::Result<NativeOutput> {
        DesktopServices.compile_portable_shaders(request)
    }
    fn prepare_device_pipelines(
        &self,
        job: &DeviceJob,
        cancel: &CancelToken,
        report: Report<'_>,
    ) -> forge_base::Result<i64> {
        self.jobs.borrow_mut().push(job.clone());
        DesktopServices.prepare_device_pipelines(job, cancel, report)
    }
}

const SEPARATOR: &str = if cfg!(windows) { "\\\\" } else { "/" };

fn helper_plan(prewarm: &str, exit: i64) -> Value {
    helper_plan_writing(prewarm, exit, CACHE_FILENAME)
}

/// A helper that writes (and reports) `cache` in the cache directory.
fn helper_plan_writing(prewarm: &str, exit: i64, cache: &str) -> Value {
    let report = format!(
        "{{\"format\": \"{FORMAT}\", \"device_pipeline_prewarm\": \"{prewarm}\", \"total\": 500, \"prepared\": 500, \
         \"failed\": 0, \"game_booted\": false, \"gpu\": \"test device\", \
         \"cache_path\": \"{{flag:--cache-dir}}{SEPARATOR}{cache}\"}}"
    );
    json!({"exit": exit, "write": [
        {"flag": "--cache-dir", "join": cache, "content": "device cache"},
        {"flag": "--report", "content": report},
    ]})
}

struct Fixture {
    _temp: tempfile::TempDir,
    root: PathBuf,
    pack: PathBuf,
    cache: PathBuf,
    helper: PathBuf,
    title: Value,
    services: Recording,
}

impl Fixture {
    fn new() -> Self {
        let temp = tempfile::tempdir().unwrap();
        let root = forge_prepare::paths::resolve(temp.path()).unwrap();
        let pack = root.join("pack.o3ps");
        std::fs::write(&pack, b"fixture").unwrap();
        let helper = install_fake(&root, "helper", &helper_plan("complete", 0));
        let helper_record = common::record(&root, &helper.file_name().unwrap().to_string_lossy());
        let title = json!({"device_pipeline_preparation": {
            "format": FORMAT, "helper": helper_record,
            "manifest": artifact(&root, "manifest.json", b"fixture"),
            "dependencies": [artifact(&root, "dependency", b"fixture")]}});
        let cache = root.join("cache");
        Self { _temp: temp, root, pack, cache, helper, title, services: Recording::default() }
    }

    fn contract(&mut self) -> &mut Map<String, Value> {
        self.title["device_pipeline_preparation"].as_object_mut().unwrap()
    }

    fn prepare(&self) -> forge_base::Result<Map<String, Value>> {
        self.prepare_with(&CancelToken::new())
    }

    fn prepare_with(&self, cancel: &CancelToken) -> forge_base::Result<Map<String, Value>> {
        prepare_device_pipelines(
            &self.services,
            &self.root,
            &self.root.join("data"),
            self.title.as_object().unwrap(),
            Some(&self.pack),
            Some(&self.cache),
            &mut |_, _| {},
            cancel,
        )
        .map(Option::unwrap)
    }

    fn prepare_title(&self, namespace: &str) -> forge_base::Result<Map<String, Value>> {
        prepare_title_device_pipelines(
            namespace,
            &self.services,
            &self.root,
            &self.root.join("data"),
            self.title.as_object().unwrap(),
            Some(&self.pack),
            Some(&self.cache),
            &mut |_, _| {},
            &CancelToken::new(),
        )
        .map(Option::unwrap)
    }

    fn last_args(&self) -> Vec<String> {
        let all = runs(&self.helper);
        all.last().unwrap()["args"].as_array().unwrap().iter().map(|value| value.as_str().unwrap().to_owned()).collect()
    }
}

#[test]
fn off_is_inert() {
    let fixture = Fixture::new();
    let result = prepare_device_pipelines(
        &fixture.services,
        &fixture.root,
        &fixture.root,
        &Map::new(),
        None,
        None,
        &mut |_, _| {},
        &CancelToken::new(),
    );
    assert_eq!(result.unwrap(), None);
    assert!(runs(&fixture.helper).is_empty());
}

#[test]
fn device_is_checked_again_on_every_install() {
    let fixture = Fixture::new();
    for _ in 0..2 {
        let result = fixture.prepare().unwrap();
        assert_eq!(result["device_pipeline_prewarm"], "complete");
        assert_eq!(result["game_coverage_proven"], false);
        assert_eq!(result["pack_sha256"], forge_base::hashing::sha256_hex(b"fixture"));
        assert_eq!(result["cache_sha256"], forge_base::hashing::sha256_hex(b"device cache"));
        assert_eq!(result["manifest_sha256"], forge_base::hashing::sha256_hex(b"fixture"));
    }
    assert_eq!(runs(&fixture.helper).len(), 2);
    let args = fixture.last_args();
    assert_eq!(args[0], "--pack");
    assert_eq!(args[1], fixture.pack.to_string_lossy());
    assert_eq!(&args[2..4], ["--cache-dir".to_string(), fixture.cache.to_string_lossy().into_owned()]);
    assert_eq!(args[4], "--report");
    assert_eq!(args[6], "--cancel-file");
    assert_eq!(&args[8..10], ["--adapter", "0"]);
    assert_eq!(args[10], "--manifest");
    assert_eq!(args.len(), 12);
    // The stage (report, logs, cancel file) is removed afterwards.
    let receipts = fixture.root.join("data/shader-seeds/device-preparation");
    assert_eq!(std::fs::read_dir(receipts).unwrap().count(), 1);
}

#[test]
fn native_and_effect_manifests_share_one_device_job_and_cache() {
    let mut fixture = Fixture::new();
    let effects = artifact(&fixture.root, "effects.json", b"effects");
    let contract = fixture.contract();
    let native = contract.remove("manifest").unwrap();
    contract.insert("manifests".into(), json!([native, effects, native]));
    let result = fixture.prepare().unwrap();
    assert_eq!(result["device_pipeline_prewarm"], "complete");
    assert_eq!(runs(&fixture.helper).len(), 1);
    assert_eq!(fixture.last_args().iter().filter(|value| *value == "--manifest").count(), 2);
    assert_eq!(result["manifest_sha256s"].as_array().unwrap().len(), 2);
    assert_eq!(result["manifest_sha256"], Value::Null);
}

#[test]
fn optional_title_program_family_is_verified_routed_and_receipted() {
    let mut fixture = Fixture::new();
    fixture.title["title_program_pack"] = artifact(&fixture.root, "title.spvpack", b"programs");
    fixture.contract().insert("startup_programs".into(), json!(true));
    let result = fixture.prepare().unwrap();
    assert_eq!(result["title_program_pack_sha256"], forge_base::hashing::sha256_hex(b"programs"));
    let args = fixture.last_args();
    let at = args.iter().position(|a| a == "--title-program-pack").unwrap();
    assert_eq!(args[at + 1], fixture.root.join("title.spvpack").to_string_lossy());
    std::fs::write(fixture.root.join("title.spvpack"), b"corrupt").unwrap();
    assert!(fixture.prepare().is_err());
    assert_eq!(runs(&fixture.helper).len(), 1);
}

#[test]
fn startup_programs_join_the_same_job_with_a_longer_budget() {
    let mut fixture = Fixture::new();
    fixture.prepare().unwrap();
    assert!(!fixture.last_args().contains(&"--startup-programs".to_string()));
    assert_eq!(fixture.services.jobs.borrow()[0].timeout_seconds, 600.0);
    fixture.contract().insert("startup_programs".into(), json!(true));
    assert_eq!(fixture.prepare().unwrap()["device_pipeline_prewarm"], "complete");
    assert!(fixture.last_args().contains(&"--startup-programs".to_string()));
    assert_eq!(fixture.last_args().iter().filter(|value| *value == "--manifest").count(), 1);
    assert_eq!(fixture.services.jobs.borrow()[1].timeout_seconds, 1800.0);
    fixture.contract().insert("startup_programs".into(), json!("yes"));
    assert!(fixture.prepare().unwrap_err().message.contains("startup programs"));
    assert_eq!(runs(&fixture.helper).len(), 2);
}

#[test]
fn ambiguous_or_empty_manifests_do_not_run() {
    let mut fixture = Fixture::new();
    let manifest = fixture.contract()["manifest"].clone();
    fixture.contract().insert("manifests".into(), json!([manifest]));
    assert!(fixture.prepare().unwrap_err().message.contains("not both"));
    fixture.contract().remove("manifest");
    fixture.contract().insert("manifests".into(), json!([]));
    assert!(fixture.prepare().unwrap_err().message.contains("1 to 64"));
    assert!(runs(&fixture.helper).is_empty());
}

#[test]
fn missing_gpu_report_or_failed_job_is_not_success() {
    let fixture = Fixture::new();
    set_plan(&fixture.helper, &json!({"exit": 1, "stderr": "NRI pipeline preparation: no Vulkan device\n"}));
    let result = fixture.prepare().unwrap();
    assert_eq!(result["device_pipeline_prewarm"], "failed");
    assert!(result["error"].as_str().unwrap().starts_with("[Errno 2] No such file or directory: "));
    assert_eq!(result["diagnostics"], "NRI pipeline preparation: no Vulkan device\n");
    assert!(result.get("cache_sha256").is_none());
}

#[test]
fn partial_result_does_not_activate_a_success_receipt() {
    let fixture = Fixture::new();
    set_plan(&fixture.helper, &helper_plan("partial", 3));
    let result = fixture.prepare().unwrap();
    assert_eq!(result["device_pipeline_prewarm"], "failed");
    assert_eq!(result["error"], "Device pipeline preparation was incomplete");
    set_plan(&fixture.helper, &helper_plan("complete", 3));
    assert_eq!(fixture.prepare().unwrap()["device_pipeline_prewarm"], "failed");
}

#[test]
fn cancelled_job_is_cancelled_never_success() {
    let fixture = Fixture::new();
    set_plan(&fixture.helper, &helper_plan("cancelled", 4));
    let result = fixture.prepare().unwrap();
    assert_eq!(result["device_pipeline_prewarm"], "cancelled");
    assert!(result.get("cache_sha256").is_none());
    assert!(result.get("error").is_none());
}

#[test]
fn unverified_artifact_never_executes() {
    let mut fixture = Fixture::new();
    fixture.contract()["helper"]["sha256"] = json!("0".repeat(64));
    assert!(fixture.prepare().is_err());
    assert!(runs(&fixture.helper).is_empty());
}

#[test]
fn bad_adapter_or_missing_pack_never_executes() {
    let mut fixture = Fixture::new();
    for value in [json!(-1), json!(64), json!(true), json!("1"), json!(1.0), Value::Null] {
        fixture.contract().insert("adapter".into(), value);
        assert_eq!(fixture.prepare().unwrap_err().message, "Invalid device preparation adapter");
    }
    fixture.contract().insert("adapter".into(), json!(63));
    fixture.prepare().unwrap();
    assert!(fixture.last_args().ends_with(&[
        "--adapter".into(),
        "63".into(),
        "--manifest".into(),
        fixture.last_args()[11].clone()
    ]));
    fixture.contract().remove("adapter");
    std::fs::remove_file(&fixture.pack).unwrap();
    assert_eq!(
        fixture.prepare().unwrap_err().message,
        "Device pipeline preparation requires prepared portable shaders"
    );
}

#[test]
fn renderer_cache_directory_policy() {
    // The only test in this binary that changes the environment.
    let directory = tempfile::tempdir().unwrap();
    let saved: Vec<(&str, Option<OsString>)> =
        ["TRIAEVUM_RENDERER_CACHE_DIR", "APPDATA", "XDG_DATA_HOME"].map(|name| (name, std::env::var_os(name))).into();
    std::env::set_var("TRIAEVUM_RENDERER_CACHE_DIR", directory.path());
    assert_eq!(renderer_cache_directory().unwrap(), directory.path());
    std::env::set_var("TRIAEVUM_RENDERER_CACHE_DIR", "relative/cache");
    assert_eq!(renderer_cache_directory().unwrap_err().message, "TRIAEVUM_RENDERER_CACHE_DIR must be absolute");
    std::env::set_var("TRIAEVUM_RENDERER_CACHE_DIR", "");
    std::env::set_var(if cfg!(windows) { "APPDATA" } else { "XDG_DATA_HOME" }, directory.path());
    if cfg!(any(windows, target_os = "linux")) {
        assert_eq!(
            renderer_cache_directory().unwrap(),
            directory.path().join("oot3d_native_vulkan").join("shader_cache")
        );
    } else {
        // macOS/Android imports always pass their installation's cache path;
        // they must not fall back to the Linux XDG or Windows APPDATA roots.
        assert_eq!(
            renderer_cache_directory().unwrap_err().message,
            "This host must supply the renderer cache directory"
        );
    }
    for (name, value) in saved {
        match value {
            Some(value) => std::env::set_var(name, value),
            None => std::env::remove_var(name),
        }
    }
}

#[test]
fn subprocess_timeout_is_reaped() {
    let fixture = Fixture::new();
    set_plan(&fixture.helper, &json!({"sleep_ms": 60000}));
    let started = std::time::Instant::now();
    let error = run_device_helper(
        &[fixture.helper.clone().into()],
        &fixture.root,
        &fixture.root,
        &CancelToken::new(),
        &mut |_, _| {},
        0.1,
    )
    .unwrap_err();
    assert_eq!(error.message, "Device pipeline preparation exceeded its time budget");
    assert!(started.elapsed().as_secs() < 10);
}

#[test]
fn subprocess_progress_and_cooperative_cancel() {
    let fixture = Fixture::new();
    let progress = json!({"event": "pipeline_progress", "attempted": 1, "total": 2}).to_string();
    set_plan(
        &fixture.helper,
        &json!({"progress": [progress, "not json"], "wait_for": "--cancel-file", "sleep_ms": 300}),
    );
    let stage = fixture.root.join("stage");
    std::fs::create_dir(&stage).unwrap();
    let command: Vec<OsString> =
        vec![fixture.helper.clone().into(), "--cancel-file".into(), stage.join("cancel").into()];
    let cancel = CancelToken::new();
    cancel.cancel();
    let mut messages = Vec::new();
    let status = run_device_helper(
        &command,
        &fixture.root,
        &stage,
        &cancel,
        &mut |stage, message| messages.push(format!("{stage}: {message}")),
        600.0,
    )
    .unwrap();
    assert_eq!(status, 0);
    assert_eq!(messages, ["pipelines: Preparing GPU pipelines: 1/2"]);
    assert!(stage.join("cancel").is_file());
}

#[test]
fn existing_cache_is_copied_without_overwrite_or_unknown_files() {
    let fixture = Fixture::new();
    let source = fixture.root.join("previous-cache");
    std::fs::create_dir_all(source.join("spirv-v2")).unwrap();
    for name in [CACHE_FILENAME, "spirv-v2/ab_cd.spvc", "unknown.bin", "spirv-v2/invalid-name.spvc"] {
        std::fs::write(source.join(name), b"fixture, validated only by renderer").unwrap();
    }
    let mut messages = Vec::new();
    assert_eq!(
        adopt_existing_cache(&fixture.cache, Some(&source), &mut |_, message| messages.push(message.to_string())),
        2
    );
    assert_eq!(messages, ["Preserved 2 existing renderer cache entries (compatibility checked by renderer)."]);
    assert!(!fixture.cache.join("unknown.bin").exists());
    std::fs::write(fixture.cache.join(CACHE_FILENAME), b"new learned cache").unwrap();
    assert_eq!(adopt_existing_cache(&fixture.cache, Some(&source), &mut |_, _| {}), 0);
    assert_eq!(std::fs::read(fixture.cache.join(CACHE_FILENAME)).unwrap(), b"new learned cache");
    assert!(source.join("spirv-v2/ab_cd.spvc").is_file());
}

/// The Moonfall Curse runtime reads `nri_pipeline_cache.mm3d.bin`: the job of
/// a title with a pipeline cache namespace names it to the helper and is
/// complete only when the helper produced that title's file.
#[test]
fn a_title_namespace_prepares_the_cache_file_its_runtime_reads() {
    let mut fixture = Fixture::new();
    fixture.contract().insert("startup_programs".into(), json!(true));
    let own = title_cache_filename(CACHE_FILENAME, "mm3d").unwrap();
    assert_eq!(own, "nri_pipeline_cache.mm3d.bin");
    set_plan(&fixture.helper, &helper_plan_writing("complete", 0, &own));
    let result = fixture.prepare_title("mm3d").unwrap();
    assert_eq!(result["device_pipeline_prewarm"], "complete");
    assert_eq!(result["cache_sha256"], forge_base::hashing::sha256_hex(b"device cache"));
    assert!(fixture.cache.join(&own).is_file());
    assert!(!fixture.cache.join(CACHE_FILENAME).exists());
    let args = fixture.last_args();
    let at = args.iter().position(|a| a == "--pipeline-cache-namespace").unwrap();
    assert_eq!(args[at + 1], "mm3d");
    assert_eq!(fixture.services.jobs.borrow()[0].pipeline_cache_namespace, "mm3d");

    // Negative (the defect): a helper that writes the original file prepared
    // nothing this title's runtime opens. The receipt says failed, not complete.
    set_plan(&fixture.helper, &helper_plan("complete", 0));
    let result = fixture.prepare_title("mm3d").unwrap();
    assert_eq!(result["device_pipeline_prewarm"], "failed");
    assert_eq!(result["error"], "Preparation did not produce the requested device cache");

    // Negative: a namespace the renderer refuses never reaches the helper.
    let before = runs(&fixture.helper).len();
    assert!(fixture.prepare_title("../mm3d").unwrap_err().message.contains("pipeline cache namespace"));
    assert_eq!(runs(&fixture.helper).len(), before);
}

/// Echoes of Time: no namespace, the original command line and the original
/// file (the helper is not told about namespaces at all).
#[test]
fn no_namespace_keeps_the_original_job() {
    let fixture = Fixture::new();
    let result = fixture.prepare_title("").unwrap();
    assert_eq!(result["device_pipeline_prewarm"], "complete");
    assert!(fixture.cache.join("nri_pipeline_cache.bin").is_file());
    let args = fixture.last_args();
    assert!(!args.iter().any(|a| a == "--pipeline-cache-namespace"));
    assert_eq!(args.len(), 12);
    // A helper that writes a namespaced file did not prepare the original one.
    set_plan(&fixture.helper, &helper_plan_writing("complete", 0, "nri_pipeline_cache.mm3d.bin"));
    std::fs::remove_file(fixture.cache.join("nri_pipeline_cache.bin")).unwrap();
    assert_eq!(fixture.prepare_title("").unwrap()["device_pipeline_prewarm"], "failed");
}
