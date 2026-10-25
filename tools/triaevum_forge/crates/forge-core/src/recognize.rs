// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! ROM recognition as soon as the user selects a ROM (a Rust addition, the
//! `rom_recognize` job): which supported revision it is and which game
//! languages its RomFS offers, read in place without extracting anything.
//! The revision comes from the executable's identity
//! ([`forge_catalog::recognize_recipes`]); the languages come from the
//! runtime, the only authority on them, reading the RomFS extent of the
//! cartridge. Installing still verifies all three inputs.

use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_catalog::{recipe_label, recognize_recipes};
use forge_native::{LanguageQuery, NativeServices};
use forge_prepare::paths::{expanduser, resolve};
use serde_json::{json, Map, Value};

use crate::install::{installation_paths, recipes_path, target_runtime, InstallSource, InstallTarget, Report};

pub struct RecognizeRequest {
    pub source: InstallSource,
    /// The private data folder whose language setting is the default.
    pub data_root: Option<PathBuf>,
    pub package_root: PathBuf,
    pub target: InstallTarget,
}

/// The executable identity and where the runtime finds the RomFS.
struct Executable {
    program_id: u64,
    exheader: Vec<u8>,
    code: Vec<u8>,
    romfs: LanguageQuery,
    /// An opened document stays open while the runtime reads it by path.
    _document: Option<File>,
}

fn read_cartridge(mut file: File, image: PathBuf, cancel: &CancelToken) -> Result<Executable> {
    let size = file.metadata().map_err(|error| ForgeError::io("Cannot inspect the selected ROM", error))?.len();
    let layout = forge_rom::find_title_layout(&mut file, size, cancel)?;
    let (exheader, code) = forge_rom::read_title_executable(&mut file, &layout, cancel)?;
    Ok(Executable {
        program_id: layout.program_id,
        exheader,
        code,
        romfs: LanguageQuery { romfs: image, extent: Some((layout.romfs_offset, layout.romfs_size)) },
        _document: Some(file),
    })
}

/// The path under which this process reads an opened document again.
#[cfg(unix)]
fn document_path(file: &File) -> Result<PathBuf> {
    use std::os::fd::AsRawFd;
    Ok(PathBuf::from(format!("/proc/self/fd/{}", file.as_raw_fd())))
}

#[cfg(not(unix))]
fn document_path(_: &File) -> Result<PathBuf> {
    Err(ForgeError::new(ErrorKind::IncompatibleTarget, "Opened ROM documents are read only on Android"))
}

fn read_limited(path: &Path, limit: u64) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    File::open(path)
        .and_then(|file| file.take(limit + 1).read_to_end(&mut bytes))
        .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    Ok(bytes)
}

/// The checks of `stage_directory` that need no copy.
fn read_extracted(directory: &Path) -> Result<Executable> {
    let directory = resolve(&expanduser(directory))?;
    if !directory.is_dir() {
        return Err(ForgeError::invalid("Select a regular extracted-data directory"));
    }
    for kind in forge_rom::INPUT_KINDS {
        if !directory.join(format!("{kind}.bin")).is_file() {
            return Err(ForgeError::invalid(format!("Extracted data requires a local {kind}.bin file")));
        }
    }
    let exheader = read_limited(&directory.join("exheader.bin"), forge_rom::EXHEADER_SIZE)?;
    if exheader.len() as u64 != forge_rom::EXHEADER_SIZE {
        return Err(ForgeError::invalid("Extracted ExHeader must contain exactly 2048 bytes"));
    }
    let code = read_limited(&directory.join("code.bin"), forge_rom::MAX_DECOMPRESSED_CODE_SIZE)?;
    if code.is_empty() || code.len() as u64 > forge_rom::MAX_DECOMPRESSED_CODE_SIZE {
        return Err(ForgeError::invalid("Extracted code.bin has an invalid size"));
    }
    let program_id = u64::from_le_bytes(exheader[0x200..0x208].try_into().expect("eight bytes"));
    Ok(Executable {
        program_id,
        exheader,
        code,
        romfs: LanguageQuery::romfs(directory.join("romfs.bin")),
        _document: None,
    })
}

fn read_executable(source: InstallSource, cancel: &CancelToken) -> Result<Executable> {
    match source {
        InstallSource::ExtractedDirectory(directory) => read_extracted(&directory),
        InstallSource::Rom(path) => {
            let path = resolve(&expanduser(&path))?;
            if !path.is_file() {
                return Err(ForgeError::invalid(format!("the selected ROM does not exist: {}", path.display())));
            }
            if !forge_rom::is_supported_rom_path(&path) {
                return Err(ForgeError::new(
                    ErrorKind::UnsupportedFormat,
                    "Forge accepts decrypted ROMs with .3ds or .cci extension",
                ));
            }
            let file =
                File::open(&path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
            read_cartridge(file, path, cancel)
        }
        InstallSource::RomFile(file) => {
            let image = document_path(&file)?;
            read_cartridge(file, image, cancel)
        }
    }
}

/// Recognizes the selected ROM. Returns `{recipe, label, program_id,
/// languages, language_error}`: `languages` is the runtime's language
/// document with the private data's current selection when the ROM still
/// offers it (else the runtime's default), or null with the reason in
/// `language_error`. A ROM no supported revision matches is an error.
pub fn recognize_rom(
    request: RecognizeRequest,
    services: &dyn NativeServices,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    let (package_root, data_root) =
        installation_paths(&request.package_root, request.data_root.as_deref(), &request.target)?;
    report("identify", "Reading the cartridge layout");
    let executable = read_executable(request.source, cancel)?;
    cancel.check()?;

    report("verify", "Identifying the supported game revision...");
    let recipes =
        recognize_recipes(executable.program_id, &executable.code, &executable.exheader, &recipes_path(&package_root))?;
    let labelled: Vec<(String, String)> = recipes
        .iter()
        .map(|recipe| {
            let id = recipe.get("id").and_then(Value::as_str).unwrap_or_default().to_string();
            let label = recipe_label(recipe, &id);
            (id, label)
        })
        .collect();
    // Revisions told apart only by their RomFS share one label; installing decides.
    if labelled.iter().any(|(_, label)| *label != labelled[0].1) {
        return Err(ForgeError::integrity("the decrypted ROM matches multiple revision recipes"));
    }
    cancel.check()?;

    report("languages", "Reading the game languages...");
    let languages = target_runtime(&package_root, &request.target)
        .and_then(|runtime| forge_native::language::discover_query(services, &runtime, &executable.romfs));
    let (languages, language_error) = match languages {
        Ok(detected) => {
            let detected = Value::Object(detected);
            // A malformed setting is reported (and kept) by the installation.
            let current = forge_native::language::merged(&data_root, &detected, None).map(Value::Object);
            (current.unwrap_or(detected), Value::Null)
        }
        Err(error) if error.kind == ErrorKind::Cancelled => return Err(error),
        Err(error) => (Value::Null, Value::String(error.message)),
    };
    let (recipe, label) = labelled.into_iter().next().expect("recognize_recipes found one");
    Ok(json!({
        "recipe": recipe,
        "label": label,
        "program_id": format!("{:016X}", executable.program_id),
        "languages": languages,
        "language_error": language_error,
    }))
}

/// The `(code, label)` pairs and the selected code of a language document.
pub fn language_choices(document: &Map<String, Value>) -> (Vec<(String, String)>, Option<String>) {
    let field = |entry: &Value, key: &str| entry.get(key).and_then(Value::as_str).unwrap_or_default().to_string();
    let choices = document
        .get("available")
        .and_then(Value::as_array)
        .map(|entries| entries.iter().map(|entry| (field(entry, "code"), field(entry, "label"))).collect())
        .unwrap_or_default();
    (choices, document.get("selected").and_then(Value::as_str).map(str::to_string))
}
