// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Diagnostics (UX plan WP-03): the installation state, the last game run,
//! a display recovery, the log folders, the support bundle's contents and
//! the report Copy diagnostics puts on the clipboard (forge-core
//! `diagnostics`, at most 16 KB, user names and installation folders
//! removed). "Save support bundle..." writes the redacted zip where the user
//! chooses.

use std::path::{Path, PathBuf};

use eframe::egui::{self, Button, Label, RichText};
use forge_core::diagnostics::{self, exit_meaning, GameRun};
use forge_core::explain::Explanation;
use forge_core::inventory::InventoryRequest;
use forge_core::state::InstallationState;
use serde_json::{json, Value};

use crate::app::{Action, ForgeApp, Recorder};
use crate::installation::Installation;
use crate::texts::{Msg, Texts};

/// What the page shows, collected on a task thread.
#[derive(Clone, Debug, PartialEq)]
pub struct DiagnosticsView {
    /// forge-core's redacted report.
    pub text: String,
    /// The bundle's members (name, bytes before redaction).
    pub members: Vec<(String, u64)>,
    /// Allowed files left out, with why.
    pub omitted: Vec<(String, String)>,
    pub last_run: Option<GameRun>,
    pub recovery: Option<Explanation>,
    pub game_logs: PathBuf,
    pub forge_logs: PathBuf,
}

fn request(installation: &Installation) -> InventoryRequest {
    InventoryRequest {
        data_root: Some(installation.data_root.clone()),
        sizes: false,
        ..InventoryRequest::package(&installation.root)
    }
}

impl DiagnosticsView {
    pub fn collect(installation: &Installation) -> Result<Self, String> {
        let collected = diagnostics::collect(&request(installation)).map_err(|error| error.message)?;
        let recovery = collected.recovery.as_ref().map(diagnostics::recovery_notice);
        Ok(Self {
            members: collected.plan.members.iter().map(|member| (member.name.clone(), member.size)).collect(),
            omitted: collected.plan.omitted.clone(),
            last_run: collected.last_run.clone(),
            recovery: recovery.as_ref().and_then(forge_core::explain::explain_value),
            game_logs: collected.sources.game_logs.clone(),
            forge_logs: collected.sources.forge_logs.clone(),
            text: collected.text,
        })
    }
}

/// Collects the diagnostics again and writes the support bundle to `path`.
pub fn save_bundle(installation: &Installation, path: &Path) -> Result<PathBuf, String> {
    let collected = diagnostics::collect(&request(installation)).map_err(|error| error.message)?;
    diagnostics::write_bundle(&collected.plan, path).map(|summary| summary.path).map_err(|error| error.message)
}

/// The report Copy diagnostics puts on the clipboard: the failure on screen
/// and forge-core's report when it was collected, else the window's own
/// facts as JSON. Redacted, at most 16 KB.
pub fn report(
    state: Option<&InstallationState>,
    failure: Option<&Explanation>,
    notices: &[Explanation],
    collected: Option<&str>,
) -> String {
    let Some(collected) = collected else {
        let value = json!({
            "forge": env!("CARGO_PKG_VERSION"),
            "platform": format!("{}-{}", std::env::consts::OS, std::env::consts::ARCH),
            "state": state.map_or(Value::Null, InstallationState::to_value),
            "failure": failure.map_or(Value::Null, Explanation::to_value),
            "notices": notices.iter().map(Explanation::to_value).collect::<Vec<_>>(),
        });
        let text = String::from_utf8_lossy(&forge_base::legacy_json::pretty_file_bytes(&value)).into_owned();
        return forge_core::redact::redact(&text);
    };
    let mut text = String::new();
    for explanation in failure.into_iter().chain(notices) {
        text.push_str(&format!("{}\n{}\n\n", explanation.title, forge_core::redact::redact(&explanation.details)));
    }
    text.push_str(collected);
    diagnostics::truncate_text(text, diagnostics::TEXT_LIMIT)
}

fn size_text(texts: &Texts, bytes: u64) -> String {
    if bytes < 1024 {
        texts.get(Msg::Size { value: &bytes.to_string(), power: 0 })
    } else {
        texts.get(Msg::Size { value: &bytes.div_ceil(1024).to_string(), power: 1 })
    }
}

pub fn show(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    record.add(ui, "diagnostics_body", "label", Label::new(app.t(Msg::DiagnosticsBody)).wrap());
    ui.add_space(8.0);
    let state = app.state.as_ref().map_or("checking", InstallationState::code);
    record.add(ui, "diagnostics_state", "label", Label::new(app.t(Msg::StateLabel { state })).wrap());
    let view = match &app.diagnostics {
        Some(Ok(view)) => Some(view.as_ref().clone()),
        _ => None,
    };
    let last_run = match view.as_ref().and_then(|view| view.last_run.as_ref()) {
        _ if app.game_running => app.t(Msg::GameRunning),
        Some(run) => app.t(Msg::LastGameRun {
            started: &forge_core::log::timestamp(run.started),
            seconds: run.duration.as_secs(),
            meaning: &exit_meaning(run.exit_code),
        }),
        None => app.t(Msg::NoGameRun),
    };
    record.add(ui, "diagnostics_last_run", "label", Label::new(last_run).wrap());
    if let Some(recovery) = view.as_ref().and_then(|view| view.recovery.as_ref()) {
        ui.add_space(6.0);
        let title = app.texts.title(recovery);
        record.add(ui, "diagnostics_recovery", "label", Label::new(RichText::new(title).strong()).wrap());
        record.add(ui, "diagnostics_recovery_details", "label", Label::new(recovery.details.as_str()).wrap());
    }
    ui.add_space(6.0);
    ui.horizontal_wrapped(|ui| {
        if record.button(ui, true, "diagnostics_copy", Button::new(app.t(Msg::ButtonCopyDiagnostics))) {
            actions.push(Action::CopyDiagnostics);
        }
        if record.button(ui, view.is_some(), "diagnostics_save_bundle", Button::new(app.t(Msg::ButtonSaveBundle))) {
            actions.push(Action::SaveBundle);
        }
        if record.button(ui, app.idle(), "diagnostics_check", Button::new(app.t(Msg::ButtonCheckInstallation))) {
            actions.push(Action::CheckInstallation);
        }
    });
    if app.copied {
        record.add(ui, "diagnostics_copied", "label", Label::new(app.t(Msg::Copied)));
    }
    match &app.diagnostics {
        Some(Err(error)) => {
            let text = app.t(Msg::DiagnosticsFailed { error });
            record.add(ui, "diagnostics_failed", "label", Label::new(text).wrap());
        }
        None => {
            record.add(ui, "diagnostics_collecting", "label", Label::new(app.t(Msg::CollectingDiagnostics)));
        }
        Some(Ok(_)) => {}
    }
    if cfg!(target_os = "linux") {
        // Steam Deck and controller-only use (UX plan WP-11).
        ui.add_space(8.0);
        let heading = RichText::new(app.t(Msg::SteamDeckHeading)).strong();
        record.add(ui, "steam_deck_heading", "label", Label::new(heading).wrap());
        record.add(ui, "steam_deck_body", "label", Label::new(app.t(Msg::SteamDeckBody)).wrap());
    }
    if let Some(view) = &view {
        ui.add_space(8.0);
        let folders =
            [("game_logs", Msg::GameLogFolder, &view.game_logs), ("forge_logs", Msg::ForgeLog, &view.forge_logs)];
        for (name, heading, path) in folders {
            record.add(ui, name, "label", Label::new(RichText::new(app.t(heading)).strong()));
            let text = RichText::new(forge_core::redact::redact(&path.display().to_string())).monospace();
            record.add(ui, "diagnostics_path", "label", Label::new(text).wrap());
            if record.button(ui, true, "open_folder", Button::new(app.t(Msg::ButtonOpenFolder))) {
                actions.push(Action::OpenFolder(path.clone()));
            }
            ui.add_space(4.0);
        }
        ui.add_space(4.0);
        let heading = RichText::new(app.t(Msg::BundleContents)).strong();
        record.add(ui, "diagnostics_bundle", "label", Label::new(heading).wrap());
        for (name, size) in &view.members {
            let line = RichText::new(format!("{name}  ({})", size_text(&app.texts, *size))).monospace().small();
            record.add(ui, "diagnostics_member", "label", Label::new(line));
        }
        for (name, reason) in &view.omitted {
            let line = app.t(Msg::BundleOmitted { name, reason });
            record.add(ui, "diagnostics_omitted", "label", Label::new(RichText::new(line).small()).wrap());
        }
    }
    ui.add_space(6.0);
    let collected = view.as_ref().map(|view| view.text.as_str());
    let text = report(app.state.as_ref(), app.failure.as_ref(), &app.notices, collected);
    record.add(ui, "diagnostics_report", "label", Label::new(RichText::new(text).monospace().small()).wrap());
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_report_carries_the_state_and_the_failure() {
        let failure = forge_core::explain::explain(&forge_base::ForgeError::invalid("bad ROM"));
        let report = report(Some(&InstallationState::NotInstalled), Some(&failure), &[], None);
        let value: Value = serde_json::from_str(&report).unwrap();
        assert_eq!(value["state"]["state"], "not_installed");
        assert_eq!(value["failure"]["id"], "invalid_input");
        assert_eq!(value["forge"], env!("CARGO_PKG_VERSION"));
    }

    #[test]
    fn the_collected_report_leads_with_the_failure_and_stays_bounded() {
        let failure = forge_core::explain::explain_outcome(
            forge_core::explain::Outcome::GameExitedWithError,
            r"Exit code: 1 (the game stopped with an error)
Last lines in C:\Users\Someone\TriAevum\logs",
        );
        let collected = "TriAevum diagnostics, 2026-10-26T12:00:00Z\n".to_string() + &"x".repeat(40_000);
        let text = report(None, Some(&failure), &[], Some(&collected));
        assert!(text.starts_with("The game closed with an error\nExit code: 1"), "{}", &text[..80]);
        assert!(text.contains(r"C:\Users\<user>\TriAevum") && !text.contains("Someone"));
        assert!(text.len() <= diagnostics::TEXT_LIMIT);
    }

    #[test]
    fn a_saved_bundle_holds_the_report() {
        let directory = tempfile::tempdir().unwrap();
        let installation = Installation::at(directory.path()).unwrap();
        let path = directory.path().join("out").join("bundle.zip");
        assert_eq!(save_bundle(&installation, &path).unwrap(), path);
        assert!(path.is_file());
        let view = DiagnosticsView::collect(&installation).unwrap();
        assert_eq!(view.members[0].0, "diagnostics.txt");
        assert!(view.text.starts_with("TriAevum diagnostics, "));
    }
}
