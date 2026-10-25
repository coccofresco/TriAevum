// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The three extracted title inputs as the catalog sees them: path, size and
//! SHA-256 per kind (`ctr_rom.ExtractedTitleInputs` without the container
//! fields, which recipe matching and adaptation never read).

use std::path::PathBuf;

use serde_json::{json, Value};

use crate::family::InputPaths;

pub const KINDS: [&str; 3] = ["code", "exheader", "romfs"];

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct InputFile {
    pub path: PathBuf,
    pub bytes: u64,
    /// Lowercase hex SHA-256.
    pub sha256: String,
}

impl InputFile {
    /// `{"bytes": ..., "sha256": ...}`, the recipe identity of this file.
    pub fn identity(&self) -> Value {
        json!({"bytes": self.bytes, "sha256": self.sha256})
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TitleInputs {
    pub program_id: u64,
    pub code: InputFile,
    pub exheader: InputFile,
    pub romfs: InputFile,
}

impl TitleInputs {
    /// `by_kind()`: code, exheader, romfs in that order.
    pub fn by_kind(&self) -> [(&'static str, &InputFile); 3] {
        [("code", &self.code), ("exheader", &self.exheader), ("romfs", &self.romfs)]
    }

    pub fn paths(&self) -> InputPaths<'_> {
        InputPaths { code: &self.code.path, exheader: &self.exheader.path, romfs: &self.romfs.path }
    }
}
