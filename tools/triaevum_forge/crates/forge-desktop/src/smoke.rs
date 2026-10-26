// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `--gui-smoke`: the real window, laid out by the real widgets, checked
//! unattended (`forge_gui_probe.py`). Pages may scroll vertically, never
//! horizontally; the primary action in the footer must be entirely inside
//! the window, and the window inside the work area.
//!
//! `--work-area WxH` is in logical points. `--pixels-per-point SCALE` (a
//! Rust addition, issue #111) simulates the operating system's display
//! scale, 1.5 for 150 %, on any monitor; with it, `--work-area` is the
//! scaled screen's logical work area (a 1920x1032 work area at 150 % is
//! 1280x688). `--screenshot PATH` (a Rust addition) writes the first screen
//! as a binary PPM; `--screenshots DIR` writes one `<screen>.ppm` per screen.
//! `--language CODE` starts in that Forge language (`pseudo` for the
//! pseudo-locale).
//!
//! Without `--rom`, the probe shows a fixed card for every installation
//! state and every page (UX plan WP-02), measures each, scrolls every
//! recorded widget into view and points at every interactive one, then
//! checks the keyboard: the Tab order, Enter on the focused action only,
//! and Esc never cancelling a preparation without the confirmation. With
//! `--rom`, the ROM's real recognition chooses its last language, the
//! preparation runs, and the installed window is measured.

use std::collections::VecDeque;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use eframe::egui::{self, Key, Modifiers, Rect, Vec2};
use forge_base::{ErrorKind, ForgeError, Operation};
use forge_core::state::{IncompatibleReason, InstallationState, PackageIdentity, RepairReason};
use serde_json::{json, Map, Value};

use crate::app::{DialogKind, ForgeApp, WidgetRecord};
use crate::pages::Page;
use crate::recognition::{heading, Card, RecognizedRom};
use crate::window::{parse_work_area, SimulatedDisplay, MIN_SIZE};

const USAGE: &str = "Usage: TriAevumForge --gui-smoke --output PATH [--rom PATH] [--timeout SEC] [--work-area WxH] \
     [--pixels-per-point SCALE] [--screenshot PATH] [--screenshots DIR] [--language CODE]";

#[derive(Clone, Debug, Default)]
pub struct ProbeOptions {
    pub output: PathBuf,
    pub rom: Option<PathBuf>,
    pub timeout: f64,
    pub display: SimulatedDisplay,
    pub screenshot: Option<PathBuf>,
    pub screenshots: Option<PathBuf>,
    pub language: Option<String>,
}

pub fn parse(arguments: &[String]) -> Result<ProbeOptions, String> {
    let mut options = ProbeOptions { timeout: 180.0, ..ProbeOptions::default() };
    let mut output = None;
    let mut iter = arguments.iter();
    while let Some(argument) = iter.next() {
        let mut value = || iter.next().cloned().ok_or_else(|| format!("{argument} needs a value"));
        match argument.as_str() {
            "--output" => output = Some(PathBuf::from(value()?)),
            "--rom" => options.rom = Some(PathBuf::from(value()?)),
            "--screenshot" => options.screenshot = Some(PathBuf::from(value()?)),
            "--screenshots" => options.screenshots = Some(PathBuf::from(value()?)),
            "--language" => options.language = Some(value()?),
            "--timeout" => {
                options.timeout = value()?.parse().map_err(|_| "--timeout needs a number of seconds".to_string())?
            }
            "--work-area" => {
                options.display.work_area =
                    Some(parse_work_area(&value()?).ok_or_else(|| "--work-area needs WIDTHxHEIGHT".to_string())?)
            }
            "--pixels-per-point" => {
                let scale = value()?.parse().ok().filter(|scale: &f32| (0.5..=4.0).contains(scale));
                options.display.pixels_per_point =
                    Some(scale.ok_or_else(|| "--pixels-per-point needs a scale from 0.5 to 4".to_string())?)
            }
            other => return Err(format!("Unknown option: {other}")),
        }
    }
    options.output = output.ok_or_else(|| "--output is required".to_string())?;
    Ok(options)
}

pub fn main(arguments: &[String]) -> i32 {
    let options = match parse(arguments) {
        Ok(options) => options,
        Err(error) => {
            eprintln!("{error}\n{USAGE}");
            return 2;
        }
    };
    let result = Arc::new(Mutex::new(None));
    let probe = Box::new(Probe::new(options.clone(), Arc::clone(&result)));
    let outcome = crate::app::run_window(None, None, Some(probe), options.display);
    let recorded = result.lock().unwrap_or_else(|poison| poison.into_inner()).take();
    let report = recorded.unwrap_or_else(|| {
        let error = outcome.err().unwrap_or_else(|| "The window closed before the probe finished".to_string());
        json!({"status": "failed", "error": error})
    });
    if let Some(parent) = options.output.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    if let Err(error) = std::fs::write(&options.output, forge_base::legacy_json::pretty_file_bytes(&report)) {
        eprintln!("Cannot write {}: {error}", options.output.display());
        return 1;
    }
    if report["status"] == "passed" {
        0
    } else {
        1
    }
}

fn bounds(rect: Rect) -> Value {
    json!([rect.min.x, rect.min.y, rect.width(), rect.height()])
}

fn record_json(widget: &WidgetRecord) -> Value {
    json!({"name": widget.name, "class": widget.class, "bounds": bounds(widget.rect),
           "enabled": widget.enabled, "scrolled": widget.scrolled})
}

/// The footer's main action: `primary`, or `cancel` while preparing.
fn is_primary(widget: &WidgetRecord) -> bool {
    !widget.scrolled && matches!(widget.name, "primary" | "cancel")
}

/// The layout verdict for one frame: clipped and collapsed widgets, whether
/// the primary action is fully visible, whether the window fits.
pub fn layout_report(widgets: &[WidgetRecord], viewport: Rect, outer: Vec2, work_area: Option<Vec2>) -> (bool, Value) {
    const SLACK: f32 = 0.5;
    let horizontal = |rect: Rect| rect.left() < viewport.left() - SLACK || rect.right() > viewport.right() + SLACK;
    let vertical = |rect: Rect| rect.top() < viewport.top() - SLACK || rect.bottom() > viewport.bottom() + SLACK;
    let clipped: Vec<Value> = widgets
        .iter()
        .filter(|widget| horizontal(widget.rect) || (!widget.scrolled && vertical(widget.rect)))
        .map(record_json)
        .collect();
    let unmapped: Vec<Value> = widgets.iter().filter(|widget| !widget.rect.is_positive()).map(record_json).collect();
    let primary_visible = widgets.iter().any(|widget| {
        is_primary(widget) && widget.rect.is_positive() && viewport.expand(SLACK).contains_rect(widget.rect)
    });
    let fits = work_area.is_none_or(|area| outer.x <= area.x + SLACK && outer.y <= area.y + SLACK);
    let scrolled_bottom = widgets.iter().filter(|w| w.scrolled).map(|w| w.rect.bottom()).fold(f32::MIN, f32::max);
    let footer_top = widgets.iter().filter(|w| is_primary(w)).map(|w| w.rect.top()).fold(f32::MAX, f32::min);
    let passed = !widgets.is_empty() && clipped.is_empty() && unmapped.is_empty() && primary_visible && fits;
    let details = json!({
        "fits_work_area": fits,
        "primary_visible": primary_visible,
        "options_scroll": scrolled_bottom > footer_top,
        "viewport": bounds(viewport),
        "outer_size": [outer.x, outer.y],
        "clipped_widgets": clipped,
        "unmapped_widgets": unmapped,
    });
    (passed, details)
}

/// The screens the probe shows: every Home state and every other page.
pub const SCREENS: &[&str] = &[
    "not_installed",
    "checking",
    "preparing",
    "preparing_elsewhere",
    "ready",
    "update_available",
    "repair_needed",
    "needs_game_input",
    "incompatible",
    "failed",
    "enhancements",
    "data",
    "diagnostics",
    "about",
];

fn identity(version: &str) -> PackageIdentity {
    PackageIdentity { runtime_sha256: "0".repeat(64), version: Some(version.to_string()) }
}

/// Shows `screen` with synthetic data (no proprietary input).
fn apply_screen(app: &mut ForgeApp, screen: &str) {
    app.freeze_state = true;
    app.scroll_top = true;
    (app.failure, app.preparing, app.progress, app.busy, app.page) = (None, None, None, false, Page::Home);
    (app.show_details, app.show_needs, app.show_prepare_again) = (false, false, false);
    app.notices.clear();
    app.status_note = None;
    app.probe_installed_languages(Vec::new(), None);
    app.probe_card("probe.cci", sample_card());
    if app.revisions.is_empty() {
        let label = "The Legend of Zelda: Ocarina of Time 3D (EUR (No-Intro original and Rev 1))";
        app.revisions.push(("oot3d-eur-catalogue-a584b067".into(), label.into()));
    }
    let recipe = app.revisions[0].0.clone();
    app.state = Some(match screen {
        "checking" => {
            app.state = None;
            return;
        }
        "preparing" => {
            app.busy = true;
            app.preparing = Instant::now().checked_sub(Duration::from_secs(83));
            app.stage = "Preparing GPU pipelines: 120/400".into();
            app.progress = Some((120, Some(400)));
            InstallationState::NotInstalled
        }
        "preparing_elsewhere" => InstallationState::Preparing,
        "ready" => {
            app.rom.clear();
            app.probe_installed_languages(sample_languages(), Some(1));
            InstallationState::Ready { recipe, package_version: Some("0.8.0".into()), language: Some("de".into()) }
        }
        "update_available" => InstallationState::UpdateAvailable {
            recipe,
            installed_package_identity: identity("0.8.0-beta.1.2"),
            present_package_identity: identity("0.8.0-beta.1.3"),
        },
        "repair_needed" => InstallationState::RepairNeeded {
            reason: RepairReason::ConfigUnreadable { file: "config/topscreen_ui.json".into() },
            details: "Expecting ',' delimiter: line 12 column 5 (char 311)".into(),
        },
        "needs_game_input" => InstallationState::NeedsGameInput {
            recipe,
            details: "The romfs data kept from your ROM is missing or changed".into(),
        },
        "incompatible" => InstallationState::Incompatible {
            reason: IncompatibleReason::MissingTitleModule,
            details: format!("The package has no precompiled module for the installed revision {recipe}"),
        },
        "failed" => {
            let details = [
                "The step named below did not finish.",
                "Stage: Preparing graphics (shaders)",
                "C:\\Games\\TriAevum\\data\\titles\\0004000000033500\\modules\\0123456789abcdef0123456789abcdef.tam",
            ]
            .join("\n");
            let error = ForgeError::new(ErrorKind::PreparationFailed, details).during(Operation::NativeHelper);
            app.failure = Some(forge_core::explain::explain(&error));
            InstallationState::NotInstalled
        }
        page => {
            app.page = Page::ALL.into_iter().find(|candidate| candidate.code() == page).unwrap_or(Page::Home);
            InstallationState::NotInstalled
        }
    });
}

/// What the probe does next; `Wait` lets the layout settle.
#[derive(Clone, Debug)]
enum Step {
    Wait(u32),
    Apply(&'static str),
    Screenshot(PathBuf),
    AwaitScreenshot(PathBuf),
    Expand,
    Measure(&'static str),
    ScrollCheck(&'static str),
    ScrollTo { screen: &'static str, index: usize, retry: bool },
    Visible { screen: &'static str, index: usize, retry: bool },
    Hovered { screen: &'static str, index: usize },
    TrackInput(u8),
    Keyboard(KeyStep),
    Finish,
}

#[derive(Clone, Copy, Debug)]
enum KeyStep {
    Start,
    Tab,
    ShiftTabCheck,
    Enter(&'static str),
    EnterCheck(&'static str),
    EscapeWhilePreparing,
    EscapeCheck,
    CancelFocus,
    CancelEnter,
    CancelCheck,
    DialogEscape,
    DialogEscapeCheck,
}

pub struct Probe {
    options: ProbeOptions,
    result: Arc<Mutex<Option<Value>>>,
    started: Instant,
    settled: bool,
    stable_frames: u32,
    last_size: Vec2,
    recognizing: bool,
    installing: bool,
    steps: VecDeque<Step>,
    wait: u32,
    pending: Vec<egui::Event>,
    notifications: Vec<Value>,
    report: Map<String, Value>,
    screens: Map<String, Value>,
    clipped: Vec<Value>,
    unmapped: Vec<Value>,
    scroll_failures: Vec<Value>,
    scroll_checked: usize,
    tab_order: Vec<&'static str>,
    tab_presses: usize,
    keyboard: Map<String, Value>,
    passed: bool,
    frames: u64,
    done: bool,
}

impl Probe {
    pub fn new(options: ProbeOptions, result: Arc<Mutex<Option<Value>>>) -> Self {
        Self {
            options,
            result,
            started: Instant::now(),
            settled: false,
            stable_frames: 0,
            last_size: Vec2::ZERO,
            recognizing: false,
            installing: false,
            steps: VecDeque::new(),
            wait: 0,
            pending: Vec::new(),
            notifications: Vec::new(),
            report: Map::new(),
            screens: Map::new(),
            clipped: Vec::new(),
            unmapped: Vec::new(),
            scroll_failures: Vec::new(),
            scroll_checked: 0,
            tab_order: Vec::new(),
            tab_presses: 0,
            keyboard: Map::new(),
            passed: true,
            frames: 0,
            done: false,
        }
    }

    pub fn language(&self) -> Option<String> {
        self.options.language.clone()
    }

    pub fn record(&mut self, kind: DialogKind, message: &str) {
        let kind = if kind == DialogKind::Info { "info" } else { "error" };
        self.notifications.push(json!({"kind": kind, "message": message}));
    }

    /// The events queued for the next frame (the keyboard and pointer checks).
    pub fn inject(&mut self, raw_input: &mut egui::RawInput) {
        // As if the user worked in the window, whatever window the desktop focused.
        raw_input.focused = true;
        raw_input.events.append(&mut self.pending);
    }

    fn key(&mut self, key: Key, modifiers: Modifiers) {
        for pressed in [true, false] {
            self.pending.push(egui::Event::Key { key, physical_key: None, pressed, repeat: false, modifiers });
        }
    }

    fn has(&self, kind: &str) -> bool {
        self.notifications.iter().any(|item| item["kind"] == kind)
    }

    fn widget<'a>(app: &'a ForgeApp, name: &str) -> Option<&'a WidgetRecord> {
        app.widgets.iter().find(|widget| widget.name == name)
    }

    fn check(&mut self, key: &str, value: bool) {
        self.keyboard.insert(key.into(), json!(value));
        self.passed &= value;
    }

    /// The screens and checks of a run without `--rom`.
    fn plan(&mut self) {
        for screen in SCREENS {
            self.steps.push_back(Step::Apply(screen));
            self.steps.push_back(Step::Wait(4));
            let path = match (&self.options.screenshots, &self.options.screenshot) {
                (Some(directory), _) => Some(directory.join(format!("{screen}.ppm"))),
                (None, Some(path)) if *screen == SCREENS[0] => Some(path.clone()),
                _ => None,
            };
            if let Some(path) = path {
                self.steps.push_back(Step::Screenshot(path));
            }
            self.steps.push_back(Step::Expand);
            self.steps.push_back(Step::Wait(4));
            self.steps.push_back(Step::Measure(screen));
            self.steps.push_back(Step::ScrollCheck(screen));
        }
        self.steps.push_back(Step::Apply("not_installed"));
        self.steps.push_back(Step::Wait(3));
        self.steps.push_back(Step::TrackInput(0));
        self.steps.push_back(Step::Apply("not_installed"));
        self.steps.push_back(Step::Wait(3));
        self.steps.push_back(Step::Keyboard(KeyStep::Start));
        self.steps.push_back(Step::Finish);
    }

    pub fn after_frame(&mut self, app: &mut ForgeApp, context: &egui::Context) {
        context.request_repaint();
        if self.done {
            return;
        }
        self.frames += 1;
        let elapsed = self.started.elapsed().as_secs_f64();
        if elapsed >= self.options.timeout {
            self.notifications.push(json!({"kind": "error", "message": "GUI probe timed out"}));
            self.passed = false;
            self.finish(app, context);
            return;
        }
        if !self.settled {
            let size = context.content_rect().size();
            self.stable_frames = if size == self.last_size { self.stable_frames + 1 } else { 0 };
            self.last_size = size;
            if app.geometry.applied && self.stable_frames >= 10 && elapsed >= 1.0 && app.state.is_some() {
                self.settled = true;
                match &self.options.rom {
                    Some(rom) => {
                        app.rom = std::path::absolute(rom).unwrap_or_else(|_| rom.clone()).to_string_lossy().into();
                        app.recognize();
                        self.recognizing = true;
                    }
                    None => self.plan(),
                }
            }
            return;
        }
        if self.recognizing {
            if !app.recognizing() {
                self.recognizing = false;
                let card = app.current_card().cloned();
                self.passed &= matches!(card, Some(Card::Recognized(_)));
                if let Some(Card::Recognized(rom)) = &card {
                    if let Some(last) = rom.languages.len().checked_sub(1) {
                        app.choose_language(last);
                        self.report.insert("chosen_language".into(), json!(rom.languages[last].0));
                    }
                }
                self.report.insert("recognized_rom".into(), card_json(card.as_ref(), app));
                app.start_install();
                self.installing = true;
            }
            return;
        }
        if self.installing {
            if !app.busy && !self.notifications.is_empty() && app.state.is_some() {
                self.installing = false;
                let installed = app.active_title.is_some() && self.has("info") && !self.has("error");
                self.report.insert("installation_ready".into(), json!(installed));
                self.report.insert("install_wall_seconds".into(), json!(elapsed));
                self.passed &= installed;
                if let Some(chosen) = self.report.get("chosen_language").cloned() {
                    let path = forge_native::language::config_path(&app.installation.data_root);
                    let saved =
                        forge_base::fsutil::load_json_object(&path).ok().and_then(|d| d.get("selected").cloned());
                    let applied = saved.as_ref() == Some(&chosen);
                    self.report.insert("language_applied".into(), json!(applied));
                    self.passed &= applied;
                }
                self.steps.push_back(Step::Wait(4));
                if let Some(path) = &self.options.screenshot {
                    self.steps.push_back(Step::Screenshot(path.clone()));
                }
                self.steps.push_back(Step::Measure("installed"));
                self.steps.push_back(Step::Finish);
            }
            return;
        }
        if self.wait > 0 {
            self.wait -= 1;
            return;
        }
        if let Some(step) = self.steps.pop_front() {
            self.step(step, app, context);
        }
    }

    fn step(&mut self, step: Step, app: &mut ForgeApp, context: &egui::Context) {
        match step {
            Step::Wait(frames) => self.wait = frames,
            Step::Apply(screen) => apply_screen(app, screen),
            Step::Screenshot(path) => {
                self.pending.push(egui::Event::PointerGone);
                context.send_viewport_cmd(egui::ViewportCommand::Screenshot(egui::UserData::default()));
                self.steps.push_front(Step::AwaitScreenshot(path));
            }
            Step::AwaitScreenshot(path) => {
                let image = context.input(|input| {
                    input.raw.events.iter().find_map(|event| match event {
                        egui::Event::Screenshot { image, .. } => Some(Arc::clone(image)),
                        _ => None,
                    })
                });
                match image {
                    Some(image) => {
                        let written = write_ppm(&path, &image).map_err(|error| error.to_string());
                        self.passed &= written.is_ok();
                        let shown = path.to_string_lossy().into_owned();
                        let entry = self.report.entry("screenshots").or_insert_with(|| json!([]));
                        if let Value::Array(list) = entry {
                            list.push(json!(written.map(|()| shown).unwrap_or_else(|error| error)));
                        }
                    }
                    None => self.steps.push_front(Step::AwaitScreenshot(path)),
                }
            }
            Step::Expand => {
                (app.show_details, app.show_needs, app.show_prepare_again) = (true, true, true);
            }
            Step::Measure(screen) => self.measure(screen, app, context),
            Step::ScrollCheck(screen) => {
                let indices: Vec<usize> = app
                    .widgets
                    .iter()
                    .enumerate()
                    .filter(|(_, widget)| widget.scrolled)
                    .map(|(index, _)| index)
                    .collect();
                for index in indices.into_iter().rev() {
                    self.steps.push_front(Step::ScrollTo { screen, index, retry: false });
                }
            }
            Step::ScrollTo { screen, index, retry } => {
                app.scroll_to = Some(index);
                self.wait = 2;
                self.steps.push_front(Step::Visible { screen, index, retry });
            }
            Step::Visible { screen, index, retry } => {
                let Some(widget) = app.widgets.get(index).cloned() else {
                    self.scroll_failure(screen, "missing", index, None);
                    return;
                };
                let viewport = app.scroll_viewport;
                let visible = viewport.intersect(widget.rect).height();
                let wide = widget.rect.width() > viewport.width() + 0.5;
                // A group taller than the view is reached from its top: it fills the view.
                let slack = if widget.rect.height() > viewport.height() { 8.0 } else { 0.5 };
                let hidden = wide || visible + slack < widget.rect.height().min(viewport.height());
                if hidden && !retry {
                    // A scroll that lands while the layout settles is asked once more.
                    self.steps.push_front(Step::ScrollTo { screen, index, retry: true });
                    return;
                }
                self.scroll_checked += 1;
                if hidden {
                    self.scroll_failure(screen, "not scrolled into view", index, Some(&widget));
                } else if widget.interactive() {
                    let point = viewport.intersect(widget.rect).center();
                    self.pending.push(egui::Event::PointerMoved(point));
                    self.steps.push_front(Step::Hovered { screen, index });
                }
            }
            Step::Hovered { screen, index } => {
                let widget = app.widgets.get(index).cloned();
                if !widget.as_ref().is_some_and(|widget| widget.hovered) {
                    self.scroll_failure(screen, "covered: the pointer does not reach it", index, widget.as_ref());
                }
                self.pending.push(egui::Event::PointerGone);
            }
            Step::TrackInput(stage) => self.track_input(stage, app),
            Step::Keyboard(stage) => self.keyboard(stage, app, context),
            Step::Finish => self.finish(app, context),
        }
    }

    fn scroll_failure(&mut self, screen: &str, reason: &str, index: usize, widget: Option<&WidgetRecord>) {
        self.passed = false;
        let mut entry = json!({"screen": screen, "index": index, "reason": reason});
        if let Some(widget) = widget {
            entry["widget"] = record_json(widget);
        }
        self.scroll_failures.push(entry);
    }

    fn measure(&mut self, screen: &str, app: &mut ForgeApp, context: &egui::Context) {
        let info = context.input(|input| input.viewport().clone());
        let viewport = context.content_rect();
        let outer = info.outer_rect.map_or(viewport.size() + app.geometry.decoration, |outer| outer.size());
        let work_area = self.options.display.work_area.or(app.geometry.work_area);
        let (passed, mut details) = layout_report(&app.widgets, viewport, outer, work_area);
        self.passed &= passed;
        for (key, list) in [("clipped_widgets", &mut self.clipped), ("unmapped_widgets", &mut self.unmapped)] {
            for mut widget in details[key].as_array().cloned().unwrap_or_default() {
                widget["screen"] = json!(screen);
                list.push(widget);
            }
        }
        details["passed"] = json!(passed);
        details["widgets"] = json!(app.widgets.len());
        details["language"] = json!(app.texts.language());
        let card = app.home_card();
        details["card"] = json!({"kind": card.kind.code(), "heading": card.heading,
                                 "primary": format!("{:?}", card.primary.command), "primary_enabled": card.primary.enabled});
        if screen == "not_installed" || screen == "installed" {
            let (choices, _) = app.language_options();
            let buttons = app.widgets.iter().filter(|widget| widget.name == "language_choice").count();
            let card_ok = app.widgets.iter().any(|widget| widget.name == "rom_card") && buttons == choices.len();
            self.report.insert(
                "language_card".into(),
                json!({"card": card_json(app.current_card(), app), "buttons": buttons,
                       "synthetic": self.options.rom.is_none(), "laid_out": card_ok}),
            );
            self.passed &= card_ok;
        }
        // A simulated display scale must be the one laid out.
        let scale = context.pixels_per_point();
        self.passed &= self.options.display.pixels_per_point.is_none_or(|wanted| (scale - wanted).abs() < 0.001);
        for key in ["viewport", "outer_size"] {
            self.report.insert(key.into(), details[key].clone());
        }
        self.report.insert("size".into(), json!([viewport.width(), viewport.height()]));
        self.report.insert("work_area".into(), json!(work_area.map(|area| [area.x, area.y])));
        self.report.insert("pixels_per_point".into(), json!(scale));
        self.report.insert("simulated_pixels_per_point".into(), json!(self.options.display.pixels_per_point));
        self.report.insert("min_inner_size".into(), json!([MIN_SIZE.x, MIN_SIZE.y]));
        self.screens.insert(screen.to_string(), details);
    }

    /// Prepare follows the field: enabled for the recognized ROM's path,
    /// disabled for another path.
    fn track_input(&mut self, stage: u8, app: &mut ForgeApp) {
        let enabled = Self::widget(app, "primary").map(|widget| widget.enabled);
        if stage == 0 {
            self.report.insert("install_enabled_with_rom".into(), json!(enabled == Some(true)));
            app.rom = "other.cci".into();
            self.wait = 1;
            self.steps.push_front(Step::TrackInput(1));
        } else {
            let tracks = self.report["install_enabled_with_rom"] == true && enabled == Some(false);
            self.report.insert("prepare_button_tracks_input".into(), json!(tracks));
            self.passed &= tracks;
        }
    }

    fn focused(app: &ForgeApp) -> Option<&'static str> {
        app.widgets.iter().find(|widget| widget.focused).map(|widget| widget.name)
    }

    /// Runs `stage` after the next frame.
    fn then(&mut self, stage: KeyStep) {
        self.wait = 1;
        self.steps.push_front(Step::Keyboard(stage));
    }

    fn keyboard(&mut self, stage: KeyStep, app: &mut ForgeApp, context: &egui::Context) {
        match stage {
            KeyStep::Start => {
                context.memory_mut(|memory| {
                    if let Some(id) = memory.focused() {
                        memory.surrender_focus(id);
                    }
                });
                app.action_log = Some(Vec::new());
                self.tab_order.clear();
                self.key(Key::Tab, Modifiers::NONE);
                self.then(KeyStep::Tab);
            }
            KeyStep::Tab => {
                let focused = Self::focused(app);
                self.tab_presses += 1;
                match focused {
                    Some(name) => self.tab_order.push(name),
                    None if context.memory(|memory| memory.focused()).is_some() => self.tab_order.push("(unrecorded)"),
                    None => self.tab_order.push("(none)"),
                }
                if focused != Some("primary") && self.tab_presses < 60 {
                    self.key(Key::Tab, Modifiers::NONE);
                    self.then(KeyStep::Tab);
                    return;
                }
                let wanted = ["nav_home", "rom", "rom_browse", "language_choice", "primary"];
                let mut position = 0;
                for name in &self.tab_order {
                    if position < wanted.len() && *name == wanted[position] {
                        position += 1;
                    }
                }
                self.keyboard.insert("tab_order".into(), json!(self.tab_order));
                self.check("tab_order_ok", position == wanted.len() && self.tab_order[0] == "nav_home");
                self.key(Key::Tab, Modifiers::SHIFT);
                self.then(KeyStep::ShiftTabCheck);
            }
            KeyStep::ShiftTabCheck => {
                let before = self.tab_order.len().checked_sub(2).map(|index| self.tab_order[index]);
                let focused = Self::focused(app);
                self.keyboard.insert("shift_tab_from_primary".into(), json!(focused));
                self.check("shift_tab_goes_back", focused.is_some() && focused == before);
                self.focus_then_enter("rom", app, context);
            }
            KeyStep::Enter(name) => {
                self.key(Key::Enter, Modifiers::NONE);
                self.then(KeyStep::EnterCheck(name));
            }
            KeyStep::EnterCheck(name) => {
                let log = app.action_log.replace(Vec::new()).unwrap_or_default();
                self.keyboard.insert(format!("enter_on_{name}"), json!(log));
                let prepared = log.iter().any(|action| action == "Prepare");
                match name {
                    "rom" => {
                        self.check("enter_in_rom_field_does_not_prepare", !prepared);
                        self.focus_then_enter("language_choice", app, context);
                    }
                    "language_choice" => {
                        let only = log.iter().all(|action| action.starts_with("SelectLanguage"));
                        self.check("enter_on_language_selects_it", only && !log.is_empty());
                        self.focus_then_enter("primary", app, context);
                    }
                    _ => {
                        self.check("enter_on_primary_prepares", log == ["Prepare"]);
                        apply_screen(app, "preparing");
                        self.then(KeyStep::EscapeWhilePreparing);
                    }
                }
            }
            KeyStep::EscapeWhilePreparing => {
                self.key(Key::Escape, Modifiers::NONE);
                self.then(KeyStep::EscapeCheck);
            }
            KeyStep::EscapeCheck => {
                let log = app.action_log.replace(Vec::new()).unwrap_or_default();
                self.check("escape_does_not_cancel", log.is_empty() && app.busy && app.dialogs.is_empty());
                self.then(KeyStep::CancelFocus);
            }
            KeyStep::CancelFocus => {
                // The real Cancel action: it must ask first.
                app.action_log = None;
                match Self::widget(app, "cancel").map(|widget| widget.id) {
                    Some(id) => {
                        context.memory_mut(|memory| memory.request_focus(id));
                        self.then(KeyStep::CancelEnter);
                    }
                    None => {
                        self.check("cancel_asks_for_confirmation", false);
                        apply_screen(app, "not_installed");
                    }
                }
            }
            KeyStep::CancelEnter => {
                self.key(Key::Enter, Modifiers::NONE);
                self.then(KeyStep::CancelCheck);
            }
            KeyStep::CancelCheck => {
                let asked = app.dialogs.front().map(|dialog| dialog.kind) == Some(DialogKind::ConfirmCancel);
                self.check("cancel_asks_for_confirmation", asked && !app.install_cancelled());
                self.then(KeyStep::DialogEscape);
            }
            KeyStep::DialogEscape => {
                self.key(Key::Escape, Modifiers::NONE);
                self.then(KeyStep::DialogEscapeCheck);
            }
            KeyStep::DialogEscapeCheck => {
                let closed = app.dialogs.is_empty() && app.busy && !app.install_cancelled();
                self.check("escape_closes_the_dialog_without_cancelling", closed);
                apply_screen(app, "not_installed");
            }
        }
    }

    /// Focuses the first widget called `name`, then presses Enter.
    fn focus_then_enter(&mut self, name: &'static str, app: &mut ForgeApp, context: &egui::Context) {
        let Some(id) = Self::widget(app, name).map(|widget| widget.id) else {
            self.check(&format!("{name}_present"), false);
            return;
        };
        context.memory_mut(|memory| memory.request_focus(id));
        app.action_log = Some(Vec::new());
        self.then(KeyStep::Enter(name));
    }

    fn finish(&mut self, app: &mut ForgeApp, context: &egui::Context) {
        self.done = true;
        self.steps.clear();
        let runtime = app.installation.runtime_path().map(|path| path.to_string_lossy().into_owned());
        let all = |key: &str| !self.screens.is_empty() && self.screens.values().all(|screen| screen[key] == true);
        let (primary_visible, fits) = (all("primary_visible"), all("fits_work_area"));
        let mut report = Map::new();
        report.insert("status".into(), json!(if self.passed { "passed" } else { "failed" }));
        report.insert("platform".into(), json!(std::env::consts::OS));
        report.insert(
            "executable".into(),
            json!(std::env::current_exe().ok().map(|path| path.to_string_lossy().into_owned())),
        );
        report.insert("runtime".into(), json!(runtime.unwrap_or_else(|error| error.message)));
        report.insert("frames".into(), json!(self.frames));
        report.insert("seconds".into(), json!(self.started.elapsed().as_secs_f64()));
        report.insert("final_status_text".into(), json!(app.status_note.clone().unwrap_or(app.home_card().status)));
        report.append(&mut self.report);
        report.insert("fits_work_area".into(), json!(fits));
        report.insert("primary_visible".into(), json!(primary_visible));
        report.insert("clipped_widgets".into(), Value::Array(std::mem::take(&mut self.clipped)));
        report.insert("unmapped_widgets".into(), Value::Array(std::mem::take(&mut self.unmapped)));
        if self.options.rom.is_none() {
            let scroll = json!({"checked": self.scroll_checked, "failures": std::mem::take(&mut self.scroll_failures)});
            report.insert("scroll_check".into(), scroll);
            report.insert("keyboard".into(), Value::Object(std::mem::take(&mut self.keyboard)));
        }
        report.insert("screens".into(), Value::Object(std::mem::take(&mut self.screens)));
        if self.options.rom.is_some() || !self.notifications.is_empty() {
            report.insert("notifications".into(), Value::Array(std::mem::take(&mut self.notifications)));
        }
        *self.result.lock().unwrap_or_else(|poison| poison.into_inner()) = Some(Value::Object(report));
        app.close(context);
    }
}

fn sample_languages() -> Vec<(String, String)> {
    let languages = [("en", "English"), ("de", "Deutsch"), ("fr", "Francais"), ("es", "Espanol"), ("it", "Italiano")];
    languages.iter().map(|(code, label)| (code.to_string(), label.to_string())).collect()
}

/// The card the probe shows without a ROM: a long revision label and five
/// languages, the most a supported revision offers.
fn sample_card() -> Card {
    Card::Recognized(RecognizedRom {
        label: "The Legend of Zelda: Ocarina of Time 3D (EUR (No-Intro original and Rev 1))".into(),
        languages: sample_languages(),
        selected: Some(0),
        language_error: None,
    })
}

fn card_json(card: Option<&Card>, app: &ForgeApp) -> Value {
    let Some(card) = card else { return Value::Null };
    let (state, languages, selected) = match card {
        Card::Recognizing(_) => ("recognizing", Vec::new(), None),
        Card::Failed(_) => ("failed", Vec::new(), None),
        Card::Recognized(rom) => ("recognized", rom.languages.clone(), rom.chosen().map(str::to_string)),
    };
    let message = match card {
        Card::Failed(explanation) => Some(crate::recognition::failure_text(explanation, &app.texts)),
        Card::Recognized(rom) => rom.language_error.clone(),
        Card::Recognizing(_) => None,
    };
    let codes: Vec<&String> = languages.iter().map(|(code, _)| code).collect();
    json!({"state": state, "title": heading(card, &app.texts), "languages": codes, "selected": selected,
           "message": message})
}

fn write_ppm(path: &Path, image: &egui::ColorImage) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let [width, height] = image.size;
    let mut bytes = format!("P6\n{width} {height}\n255\n").into_bytes();
    for pixel in &image.pixels {
        bytes.extend_from_slice(&[pixel.r(), pixel.g(), pixel.b()]);
    }
    std::fs::write(path, bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use eframe::egui::{pos2, vec2};

    fn widget(name: &'static str, rect: Rect, scrolled: bool) -> WidgetRecord {
        WidgetRecord {
            name,
            class: "button",
            rect,
            enabled: true,
            scrolled,
            id: egui::Id::new(name),
            hovered: false,
            focused: false,
        }
    }

    #[test]
    fn options_parse() {
        let arguments: Vec<String> =
            ["--output", "o.json", "--work-area", "800x500"].iter().map(|text| text.to_string()).collect();
        let options = parse(&arguments).unwrap();
        assert_eq!(options.output, PathBuf::from("o.json"));
        assert_eq!(options.display.work_area, Some(vec2(800.0, 500.0)));
        assert_eq!(options.display.pixels_per_point, None);
        assert_eq!(options.timeout, 180.0);
        assert!(parse(&["--work-area".to_string()]).is_err());
        let scaled: Vec<String> = ["--output", "o.json", "--pixels-per-point", "1.25", "--language", "pseudo"]
            .iter()
            .map(|text| text.to_string())
            .collect();
        let scaled = parse(&scaled).unwrap();
        assert_eq!(scaled.display.pixels_per_point, Some(1.25));
        assert_eq!(scaled.language.as_deref(), Some("pseudo"));
        for bad in ["0", "x", "9"] {
            let arguments: Vec<String> =
                ["--output", "o.json", "--pixels-per-point", bad].iter().map(|text| text.to_string()).collect();
            assert!(parse(&arguments).is_err(), "{bad}");
        }
        assert!(parse(&[]).is_err());
    }

    #[test]
    fn scrolled_pages_may_leave_the_view_vertically_but_the_primary_action_may_not() {
        let viewport = Rect::from_min_size(pos2(0.0, 0.0), vec2(720.0, 500.0));
        let footer = |y: f32| {
            vec![
                widget("secondary", Rect::from_min_size(pos2(500.0, y), vec2(120.0, 24.0)), false),
                widget("primary", Rect::from_min_size(pos2(630.0, y), vec2(80.0, 24.0)), false),
                widget("option", Rect::from_min_size(pos2(10.0, 900.0), vec2(100.0, 20.0)), true),
            ]
        };
        let (passed, details) = layout_report(&footer(460.0), viewport, vec2(736.0, 539.0), Some(vec2(800.0, 560.0)));
        assert!(passed, "{details}");
        assert_eq!(details["options_scroll"], true);
        let (passed, details) = layout_report(&footer(490.0), viewport, vec2(736.0, 539.0), None);
        assert!(!passed);
        assert_eq!(details["primary_visible"], false);
        let (passed, details) = layout_report(&footer(460.0), viewport, vec2(736.0, 539.0), Some(vec2(800.0, 500.0)));
        assert!(!passed);
        assert_eq!(details["fits_work_area"], false);
        let mut wide = footer(460.0);
        wide[2].rect = Rect::from_min_size(pos2(10.0, 900.0), vec2(800.0, 20.0));
        assert!(!layout_report(&wide, viewport, vec2(736.0, 539.0), None).0);
        // While preparing, Cancel is the main action.
        let mut cancel = footer(460.0);
        cancel[1].name = "cancel";
        assert!(layout_report(&cancel, viewport, vec2(736.0, 539.0), None).0);
    }

    #[test]
    fn every_state_and_page_has_a_screen() {
        for page in Page::ALL.iter().filter(|page| **page != Page::Home) {
            assert!(SCREENS.contains(&page.code()), "{page:?}");
        }
        let kinds = [
            "not_installed",
            "checking",
            "preparing",
            "preparing_elsewhere",
            "ready",
            "update_available",
            "repair_needed",
            "needs_game_input",
            "incompatible",
            "failed",
        ];
        for kind in kinds {
            assert!(SCREENS.contains(&kind), "{kind}");
        }
    }
}
