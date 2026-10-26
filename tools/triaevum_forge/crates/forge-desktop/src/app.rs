// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Forge window (`forge_gui.ForgeWindow`): tabs on top, the current page
//! in a scroll area, and a footer with one status line, the progress bar and
//! the primary and secondary actions, always visible. Home shows the
//! installation state (`pages::home`). Anything slow (inspection, stored-file
//! checks, imports, downloads, the install job) runs on a task thread and
//! reports through [`Event`]s.

use std::collections::VecDeque;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, Receiver};
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use eframe::egui::{self, Align, Button, Label, Layout, Margin, ProgressBar, Rect, RichText, Vec2};
use forge_base::{CancelToken, ForgeError, Operation};
use forge_core::explain::Explanation;
use forge_core::state::InstallationState;
use forge_mods::Status;
use serde_json::Value;

use crate::active_title::{load_active_title, ActiveTitle};
use crate::install::{run_install, Event, InstallPlan, Notifier};
use crate::installation::Installation;
use crate::mods::Optional;
use crate::pages::enhancements::{self, Checked};
use crate::pages::home::{self, CardButton, Command, HomeCard, HomeInput, Preparing};
use crate::pages::{self, Page};
use crate::recognition::{recognize_request, run_recognition, Card, RecognizedRom};
use crate::smoke::Probe;
use crate::texts::{self, native_name, Msg, Texts};
use crate::window::{Geometry, SimulatedDisplay, INITIAL_SIZE, MIN_SIZE};

/// Below this width (logical points) the footer stacks its buttons.
pub const STACK_WIDTH: f32 = 420.0;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PickTarget {
    Rom,
    Extracted,
    HdArchive,
    HdFolder,
    ModArchive,
    ModFolder,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Action {
    Pick(PickTarget),
    Install,
    Launch,
    /// Asks before cancelling the preparation.
    Cancel,
    DownloadHd,
    DownloadMod,
    ToggleMod,
    SelectLanguage(usize),
    Recognize,
    Navigate(Page),
    CheckInstallation,
    CopyDiagnostics,
    /// Asks where to save the support bundle, then writes it.
    SaveBundle,
    OpenFolder(PathBuf),
    Dismiss,
    ToggleDetails,
    ToggleNeeds,
    TogglePrepareAgain,
    SetUiLanguage(String),
    Command(Command),
    /// The Data & backups page's actions.
    Data(pages::data::DataAction),
}

impl Action {
    /// The name the layout probe logs instead of running the action.
    pub fn name(&self) -> String {
        match self {
            Self::Command(command) => format!("{command:?}"),
            Self::Navigate(page) => format!("Navigate({})", page.code()),
            Self::OpenFolder(_) => "OpenFolder".to_string(),
            other => format!("{other:?}"),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DialogKind {
    Info,
    Error,
    ConfirmClose,
    ConfirmCancel,
}

pub struct Dialog {
    pub kind: DialogKind,
    title: String,
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
    /// Inside the page's scroll area.
    pub scrolled: bool,
    pub id: egui::Id,
    pub hovered: bool,
    pub focused: bool,
}

impl WidgetRecord {
    /// Takes input: a button, link, check box, combo box or text field.
    pub fn interactive(&self) -> bool {
        matches!(self.class, "button" | "link" | "checkbox" | "entry" | "combo")
    }
}

#[derive(Default)]
pub struct Recorder {
    pub widgets: Vec<WidgetRecord>,
    scrolled: bool,
    /// The probe's scroll check: the index of the widget to scroll into view.
    scroll_to: Option<usize>,
    viewport_height: f32,
}

impl Recorder {
    pub fn push(&mut self, name: &'static str, class: &'static str, response: &egui::Response) {
        if self.scrolled && self.scroll_to == Some(self.widgets.len()) {
            let align = if response.rect.height() > self.viewport_height { Align::Min } else { Align::Center };
            response.scroll_to_me_animation(Some(align), egui::style::ScrollAnimation::none());
        }
        self.widgets.push(WidgetRecord {
            name,
            class,
            rect: response.rect,
            enabled: response.enabled(),
            scrolled: self.scrolled,
            id: response.id,
            hovered: response.contains_pointer(),
            // Keyboard focus, also while the desktop focuses another window.
            focused: response.ctx.memory(|memory| memory.has_focus(response.id)),
        });
    }

    pub fn add(&mut self, ui: &mut egui::Ui, name: &'static str, class: &'static str, widget: impl egui::Widget) {
        let response = ui.add(widget);
        self.push(name, class, &response);
    }

    pub fn button(&mut self, ui: &mut egui::Ui, enabled: bool, name: &'static str, widget: impl egui::Widget) -> bool {
        let response = ui.add_enabled(enabled, widget);
        self.push(name, "button", &response);
        response.clicked()
    }

    /// Two links kept whole: when the row has no room for both, they start
    /// the next row.
    pub fn links(&mut self, ui: &mut egui::Ui, links: [(&'static str, &str, &str); 2]) {
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
        .map(|entries| {
            entries
                .iter()
                .map(|entry| {
                    let code = field(entry, "code");
                    let label = texts::language_label(&code, &field(entry, "label"));
                    (code, label)
                })
                .collect()
        })
        .unwrap_or_default();
    let selected = languages.iter().position(|(code, _)| Some(code.as_str()) == document["selected"].as_str());
    Ok((languages, selected))
}

pub struct ForgeApp {
    pub(crate) installation: Installation,
    pub(crate) optional: Arc<Optional>,
    pub(crate) texts: Texts,
    /// The Forge language the user picked (About); the system's otherwise.
    ui_language_choice: Option<String>,
    system_language: Option<String>,
    title_language: String,
    pub(crate) page: Page,
    /// The inspected installation; None while it is being checked.
    pub(crate) state: Option<InstallationState>,
    /// The probe shows fixed states: inspection results are ignored.
    pub(crate) freeze_state: bool,
    /// The last operation's failure, shown on Home until dismissed.
    pub(crate) failure: Option<Explanation>,
    /// Non-error outcomes of the last preparation.
    pub(crate) notices: Vec<Explanation>,
    /// The Diagnostics page's facts (collected when the page opens).
    pub(crate) diagnostics: Option<Result<Box<pages::diagnostics::DiagnosticsView>, String>>,
    /// A game started from this window is running.
    pub(crate) game_running: bool,
    pub(crate) rom: String,
    pub(crate) languages: Vec<(String, String)>,
    pub(crate) language: Option<usize>,
    pub(crate) language_error: Option<String>,
    /// The recognized-ROM card and the ROM path it belongs to.
    pub(crate) card: Option<(String, Card)>,
    /// The card's ROM is the installed title: its buttons edit the setting.
    card_installed: bool,
    card_cancel: CancelToken,
    /// A note for the status line (a saved setting, a cancelled preparation).
    pub(crate) status_note: Option<String>,
    /// The current stage of a task, for the Preparing card.
    pub(crate) stage: String,
    pub(crate) hd_status: String,
    hd_state: Checked,
    pub(crate) hd_auto_download: bool,
    hd_failed: bool,
    pub(crate) mod_status: String,
    mod_state: Checked,
    pub(crate) mod_enabled: bool,
    mod_failed: bool,
    pub(crate) mod_settings_error: Option<String>,
    pub(crate) contracts_error: Option<String>,
    /// The supported revisions (id, label).
    pub(crate) revisions: Vec<(String, String)>,
    pub(crate) package_version: String,
    pub(crate) active_title: Option<ActiveTitle>,
    pub(crate) busy: bool,
    /// When this window's preparation started.
    pub(crate) preparing: Option<Instant>,
    picking: bool,
    pub(crate) progress: Option<(u64, Option<u64>)>,
    events: Receiver<Event>,
    notifier: Notifier,
    cancel: CancelToken,
    install_cancel: CancelToken,
    workers: Vec<JoinHandle<()>>,
    pub(crate) dialogs: VecDeque<Dialog>,
    closing: bool,
    pub(crate) show_details: bool,
    pub(crate) show_needs: bool,
    pub(crate) show_prepare_again: bool,
    pub(crate) copied: bool,
    pub(crate) geometry: Geometry,
    pub(crate) widgets: Vec<WidgetRecord>,
    /// The page scroll area's visible rectangle in the last frame.
    pub(crate) scroll_viewport: Rect,
    footer_height: f32,
    /// The probe's scroll check: the widget to scroll into view next frame.
    pub(crate) scroll_to: Option<usize>,
    /// The page starts at its top in the next frame.
    pub(crate) scroll_top: bool,
    /// The probe's keyboard check: actions are logged instead of run.
    pub(crate) action_log: Option<Vec<String>>,
    probe: Option<Box<Probe>>,
    /// The Data & backups page: inventory, previews and its task.
    pub(crate) data: pages::data::DataState,
    /// Update and Repair in place: the pending question, the last operation.
    pub(crate) maintenance: crate::maintenance::MaintenanceState,
}

impl ForgeApp {
    fn new(
        context: &egui::Context,
        installation: Installation,
        startup_error: Option<ForgeError>,
        probe: Option<Box<Probe>>,
        simulated: SimulatedDisplay,
    ) -> Result<Self, String> {
        context.set_fonts(crate::fonts::system_fonts()?);
        // Scroll bars that stay visible: on a small screen the page goes on
        // below the window's edge, and egui's default hidden bar would not
        // say so (issue #111).
        context.all_styles_mut(|style| style.spacing.scroll = egui::style::ScrollStyle::thin());
        let (sender, events) = channel();
        let (optional, contracts_error) = Optional::load(&installation);
        let (mod_enabled, mod_settings_error) = match optional.enabled() {
            Ok(enabled) => (enabled, None),
            Err(error) => (false, Some(error.message)),
        };
        let revisions = forge_catalog::recipes::load_recipe_options(&installation.recipes())
            .map(|options| options.into_iter().map(|option| (option.id, option.label)).collect())
            .unwrap_or_default();
        let package_version = pages::about::package_version(&installation.root);
        let language_override = probe.as_ref().and_then(|probe| probe.language());
        let system_language = language_override.or_else(texts::system_language);
        // The language picked on About in an earlier session (Forge's own
        // settings); the layout probe starts from its fixed language.
        let ui_language_choice = if probe.is_some() { None } else { texts::preference::load(&installation.data_root) };
        let mut app = Self {
            installation,
            optional: Arc::new(optional),
            texts: Texts::default(),
            ui_language_choice,
            system_language,
            title_language: texts::ENGLISH.to_string(),
            page: Page::Home,
            state: None,
            freeze_state: false,
            failure: startup_error.map(|error| forge_core::explain::explain(&error)),
            notices: Vec::new(),
            diagnostics: None,
            game_running: false,
            rom: String::new(),
            languages: Vec::new(),
            language: None,
            language_error: None,
            card: None,
            card_installed: false,
            card_cancel: CancelToken::new(),
            status_note: None,
            stage: String::new(),
            hd_status: String::new(),
            hd_state: None,
            // On by default each start: "Prepare" downloads the HD pack first
            // when it is not stored yet (owner decision 5).
            hd_auto_download: true,
            hd_failed: false,
            mod_status: String::new(),
            mod_state: None,
            mod_enabled,
            mod_failed: false,
            mod_settings_error,
            contracts_error,
            package_version,
            revisions,
            active_title: None,
            busy: false,
            preparing: None,
            picking: false,
            progress: None,
            events,
            notifier: Notifier::new(sender, Some(context.clone())),
            cancel: CancelToken::new(),
            install_cancel: CancelToken::new(),
            workers: Vec::new(),
            dialogs: VecDeque::new(),
            closing: false,
            show_details: false,
            show_needs: false,
            show_prepare_again: false,
            copied: false,
            geometry: Geometry::new(simulated),
            widgets: Vec::new(),
            scroll_viewport: Rect::NOTHING,
            footer_height: 120.0,
            scroll_to: None,
            scroll_top: false,
            action_log: None,
            probe,
            data: Default::default(),
            maintenance: Default::default(),
        };
        app.refresh_installation();
        app.refresh_hd();
        app.refresh_mod();
        app.spawn(|optional, notify, cancel| {
            notify.send(Event::HdChecked(optional.hd_state(&cancel)));
            notify.send(Event::ModChecked(optional.mod_state(&cancel)));
        });
        Ok(app)
    }

    pub(crate) fn t(&self, msg: Msg) -> String {
        self.texts.get(msg)
    }

    pub(crate) fn install_cancelled(&self) -> bool {
        self.install_cancel.is_cancelled()
    }

    pub(crate) fn idle(&self) -> bool {
        !self.busy && !self.picking
    }

    /// Prepare needs a recognized ROM for the path in the field.
    pub(crate) fn can_install(&self) -> bool {
        self.pending_rom().is_some() && self.idle()
    }

    pub(crate) fn recognizing(&self) -> bool {
        matches!(self.current_card(), Some(Card::Recognizing(_)))
    }

    /// The card of the ROM in the field; none while a new path is typed.
    pub(crate) fn current_card(&self) -> Option<&Card> {
        self.card.as_ref().filter(|(source, _)| *source == self.rom.trim()).map(|(_, card)| card)
    }

    /// The recognized ROM whose language choice is still to be installed.
    pub(crate) fn pending_rom(&self) -> Option<&RecognizedRom> {
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

    /// The Forge languages (decision 8): the game languages of the ROM in
    /// use, else those a supported ROM provides.
    pub(crate) fn ui_languages(&self) -> Vec<(String, String)> {
        let rom = match self.pending_rom() {
            Some(rom) if !rom.languages.is_empty() => &rom.languages,
            _ => &self.languages,
        };
        let mut choices: Vec<(String, String)> = if rom.is_empty() {
            texts::SUPPORTED_ROM_LANGUAGES
                .iter()
                .map(|code| (code.to_string(), native_name(code).to_string()))
                .collect()
        } else {
            rom.iter()
                .map(|(code, label)| {
                    let name = native_name(code);
                    (code.clone(), if name == code { label.clone() } else { name.to_string() })
                })
                .collect()
        };
        if self.texts.language() == texts::PSEUDO {
            choices.push((texts::PSEUDO.to_string(), native_name(texts::PSEUDO).to_string()));
        }
        choices
    }

    /// Follows the ROM in use with the window's language.
    fn refresh_texts(&mut self) {
        let rom: Vec<String> = match self.pending_rom() {
            Some(rom) if !rom.languages.is_empty() => rom.languages.iter().map(|(code, _)| code.clone()).collect(),
            _ => self.languages.iter().map(|(code, _)| code.clone()).collect(),
        };
        let preferred: Vec<&str> =
            [self.ui_language_choice.as_deref(), self.system_language.as_deref()].into_iter().flatten().collect();
        let code = texts::choose_language(&rom, &preferred);
        if code != self.texts.language() {
            self.texts = Texts::new(&code);
            if !self.busy {
                self.refresh_hd();
                self.refresh_mod();
            }
        }
    }

    /// The layout probe's fixed card for `source`, without recognition.
    pub(crate) fn probe_card(&mut self, source: &str, card: Card) {
        self.rom = source.to_string();
        self.card = Some((source.to_string(), card));
        self.card_installed = false;
    }

    /// The layout probe's installed languages.
    pub(crate) fn probe_installed_languages(&mut self, languages: Vec<(String, String)>, selected: Option<usize>) {
        (self.languages, self.language) = (languages, selected);
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
        self.card = Some((source.clone(), Card::Recognizing(self.t(Msg::Recognizing))));
        let request = recognize_request(Path::new(&source), &self.installation.data_root, &self.installation.root);
        let own = self.card_cancel.clone();
        let texts = self.texts.clone();
        self.spawn(move |_, notify, cancel| run_recognition(source, request, texts, notify, cancel, own));
    }

    fn spawn(&mut self, work: impl FnOnce(Arc<Optional>, Notifier, CancelToken) + Send + 'static) {
        let (optional, notify, cancel) = (Arc::clone(&self.optional), self.notifier.clone(), self.cancel.clone());
        self.workers.retain(|worker| !worker.is_finished());
        self.workers.push(std::thread::spawn(move || work(optional, notify, cancel)));
    }

    fn dialog(&mut self, kind: DialogKind, message: String) {
        if let Some(probe) = &mut self.probe {
            // Unattended: record the notification instead of waiting for a click.
            probe.record(kind, &message);
            return;
        }
        self.push_dialog(kind, message);
    }

    pub(crate) fn push_dialog(&mut self, kind: DialogKind, message: String) {
        if self.dialogs.iter().any(|dialog| dialog.kind == kind && dialog.message == message) {
            return;
        }
        let title = self.t(Msg::DialogTitle);
        self.dialogs.push_back(Dialog { kind, title, message, focus_pending: true });
    }

    /// Reads the installed title and inspects the installation (off the
    /// window thread: inspection hashes the runtime).
    pub(crate) fn refresh_installation(&mut self) {
        self.active_title = load_active_title(&self.installation.active_title_state());
        self.refresh_languages();
        if self.freeze_state {
            return;
        }
        self.state = None;
        let installation = self.installation.clone();
        self.spawn(move |_, notify, _| notify.send(Event::State(Box::new(installation.state()))));
    }

    fn refresh_languages(&mut self) {
        match read_languages(&self.installation.data_root) {
            Ok((languages, selected)) => {
                (self.languages, self.language) = (languages, selected);
                self.language_error = None;
            }
            Err(error) => {
                (self.languages, self.language) = (Vec::new(), None);
                self.language_error = Some(error);
            }
        }
        self.refresh_texts();
    }

    pub(crate) fn refresh_hd(&mut self) {
        let contract = self.optional.hd_contract.as_ref();
        self.hd_status = enhancements::hd_status(&self.texts, contract, &self.hd_state, self.hd_auto_download);
    }

    fn refresh_mod(&mut self) {
        self.mod_status = enhancements::mod_status(
            &self.texts,
            self.mod_settings_error.as_deref(),
            self.optional.mod_contract.as_ref(),
            &self.mod_state,
            self.mod_enabled,
        );
    }

    pub(crate) fn home_hd_note(&self) -> Option<String> {
        let contract = self.optional.hd_contract.as_ref();
        enhancements::home_hd_note(&self.texts, contract, &self.hd_state, self.hd_auto_download)
    }

    /// The Home card for the current state.
    pub(crate) fn home_card(&self) -> HomeCard {
        let preparing = self.preparing.map(|started| Preparing {
            stage: &self.stage,
            elapsed: started.elapsed(),
            progress: self.progress,
        });
        let recipe = match &self.state {
            Some(InstallationState::Ready { recipe, .. }) => Some(recipe.as_str()),
            _ => None,
        };
        let revision = recipe.map(|recipe| {
            self.revisions.iter().find(|(id, _)| id == recipe).map_or(recipe, |(_, label)| label.as_str())
        });
        let language = self.language.and_then(|index| self.languages.get(index)).map(|(_, label)| label.as_str());
        home::card(&HomeInput {
            texts: &self.texts,
            state: self.state.as_ref(),
            failure: self.failure.as_ref(),
            preparing,
            recognized: self.pending_rom().is_some(),
            idle: self.idle(),
            revision,
            language,
            retry_in_place: self.maintenance.last.is_some(),
        })
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
                self.stage = message;
                self.progress = None;
            }
            Event::Progress { done, total } => self.progress = Some((done, total)),
            Event::HdStage(text) => self.hd_status = text,
            Event::ModStage(text) => self.mod_status = text,
            Event::HdFailed(text) => {
                self.hd_failed = true;
                self.hd_status = text;
            }
            Event::ModFailed(text) => {
                self.mod_failed = true;
                self.mod_status = text;
            }
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
                    let page = &self.optional.pack.page;
                    let message = self.t(Msg::ManualHdFailure { error: &error, archive: &contract.name, page });
                    self.dialog(DialogKind::Error, message);
                } else if self.active_title.is_some() {
                    self.hd_status = self.t(Msg::HdReadyReinstall);
                }
            }
            Event::ModDone { error, state } => {
                self.busy = false;
                self.mod_state = Some(state);
                self.refresh_mod();
                if let (Some(error), Some(contract)) = (error, self.optional.mod_contract.as_ref()) {
                    let page = &self.optional.mod_.page;
                    let message = self.t(Msg::ManualModFailure { error: &error, archive: &contract.name, page });
                    self.dialog(DialogKind::Error, message);
                } else if self.active_title.is_some() && self.mod_enabled {
                    self.mod_status = self.t(Msg::ModReadyReinstall { title: &self.optional.mod_.title });
                }
            }
            Event::InstallFailed(explanation) => {
                self.finish_preparation();
                if let Some(probe) = &mut self.probe {
                    probe.record(DialogKind::Error, &self.texts.body(&explanation));
                }
                self.failure = Some(*explanation);
                self.show_details = false;
            }
            Event::InstallComplete { hd, mod_, notices } => {
                self.finish_preparation();
                self.card_installed = true;
                (self.hd_state, self.mod_state) = (Some(hd), Some(mod_));
                if !self.hd_failed {
                    self.refresh_hd();
                }
                if !self.mod_failed {
                    self.refresh_mod();
                }
                let mut message = self.t(Msg::ReadyDialog);
                for notice in &notices {
                    let (title, next) = (self.texts.title(notice), self.texts.next_action(notice));
                    message.push_str(&format!("\n\n{title}\n{next}\n{}", notice.details));
                }
                self.notices = notices;
                self.dialog(DialogKind::Info, message);
            }
            Event::InstallCancelled => {
                self.finish_preparation();
                self.status_note = Some(self.t(Msg::StatusCancelled));
            }
            Event::MaintenanceDone { mut message, notices } => {
                self.finish_preparation();
                self.maintenance.last = None;
                for notice in &notices {
                    let (title, next) = (self.texts.title(notice), self.texts.next_action(notice));
                    message.push_str(&format!("\n\n{title}\n{next}\n{}", notice.details));
                }
                self.notices = notices;
                self.dialog(DialogKind::Info, message);
            }
            Event::MaintenanceQuestion(question) => {
                self.finish_preparation();
                crate::maintenance::ask(self, question);
            }
            Event::Recognition { source, card } => {
                if self.card.as_ref().is_some_and(|(shown, _)| *shown == source) {
                    self.card = Some((source, card));
                    self.refresh_texts();
                }
            }
            Event::State(state) => {
                if !self.freeze_state {
                    self.state = Some(*state);
                }
            }
            Event::GameExited(explanation) => {
                self.game_running = false;
                if let Some(explanation) = explanation {
                    // The card shows the exit code and the end of the logs at once.
                    self.failure = Some(*explanation);
                    self.show_details = true;
                    self.page = Page::Home;
                }
                self.refresh_diagnostics();
            }
            Event::Diagnostics(view) => self.diagnostics = Some(view),
            Event::BundleSaved(Ok(path)) => {
                self.status_note = Some(self.t(Msg::BundleSaved { path: &path.display().to_string() }));
            }
            Event::BundleSaved(Err(error)) => {
                let message = self.t(Msg::BundleFailed { error: &error });
                self.dialog(DialogKind::Error, message);
            }
        }
    }

    /// Collects the Diagnostics page's facts on a task thread (it reads
    /// logs and hashes the installed runtime).
    pub(crate) fn refresh_diagnostics(&mut self) {
        if self.freeze_state {
            return;
        }
        let installation = self.installation.clone();
        self.spawn(move |_, notify, _| {
            let view = pages::diagnostics::DiagnosticsView::collect(&installation).map(Box::new);
            notify.send(Event::Diagnostics(view));
        });
    }

    /// Asks where to save the support bundle, then collects and writes it on
    /// the dialog's thread.
    fn save_bundle(&mut self, frame: Option<&eframe::Frame>) {
        let name = forge_core::diagnostics::bundle_file_name(std::time::SystemTime::now());
        let dialog = rfd::FileDialog::new()
            .set_title(self.t(Msg::SaveBundleTitle))
            .add_filter(self.t(Msg::FilterZip), &["zip"])
            .set_file_name(name);
        let dialog = match frame {
            Some(frame) => dialog.set_parent(frame),
            None => dialog,
        };
        let installation = self.installation.clone();
        let notify = self.notifier.clone();
        std::thread::spawn(move || {
            let Some(path) = dialog.save_file() else { return };
            notify.send(Event::BundleSaved(pages::diagnostics::save_bundle(&installation, &path)));
        });
    }

    /// The end of this window's preparation, whatever its outcome.
    pub(crate) fn finish_preparation(&mut self) {
        self.busy = false;
        self.preparing = None;
        self.progress = None;
        self.dialogs.retain(|dialog| dialog.kind != DialogKind::ConfirmCancel);
        self.refresh_installation();
    }

    fn selected(&mut self, target: PickTarget, path: PathBuf) {
        match target {
            PickTarget::Rom | PickTarget::Extracted => {
                // A picked file is recognized again, also when it is the same path.
                self.rom = path.to_string_lossy().into_owned();
                self.card = None;
                self.failure = None;
                self.recognize();
            }
            PickTarget::HdArchive => {
                let message = self.t(Msg::VerifyingArchive);
                self.run_hd(&message, move |optional, _, cancel| optional.import_hd_archive(&path, cancel))
            }
            PickTarget::HdFolder => {
                let message = self.t(Msg::VerifyingTextures);
                self.run_hd(&message, move |optional, _, cancel| optional.import_hd_folder(&path, cancel))
            }
            PickTarget::ModArchive => {
                let message = self.t(Msg::VerifyingArchive);
                self.run_mod(&message, move |optional, _, cancel| optional.import_mod_archive(&path, cancel))
            }
            PickTarget::ModFolder => {
                let message = self.t(Msg::VerifyingFiles);
                self.run_mod(&message, move |optional, _, cancel| optional.import_mod_folder(&path, cancel))
            }
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

    pub(crate) fn apply(&mut self, action: Action, context: &egui::Context, frame: Option<&eframe::Frame>) {
        if let Some(log) = &mut self.action_log {
            log.push(action.name());
            return;
        }
        self.copied &= matches!(action, Action::Navigate(Page::Diagnostics));
        match action {
            Action::Pick(target) => self.pick(target, frame),
            Action::Install => self.start_install(),
            Action::Launch => self.launch(),
            Action::Cancel => {
                if self.preparing.is_some() {
                    self.push_dialog(DialogKind::ConfirmCancel, self.t(Msg::ConfirmCancel));
                }
            }
            Action::DownloadHd => {
                let message = self.t(Msg::DownloadingHd);
                self.run_hd(&message, |optional, report, cancel| optional.download_hd(report, cancel));
            }
            Action::DownloadMod => {
                let message = self.t(Msg::DownloadingMod { title: &self.optional.mod_.title });
                self.run_mod(&message, |optional, report, cancel| optional.download_mod(report, cancel));
            }
            Action::ToggleMod => self.toggle_mod(),
            Action::SelectLanguage(index) => self.choose_language(index),
            Action::Recognize => self.recognize(),
            Action::Navigate(page) => {
                self.page = page;
                if page == Page::Diagnostics {
                    self.refresh_diagnostics();
                }
            }
            Action::CheckInstallation => {
                if self.idle() {
                    self.failure = None;
                    self.status_note = None;
                    self.refresh_installation();
                }
            }
            Action::SaveBundle => self.save_bundle(frame),
            Action::CopyDiagnostics => {
                let collected = match &self.diagnostics {
                    Some(Ok(view)) => Some(view.text.as_str()),
                    _ => None,
                };
                let report =
                    pages::diagnostics::report(self.state.as_ref(), self.failure.as_ref(), &self.notices, collected);
                context.copy_text(report);
                self.copied = true;
                self.status_note = Some(self.t(Msg::Copied));
            }
            Action::OpenFolder(path) => {
                // The folder exists once Forge wrote anything; create it so
                // the file manager does not open somewhere else.
                let _ = std::fs::create_dir_all(&path);
                if let Err(error) = pages::data::open_folder(&path) {
                    self.dialog(DialogKind::Error, format!("{}\n{error}", path.display()));
                }
            }
            Action::Dismiss => {
                self.failure = None;
                self.notices.clear();
            }
            Action::ToggleDetails => self.show_details = !self.show_details,
            Action::ToggleNeeds => self.show_needs = !self.show_needs,
            Action::TogglePrepareAgain => self.show_prepare_again = !self.show_prepare_again,
            Action::Data(action) => pages::data::apply(self, action, frame),
            Action::SetUiLanguage(code) => {
                // Kept for the next sessions; decision 8 still applies to
                // each ROM (refresh_texts skips a language it lacks).
                let saved = texts::preference::save(&self.installation.data_root, &code);
                self.ui_language_choice = Some(code);
                self.refresh_texts();
                if let Err(error) = saved {
                    self.status_note = Some(self.t(Msg::ForgeLanguageNotSaved { error: &error }));
                }
            }
            Action::Command(command) => {
                use crate::maintenance::Kind;
                let action = match command {
                    // Update and Repair in place (UX plan WP-04A); Retry repeats the failed one.
                    Command::Update => return crate::maintenance::begin(self, Kind::Update),
                    Command::Repair => return crate::maintenance::begin(self, Kind::Repair),
                    Command::UpdateFromZip => return crate::maintenance::begin_zip(self),
                    Command::Retry if self.maintenance.last.is_some() => {
                        let last = self.maintenance.last.expect("checked");
                        return crate::maintenance::begin(self, last);
                    }
                    Command::Prepare | Command::RepairFromRom | Command::Retry => Action::Install,
                    Command::Play => Action::Launch,
                    Command::Cancel => Action::Cancel,
                    Command::CheckInstallation => Action::CheckInstallation,
                    Command::ChooseRom => Action::Pick(PickTarget::Rom),
                    Command::CopyDiagnostics => Action::CopyDiagnostics,
                    Command::ViewDiagnostics => Action::Navigate(Page::Diagnostics),
                    Command::SupportedRevisions => Action::Navigate(Page::About),
                    Command::DataPage => Action::Navigate(Page::Data),
                    Command::Continue => Action::Dismiss,
                };
                self.apply(action, context, frame);
            }
        }
    }

    fn launch(&mut self) {
        if !self.idle() {
            return;
        }
        let sources = crate::launch::diagnostics_sources(&self.installation);
        match crate::launch::launch_runtime_capturing(&self.installation, &sources) {
            Ok(mut child) => {
                // The game outlives nothing of the window's: it is watched on
                // its own thread, never joined when the window closes.
                self.game_running = true;
                let notify = self.notifier.clone();
                let started = std::time::SystemTime::now();
                std::thread::spawn(move || {
                    let explanation = crate::launch::finish_game(&sources, &mut child, started);
                    notify.send(Event::GameExited(explanation.map(Box::new)));
                });
            }
            Err(error) => {
                let error = error.during(Operation::Launch);
                forge_core::log::ForgeLog::new(&self.installation.data_root, "launch").error(&error);
                self.failure = Some(forge_core::explain::explain(&error));
                self.show_details = false;
                self.page = Page::Home;
            }
        }
    }

    fn pick(&mut self, target: PickTarget, frame: Option<&eframe::Frame>) {
        if !self.idle() {
            return;
        }
        let optional = &self.optional;
        let texts = &self.texts;
        let archive = |contract: Option<&forge_mods::ArchiveContract>, fallback: Msg| match contract {
            Some(contract) => texts.get(Msg::PickArchive { name: &contract.name }),
            None => texts.get(fallback),
        };
        let dialog = rfd::FileDialog::new();
        let (dialog, folder) = match target {
            PickTarget::Rom => (
                dialog
                    .set_title(texts.get(Msg::PickRomTitle))
                    .add_filter(texts.get(Msg::FilterRom), &["3ds", "cci"])
                    .add_filter(texts.get(Msg::Filter3ds), &["3ds"])
                    .add_filter(texts.get(Msg::FilterCci), &["cci"]),
                false,
            ),
            PickTarget::Extracted => (dialog.set_title(texts.get(Msg::PickExtractedTitle)), true),
            PickTarget::HdArchive => (
                dialog
                    .set_title(archive(optional.hd_contract.as_ref(), Msg::PickArchiveFallbackHd))
                    .add_filter(texts.get(Msg::FilterTexturePack), &["zip"]),
                false,
            ),
            PickTarget::HdFolder => {
                (dialog.set_title(texts.get(Msg::PickFolder { title: &optional.pack.title })), true)
            }
            PickTarget::ModArchive => (
                dialog
                    .set_title(archive(optional.mod_contract.as_ref(), Msg::PickArchiveFallbackMod))
                    .add_filter(texts.get(Msg::FilterModArchive), &["rar"]),
                false,
            ),
            PickTarget::ModFolder => {
                (dialog.set_title(texts.get(Msg::PickFolder { title: &optional.mod_.title })), true)
            }
        };
        let dialog = match frame {
            Some(frame) => dialog.set_parent(frame),
            None => dialog,
        };
        self.picking = true;
        let notify = self.notifier.clone();
        // The dialog runs on its own thread so the window keeps repainting.
        std::thread::spawn(move || {
            let path = if folder { dialog.pick_folder() } else { dialog.pick_file() };
            notify.send(Event::Selection(target, path));
        });
    }

    pub(crate) fn install_plan(&self) -> InstallPlan {
        InstallPlan {
            source: PathBuf::from(self.rom.trim()),
            data_root: self.installation.data_root.clone(),
            package_root: self.installation.root.clone(),
            hd_auto_download: self.hd_auto_download,
            mod_enabled: self.mod_enabled,
            game_language: self.pending_rom().and_then(RecognizedRom::chosen).map(str::to_string),
            texts: self.texts.clone(),
        }
    }

    /// Prepare (also Repair from a ROM and Retry of a preparation): the
    /// recognized ROM in the field is prepared and installed.
    pub(crate) fn start_install(&mut self) {
        if !self.can_install() {
            return;
        }
        let plan = self.install_plan();
        let missing = |contract: Option<&forge_mods::ArchiveContract>, state: &Checked| {
            contract.is_some() && *state == Some(Ok(Status::Missing))
        };
        let stage = self.t(if self.hd_auto_download && missing(self.optional.hd_contract.as_ref(), &self.hd_state) {
            Msg::DownloadingHdFirst
        } else if self.mod_enabled && missing(self.optional.mod_contract.as_ref(), &self.mod_state) {
            Msg::DownloadingModFirst
        } else {
            Msg::Starting
        });
        self.maintenance.last = None;
        self.start_task(stage, move |optional, notify, cancel| run_install(optional, plan, notify, cancel));
    }

    /// Starts a preparation (install, update, repair): Home shows its stage
    /// and Cancel until the task's terminal event.
    pub(crate) fn start_task(
        &mut self,
        stage: String,
        work: impl FnOnce(Arc<Optional>, Notifier, CancelToken) + Send + 'static,
    ) {
        self.stage = stage;
        self.busy = true;
        self.preparing = Some(Instant::now());
        self.progress = None;
        self.failure = None;
        self.notices.clear();
        self.status_note = None;
        (self.hd_failed, self.mod_failed) = (false, false);
        self.page = Page::Home;
        self.install_cancel = CancelToken::new();
        let (optional, notify, cancel) =
            (Arc::clone(&self.optional), self.notifier.clone(), self.install_cancel.clone());
        self.workers.retain(|worker| !worker.is_finished());
        self.workers.push(std::thread::spawn(move || work(optional, notify, cancel)));
    }

    fn toggle_mod(&mut self) {
        match self.optional.set_enabled(self.mod_enabled) {
            Ok(()) => self.mod_settings_error = None,
            Err(error) => {
                let message = self.t(Msg::ModSettingSaveFailed { error: &error.message });
                self.dialog(DialogKind::Error, message);
                self.mod_settings_error = Some(error.message);
            }
        }
        self.refresh_mod();
        if self.active_title.is_some() && self.mod_settings_error.is_none() {
            self.mod_status.push_str(&self.t(Msg::ApplyModChange));
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
                    self.status_note = Some(self.texts.get(Msg::LanguageChosen { label }));
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
                self.status_note = Some(self.t(Msg::LanguageSaved));
            }
            Err(error) => {
                self.dialog(DialogKind::Error, error.message);
                self.refresh_languages();
            }
        }
    }

    /// Cancels the tasks and waits for them a bounded time; an import or
    /// download stops at its next cancellation check.
    pub(crate) fn shut_down(&mut self) {
        self.cancel.cancel();
        self.card_cancel.cancel();
        self.install_cancel.cancel();
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
            self.push_dialog(DialogKind::ConfirmClose, self.t(Msg::CloseWhileBusy));
        } else {
            self.shut_down();
            self.closing = true;
        }
    }

    fn show_dialog(&mut self, context: &egui::Context) {
        let (yes, no, ok) = (self.t(Msg::ButtonYes), self.t(Msg::ButtonNo), self.t(Msg::ButtonOk));
        let Some(dialog) = self.dialogs.front_mut() else { return };
        let mut answer = None;
        let width = (context.content_rect().width() - 48.0).clamp(200.0, 460.0);
        let question = matches!(dialog.kind, DialogKind::ConfirmClose | DialogKind::ConfirmCancel);
        let modal = egui::Modal::new(egui::Id::new("forge-dialog")).show(context, |ui| {
            ui.set_width(width);
            ui.heading(&dialog.title);
            ui.add_space(6.0);
            let height = (context.content_rect().height() * 0.6).max(80.0);
            egui::ScrollArea::vertical().max_height(height).show(ui, |ui| {
                ui.add(Label::new(&dialog.message).wrap());
            });
            ui.add_space(10.0);
            ui.horizontal_wrapped(|ui| {
                let primary = if question {
                    let yes = ui.button(&yes);
                    if ui.button(&no).clicked() {
                        answer = Some(false);
                    }
                    yes
                } else {
                    ui.button(&ok)
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
        // Esc or a click outside dismisses; for a question that is "No".
        if answer.is_none() && modal.should_close() {
            answer = Some(false);
        }
        if let Some(confirmed) = answer {
            let kind = self.dialogs.pop_front().map(|dialog| dialog.kind);
            match (kind, confirmed) {
                (Some(DialogKind::ConfirmClose), true) => self.close(context),
                (Some(DialogKind::ConfirmCancel), true) => self.install_cancel.cancel(),
                _ => {}
            }
        }
    }

    fn page_body(&mut self, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>, card: &HomeCard) {
        match self.page {
            Page::Home => home::show(self, ui, record, actions, card),
            Page::Enhancements => enhancements::show(self, ui, record, actions),
            Page::Data => pages::data::show(self, ui, record, actions),
            Page::Diagnostics => pages::diagnostics::show(self, ui, record, actions),
            Page::About => pages::about::show(self, ui, record, actions),
        }
    }

    /// The footer: one status line, the progress bar, the primary and
    /// secondary actions (stacked on a narrow window).
    fn footer(&mut self, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>, card: &HomeCard) {
        let status = match (&self.status_note, self.preparing) {
            (Some(note), None) => note.clone(),
            _ => card.status.clone(),
        };
        record.add(ui, "status", "label", Label::new(status).truncate());
        ui.add_space(6.0);
        let fraction = match (self.busy, self.progress) {
            (true, Some((done, Some(total)))) if total > 0 => done as f32 / total as f32,
            // Indeterminate: a sweeping bar.
            (true, _) => (ui.input(|input| input.time) * 0.6).fract() as f32,
            (false, _) => 0.0,
        };
        let mut bar = ProgressBar::new(fraction).desired_height(8.0);
        if !self.busy {
            bar = bar.fill(egui::Color32::TRANSPARENT);
        }
        record.add(ui, "progress", "progress", bar);
        ui.add_space(8.0);
        let buttons: Vec<(CardButton, bool)> =
            std::iter::once((card.primary, true)).chain(card.secondary.map(|button| (button, false))).collect();
        let stacked = ui.available_width() < STACK_WIDTH;
        let mut draw = |ui: &mut egui::Ui, record: &mut Recorder, width: f32| {
            for (button, primary) in &buttons {
                let name = match (button.command, primary) {
                    (Command::Cancel, _) => "cancel",
                    (_, true) => "primary",
                    (_, false) => "secondary",
                };
                let mut text = RichText::new(self.t(button.command.label()));
                if *primary {
                    text = text.strong().size(16.0);
                }
                let mut widget = Button::new(text).min_size(Vec2::new(width, if *primary { 34.0 } else { 30.0 }));
                if *primary && button.enabled && button.command != Command::Cancel {
                    widget = widget.fill(ui.visuals().selection.bg_fill);
                }
                if record.button(ui, button.enabled, name, widget) {
                    actions.push(Action::Command(button.command));
                }
            }
        };
        if stacked {
            ui.vertical(|ui| {
                let width = ui.available_width();
                draw(ui, record, width)
            });
        } else {
            ui.horizontal(|ui| {
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| draw(ui, record, 120.0));
            });
        }
    }
}

impl eframe::App for ForgeApp {
    fn raw_input_hook(&mut self, _context: &egui::Context, raw_input: &mut egui::RawInput) {
        if let Some(probe) = &mut self.probe {
            probe.inject(raw_input);
        }
    }

    fn ui(&mut self, ui: &mut egui::Ui, frame: &mut eframe::Frame) {
        let context = ui.ctx().clone();
        self.drain_events();
        pages::data::poll(self, &context);
        self.handle_close_request(&context);
        self.geometry.update(&context, frame);
        if self.title_language != self.texts.language() {
            self.title_language = self.texts.language().to_string();
            context.send_viewport_cmd(egui::ViewportCommand::Title(self.t(Msg::WindowTitle)));
        }
        let card = self.home_card();
        let mut actions = Vec::new();
        let mut record = Recorder {
            scroll_to: self.scroll_to.take(),
            viewport_height: self.scroll_viewport.height(),
            ..Default::default()
        };
        let panel = egui::Frame::central_panel(ui.style()).inner_margin(0);
        egui::CentralPanel::default().frame(panel).show(ui, |ui| {
            let navigation = egui::Frame::new().inner_margin(Margin { left: 16, right: 16, top: 8, bottom: 2 });
            navigation.show(ui, |ui| {
                ui.set_width(ui.available_width());
                pages::navigation(ui, &self.texts, self.page, &mut record, &mut actions);
            });
            // The page scrolls; the footer below it always stays in view.
            let height = (ui.available_height() - self.footer_height).max(40.0);
            let mut area = egui::ScrollArea::vertical()
                .id_salt(("page", self.page.code()))
                .auto_shrink(false)
                .max_height(height)
                .min_scrolled_height(height);
            if std::mem::take(&mut self.scroll_top) {
                area = area.vertical_scroll_offset(0.0);
            }
            let output = area.show(ui, |ui| {
                egui::Frame::new().inner_margin(Margin::symmetric(16, 8)).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    record.scrolled = true;
                    self.page_body(ui, &mut record, &mut actions, &card);
                    record.scrolled = false;
                });
            });
            self.scroll_viewport = output.inner_rect;
            let footer = egui::Frame::side_top_panel(ui.style()).inner_margin(Margin::symmetric(16, 8));
            let line = ui.visuals().widgets.noninteractive.bg_stroke;
            ui.painter().hline(ui.max_rect().x_range(), ui.cursor().top() + ui.spacing().item_spacing.y / 2.0, line);
            let response = footer.show(ui, |ui| {
                ui.set_width(ui.available_width());
                self.footer(ui, &mut record, &mut actions, &card);
            });
            let measured = response.response.rect.height() + ui.spacing().item_spacing.y;
            if (measured - self.footer_height).abs() > 0.5 {
                self.footer_height = measured;
                context.request_discard("the footer changed height");
            }
        });
        self.show_dialog(&context);
        crate::maintenance::show(self, &context);
        self.widgets = record.widgets;
        for action in actions {
            self.apply(action, &context, Some(frame));
        }
        if self.busy || self.recognizing() || self.state.is_none() {
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
            .with_title(Texts::default().get(Msg::WindowTitle))
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
    startup_error: Option<ForgeError>,
    probe: Option<Box<Probe>>,
    simulated: SimulatedDisplay,
) -> Result<(), String> {
    let installation = match installation {
        Some(installation) => installation,
        None => Installation::current().map_err(|error| error.message)?,
    };
    let title = Texts::default().get(Msg::WindowTitle);
    eframe::run_native(
        &title,
        native_options(),
        Box::new(move |creation| {
            let app = ForgeApp::new(&creation.egui_ctx, installation, startup_error, probe, simulated)?;
            Ok(Box::new(app))
        }),
    )
    .map_err(|error| error.to_string())
}

/// `forge_gui.main`. A window that cannot open (no OpenGL, no display) is
/// reported with the command line alternative.
pub fn run(installation: Option<Installation>, startup_error: Option<ForgeError>) -> i32 {
    match run_window(installation, startup_error, None, SimulatedDisplay::default()) {
        Ok(()) => 0,
        Err(error) => {
            let texts = Texts::new(&texts::system_language().unwrap_or_default());
            let message = texts.get(Msg::GuiFailed { error: &error });
            eprintln!("{message}");
            let _ = rfd::MessageDialog::new()
                .set_level(rfd::MessageLevel::Error)
                .set_title(texts.get(Msg::DialogTitle))
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
        assert_eq!(languages[1], ("fr".to_string(), "Fran\u{e7}ais".to_string()), "the native name, not the runtime's");
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

    fn app(directory: &Path) -> ForgeApp {
        let installation = Installation::at(directory).unwrap();
        ForgeApp::new(&egui::Context::default(), installation, None, None, SimulatedDisplay::default()).unwrap()
    }

    #[test]
    fn the_recognized_rom_card_drives_the_installation_language() {
        let directory = tempfile::tempdir().unwrap();
        let mut app = app(directory.path());
        let data_root = app.installation.data_root.clone();
        assert_eq!(app.language_options(), (&[][..], None));

        app.probe_card("game.cci", Card::Recognizing(app.t(Msg::Recognizing)));
        assert!(app.recognizing() && !app.can_install());
        app.probe_card("game.cci", recognized(&["en", "it"]));
        assert!(app.can_install());
        app.choose_language(1);
        assert_eq!(app.language_options().1, Some(1));
        assert_eq!(app.install_plan().game_language.as_deref(), Some("it"));
        assert!(!forge_native::language::config_path(&data_root).exists(), "chosen, not saved");

        // A path being typed hides the card and its choice, and disables Prepare.
        app.rom = "other.cci".into();
        assert_eq!(app.current_card(), None);
        assert_eq!(app.install_plan().game_language, None);
        assert!(!app.can_install());
        app.rom = "game.cci".into();

        // A late event for another ROM changes nothing.
        let failure = Box::new(forge_core::explain::explain(&ForgeError::invalid("x")));
        app.handle(Event::Recognition { source: "old.cci".into(), card: Card::Failed(failure) });
        assert!(matches!(app.current_card(), Some(Card::Recognized(_))));

        // Installed: the buttons show and save the installed setting.
        let detected = serde_json::json!({"format": "triaevum_game_language_v1", "selected": "en", "available": [
            {"code": "en", "label": "English", "system_id": 1}, {"code": "it", "label": "Italiano", "system_id": 4}]});
        forge_native::language::install_preferring(&data_root, &detected, Some("it")).unwrap();
        app.handle(Event::InstallComplete { hd: Ok(Status::Missing), mod_: Ok(Status::Missing), notices: Vec::new() });
        assert_eq!(app.language_options().1, Some(1));
        assert_eq!(app.install_plan().game_language, None);
        app.choose_language(0);
        assert_eq!(read_languages(&data_root).unwrap().1, Some(0));
        assert_eq!(app.status_note.as_deref(), Some(app.t(Msg::LanguageSaved).as_str()));
        app.shut_down();
    }

    #[test]
    fn the_window_language_follows_the_rom_in_use() {
        let directory = tempfile::tempdir().unwrap();
        let mut app = app(directory.path());
        app.system_language = Some("it".into());
        app.refresh_texts();
        assert_eq!(app.texts.language(), "it", "a supported ROM language before recognition");
        app.probe_card("game.cci", recognized(&["en", "fr"]));
        app.refresh_texts();
        assert_eq!(app.texts.language(), "en", "Italian is not in this ROM");
        assert_eq!(app.ui_languages().iter().map(|(code, _)| code.as_str()).collect::<Vec<_>>(), ["en", "fr"]);
        app.apply(Action::SetUiLanguage("fr".into()), &egui::Context::default(), None);
        assert_eq!(app.texts.language(), "fr");
        let data_root = app.installation.data_root.clone();
        app.shut_down();
        // The choice is kept in Forge's own settings, not the game's.
        assert_eq!(texts::preference::load(&data_root).as_deref(), Some("fr"));
        assert!(!forge_native::language::config_path(&data_root).exists());
        let mut next = self::app(directory.path());
        next.system_language = Some("it".into());
        next.refresh_texts();
        assert_eq!(next.texts.language(), "fr", "the kept choice comes before the system language");
        next.probe_card("game.cci", recognized(&["en", "it"]));
        next.refresh_texts();
        assert_eq!(next.texts.language(), "it", "this ROM has no French: the system language");
        next.shut_down();
    }

    #[test]
    fn a_failed_preparation_is_a_card_not_a_status_line() {
        let directory = tempfile::tempdir().unwrap();
        let mut app = app(directory.path());
        app.probe_card("game.cci", recognized(&["en"]));
        app.busy = true;
        app.preparing = Some(Instant::now());
        let explanation = forge_core::explain::explain(&ForgeError::invalid("bad ROM"));
        app.handle(Event::InstallFailed(Box::new(explanation.clone())));
        assert!(!app.busy && app.preparing.is_none());
        assert_eq!(app.failure, Some(explanation));
        let card = app.home_card();
        assert_eq!(card.kind, home::Kind::Failed);
        assert_eq!(card.status, app.t(Msg::StatusActionNeeded));
        app.apply(Action::Dismiss, &egui::Context::default(), None);
        assert_eq!(app.failure, None);
        app.shut_down();
    }

    #[test]
    fn a_game_that_closed_with_an_error_is_a_card_with_its_log_tail() {
        let directory = tempfile::tempdir().unwrap();
        let mut app = app(directory.path());
        app.page = Page::Diagnostics;
        app.game_running = true;
        let details = "Exit code: 1 (the game stopped with an error)\n\nLast lines of oot3d_native_game.log:\nline 40";
        let explanation =
            forge_core::explain::explain_outcome(forge_core::explain::Outcome::GameExitedWithError, details);
        app.handle(Event::GameExited(Some(Box::new(explanation.clone()))));
        assert!(!app.game_running);
        assert_eq!(app.page, Page::Home);
        assert_eq!(app.failure, Some(explanation));
        assert!(app.show_details, "the log tail is shown at once");
        let card = app.home_card();
        assert_eq!(card.kind, home::Kind::Failed);
        app.apply(Action::Dismiss, &egui::Context::default(), None);
        // A clean exit shows nothing.
        app.page = Page::Data;
        app.handle(Event::GameExited(None));
        assert_eq!((app.page, app.failure.as_ref()), (Page::Data, None));
        app.shut_down();
    }

    #[test]
    fn cancelling_asks_first() {
        let directory = tempfile::tempdir().unwrap();
        let mut app = app(directory.path());
        let context = egui::Context::default();
        app.apply(Action::Cancel, &context, None);
        assert!(app.dialogs.is_empty(), "nothing to cancel");
        app.busy = true;
        app.preparing = Some(Instant::now());
        app.apply(Action::Command(Command::Cancel), &context, None);
        assert_eq!(app.dialogs.front().map(|dialog| dialog.kind), Some(DialogKind::ConfirmCancel));
        assert!(!app.install_cancel.is_cancelled());
        app.handle(Event::InstallCancelled);
        assert!(app.dialogs.is_empty() && !app.busy);
        assert_eq!(app.status_note.as_deref(), Some(app.t(Msg::StatusCancelled).as_str()));
        app.shut_down();
    }
}
