// Copyright Citra Emulator Project / Azahar Emulator Project
// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Content-addressed retention of extracted inputs under `data/sources`
//! (`ctr_rom._input_identity` and `publish_extracted_inputs`).

use std::path::Path;

use forge_base::hashing::{hex, sha256_bytes, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};

use crate::paths::{expand_user, resolve};
use crate::{rom_error, ExtractedFile, ExtractedTitleInputs};

const IDENTITY_DOMAIN: &[u8] = b"triaevum-extracted-title-inputs-v1\0";

/// SHA-256 of the domain string followed, per kind in code/exheader/romfs
/// order, by the ASCII kind, a NUL and the raw 32-byte digest. No name,
/// program ID, region or timestamp enters the key.
pub fn input_identity(extracted: &ExtractedTitleInputs) -> String {
    let mut preimage = IDENTITY_DOMAIN.to_vec();
    for (kind, item) in extracted.by_kind() {
        preimage.extend_from_slice(kind.as_bytes());
        preimage.push(0);
        preimage.extend_from_slice(&item.sha256);
    }
    hex(&sha256_bytes(&preimage))
}

fn is_symlink(path: &Path) -> bool {
    std::fs::symlink_metadata(path).is_ok_and(|metadata| metadata.file_type().is_symlink())
}

fn file_name(item: &ExtractedFile) -> &std::ffi::OsStr {
    item.path.file_name().unwrap_or_default()
}

fn matches(existing: &Path, expected: &ExtractedFile, cancel: &CancelToken) -> Result<bool> {
    let regular =
        std::fs::metadata(existing).is_ok_and(|metadata| metadata.is_file() && metadata.len() == expected.bytes);
    if !regular || is_symlink(existing) {
        return Ok(false);
    }
    Ok(sha256_file(existing, cancel)?.1 == expected.sha256)
}

/// Atomically retains an extracted title under its content identity. An
/// existing destination is reused only after an exact size and hash check
/// of every input, and the staging directory is then removed; otherwise the
/// staging directory is renamed into place.
pub fn publish_extracted_inputs(
    extracted: &ExtractedTitleInputs,
    source_root: &Path,
    cancel: &CancelToken,
) -> Result<ExtractedTitleInputs> {
    let source_root = resolve(&expand_user(source_root));
    std::fs::create_dir_all(&source_root)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", source_root.display()), error))?;
    let staging = extracted.code.path.parent().unwrap_or(Path::new(""));
    if extracted.by_kind().iter().any(|(_, item)| item.path.parent() != Some(staging)) {
        return Err(ForgeError::new(ErrorKind::Internal, "extracted title inputs do not share one staging directory"));
    }

    let destination = source_root.join(input_identity(extracted));
    if std::fs::metadata(&destination).is_ok() {
        if !destination.is_dir() || is_symlink(&destination) {
            return Err(rom_error(format!("private title source path is invalid: {}", destination.display())));
        }
        for (kind, expected) in extracted.by_kind() {
            if !matches(&destination.join(file_name(expected)), expected, cancel)? {
                return Err(ForgeError::integrity(format!(
                    "existing private {kind} input does not match its content identity"
                )));
            }
        }
        std::fs::remove_dir_all(staging)
            .map_err(|error| ForgeError::io(format!("Cannot remove {}", staging.display()), error))?;
    } else {
        std::fs::rename(staging, &destination).map_err(|error| {
            ForgeError::io(format!("Cannot move {} to {}", staging.display(), destination.display()), error)
        })?;
    }

    let retained = |item: &ExtractedFile| ExtractedFile {
        path: resolve(&destination.join(file_name(item))),
        bytes: item.bytes,
        sha256: item.sha256,
    };
    Ok(ExtractedTitleInputs {
        container_kind: extracted.container_kind,
        partition_index: extracted.partition_index,
        program_id: extracted.program_id,
        code: retained(&extracted.code),
        exheader: retained(&extracted.exheader),
        romfs: retained(&extracted.romfs),
    })
}
