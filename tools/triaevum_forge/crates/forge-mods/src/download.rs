// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! HTTPS downloads: the catalogued-archive downloader (`romfs_mods.download_archive`)
//! and the generic bounded downloader (`verified_download.download_verified`),
//! both over the [`Downloader`] transport trait so tests, Android and other
//! hosts can plug their own client.
//!
//! Deliberate difference from Python: the catalogued downloader now requires
//! the final redirect target to stay HTTPS, as `download_verified` always did
//! (`FORGE_SPEC.md` section 2.7).

use std::fs;
use std::io::{ErrorKind as IoKind, Read, Write};
use std::path::Path;
use std::time::{Duration, Instant};

use forge_base::hashing::hex;
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use sha2::{Digest, Sha256};

use crate::archive::{keep_archive, part_file, ArchiveContract};
use crate::catalog::CataloguedArchive;
use crate::util::io_error;

const READ_BYTES: usize = 1024 * 1024;
/// Per-open timeout of the catalogued downloader.
pub const ARCHIVE_OPEN_TIMEOUT: Duration = Duration::from_secs(60);
/// `urllib` User-Agent of the catalogued downloader.
pub const ARCHIVE_USER_AGENT: &str = "TriAevumForge";
/// User-Agent of the generic verified downloader.
pub const VERIFIED_USER_AGENT: &str = "TriAevum-Forge";

pub struct HttpRequest<'a> {
    pub url: &'a str,
    pub user_agent: &'a str,
    /// Bound on connecting and receiving the response head, and on each read.
    pub timeout: Duration,
    /// Bound on the whole transfer, when the caller has one.
    pub deadline: Option<Instant>,
}

pub struct HttpResponse {
    /// The URL after redirects.
    pub final_url: String,
    pub body: Box<dyn Read + Send>,
}

/// Transport failures, split as the retry policy needs them.
#[derive(Debug)]
pub enum HttpError {
    /// The server answered with an HTTP error status.
    Status { code: u16, message: String },
    /// Connection, TLS, timeout or protocol failure.
    Transport(String),
}

impl std::fmt::Display for HttpError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Status { code, message } if message.is_empty() => write!(formatter, "HTTP Error {code}"),
            Self::Status { code, message } => write!(formatter, "HTTP Error {code}: {message}"),
            Self::Transport(message) => formatter.write_str(message),
        }
    }
}

/// A blocking HTTP GET that follows redirects. Implementations must send the
/// request's User-Agent and verify TLS certificates.
pub trait Downloader: Send + Sync {
    fn open(&self, request: &HttpRequest<'_>) -> std::result::Result<HttpResponse, HttpError>;
}

fn insecure_url(url: &str) -> bool {
    !url.get(..8).is_some_and(|prefix| prefix.eq_ignore_ascii_case("https://"))
}

fn too_large(archive: &dyn CataloguedArchive) -> ForgeError {
    ForgeError::integrity(format!(
        "the file is larger than {} {}: the source may offer another release",
        archive.title(),
        archive.version()
    ))
}

fn download_once(
    url: &str,
    data_root: &Path,
    contract: &ArchiveContract,
    archive: &dyn CataloguedArchive,
    progress: &mut dyn FnMut(u64, u64),
    downloader: &dyn Downloader,
    cancel: &CancelToken,
) -> Result<std::path::PathBuf> {
    if insecure_url(url) {
        return Err(ForgeError::invalid("Download URL is not HTTPS"));
    }
    let mut part = part_file(data_root)?;
    let request = HttpRequest { url, user_agent: ARCHIVE_USER_AGENT, timeout: ARCHIVE_OPEN_TIMEOUT, deadline: None };
    let mut response =
        downloader.open(&request).map_err(|error| ForgeError::new(ErrorKind::PreparationFailed, error.to_string()))?;
    if insecure_url(&response.final_url) {
        return Err(ForgeError::invalid("Download redirected to an insecure URL"));
    }
    let mut hasher = Sha256::new();
    let mut done = 0u64;
    let mut buffer = vec![0u8; READ_BYTES];
    loop {
        cancel.check()?;
        let read = match response.body.read(&mut buffer) {
            Ok(0) => break,
            Ok(read) => read,
            Err(error) if error.kind() == IoKind::Interrupted => continue,
            Err(error) => return Err(io_error("Download failed", error)),
        };
        done += read as u64;
        if done > contract.bytes {
            return Err(too_large(archive));
        }
        hasher.update(&buffer[..read]);
        part.write_all(&buffer[..read]).map_err(|error| io_error("Cannot write the download", error))?;
        progress(done, contract.bytes);
    }
    part.as_file().sync_all().map_err(|error| io_error("Cannot write the download", error))?;
    let digest: [u8; 32] = hasher.finalize().into();
    if done != contract.bytes || hex(&digest) != contract.sha256 {
        return Err(ForgeError::integrity(format!(
            "the file is not the {} {} archive",
            archive.title(),
            archive.version()
        )));
    }
    keep_archive(part.into_temp_path(), data_root, contract)
}

/// Downloads the original archive: the mod page's file first, then each
/// approved mirror. Only a verified file is kept; every failed source is
/// recorded and the next one tried.
#[allow(clippy::too_many_arguments)]
pub fn download_archive(
    data_root: &Path,
    archive: &dyn CataloguedArchive,
    contract: &ArchiveContract,
    urls: Option<&[String]>,
    downloader: &dyn Downloader,
    progress: &mut dyn FnMut(u64, u64),
    report: &mut dyn FnMut(&str),
    cancel: &CancelToken,
) -> Result<std::path::PathBuf> {
    let mut failures = Vec::new();
    for url in urls.unwrap_or(archive.downloads()) {
        match download_once(url, data_root, contract, archive, progress, downloader, cancel) {
            Ok(path) => return Ok(path),
            Err(error) if error.kind == ErrorKind::Cancelled => return Err(error),
            Err(error) => {
                failures.push(format!("{url}: {}", error.message));
                report(&format!("Download from {url} failed ({}); trying the next source...", error.message));
            }
        }
    }
    let message =
        if failures.is_empty() { "no download source is configured".to_string() } else { failures.join("; ") };
    Err(ForgeError::new(ErrorKind::PreparationFailed, message))
}

/// Contract and limits of [`download_verified`].
pub struct VerifiedDownload<'a> {
    pub url: &'a str,
    pub destination: &'a Path,
    pub size: u64,
    pub sha256: &'a str,
    /// 1..=3.
    pub attempts: u32,
    pub max_seconds: f64,
}

impl<'a> VerifiedDownload<'a> {
    pub fn new(url: &'a str, destination: &'a Path, size: u64, sha256: &'a str) -> Self {
        Self { url, destination, size, sha256, attempts: 3, max_seconds: 900.0 }
    }
}

const RETRYABLE_STATUS: [u16; 6] = [408, 429, 500, 502, 503, 504];

enum Attempt {
    Done,
    Retry(ForgeError),
}

fn is_lower_sha256(text: &str) -> bool {
    forge_base::hashing::is_sha256_hex(text)
}

fn attempt_download(
    job: &VerifiedDownload<'_>,
    deadline: Instant,
    downloader: &dyn Downloader,
    progress: &mut dyn FnMut(u64, u64),
    cancel: &CancelToken,
) -> Result<Attempt> {
    let remaining = deadline.saturating_duration_since(Instant::now());
    if remaining.is_zero() {
        return Ok(Attempt::Retry(ForgeError::new(ErrorKind::PreparationFailed, "Download exceeded its time limit")));
    }
    let request = HttpRequest {
        url: job.url,
        user_agent: VERIFIED_USER_AGENT,
        timeout: remaining.min(Duration::from_secs(30)),
        deadline: Some(deadline),
    };
    let mut response = match downloader.open(&request) {
        Ok(response) => response,
        Err(HttpError::Status { code, message }) => {
            let error = ForgeError::new(ErrorKind::PreparationFailed, HttpError::Status { code, message }.to_string());
            return if RETRYABLE_STATUS.contains(&code) { Ok(Attempt::Retry(error)) } else { Err(error) };
        }
        Err(error) => return Ok(Attempt::Retry(ForgeError::new(ErrorKind::PreparationFailed, error.to_string()))),
    };
    if insecure_url(&response.final_url) {
        return Err(ForgeError::invalid("Download redirected to an insecure URL"));
    }
    let parent = job.destination.parent().filter(|path| !path.as_os_str().is_empty()).unwrap_or(Path::new("."));
    let mut partial = tempfile::Builder::new()
        .suffix(".partial")
        .tempfile_in(parent)
        .map_err(|error| io_error(format!("Cannot write in {}", parent.display()), error))?;
    let mut hasher = Sha256::new();
    let mut count = 0u64;
    let mut buffer = vec![0u8; READ_BYTES];
    loop {
        cancel.check()?;
        let read = match response.body.read(&mut buffer) {
            Ok(0) => break,
            Ok(read) => read,
            Err(error) if error.kind() == IoKind::Interrupted => continue,
            Err(error) => return Ok(Attempt::Retry(io_error("Download failed", error))),
        };
        count += read as u64;
        if count > job.size || Instant::now() > deadline {
            return Err(ForgeError::new(ErrorKind::ArtifactIntegrity, "Download exceeded its size or time limit"));
        }
        hasher.update(&buffer[..read]);
        partial.write_all(&buffer[..read]).map_err(|error| io_error("Cannot write the download", error))?;
        progress(count, job.size);
    }
    if count != job.size {
        return Ok(Attempt::Retry(ForgeError::new(
            ErrorKind::PreparationFailed,
            format!("Incomplete download: received {count} of {} bytes", job.size),
        )));
    }
    let digest: [u8; 32] = hasher.finalize().into();
    if hex(&digest) != job.sha256 {
        return Err(ForgeError::integrity("Download failed SHA-256 integrity verification"));
    }
    partial.as_file().sync_all().map_err(|error| io_error("Cannot write the download", error))?;
    let path = partial.into_temp_path();
    path.persist(job.destination)
        .map_err(|error| io_error(format!("Cannot replace {}", job.destination.display()), error.error))?;
    Ok(Attempt::Done)
}

/// Bounded HTTPS download with atomic activation: the destination is replaced
/// only by a file of the exact size and hash. Retries transient failures
/// (network errors, truncation, HTTP 408/429/500/502/503/504) up to
/// `attempts` within `max_seconds`; integrity failures are never retried.
/// `retry` receives (next attempt number, reason).
pub fn download_verified(
    job: &VerifiedDownload<'_>,
    downloader: &dyn Downloader,
    progress: &mut dyn FnMut(u64, u64),
    retry: &mut dyn FnMut(u32, &str),
    sleep: &dyn Fn(Duration),
    cancel: &CancelToken,
) -> Result<()> {
    if insecure_url(job.url)
        || job.size == 0
        || !is_lower_sha256(job.sha256)
        || !(1..=3).contains(&job.attempts)
        || !(job.max_seconds > 0.0 && job.max_seconds.is_finite())
    {
        return Err(ForgeError::invalid("Invalid verified-download contract"));
    }
    let parent = job.destination.parent().filter(|path| !path.as_os_str().is_empty()).unwrap_or(Path::new("."));
    fs::create_dir_all(parent).map_err(|error| io_error(format!("Cannot create {}", parent.display()), error))?;
    if fs::symlink_metadata(job.destination).is_ok_and(|metadata| metadata.file_type().is_symlink()) {
        return Err(ForgeError::invalid("Download cache must not be a symbolic link"));
    }
    let deadline = Instant::now() + Duration::from_secs_f64(job.max_seconds.min(1.0e9));
    for attempt in 1..=job.attempts {
        match attempt_download(job, deadline, downloader, progress, cancel)? {
            Attempt::Done => return Ok(()),
            Attempt::Retry(error) => {
                if attempt == job.attempts || Instant::now() >= deadline {
                    return Err(error);
                }
                retry(attempt + 1, &error.message);
                sleep(
                    Duration::from_secs_f64(f64::from(attempt)).min(deadline.saturating_duration_since(Instant::now())),
                );
            }
        }
    }
    Err(ForgeError::internal("Unreachable verified-download state"))
}

#[cfg(test)]
pub(crate) mod fake {
    use super::*;
    use std::collections::VecDeque;
    use std::sync::Mutex;

    pub enum Scripted {
        Body(Vec<u8>, String),
        Fail(HttpError),
    }

    /// Serves scripted responses in order and records the requested URLs.
    pub struct ScriptedDownloader {
        script: Mutex<VecDeque<Scripted>>,
        pub requested: Mutex<Vec<String>>,
    }

    impl ScriptedDownloader {
        pub fn new(script: Vec<Scripted>) -> Self {
            Self { script: Mutex::new(script.into()), requested: Mutex::new(Vec::new()) }
        }
    }

    impl Downloader for ScriptedDownloader {
        fn open(&self, request: &HttpRequest<'_>) -> std::result::Result<HttpResponse, HttpError> {
            self.requested.lock().unwrap().push(request.url.to_string());
            match self.script.lock().unwrap().pop_front().expect("scripted response") {
                Scripted::Body(bytes, final_url) => {
                    Ok(HttpResponse { final_url, body: Box::new(std::io::Cursor::new(bytes)) })
                }
                Scripted::Fail(error) => Err(error),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::fake::*;
    use super::*;
    use crate::catalog::RomFsMod;
    use forge_base::hashing::sha256_hex;

    fn https(bytes: &[u8]) -> Scripted {
        Scripted::Body(bytes.to_vec(), "https://example.test/archive".into())
    }

    fn contract(payload: &[u8]) -> ArchiveContract {
        ArchiveContract {
            kind: "vanilla_plus_link_1_9_archive".into(),
            name: "fixture.rar".into(),
            bytes: payload.len() as u64,
            sha256: sha256_hex(payload),
        }
    }

    #[test]
    fn download_falls_back_to_the_next_source_and_keeps_only_verified_files() {
        let directory = tempfile::tempdir().unwrap();
        let data = directory.path().join("data");
        let payload = b"original archive bytes";
        let archive = RomFsMod::vanilla_plus_link();
        let urls = vec!["https://example.invalid/primary".to_string(), "https://example.invalid/mirror".to_string()];
        let fake = ScriptedDownloader::new(vec![https(b"<html>challenge</html>"), https(payload)]);
        let mut reports = Vec::new();
        let path = download_archive(
            &data,
            &archive,
            &contract(payload),
            Some(&urls),
            &fake,
            &mut |_, _| {},
            &mut |message| reports.push(message.to_string()),
            &CancelToken::new(),
        )
        .unwrap();
        assert_eq!(*fake.requested.lock().unwrap(), urls);
        assert_eq!(fs::read(&path).unwrap(), payload);
        assert!(reports[0].contains("primary failed"));
        assert_eq!(fs::read_dir(data.join("downloads")).unwrap().count(), 1);

        fs::remove_file(&path).unwrap();
        let fake = ScriptedDownloader::new(vec![https(b"original archive bytes newer"), https(b"short")]);
        let error = download_archive(
            &data,
            &archive,
            &contract(payload),
            Some(&urls),
            &fake,
            &mut |_, _| {},
            &mut |_| {},
            &CancelToken::new(),
        )
        .unwrap_err();
        assert!(error.message.contains("primary") && error.message.contains("mirror"), "{}", error.message);
        assert_eq!(fs::read_dir(data.join("downloads")).unwrap().count(), 0);
    }

    #[test]
    fn archive_download_rejects_insecure_urls_and_redirects() {
        let directory = tempfile::tempdir().unwrap();
        let archive = RomFsMod::vanilla_plus_link();
        let payload = b"abc";
        let urls = vec!["http://example.test/a".to_string(), "https://example.test/b".to_string()];
        let fake = ScriptedDownloader::new(vec![Scripted::Body(payload.to_vec(), "http://elsewhere/x".into())]);
        let error = download_archive(
            directory.path(),
            &archive,
            &contract(payload),
            Some(&urls),
            &fake,
            &mut |_, _| {},
            &mut |_| {},
            &CancelToken::new(),
        )
        .unwrap_err();
        assert!(error.message.contains("not HTTPS") && error.message.contains("insecure"));
        let none = download_archive(
            directory.path(),
            &archive,
            &contract(payload),
            Some(&[]),
            &fake,
            &mut |_, _| {},
            &mut |_| {},
            &CancelToken::new(),
        )
        .unwrap_err();
        assert_eq!(none.message, "no download source is configured");
    }

    struct Fixture {
        _directory: tempfile::TempDir,
        output: std::path::PathBuf,
        data: Vec<u8>,
    }

    impl Fixture {
        fn new() -> Self {
            let directory = tempfile::tempdir().unwrap();
            let output = directory.path().join("archive.zip");
            Self { _directory: directory, output, data: b"expected archive".to_vec() }
        }

        fn run(&self, fake: &ScriptedDownloader, retries: &mut Vec<u32>, attempts: u32) -> Result<()> {
            let sha = sha256_hex(&self.data);
            let mut job =
                VerifiedDownload::new("https://example.test/archive", &self.output, self.data.len() as u64, &sha);
            job.attempts = attempts;
            download_verified(
                &job,
                fake,
                &mut |_, _| {},
                &mut |attempt, _| retries.push(attempt),
                &|_| {},
                &CancelToken::new(),
            )
        }

        fn partials(&self) -> usize {
            fs::read_dir(self.output.parent().unwrap())
                .unwrap()
                .filter(|entry| entry.as_ref().unwrap().file_name().to_string_lossy().ends_with(".partial"))
                .count()
        }
    }

    #[test]
    fn timeout_then_truncated_then_success() {
        let fixture = Fixture::new();
        let fake = ScriptedDownloader::new(vec![
            Scripted::Fail(HttpError::Transport("slow CDN".into())),
            https(b"short"),
            https(&fixture.data),
        ]);
        let mut retries = Vec::new();
        fixture.run(&fake, &mut retries, 3).unwrap();
        assert_eq!(fake.requested.lock().unwrap().len(), 3);
        assert_eq!(retries, [2, 3]);
        assert_eq!(fs::read(&fixture.output).unwrap(), fixture.data);
        assert_eq!(fixture.partials(), 0);
    }

    #[test]
    fn integrity_failure_does_not_retry_or_replace_existing_cache() {
        let fixture = Fixture::new();
        fs::write(&fixture.output, b"old cache").unwrap();
        let fake = ScriptedDownloader::new(vec![https(&vec![b'x'; fixture.data.len()])]);
        let error = fixture.run(&fake, &mut Vec::new(), 3).unwrap_err();
        assert_eq!(error.kind, ErrorKind::ArtifactIntegrity);
        assert!(error.message.contains("SHA-256"));
        assert_eq!(fake.requested.lock().unwrap().len(), 1);
        assert_eq!(fs::read(&fixture.output).unwrap(), b"old cache");
        assert_eq!(fixture.partials(), 0);
    }

    #[test]
    fn retryable_status_and_permanent_failure() {
        for (code, calls) in [(503u16, 3usize), (429, 3), (403, 1), (404, 1)] {
            let fixture = Fixture::new();
            let script =
                (0..3).map(|_| Scripted::Fail(HttpError::Status { code, message: "CDN error".into() })).collect();
            let fake = ScriptedDownloader::new(script);
            let error = fixture.run(&fake, &mut Vec::new(), 3).unwrap_err();
            assert_eq!(error.message, format!("HTTP Error {code}: CDN error"));
            assert_eq!(fake.requested.lock().unwrap().len(), calls, "{code}");
            assert!(!fixture.output.exists());
        }
    }

    #[test]
    fn deadline_and_insecure_redirect_and_contract() {
        let fixture = Fixture::new();
        let fake = ScriptedDownloader::new(vec![]);
        let sha = sha256_hex(&fixture.data);
        let mut job = VerifiedDownload::new("https://example.test/archive", &fixture.output, 16, &sha);
        job.attempts = 1;
        job.max_seconds = 1e-9;
        let error =
            download_verified(&job, &fake, &mut |_, _| {}, &mut |_, _| {}, &|_| {}, &CancelToken::new()).unwrap_err();
        assert_eq!(error.message, "Download exceeded its time limit");
        assert!(fake.requested.lock().unwrap().is_empty());

        let fake = ScriptedDownloader::new(vec![Scripted::Body(fixture.data.clone(), "http://example.test".into())]);
        let error = fixture.run(&fake, &mut Vec::new(), 3).unwrap_err();
        assert!(error.message.contains("insecure"));
        assert!(!fixture.output.exists());

        let bad = VerifiedDownload::new("http://example.test/archive", &fixture.output, 16, &sha);
        let error =
            download_verified(&bad, &fake, &mut |_, _| {}, &mut |_, _| {}, &|_| {}, &CancelToken::new()).unwrap_err();
        assert_eq!(error.message, "Invalid verified-download contract");
    }
}
