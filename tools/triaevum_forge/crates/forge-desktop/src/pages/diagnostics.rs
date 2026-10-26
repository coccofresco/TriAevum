// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Diagnostics: the installation state and the last failure as text a user
//! can copy into a report, with user names removed. The support bundle and
//! the runtime's exit report are UX plan WP-03.

use eframe::egui::{self, Button, Label, RichText};
use forge_core::explain::Explanation;
use forge_core::state::InstallationState;
use serde_json::{json, Value};

use crate::app::{Action, ForgeApp, Recorder};
use crate::texts::Msg;

/// The report Copy diagnostics puts on the clipboard.
pub fn report(state: Option<&InstallationState>, failure: Option<&Explanation>, notices: &[Explanation]) -> String {
    let value = json!({
        "forge": env!("CARGO_PKG_VERSION"),
        "platform": format!("{}-{}", std::env::consts::OS, std::env::consts::ARCH),
        "state": state.map_or(Value::Null, InstallationState::to_value),
        "failure": failure.map_or(Value::Null, Explanation::to_value),
        "notices": notices.iter().map(Explanation::to_value).collect::<Vec<_>>(),
    });
    let text = String::from_utf8_lossy(&forge_base::legacy_json::pretty_file_bytes(&value)).into_owned();
    forge_core::redact::redact(&text)
}

pub fn show(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    record.add(ui, "diagnostics_body", "label", Label::new(app.t(Msg::DiagnosticsBody)).wrap());
    ui.add_space(8.0);
    let state = app.state.as_ref().map_or("checking", InstallationState::code);
    record.add(ui, "diagnostics_state", "label", Label::new(app.t(Msg::StateLabel { state })).wrap());
    ui.add_space(6.0);
    ui.horizontal_wrapped(|ui| {
        if record.button(ui, true, "diagnostics_copy", Button::new(app.t(Msg::ButtonCopyDiagnostics))) {
            actions.push(Action::CopyDiagnostics);
        }
        if record.button(ui, app.idle(), "diagnostics_check", Button::new(app.t(Msg::ButtonCheckInstallation))) {
            actions.push(Action::CheckInstallation);
        }
    });
    if app.copied {
        record.add(ui, "diagnostics_copied", "label", Label::new(app.t(Msg::Copied)));
    }
    ui.add_space(6.0);
    let text = report(app.state.as_ref(), app.failure.as_ref(), &app.notices);
    record.add(ui, "diagnostics_report", "label", Label::new(RichText::new(text).monospace().small()).wrap());
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_report_carries_the_state_and_the_failure() {
        let failure = forge_core::explain::explain(&forge_base::ForgeError::invalid("bad ROM"));
        let report = report(Some(&InstallationState::NotInstalled), Some(&failure), &[]);
        let value: Value = serde_json::from_str(&report).unwrap();
        assert_eq!(value["state"]["state"], "not_installed");
        assert_eq!(value["failure"]["id"], "invalid_input");
        assert_eq!(value["forge"], env!("CARGO_PKG_VERSION"));
    }
}
