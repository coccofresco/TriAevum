// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Job requests accepted through the C ABI and the CLI's machine interface.

use std::fs::{self, File};
use std::io::{BufWriter, Read};
use std::path::{Path, PathBuf};

use serde_json::{json, Map, Value};

use crate::abi::{EventSink, Job};
use crate::cancel::CancelToken;
use crate::error::{ForgeError, Result};
use crate::hashing::{copy_hashing, hex};

pub const REQUEST_FORMAT: &str = "triaevum_forge_request_v1";

pub fn capabilities() -> Value {
    json!({
        "format": "triaevum_forge_capabilities_v1",
        "abi_version": crate::abi::ABI_VERSION,
        "engine_version": env!("CARGO_PKG_VERSION"),
        "target": target_triple(),
        "input_formats": ["3ds", "cci", "extracted_directory"],
        "encrypted_input": false,
        "jobs": ["spool_copy"],
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
        let mut reader = ProgressReader {
            inner: self.source.open()?,
            sink,
            done: 0,
            reported: 0,
            total: self.declared_bytes,
        };
        let temporary = temporary_sibling(&self.destination)?;
        let outcome = (|| {
            let file = File::create(&temporary)
                .map_err(|error| ForgeError::io(format!("Cannot create {}", temporary.display()), error))?;
            let mut writer = BufWriter::new(file);
            // One byte past the limit detects an oversized source.
            let (bytes, digest) = copy_hashing(&mut reader, &mut writer, self.max_bytes + 1, cancel)?;
            if bytes > self.max_bytes {
                return Err(ForgeError::invalid(format!(
                    "The selected file is larger than {} bytes",
                    self.max_bytes
                )));
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

fn temporary_sibling(destination: &Path) -> Result<PathBuf> {
    let name = destination
        .file_name()
        .ok_or_else(|| ForgeError::invalid("Destination has no file name"))?
        .to_string_lossy();
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
    fn unknown_requests_fail_without_a_job() {
        assert!(start(b"{\"format\":\"triaevum_forge_request_v1\",\"job\":\"x\"}").is_err());
        assert!(start(b"[]").is_err());
    }
}
