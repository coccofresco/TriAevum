// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Data & backups: where the installation keeps the user's data and the
//! Forge log. Backup, restore and removal are UX plan WP-05B.

use eframe::egui::{self, Button, Label, RichText};

use crate::app::{Action, ForgeApp, Recorder};
use crate::texts::Msg;

pub fn show(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    record.add(ui, "data_body", "label", Label::new(app.t(Msg::DataBody)).wrap());
    ui.add_space(10.0);
    let logs = forge_core::log::logs_directory(&app.installation.data_root);
    let folders =
        [("data_folder", Msg::DataFolder, app.installation.data_root.clone()), ("forge_log", Msg::ForgeLog, logs)];
    for (name, heading, path) in folders {
        record.add(ui, name, "label", Label::new(RichText::new(app.t(heading)).strong()));
        let text = RichText::new(path.display().to_string()).monospace();
        record.add(ui, "data_path", "label", Label::new(text).wrap());
        if record.button(ui, true, "open_folder", Button::new(app.t(Msg::ButtonOpenFolder))) {
            actions.push(Action::OpenFolder(path));
        }
        ui.add_space(8.0);
    }
}

/// Opens `path` in the system's file manager.
pub fn open_folder(path: &std::path::Path) -> std::io::Result<()> {
    let program = if cfg!(windows) {
        "explorer"
    } else if cfg!(target_os = "macos") {
        "open"
    } else {
        "xdg-open"
    };
    std::process::Command::new(program).arg(path).spawn().map(drop)
}
