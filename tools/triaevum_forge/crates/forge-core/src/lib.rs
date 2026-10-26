// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The TriAevum Forge preparation engine.
//!
//! One engine serves the desktop CLI and GUI, the Android installer (through
//! the C ABI in [`abi`]) and the release tools. The import stages live in the
//! area crates (forge-rom, forge-catalog, forge-prepare, forge-assets,
//! forge-mods); this crate orchestrates them and owns the jobs and the ABI.

pub mod abi;
pub mod explain;
pub mod install;
pub mod jobs;
pub mod layout;
pub mod lock;
pub mod log;
pub mod publish;
pub mod recognize;
pub mod redact;
pub mod state;
pub mod transaction;

pub use forge_base::{cancel, error, fsutil, hashing, legacy_json};
pub use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result, Subject, LEGACY_TOOL_VERSION};
