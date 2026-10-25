// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Forge window (`forge_gui.ForgeWindow`). Options scroll; the status,
//! progress and primary actions sit in a footer that always stays visible.
//! Anything slow (stored-file checks, imports, downloads, the install job)
//! runs on a task thread and reports through [`Event`]s.

use std::collections::VecDeque;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, Receiver};
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use eframe::egui::{
    self, Align, Button, Checkbox, Label, Layout, ProgressBar, Rect, RichText, Spinner, Stroke, TextEdit, Vec2,
};
use forge_base::CancelToken;
use forge_mods::Status;
use serde_json::Value;

use crate::active_title::{load_active_title, ActiveTitle};
use crate::install::{run_install, Event, InstallPlan, Notifier};
use crate::installation::Installation;
use crate::mods::Optional;
use crate::recognition::{recognize_request, run_recognition, Card, RecognizedRom};
use crate::smoke::Probe;
use crate::texts::{self, Checked};
use crate::window::{Geometry, INITIAL_SIZE, MIN_SIZE};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PickTarget {
    Rom,
    Extracted,
    HdArchive,
    HdFolder,
    ModArchive,
    ModFolder,
}

enum Action {
    Pick(PickTarget),
    Install,
    Launch,
    DownloadHd,
    DownloadMod,
    ToggleMod,
    SelectLanguage(usize),
    Recognize,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DialogKind {
    Info,
    Error,
    ConfirmClose,
}

struct Dialog {
    kind: DialogKind,
    title: &'static str,
    message: String,
    focus_pending: bool,
}

/// A widget as laid out in the last frame, for the layout probe.
#[derive(Clone, Debug)]
pub struct WidgetRecord {
    pub name: &'static str,
    pub class: &'static str,
    pub rect: Rect,
    pub enabled: bool,
    pub scrolled: bool,
}

#[derive(Default)]
struct Recorder {
    widgets: Vec<WidgetRecord>,
    scrolled: bool,
}

impl Recorder {
    fn push(&mut self, name: &'static str, class: &'static str, response: &egui::Response) {
        self.widgets.push(WidgetRecord {
            name,
            class,
            rect: response.rect,
            enabled: response.enabled(),
            scrolled: self.scrolled,
        });
    }

    fn add(&mut self, ui: &mut egui::Ui, name: &'static str, class: &'static str, widget: impl egui::Widget) {
        let response = ui.add(widget);
        self.push(name, class, &response);
    }

    fn button(&mut self, ui: &mut egui::Ui, enabled: bool, name: &'static str, widget: impl egui::Widget) -> bool {
        let response = ui.add_enabled(enabled, widget);
        self.push(name, "button", &response);
        response.clicked()
    }

    /// The "Mod page" and "License" links, kept whole: when the row has no
    /// room for both, they start the next row.
    fn links(&mut self, ui: &mut egui::Ui, links: [(&'static str, &str, &str); 2]) {
        let font = egui::TextStyle::Body.resolve(ui.style());
        let spacing = ui.spacing().item_spacing.x;
        let needed: f32 = links
            .iter()
            .map(|(_, text, _)| {
                ui.painter().layout_no_wrap(text.to_string(), font.clone(), egui::Color32::PLACEHOLDER).size().x
                    + spacing
            })
            .sum();
        if ui.available_size_before_wrap().x < needed {
            ui.end_row();
        }
        for (name, text, url) in links {
            let response = ui
                .scope(|ui| {
                    ui.style_mut().wrap_mode = Some(egui::TextWrapMode::Extend);
                    ui.hyperlink_to(text, url)
                })
                .inner;
            self.push(name, "link", &response);
        }
    }
}

/// Language (code, label) pairs and the index of the selected one.
type Languages = (Vec<(String, String)>, Option<usize>);

/// The languages of `data/config/game_language.json` and the selected one;
/// none before the first preparation.
pub fn read_languages(data_root: &Path) -> Result<Languages, String> {
    let path = forge_native::language::config_path(data_root);
    if !path.exists() {
        return Ok((Vec::new(), None));
    }
    let document = forge_native::pyerr::load_json_object(&path)
        .and_then(|document| forge_native::language::validate(&Value::Object(document)))
        .map_err(|error| error.message)?;
    let field = |entry: &Value, key: &str| entry.get(key).and_then(Value::as_str).unwrap_or_default().to_string();
    let languages: Vec<(String, String)> = document["available"]
        .as_array()
        .map(|entries| entries.iter().map(|entry| (field(entry, "code"), field(entry, "label"))).collect())
        .unwrap_or_default();
    let selected = languages.iter().position(|(code, _)| Some(code.as_str()) == document["selected"].as_str());
    Ok((languages, selected))
}

pub struct ForgeApp {
    pub(crate) installation: Installation,
    optional: Arc<Optional>,
    pub(crate) rom: String,
    languages: Vec<(String, String)>,
    language: Option<usize>,
    /// The recognized-ROM card and the ROM path it belongs to.
    pub(crate) card: Option<(String, Card)>,
    /// The card's ROM is the installed title: its buttons edit the setting.
    card_installed: bool,
    card_cancel: CancelToken,
    pub(crate) status: String,
    hd_status: String,
    hd_state: Checked,
    hd_auto_download: bool,
    mod_status: String,
    mod_state: Checked,
    mod_enabled: bool,
    mod_settings_error: Option<String>,
    pub(crate) active_title: Option<ActiveTitle>,
    pub(crate) busy: bool,
    picking: bool,
    progress: Option<(u64, Option<u64>)>,
    events: Receiver<Event>,
    notifier: Notifier,
    cancel: CancelToken,
    workers: Vec<JoinHandle<()>>,
    dialogs: VecDeque<Dialog>,
    closing: bool,
    pub(crate) geometry: Geometry,
    pub(crate) widgets: Vec<WidgetRecord>,
    probe: Option<Box<Probe>>,
}

impl ForgeApp {
    fn new(
        context: &egui::Context,
        installation: Installation,
        startup_error: Option<String>,
        probe: Option<Box<Probe>>,
        work_area: Option<Vec2>,
    ) -> Result<Self, String> {
        context.set_fonts(crate::fonts::system_fonts()?);
        let (sender, events) = channel();
        let (optional, contracts_error) = Optional::load(&installation);
        let (mod_enabled, mod_settings_error) = match optional.enabled() {
            Ok(enabled) => (enabled, None),
            Err(error) => (false, Some(error.message)),
        };
        let mut app = Self {
            installation,
            optional: Arc::new(optional),
            rom: String::new(),
            languages: Vec::new(),
            language: None,
            card: None,
            card_installed: false,
            card_cancel: CancelToken::new(),
            status: texts::CHECKING_INSTALLATION.to_string(),
            hd_status: String::new(),
            hd_state: None,
            // On by default each start: "Prepare and install" downloads the
            // HD pack first when it is not stored yet.
            hd_auto_download: true,
            mod_status: String::new(),
            mod_state: None,
            mod_enabled,
            mod_settings_error,
            active_title: None,
            busy: false,
            picking: false,
            progress: None,
            events,
            notifier: Notifier::new(sender, Some(context.clone())),
            cancel: CancelToken::new(),
            workers: Vec::new(),
            dialogs: VecDeque::new(),
            closing: false,
            geometry: Geometry::new(work_area),
            widgets: Vec::new(),
            probe,
        };
        app.refresh_active_title();
        if let Some(error) = contracts_error {
            app.status = format!("Cannot read the supported game revisions: {error}");
        }
        app.refresh_hd();
        app.refresh_mod();
        app.spawn(|optional, notify, cancel| {
            notify.send(Event::HdChecked(optional.hd_state(&cancel)));
            notify.send(Event::ModChecked(optional.mod_state(&cancel)));
        });
        if let Some(error) = startup_error {
            app.dialog(DialogKind::Error, "TriAevum", error);
        }
        Ok(app)
    }

    fn idle(&self) -> bool {
        !self.busy && !self.picking
    }

    pub(crate) fn can_install(&self) -> bool {
        !self.rom.trim().is_empty() && self.idle() && !self.recognizing()
    }

    pub(crate) fn recognizing(&self) -> bool {
        matches!(self.current_card(), Some(Card::Recognizing(_)))
    }

    /// The card of the ROM in the field; none while a new path is typed.
    pub(crate) fn current_card(&self) -> Option<&Card> {
        self.card.as_ref().filter(|(source, _)| *source == self.rom.trim()).map(|(_, card)| card)
    }

    /// The recognized ROM whose language choice is still to be installed.
    fn pending_rom(&self) -> Option<&RecognizedRom> {
        match self.current_card() {
            Some(Card::Recognized(rom)) if !self.card_installed => Some(rom),
            _ => None,
        }
    }

    /// The language buttons: the pending ROM's, else the installed setting's,
    /// with the selected index.
    pub(crate) fn language_options(&self) -> (&[(String, String)], Option<usize>) {
        match self.pending_rom() {
            Some(rom) => (&rom.languages, rom.selected),
            None => (&self.languages, self.language),
        }
    }

    /// The layout probe's fixed card for `source`, without recognition.
    pub(crate) fn probe_card(&mut self, source: &str, card: Card) {
        self.rom = source.to_string();
        self.card = Some((source.to_string(), card));
    }

    /// Recognizes the ROM in the field unless its card is already shown (a
    /// failed one is retried). Not while a preparation runs.
    pub(crate) fn recognize(&mut self) {
        let source = self.rom.trim().to_string();
        let shown = |(shown, card): &(String, Card)| *shown == source && !matches!(card, Card::Failed(_));
        if self.busy || self.card.as_ref().is_some_and(shown) {
            return;
        }
        self.card_cancel.cancel();
        self.card_installed = false;
        if source.is_empty() {
            self.card = None;
            return;
        }
        self.card_cancel = CancelToken::new();
        self.card = Some((source.clone(), Card::Recognizing(texts::RECOGNIZING.to_string())));
        let request = recognize_request(Path::new(&source), &self.installation.data_root, &self.installation.root);
        let own = self.card_cancel.clone();
        self.spawn(move |_, notify, cancel| run_recognition(source, request, notify, cancel, own));
    }

    fn spawn(&mut self, work: impl FnOnce(Arc<Optional>, Notifier, CancelToken) + Send + 'static) {
        let (optional, notify, cancel) = (Arc::clone(&self.optional), self.notifier.clone(), self.cancel.clone());
        self.workers.retain(|worker| !worker.is_finished());
        self.workers.push(std::thread::spawn(move || work(optional, notify, cancel)));
    }

    fn dialog(&mut self, kind: DialogKind, title: &'static str, message: String) {
        if let Some(probe) = &mut self.probe {
            // Unattended: record the notification instead of waiting for a click.
            probe.record(kind, &message);
            return;
        }
        self.dialogs.push_back(Dialog { kind, title, message, focus_pending: true });
    }

    fn refresh_active_title(&mut self) {
        self.active_title = load_active_title(&self.installation.active_title_state());
        self.status = match &self.active_title {
            None => texts::NO_ACTIVE_TITLE.to_string(),
            Some(active) => texts::ready_to_play(&active.recipe_id, &active.directory),
        };
        self.refresh_languages();
    }

    fn refresh_languages(&mut self) {
        match read_languages(&self.installation.data_root) {
            Ok((languages, selected)) => (self.languages, self.language) = (languages, selected),
            Err(error) => {
                (self.languages, self.language) = (Vec::new(), None);
                self.status = format!("Cannot read game language settings: {error}");
            }
        }
    }

    fn refresh_hd(&mut self) {
        self.hd_status = texts::hd_status(self.optional.hd_contract.as_ref(), &self.hd_state, self.hd_auto_download);
    }

    fn refresh_mod(&mut self) {
        self.mod_status = texts::mod_status(
            self.mod_settings_error.as_deref(),
            self.optional.mod_contract.as_ref(),
            &self.mod_state,
            self.mod_enabled,
        );
    }

    fn drain_events(&mut self) {
        while let Ok(event) = self.events.try_recv() {
            self.handle(event);
        }
    }

    fn handle(&mut self, event: Event) {
        match event {
            Event::Selection(target, path) => {
                self.picking = false;
                if let Some(path) = path {
                    self.selected(target, path);
                }
            }
            Event::Stage(message) => {
                self.status = message;
                self.progress = None;
            }
            Event::Progress { done, total } => self.progress = Some((done, total)),
            Event::HdStage(text) => self.hd_status = text,
            Event::ModStage(text) => self.mod_status = text,
            Event::HdChecked(state) => {
                if self.hd_state.is_none() {
                    self.hd_state = Some(state);
                    if !self.busy {
                        self.refresh_hd();
                    }
                }
            }
            Event::ModChecked(state) => {
                if self.mod_state.is_none() {
                    self.mod_state = Some(state);
                    if !self.busy {
                        self.refresh_mod();
                    }
                }
            }
            Event::HdDone { error, state } => {
                self.busy = false;
                self.hd_state = Some(state);
                self.refresh_hd();
                if let (Some(error), Some(contract)) = (error, self.optional.hd_contract.as_ref()) {
                    let message = texts::manual_hd_failure(&error, contract, &self.optional.pack);
                    self.dialog(DialogKind::Error, texts::DIALOG_TITLE, message);
                } else if self.active_title.is_some() {
                    self.hd_status = "HD textures ready. Prepare and install the title again to apply them.".into();
                }
            }
            Event::ModDone { error, state } => {
                self.busy = false;
                self.mod_state = Some(state);
                self.refresh_mod();
                if let (Some(error), Some(contract)) = (error, self.optional.mod_contract.as_ref()) {
                    let message = texts::manual_mod_failure(&error, contract, &self.optional.mod_);
                    self.dialog(DialogKind::Error, texts::DIALOG_TITLE, message);
                } else if self.active_title.is_some() && self.mod_enabled {
                    self.mod_status = format!(
                        "{} is ready. Prepare and install the title again to apply it.",
                        self.optional.mod_.title
                    );
                }
            }
            Event::InstallFailed(message) => {
                self.busy = false;
                self.progress = None;
                self.status = texts::PREPARATION_FAILED.to_string();
                self.dialog(DialogKind::Error, texts::DIALOG_TITLE, message);
            }
            Event::InstallComplete { hd, mod_ } => {
                self.busy = false;
                self.progress = None;
                self.card_installed = true;
                self.refresh_active_title();
                (self.hd_state, self.mod_state) = (Some(hd), Some(mod_));
                if !self.hd_status.starts_with(texts::HD_DOWNLOAD_FAILED_PREFIX) {
                    self.refresh_hd();
                }
                if !self.mod_status.starts_with(texts::MOD_DOWNLOAD_FAILED_PREFIX) {
                    self.refresh_mod();
                }
                self.dialog(DialogKind::Info, texts::DIALOG_TITLE, texts::READY_DIALOG.to_string());
            }
            Event::InstallCancelled => {
                self.busy = false;
                self.progress = None;
            }
            Event::Recognition { source, card } => {
                if self.card.as_ref().is_some_and(|(shown, _)| *shown == source) {
                    self.card = Some((source, card));
                }
            }
        }
    }

    fn selected(&mut self, target: PickTarget, path: PathBuf) {
        match target {
            PickTarget::Rom | PickTarget::Extracted => {
                // A picked file is recognized again, also when it is the same path.
                self.rom = path.to_string_lossy().into_owned();
                self.card = None;
                self.recognize();
            }
            PickTarget::HdArchive => self.run_hd("Verifying the selected archive...", move |optional, _, cancel| {
                optional.import_hd_archive(&path, cancel)
            }),
            PickTarget::HdFolder => self.run_hd("Verifying the extracted textures...", move |optional, _, cancel| {
                optional.import_hd_folder(&path, cancel)
            }),
            PickTarget::ModArchive => self.run_mod("Verifying the selected archive...", move |optional, _, cancel| {
                optional.import_mod_archive(&path, cancel)
            }),
            PickTarget::ModFolder => self.run_mod("Verifying the extracted files...", move |optional, _, cancel| {
                optional.import_mod_folder(&path, cancel)
            }),
        }
    }

    /// `_run_hd`: one HD-pack task at a time, while nothing else runs.
    fn run_hd(
        &mut self,
        message: &str,
        task: impl FnOnce(&Optional, &mut dyn FnMut(String), &CancelToken) -> forge_base::Result<()> + Send + 'static,
    ) {
        if !self.idle() || self.optional.hd_contract.is_none() {
            return;
        }
        self.busy = true;
        self.progress = None;
        self.hd_status = message.to_string();
        self.spawn(move |optional, notify, cancel| {
            let mut report = |text: String| notify.send(Event::HdStage(text));
            let error = task(&optional, &mut report, &cancel).err().map(|error| error.message);
            notify.send(Event::HdDone { error, state: optional.hd_state(&cancel) });
        });
    }

    /// `_run_mod`.
    fn run_mod(
        &mut self,
        message: &str,
        task: impl FnOnce(&Optional, &mut dyn FnMut(String), &CancelToken) -> forge_base::Result<()> + Send + 'static,
    ) {
        if !self.idle() || self.optional.mod_contract.is_none() {
            return;
        }
        self.busy = true;
        self.progress = None;
        self.mod_status = message.to_string();
        self.spawn(move |optional, notify, cancel| {
            let mut report = |text: String| notify.send(Event::ModStage(text));
            let error = task(&optional, &mut report, &cancel).err().map(|error| error.message);
            notify.send(Event::ModDone { error, state: optional.mod_state(&cancel) });
        });
    }

    fn apply(&mut self, action: Action, frame: &eframe::Frame) {
        match action {
            Action::Pick(target) => self.pick(target, frame),
            Action::Install => self.start_install(),
            Action::Launch => {
                if let Err(error) = crate::launch::launch_runtime(&self.installation) {
                    self.dialog(DialogKind::Error, texts::DIALOG_TITLE, error.message);
                }
            }
            Action::DownloadHd => {
                let message = "Downloading the HD textures...";
                self.run_hd(message, |optional, report, cancel| optional.download_hd(report, cancel));
            }
            Action::DownloadMod => {
                let message = format!("Downloading {}...", self.optional.mod_.title);
                self.run_mod(&message, |optional, report, cancel| optional.download_mod(report, cancel));
            }
            Action::ToggleMod => self.toggle_mod(),
            Action::SelectLanguage(index) => self.choose_language(index),
            Action::Recognize => self.recognize(),
        }
    }

    fn pick(&mut self, target: PickTarget, frame: &eframe::Frame) {
        if !self.idle() {
            return;
        }
        let optional = &self.optional;
        let contract_name = |contract: Option<&forge_mods::ArchiveContract>, fallback: &str| {
            format!("Select {}", contract.map_or(fallback, |contract| contract.name.as_str()))
        };
        let dialog = rfd::FileDialog::new();
        let (dialog, folder) = match target {
            PickTarget::Rom => (
                dialog
                    .set_title("Select a decrypted .3ds or .cci ROM")
                    .add_filter("Nintendo 3DS ROM", &["3ds", "cci"])
                    .add_filter("3DS image", &["3ds"])
                    .add_filter("CCI image", &["cci"]),
                false,
            ),
            PickTarget::Extracted => (dialog.set_title("Select extracted title data"), true),
            PickTarget::HdArchive => (
                dialog
                    .set_title(contract_name(optional.hd_contract.as_ref(), "the texture pack zip"))
                    .add_filter("Texture pack archive", &["zip"]),
                false,
            ),
            PickTarget::HdFolder => {
                (dialog.set_title(format!("Select the extracted {} folder", optional.pack.title)), true)
            }
            PickTarget::ModArchive => (
                dialog
                    .set_title(contract_name(optional.mod_contract.as_ref(), "the archive"))
                    .add_filter("Mod archive", &["rar"]),
                false,
            ),
            PickTarget::ModFolder => {
                (dialog.set_title(format!("Select the extracted {} folder", optional.mod_.title)), true)
            }
        };
        let dialog = dialog.set_parent(frame);
        self.picking = true;
        let notify = self.notifier.clone();
        // The dialog runs on its own thread so the window keeps repainting.
        std::thread::spawn(move || {
            let path = if folder { dialog.pick_folder() } else { dialog.pick_file() };
            notify.send(Event::Selection(target, path));
        });
    }

    fn install_plan(&self) -> InstallPlan {
        InstallPlan {
            source: PathBuf::from(self.rom.trim()),
            data_root: self.installation.data_root.clone(),
            package_root: self.installation.root.clone(),
            hd_auto_download: self.hd_auto_download,
            mod_enabled: self.mod_enabled,
            game_language: self.pending_rom().and_then(RecognizedRom::chosen).map(str::to_string),
        }
    }

    pub(crate) fn start_install(&mut self) {
        if !self.idle() {
            return;
        }
        let plan = self.install_plan();
        let missing = |contract: Option<&forge_mods::ArchiveContract>, state: &Checked| {
            contract.is_some() && *state == Some(Ok(Status::Missing))
        };
        self.status = if self.hd_auto_download && missing(self.optional.hd_contract.as_ref(), &self.hd_state) {
            texts::DOWNLOADING_HD_FIRST
        } else if self.mod_enabled && missing(self.optional.mod_contract.as_ref(), &self.mod_state) {
            texts::DOWNLOADING_MOD_FIRST
        } else {
            texts::STARTING
        }
        .to_string();
        self.busy = true;
        self.progress = None;
        self.spawn(move |optional, notify, cancel| run_install(optional, plan, notify, cancel));
    }

    fn toggle_mod(&mut self) {
        match self.optional.set_enabled(self.mod_enabled) {
            Ok(()) => self.mod_settings_error = None,
            Err(error) => {
                self.dialog(DialogKind::Error, texts::DIALOG_TITLE, format!("Cannot save the mod setting: {error}"));
                self.mod_settings_error = Some(error.message);
            }
        }
        self.refresh_mod();
        if self.active_title.is_some() && self.mod_settings_error.is_none() {
            self.mod_status.push_str(texts::APPLY_MOD_CHANGE);
        }
    }

    /// A language button: chosen for the pending installation, or saved
    /// for the installed title.
    pub(crate) fn choose_language(&mut self, index: usize) {
        if !self.idle() {
            return;
        }
        let rom = self.rom.trim().to_string();
        let card_installed = self.card_installed;
        if let Some((source, Card::Recognized(pending))) = &mut self.card {
            if !card_installed && *source == rom {
                if let Some((_, label)) = pending.languages.get(index) {
                    self.status = texts::language_chosen(label);
                    pending.selected = Some(index);
                }
                return;
            }
        }
        self.select_language(index);
    }

    fn select_language(&mut self, index: usize) {
        let Some((code, _)) = self.languages.get(index).cloned() else { return };
        match forge_native::language::select(&self.installation.data_root, &code) {
            Ok(()) => {
                self.language = Some(index);
                self.status = texts::LANGUAGE_SAVED.to_string();
            }
            Err(error) => {
                self.dialog(DialogKind::Error, texts::DIALOG_TITLE, error.message);
                self.refresh_languages();
            }
        }
    }

    /// Cancels the tasks and waits for them a bounded time; an import or
    /// download stops at its next cancellation check.
    fn shut_down(&mut self) {
        self.cancel.cancel();
        self.card_cancel.cancel();
        let deadline = Instant::now() + Duration::from_secs(15);
        while self.workers.iter().any(|worker| !worker.is_finished()) && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(20));
        }
    }

    pub(crate) fn close(&mut self, context: &egui::Context) {
        self.shut_down();
        self.closing = true;
        context.send_viewport_cmd(egui::ViewportCommand::Close);
    }

    fn handle_close_request(&mut self, context: &egui::Context) {
        if self.closing || !context.input(|input| input.viewport().close_requested()) {
            return;
        }
        if self.busy {
            context.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            if !self.dialogs.iter().any(|dialog| dialog.kind == DialogKind::ConfirmClose) {
                self.dialogs.push_back(Dialog {
                    kind: DialogKind::ConfirmClose,
                    title: texts::DIALOG_TITLE,
                    message: texts::CLOSE_WHILE_BUSY.to_string(),
                    focus_pending: true,
                });
            }
        } else {
            self.shut_down();
            self.closing = true;
        }
    }

    fn show_dialog(&mut self, context: &egui::Context) {
        let Some(dialog) = self.dialogs.front_mut() else { return };
        let mut answer = None;
        let width = (context.content_rect().width() - 48.0).clamp(200.0, 460.0);
        let modal = egui::Modal::new(egui::Id::new("forge-dialog")).show(context, |ui| {
            ui.set_width(width);
            ui.heading(dialog.title);
            ui.add_space(6.0);
            let height = (context.content_rect().height() * 0.6).max(80.0);
            egui::ScrollArea::vertical().max_height(height).show(ui, |ui| {
                ui.add(Label::new(&dialog.message).wrap());
            });
            ui.add_space(10.0);
            ui.horizontal(|ui| {
                let primary = if dialog.kind == DialogKind::ConfirmClose {
                    let yes = ui.button("Yes");
                    if ui.button("No").clicked() {
                        answer = Some(false);
                    }
                    yes
                } else {
                    ui.button("OK")
                };
                if dialog.focus_pending {
                    primary.request_focus();
                    dialog.focus_pending = false;
                }
                if primary.clicked() {
                    answer = Some(true);
                }
            });
        });
        // Esc or a click outside dismisses; for the close question that is "No".
        if answer.is_none() && modal.should_close() {
            answer = Some(false);
        }
        if let Some(confirmed) = answer {
            let kind = self.dialogs.pop_front().map(|dialog| dialog.kind);
            if kind == Some(DialogKind::ConfirmClose) && confirmed {
                self.close(context);
            }
        }
    }

    fn body(&mut self, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
        let idle = self.idle();
        record.add(ui, "title", "label", Label::new(RichText::new("TriAevum Forge").size(24.0).strong()));
        record.add(ui, "subtitle", "label", Label::new(texts::SUBTITLE).wrap());
        ui.add_space(12.0);
        ui.horizontal_wrapped(|ui| {
            record.add(ui, "rom_label", "label", Label::new("Decrypted ROM"));
            let browse_width = 96.0;
            let width = (ui.available_width() - browse_width).max(160.0);
            let entry = ui.add(TextEdit::singleline(&mut self.rom).desired_width(width));
            record.push("rom", "entry", &entry);
            // A typed path is recognized once committed (Enter or leaving the field).
            if entry.lost_focus() {
                actions.push(Action::Recognize);
            }
            if record.button(ui, idle, "rom_browse", Button::new("Browse...")) {
                actions.push(Action::Pick(PickTarget::Rom));
            }
        });
        ui.add_space(6.0);
        self.rom_card(ui, record, actions);
        ui.add_space(6.0);
        record.add(ui, "private_data", "label", Label::new(texts::private_data(&self.installation.data_root)).wrap());
        if record.button(ui, idle, "use_extracted", Button::new("Use extracted data...")) {
            actions.push(Action::Pick(PickTarget::Extracted));
        }
        ui.add_space(16.0);
        self.hd_section(ui, record, actions);
        ui.add_space(12.0);
        self.mod_section(ui, record, actions);
        ui.add_space(8.0);
    }

    /// The recognized-ROM card right under the ROM row: the revision and the
    /// game languages as large buttons; the installed title's languages when
    /// no ROM is being prepared.
    fn rom_card(&mut self, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
        let visuals = ui.visuals().clone();
        let card = self.current_card().cloned();
        let (choices, selected) = self.language_options();
        let choices = choices.to_vec();
        // Highlighted once there is a ROM or a language to show.
        let accent = match &card {
            Some(Card::Failed(_)) => visuals.error_fg_color,
            None if choices.is_empty() => visuals.weak_text_color(),
            _ => visuals.selection.stroke.color,
        };
        let pending = self.pending_rom().is_some();
        let enabled = self.idle();
        let frame = egui::Frame::group(ui.style())
            .inner_margin(14.0)
            .fill(accent.gamma_multiply(0.08))
            .stroke(Stroke::new(1.5, accent));
        let response = frame.show(ui, |ui| {
            ui.set_width(ui.available_width());
            let heading = card.as_ref().map_or(texts::GAME_LANGUAGE.to_string(), crate::recognition::heading);
            ui.horizontal_wrapped(|ui| {
                if matches!(card, Some(Card::Recognizing(_))) {
                    ui.add(Spinner::new());
                }
                let text = RichText::new(heading).size(18.0).strong().color(accent);
                record.add(ui, "rom_card_title", "label", Label::new(text).wrap());
            });
            match &card {
                Some(Card::Recognizing(stage)) => record.add(ui, "rom_card_status", "label", Label::new(stage).wrap()),
                Some(Card::Failed(message)) => {
                    let text = RichText::new(message).color(visuals.error_fg_color);
                    record.add(ui, "rom_card_status", "label", Label::new(text).wrap());
                }
                Some(Card::Recognized(rom)) if pending && rom.languages.is_empty() => {
                    let message = texts::languages_unreadable(rom.language_error.as_deref().unwrap_or_default());
                    record.add(ui, "rom_card_status", "label", Label::new(message).wrap());
                }
                _ if choices.is_empty() => {
                    record.add(ui, "rom_card_status", "label", Label::new(texts::NO_LANGUAGES).wrap());
                }
                _ => {
                    ui.add_space(4.0);
                    record.add(ui, "language_label", "label", Label::new(texts::CHOOSE_LANGUAGE));
                    ui.add_space(2.0);
                    ui.horizontal_wrapped(|ui| {
                        ui.spacing_mut().item_spacing = Vec2::new(8.0, 8.0);
                        for (index, (_, label)) in choices.iter().enumerate() {
                            let text = RichText::new(label).size(17.0);
                            let button =
                                Button::new(text).selected(selected == Some(index)).min_size(Vec2::new(116.0, 36.0));
                            if record.button(ui, enabled, "language_choice", button) {
                                actions.push(Action::SelectLanguage(index));
                            }
                        }
                    });
                    ui.add_space(2.0);
                    let hint = if pending { texts::LANGUAGE_FOR_INSTALL } else { texts::LANGUAGE_INSTALLED };
                    record.add(ui, "language_hint", "label", Label::new(hint).wrap());
                }
            }
        });
        record.push("rom_card", "group", &response.response);
    }

    fn section(ui: &mut egui::Ui, heading: &str, contents: impl FnOnce(&mut egui::Ui)) {
        egui::Frame::group(ui.style()).inner_margin(12.0).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.label(RichText::new(heading).size(16.0).strong());
            ui.add_space(4.0);
            contents(ui);
        });
    }

    fn hd_section(&mut self, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
        let enabled = self.idle() && self.optional.hd_contract.is_some();
        let pack = self.optional.pack.clone();
        Self::section(ui, "HD UI textures (recommended)", |ui| {
            record.add(ui, "hd_description", "label", Label::new(texts::hd_description(&pack)).wrap());
            ui.add_space(8.0);
            ui.horizontal_wrapped(|ui| {
                let download = Button::new(RichText::new("Download HD textures").strong());
                if record.button(ui, enabled, "hd_download", download) {
                    actions.push(Action::DownloadHd);
                }
                if record.button(ui, enabled, "hd_select_zip", Button::new("Select .zip...")) {
                    actions.push(Action::Pick(PickTarget::HdArchive));
                }
                if record.button(ui, enabled, "hd_select_folder", Button::new("Select extracted folder...")) {
                    actions.push(Action::Pick(PickTarget::HdFolder));
                }
                record.links(ui, [("hd_page", "Mod page", &pack.page), ("hd_license", "License", &pack.license_url)]);
            });
            let check = ui.add_enabled(enabled, Checkbox::new(&mut self.hd_auto_download, texts::HD_AUTO_DOWNLOAD));
            record.push("hd_auto_download", "checkbox", &check);
            if check.changed() {
                self.hd_status =
                    texts::hd_status(self.optional.hd_contract.as_ref(), &self.hd_state, self.hd_auto_download);
            }
            record.add(ui, "hd_status", "label", Label::new(&self.hd_status).wrap());
        });
    }

    fn mod_section(&mut self, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
        let available = self.optional.mod_contract.is_some() && self.mod_settings_error.is_none();
        let enabled = self.idle() && available;
        let mod_ = self.optional.mod_.clone();
        Self::section(ui, "Optional mods", |ui| {
            record.add(ui, "mod_description", "label", Label::new(texts::mod_description(&mod_)).wrap());
            let check = ui.add_enabled(enabled, Checkbox::new(&mut self.mod_enabled, texts::mod_checkbox(&mod_)));
            record.push("mod_enabled", "checkbox", &check);
            if check.changed() {
                actions.push(Action::ToggleMod);
            }
            ui.horizontal_wrapped(|ui| {
                if record.button(ui, enabled, "mod_download", Button::new("Download mod")) {
                    actions.push(Action::DownloadMod);
                }
                if record.button(ui, enabled, "mod_select_rar", Button::new("Select .rar...")) {
                    actions.push(Action::Pick(PickTarget::ModArchive));
                }
                if record.button(ui, enabled, "mod_select_folder", Button::new("Select extracted folder...")) {
                    actions.push(Action::Pick(PickTarget::ModFolder));
                }
                record.links(ui, [("mod_page", "Mod page", &mod_.page), ("mod_license", "License", &mod_.license_url)]);
            });
            record.add(ui, "mod_status", "label", Label::new(&self.mod_status).wrap());
        });
    }

    fn footer(&mut self, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
        ui.add_space(4.0);
        // A long error scrolls inside a few lines instead of pushing the
        // actions out of the window.
        let lines = ui.text_style_height(&egui::TextStyle::Body) * 4.0;
        egui::ScrollArea::vertical().id_salt("status").max_height(lines).show(ui, |ui| {
            record.add(ui, "status", "label", Label::new(&self.status).wrap());
        });
        ui.add_space(8.0);
        let fraction = match (self.busy, self.progress) {
            (true, Some((done, Some(total)))) if total > 0 => done as f32 / total as f32,
            // Indeterminate: a sweeping bar.
            (true, _) => (ui.input(|input| input.time) * 0.6).fract() as f32,
            (false, _) => 0.0,
        };
        let mut bar = ProgressBar::new(fraction).desired_height(10.0);
        if !self.busy {
            bar = bar.fill(egui::Color32::TRANSPARENT);
        }
        record.add(ui, "progress", "progress", bar);
        ui.add_space(10.0);
        ui.horizontal(|ui| {
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                let launch = self.active_title.is_some() && self.idle();
                if record.button(ui, launch, "launch", Button::new("Launch game")) {
                    actions.push(Action::Launch);
                }
                if record.button(ui, self.can_install(), "install", Button::new("Prepare and install")) {
                    actions.push(Action::Install);
                }
            });
        });
        ui.add_space(4.0);
    }
}

impl eframe::App for ForgeApp {
    fn ui(&mut self, ui: &mut egui::Ui, frame: &mut eframe::Frame) {
        let context = ui.ctx().clone();
        self.drain_events();
        self.handle_close_request(&context);
        self.geometry.update(&context, frame);
        let mut actions = Vec::new();
        let mut record = Recorder::default();
        let margin = egui::Margin::symmetric(16, 8);
        egui::Panel::bottom("footer")
            .frame(egui::Frame::side_top_panel(ui.style()).inner_margin(margin))
            .show(ui, |ui| self.footer(ui, &mut record, &mut actions));
        egui::CentralPanel::default_margins().show(ui, |ui| {
            egui::ScrollArea::vertical().auto_shrink(false).show(ui, |ui| {
                record.scrolled = true;
                self.body(ui, &mut record, &mut actions);
                record.scrolled = false;
            });
        });
        self.show_dialog(&context);
        self.widgets = record.widgets;
        for action in actions {
            self.apply(action, frame);
        }
        if self.busy || self.recognizing() {
            context.request_repaint_after(Duration::from_millis(50));
        }
        if let Some(mut probe) = self.probe.take() {
            probe.after_frame(self, &context);
            self.probe = Some(probe);
        }
    }
}

fn native_options() -> eframe::NativeOptions {
    eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title(texts::WINDOW_TITLE)
            .with_app_id(forge_core::layout::FLATPAK_ID)
            .with_inner_size(INITIAL_SIZE)
            .with_min_inner_size(MIN_SIZE)
            // No egui logo: the platform's default application icon.
            .with_icon(egui::IconData::default()),
        renderer: eframe::Renderer::Glow,
        centered: true,
        ..Default::default()
    }
}

/// Opens the window and runs it until closed.
pub fn run_window(
    installation: Option<Installation>,
    startup_error: Option<String>,
    probe: Option<Box<Probe>>,
    work_area: Option<Vec2>,
) -> Result<(), String> {
    let installation = match installation {
        Some(installation) => installation,
        None => Installation::current().map_err(|error| error.message)?,
    };
    eframe::run_native(
        texts::WINDOW_TITLE,
        native_options(),
        Box::new(move |creation| {
            let app = ForgeApp::new(&creation.egui_ctx, installation, startup_error, probe, work_area)?;
            Ok(Box::new(app))
        }),
    )
    .map_err(|error| error.to_string())
}

/// `forge_gui.main`. A window that cannot open (no OpenGL, no display) is
/// reported with the command line alternative.
pub fn run(installation: Option<Installation>, startup_error: Option<String>) -> i32 {
    match run_window(installation, startup_error, None, None) {
        Ok(()) => 0,
        Err(error) => {
            let message = format!(
                "TriAevum Forge GUI failed: {error}\n\nThe command line tool triaevum-forge can still prepare titles."
            );
            eprintln!("{message}");
            let _ = rfd::MessageDialog::new()
                .set_level(rfd::MessageLevel::Error)
                .set_title(texts::DIALOG_TITLE)
                .set_description(message)
                .show();
            1
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn languages_come_from_the_prepared_settings() {
        let directory = tempfile::tempdir().unwrap();
        assert_eq!(read_languages(directory.path()).unwrap(), (Vec::new(), None));
        let path = forge_native::language::config_path(directory.path());
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(
            &path,
            br#"{"format": "triaevum_game_language_v1", "selected": "fr", "available": [
                {"code": "en", "label": "English", "system_id": 1},
                {"code": "fr", "label": "Francais", "system_id": 2}]}"#,
        )
        .unwrap();
        let (languages, selected) = read_languages(directory.path()).unwrap();
        assert_eq!(languages[1], ("fr".to_string(), "Francais".to_string()));
        assert_eq!(selected, Some(1));
        std::fs::write(&path, br#"{"format": "x"}"#).unwrap();
        assert_eq!(read_languages(directory.path()).unwrap_err(), "Invalid game language information");
    }

    fn recognized(codes: &[&str]) -> Card {
        Card::Recognized(RecognizedRom {
            label: "Game (EUR)".into(),
            languages: codes.iter().map(|code| (code.to_string(), code.to_uppercase())).collect(),
            selected: Some(0),
            language_error: None,
        })
    }

    #[test]
    fn the_recognized_rom_card_drives_the_installation_language() {
        let directory = tempfile::tempdir().unwrap();
        let installation = Installation::at(directory.path()).unwrap();
        let data_root = installation.data_root.clone();
        let mut app = ForgeApp::new(&egui::Context::default(), installation, None, None, None).unwrap();
        assert_eq!(app.language_options(), (&[][..], None));

        app.probe_card("game.cci", Card::Recognizing(texts::RECOGNIZING.into()));
        assert!(app.recognizing() && !app.can_install());
        app.probe_card("game.cci", recognized(&["en", "it"]));
        assert!(app.can_install());
        app.choose_language(1);
        assert_eq!(app.language_options().1, Some(1));
        assert_eq!(app.install_plan().game_language.as_deref(), Some("it"));
        assert!(!forge_native::language::config_path(&data_root).exists(), "chosen, not saved");

        // A path being typed hides the card and its choice.
        app.rom = "other.cci".into();
        assert_eq!(app.current_card(), None);
        assert_eq!(app.install_plan().game_language, None);
        app.rom = "game.cci".into();

        // A late event for another ROM changes nothing.
        let stale = Event::Recognition { source: "old.cci".into(), card: Card::Failed("x".into()) };
        app.handle(stale);
        assert!(matches!(app.current_card(), Some(Card::Recognized(_))));

        // Installed: the buttons show and save the installed setting.
        let detected = serde_json::json!({"format": "triaevum_game_language_v1", "selected": "en", "available": [
            {"code": "en", "label": "English", "system_id": 1}, {"code": "it", "label": "Italiano", "system_id": 4}]});
        forge_native::language::install_preferring(&data_root, &detected, Some("it")).unwrap();
        app.handle(Event::InstallComplete { hd: Ok(Status::Missing), mod_: Ok(Status::Missing) });
        assert_eq!(app.language_options().1, Some(1));
        assert_eq!(app.install_plan().game_language, None);
        app.choose_language(0);
        assert_eq!(read_languages(&data_root).unwrap().1, Some(0));
        assert_eq!(app.status, texts::LANGUAGE_SAVED);
        app.shut_down();
    }
}
