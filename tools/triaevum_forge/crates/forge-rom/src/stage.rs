// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Container-independent import of a decoded title input directory
//! (`extracted_inputs.stage_directory`). This is an input transport, not a
//! compatibility decision: recipe verification and the precompiled module
//! catalog remain authoritative after staging.

use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use forge_base::{CancelToken, ForgeError, Result};

use crate::extract::{copy_region, CreatedDirectory};
use crate::layout::u64_at;
use crate::paths::{expand_user, resolve};
use crate::{
    encrypted_error, rom_error, ContainerKind, ExtractedFile, ExtractedTitleInputs, EXHEADER_SIZE, INPUT_KINDS,
    MAX_DECOMPRESSED_CODE_SIZE,
};

fn is_symlink(path: &Path) -> bool {
    std::fs::symlink_metadata(path).is_ok_and(|metadata| metadata.file_type().is_symlink())
}

fn file_size(path: &Path) -> Result<u64> {
    std::fs::metadata(path)
        .map(|metadata| metadata.len())
        .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))
}

fn starts_with_ivfc(path: &Path) -> Result<bool> {
    let mut magic = Vec::with_capacity(4);
    File::open(path)
        .and_then(|file| file.take(4).read_to_end(&mut magic))
        .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    Ok(magic == b"IVFC")
}

/// Copies and hashes `code.bin`, `exheader.bin` and `romfs.bin` from a
/// regular directory into a new staging directory outside it. The program ID
/// comes from ExHeader offset 0x200. An existing `output` is an error and is
/// left untouched; on failure only the newly created `output` is removed.
pub fn stage_directory(source: &Path, output: &Path, cancel: &CancelToken) -> Result<ExtractedTitleInputs> {
    let source = expand_user(source);
    if is_symlink(&source) || !source.is_dir() {
        return Err(rom_error("Select a regular extracted-data directory"));
    }
    let source = resolve(&source);
    let paths: Vec<PathBuf> = INPUT_KINDS.iter().map(|kind| source.join(format!("{kind}.bin"))).collect();
    for (kind, path) in INPUT_KINDS.iter().zip(&paths) {
        if is_symlink(path) || !path.is_file() || resolve(path).parent() != Some(source.as_path()) {
            return Err(rom_error(format!("Extracted data requires a local {kind}.bin file")));
        }
    }
    let [code_path, exheader_path, romfs_path] = [&paths[0], &paths[1], &paths[2]];
    if file_size(exheader_path)? != EXHEADER_SIZE {
        return Err(rom_error("Extracted ExHeader must contain exactly 2048 bytes"));
    }
    if !(1..=MAX_DECOMPRESSED_CODE_SIZE).contains(&file_size(code_path)?) {
        return Err(rom_error("Extracted code.bin has an invalid size"));
    }
    if !starts_with_ivfc(romfs_path)? {
        return Err(encrypted_error("Extracted romfs.bin must be a decrypted IVFC image"));
    }
    let output = resolve(&expand_user(output));
    if output.starts_with(&source) {
        return Err(rom_error("Staging must be outside the source directory"));
    }

    let directory = CreatedDirectory::create(output)?;
    let mut staged: Vec<ExtractedFile> = Vec::with_capacity(paths.len());
    for path in &paths {
        let mut stream =
            File::open(path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
        let size = file_size(path)?;
        let destination = directory.path().join(path.file_name().unwrap_or_default());
        staged.push(copy_region(&mut stream, 0, size, &destination, cancel)?);
    }
    let header = std::fs::read(&staged[1].path)
        .map_err(|error| ForgeError::io(format!("Cannot read {}", staged[1].path.display()), error))?;
    if header.len() < 0x208 {
        // Python failed here with struct.error when the source shrank mid-copy.
        return Err(rom_error("Extracted ExHeader must contain exactly 2048 bytes"));
    }
    let program_id = u64_at(&header, 0x200);
    let mut staged = staged.into_iter();
    let (code, exheader, romfs) = (staged.next().unwrap(), staged.next().unwrap(), staged.next().unwrap());
    directory.keep();
    Ok(ExtractedTitleInputs {
        container_kind: ContainerKind::Extracted,
        partition_index: 0,
        program_id,
        code,
        exheader,
        romfs,
    })
}
