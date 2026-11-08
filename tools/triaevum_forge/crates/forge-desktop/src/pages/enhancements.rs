// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Enhancements (UX plan WP-07): the HD interface pack, the catalogued
//! Vanilla+ Link remodel, the user's own texture pack and the explicitly
//! unsupported custom RomFS folder (owner decision 6). Each card shows three
//! separate states: downloaded or imported, enabled (the user's choice) and
//! applied (what the installed game routes, `forge_core::enhancements`).
//! "Apply changes" runs the repair in place with the current choices from
//! the game data kept from the ROM (`maintenance::Kind::ApplyEnhancements`);
//! the ROM is asked for only when that data no longer verifies.

use std::path::PathBuf;
use std::sync::mpsc::{Receiver, Sender, channel};

use eframe::egui::{self, Button, Checkbox, Label, RichText, Stroke};
use forge_base::CancelToken;
use forge_core::inventory::InventoryRequest;
use forge_mods::{ArchiveContract, RomFsMod, Status, UiTexturePack};
use serde_json::Value;

use crate::app::{Action, ForgeApp, PickTarget, Recorder};
use crate::installation::Installation;
use crate::texts::{Msg, Texts};

/// A stored-file check: not finished yet, finished, or failed.
pub type Checked = Option<Result<Status, String>>;

/// What the page asks the window to do.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EnhancementsAction {
    Refresh,
    SetHd(bool),
    SetCustomTextures(bool),
    UseManagedFolder,
    PickTextures { zip: bool },
    ImportLegacy(PathBuf),
    ConfirmFlat,
    CancelFlat,
    AskCustomRomfs,
    ConfirmCustomRomfs,
    CancelCustomRomfs,
    DisableCustomRomfs,
    Apply,
}

enum EnhancementsEvent {
    Loaded(Result<Value, String>),
    Picked(Option<PathBuf>),
    Imported { source: PathBuf, result: Result<Value, String> },
    Changed(Result<Value, String>),
}

#[derive(Default)]
pub struct EnhancementsState {
    /// `forge_core::enhancements::status`.
    pub(crate) status: Option<Value>,
    pub(crate) error: Option<String>,
    /// The states changed (an action, a download, an Apply): read again.
    stale: bool,
    loading: bool,
    /// A task of this page runs (its status text).
    pub(crate) working: Option<String>,
    pub(crate) outcome: Option<Result<String, String>>,
    /// The warning before the custom RomFS folder is turned on is open.
    pub(crate) confirm_custom_romfs: bool,
    /// A flat pack waits for the user's confirmation.
    pub(crate) flat_pending: Option<PathBuf>,
    sender: Option<Sender<EnhancementsEvent>>,
    receiver: Option<Receiver<EnhancementsEvent>>,
}

impl EnhancementsState {
    fn channel(&mut self) -> Sender<EnhancementsEvent> {
        if self.sender.is_none() {
            let (sender, receiver) = channel();
            (self.sender, self.receiver) = (Some(sender), Some(receiver));
        }
        self.sender.clone().expect("created above")
    }
}

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

fn inventory_request(installation: &Installation) -> InventoryRequest {
    InventoryRequest {
        data_root: Some(installation.data_root.clone()),
        sizes: false,
        ..InventoryRequest::package(&installation.root)
    }
}

/// Reads the states again (on a task thread).
pub fn refresh(app: &mut ForgeApp) {
    if app.freeze_state || app.enhancements.loading {
        return;
    }
    app.enhancements.loading = true;
    app.enhancements.stale = false;
    let sender = app.enhancements.channel();
    let request = inventory_request(&app.installation);
    std::thread::spawn(move || {
        let status = forge_core::enhancements::status(&request, &CancelToken::new()).map_err(|error| error.message);
        let _ = sender.send(EnhancementsEvent::Loaded(status));
    });
}

/// Something the states depend on changed (a download, an import, an
/// installation): the page reads them again when it is shown.
pub fn invalidate(app: &mut ForgeApp) {
    app.enhancements.stale = true;
}

/// Handles the task threads' events; called every frame.
pub fn poll(app: &mut ForgeApp, context: &egui::Context) {
    let events: Vec<EnhancementsEvent> = match &app.enhancements.receiver {
        Some(receiver) => receiver.try_iter().collect(),
        None => return,
    };
    for event in events {
        match event {
            EnhancementsEvent::Loaded(status) => {
                app.enhancements.loading = false;
                match status {
                    Ok(status) => (app.enhancements.status, app.enhancements.error) = (Some(status), None),
                    Err(error) => app.enhancements.error = Some(error),
                }
            }
            EnhancementsEvent::Picked(path) => {
                app.enhancements.working = None;
                app.busy = false;
                if let Some(path) = path {
                    import(app, path, false);
                }
            }
            EnhancementsEvent::Imported { source, result } => {
                finish(app);
                match result {
                    Ok(result) if result["status"] == "confirmation_required" => {
                        app.enhancements.flat_pending = Some(source);
                    }
                    Ok(result) => {
                        let textures = result["import"]["textures"].as_u64().unwrap_or(0);
                        let files = result["import"]["files"].as_u64().unwrap_or(0);
                        let mut message = app.t(Msg::TexturesImportDone { textures, files });
                        if let Some(title) = result["import"]["source_title"].as_str() {
                            message.push(' ');
                            message.push_str(&app.t(Msg::TexturesImportOtherTitle { title }));
                        }
                        if let Some(error) = result["settings_error"].as_str() {
                            message.push(' ');
                            message.push_str(&app.t(Msg::TexturesSettingsFailed { error }));
                        }
                        app.enhancements.outcome = Some(Ok(message));
                    }
                    Err(error) => {
                        app.enhancements.outcome = Some(Err(app.t(Msg::TexturesImportFailed { error: &error })))
                    }
                }
            }
            EnhancementsEvent::Changed(result) => {
                finish(app);
                match result {
                    Ok(status) => app.enhancements.status = Some(status),
                    Err(error) => app.enhancements.outcome = Some(Err(app.t(Msg::SettingNotSaved { error: &error }))),
                }
            }
        }
    }
    if app.enhancements.loading || app.enhancements.working.is_some() {
        context.request_repaint_after(std::time::Duration::from_millis(100));
    }
}

fn finish(app: &mut ForgeApp) {
    app.busy = false;
    app.enhancements.working = None;
    app.enhancements.stale = true;
}

fn begin(app: &mut ForgeApp, status: Msg) -> Option<Sender<EnhancementsEvent>> {
    if !app.idle() {
        return None;
    }
    let text = app.t(status);
    app.busy = true;
    app.enhancements.outcome = None;
    app.enhancements.working = Some(text);
    Some(app.enhancements.channel())
}

fn import(app: &mut ForgeApp, source: PathBuf, flat_confirmed: bool) {
    let Some(sender) = begin(app, Msg::TexturesImporting) else { return };
    let request = inventory_request(&app.installation);
    std::thread::spawn(move || {
        let result = forge_core::enhancements::import_textures(&request, &source, flat_confirmed, &CancelToken::new())
            .map_err(|error| error.message);
        let _ = sender.send(EnhancementsEvent::Imported { source, result });
    });
}

fn change(app: &mut ForgeApp, change: forge_core::enhancements::SettingsChange) {
    let Some(sender) = begin(app, Msg::SavingSetting) else { return };
    let request = inventory_request(&app.installation);
    std::thread::spawn(move || {
        let result = forge_core::enhancements::change_settings(&request, &change, &CancelToken::new())
            .map_err(|error| error.message);
        let _ = sender.send(EnhancementsEvent::Changed(result));
    });
}

fn pick_textures(app: &mut ForgeApp, zip: bool, frame: Option<&eframe::Frame>) {
    let Some(sender) = begin(app, Msg::TexturesChoosing) else { return };
    let dialog = rfd::FileDialog::new();
    let dialog = if zip {
        dialog.set_title(app.t(Msg::PickTexturesZip)).add_filter(app.t(Msg::FilterZip), &["zip"])
    } else {
        dialog.set_title(app.t(Msg::PickTexturesFolder))
    };
    let dialog = match frame {
        Some(frame) => dialog.set_parent(frame),
        None => dialog,
    };
    std::thread::spawn(move || {
        let path = if zip { dialog.pick_file() } else { dialog.pick_folder() };
        let _ = sender.send(EnhancementsEvent::Picked(path));
    });
}

pub fn apply(app: &mut ForgeApp, action: EnhancementsAction, frame: Option<&eframe::Frame>) {
    use forge_core::enhancements::SettingsChange;
    match action {
        EnhancementsAction::Refresh => refresh(app),
        EnhancementsAction::SetHd(enabled) => {
            // Kept for the next sessions (decision 5), at once.
            app.hd_auto_download = enabled;
            if let Err(error) = forge_core::enhancements::set_hd_pack_enabled(&app.installation.data_root, enabled) {
                app.enhancements.outcome = Some(Err(app.t(Msg::HdPreferenceNotSaved { error: &error.message })));
            }
            app.refresh_hd();
            invalidate(app);
        }
        EnhancementsAction::SetCustomTextures(enabled) => {
            change(app, SettingsChange { custom_textures: Some(enabled), ..Default::default() })
        }
        EnhancementsAction::UseManagedFolder => {
            change(app, SettingsChange { use_managed_texture_folder: true, ..Default::default() })
        }
        EnhancementsAction::PickTextures { zip } => pick_textures(app, zip, frame),
        EnhancementsAction::ImportLegacy(folder) => import(app, folder, false),
        EnhancementsAction::ConfirmFlat => {
            if let Some(source) = app.enhancements.flat_pending.take() {
                import(app, source, true);
            }
        }
        EnhancementsAction::CancelFlat => app.enhancements.flat_pending = None,
        EnhancementsAction::AskCustomRomfs => app.enhancements.confirm_custom_romfs = true,
        EnhancementsAction::CancelCustomRomfs => app.enhancements.confirm_custom_romfs = false,
        EnhancementsAction::ConfirmCustomRomfs => {
            app.enhancements.confirm_custom_romfs = false;
            change(app, SettingsChange { custom_romfs: Some(true), ..Default::default() });
        }
        EnhancementsAction::DisableCustomRomfs => {
            change(app, SettingsChange { custom_romfs: Some(false), ..Default::default() })
        }
        EnhancementsAction::Apply => crate::maintenance::begin(app, crate::maintenance::Kind::ApplyEnhancements),
    }
}

// ---- Drawing ----

fn section(ui: &mut egui::Ui, heading: &str, contents: impl FnOnce(&mut egui::Ui)) {
    egui::Frame::group(ui.style()).inner_margin(12.0).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.label(RichText::new(heading).size(16.0).strong());
        ui.add_space(4.0);
        contents(ui);
    });
}

fn line(ui: &mut egui::Ui, record: &mut Recorder, name: &'static str, text: impl Into<RichText>) {
    record.add(ui, name, "label", Label::new(text.into()).wrap());
}

/// The three states of a card, one line each.
fn states(ui: &mut egui::Ui, record: &mut Recorder, names: [&'static str; 3], lines: [String; 3]) {
    for (name, text) in names.into_iter().zip(lines) {
        line(ui, record, name, RichText::new(text).small().strong());
    }
}

fn applied_text(texts: &Texts, installed: bool, enabled: bool, applied: bool, pending: bool) -> String {
    texts.get(match (installed, applied, pending) {
        (false, _, _) if enabled => Msg::StateAppliedLater,
        (false, _, _) => Msg::StateNotApplied,
        (true, _, true) => Msg::StateApplyPending,
        (true, true, _) => Msg::StateApplied,
        (true, false, _) => Msg::StateNotApplied,
    })
}

fn enabled_text(texts: &Texts, enabled: bool) -> String {
    texts.get(if enabled { Msg::StateOn } else { Msg::StateOff })
}

pub fn show(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    let wanted = app.enhancements.status.is_none() && app.enhancements.error.is_none() || app.enhancements.stale;
    if wanted && !app.enhancements.loading && !app.freeze_state && !app.busy {
        actions.push(Action::Enhancements(EnhancementsAction::Refresh));
    }
    record.add(ui, "enhancements_body", "label", Label::new(app.t(Msg::EnhancementsBody)).wrap());
    if let Some(error) = &app.contracts_error {
        let text = RichText::new(app.t(Msg::RevisionsUnreadable { error })).color(ui.visuals().error_fg_color);
        record.add(ui, "contracts_error", "label", Label::new(text).wrap());
    }
    if let Some(working) = app.enhancements.working.clone() {
        ui.horizontal_wrapped(|ui| {
            ui.add(egui::Spinner::new());
            line(ui, record, "enhancements_working", RichText::new(working).strong());
        });
    }
    match app.enhancements.outcome.clone() {
        Some(Ok(message)) => {
            let color = ui.visuals().selection.stroke.color;
            line(ui, record, "enhancements_outcome", RichText::new(message).color(color));
        }
        Some(Err(message)) => {
            line(ui, record, "enhancements_outcome", RichText::new(message).color(ui.visuals().error_fg_color));
        }
        None => {}
    }
    if let Some(error) = app.enhancements.error.clone() {
        let text = app.t(Msg::EnhancementsStatusFailed { error: &error });
        line(ui, record, "enhancements_error", RichText::new(text).color(ui.visuals().error_fg_color));
    }
    let status = app.enhancements.status.clone().unwrap_or(Value::Null);
    if status.is_null() && app.enhancements.error.is_none() {
        ui.horizontal_wrapped(|ui| {
            ui.add(egui::Spinner::new());
            line(ui, record, "enhancements_loading", app.t(Msg::EnhancementsLoading));
        });
    }
    ui.add_space(8.0);
    apply_section(app, ui, record, actions, &status);
    ui.add_space(12.0);
    hd_section(app, ui, record, actions, &status);
    ui.add_space(12.0);
    mod_section(app, ui, record, actions, &status);
    ui.add_space(12.0);
    textures_section(app, ui, record, actions, &status);
    ui.add_space(12.0);
    custom_romfs_section(app, ui, record, actions, &status);
    ui.add_space(12.0);
    downloads_folder(app, ui, record, actions);
}

fn apply_section(app: &ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>, status: &Value) {
    let texts = app.texts.clone();
    let installed = status["installed"] == true;
    let running = status["game_running"] == true;
    section(ui, &texts.get(Msg::ApplyHeading), |ui| {
        let (msg, strong) = match (installed, status["apply_needed"] == true) {
            (false, _) => (Msg::ApplyNotInstalled, false),
            (true, true) => (Msg::ApplyPending, true),
            (true, false) => (Msg::ApplyCurrent, false),
        };
        let mut text = RichText::new(texts.get(msg));
        if strong {
            text = text.strong().color(ui.visuals().warn_fg_color);
        }
        line(ui, record, "apply_state", text);
        if running {
            let text = RichText::new(texts.get(Msg::EnhancementsGameRunning)).color(ui.visuals().warn_fg_color);
            line(ui, record, "apply_running", text);
        }
        line(ui, record, "apply_body", texts.get(Msg::ApplyBody));
        // Decision 4 (WP-04A): the layout is kept; Apply asks first when it differs.
        let policy =
            if status["hud_layout"]["differs"] == true { Msg::HudLayoutPolicyAsk } else { Msg::HudLayoutPolicyKeep };
        line(ui, record, "apply_hud_layout", RichText::new(texts.get(policy)).small());
        let enabled = installed && !running && app.idle();
        let button = Button::new(RichText::new(texts.get(Msg::ButtonApplyChanges)).strong());
        if record.button(ui, enabled, "apply_changes", button) {
            actions.push(Action::Enhancements(EnhancementsAction::Apply));
        }
    });
}

/// Where the archives are kept (UX plan WP-11). Without a network the
/// recovery is an archive downloaded elsewhere and selected with the buttons
/// above; an AppImage or Flatpak package folder is read-only, so no text asks
/// to place an archive beside Forge.
fn downloads_folder(app: &ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    let folder = forge_mods::downloads_directory(&app.installation.data_root);
    record.add(ui, "downloads_note", "label", Label::new(app.t(Msg::DownloadsFolderNote)).wrap());
    let path = RichText::new(folder.display().to_string()).monospace();
    record.add(ui, "downloads_path", "label", Label::new(path).wrap());
    if record.button(ui, true, "downloads_open", Button::new(app.t(Msg::ButtonOpenFolder))) {
        actions.push(Action::OpenFolder(folder));
    }
}

fn hd_section(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>, status: &Value) {
    let enabled = app.idle() && app.optional.hd_contract.is_some();
    let pack = app.optional.pack.clone();
    let texts = app.texts.clone();
    let installed = status["installed"] == true;
    let hd = &status["hd_pack"];
    section(ui, &texts.get(Msg::HdHeading), |ui| {
        record.add(ui, "hd_description", "label", Label::new(hd_description(&texts, &pack)).wrap());
        ui.add_space(6.0);
        let stored = matches!(hd["stored"].as_str(), Some("ready" | "archive"));
        let downloaded = if stored {
            texts.get(Msg::StateDownloaded { version: &pack.version })
        } else {
            texts.get(Msg::StateNotDownloaded)
        };
        let applied = hd["applied"] == true;
        let pending = installed && ((app.hd_auto_download && stored && !applied) || (!app.hd_auto_download && applied));
        states(
            ui,
            record,
            ["hd_state_stored", "hd_state_enabled", "hd_state_applied"],
            [
                downloaded,
                enabled_text(&texts, app.hd_auto_download),
                applied_text(&texts, installed, app.hd_auto_download, applied, pending),
            ],
        );
        ui.add_space(6.0);
        let mut choice = app.hd_auto_download;
        let check = ui.add_enabled(enabled, Checkbox::new(&mut choice, texts.get(Msg::HdAutoDownload)));
        record.push("hd_auto_download", "checkbox", &check);
        if check.changed() {
            actions.push(Action::Enhancements(EnhancementsAction::SetHd(choice)));
        }
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
        record.add(ui, "hd_status", "label", Label::new(&app.hd_status).wrap());
    });
}

fn mod_section(
    app: &mut ForgeApp,
    ui: &mut egui::Ui,
    record: &mut Recorder,
    actions: &mut Vec<Action>,
    status: &Value,
) {
    let available = app.optional.mod_contract.is_some() && app.mod_settings_error.is_none();
    let enabled = app.idle() && available;
    let mod_ = app.optional.mod_.clone();
    let texts = app.texts.clone();
    let installed = status["installed"] == true;
    let entry = status["mods"]
        .as_array()
        .and_then(|mods| mods.iter().find(|entry| entry["id"] == mod_.mod_id.as_str()))
        .cloned()
        .unwrap_or(Value::Null);
    section(ui, &texts.get(Msg::ModHeading), |ui| {
        record.add(ui, "mod_description", "label", Label::new(mod_description(&texts, &mod_)).wrap());
        ui.add_space(6.0);
        let stored = matches!(entry["stored"].as_str(), Some("ready" | "archive"));
        let downloaded = if stored {
            texts.get(Msg::StateDownloaded { version: &mod_.version })
        } else {
            texts.get(Msg::StateNotDownloaded)
        };
        let applied = entry["applied"] == true;
        let pending = installed && ((app.mod_enabled && stored && !applied) || (!app.mod_enabled && applied));
        states(
            ui,
            record,
            ["mod_state_stored", "mod_state_enabled", "mod_state_applied"],
            [
                downloaded,
                enabled_text(&texts, app.mod_enabled),
                applied_text(&texts, installed, app.mod_enabled, applied, pending),
            ],
        );
        ui.add_space(6.0);
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

fn textures_section(
    app: &ForgeApp,
    ui: &mut egui::Ui,
    record: &mut Recorder,
    actions: &mut Vec<Action>,
    status: &Value,
) {
    let texts = app.texts.clone();
    let textures = &status["custom_textures"];
    let installed = status["installed"] == true;
    let idle = app.idle() && status["game_running"] != true;
    section(ui, &texts.get(Msg::TexturesHeading), |ui| {
        line(ui, record, "textures_description", texts.get(Msg::TexturesDescription));
        ui.add_space(6.0);
        let files = textures["files"].as_u64().unwrap_or(0);
        let imported =
            if files > 0 { texts.get(Msg::TexturesImported { files }) } else { texts.get(Msg::TexturesNone) };
        let enabled = textures["enabled"] == true;
        let applied = texts.get(match (textures["applied"] == true, textures["shadowed_by"].as_str()) {
            (true, _) => Msg::TexturesApplied,
            (false, _) if !installed => Msg::StateAppliedLater,
            (false, Some("settings")) => Msg::TexturesShadowedSettings,
            (false, Some("environment")) => Msg::TexturesShadowedEnvironment,
            (false, Some("azahar_folder")) => Msg::TexturesShadowedAzahar,
            (false, Some("older_profile")) => Msg::TexturesOlderProfile,
            (false, _) => Msg::StateNotApplied,
        });
        states(
            ui,
            record,
            ["textures_state_imported", "textures_state_enabled", "textures_state_applied"],
            [imported, enabled_text(&texts, enabled), applied],
        );
        if let Some(folder) = textures["effective_folder"].as_str().filter(|_| textures["routed"] != true) {
            line(ui, record, "textures_effective", RichText::new(folder).monospace().small());
        }
        if textures["shadowed_by"] == "settings"
            && record.button(
                ui,
                idle && installed,
                "textures_use_folder",
                Button::new(texts.get(Msg::ButtonUseThisFolder)),
            )
        {
            actions.push(Action::Enhancements(EnhancementsAction::UseManagedFolder));
        }
        if let Some(error) = textures["settings_error"].as_str() {
            line(
                ui,
                record,
                "textures_settings_error",
                RichText::new(texts.get(Msg::TexturesSettingsFailed { error })).color(ui.visuals().error_fg_color),
            );
        }
        ui.add_space(6.0);
        let mut choice = enabled;
        let usable = idle && installed && textures["settings_error"].is_null();
        let check = ui.add_enabled(usable, Checkbox::new(&mut choice, texts.get(Msg::TexturesLoadCheckbox)));
        record.push("textures_enabled", "checkbox", &check);
        if check.changed() {
            actions.push(Action::Enhancements(EnhancementsAction::SetCustomTextures(choice)));
        }
        if let Some(legacy) = textures["legacy_folder"].as_str() {
            line(ui, record, "textures_legacy", texts.get(Msg::TexturesLegacy));
            line(ui, record, "textures_legacy_path", RichText::new(legacy).monospace().small());
            let button = Button::new(texts.get(Msg::ButtonImportLegacy));
            if record.button(ui, idle && installed, "textures_import_legacy", button) {
                actions.push(Action::Enhancements(EnhancementsAction::ImportLegacy(PathBuf::from(legacy))));
            }
        }
        if let Some(pending) = &app.enhancements.flat_pending {
            let warn = ui.visuals().warn_fg_color;
            egui::Frame::group(ui.style()).inner_margin(8.0).stroke(Stroke::new(1.5, warn)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                line(
                    ui,
                    record,
                    "textures_flat_question",
                    RichText::new(texts.get(Msg::TexturesFlatQuestion)).strong(),
                );
                line(
                    ui,
                    record,
                    "textures_flat_source",
                    RichText::new(pending.display().to_string()).monospace().small(),
                );
                ui.horizontal_wrapped(|ui| {
                    if record.button(
                        ui,
                        app.idle(),
                        "textures_flat_confirm",
                        Button::new(texts.get(Msg::ButtonPlaceIt)),
                    ) {
                        actions.push(Action::Enhancements(EnhancementsAction::ConfirmFlat));
                    }
                    if record.button(ui, true, "textures_flat_cancel", Button::new(texts.get(Msg::ButtonCancel))) {
                        actions.push(Action::Enhancements(EnhancementsAction::CancelFlat));
                    }
                });
            });
        }
        ui.horizontal_wrapped(|ui| {
            let can_import = idle && installed;
            if record.button(
                ui,
                can_import,
                "textures_import_folder",
                Button::new(texts.get(Msg::ButtonImportTextureFolder)),
            ) {
                actions.push(Action::Enhancements(EnhancementsAction::PickTextures { zip: false }));
            }
            if record.button(ui, can_import, "textures_import_zip", Button::new(texts.get(Msg::ButtonImportTextureZip)))
            {
                actions.push(Action::Enhancements(EnhancementsAction::PickTextures { zip: true }));
            }
            if let Some(folder) = textures["folder"].as_str() {
                if record.button(ui, true, "textures_open", Button::new(texts.get(Msg::ButtonOpenFolder))) {
                    actions.push(Action::OpenFolder(PathBuf::from(folder)));
                }
            }
        });
        if let Some(folder) = textures["folder"].as_str() {
            line(ui, record, "textures_folder", RichText::new(folder).monospace().small());
        }
    });
}

fn custom_romfs_section(
    app: &ForgeApp,
    ui: &mut egui::Ui,
    record: &mut Recorder,
    actions: &mut Vec<Action>,
    status: &Value,
) {
    let texts = app.texts.clone();
    let custom = &status["custom_romfs"];
    let installed = status["installed"] == true;
    let idle = app.idle();
    let warn = ui.visuals().warn_fg_color;
    section(ui, &texts.get(Msg::CustomRomfsHeading), |ui| {
        line(
            ui,
            record,
            "custom_romfs_warning",
            RichText::new(texts.get(Msg::CustomRomfsWarning)).strong().color(warn),
        );
        line(ui, record, "custom_romfs_description", texts.get(Msg::CustomRomfsDescription));
        ui.add_space(6.0);
        let files = custom["files"].as_u64().unwrap_or(0);
        let present = match (files, custom["name"].as_str()) {
            (0, _) => texts.get(Msg::CustomRomfsEmpty),
            (files, Some(name)) => texts.get(Msg::CustomRomfsNamed { name, files }),
            (files, None) => texts.get(Msg::CustomRomfsFiles { files }),
        };
        let enabled = custom["enabled"] == true;
        let applied_files = custom["applied_files"].as_u64().unwrap_or(0);
        let pending = installed
            && ((enabled && custom["applicable"] == true && custom["applied_current"] != true)
                || (!enabled && custom["applied"] == true));
        let applied = if custom["applied"] == true && !pending {
            texts.get(Msg::CustomRomfsApplied { files: applied_files })
        } else {
            applied_text(&texts, installed, enabled, false, pending)
        };
        states(
            ui,
            record,
            ["custom_romfs_state_present", "custom_romfs_state_enabled", "custom_romfs_state_applied"],
            [present, enabled_text(&texts, enabled), applied],
        );
        let issues = |key: &str| -> Vec<String> {
            custom[key]
                .as_array()
                .map(|items| {
                    items.iter().take(5).filter_map(|item| item["message"].as_str().map(str::to_string)).collect()
                })
                .unwrap_or_default()
        };
        for reason in issues("rejected") {
            line(
                ui,
                record,
                "custom_romfs_rejected",
                RichText::new(texts.get(Msg::CustomRomfsRejected { reason: &reason }))
                    .color(ui.visuals().error_fg_color),
            );
        }
        for reason in issues("flagged") {
            line(
                ui,
                record,
                "custom_romfs_flagged",
                RichText::new(texts.get(Msg::CustomRomfsFlagged { reason: &reason })).small(),
            );
        }
        if let Some(error) = custom["settings_error"].as_str() {
            line(
                ui,
                record,
                "custom_romfs_settings_error",
                RichText::new(texts.get(Msg::SettingNotSaved { error })).color(ui.visuals().error_fg_color),
            );
        }
        ui.add_space(6.0);
        let mut choice = enabled;
        let check = ui.add_enabled(idle, Checkbox::new(&mut choice, texts.get(Msg::CustomRomfsCheckbox)));
        record.push("custom_romfs_enabled", "checkbox", &check);
        if check.changed() {
            actions.push(Action::Enhancements(if choice {
                EnhancementsAction::AskCustomRomfs
            } else {
                EnhancementsAction::DisableCustomRomfs
            }));
        }
        if app.enhancements.confirm_custom_romfs {
            egui::Frame::group(ui.style()).inner_margin(8.0).stroke(Stroke::new(1.5, warn)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                line(ui, record, "custom_romfs_confirm", RichText::new(texts.get(Msg::CustomRomfsConfirm)).strong());
                ui.horizontal_wrapped(|ui| {
                    if record.button(ui, idle, "custom_romfs_turn_on", Button::new(texts.get(Msg::ButtonTurnOn))) {
                        actions.push(Action::Enhancements(EnhancementsAction::ConfirmCustomRomfs));
                    }
                    if record.button(ui, true, "custom_romfs_cancel", Button::new(texts.get(Msg::ButtonCancel))) {
                        actions.push(Action::Enhancements(EnhancementsAction::CancelCustomRomfs));
                    }
                });
            });
        }
        let folder = forge_mods::custom_romfs::folder(&app.installation.data_root);
        line(ui, record, "custom_romfs_folder", RichText::new(folder.display().to_string()).monospace().small());
        if record.button(ui, true, "custom_romfs_open", Button::new(texts.get(Msg::ButtonOpenFolder))) {
            actions.push(Action::OpenFolder(folder));
        }
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
            "Not stored yet: the HD textures (451 MB) are downloaded when you prepare the title or apply changes. \
             Without them the game uses the native textures."
        );
        assert_eq!(
            hd_status(&texts, Some(&contract), &Some(Ok(Status::Missing)), false),
            "Not installed: the game uses the native textures."
        );
        assert!(
            hd_status(&texts, Some(&contract), &Some(Ok(Status::Archive)), false)
                .starts_with("HD texture archive stored.")
        );
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
            "Not downloaded. It is downloaded when you apply changes or prepare the title."
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
             from the mod page and select it with \"Select .zip...\", then apply changes."
        );
    }

    #[test]
    fn the_applied_state_says_what_the_installed_game_uses() {
        let texts = Texts::default();
        assert_eq!(applied_text(&texts, false, true, false, false), texts.get(Msg::StateAppliedLater));
        assert_eq!(applied_text(&texts, true, true, false, true), texts.get(Msg::StateApplyPending));
        assert_eq!(applied_text(&texts, true, true, true, false), texts.get(Msg::StateApplied));
        assert_eq!(applied_text(&texts, true, false, false, false), texts.get(Msg::StateNotApplied));
    }
}
