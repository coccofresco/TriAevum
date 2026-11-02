// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Supported revisions, precompiled catalogs, input adapters, content families
//! and RomFS identity: the Python Forge's supported_revisions/recipe handling,
//! qualified_input_coverage.py, precompiled_titles.py, precompiled_variants.py,
//! input_adapters.py, input_copy_adapter.py, data_compatibility.py,
//! romfs_identity.py and oot3d_region_assets.py (tools/triaevum_release/),
//! byte- and decision-compatible per FORGE_SPEC.md.
//!
//! Recipes and catalogs stay `serde_json` values compared with Python
//! semantics ([`pyjson`]), so unknown publisher fields survive unchanged.

pub mod adapters;
pub mod copy_adapter;
pub mod coverage;
pub mod family;
pub mod inputs;
pub mod paths;
pub mod platform;
pub mod precompiled;
pub mod pyjson;
pub mod recipes;
pub mod region_assets;
pub mod romfs_identity;
pub mod variants;

pub use adapters::{adapt_extracted_inputs, import_contract, validate_adapter, Adaptation};
pub use family::{InputPaths, FORMAT as CONTENT_FAMILY_FORMAT};
pub use inputs::{InputFile, TitleInputs, KINDS};
pub use platform::{desktop_target, for_target, host_platform, ReleasePlatform, ANDROID, LINUX, MACOS, WINDOWS};
pub use precompiled::{
    artifact_path, checked_file, load_catalog, runtime_record, select_title, set_native_library_directory,
    validate_title,
};
pub use recipes::{
    default_recipes, load_recipe, match_extracted_recipe, recipe_label, recognize_recipes, verify_input, InputDigest,
    VerifiedInput,
};
pub use romfs_identity::{fingerprint, RomfsFingerprint};
