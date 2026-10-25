// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The native services interface (`FORGE_SPEC.md` 4.1, 4.2): every call into
//! the runtime or a renderer helper goes through [`NativeServices`]. The
//! desktop implementation runs the catalog-verified executables as helper
//! processes exactly as the Python Forge did ([`crate::desktop`]); Android
//! will call the same C++ functions in process ([`crate::android`]).
//!
//! The contract is the helper's observable output: an exit status with its
//! text streams, and result files written to the requested paths. Result
//! validation, receipts and messages stay in the platform-neutral callers
//! ([`crate::product`], [`crate::language`], [`crate::shader`],
//! [`crate::device`]), so both implementations are judged identically.

use std::ffi::OsString;
use std::path::PathBuf;

use forge_base::{CancelToken, Result};

/// Progress callback `(stage, message)`, as Python's `report`.
pub type Report<'a> = &'a mut dyn FnMut(&str, &str);

/// What a native operation printed, decoded as Python's text mode did
/// ([`crate::pyerr::decode_text`]), with Python's `returncode`.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NativeOutput {
    pub status: i64,
    pub stdout: String,
    pub stderr: String,
}

/// `--game-language-info ROMFS [--romfs-offset N --romfs-size N]`: the
/// RomFS is the whole file, or the `(offset, size)` extent of a cartridge
/// image read in place (ROM recognition before extraction).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LanguageQuery {
    pub romfs: PathBuf,
    pub extent: Option<(u64, u64)>,
}

impl LanguageQuery {
    pub fn romfs(path: impl Into<PathBuf>) -> Self {
        Self { romfs: path.into(), extent: None }
    }

    pub fn arguments(&self) -> Vec<OsString> {
        let mut arguments: Vec<OsString> = vec!["--game-language-info".into(), self.romfs.clone().into()];
        if let Some((offset, size)) = self.extent {
            arguments.extend([
                "--romfs-offset".into(),
                offset.to_string().into(),
                "--romfs-size".into(),
                size.to_string().into(),
            ]);
        }
        arguments
    }
}

/// `--prebuild-interior-sun`: run by the verified runtime with the package
/// root as working directory. The result manifest is written to `manifest`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct InteriorSunRequest {
    pub runtime: PathBuf,
    pub root: PathBuf,
    pub romfs: PathBuf,
    pub cache_directory: PathBuf,
    pub manifest: PathBuf,
    pub mask_table: Option<PathBuf>,
}

impl InteriorSunRequest {
    pub fn arguments(&self) -> Vec<OsString> {
        let mut arguments: Vec<OsString> = vec![
            "--prebuild-interior-sun".into(),
            "--romfs".into(),
            self.romfs.clone().into(),
            "--renderer-cache-directory".into(),
            self.cache_directory.clone().into(),
            "--manifest".into(),
            self.manifest.clone().into(),
        ];
        if let Some(table) = &self.mask_table {
            arguments.extend(["--mask-table".into(), table.clone().into()]);
        }
        arguments
    }
}

/// Renderer pass shaders: the verified pass compiler checks and fills the
/// SPIR-V cache and writes a `triaevum_renderer_shader_preparation_v1`
/// result to `manifest`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RendererPassRequest {
    pub compiler: PathBuf,
    pub root: PathBuf,
    pub cache_directory: PathBuf,
    pub manifest: PathBuf,
}

impl RendererPassRequest {
    pub fn arguments(&self) -> Vec<OsString> {
        vec![
            "--prepare-renderer-cache".into(),
            self.cache_directory.clone().into(),
            "--manifest".into(),
            self.manifest.clone().into(),
        ]
    }
}

/// Citra/Azahar transferable cache import into an inventory (`output`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TransferableImport {
    pub importer: PathBuf,
    pub root: PathBuf,
    pub dialect: String,
    pub output: PathBuf,
    pub inputs: Vec<PathBuf>,
}

impl TransferableImport {
    pub fn arguments(&self) -> Vec<OsString> {
        let mut arguments: Vec<OsString> =
            vec!["--dialect".into(), self.dialect.clone().into(), "--output".into(), self.output.clone().into()];
        for input in &self.inputs {
            arguments.extend(["--input".into(), input.clone().into()]);
        }
        arguments
    }
}

/// Portable shader compilation of inventories into an O3PS `pack`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PortableCompile {
    pub compiler: PathBuf,
    pub root: PathBuf,
    pub pack: PathBuf,
    pub manifest: PathBuf,
    pub inventories: Vec<PathBuf>,
}

impl PortableCompile {
    pub fn arguments(&self) -> Vec<OsString> {
        let mut arguments: Vec<OsString> =
            vec!["--pack".into(), self.pack.clone().into(), "--manifest".into(), self.manifest.clone().into()];
        for inventory in &self.inventories {
            arguments.extend(["--inventory".into(), inventory.clone().into()]);
        }
        arguments
    }
}

/// The headless NRI device pipeline job. `stage` is the temporary directory
/// that holds the report, the cancel file and the helper's diagnostics.
#[derive(Clone, Debug, PartialEq)]
pub struct DeviceJob {
    pub helper: PathBuf,
    pub root: PathBuf,
    pub stage: PathBuf,
    pub pack: PathBuf,
    pub cache_directory: PathBuf,
    pub adapter: u32,
    pub manifests: Vec<PathBuf>,
    pub startup_programs: bool,
    pub timeout_seconds: f64,
}

impl DeviceJob {
    pub fn report_path(&self) -> PathBuf {
        self.stage.join("result.json")
    }

    pub fn cancel_path(&self) -> PathBuf {
        self.stage.join("cancel")
    }

    /// `--pack P --cache-dir C --report R --cancel-file F --adapter N
    /// (--manifest M)... [--startup-programs]`.
    pub fn arguments(&self) -> Vec<OsString> {
        let mut arguments: Vec<OsString> = vec![
            "--pack".into(),
            self.pack.clone().into(),
            "--cache-dir".into(),
            self.cache_directory.clone().into(),
            "--report".into(),
            self.report_path().into(),
            "--cancel-file".into(),
            self.cancel_path().into(),
            "--adapter".into(),
            self.adapter.to_string().into(),
        ];
        for manifest in &self.manifests {
            arguments.extend(["--manifest".into(), manifest.clone().into()]);
        }
        if self.startup_programs {
            arguments.push("--startup-programs".into());
        }
        arguments
    }
}

/// Native work Forge requests. Each method performs one operation without
/// booting a title; `Err` is a failure to run it at all (start failure,
/// timeout, cancellation) carrying Python's exception text, while a native
/// failure is an [`NativeOutput`] with a nonzero status.
pub trait NativeServices {
    /// `--product-info`, or `--verify-title-plugin PLUGIN`, of `runtime`
    /// (15 s budget).
    fn product_info(&self, runtime: &std::path::Path, plugin: Option<&std::path::Path>) -> Result<NativeOutput>;

    /// `--game-language-info` (30 s budget).
    fn game_language_info(&self, runtime: &std::path::Path, query: &LanguageQuery) -> Result<NativeOutput>;

    /// The interior-sun prebuild (3600 s budget).
    fn prebuild_interior_sun(&self, request: &InteriorSunRequest, cancel: &CancelToken) -> Result<NativeOutput>;

    /// Renderer pass shader preparation (600 s budget).
    fn prepare_renderer_passes(&self, request: &RendererPassRequest) -> Result<NativeOutput>;

    /// Transferable shader cache import (600 s budget).
    fn import_transferable_shaders(&self, request: &TransferableImport) -> Result<NativeOutput>;

    /// Portable shader compilation (600 s budget).
    fn compile_portable_shaders(&self, request: &PortableCompile) -> Result<NativeOutput>;

    /// The device pipeline job; returns the helper's exit status. It writes
    /// its report to [`DeviceJob::report_path`] and stops when `cancel` is
    /// set (cooperatively, then forcibly after a grace period).
    fn prepare_device_pipelines(&self, job: &DeviceJob, cancel: &CancelToken, report: Report<'_>) -> Result<i64>;
}
