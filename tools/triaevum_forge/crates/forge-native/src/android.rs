// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Android [`NativeServices`]: not implemented yet.
//!
//! Android cannot extract and execute helper binaries from writable storage
//! (`FORGE_SPEC.md` 2.10, 4.4 point 6). The APK will export in-process C++
//! wrappers around the same native functions the desktop helpers run:
//!
//! * product/plugin preflight and language discovery return the JSON the
//!   desktop `--product-info`, `--verify-title-plugin` and
//!   `--game-language-info` modes print, as a [`NativeOutput`] with status 0
//!   (or a nonzero status and the error text on stderr);
//! * interior sun, renderer passes, shader import/compilation and device
//!   pipelines take the request fields of [`InteriorSunRequest`],
//!   [`RendererPassRequest`], [`TransferableImport`], [`PortableCompile`]
//!   and [`DeviceJob`] (the `arguments()` of each request are the exact
//!   parameter list) and write the same result manifests to the requested
//!   paths, so the shared validation and receipts apply unchanged.
//!
//! Requirements for the implementation: the pass compiler must be the
//! gameplay module's shaderc build (its fingerprint keys the SPIR-V cache);
//! device preparation runs on the game's GPU/device owner while gameplay is
//! not running; the cancel token maps to the native cooperative cancel flag;
//! no callback may run after a method returns.
//!
//! Until then every method reports [`ErrorKind::IncompatibleTarget`], so the
//! core can offer desktop-side data preparation with device steps pending.

use std::path::Path;

use forge_base::{CancelToken, ErrorKind, ForgeError, Result};

use crate::services::{
    DeviceJob, InteriorSunRequest, NativeOutput, NativeServices, PortableCompile, RendererPassRequest, Report,
    TransferableImport,
};

/// Placeholder for the in-process Android services.
#[derive(Clone, Copy, Debug, Default)]
pub struct AndroidServices;

fn unavailable(operation: &str) -> ForgeError {
    ForgeError::new(
        ErrorKind::IncompatibleTarget,
        format!("In-process native {operation} is not available in this Android build"),
    )
}

impl NativeServices for AndroidServices {
    fn product_info(&self, _runtime: &Path, _plugin: Option<&Path>) -> Result<NativeOutput> {
        Err(unavailable("product query"))
    }

    fn game_language_info(&self, _runtime: &Path, _romfs: &Path) -> Result<NativeOutput> {
        Err(unavailable("language discovery"))
    }

    fn prebuild_interior_sun(&self, _request: &InteriorSunRequest, _cancel: &CancelToken) -> Result<NativeOutput> {
        Err(unavailable("interior-sun preparation"))
    }

    fn prepare_renderer_passes(&self, _request: &RendererPassRequest) -> Result<NativeOutput> {
        Err(unavailable("renderer pass preparation"))
    }

    fn import_transferable_shaders(&self, _request: &TransferableImport) -> Result<NativeOutput> {
        Err(unavailable("shader cache import"))
    }

    fn compile_portable_shaders(&self, _request: &PortableCompile) -> Result<NativeOutput> {
        Err(unavailable("portable shader compilation"))
    }

    fn prepare_device_pipelines(&self, _job: &DeviceJob, _cancel: &CancelToken, _report: Report<'_>) -> Result<i64> {
        Err(unavailable("device pipeline preparation"))
    }
}
