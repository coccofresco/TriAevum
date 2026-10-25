// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Process manifest, content index, forge state and the TAM container.
//!
//! Byte-compatible port (FORGE_SPEC.md sections 2.5, 2.6, 3.1) of
//! `tools/oot3d/native_a32_runtime/build_process_manifest.py`, the content
//! preparation half of `tools/triaevum_release/forge.py` (verification, hash
//! cache, content key, title folder, `prepare_content`,
//! `load_prepared_content`, `package_private_module`,
//! `activate_prepared_title`), `installation_context.py` and
//! `tam_builder.py`, plus a TAM reader that mirrors the runtime's
//! `tam_reader.cpp`. The developer AOT/compiler commands are not ported.

pub mod content;
pub mod hash_cache;
pub mod module;
pub mod paths;
pub mod process_manifest;
pub mod pyvalue;
pub mod tam;
pub mod tam_reader;

pub use content::{
    check_title_path_length, content_key, load_prepared_content, prepare_content, title_directory,
    validate_process_manifest, verify_input, verify_sources, ArchiveContract, FamilyVerifier, PrepareResult,
    PreparedContent, ProcessManifestBuilder, SourcePaths, VerifiedInput, VerifiedInputs,
};
pub use hash_cache::HashCache;
pub use module::{activate_prepared_title, package_private_module, ActivationResult, PackageResult};
pub use paths::{InstallationContext, PathScope};
pub use process_manifest::build_manifest;
pub use tam::{write_tam_file, Tam, TamFile, TamRequest};
pub use tam_reader::{parse_tam, TamError, TamErrorCode, TamView};
