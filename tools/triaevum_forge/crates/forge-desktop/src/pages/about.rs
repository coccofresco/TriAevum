// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! About: version, license, links, the supported revisions and the Forge
//! language switch.

use std::path::Path;

use eframe::egui::{self, Label, RichText};

use crate::app::{Action, ForgeApp, Recorder};
use crate::texts::{native_name, Msg};

pub const PROJECT_URL: &str = "https://github.com/coccofresco/TriAevum";
pub const LICENSE_URL: &str = "https://www.gnu.org/licenses/gpl-3.0.html";

/// `release.version` of the package's release manifest, else Forge's own.
pub fn package_version(package_root: &Path) -> String {
    ["release-manifest.json", "docs/release-manifest.json"]
        .iter()
        .find_map(|relative| {
            let manifest = forge_base::fsutil::load_json_object(&package_root.join(relative)).ok()?;
            manifest.get("release")?.get("version")?.as_str().map(str::to_string)
        })
        .unwrap_or_else(|| env!("CARGO_PKG_VERSION").to_string())
}

pub fn show(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    let version = app.t(Msg::Version { version: &app.package_version });
    record.add(ui, "about_version", "label", Label::new(RichText::new(version).strong()));
    record.add(ui, "about_body", "label", Label::new(app.t(Msg::AboutBody)).wrap());
    ui.horizontal_wrapped(|ui| {
        let (project, license) = (app.t(Msg::ProjectPage), app.t(Msg::License));
        record.links(ui, [("about_project", &project, PROJECT_URL), ("about_license", &license, LICENSE_URL)]);
    });
    ui.add_space(10.0);
    record.add(ui, "about_revisions", "label", Label::new(RichText::new(app.t(Msg::SupportedRevisions)).strong()));
    if app.revisions.is_empty() {
        record.add(ui, "about_revision", "label", Label::new(app.t(Msg::NoSupportedRevisions)).wrap());
    }
    for (_, label) in &app.revisions {
        record.add(ui, "about_revision", "label", Label::new(format!("\u{2022} {label}")).wrap());
    }
    ui.add_space(10.0);
    record.add(ui, "about_language", "label", Label::new(RichText::new(app.t(Msg::ForgeLanguage)).strong()));
    let choices = app.ui_languages();
    let current = app.texts.language().to_string();
    let label = |code: &str| {
        choices
            .iter()
            .find(|(choice, _)| choice == code)
            .map_or(native_name(code).to_string(), |(_, label)| label.clone())
    };
    let combo = egui::ComboBox::from_id_salt("ui_language").selected_text(label(&current)).show_ui(ui, |ui| {
        for (code, name) in &choices {
            if ui.selectable_label(*code == current, name).clicked() && *code != current {
                actions.push(Action::SetUiLanguage(code.clone()));
            }
        }
    });
    record.push("ui_language", "combo", &combo.response);
    record.add(ui, "about_language_note", "label", Label::new(app.t(Msg::ForgeLanguageNote)).wrap());
}
