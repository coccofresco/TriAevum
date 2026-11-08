// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! A synthetic package, extracted inputs and a fake native runtime, shared
//! by the installation and installation-state tests.

#![allow(dead_code)]

use std::cell::{Cell, RefCell};
use std::path::{Path, PathBuf};

use forge_base::hashing::sha256_hex;
use forge_base::{CancelToken, ForgeError, Result};
use forge_catalog::{host_platform, ReleasePlatform};
use forge_core::install::{install_private_title, AndroidTarget, InstallRequest, InstallSource, InstallTarget};
use forge_core::layout::{installation_state, HostOs};
use forge_native::{
    DeviceJob, InteriorSunRequest, LanguageQuery, NativeOutput, NativeServices, PortableCompile, RendererPassRequest,
    Report, TransferableImport,
};
use serde_json::{json, Value};

pub const RECIPE: &str = "fixture-eur";
pub const ANDROID: &str = "aarch64-linux-android";
const NONE: u32 = 0xFFFF_FFFF;

/// The desktop package targets the host, whose runtime Forge installs.
pub fn host() -> ReleasePlatform {
    host_platform().unwrap()
}

/// A valid ExHeader (three one-page segments at 0x100000), 2048 bytes.
pub fn exheader() -> Vec<u8> {
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

pub fn code() -> Vec<u8> {
    (0..0x3000u32).map(|index| (index * 7 % 251) as u8).collect()
}

/// A raw IVFC RomFS whose Level-3 image holds only an empty root.
pub fn romfs() -> Vec<u8> {
    let mut bytes = b"IVFC".to_vec();
    bytes.resize(0x1000, 0);
    for word in [0x28u32, 0, 0, 0x28, 24, 0, 0, 0x40, 0, 0x40, 0, NONE, NONE, NONE, NONE, 0] {
        bytes.extend(word.to_le_bytes());
    }
    bytes
}

pub fn identity(bytes: &[u8]) -> Value {
    json!({"bytes": bytes.len(), "sha256": sha256_hex(bytes)})
}

pub fn write(path: &Path, bytes: &[u8]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, bytes).unwrap();
}

pub struct Fixture {
    pub _directory: tempfile::TempDir,
    pub package: PathBuf,
    pub extracted: PathBuf,
    /// The Android application's native library and profile folders.
    pub android: Option<(PathBuf, PathBuf)>,
}

impl Fixture {
    pub fn new() -> Self {
        Self::build(false)
    }

    /// An APK package: the libraries are in the native library folder.
    pub fn android() -> Self {
        Self::build(true)
    }

    pub fn build(android: bool) -> Self {
        Self::build_with(android, None)
    }

    /// A package for a ROM whose ExHeader names `program_id` (the fixture's
    /// own ExHeader leaves it zero).
    pub fn build_with(android: bool, program_id: Option<u64>) -> Self {
        let directory = tempfile::tempdir().unwrap();
        let root = forge_prepare::paths::resolve(directory.path()).unwrap();
        let package = root.join("package");
        let extracted = root.join("extracted");
        let native = root.join("native-libraries");
        let (code, mut exheader, romfs) = (code(), exheader(), romfs());
        if let Some(id) = program_id {
            exheader[0x200..0x208].copy_from_slice(&id.to_le_bytes());
        }
        write(&extracted.join("code.bin"), &code);
        write(&extracted.join("exheader.bin"), &exheader);
        write(&extracted.join("romfs.bin"), &romfs);
        let inputs = json!({"code": identity(&code), "exheader": identity(&exheader), "romfs": identity(&romfs)});
        let recipe = json!({"id": RECIPE, "title": "Fixture", "region": "EUR", "inputs": inputs});
        write(
            &package.join(forge_catalog::title::ACTIVE.packaged_recipes),
            json!({"format": "triaevum_supported_revisions_v1", "recipes": [recipe]}).to_string().as_bytes(),
        );
        let artifact = |relative: &str, bytes: &[u8]| {
            match relative.strip_prefix("lib/arm64-v8a/") {
                Some(name) => write(&native.join(name), bytes),
                None => write(&package.join(relative), bytes),
            }
            json!({"path": relative, "bytes": bytes.len(), "sha256": sha256_hex(bytes)})
        };
        let catalog = if android {
            json!({
                "format": "triaevum_precompiled_titles_v1",
                "install_model": "precompiled_title_rom_import_v1",
                "target": ANDROID,
                "runtime": artifact("lib/arm64-v8a/libTriAevum.so", b"fake runtime"),
                "titles": [{
                    "recipe": RECIPE,
                    "inputs": inputs,
                    "abi_version": 2,
                    "target": ANDROID,
                    "translator_identity_sha256": "7".repeat(64),
                    "plugin": artifact("lib/arm64-v8a/libtriaevum_title_aot.so", b"ELF fake title plugin"),
                }],
            })
        } else {
            json!({
            "format": "triaevum_precompiled_titles_v1",
            "install_model": "precompiled_title_rom_import_v1",
            "target": host().target,
            "runtime": artifact(host().runtime, b"fake runtime"),
            "native_module": artifact(host().native_module, b"MZ fake native module"),
            "titles": [{
                "recipe": RECIPE,
                "inputs": inputs,
                "abi_version": 2,
                "target": host().target,
                "translator_identity_sha256": "7".repeat(64),
                "plugin": artifact(&format!("titles/{RECIPE}/{}", host().title_module), b"MZ fake title plugin"),
            }],
            })
        };
        write(&package.join("recipes/precompiled-titles.json"), catalog.to_string().as_bytes());
        std::fs::create_dir_all(package.join("resources")).unwrap();
        let android = android.then(|| (native, root.join("external-files")));
        Self { _directory: directory, package, extracted, android }
    }

    /// Windows keeps the launch profile in data/, Linux beside the runtime.
    pub fn launch_profile(&self) -> PathBuf {
        installation_state(&self.package, HostOs::current()).join("TriAevum.launch.json")
    }

    pub fn data(&self) -> PathBuf {
        match &self.android {
            Some((_, profile)) => profile.join("data"),
            None => self.package.join("data"),
        }
    }

    pub fn install(&self, services: &FakeRuntime, stages: &mut Vec<(String, String)>) -> Result<Value> {
        self.install_choosing(services, stages, None)
    }

    pub fn install_choosing(
        &self,
        services: &FakeRuntime,
        stages: &mut Vec<(String, String)>,
        game_language: Option<&str>,
    ) -> Result<Value> {
        let request = InstallRequest {
            source: InstallSource::ExtractedDirectory(self.extracted.clone()),
            data_root: None,
            package_root: self.package.clone(),
            target: match &self.android {
                Some((native, profile)) => InstallTarget::Android(AndroidTarget {
                    native_library_dir: native.clone(),
                    profile_root: profile.clone(),
                }),
                None => InstallTarget::Package,
            },
            game_language: game_language.map(str::to_string),
        };
        let mut report = |stage: &str, message: &str| stages.push((stage.to_string(), message.to_string()));
        install_private_title(request, services, &mut report, &CancelToken::new())
    }
}

/// The runtime as the import sees it: product, plugin preflight, languages
/// and the interior-sun job. The fixture title asks for no shader work.
#[derive(Default)]
pub struct FakeRuntime {
    pub reject_plugin: Cell<bool>,
    pub plugin_queries: RefCell<Vec<PathBuf>>,
    /// Advertises `managed_data_paths` (UX plan WP-05C).
    pub managed_data_paths: bool,
}

impl NativeServices for FakeRuntime {
    fn product_info(&self, _runtime: &Path, plugin: Option<&Path>) -> Result<NativeOutput> {
        let mut product = json!({
            "format": "triaevum_product_info_v1", "runtime": "oot3d_native_game", "whole_aot_plugin_abi": 2, "titles": ["oot3d", "mm3d"],
            "capabilities": {"nri": true, "f1": true, "topscreen": true},
            "default_config": {"Graphics": {"Preset": "Enhanced", "FrameRate": {"Mode": "Interpolated2x"}}},
        });
        if self.managed_data_paths {
            product["capabilities"]["managed_data_paths"] = json!(true);
        }
        if let Some(plugin) = plugin {
            self.plugin_queries.borrow_mut().push(plugin.to_path_buf());
            product["private_title_loaded"] = json!(!self.reject_plugin.get());
        }
        Ok(NativeOutput { status: 0, stdout: format!("startup noise\n{product}\n"), stderr: String::new() })
    }

    fn game_language_info(&self, _runtime: &Path, query: &LanguageQuery) -> Result<NativeOutput> {
        assert!(query.romfs.is_file());
        let document = json!({"format": "triaevum_game_language_v1", "selected": "en",
            "available": [{"code": "en", "label": "English", "system_id": 1},
                          {"code": "it", "label": "Italiano", "system_id": 4}]});
        Ok(NativeOutput { status: 0, stdout: document.to_string(), stderr: String::new() })
    }

    fn prebuild_interior_sun(&self, request: &InteriorSunRequest, _: &CancelToken) -> Result<NativeOutput> {
        // The packaged runtime, or the placed one when a repair rebuilds the cache.
        let runtime = &request.runtime;
        assert!(
            runtime.ends_with(host().runtime)
                || runtime.ends_with(host().installed_runtime)
                || runtime.ends_with("libTriAevum.so")
        );
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
