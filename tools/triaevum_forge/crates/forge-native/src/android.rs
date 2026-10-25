// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Android [`NativeServices`]: the runtime library's helpers, in process.
//!
//! An APK cannot run helper executables (`FORGE_SPEC.md` 2.10, 4.4 point 6).
//! The runtime library `libTriAevum.so`, installed by Android in the APK's
//! native library directory, exports `triaevum_forge_tool`
//! (`tools/oot3d/native_game_runtime/triaevum_forge_tool.cpp`): it runs the
//! runtime's tool modes, the pass compiler and the device pipeline helper
//! with the desktop helpers' command lines, writes their standard streams to
//! two files and returns their exit status. The pass compiler and the device
//! job are therefore the gameplay build's own shaderc and renderer.
//!
//! The Android catalog names that library as the runtime, the pass compiler
//! and the device helper; every call checks that the catalog-verified file it
//! was given is the loaded library. The caller runs Forge in a process of its
//! own, never in the process that runs the game: the helpers keep the
//! runtime's global state.
//!
//! In-process work cannot be killed. Cancellation and the device job's time
//! budget create the helper's cancel file, which it honours between
//! pipelines; the interior-sun prebuild and the shader steps run to
//! completion before a cancellation takes effect.

use std::ffi::OsString;
use std::fs::File;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use forge_base::{CancelToken, ErrorKind, ForgeError, Result};

use crate::desktop::{ProgressReader, DEVICE_LOG_BYTES};
use crate::pyerr::{decode_text, io_failure, open_error};
use crate::services::{
    DeviceJob, InteriorSunRequest, LanguageQuery, NativeOutput, NativeServices, PortableCompile, RendererPassRequest,
    Report, TransferableImport,
};

/// The exported entry of the runtime library.
pub const TOOL_SYMBOL: &str = "triaevum_forge_tool";
/// `argv[0]` of the runtime's tool modes.
pub const RUNTIME_TOOL: &str = "runtime";
/// `argv[0]` of the pass compiler (renderer cache and portable packs).
pub const PASS_COMPILER_TOOL: &str = "pass-compiler";
/// `argv[0]` of the device pipeline helper.
pub const PIPELINE_TOOL: &str = "pipeline-prepare";
const DEVICE_POLL: Duration = Duration::from_millis(50);

/// One in-process helper call. `arguments[0]` names the helper; its
/// standard output and error go to `stdout` and `stderr`, created or
/// truncated. Returns the helper's exit status.
pub trait ToolEntry: Sync {
    /// The library that runs the helpers.
    fn library(&self) -> &Path;
    fn run(&self, arguments: &[OsString], stdout: &Path, stderr: &Path) -> Result<i32>;
}

fn unavailable(operation: &str) -> ForgeError {
    ForgeError::new(
        ErrorKind::IncompatibleTarget,
        format!("In-process native {operation} is not available in this Android build"),
    )
}

#[cfg(unix)]
mod library {
    use std::ffi::{c_char, c_int, CStr, CString, OsString};
    use std::os::unix::ffi::OsStrExt;
    use std::path::{Path, PathBuf};
    use std::sync::OnceLock;

    use forge_base::{ErrorKind, ForgeError, Result};

    type ToolFunction = unsafe extern "C" fn(c_int, *mut *mut c_char, *const c_char, *const c_char) -> c_int;

    /// The runtime library, loaded on first use and never unloaded.
    pub struct RuntimeLibrary {
        path: PathBuf,
        entry: OnceLock<std::result::Result<ToolFunction, String>>,
    }

    fn c_string(value: &std::ffi::OsStr) -> Result<CString> {
        CString::new(value.as_bytes()).map_err(|_| ForgeError::invalid("Native helper argument contains NUL"))
    }

    fn dl_error() -> String {
        // SAFETY: dlerror returns null or a thread-local NUL-terminated message.
        let message = unsafe { libc::dlerror() };
        if message.is_null() {
            return "unknown error".into();
        }
        // SAFETY: checked non-null above.
        unsafe { CStr::from_ptr(message) }.to_string_lossy().into_owned()
    }

    impl RuntimeLibrary {
        pub fn new(path: impl Into<PathBuf>) -> Self {
            Self { path: path.into(), entry: OnceLock::new() }
        }

        fn entry(&self) -> Result<ToolFunction> {
            let loaded = self.entry.get_or_init(|| {
                let path = c_string(self.path.as_os_str()).map_err(|error| error.message)?;
                // SAFETY: a NUL-terminated path; the handle is intentionally kept.
                let handle = unsafe { libc::dlopen(path.as_ptr(), libc::RTLD_NOW) };
                if handle.is_null() {
                    return Err(dl_error());
                }
                let symbol = CString::new(super::TOOL_SYMBOL).expect("constant symbol");
                // SAFETY: a valid handle and a NUL-terminated symbol name.
                let address = unsafe { libc::dlsym(handle, symbol.as_ptr()) };
                if address.is_null() {
                    return Err(dl_error());
                }
                // SAFETY: the symbol is triaevum_forge_tool with this signature.
                Ok(unsafe { std::mem::transmute::<*mut libc::c_void, ToolFunction>(address) })
            });
            loaded.clone().map_err(|message| {
                ForgeError::new(
                    ErrorKind::IncompatibleTarget,
                    format!("Cannot load the runtime's native services ({}): {message}", self.path.display()),
                )
            })
        }
    }

    impl super::ToolEntry for RuntimeLibrary {
        fn library(&self) -> &Path {
            &self.path
        }

        fn run(&self, arguments: &[OsString], stdout: &Path, stderr: &Path) -> Result<i32> {
            let entry = self.entry()?;
            let owned = arguments.iter().map(|argument| c_string(argument)).collect::<Result<Vec<_>>>()?;
            let mut pointers: Vec<*mut c_char> = owned.iter().map(|value| value.as_ptr().cast_mut()).collect();
            pointers.push(std::ptr::null_mut());
            let stdout = c_string(stdout.as_os_str())?;
            let stderr = c_string(stderr.as_os_str())?;
            let count = c_int::try_from(arguments.len()).map_err(|_| ForgeError::invalid("Too many arguments"))?;
            // SAFETY: argv holds `count` NUL-terminated strings and a null
            // terminator, all alive for the call; the helper does not keep them.
            let status = unsafe { entry(count, pointers.as_mut_ptr(), stdout.as_ptr(), stderr.as_ptr()) };
            if status < 0 {
                return Err(ForgeError::new(ErrorKind::PreparationFailed, "The native helper could not start"));
            }
            Ok(status)
        }
    }
}

#[cfg(unix)]
pub use library::RuntimeLibrary;

/// The in-process services. `scratch` holds the streams of each call (an
/// app-private directory: the application's cache folder).
pub struct AndroidServices<T: ToolEntry> {
    tool: T,
    scratch: PathBuf,
}

#[cfg(unix)]
impl AndroidServices<RuntimeLibrary> {
    /// `library` is `libTriAevum.so` in the APK's native library directory.
    pub fn new(library: impl Into<PathBuf>, scratch: impl Into<PathBuf>) -> Self {
        Self::with_tool(RuntimeLibrary::new(library), scratch)
    }
}

fn same_file(left: &Path, right: &Path) -> bool {
    match (std::fs::canonicalize(left), std::fs::canonicalize(right)) {
        (Ok(left), Ok(right)) => left == right,
        _ => false,
    }
}

fn argv(tool: &str, arguments: Vec<OsString>) -> Vec<OsString> {
    let mut command = vec![OsString::from(tool)];
    command.extend(arguments);
    command
}

fn read_text(path: &Path) -> Result<String> {
    match std::fs::read(path) {
        Ok(bytes) => Ok(decode_text(&bytes)),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(String::new()),
        Err(error) => {
            let message = open_error(&error, path);
            Err(io_failure(error, message))
        }
    }
}

fn create_empty(path: &Path) -> Result<File> {
    File::create(path).map_err(|error| {
        let message = open_error(&error, path);
        io_failure(error, message)
    })
}

fn touch(path: &Path) -> Result<()> {
    std::fs::OpenOptions::new().create(true).append(true).open(path).map(drop).map_err(|error| {
        let message = open_error(&error, path);
        io_failure(error, message)
    })
}

impl<T: ToolEntry> AndroidServices<T> {
    pub fn with_tool(tool: T, scratch: impl Into<PathBuf>) -> Self {
        Self { tool, scratch: scratch.into() }
    }

    /// Only the loaded library runs helpers: the catalog verified `executable`.
    fn check(&self, executable: &Path) -> Result<()> {
        if same_file(executable, self.tool.library()) {
            return Ok(());
        }
        Err(ForgeError::new(
            ErrorKind::IncompatibleTarget,
            format!("{} is not this application's runtime library", executable.display()),
        ))
    }

    /// Runs one helper to completion and returns what it printed.
    fn call(&self, executable: &Path, tool: &str, arguments: Vec<OsString>) -> Result<NativeOutput> {
        self.check(executable)?;
        std::fs::create_dir_all(&self.scratch)
            .map_err(|error| ForgeError::io(format!("Cannot create {}", self.scratch.display()), error))?;
        let stage = tempfile::Builder::new()
            .prefix("native-")
            .tempdir_in(&self.scratch)
            .map_err(|error| ForgeError::io(format!("Cannot create a folder in {}", self.scratch.display()), error))?;
        let stdout = stage.path().join("stdout.log");
        let stderr = stage.path().join("stderr.log");
        let status = self.tool.run(&argv(tool, arguments), &stdout, &stderr)?;
        Ok(NativeOutput { status: i64::from(status), stdout: read_text(&stdout)?, stderr: read_text(&stderr)? })
    }
}

impl<T: ToolEntry> NativeServices for AndroidServices<T> {
    fn product_info(&self, runtime: &Path, plugin: Option<&Path>) -> Result<NativeOutput> {
        let arguments = match plugin {
            None => vec!["--product-info".into()],
            Some(plugin) => vec!["--verify-title-plugin".into(), plugin.as_os_str().to_os_string()],
        };
        self.call(runtime, RUNTIME_TOOL, arguments)
    }

    fn game_language_info(&self, runtime: &Path, query: &LanguageQuery) -> Result<NativeOutput> {
        self.call(runtime, RUNTIME_TOOL, query.arguments())
    }

    fn prebuild_interior_sun(&self, request: &InteriorSunRequest, cancel: &CancelToken) -> Result<NativeOutput> {
        cancel.check()?;
        let output = self.call(&request.runtime, RUNTIME_TOOL, request.arguments())?;
        cancel.check()?;
        Ok(output)
    }

    fn prepare_renderer_passes(&self, request: &RendererPassRequest) -> Result<NativeOutput> {
        self.call(&request.compiler, PASS_COMPILER_TOOL, request.arguments())
    }

    fn import_transferable_shaders(&self, _request: &TransferableImport) -> Result<NativeOutput> {
        Err(unavailable("shader cache import"))
    }

    fn compile_portable_shaders(&self, request: &PortableCompile) -> Result<NativeOutput> {
        self.call(&request.compiler, PASS_COMPILER_TOOL, request.arguments())
    }

    /// As the desktop helper: streams in `stage/stdout.log` and
    /// `stage/stderr.log`, progress read every 50 ms; cancellation, the time
    /// budget and the diagnostic budget create `stage/cancel`.
    fn prepare_device_pipelines(&self, job: &DeviceJob, cancel: &CancelToken, report: Report<'_>) -> Result<i64> {
        self.check(&job.helper)?;
        let stdout = job.stage.join("stdout.log");
        let stderr = job.stage.join("stderr.log");
        create_empty(&stdout)?;
        create_empty(&stderr)?;
        let mut progress = ProgressReader::new(File::open(&stdout).map_err(|error| {
            let message = open_error(&error, &stdout);
            io_failure(error, message)
        })?);
        let command = argv(PIPELINE_TOOL, job.arguments());
        let budget = Duration::from_secs_f64(job.timeout_seconds.max(0.0));
        let started = Instant::now();
        std::thread::scope(|scope| {
            let worker = scope.spawn(|| self.tool.run(&command, &stdout, &stderr));
            let mut stopped: Option<ForgeError> = None;
            let mut cancel_requested = false;
            while !worker.is_finished() {
                let over_budget = started.elapsed() > budget;
                let log_bytes = progress.size().max(std::fs::metadata(&stderr).map_or(0, |data| data.len()));
                let over_logs = log_bytes > DEVICE_LOG_BYTES;
                if !cancel_requested && (cancel.is_cancelled() || over_budget || over_logs) {
                    touch(&job.cancel_path())?;
                    cancel_requested = true;
                    let message = if over_budget {
                        Some("Device pipeline preparation exceeded its time budget")
                    } else if over_logs {
                        Some("Device pipeline preparation exceeded its diagnostic budget")
                    } else {
                        None
                    };
                    if let Some(message) = message {
                        stopped.get_or_insert(ForgeError::new(ErrorKind::PreparationFailed, message));
                    }
                }
                if stopped.is_none() {
                    if let Err(error) = progress.poll(report) {
                        stopped = Some(error);
                    }
                }
                std::thread::sleep(DEVICE_POLL);
            }
            let status = worker.join().map_err(|_| ForgeError::internal("The device pipeline helper panicked"))??;
            if let Some(error) = stopped {
                return Err(error);
            }
            progress.poll(report)?;
            Ok(i64::from(status))
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    /// Records each call and answers with a scripted status and streams.
    struct FakeTool {
        library: PathBuf,
        calls: Mutex<Vec<Vec<OsString>>>,
        status: i32,
        stdout: &'static str,
        stderr: &'static str,
    }

    impl ToolEntry for FakeTool {
        fn library(&self) -> &Path {
            &self.library
        }

        fn run(&self, arguments: &[OsString], stdout: &Path, stderr: &Path) -> Result<i32> {
            self.calls.lock().unwrap().push(arguments.to_vec());
            std::fs::write(stdout, self.stdout).unwrap();
            std::fs::write(stderr, self.stderr).unwrap();
            if arguments[0] == PIPELINE_TOOL {
                let report = arguments.iter().position(|argument| argument == "--report").unwrap() + 1;
                std::fs::write(&arguments[report], "{}").unwrap();
            }
            Ok(self.status)
        }
    }

    fn fixture(
        status: i32,
        stdout: &'static str,
        stderr: &'static str,
    ) -> (tempfile::TempDir, AndroidServices<FakeTool>) {
        let directory = tempfile::tempdir().unwrap();
        let library = directory.path().join("libTriAevum.so");
        std::fs::write(&library, b"ELF").unwrap();
        let tool = FakeTool { library, calls: Mutex::new(Vec::new()), status, stdout, stderr };
        let scratch = directory.path().join("cache");
        (directory, AndroidServices::with_tool(tool, scratch))
    }

    #[test]
    fn runtime_modes_return_the_helper_streams() {
        let (directory, services) = fixture(0, "{\"product\":1}\n", "");
        let library = directory.path().join("libTriAevum.so");
        let output = services.product_info(&library, None).unwrap();
        assert_eq!(output, NativeOutput { status: 0, stdout: "{\"product\":1}\n".into(), stderr: String::new() });
        let romfs = directory.path().join("romfs.bin");
        services.game_language_info(&library, &LanguageQuery::romfs(&romfs)).unwrap();
        let rom = LanguageQuery { romfs: directory.path().join("game.cci"), extent: Some((0x1000, 0x2000)) };
        services.game_language_info(&library, &rom).unwrap();
        let calls = services.tool.calls.lock().unwrap();
        assert_eq!(calls[0], vec![OsString::from("runtime"), "--product-info".into()]);
        assert_eq!(calls[1], vec![OsString::from("runtime"), "--game-language-info".into(), romfs.into()]);
        let extent: Vec<OsString> = ["--romfs-offset", "4096", "--romfs-size", "8192"].map(OsString::from).into();
        assert_eq!(calls[2][3..], extent[..]);
        // Each call's streams are removed with its folder.
        assert_eq!(std::fs::read_dir(directory.path().join("cache")).unwrap().count(), 0);
    }

    #[test]
    fn a_native_failure_is_an_output_with_its_status() {
        let (directory, services) = fixture(1, "", "oot3d_native_game: broken\r\n");
        let library = directory.path().join("libTriAevum.so");
        let output = services.product_info(&library, Some(&directory.path().join("plugin.so"))).unwrap();
        assert_eq!(output.status, 1);
        assert_eq!(output.stderr, "oot3d_native_game: broken\n");
    }

    #[test]
    fn refuses_an_executable_other_than_the_loaded_library() {
        let (directory, services) = fixture(0, "", "");
        let other = directory.path().join("other.so");
        std::fs::write(&other, b"ELF").unwrap();
        let error = services.product_info(&other, None).unwrap_err();
        assert_eq!(error.kind, ErrorKind::IncompatibleTarget);
        assert!(services.tool.calls.lock().unwrap().is_empty());
    }

    #[test]
    fn pass_compiler_requests_name_the_pass_compiler() {
        let (directory, services) = fixture(0, "", "");
        let library = directory.path().join("libTriAevum.so");
        let request = RendererPassRequest {
            compiler: library.clone(),
            root: directory.path().to_path_buf(),
            cache_directory: directory.path().join("renderer"),
            manifest: directory.path().join("result.json"),
        };
        services.prepare_renderer_passes(&request).unwrap();
        let calls = services.tool.calls.lock().unwrap();
        assert_eq!(calls[0][0], "pass-compiler");
        assert_eq!(&calls[0][1..], request.arguments().as_slice());
    }

    #[test]
    fn device_job_reports_progress_from_the_helper_log() {
        let progress = "{\"event\": \"pipeline_progress\", \"total\": 4, \"attempted\": 2}\n";
        let (directory, services) = fixture(0, progress, "");
        let stage = directory.path().join("stage");
        std::fs::create_dir(&stage).unwrap();
        let job = DeviceJob {
            helper: directory.path().join("libTriAevum.so"),
            root: directory.path().to_path_buf(),
            stage: stage.clone(),
            pack: directory.path().join("portable.o3ps"),
            cache_directory: directory.path().join("renderer"),
            adapter: 0,
            manifests: vec![directory.path().join("pipelines-0.json")],
            startup_programs: true,
            timeout_seconds: 60.0,
        };
        let mut messages = Vec::new();
        let status = services
            .prepare_device_pipelines(&job, &CancelToken::new(), &mut |stage, message| {
                messages.push(format!("[{stage}] {message}"));
            })
            .unwrap();
        assert_eq!(status, 0);
        assert_eq!(messages, vec!["[pipelines] Preparing GPU pipelines: 2/4"]);
        assert!(job.report_path().is_file());
        assert!(!job.cancel_path().exists());
    }
}
