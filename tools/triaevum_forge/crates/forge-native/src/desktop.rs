// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Desktop [`NativeServices`]: the catalog-verified runtime and renderer
//! helpers run as child processes with the executables, arguments, working
//! directories, environments, creation flags and time budgets the Python
//! Forge used (`product_contract.py`, `game_language.py`, `interior_sun.py`,
//! `shader_preparation._run`, `device_pipeline_preparation._run`).

use std::ffi::OsString;
use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::process::Stdio;
use std::time::{Duration, Instant};

use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_catalog::pyjson::py_str;
use serde_json::Value;

use crate::process::{
    native_helper_environment, popen_native, return_code, run_native, Environment, RunOptions,
    BELOW_NORMAL_PRIORITY_CLASS, CREATE_NO_WINDOW,
};
use crate::pyerr::{decode_text, io_failure, open_error, os_error};
use crate::services::{
    DeviceJob, InteriorSunRequest, LanguageQuery, NativeOutput, NativeServices, PortableCompile, RendererPassRequest,
    Report, TransferableImport,
};

pub const PRODUCT_TIMEOUT_SECONDS: f64 = 15.0;
pub const LANGUAGE_TIMEOUT_SECONDS: f64 = 30.0;
pub const INTERIOR_SUN_TIMEOUT_SECONDS: f64 = 3600.0;
pub const SHADER_TIMEOUT_SECONDS: f64 = 600.0;
/// Diagnostic budget of the device job's stdout/stderr logs.
pub const DEVICE_LOG_BYTES: u64 = 16 * 1024 * 1024;
/// Longest pending progress record.
pub const DEVICE_PENDING_BYTES: usize = 65536;
/// Grace period after the cancel file before the helper is killed.
pub const DEVICE_CANCEL_GRACE: Duration = Duration::from_secs(10);
const DEVICE_POLL: Duration = Duration::from_millis(50);

/// Helper processes, as the Python Forge launched them.
#[derive(Clone, Copy, Debug, Default)]
pub struct DesktopServices;

fn program(executable: &Path, arguments: Vec<OsString>) -> Vec<OsString> {
    let mut command = vec![executable.as_os_str().to_os_string()];
    command.extend(arguments);
    command
}

fn parent(path: &Path) -> std::path::PathBuf {
    path.parent().map(Path::to_path_buf).unwrap_or_default()
}

fn output(completed: crate::process::Completed) -> NativeOutput {
    NativeOutput { status: completed.status, stdout: completed.stdout_text(), stderr: completed.stderr_text() }
}

/// `shader_preparation._run` / `interior_sun` options: package root as
/// working directory, verified-sibling library path, hidden window.
fn helper_options(executable: &Path, root: &Path, timeout_seconds: f64, creation_flags: u32) -> Result<RunOptions> {
    Ok(RunOptions {
        cwd: root.to_path_buf(),
        environment: Environment::Replace(native_helper_environment(executable)?),
        timeout_seconds,
        creation_flags,
    })
}

fn run_shader_tool(executable: &Path, root: &Path, arguments: Vec<OsString>) -> Result<NativeOutput> {
    let options =
        helper_options(executable, root, SHADER_TIMEOUT_SECONDS, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS)?;
    run_native(&program(executable, arguments), &options, None).map(output)
}

impl NativeServices for DesktopServices {
    fn product_info(&self, runtime: &Path, plugin: Option<&Path>) -> Result<NativeOutput> {
        let arguments = match plugin {
            None => vec!["--product-info".into()],
            Some(plugin) => vec!["--verify-title-plugin".into(), plugin.as_os_str().to_os_string()],
        };
        let options = RunOptions {
            cwd: parent(runtime),
            environment: Environment::Inherit,
            timeout_seconds: PRODUCT_TIMEOUT_SECONDS,
            creation_flags: 0,
        };
        run_native(&program(runtime, arguments), &options, None).map(output)
    }

    fn game_language_info(&self, runtime: &Path, query: &LanguageQuery) -> Result<NativeOutput> {
        let options = RunOptions {
            cwd: parent(runtime),
            environment: Environment::Inherit,
            timeout_seconds: LANGUAGE_TIMEOUT_SECONDS,
            creation_flags: 0,
        };
        run_native(&program(runtime, query.arguments()), &options, None).map(output)
    }

    fn prebuild_interior_sun(&self, request: &InteriorSunRequest, cancel: &CancelToken) -> Result<NativeOutput> {
        let options = helper_options(&request.runtime, &request.root, INTERIOR_SUN_TIMEOUT_SECONDS, CREATE_NO_WINDOW)?;
        run_native(&program(&request.runtime, request.arguments()), &options, Some(cancel)).map(output)
    }

    fn prepare_renderer_passes(&self, request: &RendererPassRequest) -> Result<NativeOutput> {
        run_shader_tool(&request.compiler, &request.root, request.arguments())
    }

    fn import_transferable_shaders(&self, request: &TransferableImport) -> Result<NativeOutput> {
        run_shader_tool(&request.importer, &request.root, request.arguments())
    }

    fn compile_portable_shaders(&self, request: &PortableCompile) -> Result<NativeOutput> {
        run_shader_tool(&request.compiler, &request.root, request.arguments())
    }

    fn prepare_device_pipelines(&self, job: &DeviceJob, cancel: &CancelToken, report: Report<'_>) -> Result<i64> {
        let mut arguments = job.arguments();
        if let Some(mode) = &job.tool_mode {
            arguments.insert(0, mode.into());
        }
        run_device_helper(
            &program(&job.helper, arguments),
            &job.root,
            &job.stage,
            cancel,
            report,
            job.timeout_seconds,
        )
    }
}

fn failed(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::PreparationFailed, message)
}

fn create_log(path: &Path) -> Result<File> {
    File::create(path).map_err(|error| {
        let message = open_error(&error, path);
        io_failure(error, message)
    })
}

/// Reads complete lines of the helper's stdout log as it grows and reports
/// `pipeline_progress` records.
pub(crate) struct ProgressReader {
    file: File,
    pending: Vec<u8>,
}

impl ProgressReader {
    pub(crate) fn new(file: File) -> Self {
        Self { file, pending: Vec::new() }
    }

    /// The log's current length.
    pub(crate) fn size(&self) -> u64 {
        file_size(&self.file)
    }

    pub(crate) fn poll(&mut self, report: &mut dyn FnMut(&str, &str)) -> Result<()> {
        let mut chunk = vec![0u8; DEVICE_PENDING_BYTES];
        let read = self.file.read(&mut chunk).map_err(|error| {
            let message = os_error(&error, None);
            io_failure(error, message)
        })?;
        self.pending.extend_from_slice(&chunk[..read]);
        let complete = match self.pending.iter().rposition(|byte| *byte == b'\n') {
            Some(index) => self.pending.drain(..=index).collect::<Vec<u8>>(),
            None => Vec::new(),
        };
        if self.pending.len() > DEVICE_PENDING_BYTES {
            return Err(failed("Invalid pipeline progress record"));
        }
        for line in decode_text(&complete).split('\n') {
            let Ok(Value::Object(record)) = serde_json::from_str::<Value>(line) else { continue };
            if record.get("event").and_then(Value::as_str) == Some("pipeline_progress") {
                let attempted = py_str(record.get("attempted").unwrap_or(&Value::Null));
                let total = py_str(record.get("total").unwrap_or(&Value::Null));
                report("pipelines", &format!("Preparing GPU pipelines: {attempted}/{total}"));
            }
        }
        Ok(())
    }
}

/// The log's current length, through a separate read handle (the child's
/// handle shares its file position on Windows and must not be moved).
fn file_size(file: &File) -> u64 {
    file.metadata().map_or(0, |metadata| metadata.len())
}

/// `device_pipeline_preparation._run`: the helper runs below normal priority
/// with its streams in `stage/stdout.log` and `stage/stderr.log`; progress
/// is read from the log every 50 ms. Cancellation creates `stage/cancel`
/// and allows a 10 s grace period. The helper is always killed and reaped.
pub fn run_device_helper(
    command: &[OsString],
    root: &Path,
    stage: &Path,
    cancel: &CancelToken,
    report: Report<'_>,
    timeout_seconds: f64,
) -> Result<i64> {
    let stdout_path = stage.join("stdout.log");
    let stderr_path = stage.join("stderr.log");
    let output = create_log(&stdout_path)?;
    let errors = create_log(&stderr_path)?;
    let mut progress = ProgressReader::new(File::open(&stdout_path).map_err(|error| {
        let message = open_error(&error, &stdout_path);
        io_failure(error, message)
    })?);
    let errors_size = File::open(&stderr_path).map_err(|error| {
        let message = open_error(&error, &stderr_path);
        io_failure(error, message)
    })?;
    let started = Instant::now();
    let budget = Duration::from_secs_f64(timeout_seconds.max(0.0));
    let options =
        helper_options(Path::new(&command[0]), root, timeout_seconds, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS)?;
    let mut child = popen_native(command, &options, Stdio::from(output), Stdio::from(errors))?;
    let mut cancel_at: Option<Instant> = None;
    let result = (|| loop {
        match child.try_wait() {
            Ok(Some(status)) => return Ok(return_code(status)),
            Ok(None) => {}
            Err(error) => {
                let message = os_error(&error, None);
                return Err(io_failure(error, message));
            }
        }
        let now = Instant::now();
        if cancel_at.is_none() && cancel.is_cancelled() {
            let path = stage.join("cancel");
            std::fs::OpenOptions::new().create(true).append(true).open(&path).map_err(|error| {
                let message = open_error(&error, &path);
                io_failure(error, message)
            })?;
            cancel_at = Some(now);
        }
        if now - started > budget || cancel_at.is_some_and(|at| now - at > DEVICE_CANCEL_GRACE) {
            return Err(failed("Device pipeline preparation exceeded its time budget"));
        }
        if progress.size().max(file_size(&errors_size)) > DEVICE_LOG_BYTES {
            return Err(failed("Device pipeline preparation exceeded its diagnostic budget"));
        }
        progress.poll(report)?;
        std::thread::sleep(DEVICE_POLL);
    })();
    if !matches!(child.try_wait(), Ok(Some(_))) {
        let _ = child.kill();
    }
    let _ = child.wait();
    result
}
