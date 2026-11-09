// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The TriAevum Forge preparation engine.
//!
//! One engine serves the desktop CLI and GUI, the Android installer (through
//! the C ABI in [`abi`]) and the release tools. The import stages live in the
//! area crates (forge-rom, forge-catalog, forge-prepare, forge-assets,
//! forge-mods); this crate orchestrates them and owns the jobs and the ABI.

pub mod abi;
pub mod active_title;
pub mod backup;
pub mod config_report;
pub mod data_actions;
pub mod diagnostics;
pub mod enhancements;
pub mod explain;
pub mod install;
pub mod inventory;
pub mod jobs;
pub mod layout;
pub mod lock;
pub mod log;
pub mod package_inventory;
pub mod preflight;
pub mod publish;
pub mod recognize;
pub mod redact;
pub mod refresh;
// Also on Android: forge-android verifies the Forge package with it.
pub mod release_signature;
pub mod repair;
pub mod restore;
pub mod session;
pub mod settings_service;
pub mod state;
pub mod transaction;
// Update from ZIP (UX plan WP-04B); the signature, planning and helper
// parts are desktop only.
#[cfg(not(target_os = "android"))]
pub mod update;
pub mod update_gate;
#[cfg(not(target_os = "android"))]
pub mod update_helper;
pub mod update_journal;

pub use forge_base::{cancel, error, fsutil, hashing, legacy_json};
pub use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result, Subject, LEGACY_TOOL_VERSION};
