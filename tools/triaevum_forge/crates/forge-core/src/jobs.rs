// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Job requests accepted through the C ABI and the CLI's machine interface.

use std::fs::{self, File};
use std::io::{BufWriter, Read};
use std::path::{Path, PathBuf};

use serde_json::{json, Map, Value};

use forge_native::DesktopServices;

use crate::abi::{EventSink, Job};
use crate::cancel::CancelToken;
use crate::error::{ForgeError, Result};
use crate::hashing::{copy_hashing, hex};
use crate::install::{self, install_private_title, InstallRequest, InstallSource};

pub const REQUEST_FORMAT: &str = "triaevum_forge_request_v1";

pub fn capabilities() -> Value {
    json!({
        "format": "triaevum_forge_capabilities_v1",
        "abi_version": crate::abi::ABI_VERSION,
        "engine_version": env!("CARGO_PKG_VERSION"),
        "target": target_triple(),
        "input_formats": ["3ds", "cci", "extracted_directory"],
        "encrypted_input": false,
        "jobs": ["spool_copy", "rom_identify", "install"],
    })
}

pub fn target_triple() -> &'static str {
    if cfg!(all(target_os = "windows", target_arch = "x86_64")) {
        "x86_64-pc-windows-msvc"
    } else if cfg!(all(target_os = "linux", target_arch = "x86_64")) {
        "x86_64-unknown-linux-gnu"
    } else if cfg!(all(target_os = "android", target_arch = "aarch64")) {
        "aarch64-linux-android"
    } else if cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        "aarch64-apple-darwin"
    } else if cfg!(all(target_os = "macos", target_arch = "x86_64")) {
        "x86_64-apple-darwin"
    } else {
        "unsupported"
    }
}

pub fn start(request: &[u8]) -> Result<Job> {
    let request = crate::legacy_json::load_object(request)
        .map_err(|error| ForgeError::invalid(format!("Invalid Forge request: {error}")))?;
    if request.get("format").and_then(Value::as_str) != Some(REQUEST_FORMAT) {
        return Err(ForgeError::invalid(format!("Forge request format must be {REQUEST_FORMAT}")));
    }
    match request.get("job").and_then(Value::as_str) {
        Some("spool_copy") => {
            let job = SpoolCopy::parse(&request)?;
            Ok(Job::spawn(move |sink, cancel| job.run(sink, cancel)))
        }
        Some("rom_identify") => {
            let source = Source::parse(request.get("source"))?;
            Ok(Job::spawn(move |sink, cancel| rom_identify(source, sink, cancel)))
        }
        Some("install") => {
            let job = InstallJob::parse(&request)?;
            Ok(Job::spawn(move |sink, cancel| job.run(sink, cancel)))
        }
        Some(other) => Err(ForgeError::invalid(format!("Unknown Forge job: {other}"))),
        None => Err(ForgeError::invalid("Forge request has no job")),
    }
}

/// Where a job reads its input: a local path, or (Android) a descriptor the
/// host duplicated for Forge, which then owns and closes it.
pub enum Source {
    Path(PathBuf),
    #[cfg(unix)]
    Descriptor(std::os::fd::OwnedFd),
}

impl Source {
    fn parse(value: Option<&Value>) -> Result<Self> {
        let object = value.and_then(Value::as_object).ok_or_else(|| ForgeError::invalid("Request has no source"))?;
        if let Some(path) = object.get("path").and_then(Value::as_str) {
            return Ok(Self::Path(PathBuf::from(path)));
        }
        #[cfg(unix)]
        if let Some(fd) = object.get("fd").and_then(Value::as_i64) {
            use std::os::fd::FromRawFd;
            let fd = i32::try_from(fd).map_err(|_| ForgeError::invalid("Invalid source descriptor"))?;
            if fd < 0 {
                return Err(ForgeError::invalid("Invalid source descriptor"));
            }
            // SAFETY: the host transfers ownership of this duplicated descriptor.
            return Ok(Self::Descriptor(unsafe { std::os::fd::OwnedFd::from_raw_fd(fd) }));
        }
        Err(ForgeError::invalid("Source needs a path or a descriptor"))
    }

    pub fn open(self) -> Result<File> {
        match self {
            Self::Path(path) => {
                File::open(&path).map_err(|error| ForgeError::io(format!("Cannot open {}", path.display()), error))
            }
            #[cfg(unix)]
            Self::Descriptor(fd) => Ok(File::from(fd)),
        }
    }
}

/// Copies a source (seekable or a provider pipe) into an owned file with a
/// byte limit, hashing it. The destination appears only when complete.
struct SpoolCopy {
    source: Source,
    destination: PathBuf,
    max_bytes: u64,
    declared_bytes: Option<u64>,
}

impl SpoolCopy {
    fn parse(request: &Map<String, Value>) -> Result<Self> {
        let destination = request
            .get("destination")
            .and_then(Value::as_str)
            .map(PathBuf::from)
            .ok_or_else(|| ForgeError::invalid("spool_copy needs a destination"))?;
        let max_bytes = request
            .get("max_bytes")
            .and_then(Value::as_u64)
            .ok_or_else(|| ForgeError::invalid("spool_copy needs max_bytes"))?;
        Ok(Self {
            source: Source::parse(request.get("source"))?,
            destination,
            max_bytes,
            declared_bytes: request.get("declared_bytes").and_then(Value::as_u64),
        })
    }

    fn run(self, sink: &EventSink, cancel: &CancelToken) -> Result<Value> {
        sink.stage("spool", "Copying the selected file");
        let mut reader =
            ProgressReader { inner: self.source.open()?, sink, done: 0, reported: 0, total: self.declared_bytes };
        let temporary = temporary_sibling(&self.destination)?;
        let outcome = (|| {
            let file = File::create(&temporary)
                .map_err(|error| ForgeError::io(format!("Cannot create {}", temporary.display()), error))?;
            let mut writer = BufWriter::new(file);
            // One byte past the limit detects an oversized source.
            let (bytes, digest) = copy_hashing(&mut reader, &mut writer, self.max_bytes + 1, cancel)?;
            if bytes > self.max_bytes {
                return Err(ForgeError::invalid(format!("The selected file is larger than {} bytes", self.max_bytes)));
            }
            let file = writer.into_inner().map_err(|error| ForgeError::io("Write failed", error.into_error()))?;
            file.sync_all().map_err(|error| ForgeError::io("Cannot flush the copy", error))?;
            drop(file);
            fs::rename(&temporary, &self.destination)
                .map_err(|error| ForgeError::io(format!("Cannot publish {}", self.destination.display()), error))?;
            Ok(json!({"bytes": bytes, "sha256": hex(&digest), "path": self.destination.to_string_lossy()}))
        })();
        if outcome.is_err() {
            let _ = fs::remove_file(&temporary);
        }
        outcome
    }
}

/// Reads the cartridge layout in place (no copy): program ID, container and
/// the extents Forge would extract. The source must be seekable.
fn rom_identify(source: Source, sink: &EventSink, cancel: &CancelToken) -> Result<Value> {
    sink.stage("identify", "Reading the cartridge layout");
    let mut file = source.open()?;
    let size = file.metadata().map_err(|error| ForgeError::io("Cannot inspect the selected file", error))?.len();
    let layout = forge_rom::find_title_layout(&mut file, size, cancel)?;
    Ok(json!({
        "container": layout.container_kind.as_str(),
        "partition": layout.partition_index,
        "program_id": format!("{:016X}", layout.program_id),
        "compressed_code": layout.compressed_code,
        "code_size": layout.code_size,
        "romfs_size": layout.romfs_size,
        "file_size": size,
    }))
}

/// The `install` job: `forge_gui.install_private_title` with the desktop
/// helper processes, optionally after the GUI's HD texture download.
struct InstallJob {
    source: InstallSource,
    data_root: Option<PathBuf>,
    package_root: PathBuf,
    download_hd_textures: bool,
}

impl InstallJob {
    fn parse(request: &Map<String, Value>) -> Result<Self> {
        let source = Source::parse(request.get("source"))?;
        let source = match (request.get("source_kind").and_then(Value::as_str), source) {
            (Some("rom"), Source::Path(path)) => InstallSource::Rom(path),
            (Some("extracted_directory"), Source::Path(path)) => InstallSource::ExtractedDirectory(path),
            #[cfg(unix)]
            (Some("rom"), source @ Source::Descriptor(_)) => InstallSource::RomFile(source.open()?),
            #[cfg(unix)]
            (Some("extracted_directory"), Source::Descriptor(_)) => {
                return Err(ForgeError::invalid("Extracted data must be a folder path"))
            }
            _ => return Err(ForgeError::invalid("source_kind must be \"rom\" or \"extracted_directory\"")),
        };
        let path = |key: &str| request.get(key).and_then(Value::as_str).map(PathBuf::from);
        let package_root = match path("package_root") {
            Some(root) => root,
            None => executable_directory()?,
        };
        let download_hd_textures = match request.get("download_hd_textures") {
            None | Some(Value::Null) => cfg!(not(target_os = "android")),
            Some(Value::Bool(flag)) => *flag,
            Some(_) => return Err(ForgeError::invalid("download_hd_textures must be a boolean")),
        };
        Ok(Self { source, data_root: path("data_root"), package_root, download_hd_textures })
    }

    fn run(self, sink: &EventSink, cancel: &CancelToken) -> Result<Value> {
        if self.download_hd_textures {
            download_hd_textures(&self.package_root, self.data_root.as_deref(), sink, cancel)?;
        }
        let request =
            InstallRequest { source: self.source, data_root: self.data_root, package_root: self.package_root };
        let mut events = StageEvents { sink, current: String::new() };
        install_private_title(request, &DesktopServices, &mut |stage, message| events.report(stage, message), cancel)
    }
}

/// Python's top-level install stages; the area crates' own report names
/// (water, topscreen, interior-sun, pipelines, ...) map to `assets` or
/// `shaders`. The first message of a stage is its `stage` event, later
/// messages of the same stage are `report` events.
struct StageEvents<'a> {
    sink: &'a EventSink,
    current: String,
}

impl StageEvents<'_> {
    fn report(&mut self, stage: &str, message: &str) {
        let stage = match stage {
            "preflight" | "extract" | "verify" | "adapt" | "prepare" | "activate" | "ready" => stage,
            "shaders" | "interior-sun" | "pipelines" => "shaders",
            _ => "assets",
        };
        if stage == self.current {
            self.sink.emit(json!({"event": "report", "stage": stage, "message": message}));
        } else {
            self.current = stage.to_string();
            self.sink.stage(stage, message);
        }
    }
}

fn executable_directory() -> Result<PathBuf> {
    let executable =
        std::env::current_exe().map_err(|error| ForgeError::io("Cannot locate the Forge executable", error))?;
    Ok(executable.parent().map(Path::to_path_buf).unwrap_or_default())
}

/// The GUI's "download the HD textures first" (`_download_hd_for_install`):
/// only when the archive is missing; a failure keeps the native textures and
/// never stops the installation.
fn download_hd_textures(
    package_root: &Path,
    data_root: Option<&Path>,
    sink: &EventSink,
    cancel: &CancelToken,
) -> Result<()> {
    let pack = forge_mods::UiTexturePack::vanilla_ui_hud();
    let missing = || -> Result<Option<(PathBuf, forge_mods::ArchiveContract)>> {
        let recipes = Value::Object(crate::fsutil::load_json_object(&install::recipes_path(package_root))?);
        let Some(contract) = forge_mods::ui_pack::pack_contract(&recipes, &pack)? else { return Ok(None) };
        let data_root = install::data_root(data_root, package_root)?;
        let status = forge_mods::pack_status(package_root, &data_root, Some(&contract), &pack, cancel)?;
        Ok((status == forge_mods::Status::Missing).then_some((data_root, contract)))
    };
    // Without a contract or a readable package, the installation says why.
    let Ok(Some((data_root, contract))) = missing() else { return Ok(()) };
    sink.stage("preflight", "Downloading the HD textures...");
    let message = match fetch_archive(&data_root, &pack, &contract, sink, cancel) {
        Ok(()) => "HD texture archive ready. It is applied by this installation.".to_string(),
        Err(error) if error.kind == crate::ErrorKind::Cancelled => return Err(error),
        Err(error) => format!(
            "HD texture download failed: {} The title is prepared with the native textures. Download the zip \
             from the mod page and select it with \"Select .zip...\", then prepare and install again.",
            error.message
        ),
    };
    sink.emit(json!({"event": "report", "stage": "preflight", "message": message}));
    Ok(())
}

#[cfg(feature = "https")]
fn fetch_archive(
    data_root: &Path,
    pack: &forge_mods::UiTexturePack,
    contract: &forge_mods::ArchiveContract,
    sink: &EventSink,
    cancel: &CancelToken,
) -> Result<()> {
    forge_mods::download_archive(
        data_root,
        pack,
        contract,
        None,
        &forge_mods::UreqDownloader,
        &mut |done, total| sink.progress("preflight", done, Some(total)),
        &mut |message| sink.emit(json!({"event": "report", "stage": "preflight", "message": message})),
        cancel,
    )
    .map(|_| ())
}

#[cfg(not(feature = "https"))]
fn fetch_archive(
    _: &Path,
    _: &forge_mods::UiTexturePack,
    _: &forge_mods::ArchiveContract,
    _: &EventSink,
    _: &CancelToken,
) -> Result<()> {
    Err(ForgeError::new(crate::ErrorKind::UnsupportedFormat, "This Forge build cannot download."))
}

fn temporary_sibling(destination: &Path) -> Result<PathBuf> {
    let name =
        destination.file_name().ok_or_else(|| ForgeError::invalid("Destination has no file name"))?.to_string_lossy();
    Ok(destination.with_file_name(format!(".{name}.{}.partial", std::process::id())))
}

struct ProgressReader<'a, R> {
    inner: R,
    sink: &'a EventSink,
    done: u64,
    reported: u64,
    total: Option<u64>,
}

impl<R: Read> Read for ProgressReader<'_, R> {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        let read = self.inner.read(buffer)?;
        self.done += read as u64;
        if read == 0 || self.done - self.reported >= 16 * 1024 * 1024 {
            self.reported = self.done;
            self.sink.progress("spool", self.done, self.total);
        }
        Ok(read)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run_request(request: Value) -> Vec<Value> {
        let mut job = start(&crate::legacy_json::compact_line_bytes(&request)).unwrap();
        job.join();
        std::iter::from_fn(|| job.poll(0)).collect()
    }

    #[test]
    fn spool_copy_hashes_and_publishes() {
        let directory = tempfile::tempdir().unwrap();
        let source = directory.path().join("in.bin");
        let destination = directory.path().join("out.bin");
        fs::write(&source, vec![3u8; 100_000]).unwrap();
        let events = run_request(json!({
            "format": REQUEST_FORMAT, "job": "spool_copy",
            "source": {"path": source}, "destination": destination, "max_bytes": 200_000,
        }));
        let terminal = events.last().unwrap();
        assert_eq!(terminal["event"], "succeeded");
        assert_eq!(terminal["result"]["bytes"], 100_000);
        assert_eq!(terminal["result"]["sha256"], crate::hashing::sha256_hex(&vec![3u8; 100_000]));
        assert_eq!(fs::read(&destination).unwrap().len(), 100_000);
    }

    #[test]
    fn spool_copy_rejects_oversized_input_and_leaves_nothing() {
        let directory = tempfile::tempdir().unwrap();
        let source = directory.path().join("in.bin");
        let destination = directory.path().join("out.bin");
        fs::write(&source, vec![1u8; 1000]).unwrap();
        let events = run_request(json!({
            "format": REQUEST_FORMAT, "job": "spool_copy",
            "source": {"path": source}, "destination": destination, "max_bytes": 999,
        }));
        assert_eq!(events.last().unwrap()["event"], "failed");
        assert_eq!(events.last().unwrap()["error"]["code"], "invalid_input");
        assert_eq!(fs::read_dir(directory.path()).unwrap().count(), 1);
    }

    #[test]
    fn install_requests_name_their_source_kind() {
        let request = |extra: Value| {
            let mut request = json!({"format": REQUEST_FORMAT, "job": "install", "source": {"path": "x.cci"}});
            request.as_object_mut().unwrap().extend(extra.as_object().unwrap().clone());
            start(&crate::legacy_json::compact_line_bytes(&request)).err().map(|error| error.message)
        };
        assert_eq!(request(json!({})).unwrap(), "source_kind must be \"rom\" or \"extracted_directory\"");
        assert_eq!(
            request(json!({"source_kind": "rom", "download_hd_textures": "yes"})).unwrap(),
            "download_hd_textures must be a boolean"
        );
    }

    #[test]
    fn inner_reports_map_to_contract_stages() {
        let mut job = Job::spawn(|sink, _| {
            let mut events = StageEvents { sink, current: String::new() };
            for (stage, message) in [
                ("activate", "Installing"),
                ("topscreen", "a"),
                ("water", "b"),
                ("interior-sun", "c"),
                ("pipelines", "d"),
                ("ready", "done"),
            ] {
                events.report(stage, message);
            }
            Ok(json!({}))
        });
        job.join();
        let events: Vec<Value> = std::iter::from_fn(|| job.poll(0)).collect();
        let kinds: Vec<(&str, &str)> =
            events.iter().map(|e| (e["event"].as_str().unwrap(), e["stage"].as_str().unwrap_or(""))).collect();
        assert_eq!(
            kinds,
            [
                ("stage", "activate"),
                ("stage", "assets"),
                ("report", "assets"),
                ("stage", "shaders"),
                ("report", "shaders"),
                ("stage", "ready"),
                ("succeeded", ""),
            ]
        );
    }

    #[test]
    fn unknown_requests_fail_without_a_job() {
        assert!(start(b"{\"format\":\"triaevum_forge_request_v1\",\"job\":\"x\"}").is_err());
        assert!(start(b"[]").is_err());
    }
}
