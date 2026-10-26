// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `triaevum-forge`: the Forge command line. Successful commands print one
//! JSON object (`json.dumps(result, indent=2)`, insertion order) and exit 0;
//! failures print `{"status":"error",...}` and exit 1, as the Python Forge CLI
//! does. `run-job` streams one compact JSON event per line on stdout
//! (diagnostics go to stderr). `--install-worker` speaks the Python GUI's
//! worker line protocol, so the Python GUI can drive this engine. `status`
//! prints the installation state, `explain` the explanation of an event,
//! `inventory` where the installation keeps its files and `diagnostics` the
//! redacted report (optionally saving the support bundle), the same JSON the
//! C ABI and the Android installer receive.

use std::ffi::OsString;
use std::io::Write;
use std::path::PathBuf;
use std::process::ExitCode;

use clap::{Args, Parser, Subcommand};
use forge_core::install::{install_private_title, InstallRequest, InstallSource, InstallTarget};
use forge_core::legacy_json::{self, Options, Style};
use forge_core::log::ForgeLog;
use forge_core::state::{inspect, InspectRequest, InstallationState};
use forge_core::CancelToken;
use forge_native::DesktopServices;
use serde_json::{json, Value};

/// `json.dumps(value, indent=2)` without `sort_keys`.
const PYTHON_RESULT: Options = Options { style: Style::Pretty, sort_keys: false, ensure_ascii: true };
/// `json.dumps(value)`: default separators, insertion order.
const PYTHON_LINE: Options = Options { style: Style::Spaced, sort_keys: false, ensure_ascii: true };

#[derive(Parser)]
#[command(name = "triaevum-forge", about = "TriAevum Forge")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Print what this Forge build supports.
    Capabilities,
    /// Run one versioned job request (a JSON file, or - for stdin), streaming events.
    RunJob { request: String },
    /// Import a decrypted ROM (or extracted data) and install its precompiled title.
    Install(InstallArgs),
    /// Print the installation state as JSON (read-only).
    Status(StatusArgs),
    /// Explain a failed or cancelled event, an error object or a notice (a JSON file, or - for stdin).
    Explain { input: String },
    /// Print where the installation keeps its files, and whether the game or another Forge runs (read-only).
    Inventory(InventoryArgs),
    /// Print the redacted diagnostics report and the support bundle's planned contents; --bundle saves it.
    Diagnostics(DiagnosticsArgs),
}

#[derive(Args)]
struct DiagnosticsArgs {
    /// The package (default: this executable's package).
    #[arg(long)]
    package_root: Option<PathBuf>,
    /// The private data folder (default: the package's data folder).
    #[arg(long)]
    data_root: Option<PathBuf>,
    /// Save the support bundle (a zip) at this path.
    #[arg(long)]
    bundle: Option<PathBuf>,
}

#[derive(Args)]
struct InventoryArgs {
    /// The package (default: this executable's package).
    #[arg(long)]
    package_root: Option<PathBuf>,
    /// The private data folder (default: the package's data folder).
    #[arg(long)]
    data_root: Option<PathBuf>,
    /// Do not measure folder sizes.
    #[arg(long)]
    no_sizes: bool,
}

#[derive(Args)]
struct StatusArgs {
    /// The package (default: this executable's package).
    #[arg(long)]
    package_root: Option<PathBuf>,
    /// The private data folder (default: the package's data folder).
    #[arg(long)]
    data_root: Option<PathBuf>,
    /// Rehash the game data kept from the ROM instead of checking its size.
    #[arg(long)]
    verify_sources: bool,
}

#[derive(Args)]
struct InstallArgs {
    /// A decrypted .3ds or .cci file.
    #[arg(long, conflicts_with = "extracted", required_unless_present = "extracted")]
    rom: Option<PathBuf>,
    /// A folder with code.bin, exheader.bin and romfs.bin.
    #[arg(long)]
    extracted: Option<PathBuf>,
    /// The private data folder (default: the package's data folder).
    #[arg(long)]
    data_root: Option<PathBuf>,
    /// The package (default: this executable's folder).
    #[arg(long)]
    package_root: Option<PathBuf>,
    /// The game language to select when the ROM offers it (default: the
    /// current selection, else the runtime's default).
    #[arg(long)]
    game_language: Option<String>,
}

/// `install_worker.main`'s arguments (`--package-root` is a Rust addition).
#[derive(Parser)]
#[command(name = "triaevum-forge --install-worker")]
struct WorkerArgs {
    #[arg(long)]
    rom: PathBuf,
    #[arg(long)]
    data_root: PathBuf,
    #[arg(long)]
    owner_pid: Option<i64>,
    #[arg(long)]
    package_root: Option<PathBuf>,
}

fn main() -> ExitCode {
    let arguments: Vec<OsString> = std::env::args_os().collect();
    if arguments.get(1).is_some_and(|argument| argument == "--install-worker") {
        let mut worker = vec![arguments[0].clone()];
        worker.extend(arguments[2..].iter().cloned());
        return install_worker(WorkerArgs::parse_from(worker));
    }
    match Cli::parse().command {
        Command::Capabilities => print_result(&forge_core::jobs::capabilities()),
        Command::RunJob { request } => run_job(&request),
        Command::Install(args) => install(args),
        Command::Status(args) => status(args),
        Command::Explain { input } => explain(&input),
        Command::Inventory(args) => inventory(args),
        Command::Diagnostics(args) => diagnostics(args),
    }
}

fn diagnostics(args: DiagnosticsArgs) -> ExitCode {
    let package_root = match package_root(args.package_root) {
        Ok(root) => root,
        Err(message) => return print_error(&message),
    };
    let mut request = json!({
        "format": forge_core::diagnostics::REQUEST_FORMAT,
        "package_root": package_root.to_string_lossy(),
    });
    if let Some(data_root) = args.data_root {
        request["data_root"] = json!(data_root.to_string_lossy());
    }
    if let Some(bundle) = args.bundle {
        request["bundle"] = json!(bundle.to_string_lossy());
    }
    let bytes = forge_core::diagnostics::diagnostics_bytes(&serde_json::to_vec(&request).unwrap_or_default());
    let value: Value = serde_json::from_slice(&bytes).unwrap_or(Value::Null);
    println!("{}", legacy_json::dumps(&value, PYTHON_RESULT));
    if value["status"] == "ok" {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

fn inventory(args: InventoryArgs) -> ExitCode {
    let package_root = match package_root(args.package_root) {
        Ok(root) => root,
        Err(message) => return print_error(&message),
    };
    let request = forge_core::inventory::InventoryRequest {
        data_root: args.data_root,
        sizes: !args.no_sizes,
        ..forge_core::inventory::InventoryRequest::package(package_root)
    };
    match forge_core::inventory::inventory_of(&request) {
        Ok(inventory) => print_result(&inventory.to_value()),
        Err(error) => print_error(&error.message),
    }
}

/// One explanation line on stderr for a failure the JSON output reports.
fn explain_on_stderr(event: &Value) {
    if let Some(explanation) = forge_core::explain::explain_value(event) {
        eprintln!("{}: {}", explanation.title, explanation.next_action);
    }
}

fn status(args: StatusArgs) -> ExitCode {
    let package_root = match package_root(args.package_root) {
        Ok(root) => root,
        Err(message) => return print_error(&message),
    };
    let request = InspectRequest {
        data_root: args.data_root,
        verify_sources: args.verify_sources,
        ..InspectRequest::package(package_root)
    };
    let state = inspect(&request);
    println!("{}", legacy_json::dumps(&state.to_value(), PYTHON_RESULT));
    if matches!(state, InstallationState::Failed { .. }) {
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}

fn read_input(input: &str) -> Result<Vec<u8>, String> {
    if input == "-" {
        let mut bytes = Vec::new();
        std::io::Read::read_to_end(&mut std::io::stdin(), &mut bytes)
            .map_err(|error| format!("Cannot read the input: {error}"))?;
        return Ok(bytes);
    }
    std::fs::read(input).map_err(|error| format!("Cannot read {input}: {error}"))
}

fn explain(input: &str) -> ExitCode {
    let bytes = match read_input(input) {
        Ok(bytes) => bytes,
        Err(message) => return print_error(&message),
    };
    let explanation: Value = serde_json::from_slice(&forge_core::explain::explain_bytes(&bytes)).unwrap_or(Value::Null);
    println!("{}", legacy_json::dumps(&explanation, PYTHON_RESULT));
    if explanation.is_null() {
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}

fn print_result(value: &Value) -> ExitCode {
    println!("{}", legacy_json::dumps(value, PYTHON_RESULT));
    ExitCode::SUCCESS
}

fn print_error(message: &str) -> ExitCode {
    println!("{}", legacy_json::dumps(&json!({"status": "error", "error": message}), PYTHON_RESULT));
    ExitCode::FAILURE
}

fn package_root(explicit: Option<PathBuf>) -> Result<PathBuf, String> {
    if let Some(root) = explicit {
        return Ok(root);
    }
    let executable = std::env::current_exe().map_err(|error| format!("Cannot locate the Forge executable: {error}"))?;
    // Packages ship this tool in forge/: its package is the folder above.
    Ok(forge_core::layout::package_root_of(&executable))
}

fn install(args: InstallArgs) -> ExitCode {
    let source = match (args.rom, args.extracted) {
        (Some(rom), _) => InstallSource::Rom(rom),
        (None, Some(directory)) => InstallSource::ExtractedDirectory(directory),
        (None, None) => return print_error("Select a ROM or an extracted-data folder"),
    };
    let package_root = match package_root(args.package_root) {
        Ok(root) => root,
        Err(message) => return print_error(&message),
    };
    let log = forge_core::install::data_root(args.data_root.as_deref(), &package_root)
        .ok()
        .map(|data_root| ForgeLog::new(&data_root, "install"));
    if let Some(log) = &log {
        log.line("started");
    }
    let request = InstallRequest {
        source,
        data_root: args.data_root,
        package_root,
        target: InstallTarget::Package,
        game_language: args.game_language,
    };
    let mut report = |stage: &str, message: &str| {
        eprintln!("[{stage}] {message}");
        if let Some(log) = &log {
            log.line(&format!("stage {stage}: {message}"));
        }
    };
    match install_private_title(request, &DesktopServices, &mut report, &CancelToken::new()) {
        Ok(result) => {
            if let Some(log) = &log {
                log.event(&forge_core::abi::succeeded_event(result.clone()));
            }
            print_result(&result)
        }
        Err(error) => {
            if let Some(log) = &log {
                log.error(&error);
            }
            explain_on_stderr(&forge_core::abi::error_event(&error));
            print_error(&error.message)
        }
    }
}

fn emit(event: &str, payload: Value) {
    let line = legacy_json::dumps(&json!({"forge_worker_event": event, "payload": payload}), PYTHON_LINE);
    let mut stdout = std::io::stdout().lock();
    let _ = writeln!(stdout, "{line}");
    let _ = stdout.flush();
}

/// `install_worker.main`: stage lines while installing, then exactly one
/// `complete` (exit 0) or `error` (exit 1) line.
fn install_worker(args: WorkerArgs) -> ExitCode {
    let outcome = (|| {
        forge_native::bind_worker_lifetime()?;
        if let Some(owner) = args.owner_pid {
            forge_native::watch_owner(owner)?;
        }
        let package_root = package_root(args.package_root).map_err(forge_core::ForgeError::internal)?;
        let request = InstallRequest {
            source: InstallSource::from_path(args.rom),
            data_root: Some(args.data_root),
            package_root,
            target: InstallTarget::Package,
            game_language: None,
        };
        let mut report = |stage: &str, message: &str| emit("stage", json!([stage, message]));
        install_private_title(request, &DesktopServices, &mut report, &CancelToken::new())
    })();
    match outcome {
        Ok(result) => {
            emit("complete", result);
            ExitCode::SUCCESS
        }
        Err(error) => {
            emit("error", json!(error.message));
            ExitCode::FAILURE
        }
    }
}

fn run_job(request: &str) -> ExitCode {
    let bytes = if request == "-" {
        let mut bytes = Vec::new();
        if let Err(error) = std::io::Read::read_to_end(&mut std::io::stdin(), &mut bytes) {
            return print_error(&format!("Cannot read the request: {error}"));
        }
        bytes
    } else {
        match std::fs::read(request) {
            Ok(bytes) => bytes,
            Err(error) => return print_error(&format!("Cannot read {request}: {error}")),
        }
    };
    let mut job = match forge_core::jobs::start(&bytes) {
        Ok(job) => job,
        Err(error) => return print_error(&error.message),
    };
    let stdout = std::io::stdout();
    loop {
        let Some(event) = job.poll(250) else { continue };
        let terminal = matches!(event["event"].as_str(), Some("succeeded" | "failed" | "cancelled"));
        if event["event"] == "failed" {
            explain_on_stderr(&event);
        }
        let mut out = stdout.lock();
        let _ = out.write_all(&legacy_json::compact_line_bytes(&event));
        let _ = out.flush();
        if terminal {
            job.join();
            return if event["event"] == "succeeded" { ExitCode::SUCCESS } else { ExitCode::FAILURE };
        }
    }
}
