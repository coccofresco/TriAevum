// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Playing the installed title: `forge_gui.launch_runtime` and the single
//! desktop entry `desktop_launcher.main` (`--play [--forge]`).
//!
//! `launch_runtime` belongs in forge-core beside the activation transaction;
//! it is here until the lead moves it. Difference from Python:
//! `package_update.refresh_packaged_runtime` (refreshing the runtime receipt
//! after a package was unpacked over the installation) has no Rust port yet
//! and is not called; validation then asks to run Forge again.

use std::ffi::OsString;
use std::path::Path;
use std::process::{Child, Stdio};

use forge_base::{ErrorKind, ForgeError, Result};
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
    Ok(())
}

/// Starts the game for the active title. The caller owns the child.
pub fn launch_runtime(installation: &Installation) -> Result<Child> {
    let activation = &installation.layout.activation;
    {
        let _lock = installation_lock(activation, HostOs::current()).map_err(cannot_launch)?;
        check_journal(installation)?;
        // A package unpacked over the installation brings a new runtime.
        place_installed_runtime(&installation.root, &host_platform()?).map_err(cannot_launch)?;
    }
    let _lock = installation_lock(activation, HostOs::current()).map_err(cannot_launch)?;
    check_journal(installation)?;
    launch_runtime_locked(installation)
}

fn launch_runtime_locked(installation: &Installation) -> Result<Child> {
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
    popen_native(&program, &options, Stdio::inherit(), Stdio::inherit()).map_err(cannot_launch)
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
    let waited = launch_runtime(&installation)
        .and_then(|mut child| child.wait().map_err(|error| ForgeError::io("Cannot wait for the game", error)));
    match waited {
        Ok(status) => return_code(status) as i32,
        // Show the failure and keep import/repair accessible after an update.
        Err(error) => crate::app::run(Some(installation), Some(error.during(forge_base::Operation::Launch))),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
