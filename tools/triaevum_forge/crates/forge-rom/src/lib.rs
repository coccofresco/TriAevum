// Copyright Citra Emulator Project / Azahar Emulator Project
// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Forge inputs from a user-provided decrypted 3DS cartridge image or an
//! extracted-input directory.
//!
//! Port of the Python Forge's `ctr_rom.py` and `extracted_inputs.py`
//! (`tools/triaevum_release/`), byte-compatible per `FORGE_SPEC.md` 2.3. The
//! NCSD/NCCH layout and backwards-LZSS code path follow the documented CTR
//! container semantics implemented by Azahar. Only already-decrypted images
//! are handled; there is no key or decryption support.

mod blz;
mod extract;
mod layout;
mod paths;
mod publish;
mod stage;

use std::path::PathBuf;

use forge_base::{ErrorKind, ForgeError};

pub use blz::decompress_exefs_code;
pub use extract::{extract_decrypted_rom, extract_decrypted_rom_from, is_supported_rom_path};
pub use layout::{find_title_layout, TitleLayout};
pub use publish::{input_identity, publish_extracted_inputs};
pub use stage::stage_directory;

pub const MEDIA_UNIT: u64 = 0x200;
pub const EXHEADER_SIZE: u64 = 0x800;
pub const MAX_DECOMPRESSED_CODE_SIZE: u64 = 512 * 1024 * 1024;
pub const SUPPORTED_ROM_SUFFIXES: [&str; 2] = [".3ds", ".cci"];

/// The three retained inputs, in the order every identity and receipt uses.
pub const INPUT_KINDS: [&str; 3] = ["code", "exheader", "romfs"];

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ContainerKind {
    Ncsd,
    Ncch,
    Extracted,
}

impl ContainerKind {
    /// The Python spelling (`ExtractedTitleInputs.container_kind`).
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Ncsd => "NCSD",
            Self::Ncch => "NCCH",
            Self::Extracted => "EXTRACTED",
        }
    }
}

/// `ctr_rom.ExtractedFile`. The digest is kept raw; [`Self::sha256_hex`]
/// gives the lowercase spelling the Python dataclass stored.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ExtractedFile {
    pub path: PathBuf,
    pub bytes: u64,
    pub sha256: [u8; 32],
}

impl ExtractedFile {
    pub fn sha256_hex(&self) -> String {
        forge_base::hashing::hex(&self.sha256)
    }
}

/// `ctr_rom.ExtractedTitleInputs`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ExtractedTitleInputs {
    pub container_kind: ContainerKind,
    pub partition_index: u32,
    pub program_id: u64,
    pub code: ExtractedFile,
    pub exheader: ExtractedFile,
    pub romfs: ExtractedFile,
}

impl ExtractedTitleInputs {
    /// The inputs keyed by kind, in [`INPUT_KINDS`] order.
    pub fn by_kind(&self) -> [(&'static str, &ExtractedFile); 3] {
        [("code", &self.code), ("exheader", &self.exheader), ("romfs", &self.romfs)]
    }
}

/// A `CtrRomError` for a malformed or unusable input.
fn rom_error(message: impl Into<String>) -> ForgeError {
    ForgeError::invalid(message)
}

/// A `CtrRomError` whose cause is ciphertext where plaintext was required.
fn encrypted_error(message: impl Into<String>) -> ForgeError {
    ForgeError::new(ErrorKind::EncryptedInput, message)
}
