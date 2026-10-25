// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! RAR/RAR5 member extraction behind a small trait. Desktop uses the system's
//! libarchive `bsdtar` as the Python Forge did; Android can plug an in-process
//! reader later. No unrar dependency.

use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use forge_base::{CancelToken, ForgeError, Result};

/// Extracts exactly the named members of an archive below `destination`
/// (members keep their archive-relative paths). Failures carry the message
/// the user sees, including the "Select extracted folder..." fallback hint.
pub trait ArchiveExtractor {
    fn extract(
        &self,
        archive: &Path,
        destination: &Path,
        members: &[&str],
        title: &str,
        cancel: &CancelToken,
    ) -> Result<()>;
}

/// Runs `command`, capturing both streams, killing it after `timeout`.
/// Returns (success, stdout, stderr).
fn run_captured(
    mut command: Command,
    timeout: Duration,
    cancel: &CancelToken,
) -> std::io::Result<(bool, String, String)> {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    let mut child = command.stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn()?;
    let drain = |stream: Option<Box<dyn Read + Send>>| {
        std::thread::spawn(move || {
            let mut text = Vec::new();
            if let Some(mut stream) = stream {
                let _ = stream.read_to_end(&mut text);
            }
            String::from_utf8_lossy(&text).into_owned()
        })
    };
    let stdout = drain(child.stdout.take().map(|s| Box::new(s) as Box<dyn Read + Send>));
    let stderr = drain(child.stderr.take().map(|s| Box::new(s) as Box<dyn Read + Send>));
    let deadline = Instant::now() + timeout;
    let status = loop {
        if let Some(status) = child.try_wait()? {
            break Some(status);
        }
        if cancel.is_cancelled() || Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            break None;
        }
        std::thread::sleep(Duration::from_millis(20));
    };
    let (out, err) = (stdout.join().unwrap_or_default(), stderr.join().unwrap_or_default());
    match status {
        Some(status) => Ok((status.success(), out, err)),
        None => Err(std::io::Error::new(std::io::ErrorKind::TimedOut, "the extraction tool did not finish in time")),
    }
}

fn find_on_path(name: &str) -> Option<PathBuf> {
    let paths = std::env::var_os("PATH")?;
    let suffixes: &[&str] = if cfg!(windows) { &[".exe", ""] } else { &[""] };
    std::env::split_paths(&paths).find_map(|directory| {
        suffixes.iter().map(|suffix| directory.join(format!("{name}{suffix}"))).find(|path| path.is_file())
    })
}

fn bsdtar_candidates() -> Vec<PathBuf> {
    if cfg!(windows) {
        // Windows 10 (1803) and later ship libarchive's bsdtar as tar.exe. Use it
        // by path: a GNU tar earlier on PATH cannot read RAR.
        let root = std::env::var_os("SystemRoot").map(PathBuf::from).unwrap_or_else(|| PathBuf::from(r"C:\Windows"));
        let system = root.join("System32").join("tar.exe");
        return if system.is_file() { vec![system] } else { Vec::new() };
    }
    ["bsdtar", "tar"].into_iter().filter_map(find_on_path).collect()
}

/// A libarchive `bsdtar`, which reads RAR (including RAR5) archives.
pub fn find_bsdtar() -> Option<PathBuf> {
    bsdtar_candidates().into_iter().find(|candidate| {
        let mut command = Command::new(candidate);
        command.arg("--version");
        matches!(run_captured(command, Duration::from_secs(30), &CancelToken::new()),
                 Ok((true, out, _)) if out.contains("libarchive"))
    })
}

/// Extraction through the system's `bsdtar`, located when first needed.
#[derive(Clone, Debug, Default)]
pub struct SystemBsdtar {
    /// Overrides detection (the Python `bsdtar=` argument).
    pub tool: Option<PathBuf>,
}

impl ArchiveExtractor for SystemBsdtar {
    fn extract(
        &self,
        archive: &Path,
        destination: &Path,
        members: &[&str],
        title: &str,
        cancel: &CancelToken,
    ) -> Result<()> {
        let tool = self.tool.clone().or_else(find_bsdtar).ok_or_else(|| {
            ForgeError::new(
                forge_base::ErrorKind::UnsupportedFormat,
                "No RAR reader is available (libarchive's bsdtar). Extract the archive with any tool and choose \
                 \"Select extracted folder...\".",
            )
        })?;
        // Named members only; bsdtar refuses absolute and ".." paths by default.
        let mut command = Command::new(&tool);
        command.arg("-x").arg("-f").arg(archive).arg("-C").arg(destination).args(members);
        let hint = |detail: &str| {
            ForgeError::invalid(format!(
                "Cannot extract the {title} archive: {detail}. Extract it with any tool and choose \"Select \
                 extracted folder...\"."
            ))
        };
        let (success, out, err) = match run_captured(command, Duration::from_secs(600), cancel) {
            Ok(result) => result,
            Err(_) if cancel.is_cancelled() => return Err(ForgeError::cancelled()),
            Err(error) => return Err(hint(&error.to_string())),
        };
        if success {
            return Ok(());
        }
        let text = if err.is_empty() { out } else { err };
        let detail = text.trim().lines().last().unwrap_or("unknown error").to_string();
        Err(hint(&detail))
    }
}
