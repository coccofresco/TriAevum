// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Foundations shared by every Forge crate: error families, cancellation,
//! streaming hashes, the byte-exact legacy JSON serializers and durable file
//! writes. Files whose bytes are hashed elsewhere use [`legacy_json`], never
//! a generic pretty-printer (`FORGE_SPEC.md` section 2.6).

pub mod cancel;
pub mod error;
pub mod file_holders;
pub mod fsutil;
pub mod hashing;
pub mod legacy_json;
pub mod pyvalue;

pub use cancel::CancelToken;
pub use error::{ErrorKind, ForgeError, Result};

/// The legacy tool version written into forge-state.json and TAM metadata.
/// It identifies the on-disk format generation, not this program's version.
pub const LEGACY_TOOL_VERSION: &str = "0.5.0";
