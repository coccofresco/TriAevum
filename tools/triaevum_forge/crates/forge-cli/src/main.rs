// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `triaevum-forge`: the Forge command line. Successful commands print one
//! pretty JSON object and exit 0; failures print `{"status":"error",...}` and
//! exit 1, as the Python Forge CLI does. `run-job` streams one compact JSON
//! event per line on stdout (diagnostics go to stderr).

use std::io::Write;
use std::process::ExitCode;

use clap::{Parser, Subcommand};
use forge_core::legacy_json::{self, Options};
use serde_json::{json, Value};

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
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    match cli.command {
        Command::Capabilities => print_result(&forge_core::jobs::capabilities()),
        Command::RunJob { request } => run_job(&request),
    }
}

fn print_result(value: &Value) -> ExitCode {
    println!("{}", legacy_json::dumps(value, Options::PRETTY));
    ExitCode::SUCCESS
}

fn print_error(message: &str) -> ExitCode {
    println!("{}", legacy_json::dumps(&json!({"status": "error", "error": message}), Options::PRETTY));
    ExitCode::FAILURE
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
        let mut out = stdout.lock();
        let _ = out.write_all(&legacy_json::compact_line_bytes(&event));
        let _ = out.flush();
        if terminal {
            job.join();
            return if event["event"] == "succeeded" { ExitCode::SUCCESS } else { ExitCode::FAILURE };
        }
    }
}
