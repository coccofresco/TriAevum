// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `native_process.py`: bounded native helper runs that are always killed and
//! reaped, and the loader environment helpers get.
//!
//! The Python version also undid PyInstaller's loader changes (frozen
//! `LD_LIBRARY_PATH`, `_MEIPASS` PATH entries, `SetDllDirectory`). A Rust
//! executable has no such contamination, so the inherited environment is
//! passed unchanged; the verified-sibling library path of helpers is kept.
//! Deliberate addition: captured output is bounded ([`MAX_CAPTURE_BYTES`] per
//! stream) and a run can be cancelled; Python read unbounded output.

use std::ffi::{OsStr, OsString};
use std::io::Read;
use std::path::Path;
use std::process::{Child, Command, ExitStatus, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use forge_base::{CancelToken, ErrorKind, ForgeError, Result};

use crate::pyerr::{decode_text, io_failure, os_error, timeout_expired};

/// Per-stream capture budget of [`run_native`]. Product, language and shader
/// helpers print kilobytes; a runaway helper is stopped instead of filling
/// memory.
pub const MAX_CAPTURE_BYTES: usize = 64 * 1024 * 1024;

const POLL: Duration = Duration::from_millis(5);

/// `subprocess.CREATE_NO_WINDOW`.
pub const CREATE_NO_WINDOW: u32 = 0x0800_0000;
/// `subprocess.BELOW_NORMAL_PRIORITY_CLASS`.
pub const BELOW_NORMAL_PRIORITY_CLASS: u32 = 0x0000_4000;

/// An environment for the child: the parent's (`native_process_environment`)
/// or an explicit replacement (`native_helper_environment`).
#[derive(Clone, Debug, Default)]
pub enum Environment {
    #[default]
    Inherit,
    Replace(Vec<(OsString, OsString)>),
}

/// How one helper runs: working directory, environment, time budget and the
/// Windows creation flags Python passed (ignored elsewhere, as `getattr(...,
/// 0)` made them).
#[derive(Clone, Debug)]
pub struct RunOptions {
    pub cwd: std::path::PathBuf,
    pub environment: Environment,
    pub timeout_seconds: f64,
    pub creation_flags: u32,
}

/// A finished helper: Python's `returncode` and the captured streams.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Completed {
    pub status: i64,
    pub stdout: Vec<u8>,
    pub stderr: Vec<u8>,
}

impl Completed {
    pub fn stdout_text(&self) -> String {
        decode_text(&self.stdout)
    }

    pub fn stderr_text(&self) -> String {
        decode_text(&self.stderr)
    }
}

/// `native_process_environment()`: the parent environment, unchanged.
pub fn native_process_environment() -> Vec<(OsString, OsString)> {
    std::env::vars_os().collect()
}

/// `native_helper_environment(executable)`: on Linux the verified helper's
/// own directory comes first in `LD_LIBRARY_PATH`, which also resolves the
/// indirect shaderc dependencies RUNPATH does not reach. Callers verify the
/// executable and its dependencies first.
pub fn native_helper_environment(executable: &Path) -> Result<Vec<(OsString, OsString)>> {
    let mut environment = native_process_environment();
    if cfg!(target_os = "linux") {
        let directory = forge_prepare::paths::resolve(executable)?
            .parent()
            .map(|parent| parent.as_os_str().to_os_string())
            .unwrap_or_default();
        if directory.to_string_lossy().contains(':') {
            return Err(ForgeError::invalid("Native helper path cannot contain a Linux loader separator"));
        }
        let key = OsString::from("LD_LIBRARY_PATH");
        let mut value = directory;
        if let Some(original) = environment.iter().find(|(name, _)| *name == key).map(|(_, value)| value.clone()) {
            if !original.is_empty() {
                value.push(":");
                value.push(original);
            }
        }
        environment.retain(|(name, _)| *name != key);
        environment.push((key, value));
    }
    Ok(environment)
}

/// `describe_exit_status(code)`: the Windows loader failures that happen
/// before any ROM processing get a diagnosis; anything else is the number.
pub fn describe_exit_status(code: i64) -> String {
    let status = (code as u64) & 0xFFFF_FFFF;
    let diagnosis = match status {
        0xC000_0139 => "a required DLL export is missing (incompatible dependency)",
        0xC000_0135 => "a required DLL is missing",
        0xC000_007B => "a dependency has the wrong executable format or architecture",
        0xC000_001D => "the CPU encountered an unsupported instruction",
        _ => return code.to_string(),
    };
    format!(
        "0x{status:08X}: {diagnosis}. This failed before ROM processing. \
         Use a freshly extracted complete release, without mixing DLLs from other builds. \
         If it persists, report your Windows version and this code"
    )
}

/// Python's `Popen.returncode`: the unsigned exit code on Windows, the code
/// or minus the signal number on POSIX.
pub fn return_code(status: ExitStatus) -> i64 {
    #[cfg(windows)]
    {
        status.code().map_or(-1, |code| i64::from(code as u32))
    }
    #[cfg(unix)]
    {
        use std::os::unix::process::ExitStatusExt;
        status.code().map(i64::from).or_else(|| status.signal().map(|signal| -i64::from(signal))).unwrap_or(-1)
    }
}

fn command(program: &[OsString], options: &RunOptions) -> Command {
    let mut command = Command::new(&program[0]);
    command.args(&program[1..]).current_dir(&options.cwd);
    if let Environment::Replace(variables) = &options.environment {
        command.env_clear().envs(variables.iter().map(|(name, value)| (name, value)));
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(options.creation_flags);
    }
    command
}

fn spawn_error(program: &OsStr, error: std::io::Error) -> ForgeError {
    // CreateProcess failures carry no file name; POSIX exec failures name the
    // executable (subprocess's child_exception filename).
    let filename = if cfg!(windows) { None } else { Some(Path::new(program)) };
    let message = os_error(&error, filename);
    io_failure(error, message)
}

/// `popen_native(...)` with the caller's stdout/stderr (files for the device
/// job). The caller owns kill/reap.
pub fn popen_native(program: &[OsString], options: &RunOptions, stdout: Stdio, stderr: Stdio) -> Result<Child> {
    command(program, options).stdout(stdout).stderr(stderr).spawn().map_err(|error| spawn_error(&program[0], error))
}

struct Capture {
    handle: JoinHandle<Vec<u8>>,
}

fn capture(mut stream: impl Read + Send + 'static, overflow: Arc<AtomicBool>) -> Capture {
    let handle = std::thread::spawn(move || {
        let mut kept = Vec::new();
        let mut buffer = [0u8; 64 * 1024];
        loop {
            match stream.read(&mut buffer) {
                Ok(0) => break,
                Ok(read) => {
                    if kept.len() + read > MAX_CAPTURE_BYTES {
                        overflow.store(true, Ordering::Release);
                    } else {
                        kept.extend_from_slice(&buffer[..read]);
                    }
                }
                Err(error) if error.kind() == std::io::ErrorKind::Interrupted => {}
                Err(_) => break,
            }
        }
        kept
    });
    Capture { handle }
}

fn reap(child: &mut Child) {
    if !matches!(child.try_wait(), Ok(Some(_))) {
        let _ = child.kill();
    }
    let _ = child.wait();
}

/// `run_native(command, capture_output=True, timeout=...)`: runs to
/// completion, or kills and reaps the helper on timeout, cancellation or
/// output overflow. Like `communicate`, the budget also covers reading both
/// streams to their end (a descendant holding a pipe counts).
pub fn run_native(program: &[OsString], options: &RunOptions, cancel: Option<&CancelToken>) -> Result<Completed> {
    let mut child = command(program, options)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|error| spawn_error(&program[0], error))?;
    let overflow = Arc::new(AtomicBool::new(false));
    let stdout = capture(child.stdout.take().expect("piped stdout"), overflow.clone());
    let stderr = capture(child.stderr.take().expect("piped stderr"), overflow.clone());
    let deadline = Instant::now() + Duration::from_secs_f64(options.timeout_seconds.max(0.0));
    let mut status = None;
    let failure = loop {
        if status.is_none() {
            match child.try_wait() {
                Ok(Some(exit)) => status = Some(exit),
                Ok(None) => {}
                Err(error) => {
                    let message = os_error(&error, None);
                    break Some(io_failure(error, message));
                }
            }
        }
        if status.is_some() && stdout.handle.is_finished() && stderr.handle.is_finished() {
            break None;
        }
        if overflow.load(Ordering::Acquire) {
            break Some(ForgeError::new(
                ErrorKind::PreparationFailed,
                "Native helper output exceeded its diagnostic budget",
            ));
        }
        if cancel.is_some_and(CancelToken::is_cancelled) {
            break Some(ForgeError::cancelled());
        }
        if Instant::now() >= deadline {
            break Some(ForgeError::new(
                ErrorKind::PreparationFailed,
                timeout_expired(program, options.timeout_seconds),
            ));
        }
        std::thread::sleep(POLL);
    };
    reap(&mut child);
    if let Some(error) = failure {
        // A descendant may still hold a pipe; its reader ends with it.
        return Err(error);
    }
    let status = status.map(return_code).unwrap_or(-1);
    let stdout = stdout.handle.join().unwrap_or_default();
    let stderr = stderr.handle.join().unwrap_or_default();
    Ok(Completed { status, stdout, stderr })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn loader_failures_are_diagnosed() {
        for code in [3_221_225_785_i64, -1_073_741_511] {
            let text = describe_exit_status(code);
            assert!(text.starts_with("0xC0000139: a required DLL export is missing"), "{text}");
            assert!(text.contains("before ROM processing"));
        }
        assert_eq!(describe_exit_status(3), "3");
        assert_eq!(describe_exit_status(-9), "-9");
    }

    #[test]
    fn helper_environment_keeps_parent_variables() {
        let environment = native_helper_environment(Path::new("helper")).unwrap();
        assert!(environment.iter().any(|(name, _)| name == "PATH" || name == "Path"));
    }
}
