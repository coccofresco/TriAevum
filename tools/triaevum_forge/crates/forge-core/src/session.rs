// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The quiescence contract (UX plan WP-05A): backup, restore, migration and
//! destructive cleanup run only while no game and no other Forge uses the
//! installation.
//!
//! Two kernel locks ([`crate::lock`]) say who is active, both in the
//! installation's state folder (`layout::installation_state`: `A/data` on
//! Windows, `A` elsewhere):
//!
//! - `.forge-activation.lock`, held by a Forge while it installs, activates
//!   or launches (`transaction::installation_lock`);
//! - `.forge-activation/game-session.lock`, held by the game runtime for its
//!   whole life (`triaevum_game_session_lock.cpp`). The runtime takes it only
//!   when it was started from a launch profile and the `.forge-activation`
//!   folder exists, i.e. for installations Forge activated; developer
//!   launches take no lock.
//!
//! Both are operating-system locks (`LockFileEx`, `flock`): a crash or a
//! killed process releases them, so a stale lock cannot block anyone. The
//! lock files themselves are never deleted.
//!
//! [`quiesce`] takes both locks for the duration of an operation, so a game
//! started meanwhile refuses to run and another Forge reports `busy`.

use std::path::{Path, PathBuf};

use forge_base::{ErrorKind, ForgeError, Operation, Reason, Result};
use serde_json::{json, Value};

use crate::explain::explain;
use crate::layout::{installation_state, HostOs};
use crate::lock::{self, KernelLock};

/// The folder of the activation journal, its backups and the session lock.
pub const ACTIVATION_DIRECTORY: &str = ".forge-activation";
pub const SESSION_LOCK: &str = "game-session.lock";
pub const INSTALLATION_LOCK: &str = ".forge-activation.lock";

/// `<state>/.forge-activation/game-session.lock` of the installation whose
/// activation root is `installation`.
pub fn session_lock_path(installation: &Path, os: HostOs) -> PathBuf {
    installation_state(installation, os).join(ACTIVATION_DIRECTORY).join(SESSION_LOCK)
}

/// `<state>/.forge-activation.lock`.
pub fn installation_lock_path(installation: &Path, os: HostOs) -> PathBuf {
    installation_state(installation, os).join(INSTALLATION_LOCK)
}

/// Who is using an installation now.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Activity {
    pub game_running: bool,
    pub forge_running: bool,
}

impl Activity {
    pub fn idle(&self) -> bool {
        !self.game_running && !self.forge_running
    }

    /// The `busy` error an operation that needs quiescence reports, or None
    /// when the installation is idle. The game is named first: closing it is
    /// the user's action.
    pub fn busy_error(&self, installation: &Path, os: HostOs, operation: Operation) -> Option<ForgeError> {
        if self.game_running {
            Some(game_running(&session_lock_path(installation, os), operation))
        } else if self.forge_running {
            Some(forge_running(&installation_lock_path(installation, os), operation))
        } else {
            None
        }
    }

    /// `{"game_running", "forge_running"}`.
    pub fn to_value(&self) -> Value {
        json!({"game_running": self.game_running, "forge_running": self.forge_running})
    }

    /// [`Activity::to_value`] plus the explanation of the `busy` error a
    /// quiescent operation would report now, when there is one.
    pub fn to_value_for(&self, installation: &Path, os: HostOs, operation: Operation) -> Value {
        let mut value = self.to_value();
        if let Some(error) = self.busy_error(installation, os, operation) {
            value["explanation"] = explain(&error).to_value();
        }
        value
    }
}

/// Read-only: creates and writes nothing.
pub fn activity(installation: &Path, os: HostOs) -> Activity {
    Activity {
        game_running: lock::is_held(&session_lock_path(installation, os)),
        forge_running: lock::is_held(&installation_lock_path(installation, os)),
    }
}

fn game_running(path: &Path, operation: Operation) -> ForgeError {
    ForgeError::new(
        ErrorKind::Busy,
        format!("TriAevum is running from this installation (session lock {})", path.display()),
    )
    .with_path(path)
    .during(operation)
    .because(Reason::GameRunning)
}

fn forge_running(path: &Path, operation: Operation) -> ForgeError {
    ForgeError::new(ErrorKind::Busy, format!("Another Forge is using this installation (lock {})", path.display()))
        .with_path(path)
        .during(operation)
        .because(Reason::ForgeRunning)
}

/// Both locks, held until dropped.
#[derive(Debug)]
pub struct Quiescence {
    _forge: KernelLock,
    _game: KernelLock,
}

/// Takes the installation lock, then the game session lock, for an
/// operation that must not overlap the game or another Forge. A held lock
/// is a `busy` error whose reason names the holder; a legacy lock keeps its
/// own error.
pub fn quiesce(installation: &Path, os: HostOs, operation: Operation) -> Result<Quiescence> {
    let state = installation_state(installation, os);
    let directory = state.join(ACTIVATION_DIRECTORY);
    std::fs::create_dir_all(&directory)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", directory.display()), error).during(operation))?;
    let forge_lock = installation_lock_path(installation, os);
    let forge = lock::acquire(&forge_lock).map_err(|error| match error.kind {
        ErrorKind::Busy => forge_running(&forge_lock, operation),
        _ => error.during(operation),
    })?;
    let game_lock = session_lock_path(installation, os);
    let game = lock::acquire(&game_lock).map_err(|error| match error.kind {
        ErrorKind::Busy => game_running(&game_lock, operation),
        _ => error.during(operation),
    })?;
    Ok(Quiescence { _forge: forge, _game: game })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_session_lock_lives_beside_the_activation_journal() {
        let root = Path::new("root");
        assert_eq!(
            session_lock_path(root, HostOs::Windows),
            root.join("data").join(".forge-activation").join("game-session.lock")
        );
        assert_eq!(session_lock_path(root, HostOs::Linux), root.join(".forge-activation").join("game-session.lock"));
        assert_eq!(session_lock_path(root, HostOs::Android), root.join(".forge-activation").join("game-session.lock"));
        assert_eq!(
            session_lock_path(root, HostOs::Windows).parent(),
            crate::transaction::journal_path(root, HostOs::Windows).parent()
        );
    }

    #[test]
    fn an_idle_installation_is_quiesced_and_others_see_it_busy() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        assert!(activity(root, HostOs::Linux).idle());
        assert!(!root.join(ACTIVATION_DIRECTORY).exists(), "activity must not create anything");
        let held = quiesce(root, HostOs::Linux, Operation::Backup).unwrap();
        let seen = activity(root, HostOs::Linux);
        assert!(seen.game_running && seen.forge_running);
        let error = quiesce(root, HostOs::Linux, Operation::Restore).unwrap_err();
        assert_eq!((error.kind, error.reason), (ErrorKind::Busy, Some(Reason::ForgeRunning)));
        drop(held);
        assert!(activity(root, HostOs::Linux).idle());
        drop(quiesce(root, HostOs::Linux, Operation::Restore).unwrap());
    }

    #[test]
    fn a_running_game_refuses_with_its_explanation() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        std::fs::create_dir_all(root.join(ACTIVATION_DIRECTORY)).unwrap();
        // The runtime's lock: a plain exclusive OS lock on the file.
        let game = std::fs::File::create(session_lock_path(root, HostOs::Linux)).unwrap();
        game.lock().unwrap();
        let seen = activity(root, HostOs::Linux);
        assert_eq!(seen, Activity { game_running: true, forge_running: false });
        let error = quiesce(root, HostOs::Linux, Operation::Migration).unwrap_err();
        assert_eq!(error.kind, ErrorKind::Busy);
        assert_eq!(error.reason, Some(Reason::GameRunning));
        assert_eq!(error.operation, Some(Operation::Migration));
        let explanation = explain(&error);
        assert_eq!(explanation.id, "session.game_running");
        assert_eq!(explanation.primary_action.code(), "close_and_retry");
        // The installation lock taken first was released with the failure.
        assert!(!activity(root, HostOs::Linux).forge_running);
        let value = seen.to_value_for(root, HostOs::Linux, Operation::Backup);
        assert_eq!(value["explanation"]["id"], "session.game_running");
        drop(game);
        assert!(activity(root, HostOs::Linux).idle());
        assert!(quiesce(root, HostOs::Linux, Operation::Cleanup).is_ok());
    }

    /// A crash releases the lock: a process that held it and was killed
    /// leaves nothing behind but the file.
    #[cfg(windows)]
    #[test]
    fn a_killed_holder_releases_the_session_lock() {
        use std::process::{Command, Stdio};

        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        std::fs::create_dir_all(root.join(ACTIVATION_DIRECTORY)).unwrap();
        let path = session_lock_path(root, HostOs::Linux);
        let script = format!(
            "$f=[IO.File]::Open('{}','OpenOrCreate','ReadWrite','ReadWrite'); $f.Lock(0,[long]::MaxValue); \
             [Console]::Out.WriteLine('held'); [Console]::Out.Flush(); Start-Sleep 60",
            path.display()
        );
        let mut holder = Command::new("powershell")
            .args(["-NoProfile", "-Command", &script])
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let mut line = String::new();
        std::io::BufRead::read_line(&mut std::io::BufReader::new(holder.stdout.take().unwrap()), &mut line).unwrap();
        assert_eq!(line.trim(), "held");
        assert!(activity(root, HostOs::Linux).game_running);
        holder.kill().unwrap();
        holder.wait().unwrap();
        assert!(path.is_file());
        assert!(!activity(root, HostOs::Linux).game_running);
        assert!(quiesce(root, HostOs::Linux, Operation::Backup).is_ok());
    }
}
