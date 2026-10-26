// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Playing the installed title: `forge_gui.launch_runtime` and the single
//! desktop entry `desktop_launcher.main` (`--play [--forge]`).
//!
//! A game Forge starts is watched to its end (UX plan WP-03): the run is
//! recorded in Forge's log folder and a failed run becomes "The game closed
//! with an error" with the end of its logs. The window's Play captures the
//! game's error output in `D/logs/game-stderr.log`; `--play` keeps the
//! terminal's.
//!
//! `launch_runtime` belongs in forge-core beside the activation transaction;
//! it is here until the lead moves it. Difference from Python:
//! `package_update.refresh_packaged_runtime` (refreshing the runtime receipt
//! after a package was unpacked over the installation) has no Rust port yet
//! and is not called; validation then asks to run Forge again.

use std::ffi::OsString;
use std::path::Path;
use std::process::{Child, Stdio};
use std::time::SystemTime;

use forge_base::{ErrorKind, ForgeError, Result};
use forge_core::diagnostics::{explain_game_run, record_game_run, GameRun, Sources};
use forge_core::explain::Explanation;
use forge_core::inventory::{inventory_of, InventoryRequest, Platform};
use forge_core::layout::{place_installed_runtime, HostOs};
use forge_core::transaction::{installation_lock, journal_path};
use forge_native::process::{popen_native, return_code, Environment, RunOptions};
use forge_prepare::content::load_prepared_content;
use serde_json::Value;

use crate::active_title::load_active_title;
use crate::installation::{host_platform, Installation};

const INTERRUPTED: &str = "An activation was interrupted; run Forge again before playing";

/// Python wrapped OSError/ValueError of the lock, placement and spawn steps.
fn cannot_launch(error: ForgeError) -> ForgeError {
    ForgeError { message: format!("Cannot launch the installed title: {}", error.message), ..error }
}

fn check_journal(installation: &Installation) -> Result<()> {
    if journal_path(&installation.layout.activation, HostOs::current()).exists() {
        return Err(ForgeError::new(ErrorKind::RecoveryRequired, INTERRUPTED));
    }
    // An interrupted restore is undone by the window before Play (WP-05B).
    if forge_core::restore::pending_restore(&installation.data_root).is_some() {
        return Err(ForgeError::new(
            ErrorKind::RecoveryRequired,
            "A restore was interrupted; open Forge before playing",
        )
        .because(forge_base::Reason::InterruptedRestore));
    }
    Ok(())
}

/// Starts the game for the active title with Forge's own output streams.
/// The caller owns the child.
pub fn launch_runtime(installation: &Installation) -> Result<Child> {
    launch_runtime_with(installation, Stdio::inherit())
}

/// Starts the game with its error output captured in Forge's log folder
/// (`game-stderr.log`, replaced at each launch), where the diagnostics read
/// the game's own reason for an error exit.
pub fn launch_runtime_capturing(installation: &Installation, sources: &Sources) -> Result<Child> {
    let path = sources.game_stderr();
    let stderr = path
        .parent()
        .map_or(Ok(()), std::fs::create_dir_all)
        .and_then(|()| std::fs::File::create(&path))
        .map_or_else(|_| Stdio::inherit(), Stdio::from);
    launch_runtime_with(installation, stderr)
}

fn launch_runtime_with(installation: &Installation, stderr: Stdio) -> Result<Child> {
    let activation = &installation.layout.activation;
    {
        let _lock = installation_lock(activation, HostOs::current()).map_err(cannot_launch)?;
        check_journal(installation)?;
        // A package unpacked over the installation brings a new runtime.
        place_installed_runtime(&installation.root, &host_platform()?).map_err(cannot_launch)?;
    }
    let _lock = installation_lock(activation, HostOs::current()).map_err(cannot_launch)?;
    check_journal(installation)?;
    launch_runtime_locked(installation, stderr)
}

fn launch_runtime_locked(installation: &Installation, stderr: Stdio) -> Result<Child> {
    let executable = installation.runtime_path()?;
    if !executable.is_file() {
        return Err(ForgeError::invalid(format!("TriAevum runtime is missing: {}", executable.display())));
    }
    let Some(active) = load_active_title(&installation.active_title_state()) else {
        return Err(ForgeError::invalid("No valid active title in the selected data folder; run Forge first"));
    };
    let prepared = load_prepared_content(&active.directory, &[], &forge_base::CancelToken::new())?;
    let Some(Value::Object(runtime)) = prepared.state.get("runtime") else {
        return Err(ForgeError::invalid("The active title has no playable runtime receipt"));
    };
    let profile =
        forge_native::validate_installed_runtime(&executable, &active.directory, &installation.data_root, runtime)?;
    let program: Vec<OsString> = vec![executable.into(), "--launch-profile".into(), profile.into()];
    let options = RunOptions {
        cwd: installation.layout.activation.clone(),
        environment: Environment::Inherit,
        timeout_seconds: 0.0,
        creation_flags: 0,
    };
    popen_native(&program, &options, Stdio::inherit(), stderr).map_err(cannot_launch)
}

/// Where the diagnostics of `installation` read and write, resolved like the
/// runtime resolves its folders (the inventory); the default layout when the
/// inventory cannot be read.
pub fn diagnostics_sources(installation: &Installation) -> Sources {
    let request = InventoryRequest {
        data_root: Some(installation.data_root.clone()),
        sizes: false,
        ..InventoryRequest::package(&installation.root)
    };
    match inventory_of(&request) {
        Ok(inventory) => Sources::from_inventory(&inventory),
        Err(_) => Sources {
            platform: if cfg!(windows) { Platform::WindowsPortable } else { Platform::LinuxPortable },
            package: installation.root.clone(),
            activation: installation.layout.activation.clone(),
            data: installation.data_root.clone(),
            game_logs: installation.layout.activation.join("logs"),
            forge_logs: forge_core::log::logs_directory(&installation.data_root),
            crash_dumps: None,
            runtime_state: None,
            runtime_config: None,
            launch_profile: None,
            android_diagnostics: None,
        },
    }
}

/// A started game: a child process, or a test's stand-in.
pub trait GameProcess {
    /// Waits for the end; the exit code as `return_code` reports it.
    fn wait_for_exit(&mut self) -> std::io::Result<i64>;
}

impl GameProcess for Child {
    fn wait_for_exit(&mut self) -> std::io::Result<i64> {
        self.wait().map(return_code)
    }
}

/// Waits for a game started at `started`, records the run and returns the
/// explanation of a failed one (None after a clean exit).
pub fn finish_game(sources: &Sources, game: &mut dyn GameProcess, started: SystemTime) -> Option<Explanation> {
    // A wait that fails is reported as an unknown end, never as success.
    let exit_code = game.wait_for_exit().unwrap_or(-1);
    let duration = SystemTime::now().duration_since(started).unwrap_or_default();
    let run = GameRun { started, duration, exit_code };
    record_game_run(sources, &run);
    explain_game_run(sources, &run)
}

/// Separates options from opened-file arguments: desktop integrations may
/// pass the file they opened (a `%U` expansion of the AppImage path) although
/// this entry takes no files.
pub fn split_launch_arguments(arguments: &[String]) -> (Vec<String>, Vec<String>) {
    arguments.iter().cloned().partition(|argument| argument.starts_with('-') || !Path::new(argument).exists())
}

/// `desktop_launcher.main`: plays the active title, otherwise (or with
/// `--forge`) opens Forge. Waits for the game and returns its exit status.
pub fn play(arguments: &[String]) -> i32 {
    let (options, launched) = split_launch_arguments(arguments);
    if !launched.is_empty() {
        eprintln!("triaevum: ignoring opened file arguments: {}", launched.join(" "));
    }
    if !(options.is_empty() || options == ["--forge"]) {
        eprintln!("Usage: triaevum [--forge]");
        return 2;
    }
    let installation = match Installation::current() {
        Ok(installation) => installation,
        Err(error) => return crate::app::run(None, Some(error)),
    };
    if options == ["--forge"] || load_active_title(&installation.active_title_state()).is_none() {
        return crate::app::run(Some(installation), None);
    }
    let started = SystemTime::now();
    match launch_runtime(&installation) {
        Ok(mut child) => {
            let code = child.wait().map(return_code).unwrap_or(-1);
            let sources = diagnostics_sources(&installation);
            let mut ended = Ended(Some(code));
            let _ = finish_game(&sources, &mut ended, started);
            code as i32
        }
        // Show the failure and keep import/repair accessible after an update.
        Err(error) => crate::app::run(Some(installation), Some(error.during(forge_base::Operation::Launch))),
    }
}

/// A game that has already ended with a known code.
struct Ended(Option<i64>);

impl GameProcess for Ended {
    fn wait_for_exit(&mut self) -> std::io::Result<i64> {
        self.0.take().ok_or_else(|| std::io::Error::other("the game was already reported"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_game_that_exits_with_an_error_explains_itself() {
        let directory = tempfile::tempdir().unwrap();
        let installation = Installation::at(directory.path()).unwrap();
        let sources = diagnostics_sources(&installation);
        let started = SystemTime::now() - std::time::Duration::from_secs(3);
        std::fs::create_dir_all(&sources.game_logs).unwrap();
        let lines: String = (1..=50).map(|index| format!("log line {index}\n")).collect();
        std::fs::write(sources.game_log(), lines).unwrap();
        std::fs::create_dir_all(&sources.forge_logs).unwrap();
        std::fs::write(sources.game_stderr(), "TRIAEVUM_FATAL no Vulkan device\n").unwrap();

        let explanation = finish_game(&sources, &mut Ended(Some(1)), started).expect("an error card");
        assert_eq!(explanation.id, "outcome.game_exited_with_error");
        assert!(explanation.details.starts_with("Exit code: 1"), "{}", explanation.details);
        assert!(explanation.details.contains("Error reported by the game: no Vulkan device"));
        assert!(explanation.details.contains("log line 50") && !explanation.details.contains("log line 10\n"));
        let recorded = forge_core::diagnostics::last_game_run(&sources).unwrap();
        assert_eq!(recorded.exit_code, 1);

        assert_eq!(finish_game(&sources, &mut Ended(Some(0)), SystemTime::now()), None, "a clean exit shows nothing");
        assert_eq!(forge_core::diagnostics::last_game_run(&sources).unwrap().exit_code, 0);
    }

    #[test]
    fn opened_files_are_separated_from_options() {
        let directory = tempfile::tempdir().unwrap();
        let existing = directory.path().join("TriAevum.AppImage");
        std::fs::write(&existing, b"").unwrap();
        let existing = existing.to_string_lossy().into_owned();
        let arguments = vec!["--forge".to_string(), existing.clone(), "missing-file".to_string()];
        let (options, launched) = split_launch_arguments(&arguments);
        assert_eq!(options, ["--forge", "missing-file"]);
        assert_eq!(launched, [existing]);
    }

    #[test]
    fn unknown_options_are_a_usage_error() {
        assert_eq!(play(&["--bogus".to_string()]), 2);
        assert_eq!(play(&["--forge".to_string(), "--forge".to_string()]), 2);
    }

    #[cfg(target_arch = "x86_64")]
    #[test]
    fn launching_without_a_runtime_or_title_explains_why() {
        let directory = tempfile::tempdir().unwrap();
        let installation = Installation::at(directory.path()).unwrap();
        let error = launch_runtime(&installation).unwrap_err();
        assert!(error.message.starts_with("TriAevum runtime is missing: "), "{}", error.message);
        std::fs::write(installation.runtime_path().unwrap(), b"").unwrap();
        let error = launch_runtime(&installation).unwrap_err();
        assert_eq!(error.message, "No valid active title in the selected data folder; run Forge first");
        let journal = journal_path(&installation.layout.activation, HostOs::current());
        std::fs::create_dir_all(journal.parent().unwrap()).unwrap();
        std::fs::write(&journal, b"{}").unwrap();
        assert_eq!(launch_runtime(&installation).unwrap_err().message, INTERRUPTED);
    }
}
