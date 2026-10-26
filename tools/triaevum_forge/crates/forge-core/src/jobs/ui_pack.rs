// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The HD interface pack (OoT3D Vanilla UI/HUD) outside an installation,
//! for frontends that offer it on their own (the Android installer, UX plan
//! WP-09; owner decision 5: there it is always the user's choice):
//!
//! - `ui_pack_status`: whether the revision accepts the pack, whether it is
//!   missing, stored as its original archive or prepared, and its size,
//!   author and license for the frontend's texts.
//! - `ui_pack_download`: the user's download of the original archive into
//!   `D/downloads` (verified size and SHA-256; progress events; cancellable
//!   between reads). Nothing is downloaded when it is already there.
//! - `ui_pack_import`: a copy of an archive the user downloaded elsewhere,
//!   from a path or (Android) a document descriptor, kept only when it is
//!   the original.
//!
//! The next installation applies a stored archive
//! (`forge_mods::prepare_ui_texture_pack`); nothing here changes the
//! installed title. Requests: `package_root?`, `data_root?` and `android?`
//! as for `install`; `ui_pack_import` adds `source`.

use std::io::Read;
use std::path::{Path, PathBuf};

use serde_json::{json, Map, Value};

use super::{android_job, job_data_root, optional_path, package_root, Source};
use crate::abi::{EventSink, Job};
use crate::cancel::CancelToken;
use crate::error::{ForgeError, Operation, Result};
use crate::install;
use crate::log::ForgeLog;

/// The jobs of this module, as `jobs::start` dispatches them.
pub(super) const JOBS: [&str; 3] = ["ui_pack_status", "ui_pack_download", "ui_pack_import"];

#[derive(Clone, Copy, PartialEq, Eq)]
enum Kind {
    Status,
    Download,
    Import,
}

struct UiPackJob {
    kind: Kind,
    package_root: PathBuf,
    data_root: PathBuf,
    source: Option<Source>,
}

pub(super) fn start(name: &str, request: &Map<String, Value>) -> Result<Job> {
    let kind = match name {
        "ui_pack_status" => Kind::Status,
        "ui_pack_download" => Kind::Download,
        "ui_pack_import" => Kind::Import,
        other => return Err(ForgeError::invalid(format!("Unknown Forge job: {other}"))),
    };
    let package_root = package_root(request)?;
    let android = android_job(request)?;
    let data_root = job_data_root(optional_path(request, "data_root").as_deref(), &package_root, android.as_ref())
        .ok_or_else(|| ForgeError::invalid("The data folder of this installation cannot be resolved"))?;
    let source = match kind {
        Kind::Import => Some(Source::parse(request.get("source"))?),
        _ => None,
    };
    let job = UiPackJob { kind, package_root, data_root, source };
    let log = (kind != Kind::Status).then(|| ForgeLog::new(&job.data_root, name));
    Ok(Job::spawn_logged(log, move |sink, cancel| job.run(sink, cancel)))
}

impl UiPackJob {
    fn run(self, sink: &EventSink, cancel: &CancelToken) -> Result<Value> {
        let pack = forge_mods::UiTexturePack::vanilla_ui_hud();
        let recipes = Value::Object(crate::fsutil::load_json_object(&install::recipes_path(&self.package_root))?);
        let contract = forge_mods::ui_pack::pack_contract(&recipes, &pack)?;
        let Some(contract) = contract else {
            if self.kind == Kind::Status {
                return Ok(status_value(&pack, None, "unavailable"));
            }
            return Err(ForgeError::invalid(format!("This game revision does not use {} {}", pack.title, pack.version))
                .about(crate::Subject::OptionalEnhancement));
        };
        let status = |cancel: &CancelToken| {
            forge_mods::pack_status(&self.package_root, &self.data_root, Some(&contract), &pack, cancel)
        };
        match self.kind {
            Kind::Status => Ok(status_value(&pack, Some(&contract), status(cancel)?.as_str())),
            Kind::Download => {
                let current = status(cancel)?;
                if current == forge_mods::Status::Missing {
                    sink.stage("download", &format!("Downloading {} {}...", pack.title, pack.version));
                    download(&self.data_root, &pack, &contract, sink, cancel)?;
                }
                Ok(status_value(&pack, Some(&contract), status(cancel)?.as_str()))
            }
            Kind::Import => {
                sink.stage("import", &format!("Checking and copying {} {}...", pack.title, pack.version));
                let imported = match self.source {
                    Some(Source::Path(path)) => {
                        forge_mods::import_archive(&path, &self.data_root, &pack, &contract, cancel)
                    }
                    #[cfg(unix)]
                    Some(source @ Source::Descriptor(_)) => {
                        let mut reader = Progress::new(source.open()?, sink, contract.bytes);
                        forge_mods::import_archive_reader(&mut reader, &self.data_root, &pack, &contract, cancel)
                    }
                    None => Err(ForgeError::invalid("Request has no source")),
                };
                imported.map_err(|error| error.during(Operation::OptionalImport))?;
                Ok(status_value(&pack, Some(&contract), status(cancel)?.as_str()))
            }
        }
    }
}

/// The pack's facts for a frontend: `status` ("unavailable" when the
/// revision does not accept it), `bytes` and `archive_name` of the original
/// archive, its author, page and license, and whether this build downloads.
fn status_value(pack: &forge_mods::UiTexturePack, contract: Option<&forge_mods::ArchiveContract>, status: &str) -> Value {
    json!({
        "status": status,
        "title": pack.title,
        "version": pack.version,
        "author": pack.author,
        "page": pack.page,
        "license_name": pack.license_name,
        "license_url": pack.license_url,
        "bytes": contract.map(|contract| contract.bytes),
        "archive_name": contract.map(|contract| contract.name.clone()),
        "can_download": cfg!(feature = "https"),
    })
}

#[cfg(feature = "https")]
fn download(
    data_root: &Path,
    pack: &forge_mods::UiTexturePack,
    contract: &forge_mods::ArchiveContract,
    sink: &EventSink,
    cancel: &CancelToken,
) -> Result<()> {
    let mut reported = 0u64;
    forge_mods::download_archive(
        data_root,
        pack,
        contract,
        None,
        &forge_mods::UreqDownloader,
        &mut |done, total| {
            if done == total || done.saturating_sub(reported) >= PROGRESS_STEP || done < reported {
                reported = done;
                sink.progress("download", done, Some(total));
            }
        },
        &mut |message| sink.emit(json!({"event": "report", "stage": "download", "message": message})),
        cancel,
    )
    .map(|_| ())
}

#[cfg(not(feature = "https"))]
fn download(
    _: &Path,
    _: &forge_mods::UiTexturePack,
    _: &forge_mods::ArchiveContract,
    _: &EventSink,
    _: &CancelToken,
) -> Result<()> {
    Err(ForgeError::new(crate::ErrorKind::UnsupportedFormat, "This Forge build cannot download.")
        .during(Operation::OptionalDownload))
}

/// Progress events at most every this many bytes.
const PROGRESS_STEP: u64 = 4 * 1024 * 1024;

/// A reader that reports how much of the archive it has read.
#[cfg_attr(not(unix), allow(dead_code))]
struct Progress<'a, R> {
    inner: R,
    sink: &'a EventSink,
    total: u64,
    done: u64,
    reported: u64,
}

#[cfg_attr(not(unix), allow(dead_code))]
impl<'a, R: Read> Progress<'a, R> {
    fn new(inner: R, sink: &'a EventSink, total: u64) -> Self {
        Self { inner, sink, total, done: 0, reported: 0 }
    }
}

impl<R: Read> Read for Progress<'_, R> {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        let read = self.inner.read(buffer)?;
        self.done += read as u64;
        if self.done - self.reported >= PROGRESS_STEP || (read == 0 && self.done != self.reported) {
            self.reported = self.done;
            self.sink.progress("import", self.done, Some(self.total));
        }
        Ok(read)
    }
}

#[cfg(test)]
mod tests {
    use super::super::start;
    use serde_json::{json, Value};

    fn run(request: Value) -> Vec<Value> {
        let job = start(serde_json::to_string(&request).unwrap().as_bytes()).unwrap();
        let mut events = Vec::new();
        loop {
            let Some(event) = job.poll(5000) else { continue };
            let done = matches!(event["event"].as_str(), Some("succeeded" | "failed" | "cancelled"));
            events.push(event);
            if done {
                return events;
            }
        }
    }

    fn request(job: &str, package: &std::path::Path, data: &std::path::Path) -> Value {
        json!({"format": "triaevum_forge_request_v1", "job": job,
               "package_root": package.to_string_lossy(), "data_root": data.to_string_lossy()})
    }

    fn package_with(payload: Option<&[u8]>) -> tempfile::TempDir {
        let root = tempfile::tempdir().unwrap();
        let mut recipe = json!({"id": "oot3d-test"});
        if let Some(payload) = payload {
            recipe["optional_inputs"] = json!({"vanilla_ui_hud_3_2_archive": {
                "name": "pack.zip", "bytes": payload.len(), "sha256": forge_base::hashing::sha256_hex(payload)}});
        }
        let recipes = install::recipes_path(root.path());
        std::fs::create_dir_all(recipes.parent().unwrap()).unwrap();
        std::fs::write(&recipes, serde_json::to_vec(&json!({"recipes": [recipe]})).unwrap()).unwrap();
        root
    }

    use crate::install;

    #[test]
    fn status_reports_the_revision_contract() {
        let payload = b"original pack archive";
        let package = package_with(Some(payload));
        let data = tempfile::tempdir().unwrap();
        let events = run(request("ui_pack_status", package.path(), data.path()));
        let result = &events.last().unwrap()["result"];
        assert_eq!(result["status"], "missing", "{events:?}");
        assert_eq!(result["bytes"], payload.len());
        assert_eq!(result["archive_name"], "pack.zip");
        assert_eq!(result["license_name"], "CC BY-NC-ND 4.0");

        let without = package_with(None);
        let events = run(request("ui_pack_status", without.path(), data.path()));
        assert_eq!(events.last().unwrap()["result"]["status"], "unavailable");
        let events = run(request("ui_pack_download", without.path(), data.path()));
        assert_eq!(events.last().unwrap()["event"], "failed");
    }

    #[test]
    fn import_keeps_only_the_original_archive() {
        let payload = b"original pack archive";
        let package = package_with(Some(payload));
        let data = tempfile::tempdir().unwrap();
        let picked = data.path().join("picked.zip");
        std::fs::write(&picked, b"another archive").unwrap();
        let mut import = request("ui_pack_import", package.path(), data.path());
        import["source"] = json!({"path": picked.to_string_lossy()});
        let events = run(import.clone());
        let last = events.last().unwrap();
        assert_eq!(last["event"], "failed", "{events:?}");
        assert_eq!(last["error"]["operation"], "optional_import", "{last}");

        std::fs::write(&picked, payload).unwrap();
        let events = run(import);
        assert_eq!(events.last().unwrap()["result"]["status"], "archive", "{events:?}");
        assert_eq!(std::fs::read(data.path().join("downloads/pack.zip")).unwrap(), payload);
        // Already stored: the download job has nothing to fetch.
        let events = run(request("ui_pack_download", package.path(), data.path()));
        assert_eq!(events.last().unwrap()["result"]["status"], "archive", "{events:?}");
        assert!(events.iter().all(|event| event["event"] != "stage"), "{events:?}");
    }

    #[cfg(unix)]
    #[test]
    fn import_reads_a_descriptor() {
        use std::os::fd::IntoRawFd;
        let payload = b"original pack archive";
        let package = package_with(Some(payload));
        let data = tempfile::tempdir().unwrap();
        let picked = data.path().join("picked.zip");
        std::fs::write(&picked, payload).unwrap();
        let fd = std::fs::File::open(&picked).unwrap().into_raw_fd();
        let mut import = request("ui_pack_import", package.path(), data.path());
        import["source"] = json!({"fd": fd});
        let events = run(import);
        assert_eq!(events.last().unwrap()["result"]["status"], "archive", "{events:?}");
    }
}
