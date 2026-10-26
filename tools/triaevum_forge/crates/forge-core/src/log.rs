// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Forge log, `D/logs/forge.log`: every job's stages, notes, result and
//! error details, one line each, with user names removed from paths
//! ([`crate::redact`]). It rotates at 1 MB and keeps five files
//! (`forge.log`, `forge.log.1` ... `forge.log.4`). Logging never fails a
//! job: a log that cannot be written is skipped.

use std::fs::OpenOptions;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use serde_json::Value;

use crate::error::ForgeError;

pub const LOGS_DIRECTORY: &str = "logs";
pub const LOG_FILE: &str = "forge.log";
pub const MAX_BYTES: u64 = 1024 * 1024;
/// `forge.log` plus four rotated files.
pub const KEPT_FILES: usize = 5;
/// The runtime preflight diagnostic (`triaevum_preflight_error_v1`).
pub const PREFLIGHT_ERROR_FILE: &str = "preflight-error.json";

/// One writer at a time in this process, so rotation and appends interleave
/// cleanly between jobs.
static WRITER: Mutex<()> = Mutex::new(());

/// `D/logs`.
pub fn logs_directory(data_root: &Path) -> PathBuf {
    data_root.join(LOGS_DIRECTORY)
}

/// Where a failed runtime query writes its diagnostic for this data folder.
pub fn preflight_error_path(data_root: &Path) -> PathBuf {
    logs_directory(data_root).join(PREFLIGHT_ERROR_FILE)
}

/// The log of one job in one data folder.
#[derive(Clone, Debug)]
pub struct ForgeLog {
    path: PathBuf,
    job: String,
}

impl ForgeLog {
    pub fn new(data_root: &Path, job: &str) -> Self {
        Self { path: logs_directory(data_root).join(LOG_FILE), job: job.to_string() }
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    /// Appends one redacted line; failures are ignored.
    pub fn line(&self, text: &str) {
        let text = crate::redact::redact(text).replace(['\r', '\n'], " | ");
        let line = format!("{} [{}] {text}\n", timestamp(std::time::SystemTime::now()), self.job);
        let _guard = WRITER.lock().unwrap_or_else(|poison| poison.into_inner());
        let _ = append(&self.path, line.as_bytes());
    }

    /// A job event; byte progress is not logged.
    pub fn event(&self, event: &Value) {
        let text = |key: &str| event.get(key).and_then(Value::as_str).unwrap_or_default();
        match text("event") {
            "stage" | "report" => self.line(&format!("{} {}: {}", text("event"), text("stage"), text("message"))),
            "failed" => self.line(&failure_line(event.get("error").unwrap_or(&Value::Null))),
            "cancelled" => self.line("cancelled"),
            "succeeded" => {
                let notices: Vec<&str> = event
                    .get("notices")
                    .and_then(Value::as_array)
                    .map(|notices| notices.iter().filter_map(|notice| notice["outcome"].as_str()).collect())
                    .unwrap_or_default();
                if notices.is_empty() {
                    self.line("succeeded");
                } else {
                    self.line(&format!("succeeded with notices: {}", notices.join(", ")));
                }
            }
            _ => {}
        }
    }

    /// An error outside a job (for example a failed launch).
    pub fn error(&self, error: &ForgeError) {
        self.event(&crate::abi::error_event(error));
    }
}

fn failure_line(error: &Value) -> String {
    let mut line = String::from("failed");
    for key in ["code", "stage", "operation", "subject", "reason", "os_code"] {
        match error.get(key) {
            None | Some(Value::Null) => {}
            Some(Value::String(text)) => line.push_str(&format!(" {key}={text}")),
            Some(other) => line.push_str(&format!(" {key}={other}")),
        }
    }
    let message = error.get("message").and_then(Value::as_str).unwrap_or_default();
    format!("{line}: {message}")
}

fn rotated(path: &Path, index: usize) -> PathBuf {
    let mut name = path.file_name().unwrap_or_default().to_os_string();
    name.push(format!(".{index}"));
    path.with_file_name(name)
}

fn rotate(path: &Path) -> std::io::Result<()> {
    let _ = std::fs::remove_file(rotated(path, KEPT_FILES - 1));
    for index in (1..KEPT_FILES - 1).rev() {
        let from = rotated(path, index);
        if from.exists() {
            std::fs::rename(&from, rotated(path, index + 1))?;
        }
    }
    std::fs::rename(path, rotated(path, 1))
}

fn append(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let size = std::fs::metadata(path).map(|metadata| metadata.len()).unwrap_or(0);
    if size > 0 && size + bytes.len() as u64 > MAX_BYTES {
        rotate(path)?;
    }
    OpenOptions::new().create(true).append(true).open(path)?.write_all(bytes)
}

/// `YYYY-MM-DDTHH:MM:SSZ` (UTC).
pub fn timestamp(time: std::time::SystemTime) -> String {
    let seconds = time.duration_since(std::time::UNIX_EPOCH).map(|elapsed| elapsed.as_secs()).unwrap_or(0) as i64;
    let (days, of_day) = (seconds.div_euclid(86_400), seconds.rem_euclid(86_400));
    // Civil date from days since 1970-01-01 (H. Hinnant's algorithm).
    let shifted = days + 719_468;
    let era = shifted.div_euclid(146_097);
    let day_of_era = shifted - era * 146_097;
    let year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36_524 - day_of_era / 146_096) / 365;
    let day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    let month_index = (5 * day_of_year + 2) / 153;
    let day = day_of_year - (153 * month_index + 2) / 5 + 1;
    let month = if month_index < 10 { month_index + 3 } else { month_index - 9 };
    let year = year_of_era + era * 400 + i64::from(month <= 2);
    format!("{year:04}-{month:02}-{day:02}T{:02}:{:02}:{:02}Z", of_day / 3600, of_day % 3600 / 60, of_day % 60)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn timestamps_are_utc_dates() {
        let at = |seconds: u64| timestamp(std::time::UNIX_EPOCH + std::time::Duration::from_secs(seconds));
        assert_eq!(at(0), "1970-01-01T00:00:00Z");
        assert_eq!(at(951_782_400), "2000-02-29T00:00:00Z");
        assert_eq!(at(1_793_016_245), "2026-10-26T12:04:05Z");
    }

    #[test]
    fn rotates_at_one_megabyte_and_keeps_five_files() {
        let directory = tempfile::tempdir().unwrap();
        let log = ForgeLog::new(directory.path(), "install");
        let filler = "x".repeat(100_000);
        for _ in 0..60 {
            log.line(&filler);
        }
        let logs = logs_directory(directory.path());
        let mut names: Vec<String> =
            std::fs::read_dir(&logs).unwrap().map(|entry| entry.unwrap().file_name().into_string().unwrap()).collect();
        names.sort();
        assert_eq!(names, ["forge.log", "forge.log.1", "forge.log.2", "forge.log.3", "forge.log.4"]);
        for name in names {
            assert!(std::fs::metadata(logs.join(name)).unwrap().len() <= MAX_BYTES);
        }
    }

    #[test]
    fn events_are_logged_without_progress() {
        let directory = tempfile::tempdir().unwrap();
        let log = ForgeLog::new(directory.path(), "install");
        log.event(&json!({"event": "stage", "stage": "verify", "message": "Identifying"}));
        log.event(&json!({"event": "progress", "stage": "extract", "done": 1, "total": 2}));
        log.event(&json!({"event": "failed", "error": {"code": "storage_full", "stage": "extract",
            "message": "Cannot write", "os_code": 112, "retryable": true, "reason": "storage_full"}}));
        let text = std::fs::read_to_string(log.path()).unwrap();
        let lines: Vec<&str> = text.lines().collect();
        assert_eq!(lines.len(), 2);
        assert!(lines[0].ends_with("[install] stage verify: Identifying"), "{}", lines[0]);
        assert!(
            lines[1].ends_with("failed code=storage_full stage=extract reason=storage_full os_code=112: Cannot write"),
            "{}",
            lines[1]
        );
    }
}
