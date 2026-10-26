// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Data & backups (UX plan WP-05B): where the installation keeps the
//! user's files (the resolved inventory, `forge_core::inventory`), and the
//! actions on them: Back up, Restore, Clear renderer cache, Rebuild
//! prepared assets, Remove prepared game and the uninstall help. Every
//! action shows its preview first and runs only after the user confirms
//! it in the page; the slow work runs on a task thread
//! (`forge_core::{backup, restore, data_actions}`).

use std::collections::HashSet;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, Receiver, Sender};

use eframe::egui::{self, Button, Checkbox, Label, RichText, Stroke};
use forge_base::{CancelToken, ForgeError, Operation};
use forge_core::explain::Explanation;
use forge_core::inventory::{Backup, Category, EntryKind, Inventory, InventoryRequest, Origin, Platform};
use forge_core::restore::RestoreOptions;
use serde_json::{json, Value};

use crate::app::{Action, ForgeApp, Recorder};
use crate::installation::Installation;
use crate::texts::{Msg, Texts};

/// What the page asks the window to do.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DataAction {
    Refresh,
    ToggleHelp(String),
    ToggleOptional(String),
    OpenPanel(Panel),
    ClosePanel,
    ChooseBackupFile,
    ChooseRestoreFile,
    ConfirmRestore,
    ConfirmClearCache,
    ConfirmRemove,
    ConfirmRebuild,
    Recover,
}

/// The preview or confirmation open on the page.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub enum Panel {
    #[default]
    None,
    Backup,
    /// The verified backup and its preview (`triaevum_restore_preview_v1`).
    Restore,
    ClearCache,
    Remove,
    Rebuild,
    Uninstall,
}

type Loaded = Result<(Box<Inventory>, Value), Box<Explanation>>;

enum DataEvent {
    Loaded(Loaded),
    Progress(u64, Option<u64>),
    Previewed(PathBuf, Result<Value, Box<Explanation>>),
    BackupFile(Option<PathBuf>),
    RestoreFile(Option<PathBuf>),
    Finished(Result<String, Box<Explanation>>),
}

#[derive(Default)]
pub struct DataState {
    pub(crate) inventory: Option<Box<Inventory>>,
    /// `forge_core::data_actions::data_actions` of the inventory.
    pub(crate) plan: Option<Value>,
    pub(crate) load_error: Option<Box<Explanation>>,
    pub(crate) panel: Panel,
    /// Optional entries chosen for the backup (none by default, decision 2).
    pub(crate) chosen: Vec<String>,
    pub(crate) restore: Option<(PathBuf, Value)>,
    /// Rows whose help is open.
    pub(crate) help: HashSet<String>,
    /// The last action's outcome.
    pub(crate) outcome: Option<Result<String, Box<Explanation>>>,
    /// An operation or a file dialog runs (its status text; empty for a dialog).
    pub(crate) working: Option<String>,
    loading: bool,
    sender: Option<Sender<DataEvent>>,
    receiver: Option<Receiver<DataEvent>>,
}

impl DataState {
    fn channel(&mut self) -> Sender<DataEvent> {
        if self.sender.is_none() {
            let (sender, receiver) = channel();
            (self.sender, self.receiver) = (Some(sender), Some(receiver));
        }
        self.sender.clone().expect("created above")
    }
}

/// `1.2 GB`, `350 MB`, `12 KB`, `0 B` (in English; the unit and the
/// decimal separator come from the language).
pub fn size_text(texts: &Texts, bytes: u64) -> String {
    if bytes < 1000 {
        return texts.get(Msg::Size { value: &bytes.to_string(), power: 0 });
    }
    let mut value = bytes as f64 / 1000.0;
    let mut power = 1;
    while value >= 1000.0 && power < 4 {
        value /= 1000.0;
        power += 1;
    }
    let value = if value < 10.0 { format!("{value:.1}") } else { format!("{value:.0}") };
    texts.get(Msg::Size { value: &value, power })
}

fn explained(error: ForgeError) -> Box<Explanation> {
    Box::new(forge_core::explain::explain(&error))
}

fn inventory_request(installation: &Installation) -> InventoryRequest {
    InventoryRequest {
        data_root: Some(installation.data_root.clone()),
        ..InventoryRequest::package(&installation.root)
    }
}

fn load(installation: &Installation, chosen: &[String]) -> Loaded {
    let inventory = forge_core::inventory::inventory_of(&inventory_request(installation))
        .map_err(|error| explained(error.during(Operation::Inspect)))?;
    let plan = forge_core::data_actions::data_actions(&inventory, chosen)
        .map_err(|error| explained(error.during(Operation::Inspect)))?;
    Ok((Box::new(inventory), plan))
}

/// Reads the inventory again (after an action, or when the page opens).
pub fn refresh(app: &mut ForgeApp) {
    if app.freeze_state || app.data.loading {
        return;
    }
    app.data.loading = true;
    let sender = app.data.channel();
    let (installation, chosen) = (app.installation.clone(), app.data.chosen.clone());
    std::thread::spawn(move || {
        let _ = sender.send(DataEvent::Loaded(load(&installation, &chosen)));
    });
}

/// Handles the task threads' events; called every frame.
pub fn poll(app: &mut ForgeApp, context: &egui::Context) {
    let events: Vec<DataEvent> = match &app.data.receiver {
        Some(receiver) => receiver.try_iter().collect(),
        None => return,
    };
    for event in events {
        match event {
            DataEvent::Loaded(loaded) => {
                app.data.loading = false;
                match loaded {
                    Ok((inventory, plan)) => {
                        (app.data.inventory, app.data.plan, app.data.load_error) = (Some(inventory), Some(plan), None)
                    }
                    Err(explanation) => app.data.load_error = Some(explanation),
                }
            }
            DataEvent::Progress(done, total) => app.progress = Some((done, total)),
            DataEvent::Previewed(path, preview) => {
                finish(app);
                match preview {
                    Ok(preview) => {
                        app.data.restore = Some((path, preview));
                        app.data.panel = Panel::Restore;
                    }
                    Err(explanation) => app.data.outcome = Some(Err(explanation)),
                }
            }
            DataEvent::BackupFile(path) => {
                app.data.working = None;
                if let Some(path) = path {
                    start_backup(app, path);
                }
            }
            DataEvent::RestoreFile(path) => {
                app.data.working = None;
                if let Some(path) = path {
                    preview_restore(app, path);
                }
            }
            DataEvent::Finished(outcome) => {
                finish(app);
                app.data.panel = Panel::None;
                app.data.restore = None;
                if let Ok(message) = &outcome {
                    app.status_note = Some(message.clone());
                }
                app.data.outcome = Some(outcome);
                app.refresh_installation();
                refresh(app);
            }
        }
    }
    if app.data.loading || app.data.working.is_some() {
        context.request_repaint_after(std::time::Duration::from_millis(100));
    }
}

fn finish(app: &mut ForgeApp) {
    app.busy = false;
    app.progress = None;
    app.data.working = None;
}

fn begin(app: &mut ForgeApp, status: Msg) -> Sender<DataEvent> {
    let text = app.t(status);
    app.busy = true;
    app.progress = None;
    app.data.outcome = None;
    app.data.working = Some(text.clone());
    app.status_note = Some(text);
    app.data.channel()
}

type Work = Box<dyn FnOnce(&Sender<DataEvent>) -> Result<String, ForgeError> + Send>;

/// Runs `work` on a task thread with the window busy; its message or
/// failure becomes the page's outcome.
fn run(app: &mut ForgeApp, status: Msg, work: Work) {
    if !app.idle() {
        return;
    }
    let sender = begin(app, status);
    std::thread::spawn(move || {
        let outcome = work(&sender).map_err(explained);
        let _ = sender.send(DataEvent::Finished(outcome));
    });
}

fn current_inventory(installation: &Installation, sizes: bool) -> Result<Inventory, ForgeError> {
    forge_core::inventory::inventory_of(&InventoryRequest { sizes, ..inventory_request(installation) })
}

fn progress_to(sender: &Sender<DataEvent>) -> impl FnMut(&str, u64, Option<u64>) + '_ {
    move |_, done, total| {
        let _ = sender.send(DataEvent::Progress(done, total));
    }
}

fn start_backup(app: &mut ForgeApp, destination: PathBuf) {
    let (installation, optional, texts) = (app.installation.clone(), app.data.chosen.clone(), app.texts.clone());
    run(
        app,
        Msg::DataBackingUp,
        Box::new(move |sender| {
            let inventory = current_inventory(&installation, false)?;
            let request = forge_core::backup::BackupRequest { destination, optional, touch_preferences: None };
            let mut progress = progress_to(sender);
            let result = forge_core::backup::create_backup(&inventory, &request, &mut progress, &CancelToken::new())?;
            let size = size_text(&texts, result["bytes"].as_u64().unwrap_or(0));
            Ok(texts.get(Msg::BackupDone { path: result["path"].as_str().unwrap_or_default(), size: &size }))
        }),
    );
}

fn preview_restore(app: &mut ForgeApp, path: PathBuf) {
    if !app.idle() {
        return;
    }
    let sender = begin(app, Msg::DataCheckingBackup);
    let installation = app.installation.clone();
    std::thread::spawn(move || {
        let preview = current_inventory(&installation, false)
            .and_then(|inventory| {
                forge_core::restore::restore_preview(&inventory, &path, &RestoreOptions::default(), &CancelToken::new())
            })
            .map(|plan| plan.to_value())
            .map_err(explained);
        let _ = sender.send(DataEvent::Previewed(path, preview));
    });
}

/// A native file dialog on its own thread (the window keeps repainting).
fn ask_file(app: &mut ForgeApp, save: bool, frame: Option<&eframe::Frame>) {
    if !app.idle() || app.data.working.is_some() {
        return;
    }
    let texts = &app.texts;
    let mut dialog = rfd::FileDialog::new().add_filter(texts.get(Msg::FilterBackup), &["zip"]);
    if save {
        let stamp = forge_core::backup::stamp_minutes(std::time::SystemTime::now());
        dialog =
            dialog.set_title(texts.get(Msg::SaveBackupTitle)).set_file_name(format!("TriAevum-backup-{stamp}.zip"));
    } else {
        dialog = dialog.set_title(texts.get(Msg::OpenBackupTitle));
    }
    if let Some(frame) = frame {
        dialog = dialog.set_parent(frame);
    }
    app.data.working = Some(String::new());
    let sender = app.data.channel();
    std::thread::spawn(move || {
        let event =
            if save { DataEvent::BackupFile(dialog.save_file()) } else { DataEvent::RestoreFile(dialog.pick_file()) };
        let _ = sender.send(event);
    });
}

pub fn apply(app: &mut ForgeApp, action: DataAction, frame: Option<&eframe::Frame>) {
    let (installation, texts) = (app.installation.clone(), app.texts.clone());
    match action {
        DataAction::Refresh => refresh(app),
        DataAction::ToggleHelp(name) => {
            if !app.data.help.remove(&name) {
                app.data.help.insert(name);
            }
        }
        DataAction::ToggleOptional(name) => {
            match app.data.chosen.iter().position(|chosen| *chosen == name) {
                Some(index) => drop(app.data.chosen.remove(index)),
                None => app.data.chosen.push(name),
            }
            if let (Some(inventory), Some(plan)) = (&app.data.inventory, &mut app.data.plan) {
                if let Ok(backup) = forge_core::backup::backup_plan(inventory, &app.data.chosen, None) {
                    plan["backup"] = backup.to_value();
                }
            }
        }
        DataAction::OpenPanel(panel) => {
            app.data.outcome = None;
            app.data.panel = if app.data.panel == panel { Panel::None } else { panel };
        }
        DataAction::ClosePanel => {
            app.data.panel = Panel::None;
            app.data.restore = None;
        }
        DataAction::ChooseBackupFile => ask_file(app, true, frame),
        DataAction::ChooseRestoreFile => ask_file(app, false, frame),
        DataAction::ConfirmRestore => {
            let Some((path, _)) = app.data.restore.clone() else { return };
            let work: Work = Box::new(move |sender| {
                let inventory = current_inventory(&installation, false)?;
                let mut progress = progress_to(sender);
                let options = RestoreOptions::default();
                let result =
                    forge_core::restore::restore(&inventory, &path, &options, &mut progress, &CancelToken::new())?;
                Ok(texts.get(Msg::RestoreDone { path: result["snapshot"].as_str().unwrap_or_default() }))
            });
            run(app, Msg::DataRestoring, work);
        }
        DataAction::ConfirmClearCache => {
            let work: Work = Box::new(move |_| {
                forge_core::data_actions::clear_renderer_cache(&current_inventory(&installation, true)?)?;
                Ok(texts.get(Msg::ClearCacheDone))
            });
            run(app, Msg::DataClearingCache, work);
        }
        DataAction::ConfirmRemove => {
            let work: Work = Box::new(move |_| {
                forge_core::data_actions::remove_prepared_game(&current_inventory(&installation, true)?)?;
                Ok(texts.get(Msg::RemoveGameDone))
            });
            run(app, Msg::DataRemovingGame, work);
        }
        DataAction::ConfirmRebuild => {
            let work: Work = Box::new(move |_| {
                forge_core::data_actions::rebuild_prepared_assets(
                    &installation.root,
                    Some(&installation.data_root),
                    forge_core::install::InstallTarget::Package,
                    &forge_native::DesktopServices,
                    &mut |_, _| {},
                    &CancelToken::new(),
                )?;
                Ok(texts.get(Msg::RebuildDone))
            });
            run(app, Msg::DataRebuilding, work);
        }
        DataAction::Recover => {
            let work: Work = Box::new(move |_| {
                recover(&installation)?;
                Ok(texts.get(Msg::RecoveryDone))
            });
            run(app, Msg::DataRecovering, work);
        }
    }
}

/// Recovers an interrupted restore of `installation` (run before its state
/// is inspected, so Play is offered only for a coherent installation).
pub fn recover(installation: &Installation) -> forge_base::Result<Option<Value>> {
    forge_core::restore::recover_pending(
        &installation.layout.activation,
        forge_core::layout::HostOs::current(),
        &installation.data_root,
        &CancelToken::new(),
    )
}

// ---- Drawing ----

fn heading(app: &ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, name: &'static str, msg: Msg) {
    ui.add_space(8.0);
    record.add(ui, name, "label", Label::new(RichText::new(app.t(msg)).strong().size(16.0)).wrap());
}

fn line(ui: &mut egui::Ui, record: &mut Recorder, name: &'static str, text: impl Into<RichText>) {
    record.add(ui, name, "label", Label::new(text.into()).wrap());
}

fn path_line(ui: &mut egui::Ui, record: &mut Recorder, path: &str) {
    record.add(ui, "data_path", "label", Label::new(RichText::new(path).monospace()).wrap());
}

fn explanation_card(app: &ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, explanation: &Explanation) {
    let color = ui.visuals().error_fg_color;
    let frame = egui::Frame::group(ui.style()).inner_margin(10.0).stroke(Stroke::new(1.5, color));
    let response = frame.show(ui, |ui| {
        ui.set_width(ui.available_width());
        line(ui, record, "data_error_title", RichText::new(app.texts.title(explanation)).strong().color(color));
        line(ui, record, "data_error_cause", app.texts.cause(explanation));
        line(ui, record, "data_error_next", app.texts.next_action(explanation));
        if !explanation.details.is_empty() {
            line(ui, record, "data_error_details", RichText::new(&explanation.details).monospace().small());
        }
    });
    record.push("data_error", "group", &response.response);
}

fn origin_text(origin: Origin) -> Msg<'static> {
    match origin {
        Origin::Default => Msg::DataOriginDefault,
        Origin::Explicit => Msg::DataOriginExplicit,
        Origin::Legacy => Msg::DataOriginLegacy,
    }
}

fn backup_text(backup: Backup) -> Msg<'static> {
    match backup {
        Backup::Include => Msg::DataBackupInclude,
        Backup::Optional => Msg::DataBackupOptional,
        Backup::Exclude => Msg::DataBackupExclude,
    }
}

/// The rows: every location except installation records, a nested entry
/// only when it belongs to another category than its folder.
fn rows(inventory: &Inventory) -> Vec<&forge_core::inventory::Entry> {
    inventory
        .entries
        .iter()
        .filter(|entry| entry.category != Category::State && (entry.present || entry.active))
        .filter(|entry| {
            entry
                .parent
                .and_then(|parent| inventory.entry(parent))
                .is_none_or(|parent| parent.category != entry.category)
        })
        .collect()
}

pub fn show(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    if app.data.inventory.is_none() && app.data.load_error.is_none() && !app.data.loading && !app.freeze_state {
        actions.push(Action::Data(DataAction::Refresh));
    }
    record.add(ui, "data_body", "label", Label::new(app.t(Msg::DataBody)).wrap());
    if let Some(working) = app.data.working.clone().filter(|text| !text.is_empty()) {
        ui.horizontal_wrapped(|ui| {
            ui.add(egui::Spinner::new());
            line(ui, record, "data_working", RichText::new(working).strong());
        });
    }
    match app.data.outcome.clone() {
        Some(Ok(message)) => {
            let color = ui.visuals().selection.stroke.color;
            line(ui, record, "data_outcome", RichText::new(message).color(color));
        }
        Some(Err(explanation)) => explanation_card(app, ui, record, &explanation),
        None => {}
    }
    if let Some(explanation) = app.data.load_error.clone() {
        explanation_card(app, ui, record, &explanation);
    }
    let Some(inventory) = app.data.inventory.clone() else {
        if app.data.load_error.is_none() {
            ui.horizontal_wrapped(|ui| {
                ui.add(egui::Spinner::new());
                line(ui, record, "data_loading", app.t(Msg::DataLoading));
            });
        }
        folders(app, ui, record, actions);
        return;
    };
    let plan = app.data.plan.clone().unwrap_or(Value::Null);
    let idle = app.idle() && app.data.working.is_none();
    let warn = ui.visuals().warn_fg_color;
    if !inventory.activity.idle() {
        let msg = if inventory.activity.game_running { Msg::DataGameRunning } else { Msg::DataForgeRunning };
        line(ui, record, "data_busy", RichText::new(app.t(msg)).strong().color(warn));
    }
    if plan["pending_restore"].is_string() {
        line(ui, record, "data_recovery", RichText::new(app.t(Msg::DataRecoveryPending)).strong().color(warn));
        if record.button(ui, idle, "data_recover", Button::new(app.t(Msg::ButtonRecover))) {
            actions.push(Action::Data(DataAction::Recover));
        }
    }

    heading(app, ui, record, "data_actions_heading", Msg::DataActionsHeading);
    ui.horizontal_wrapped(|ui| {
        let buttons = [
            ("data_backup", Msg::ButtonBackUp, Panel::Backup),
            ("data_restore", Msg::ButtonRestore, Panel::Restore),
            ("data_clear_cache", Msg::ButtonClearCache, Panel::ClearCache),
            ("data_rebuild", Msg::ButtonRebuild, Panel::Rebuild),
            ("data_remove", Msg::ButtonRemoveGame, Panel::Remove),
            ("data_uninstall", Msg::ButtonUninstallHelp, Panel::Uninstall),
        ];
        for (name, msg, panel) in buttons {
            let open = app.data.panel == panel;
            let enabled = idle || panel == Panel::Uninstall;
            if record.button(ui, enabled, name, Button::new(app.t(msg)).selected(open)) {
                actions.push(Action::Data(match panel {
                    Panel::Restore if !open => DataAction::ChooseRestoreFile,
                    _ => DataAction::OpenPanel(panel),
                }));
            }
        }
    });
    panel(app, ui, record, actions, &inventory, &plan, idle);

    heading(app, ui, record, "data_files_heading", Msg::DataYourFiles);
    for entry in rows(&inventory) {
        let frame = egui::Frame::group(ui.style()).inner_margin(8.0);
        let response = frame.show(ui, |ui| {
            ui.set_width(ui.available_width());
            let label = app.t(Msg::DataEntryLabel { name: entry.logical_name });
            let size = if entry.present { size_text(&app.texts, entry.size) } else { app.t(Msg::DataNotCreated) };
            line(ui, record, "data_row_name", RichText::new(format!("{label} \u{2014} {size}")).strong());
            path_line(ui, record, &entry.resolved_path.display().to_string());
            let badges = format!("{} \u{b7} {}", app.t(origin_text(entry.origin)), app.t(backup_text(entry.backup)));
            line(ui, record, "data_row_badges", RichText::new(badges).small());
            ui.horizontal_wrapped(|ui| {
                let folder = match entry.kind {
                    EntryKind::Directory => entry.resolved_path.clone(),
                    EntryKind::File => entry.resolved_path.parent().map(Path::to_path_buf).unwrap_or_default(),
                };
                let can_open = app.freeze_state || folder.is_dir();
                if record.button(ui, can_open, "data_open", Button::new(app.t(Msg::ButtonOpenFolder)).small()) {
                    actions.push(Action::OpenFolder(folder));
                }
                let open = app.data.help.contains(entry.logical_name);
                let toggle = if open { Msg::ButtonHideHelp } else { Msg::ButtonWhatIsThis };
                if record.button(ui, true, "data_help", Button::new(app.t(toggle)).small()) {
                    actions.push(Action::Data(DataAction::ToggleHelp(entry.logical_name.to_string())));
                }
            });
            if app.data.help.contains(entry.logical_name) {
                line(ui, record, "data_row_help", app.t(Msg::DataCategoryHelp { category: entry.category.code() }));
            }
        });
        record.push("data_row", "group", &response.response);
    }
    folders(app, ui, record, actions);
}

/// The data folder and the Forge log (always available).
fn folders(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    ui.add_space(8.0);
    let logs = forge_core::log::logs_directory(&app.installation.data_root);
    let folders =
        [("data_folder", Msg::DataFolder, app.installation.data_root.clone()), ("forge_log", Msg::ForgeLog, logs)];
    for (name, heading, path) in folders {
        record.add(ui, name, "label", Label::new(RichText::new(app.t(heading)).strong()));
        path_line(ui, record, &path.display().to_string());
        if record.button(ui, true, "open_folder", Button::new(app.t(Msg::ButtonOpenFolder))) {
            actions.push(Action::OpenFolder(path));
        }
        ui.add_space(6.0);
    }
}

fn panel_frame(ui: &mut egui::Ui, record: &mut Recorder, body: impl FnOnce(&mut egui::Ui, &mut Recorder)) {
    let accent = ui.visuals().selection.stroke.color;
    let frame = egui::Frame::group(ui.style()).inner_margin(12.0).stroke(Stroke::new(1.5, accent));
    let response = frame.show(ui, |ui| {
        ui.set_width(ui.available_width());
        body(ui, record);
    });
    record.push("data_panel", "group", &response.response);
}

/// The panel's confirmation (when one applies) and Cancel.
fn confirm_row(
    texts: &Texts,
    ui: &mut egui::Ui,
    record: &mut Recorder,
    actions: &mut Vec<Action>,
    confirm: Option<(Msg, DataAction, bool)>,
) {
    ui.add_space(6.0);
    ui.horizontal_wrapped(|ui| {
        if let Some((msg, action, enabled)) = confirm {
            let fill = ui.visuals().selection.bg_fill;
            let button = Button::new(RichText::new(texts.get(msg)).strong()).fill(fill);
            if record.button(ui, enabled, "data_confirm", button) {
                actions.push(Action::Data(action));
            }
        }
        if record.button(ui, true, "data_close", Button::new(texts.get(Msg::ButtonCancel))) {
            actions.push(Action::Data(DataAction::ClosePanel));
        }
    });
}

fn label_of(texts: &Texts, name: &str) -> String {
    texts.get(Msg::DataEntryLabel { name })
}

fn sized_item(texts: &Texts, ui: &mut egui::Ui, record: &mut Recorder, item: &Value) {
    let name = item["name"].as_str().unwrap_or_default();
    let size = size_text(texts, item["bytes"].as_u64().unwrap_or(0));
    line(ui, record, "data_panel_item", format!("\u{2022} {} \u{2014} {size}", label_of(texts, name)));
}

fn panel(
    app: &mut ForgeApp,
    ui: &mut egui::Ui,
    record: &mut Recorder,
    actions: &mut Vec<Action>,
    inventory: &Inventory,
    plan: &Value,
    idle: bool,
) {
    let texts = app.texts.clone();
    let warn = ui.visuals().warn_fg_color;
    match app.data.panel.clone() {
        Panel::None => {}
        Panel::Backup => panel_frame(ui, record, |ui, record| {
            let backup = &plan["backup"];
            let files = backup["files"].as_u64().unwrap_or(0);
            let size = size_text(&texts, backup["bytes"].as_u64().unwrap_or(0));
            line(ui, record, "data_panel_body", texts.get(Msg::BackupPanelBody { files, size: &size }));
            for unit in backup["units"].as_array().into_iter().flatten() {
                sized_item(&texts, ui, record, unit);
            }
            let optional: Vec<&Value> = backup["optional"].as_array().into_iter().flatten().collect();
            if !optional.is_empty() {
                line(ui, record, "data_panel_optional", RichText::new(texts.get(Msg::BackupOptionalHeading)).strong());
                for item in optional {
                    let name = item["name"].as_str().unwrap_or_default().to_string();
                    let mut chosen = app.data.chosen.contains(&name);
                    let size = size_text(&texts, item["bytes"].as_u64().unwrap_or(0));
                    let text = format!("{} \u{2014} {size}", label_of(&texts, &name));
                    let response = ui.add_enabled(idle, Checkbox::new(&mut chosen, text));
                    record.push("data_optional", "checkbox", &response);
                    if response.changed() {
                        actions.push(Action::Data(DataAction::ToggleOptional(name)));
                    }
                }
            }
            line(ui, record, "data_panel_note", RichText::new(texts.get(Msg::BackupExcludedNote)).small());
            let confirm = (Msg::ButtonChooseBackupFile, DataAction::ChooseBackupFile, idle);
            confirm_row(&texts, ui, record, actions, Some(confirm));
        }),
        Panel::Restore => {
            let Some((path, preview)) = app.data.restore.clone() else {
                app.data.panel = Panel::None;
                return;
            };
            panel_frame(ui, record, |ui, record| restore_panel(&texts, ui, record, actions, &path, &preview, idle));
        }
        Panel::ClearCache => panel_frame(ui, record, |ui, record| match plan["renderer_cache"]["targets"].get(0) {
            Some(target) => {
                let size = size_text(&texts, target["bytes"].as_u64().unwrap_or(0));
                line(ui, record, "data_panel_body", texts.get(Msg::ClearCacheBody { size: &size }));
                path_line(ui, record, target["path"].as_str().unwrap_or_default());
                let confirm = (Msg::ButtonConfirmClear, DataAction::ConfirmClearCache, idle);
                confirm_row(&texts, ui, record, actions, Some(confirm));
            }
            None => {
                line(ui, record, "data_panel_body", texts.get(Msg::ClearCacheEmpty));
                confirm_row(&texts, ui, record, actions, None);
            }
        }),
        Panel::Remove => panel_frame(ui, record, |ui, record| {
            let removal = &plan["prepared_game"];
            let size = size_text(&texts, removal["bytes"].as_u64().unwrap_or(0));
            let body = RichText::new(texts.get(Msg::RemoveGameBody { size: &size })).strong().color(warn);
            line(ui, record, "data_panel_body", body);
            for target in removal["targets"].as_array().into_iter().flatten() {
                sized_item(&texts, ui, record, target);
            }
            line(ui, record, "data_panel_note", texts.get(Msg::RemoveGameKeep));
            let possible = removal["targets"].as_array().is_some_and(|targets| !targets.is_empty());
            let confirm = (Msg::ButtonConfirmRemove, DataAction::ConfirmRemove, idle && possible);
            confirm_row(&texts, ui, record, actions, Some(confirm));
        }),
        Panel::Rebuild => panel_frame(ui, record, |ui, record| match plan["rebuild"]["source"].as_str() {
            Some(source) => {
                let size = size_text(&texts, plan["rebuild"]["bytes"].as_u64().unwrap_or(0));
                line(ui, record, "data_panel_body", texts.get(Msg::RebuildBody { size: &size }));
                path_line(ui, record, source);
                let confirm = (Msg::ButtonConfirmRebuild, DataAction::ConfirmRebuild, idle);
                confirm_row(&texts, ui, record, actions, Some(confirm));
            }
            None => {
                line(ui, record, "data_panel_body", texts.get(Msg::RebuildNoSource));
                confirm_row(&texts, ui, record, actions, None);
            }
        }),
        Panel::Uninstall => panel_frame(ui, record, |ui, record| {
            uninstall_help(&texts, ui, record, &plan["uninstall"], inventory.roots.platform);
            confirm_row(&texts, ui, record, actions, None);
        }),
    }
}

fn restore_panel(
    texts: &Texts,
    ui: &mut egui::Ui,
    record: &mut Recorder,
    actions: &mut Vec<Action>,
    path: &Path,
    preview: &Value,
    idle: bool,
) {
    let backup = &preview["backup"];
    let created = backup["created"].as_str().unwrap_or_default();
    let platform = texts.get(Msg::PlatformName { code: backup["platform"].as_str().unwrap_or_default() });
    let title = texts.get(Msg::RestoreHeading { created, platform: &platform });
    line(ui, record, "data_panel_title", RichText::new(title).strong());
    path_line(ui, record, &path.display().to_string());
    let warn = ui.visuals().warn_fg_color;
    for warning in preview["warnings"].as_array().into_iter().flatten() {
        let path = warning["path"].as_str().unwrap_or_default();
        let text = match warning["kind"].as_str() {
            Some("other_platform") => texts.get(Msg::RestoreOtherPlatform { platform: &platform }),
            Some("different_revision") => texts.get(Msg::RestoreDifferentRevision),
            Some("unavailable_reference") => texts.get(Msg::RestoreUnavailable { path }),
            Some("kept_existing") => texts.get(Msg::RestoreKeptExisting { path }),
            _ => continue,
        };
        line(ui, record, "data_panel_warning", RichText::new(text).color(warn));
    }
    for group in preview["groups"].as_array().into_iter().flatten() {
        let count = |key: &str| group[key].as_u64().unwrap_or(0);
        let label = label_of(texts, group["name"].as_str().unwrap_or_default());
        let text = texts.get(Msg::RestoreGroupLine {
            label: &label,
            replaced: count("replaced"),
            added: count("added"),
            removed: count("removed"),
        });
        line(ui, record, "data_panel_item", format!("\u{2022} {text}"));
        path_line(ui, record, group["destination"].as_str().unwrap_or_default());
    }
    if preview["touch_preferences"] == true {
        line(ui, record, "data_panel_item", format!("\u{2022} {}", label_of(texts, "touch_preferences")));
    }
    let kept: Vec<&str> = preview["kept_settings"].as_array().into_iter().flatten().filter_map(Value::as_str).collect();
    if !kept.is_empty() {
        line(ui, record, "data_panel_note", texts.get(Msg::RestoreKeptSettings { files: &kept.join(", ") }));
    }
    let skipped: Vec<String> = preview["skipped"]
        .as_array()
        .into_iter()
        .flatten()
        .filter(|item| item["reason"] != "not_selected")
        .filter_map(|item| item["unit"].as_str().map(|unit| label_of(texts, unit)))
        .collect();
    if !skipped.is_empty() {
        line(ui, record, "data_panel_note", texts.get(Msg::RestoreSkipped { labels: &skipped.join(", ") }));
    }
    let folder = preview["snapshot_folder"].as_str().unwrap_or_default();
    line(ui, record, "data_panel_note", RichText::new(texts.get(Msg::RestoreSnapshotNote { path: folder })).small());
    let possible =
        preview["groups"].as_array().is_some_and(|groups| !groups.is_empty()) || preview["touch_preferences"] == true;
    let confirm = (Msg::ButtonConfirmRestore, DataAction::ConfirmRestore, idle && possible);
    confirm_row(texts, ui, record, actions, Some(confirm));
}

fn uninstall_help(texts: &Texts, ui: &mut egui::Ui, record: &mut Recorder, help: &Value, platform: Platform) {
    let text = |key: &str| help[key].as_str().unwrap_or_default().to_string();
    let appimage = std::env::var("APPIMAGE").unwrap_or_default();
    let steps = match platform {
        Platform::WindowsPortable => texts.get(Msg::UninstallWindows { path: &text("package") }),
        Platform::LinuxPortable => texts.get(Msg::UninstallPortable { path: &text("package") }),
        Platform::AppImage => texts.get(Msg::UninstallAppImage {
            appimage: if appimage.is_empty() { "TriAevum.AppImage" } else { &appimage },
            path: &text("activation"),
        }),
        Platform::Flatpak => texts.get(Msg::UninstallFlatpak { id: &text("flatpak_id"), path: &text("activation") }),
        Platform::Android => texts.get(Msg::UninstallAndroid { path: &text("activation") }),
    };
    line(ui, record, "data_panel_body", steps);
    let list = |key: &str| -> Vec<String> {
        help[key]
            .as_array()
            .into_iter()
            .flatten()
            .filter_map(|item| item["path"].as_str().map(str::to_string))
            .collect()
    };
    let external = list("external");
    if !external.is_empty() {
        line(ui, record, "data_panel_note", texts.get(Msg::UninstallExternal));
        for path in external {
            path_line(ui, record, &path);
        }
    }
    if !list("keep").is_empty() {
        line(ui, record, "data_panel_note", texts.get(Msg::UninstallKeep));
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

// ---- The layout probe's synthetic states ----

/// The probe's screens of this page: `data` (the table), `data_backup`,
/// `data_restore` (a preview of another platform's backup) and
/// `data_remove`, from a synthetic inventory with long paths.
pub fn probe(app: &mut ForgeApp, screen: &str) {
    // User names with spaces: the release audit rejects a binary naming a
    // plausible build-machine home folder (`/home/<name>/`).
    let root = PathBuf::from(if cfg!(windows) { r"C:\Games\TriAevum" } else { "/home/Player Name/TriAevum" });
    let inventory = probe_inventory(&root);
    let mut plan = forge_core::data_actions::data_actions(&inventory, &[]).unwrap_or_else(|_| json!({}));
    // Sizes of a real installation (the synthetic folders do not exist).
    plan["backup"] = json!({"files": 214, "bytes": 48_600_000u64, "units": [
        {"name": "save_data", "bytes": 1_200_000u64}, {"name": "save_states", "bytes": 46_000_000u64},
        {"name": "config", "bytes": 1_380_000u64}, {"name": "imgui_layout", "bytes": 2_100u64}],
        "optional": [{"name": "mods", "bytes": 310_000_000u64}, {"name": "downloads", "bytes": 451_000_000u64},
                     {"name": "texture_load", "bytes": 1_900_000_000u64}]});
    plan["prepared_game"] = json!({"bytes": 3_400_000_000u64, "targets": [
        {"name": "sources", "bytes": 1_100_000_000u64}, {"name": "titles", "bytes": 1_900_000_000u64},
        {"name": "cache", "bytes": 400_000_000u64}, {"name": "active_title", "bytes": 312u64}]});
    app.data = DataState { inventory: Some(Box::new(inventory)), plan: Some(plan), ..DataState::default() };
    app.data.help.insert("save_states".into());
    match screen {
        "data_backup" => app.data.panel = Panel::Backup,
        "data_remove" => app.data.panel = Panel::Remove,
        "data_restore" => {
            let text = |path: PathBuf| path.display().to_string();
            let preview = json!({
                "backup": {"created": "2026-10-20T18:42:07Z", "platform": "android", "files": 214},
                "warnings": [
                    {"kind": "other_platform", "from": "android"},
                    {"kind": "different_revision"},
                    {"kind": "unavailable_reference", "path":
                        "/storage/emulated/0/Android/data/org.triaevum.android/files/Azahar/load/textures/0004000000033600"}],
                "groups": [
                    {"name": "save_data", "destination": text(root.join("data").join("savedata")),
                     "replaced": 3, "added": 1, "removed": 0},
                    {"name": "save_states", "destination": text(root.join("savestates")),
                     "replaced": 0, "added": 12, "removed": 2},
                    {"name": "config", "destination": text(root.join("data").join("config")),
                     "replaced": 2, "added": 1, "removed": 0}],
                "kept_settings": ["TriAevum.android.json", "oot3d_controls.json", "romfs_mods.json"],
                "skipped": [{"unit": "imgui_layout", "reason": "target_specific"},
                            {"unit": "touch_preferences", "reason": "target_specific"}],
                "touch_preferences": false,
                "snapshot_folder": text(root.join("data").join("backups")),
            });
            app.data.restore = Some((PathBuf::from("TriAevum-backup-20261020-1842.zip"), preview));
            app.data.panel = Panel::Restore;
        }
        _ => {}
    }
}

fn probe_inventory(root: &Path) -> Inventory {
    use forge_core::inventory::{Entry, Roots};
    use Backup::{Exclude, Include, Optional};
    use Category as C;
    use EntryKind::{Directory, File};
    let data = root.join("data");
    let entry = |category, name: &'static str, path: PathBuf, kind, origin, backup, size: u64| Entry {
        category,
        logical_name: name,
        parent: None,
        resolved_path: path,
        kind,
        origin,
        active: true,
        present: true,
        managed: true,
        size,
        files: 1,
        backup,
    };
    let pack = PathBuf::from(if cfg!(windows) {
        r"D:\Users\Player Name With A Long Name\AppData\Roaming\Azahar\load\textures\0004000000033600"
    } else {
        "/home/Player Name With A Long Name/.local/share/azahar/load/textures/0004000000033600"
    });
    let mut entries = vec![
        entry(C::Saves, "save_data", data.join("savedata"), Directory, Origin::Default, Include, 1_200_000),
        entry(
            C::Logs,
            "crash_dumps",
            data.join("savedata").join("crashes"),
            Directory,
            Origin::Default,
            Exclude,
            3_100_000,
        ),
        entry(C::SaveStates, "save_states", root.join("savestates"), Directory, Origin::Default, Include, 46_000_000),
        entry(C::Settings, "config", data.join("config"), Directory, Origin::Default, Include, 1_380_000),
        entry(C::Settings, "imgui_layout", data.join("imgui.ini"), File, Origin::Default, Include, 2_100),
        entry(C::Textures, "texture_load", pack, Directory, Origin::Explicit, Optional, 1_900_000_000),
        entry(C::Enhancements, "mods", data.join("mods"), Directory, Origin::Default, Optional, 310_000_000),
        entry(C::Enhancements, "downloads", data.join("downloads"), Directory, Origin::Default, Optional, 451_000_000),
        entry(C::Generated, "titles", data.join("titles"), Directory, Origin::Default, Exclude, 1_900_000_000),
        entry(
            C::Generated,
            "renderer_cache",
            data.join("cache").join("renderer"),
            Directory,
            Origin::Default,
            Exclude,
            380_000_000,
        ),
        entry(C::Logs, "game_logs", root.join("logs"), Directory, Origin::Legacy, Exclude, 820_000),
    ];
    entries[1].parent = Some("save_data");
    entries[5].managed = false;
    Inventory {
        roots: Roots {
            platform: Platform::WindowsPortable,
            package: root.to_path_buf(),
            activation: root.to_path_buf(),
            data: data.clone(),
            state: data,
        },
        activity: Default::default(),
        entries,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sizes_read_like_a_file_manager() {
        let english = Texts::default();
        assert_eq!(size_text(&english, 0), "0 B");
        assert_eq!(size_text(&english, 999), "999 B");
        assert_eq!(size_text(&english, 1_200), "1.2 KB");
        assert_eq!(size_text(&english, 48_600_000), "49 MB");
        assert_eq!(size_text(&english, 3_400_000_000), "3.4 GB");
        // The language's unit and decimal separator.
        assert_eq!(size_text(&Texts::new("fr"), 3_400_000_000), "3,4 Go");
        assert_eq!(size_text(&Texts::new("it"), 1_200), "1,2 KB");
    }

    #[test]
    fn rows_show_categories_and_nested_entries_of_another_category() {
        let inventory = probe_inventory(Path::new("root"));
        let names: Vec<&str> = rows(&inventory).iter().map(|entry| entry.logical_name).collect();
        assert!(names.contains(&"crash_dumps"), "a log folder inside the saves is its own row");
        assert!(names.contains(&"save_data") && names.contains(&"config"));
    }
}
