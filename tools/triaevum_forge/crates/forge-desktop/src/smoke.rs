// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `--gui-smoke`: the real window, laid out by the real widgets, checked
//! unattended (`forge_gui_probe.py`). Options may scroll out of view
//! vertically, never horizontally; the footer with the primary actions must
//! be entirely inside the window, and the window inside the work area.
//!
//! `--work-area WxH` is in logical points. `--screenshot PATH` (a Rust
//! addition) writes the rendered frame as a binary PPM.

use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::Instant;

use eframe::egui::{self, Rect, Vec2};
use serde_json::{json, Map, Value};

use crate::app::{DialogKind, ForgeApp, WidgetRecord};
use crate::window::{parse_work_area, MIN_SIZE};

const USAGE: &str =
    "Usage: TriAevumForge --gui-smoke --output PATH [--rom PATH] [--timeout SEC] [--work-area WxH] [--screenshot PATH]";

#[derive(Clone, Debug, Default)]
pub struct ProbeOptions {
    pub output: PathBuf,
    pub rom: Option<PathBuf>,
    pub timeout: f64,
    pub work_area: Option<Vec2>,
    pub screenshot: Option<PathBuf>,
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
            "--timeout" => {
                options.timeout = value()?.parse().map_err(|_| "--timeout needs a number of seconds".to_string())?
            }
            "--work-area" => {
                options.work_area =
                    Some(parse_work_area(&value()?).ok_or_else(|| "--work-area needs WIDTHxHEIGHT".to_string())?)
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
    let outcome = crate::app::run_window(None, None, Some(probe), options.work_area);
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

/// The layout verdict for one frame: clipped and collapsed widgets, whether
/// both footer buttons are fully visible, whether the window fits.
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
    let footer_visible = ["install", "launch"].iter().all(|name| {
        widgets.iter().any(|widget| {
            widget.name == *name
                && !widget.scrolled
                && widget.rect.is_positive()
                && viewport.expand(SLACK).contains_rect(widget.rect)
        })
    });
    let fits = work_area.is_none_or(|area| outer.x <= area.x + SLACK && outer.y <= area.y + SLACK);
    let scrolled_bottom = widgets.iter().filter(|w| w.scrolled).map(|w| w.rect.bottom()).fold(f32::MIN, f32::max);
    let footer_top = widgets.iter().filter(|w| !w.scrolled).map(|w| w.rect.top()).fold(f32::MAX, f32::min);
    let passed = !widgets.is_empty() && clipped.is_empty() && unmapped.is_empty() && footer_visible && fits;
    let details = json!({
        "fits_work_area": fits,
        "footer_visible": footer_visible,
        "options_scroll": scrolled_bottom > footer_top,
        "viewport": bounds(viewport),
        "outer_size": [outer.x, outer.y],
        "widgets": widgets.iter().map(record_json).collect::<Vec<_>>(),
        "clipped_widgets": clipped,
        "unmapped_widgets": unmapped,
    });
    (passed, details)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Phase {
    Settling,
    Installing,
    Measure,
    InputEntered,
    InputCleared,
    Screenshot,
    Done,
}

pub struct Probe {
    options: ProbeOptions,
    result: Arc<Mutex<Option<Value>>>,
    started: Instant,
    phase: Phase,
    stable_frames: u32,
    last_size: Vec2,
    notifications: Vec<Value>,
    report: Map<String, Value>,
    passed: bool,
}

impl Probe {
    pub fn new(options: ProbeOptions, result: Arc<Mutex<Option<Value>>>) -> Self {
        Self {
            options,
            result,
            started: Instant::now(),
            phase: Phase::Settling,
            stable_frames: 0,
            last_size: Vec2::ZERO,
            notifications: Vec::new(),
            report: Map::new(),
            passed: true,
        }
    }

    pub fn record(&mut self, kind: DialogKind, message: &str) {
        let kind = if kind == DialogKind::Info { "info" } else { "error" };
        self.notifications.push(json!({"kind": kind, "message": message}));
    }

    fn has(&self, kind: &str) -> bool {
        self.notifications.iter().any(|item| item["kind"] == kind)
    }

    fn install_button(app: &ForgeApp) -> Option<bool> {
        app.widgets.iter().find(|widget| widget.name == "install").map(|widget| widget.enabled)
    }

    pub fn after_frame(&mut self, app: &mut ForgeApp, context: &egui::Context) {
        context.request_repaint();
        let elapsed = self.started.elapsed().as_secs_f64();
        if self.phase != Phase::Done && elapsed >= self.options.timeout {
            self.notifications.push(json!({"kind": "error", "message": "GUI probe timed out"}));
            self.passed = false;
            self.finish(app, context);
            return;
        }
        match self.phase {
            Phase::Settling => {
                let size = context.content_rect().size();
                self.stable_frames = if size == self.last_size { self.stable_frames + 1 } else { 0 };
                self.last_size = size;
                if app.geometry.applied && self.stable_frames >= 10 && elapsed >= 1.0 {
                    if let Some(rom) = &self.options.rom {
                        app.rom = std::path::absolute(rom).unwrap_or_else(|_| rom.clone()).to_string_lossy().into();
                        app.start_install();
                        self.phase = Phase::Installing;
                    } else {
                        self.phase = Phase::Measure;
                    }
                }
            }
            Phase::Installing => {
                if !app.busy && !self.notifications.is_empty() {
                    let installed = app.active_title.is_some() && self.has("info") && !self.has("error");
                    self.report.insert("installation_ready".into(), json!(installed));
                    self.report.insert("install_wall_seconds".into(), json!(elapsed));
                    self.passed &= installed;
                    self.phase = Phase::Measure;
                }
            }
            Phase::Measure => {
                let info = context.input(|input| input.viewport().clone());
                let viewport = context.content_rect();
                let outer = info.outer_rect.map_or(viewport.size() + app.geometry.decoration, |outer| outer.size());
                let work_area = self.options.work_area.or(app.geometry.work_area);
                let (passed, details) = layout_report(&app.widgets, viewport, outer, work_area);
                self.passed &= passed;
                if let Value::Object(details) = details {
                    self.report.extend(details);
                }
                self.report.insert("size".into(), json!([viewport.width(), viewport.height()]));
                self.report.insert("work_area".into(), json!(work_area.map(|area| [area.x, area.y])));
                self.report.insert("pixels_per_point".into(), json!(context.pixels_per_point()));
                self.report.insert("min_inner_size".into(), json!([MIN_SIZE.x, MIN_SIZE.y]));
                app.rom = "probe.cci".into();
                self.phase = Phase::InputEntered;
            }
            Phase::InputEntered => {
                self.report.insert("install_enabled_with_rom".into(), json!(Self::install_button(app) == Some(true)));
                app.rom.clear();
                self.phase = Phase::InputCleared;
            }
            Phase::InputCleared => {
                let enabled = self.report["install_enabled_with_rom"] == true;
                let tracks = enabled && Self::install_button(app) == Some(false);
                self.report.insert("prepare_button_tracks_input".into(), json!(tracks));
                self.passed &= tracks;
                if self.options.screenshot.is_some() {
                    context.send_viewport_cmd(egui::ViewportCommand::Screenshot(egui::UserData::default()));
                    self.phase = Phase::Screenshot;
                } else {
                    self.finish(app, context);
                }
            }
            Phase::Screenshot => {
                let image = context.input(|input| {
                    input.raw.events.iter().find_map(|event| match event {
                        egui::Event::Screenshot { image, .. } => Some(Arc::clone(image)),
                        _ => None,
                    })
                });
                if let (Some(image), Some(path)) = (image, self.options.screenshot.clone()) {
                    let written = write_ppm(&path, &image).map_err(|error| error.to_string());
                    self.report.insert(
                        "screenshot".into(),
                        json!(written.map(|()| path.to_string_lossy().into_owned()).unwrap_or_else(|error| error)),
                    );
                    self.finish(app, context);
                }
            }
            Phase::Done => {}
        }
    }

    fn finish(&mut self, app: &mut ForgeApp, context: &egui::Context) {
        self.phase = Phase::Done;
        let runtime = app.installation.runtime_path().map(|path| path.to_string_lossy().into_owned());
        let mut report = Map::new();
        report.insert("status".into(), json!(if self.passed { "passed" } else { "failed" }));
        report.insert("platform".into(), json!(std::env::consts::OS));
        report.insert(
            "executable".into(),
            json!(std::env::current_exe().ok().map(|path| path.to_string_lossy().into_owned())),
        );
        report.insert("runtime".into(), json!(runtime.unwrap_or_else(|error| error.message)));
        report.insert("final_status_text".into(), json!(app.status));
        report.append(&mut self.report);
        if self.options.rom.is_some() || !self.notifications.is_empty() {
            report.insert("notifications".into(), Value::Array(std::mem::take(&mut self.notifications)));
        }
        *self.result.lock().unwrap_or_else(|poison| poison.into_inner()) = Some(Value::Object(report));
        app.close(context);
    }
}

fn write_ppm(path: &Path, image: &egui::ColorImage) -> std::io::Result<()> {
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
        WidgetRecord { name, class: "button", rect, enabled: true, scrolled }
    }

    #[test]
    fn options_parse() {
        let arguments: Vec<String> =
            ["--output", "o.json", "--work-area", "800x500"].iter().map(|text| text.to_string()).collect();
        let options = parse(&arguments).unwrap();
        assert_eq!(options.output, PathBuf::from("o.json"));
        assert_eq!(options.work_area, Some(vec2(800.0, 500.0)));
        assert_eq!(options.timeout, 180.0);
        assert!(parse(&["--work-area".to_string()]).is_err());
        assert!(parse(&[]).is_err());
    }

    #[test]
    fn scrolled_options_may_leave_the_view_vertically_but_the_footer_may_not() {
        let viewport = Rect::from_min_size(pos2(0.0, 0.0), vec2(720.0, 500.0));
        let footer = |y: f32| {
            vec![
                widget("install", Rect::from_min_size(pos2(500.0, y), vec2(120.0, 24.0)), false),
                widget("launch", Rect::from_min_size(pos2(630.0, y), vec2(80.0, 24.0)), false),
                widget("option", Rect::from_min_size(pos2(10.0, 900.0), vec2(100.0, 20.0)), true),
            ]
        };
        let (passed, details) = layout_report(&footer(460.0), viewport, vec2(736.0, 539.0), Some(vec2(800.0, 560.0)));
        assert!(passed, "{details}");
        assert_eq!(details["options_scroll"], true);
        let (passed, details) = layout_report(&footer(490.0), viewport, vec2(736.0, 539.0), None);
        assert!(!passed);
        assert_eq!(details["footer_visible"], false);
        let (passed, details) = layout_report(&footer(460.0), viewport, vec2(736.0, 539.0), Some(vec2(800.0, 500.0)));
        assert!(!passed);
        assert_eq!(details["fits_work_area"], false);
        let mut wide = footer(460.0);
        wide[2].rect = Rect::from_min_size(pos2(10.0, 900.0), vec2(800.0, 20.0));
        assert!(!layout_report(&wide, viewport, vec2(736.0, 539.0), None).0);
    }
}
