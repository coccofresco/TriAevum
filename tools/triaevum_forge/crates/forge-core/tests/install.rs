// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The public import against a synthetic package and a fake native runtime:
//! stage order, written files, reuse, configuration migration, rollback.

use std::cell::{Cell, RefCell};
use std::path::{Path, PathBuf};

use forge_base::fsutil::load_json_object;
use forge_base::hashing::sha256_hex;
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_core::install::{install_private_title, InstallRequest, InstallSource};
use forge_core::publish::standard_topscreen;
use forge_native::{
    DeviceJob, InteriorSunRequest, NativeOutput, NativeServices, PortableCompile, RendererPassRequest, Report,
    TransferableImport,
};
use serde_json::{json, Value};

const RECIPE: &str = "fixture-eur";
const TARGET: &str = "x86_64-pc-windows-msvc";
const NONE: u32 = 0xFFFF_FFFF;

/// A valid ExHeader (three one-page segments at 0x100000), 2048 bytes.
fn exheader() -> Vec<u8> {
    let mut bytes = vec![0u8; 0x800];
    let words: [(usize, u32); 11] = [
        (0x10, 0x0010_0000),
        (0x14, 1),
        (0x18, 0x0000_0800),
        (0x1C, 0x0000_4000),
        (0x20, 0x0010_1000),
        (0x24, 1),
        (0x28, 0x0000_0400),
        (0x30, 0x0010_2000),
        (0x34, 1),
        (0x38, 0x0000_0200),
        (0x3C, 0x0000_1234),
    ];
    for (offset, value) in words {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    bytes[0x20F] = 0x30;
    bytes
}

fn code() -> Vec<u8> {
    (0..0x3000u32).map(|index| (index * 7 % 251) as u8).collect()
}

/// A raw IVFC RomFS whose Level-3 image holds only an empty root.
fn romfs() -> Vec<u8> {
    let mut bytes = b"IVFC".to_vec();
    bytes.resize(0x1000, 0);
    for word in [0x28u32, 0, 0, 0x28, 24, 0, 0, 0x40, 0, 0x40, 0, NONE, NONE, NONE, NONE, 0] {
        bytes.extend(word.to_le_bytes());
    }
    bytes
}

fn identity(bytes: &[u8]) -> Value {
    json!({"bytes": bytes.len(), "sha256": sha256_hex(bytes)})
}

fn write(path: &Path, bytes: &[u8]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, bytes).unwrap();
}

struct Fixture {
    _directory: tempfile::TempDir,
    package: PathBuf,
    extracted: PathBuf,
}

impl Fixture {
    fn new() -> Self {
        let directory = tempfile::tempdir().unwrap();
        let root = forge_prepare::paths::resolve(directory.path()).unwrap();
        let package = root.join("package");
        let extracted = root.join("extracted");
        let (code, exheader, romfs) = (code(), exheader(), romfs());
        write(&extracted.join("code.bin"), &code);
        write(&extracted.join("exheader.bin"), &exheader);
        write(&extracted.join("romfs.bin"), &romfs);
        let inputs = json!({"code": identity(&code), "exheader": identity(&exheader), "romfs": identity(&romfs)});
        let recipe = json!({"id": RECIPE, "title": "Fixture", "region": "EUR", "inputs": inputs});
        write(
            &package.join("recipes/oot3d.json"),
            json!({"format": "triaevum_supported_revisions_v1", "recipes": [recipe]}).to_string().as_bytes(),
        );
        let artifact = |relative: &str, bytes: &[u8]| {
            write(&package.join(relative), bytes);
            json!({"path": relative, "bytes": bytes.len(), "sha256": sha256_hex(bytes)})
        };
        let catalog = json!({
            "format": "triaevum_precompiled_titles_v1",
            "install_model": "precompiled_title_rom_import_v1",
            "target": TARGET,
            "runtime": artifact("runtime/TriAevum.exe", b"fake runtime"),
            "native_module": artifact("forge/oot3d_game_module.dll", b"MZ fake native module"),
            "titles": [{
                "recipe": RECIPE,
                "inputs": inputs,
                "abi_version": 2,
                "target": TARGET,
                "translator_identity_sha256": "7".repeat(64),
                "plugin": artifact(&format!("titles/{RECIPE}/triaevum_title_aot.dll"), b"MZ fake title plugin"),
            }],
        });
        write(&package.join("recipes/precompiled-titles.json"), catalog.to_string().as_bytes());
        std::fs::create_dir_all(package.join("resources")).unwrap();
        Self { _directory: directory, package, extracted }
    }

    fn data(&self) -> PathBuf {
        self.package.join("data")
    }

    fn install(&self, services: &FakeRuntime, stages: &mut Vec<(String, String)>) -> Result<Value> {
        let request = InstallRequest {
            source: InstallSource::ExtractedDirectory(self.extracted.clone()),
            data_root: None,
            package_root: self.package.clone(),
        };
        let mut report = |stage: &str, message: &str| stages.push((stage.to_string(), message.to_string()));
        install_private_title(request, services, &mut report, &CancelToken::new())
    }
}

/// The runtime as the import sees it: product, plugin preflight, languages
/// and the interior-sun job. The fixture title asks for no shader work.
#[derive(Default)]
struct FakeRuntime {
    reject_plugin: Cell<bool>,
    plugin_queries: RefCell<Vec<PathBuf>>,
}

impl NativeServices for FakeRuntime {
    fn product_info(&self, _runtime: &Path, plugin: Option<&Path>) -> Result<NativeOutput> {
        let mut product = json!({
            "format": "triaevum_product_info_v1", "runtime": "oot3d_native_game", "whole_aot_plugin_abi": 2,
            "capabilities": {"nri": true, "f1": true, "topscreen": true},
            "default_config": {"Graphics": {"Preset": "Enhanced", "FrameRate": {"Mode": "Interpolated2x"}}},
        });
        if let Some(plugin) = plugin {
            self.plugin_queries.borrow_mut().push(plugin.to_path_buf());
            product["private_title_loaded"] = json!(!self.reject_plugin.get());
        }
        Ok(NativeOutput { status: 0, stdout: format!("startup noise\n{product}\n"), stderr: String::new() })
    }

    fn game_language_info(&self, _runtime: &Path, romfs: &Path) -> Result<NativeOutput> {
        assert!(romfs.is_file());
        let document = json!({"format": "triaevum_game_language_v1", "selected": "en",
            "available": [{"code": "en", "label": "English", "system_id": 1}]});
        Ok(NativeOutput { status: 0, stdout: document.to_string(), stderr: String::new() })
    }

    fn prebuild_interior_sun(&self, request: &InteriorSunRequest, _: &CancelToken) -> Result<NativeOutput> {
        assert!(request.runtime.ends_with("runtime/TriAevum.exe"));
        let relative = "shadow-shells/0000000000000001.bin";
        let mut shell = b"RSSH".to_vec();
        shell.extend(3u32.to_le_bytes());
        shell.extend(1u64.to_le_bytes());
        for value in [64u32, 512, 0, 0, 1, 0] {
            shell.extend(value.to_le_bytes());
        }
        shell.extend(0u64.to_le_bytes());
        write(&request.cache_directory.join(relative), &shell);
        let result = json!({"format": "triaevum_interior_sun_v1", "criterion_version": 3, "game_booted": false,
            "cache_directory": request.cache_directory.to_string_lossy(), "rooms": 1, "fallback": 0,
            "entries": [{"status": "built", "fingerprint": "0000000000000001", "interior": true, "path": relative}]});
        std::fs::write(&request.manifest, result.to_string()).unwrap();
        Ok(NativeOutput::default())
    }

    fn prepare_renderer_passes(&self, _: &RendererPassRequest) -> Result<NativeOutput> {
        Err(ForgeError::internal("unexpected renderer pass preparation"))
    }

    fn import_transferable_shaders(&self, _: &TransferableImport) -> Result<NativeOutput> {
        Err(ForgeError::internal("unexpected shader import"))
    }

    fn compile_portable_shaders(&self, _: &PortableCompile) -> Result<NativeOutput> {
        Err(ForgeError::internal("unexpected shader compilation"))
    }

    fn prepare_device_pipelines(&self, _: &DeviceJob, _: &CancelToken, _: Report<'_>) -> Result<i64> {
        Err(ForgeError::internal("unexpected device job"))
    }
}

fn hidden_entries(directory: &Path) -> Vec<String> {
    std::fs::read_dir(directory)
        .unwrap()
        .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
        .filter(|name| name.starts_with(".rom-import-"))
        .collect()
}

#[test]
fn installs_the_precompiled_title_and_publishes_the_runtime() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime::default();
    let mut stages = Vec::new();
    let result = fixture.install(&runtime, &mut stages).unwrap();

    let names: Vec<&str> = stages.iter().map(|(stage, _)| stage.as_str()).collect();
    assert_eq!(&names[..5], ["preflight", "extract", "verify", "prepare", "activate"]);
    assert_eq!(stages.last().unwrap(), &("ready".to_string(), "The title is ready to play.".to_string()));
    assert!(names.contains(&"interior-sun"));
    assert_eq!(result["status"], "ready");
    assert_eq!(result["recipe"], RECIPE);
    assert_eq!(result["adaptation"], Value::Null);
    assert_eq!(result["prepared"]["status"], "prepared");
    assert_eq!(result["build"]["install_model"], "precompiled_title_rom_import_v1");
    assert_eq!(result["build"]["objects_compiled"], 0);
    assert_eq!(result["build"]["device_pipeline_preparation"], Value::Null);
    assert_eq!(result["build"]["interior_sun"]["receipt_reused"], false);
    assert!(result["install_wall_seconds"].as_f64().unwrap() >= 0.0);

    let data = fixture.data();
    assert!(hidden_entries(&data).is_empty(), "staging removed");
    let plugin_sha = sha256_hex(b"MZ fake title plugin");
    let generation = fixture.package.join("private-plugins").join(&plugin_sha).join("triaevum_title_aot.dll");
    assert_eq!(std::fs::read(&generation).unwrap(), b"MZ fake title plugin");
    assert_eq!(*runtime.plugin_queries.borrow(), std::slice::from_ref(&generation));
    // The packaged runtime was placed in the installation root.
    assert!(fixture.package.join("TriAevum.exe").is_file());
    assert!(!fixture.package.join("runtime/TriAevum.exe").exists());

    let profile = load_json_object(&data.join("TriAevum.launch.json")).unwrap();
    assert_eq!(profile["format"], "oot3d_native_game_launch_profile_v1");
    let arguments: Vec<&str> = profile["arguments"].as_array().unwrap().iter().map(|a| a.as_str().unwrap()).collect();
    assert_eq!(
        arguments[0..2],
        ["--title-plugin", &format!("${{runtime_dir}}/private-plugins/{plugin_sha}/triaevum_title_aot.dll")]
    );
    assert_eq!(arguments[4..6], ["--resource-root", "${runtime_dir}/resources"]);
    // A plain package is its own activation: its data is a runtime_dir path.
    assert_eq!(arguments[10..12], ["--config", "${runtime_dir}/data/config/TriAevum.json"]);
    assert_eq!(arguments[24..26], ["--height", "720"]);
    let tail: Vec<&str> = arguments[26..].iter().step_by(2).copied().collect();
    assert_eq!(tail, ["--water-texture-catalog", "--scene-light-catalog", "--renderer-cache-directory"]);
    assert!(profile["path_scopes"].as_object().unwrap().values().all(|scope| scope == "relative"));

    let state =
        load_json_object(&PathBuf::from(result["prepared"]["directory"].as_str().unwrap()).join("forge-state.json"))
            .unwrap();
    let receipt = &state["runtime"];
    assert_eq!(receipt["backend"], "generated_cpp_whole_aot_plugin_v2");
    assert_eq!(receipt["target"], TARGET);
    assert_eq!(receipt["runtime_sha256"], sha256_hex(b"fake runtime"));
    assert_eq!(receipt["plugin_sha256"], plugin_sha);
    assert_eq!(receipt["launch_profile"], "../../../TriAevum.launch.json");
    assert_eq!(
        receipt["launch_profile_sha256"],
        sha256_hex(&std::fs::read(data.join("TriAevum.launch.json")).unwrap())
    );
    assert_eq!(receipt["renderer_cache"]["path"], "../../../cache/renderer");
    assert_eq!(state["module"]["status"], "ready");

    let active = load_json_object(&data.join("active-title.json")).unwrap();
    assert_eq!(active["module_sha256"], state["module"]["sha256"]);
    assert_eq!(
        Value::Object(load_json_object(&data.join("config/topscreen_ui.json")).unwrap()),
        Value::Object(standard_topscreen().unwrap())
    );
    assert_eq!(load_json_object(&data.join("config/TriAevum.json")).unwrap()["Graphics"]["Preset"], "Enhanced");
    assert_eq!(load_json_object(&data.join("config/game_language.json")).unwrap()["selected"], "en");
    assert!(data.join("savedata").is_dir());
    assert!(!data.join(".forge-activation/journal.json").exists());
}

#[test]
fn a_second_install_reuses_the_title_and_keeps_user_state() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime::default();
    let first = fixture.install(&runtime, &mut Vec::new()).unwrap();
    let state_path = PathBuf::from(first["prepared"]["directory"].as_str().unwrap()).join("forge-state.json");
    let mut state = load_json_object(&state_path).unwrap();
    state.insert("translation".into(), json!({"status": "kept"}));
    forge_base::fsutil::atomic_write_json(&state_path, &Value::Object(state)).unwrap();
    let topscreen = fixture.data().join("config/topscreen_ui.json");
    let mut user = standard_topscreen().unwrap();
    user.insert("hud_scale".into(), json!(1.0));
    user.insert("hud_margin_x".into(), json!(40));
    user.insert("camera_fov_percent".into(), json!(120));
    forge_base::fsutil::atomic_write_json(&topscreen, &Value::Object(user)).unwrap();

    let second = fixture.install(&runtime, &mut Vec::new()).unwrap();
    assert_eq!(second["prepared"]["status"], "reused");
    assert_eq!(second["build"]["interior_sun"]["receipt_reused"], true);
    assert_eq!(second["build"]["package"]["status"], "reused");
    let state = load_json_object(&state_path).unwrap();
    assert_eq!(state["translation"]["status"], "kept");
    let migrated = load_json_object(&topscreen).unwrap();
    assert_eq!(migrated["hud_scale"], 1.0);
    assert_eq!(migrated["hud_margin_x"], 4);
    assert_eq!(migrated["camera_fov_percent"], 120);
}

#[test]
fn a_rejected_plugin_rolls_the_publication_back() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime { reject_plugin: Cell::new(true), ..FakeRuntime::default() };
    let error = fixture.install(&runtime, &mut Vec::new()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::NativePreflight);
    assert_eq!(error.stage, Some("activate"));
    let data = fixture.data();
    for created in ["active-title.json", "TriAevum.launch.json", "config/TriAevum.json", "config/topscreen_ui.json"] {
        assert!(!data.join(created).exists(), "{created} rolled back");
    }
    let titles = data.join("titles").join(RECIPE);
    let title = std::fs::read_dir(&titles).unwrap().next().unwrap().unwrap().path();
    assert_eq!(load_json_object(&title.join("forge-state.json")).unwrap()["module"]["status"], "not_built");
    assert!(!data.join(".forge-activation/journal.json").exists());
    assert!(hidden_entries(&data).is_empty());
    // Languages are written only after a committed activation.
    assert!(!data.join("config/game_language.json").exists());
}

#[test]
fn an_unknown_revision_stops_at_verification() {
    let fixture = Fixture::new();
    let mut code = code();
    code[0] ^= 1;
    std::fs::write(fixture.extracted.join("code.bin"), code).unwrap();
    let error = fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedRevision);
    assert_eq!(error.stage, Some("verify"));
    assert!(error.message.starts_with("the decrypted ROM does not match any supported game revision"));
    assert!(hidden_entries(&fixture.data()).is_empty());
    assert!(!fixture.data().join("sources").exists());
}
