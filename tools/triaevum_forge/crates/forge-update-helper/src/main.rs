// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `triaevum-update-helper`: the Windows "Update from ZIP" helper (UX plan
//! WP-04B). Forge copies it from the new package into the staging folder
//! (`A/.update-<version>-<pid>/`) and starts it with the proposal and an
//! inherited handle of the Forge process in its environment; the work is
//! `forge_core::update_helper::run`. Started without a proposal (for
//! example by double-clicking it after a crash), it settles the pending
//! update of the installation its folder belongs to and opens Forge.
//!
//! Exit status: 0 updated (or nothing pending), 2 invalid start, 3 rolled
//! back, 4 refused (nothing changed), 5 recovery required.

#![cfg_attr(all(windows, not(test)), windows_subsystem = "windows")]

use std::process::ExitCode;

use forge_core::update_helper::{run, HelperArgs};
use forge_core::update_journal::Faults;

fn main() -> ExitCode {
    let status = run(HelperArgs::from_environment(), &Faults::none());
    ExitCode::from(u8::try_from(status).unwrap_or(1))
}
