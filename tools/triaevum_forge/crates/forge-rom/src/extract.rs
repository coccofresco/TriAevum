// Copyright Citra Emulator Project / Azahar Emulator Project
// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! `ctr_rom.extract_decrypted_rom`: writes `exheader.bin`, decompressed
//! `code.bin` and the exact RomFS extent into a new directory.

use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use forge_base::hashing::{copy_hashing, sha256_bytes};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};

use crate::layout::{find_title_layout, read_exact, TitleLayout};
use crate::paths::{expand_user, resolve};
use crate::{decompress_exefs_code, rom_error, ExtractedFile, ExtractedTitleInputs, EXHEADER_SIZE};

pub fn is_supported_rom_path(path: &Path) -> bool {
    path.extension()
        .map(|extension| format!(".{}", extension.to_string_lossy().to_lowercase()))
        .is_some_and(|suffix| crate::SUPPORTED_ROM_SUFFIXES.contains(&suffix.as_str()))
}

/// Removes a directory this module created unless the work inside it
/// succeeded, including on panic. Never used for a pre-existing directory.
pub(crate) struct CreatedDirectory {
    path: PathBuf,
    keep: bool,
}

impl CreatedDirectory {
    /// Creates `path` (already resolved) after creating its parents.
    pub(crate) fn create(path: PathBuf) -> Result<Self> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)
                .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
        }
        std::fs::create_dir(&path)
            .map_err(|error| ForgeError::io(format!("Cannot create {}", path.display()), error))?;
        Ok(Self { path, keep: false })
    }

    pub(crate) fn path(&self) -> &Path {
        &self.path
    }

    pub(crate) fn keep(mut self) {
        self.keep = true;
    }
}

impl Drop for CreatedDirectory {
    fn drop(&mut self) {
        if !self.keep {
            let _ = std::fs::remove_dir_all(&self.path);
        }
    }
}

/// `ctr_rom._copy_region`: streams `size` bytes at `offset` into a new file.
/// `destination` is returned as given; callers pass resolved paths.
pub(crate) fn copy_region<R: Read + Seek>(
    stream: &mut R,
    offset: u64,
    size: u64,
    destination: &Path,
    cancel: &CancelToken,
) -> Result<ExtractedFile> {
    stream.seek(SeekFrom::Start(offset)).map_err(|error| ForgeError::io("Cannot read the ROM", error))?;
    let mut output = File::create_new(destination)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", destination.display()), error))?;
    let (copied, sha256) = copy_hashing(stream, &mut output, size, cancel)?;
    if copied != size {
        return Err(rom_error("the ROM ended while extracting RomFS"));
    }
    Ok(ExtractedFile { path: destination.to_path_buf(), bytes: size, sha256 })
}

fn write_bytes(path: PathBuf, data: &[u8]) -> Result<ExtractedFile> {
    std::fs::write(&path, data).map_err(|error| ForgeError::io(format!("Cannot write {}", path.display()), error))?;
    Ok(ExtractedFile { path, bytes: data.len() as u64, sha256: sha256_bytes(data) })
}

fn checked_output_directory(output_directory: &Path) -> Result<CreatedDirectory> {
    let output_directory = resolve(&expand_user(output_directory));
    if std::fs::metadata(&output_directory).is_ok() {
        return Err(rom_error(format!("ROM extraction directory already exists: {}", output_directory.display())));
    }
    CreatedDirectory::create(output_directory)
}

/// The ExHeader and the decompressed `.code` of a located title, exactly as
/// extraction writes them; ROM recognition reads them in place.
pub fn read_title_executable<R: Read + Seek>(
    stream: &mut R,
    layout: &TitleLayout,
    cancel: &CancelToken,
) -> Result<(Vec<u8>, Vec<u8>)> {
    let exheader = read_exact(stream, layout.exheader_offset, EXHEADER_SIZE, "ExHeader", cancel)?;
    let mut code = read_exact(stream, layout.code_offset, layout.code_size, "ExeFS .code", cancel)?;
    if layout.compressed_code {
        code = decompress_exefs_code(&code, cancel)?;
    }
    cancel.check()?;
    Ok((exheader, code))
}

fn extract_into<R: Read + Seek>(
    stream: &mut R,
    file_size: u64,
    output: &Path,
    cancel: &CancelToken,
) -> Result<ExtractedTitleInputs> {
    let layout = find_title_layout(stream, file_size, cancel)?;
    let (exheader_bytes, code_bytes) = read_title_executable(stream, &layout, cancel)?;
    let exheader = write_bytes(output.join("exheader.bin"), &exheader_bytes)?;
    let code = write_bytes(output.join("code.bin"), &code_bytes)?;
    drop(code_bytes);
    let romfs = copy_region(stream, layout.romfs_offset, layout.romfs_size, &output.join("romfs.bin"), cancel)?;
    Ok(ExtractedTitleInputs {
        container_kind: layout.container_kind,
        partition_index: layout.partition_index,
        program_id: layout.program_id,
        code,
        exheader,
        romfs,
    })
}

/// Extracts persistent Forge inputs from a ROM file into a new output
/// directory. On any failure (or cancellation) only that new directory is
/// removed; the ROM is never modified.
pub fn extract_decrypted_rom(
    rom_path: &Path,
    output_directory: &Path,
    require_rom_suffix: bool,
    cancel: &CancelToken,
) -> Result<ExtractedTitleInputs> {
    let source = expand_user(rom_path);
    if std::fs::symlink_metadata(&source).is_ok_and(|metadata| metadata.file_type().is_symlink()) {
        return Err(rom_error("the selected ROM must be a regular local file"));
    }
    let source = resolve(&source);
    if !std::fs::metadata(&source).is_ok_and(|metadata| metadata.is_file()) {
        return Err(rom_error(format!("the selected ROM does not exist: {}", source.display())));
    }
    if require_rom_suffix && !is_supported_rom_path(&source) {
        return Err(ForgeError::new(
            ErrorKind::UnsupportedFormat,
            "Forge accepts decrypted ROMs with .3ds or .cci extension",
        ));
    }

    let directory = checked_output_directory(output_directory)?;
    let extracted = {
        let mut stream =
            File::open(&source).map_err(|error| ForgeError::io(format!("Cannot read {}", source.display()), error))?;
        let file_size = stream
            .metadata()
            .map_err(|error| ForgeError::io(format!("Cannot read {}", source.display()), error))?
            .len();
        extract_into(&mut stream, file_size, directory.path(), cancel)?
    };
    directory.keep();
    Ok(extracted)
}

/// The same extraction from an already opened seekable source, such as an
/// Android document descriptor wrapped in a `File`. `file_size` is its length.
pub fn extract_decrypted_rom_from<R: Read + Seek>(
    stream: &mut R,
    file_size: u64,
    output_directory: &Path,
    cancel: &CancelToken,
) -> Result<ExtractedTitleInputs> {
    let directory = checked_output_directory(output_directory)?;
    let extracted = extract_into(stream, file_size, directory.path(), cancel)?;
    directory.keep();
    Ok(extracted)
}
