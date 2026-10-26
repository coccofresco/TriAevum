// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Checks an installation makes before it writes anything (UX plan WP-03):
//! the activation and data folders accept a file, the data folder's volume
//! has room for the peak of the import, and the system has a Vulkan loader.
//!
//! The space estimate is the peak staging of an import: the game data read
//! from the input (RomFS and code, extracted then published), the largest
//! shader corpus the package ships (graphics preparation writes about as
//! much again) and a 512 MB margin. It is an estimate: an import that runs
//! out of space anyway still fails as `storage_full` at the write that
//! failed. A missing Vulkan loader is reported, not a refusal: the game
//! itself names the problem when it starts.

use std::fs::File;
use std::path::Path;

use forge_base::{ErrorKind, ForgeError, Operation, Reason, Result, Subject};
use forge_catalog::paths::join_posix;
use serde_json::{Map, Value};

use crate::install::InstallSource;

/// Added to every estimate.
pub const MARGIN: u64 = 512 * 1024 * 1024;

/// The probe's name: hidden, per process, never left behind.
fn probe_name() -> String {
    format!(".forge-write-probe-{}", std::process::id())
}

/// Creates and deletes a probe file in `folder` (created when missing).
/// `role` names the folder in the message ("data folder", ...).
pub fn check_writable(folder: &Path, role: &str) -> Result<()> {
    let failed = |error: std::io::Error| {
        let reason = forge_base::error::os_reason(&error);
        let mut failure = ForgeError::io(format!("The {role} {} cannot be written", folder.display()), error)
            .during(Operation::Preflight)
            .about(Subject::Destination);
        // A folder that refuses a new file is a read-only destination for
        // the user, whatever family the OS error maps to.
        if matches!(reason, Some(Reason::AccessDenied | Reason::ReadOnlyDestination)) {
            failure.kind = ErrorKind::PermissionDenied;
            failure = failure.because(Reason::ReadOnlyDestination);
        }
        failure
    };
    std::fs::create_dir_all(folder).map_err(failed)?;
    let probe = folder.join(probe_name());
    File::create(&probe).map_err(failed)?;
    std::fs::remove_file(&probe).map_err(failed)
}

/// What an import is estimated to need on the data folder's volume.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StorageEstimate {
    /// RomFS and code of the selected input.
    pub game_data: u64,
    /// The package's largest shader corpus.
    pub shader_corpus: u64,
    pub margin: u64,
}

impl StorageEstimate {
    pub fn required(&self) -> u64 {
        self.game_data.saturating_add(self.shader_corpus).saturating_add(self.margin)
    }
}

/// RomFS plus code of the selected input, read from its layout (a ROM) or
/// its files (extracted data); None when it cannot be read (the import then
/// reports the real problem).
pub fn input_bytes(source: &InstallSource) -> Option<u64> {
    fn layout(file: &mut File) -> Option<u64> {
        let size = file.metadata().ok()?.len();
        let layout = forge_rom::find_title_layout(file, size, &forge_base::CancelToken::new()).ok()?;
        Some(layout.romfs_size + layout.code_size)
    }
    match source {
        InstallSource::Rom(path) => layout(&mut File::open(forge_prepare::paths::expanduser(path)).ok()?),
        InstallSource::RomFile(file) => {
            let mut file = file.try_clone().ok()?;
            layout(&mut file)
        }
        InstallSource::ExtractedDirectory(path) => {
            let folder = forge_prepare::paths::expanduser(path);
            let sizes = ["code.bin", "exheader.bin", "romfs.bin"]
                .iter()
                .map(|name| std::fs::metadata(folder.join(name)).map(|metadata| metadata.len()).ok());
            sizes.sum()
        }
    }
}

/// The largest `shader_preparation.pack` of the catalog's titles, in bytes.
pub fn shader_corpus_bytes(catalog: &Map<String, Value>, package_root: &Path) -> u64 {
    let titles = catalog.get("titles").and_then(Value::as_array).map(Vec::as_slice).unwrap_or_default();
    titles
        .iter()
        .filter_map(|title| title.pointer("/shader_preparation/pack/path")?.as_str())
        .filter_map(|path| std::fs::metadata(join_posix(package_root, path)).ok())
        .map(|metadata| metadata.len())
        .max()
        .unwrap_or(0)
}

/// `bytes` as people read sizes: `512 MB`, `2.3 GB` (binary units).
pub fn format_bytes(bytes: u64) -> String {
    const MB: f64 = 1024.0 * 1024.0;
    let megabytes = bytes as f64 / MB;
    if megabytes < 1024.0 {
        format!("{} MB", megabytes.ceil() as u64)
    } else {
        format!("{:.1} GB", megabytes / 1024.0)
    }
}

/// Fails `storage_full` with the required and free amounts when the volume
/// of `folder` has less than the estimate. `available` None (the system
/// does not say) passes.
pub fn check_storage(folder: &Path, estimate: &StorageEstimate, available: Option<u64>) -> Result<()> {
    let Some(available) = available else { return Ok(()) };
    let required = estimate.required();
    if available >= required {
        return Ok(());
    }
    Err(ForgeError::new(
        ErrorKind::StorageFull,
        format!(
            "Not enough free space for {}: preparing the game needs about {} (game data {}, graphics data {}, \
             margin {}), and {} are free. Free at least {} on that drive, or choose a data folder on another drive.",
            folder.display(),
            format_bytes(required),
            format_bytes(estimate.game_data),
            format_bytes(estimate.shader_corpus),
            format_bytes(estimate.margin),
            format_bytes(available),
            format_bytes(required - available),
        ),
    )
    .with_path(folder)
    .during(Operation::Preflight)
    .about(Subject::Destination)
    .because(Reason::InsufficientSpace))
}

/// The warning for a system without the Vulkan loader, None otherwise.
pub fn vulkan_loader_warning() -> Option<String> {
    (!forge_native::system::vulkan_loader_present()).then(|| {
        format!(
            "Warning: the Vulkan loader ({}) was not found on this system. The game draws with Vulkan: install or \
             update the graphics driver before playing. The installation continues.",
            forge_native::system::VULKAN_LOADER
        )
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::explain::explain;

    #[test]
    fn too_little_space_is_storage_full_with_both_numbers() {
        let estimate = StorageEstimate { game_data: 3 << 30, shader_corpus: 300 << 20, margin: MARGIN };
        let folder = Path::new("data");
        assert!(check_storage(folder, &estimate, Some(estimate.required())).is_ok());
        assert!(check_storage(folder, &estimate, None).is_ok());
        let error = check_storage(folder, &estimate, Some(1 << 30)).unwrap_err();
        assert_eq!(error.kind, ErrorKind::StorageFull);
        assert_eq!(error.reason, Some(Reason::InsufficientSpace));
        let explanation = explain(&error);
        assert_eq!(explanation.id, "storage.insufficient_space");
        assert!(explanation.details.contains("needs about 3.8 GB"), "{}", explanation.details);
        assert!(explanation.details.contains("1.0 GB are free"), "{}", explanation.details);
        assert!(explanation.details.contains("Free at least 2.8 GB"), "{}", explanation.details);
    }

    #[test]
    fn a_writable_folder_passes_and_keeps_no_probe() {
        let directory = tempfile::tempdir().unwrap();
        let folder = directory.path().join("new").join("data");
        check_writable(&folder, "data folder").unwrap();
        assert_eq!(std::fs::read_dir(&folder).unwrap().count(), 0);
    }

    #[test]
    fn a_file_in_place_of_the_folder_fails() {
        let directory = tempfile::tempdir().unwrap();
        let blocked = directory.path().join("blocked");
        std::fs::write(&blocked, b"").unwrap();
        let error = check_writable(&blocked.join("data"), "data folder").unwrap_err();
        assert_eq!(error.operation, Some(Operation::Preflight));
        assert!(error.message.starts_with("The data folder "), "{}", error.message);
    }

    #[test]
    fn extracted_inputs_are_measured() {
        let directory = tempfile::tempdir().unwrap();
        for (name, size) in [("code.bin", 10), ("exheader.bin", 2), ("romfs.bin", 100)] {
            std::fs::write(directory.path().join(name), vec![0u8; size]).unwrap();
        }
        let source = InstallSource::ExtractedDirectory(directory.path().to_path_buf());
        assert_eq!(input_bytes(&source), Some(112));
        assert_eq!(format_bytes(512 << 20), "512 MB");
        assert_eq!(format_bytes(1), "1 MB");
    }
}
