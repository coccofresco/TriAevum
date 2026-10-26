// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Enhancements: the HD UI texture pack and the catalogued RomFS mod, as
//! the Python window offered them (`forge_gui.py`). Both apply with the next
//! preparation. (UX plan WP-07 reworks this page.)

use eframe::egui::{self, Button, Checkbox, Label, RichText};
use forge_mods::{ArchiveContract, RomFsMod, Status, UiTexturePack};

use crate::app::{Action, ForgeApp, PickTarget, Recorder};
use crate::texts::{Msg, Texts};

/// A stored-file check: not finished yet, finished, or failed.
pub type Checked = Option<Result<Status, String>>;

fn credit_names<'a>(credits: impl Iterator<Item = &'a forge_mods::Credit>, group: &str) -> String {
    credits.filter(|credit| credit.group == group).map(|credit| credit.name.as_str()).collect::<Vec<_>>().join(", ")
}

pub fn hd_description(texts: &Texts, pack: &UiTexturePack) -> String {
    texts.get(Msg::HdDescription {
        title: &pack.title,
        version: &pack.version,
        author: &pack.author,
        credits: &credit_names(pack.credits.iter(), "Key Authors"),
        license: &pack.license_name,
    })
}

pub fn mod_description(texts: &Texts, mod_: &RomFsMod) -> String {
    texts.get(Msg::ModDescription {
        title: &mod_.title,
        version: &mod_.version,
        author: &mod_.author,
        testing: &credit_names(mod_.credits.iter(), "Testing"),
        tools: &credit_names(mod_.credits.iter(), "Tool Creation"),
        license: &mod_.license_name,
    })
}

/// `_refresh_hd`.
pub fn hd_status(texts: &Texts, contract: Option<&ArchiveContract>, state: &Checked, auto_download: bool) -> String {
    let Some(contract) = contract else {
        return texts.get(Msg::NotAvailable);
    };
    texts.get(match state {
        None => Msg::CheckingStored,
        Some(Err(error)) => Msg::HdCheckFailed { error },
        Some(Ok(Status::Ready)) => Msg::HdReady,
        Some(Ok(Status::Archive)) => Msg::HdArchive,
        Some(Ok(Status::Missing)) if auto_download => Msg::HdMissingAuto { megabytes: contract.bytes / 1_000_000 },
        Some(Ok(Status::Missing)) => Msg::HdMissing,
    })
}

/// `_refresh_mod`.
pub fn mod_status(
    texts: &Texts,
    settings_error: Option<&str>,
    contract: Option<&ArchiveContract>,
    state: &Checked,
    enabled: bool,
) -> String {
    if let Some(error) = settings_error {
        return texts.get(Msg::ModSettingsUnreadable { error });
    }
    if contract.is_none() {
        return texts.get(Msg::NotAvailable);
    }
    texts.get(match state {
        None => Msg::CheckingStored,
        Some(Err(error)) => Msg::ModCheckFailed { error },
        Some(Ok(Status::Missing)) if enabled => Msg::ModMissingEnabled,
        Some(Ok(Status::Missing)) => Msg::ModOff,
        Some(Ok(_)) if enabled => Msg::ModOn,
        Some(Ok(_)) => Msg::ModOffKept,
    })
}

/// The Home line about the HD textures of the coming preparation.
pub fn home_hd_note(texts: &Texts, contract: Option<&ArchiveContract>, state: &Checked, auto: bool) -> Option<String> {
    let contract = contract?;
    Some(texts.get(match state {
        Some(Ok(Status::Ready | Status::Archive)) => Msg::HomeHdReady,
        Some(Ok(Status::Missing)) if auto => Msg::HomeHdDownload { megabytes: contract.bytes / 1_000_000 },
        Some(Ok(Status::Missing)) => Msg::HomeHdOff,
        _ => return None,
    }))
}

fn section(ui: &mut egui::Ui, heading: &str, contents: impl FnOnce(&mut egui::Ui)) {
    egui::Frame::group(ui.style()).inner_margin(12.0).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.label(RichText::new(heading).size(16.0).strong());
        ui.add_space(4.0);
        contents(ui);
    });
}

pub fn show(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    record.add(ui, "enhancements_body", "label", Label::new(app.t(Msg::EnhancementsBody)).wrap());
    if let Some(error) = &app.contracts_error {
        let text = RichText::new(app.t(Msg::RevisionsUnreadable { error })).color(ui.visuals().error_fg_color);
        record.add(ui, "contracts_error", "label", Label::new(text).wrap());
    }
    ui.add_space(8.0);
    hd_section(app, ui, record, actions);
    ui.add_space(12.0);
    mod_section(app, ui, record, actions);
}

fn hd_section(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    let enabled = app.idle() && app.optional.hd_contract.is_some();
    let pack = app.optional.pack.clone();
    let texts = app.texts.clone();
    section(ui, &texts.get(Msg::HdHeading), |ui| {
        record.add(ui, "hd_description", "label", Label::new(hd_description(&texts, &pack)).wrap());
        ui.add_space(8.0);
        ui.horizontal_wrapped(|ui| {
            let download = Button::new(RichText::new(texts.get(Msg::HdDownload)).strong());
            if record.button(ui, enabled, "hd_download", download) {
                actions.push(Action::DownloadHd);
            }
            if record.button(ui, enabled, "hd_select_zip", Button::new(texts.get(Msg::SelectZip))) {
                actions.push(Action::Pick(PickTarget::HdArchive));
            }
            if record.button(ui, enabled, "hd_select_folder", Button::new(texts.get(Msg::SelectFolder))) {
                actions.push(Action::Pick(PickTarget::HdFolder));
            }
            let (page, license) = (texts.get(Msg::ModPage), texts.get(Msg::License));
            record.links(ui, [("hd_page", &page, &pack.page), ("hd_license", &license, &pack.license_url)]);
        });
        let check = ui.add_enabled(enabled, Checkbox::new(&mut app.hd_auto_download, texts.get(Msg::HdAutoDownload)));
        record.push("hd_auto_download", "checkbox", &check);
        if check.changed() {
            app.refresh_hd();
        }
        record.add(ui, "hd_status", "label", Label::new(&app.hd_status).wrap());
    });
}

fn mod_section(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    let available = app.optional.mod_contract.is_some() && app.mod_settings_error.is_none();
    let enabled = app.idle() && available;
    let mod_ = app.optional.mod_.clone();
    let texts = app.texts.clone();
    section(ui, &texts.get(Msg::ModHeading), |ui| {
        record.add(ui, "mod_description", "label", Label::new(mod_description(&texts, &mod_)).wrap());
        let label = texts.get(Msg::ModCheckbox { title: &mod_.title });
        let check = ui.add_enabled(enabled, Checkbox::new(&mut app.mod_enabled, label));
        record.push("mod_enabled", "checkbox", &check);
        if check.changed() {
            actions.push(Action::ToggleMod);
        }
        ui.horizontal_wrapped(|ui| {
            if record.button(ui, enabled, "mod_download", Button::new(texts.get(Msg::ModDownload))) {
                actions.push(Action::DownloadMod);
            }
            if record.button(ui, enabled, "mod_select_rar", Button::new(texts.get(Msg::SelectRar))) {
                actions.push(Action::Pick(PickTarget::ModArchive));
            }
            if record.button(ui, enabled, "mod_select_folder", Button::new(texts.get(Msg::SelectFolder))) {
                actions.push(Action::Pick(PickTarget::ModFolder));
            }
            let (page, license) = (texts.get(Msg::ModPage), texts.get(Msg::License));
            record.links(ui, [("mod_page", &page, &mod_.page), ("mod_license", &license, &mod_.license_url)]);
        });
        record.add(ui, "mod_status", "label", Label::new(&app.mod_status).wrap());
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn contract() -> ArchiveContract {
        ArchiveContract { kind: "k".into(), name: "pack.zip".into(), bytes: 451_655_008, sha256: "0".into() }
    }

    #[test]
    fn hd_status_follows_the_python_window() {
        let (texts, contract) = (Texts::default(), contract());
        assert_eq!(hd_status(&texts, None, &Some(Ok(Status::Ready)), true), texts.get(Msg::NotAvailable));
        assert_eq!(
            hd_status(&texts, Some(&contract), &Some(Ok(Status::Missing)), true),
            "Not stored yet: the HD textures (451 MB) are downloaded when you prepare and install the title. \
             Without them the game uses the native textures."
        );
        assert_eq!(
            hd_status(&texts, Some(&contract), &Some(Ok(Status::Missing)), false),
            "Not installed: the game uses the native textures."
        );
        assert!(hd_status(&texts, Some(&contract), &Some(Ok(Status::Archive)), false)
            .starts_with("HD texture archive stored."));
        assert_eq!(
            home_hd_note(&texts, Some(&contract), &Some(Ok(Status::Missing)), true).unwrap(),
            "HD interface textures (451 MB) are downloaded with the preparation. You can change this under \
             Enhancements."
        );
        assert_eq!(home_hd_note(&texts, None, &Some(Ok(Status::Missing)), true), None);
    }

    #[test]
    fn mod_status_follows_the_python_window() {
        let (texts, contract) = (Texts::default(), contract());
        let missing = Some(Ok(Status::Missing));
        assert_eq!(
            mod_status(&texts, Some("bad"), Some(&contract), &missing, true),
            "Cannot read the mod settings: bad"
        );
        assert_eq!(mod_status(&texts, None, Some(&contract), &missing, false), "Off: the game uses its native models.");
        assert_eq!(
            mod_status(&texts, None, Some(&contract), &missing, true),
            "Not downloaded. It is downloaded when you prepare and install the title."
        );
        assert_eq!(
            mod_status(&texts, None, Some(&contract), &Some(Ok(Status::Ready)), false),
            "Off: the game uses its native models. The downloaded archive is kept."
        );
    }

    #[test]
    fn descriptions_name_the_credited_people() {
        let texts = Texts::default();
        let pack = UiTexturePack::vanilla_ui_hud();
        assert!(hd_description(&texts, &pack).starts_with(
            "OoT3D Vanilla UI/HUD 3.2 by chubbycubby (credits: HenrikoMagnifico, M-1): HD textures for the HUD"
        ));
        let mod_ = RomFsMod::vanilla_plus_link();
        assert!(mod_description(&texts, &mod_).contains("Testing: Fado, M-1, seiunsky_1084. Tools: Jameriquiah."));
        assert_eq!(
            texts.get(Msg::HdDownloadFailed { error: "boom." }),
            "HD texture download failed: boom. The title is prepared with the native textures. Download the zip \
             from the mod page and select it with \"Select .zip...\", then prepare and install again."
        );
    }
}
