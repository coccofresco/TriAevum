// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Native preparation services: the runtime product query, game language
//! discovery, interior-sun, renderer pass shaders and device pipelines.
//!
//! Port of the Python Forge's product_contract.py, game_language.py,
//! native_process.py, shader_preparation.py, device_pipeline_preparation.py,
//! shader_corpus_layout.py, installed_runtime.py and worker_job.py.
//!
//! Every call into native code goes through [`NativeServices`]:
//! [`DesktopServices`] runs the verified helpers as Python did,
//! [`AndroidServices`] calls the same helpers in the runtime library. The
//! platform-neutral modules validate native results and write receipts with
//! the Python Forge's bytes and messages.

pub mod android;
pub mod corpus;
pub mod desktop;
pub mod device;
pub mod installed_runtime;
pub mod language;
pub mod o3ps;
pub mod process;
pub mod product;
pub mod pyerr;
mod pyjson_error;
mod pyplatform;
pub mod services;
pub mod shader;
pub mod system;
pub mod worker_job;

pub use android::AndroidServices;
pub use desktop::DesktopServices;
pub use device::{
    adopt_existing_cache, installation_cache_directory, prepare_device_pipelines, renderer_cache_directory,
};
pub use installed_runtime::validate_installed_runtime;
pub use product::{ensure_runtime_config, query_product, query_product_at, validate_product_info, ProductQuery};
pub use services::{
    DeviceJob, InteriorSunRequest, LanguageQuery, NativeOutput, NativeServices, PortableCompile, RendererPassRequest,
    Report, TransferableImport,
};
pub use shader::{prepare_renderer_shader_cache, prepare_shader_seed};
pub use worker_job::{bind_worker_lifetime, watch_owner};
