// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! TriAevum Forge desktop entry (`forge_entry.py` for the desktop): no
//! arguments open the window, `--play [--forge]` plays the active title
//! (`desktop_launcher.py`), `--gui-smoke` is the real-widget layout probe
//! (`forge_gui_probe.py`). The import itself runs as the `install` job of
//! forge-core; there is no separate worker process.
//!
//! Update from ZIP (UX plan WP-04B): the update helper reopens the window
//! with `--update-result PATH` (the window shows the result it finds), and
//! `--update-target OLD` run from a newly extracted package updates the
//! installation in OLD with this package.

#![cfg_attr(all(windows, not(test)), windows_subsystem = "windows")]

mod active_title;
mod app;
mod fonts;
mod install;
mod installation;
mod launch;
mod maintenance;
mod mods;
mod pages;
mod recognition;
mod smoke;
mod texts;
mod window;

use std::process::ExitCode;

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let code = match arguments.first().map(String::as_str) {
        None => app::run(None, None),
        Some("--update-result") => app::run(None, None),
        Some("--update-target") => update_target(&arguments[1..]),
        Some("--play") => launch::play(&arguments[1..]),
        Some("--gui-smoke") => smoke::main(&arguments[1..]),
        Some(_) => {
            // The command line interface is the separate triaevum-forge tool.
            eprintln!(
                "Usage: TriAevumForge [--play [--forge] | --update-target FOLDER | --gui-smoke --output PATH \
                 [--work-area WxH]]"
            );
            eprintln!("Forge commands are run with triaevum-forge.");
            2
        }
    };
    exit_code(code)
}

/// `--update-target OLD`: the window for the installation in OLD, offering
/// this executable's package as the update.
fn update_target(arguments: &[String]) -> i32 {
    let [target] = arguments else {
        eprintln!("Usage: TriAevumForge --update-target <folder of the installation to update>");
        return 2;
    };
    let installation = match installation::Installation::at(std::path::Path::new(target)) {
        Ok(installation) => installation,
        Err(error) => {
            eprintln!("{}", error.message);
            return 1;
        }
    };
    match forge_core::layout::executable_package_root() {
        Ok(package) => maintenance::update_from_package(package),
        Err(error) => {
            eprintln!("{}", error.message);
            return 1;
        }
    }
    app::run(Some(installation), None)
}

/// Process exit statuses wider than a byte (a Windows runtime's NTSTATUS)
/// keep their full value, as Python's `sys.exit(returncode)` did.
fn exit_code(code: i32) -> ExitCode {
    if (0..=255).contains(&code) {
        return ExitCode::from(code as u8);
    }
    std::process::exit(code)
}
