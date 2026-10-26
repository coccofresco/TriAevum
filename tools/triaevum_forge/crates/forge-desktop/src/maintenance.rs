// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Update and Repair in place from Home (UX plan WP-04A): the forge-core
//! `refresh` and `repair` jobs, without the ROM.
//!
//! Before either starts, owner decision 4 is asked when it matters: if the
//! user's TopScreen layout differs from the standard profile, "Reset the HUD
//! layout to the standard profile?" lists what would change, with Keep mine
//! (the default) and Use standard. An update that finds an older package
//! asks before installing it (decision 10). The result says what the
//! operation changed in the settings ("Update changed: ...").

use eframe::egui::{self, Button, Label, RichText};
use forge_base::CancelToken;
use forge_core::config_report::{hud_layout_preview, LayoutField};
use forge_core::explain::{explain, Explanation};
use forge_core::publish::HudLayout;
use serde_json::{json, Value};

use crate::app::ForgeApp;
use crate::install::{counts, job_update_in, Event, JobUpdate, Notifier};
use crate::texts::{Msg, Texts};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Update,
    Repair,
}

impl Kind {
    fn job(self) -> &'static str {
        match self {
            Self::Update => "refresh",
            Self::Repair => "repair",
        }
    }
}

/// What the window asks before an operation continues.
#[derive(Clone, Debug, PartialEq)]
pub enum Ask {
    /// Decision 4: the layout fields "Use standard" would change.
    Layout(Vec<LayoutField>),
    /// Decision 10: the package is older than the installed one.
    Downgrade { installed: String, present: String },
}

#[derive(Clone, Debug, PartialEq)]
pub struct Question {
    pub kind: Kind,
    pub ask: Ask,
    /// The layout choice already made (a downgrade is asked after it).
    pub hud_layout: HudLayout,
}

#[derive(Default)]
pub struct MaintenanceState {
    pub question: Option<Question>,
    /// The update or repair that ran last and has not succeeded: Retry
    /// repeats it.
    pub last: Option<Kind>,
    focus_pending: bool,
}

/// The job request for `kind`.
fn request(kind: Kind, app: &ForgeApp, hud_layout: HudLayout, allow_downgrade: bool) -> Value {
    let mut request = json!({
        "format": forge_core::jobs::REQUEST_FORMAT,
        "job": kind.job(),
        "data_root": app.installation.data_root.to_string_lossy(),
        "package_root": app.installation.root.to_string_lossy(),
        "hud_layout": hud_layout.code(),
    });
    if kind == Kind::Update {
        request["allow_downgrade"] = json!(allow_downgrade);
    }
    request
}

/// Home's Update or Repair: asks decision 4 first when the layout differs.
pub fn begin(app: &mut ForgeApp, kind: Kind) {
    if !app.idle() {
        return;
    }
    let fields = hud_layout_preview(&app.installation.data_root).unwrap_or_default();
    if fields.is_empty() {
        start(app, kind, HudLayout::Keep, false);
    } else {
        ask(app, Question { kind, ask: Ask::Layout(fields), hud_layout: HudLayout::Keep });
    }
}

pub fn ask(app: &mut ForgeApp, question: Question) {
    app.maintenance.question = Some(question);
    app.maintenance.focus_pending = true;
}

fn start(app: &mut ForgeApp, kind: Kind, hud_layout: HudLayout, allow_downgrade: bool) {
    let request = request(kind, app, hud_layout, allow_downgrade);
    let texts = app.texts.clone();
    let stage = app.t(match kind {
        Kind::Update => Msg::Updating,
        Kind::Repair => Msg::Repairing,
    });
    app.maintenance.last = Some(kind);
    app.start_task(stage, move |_, notify, cancel| run(kind, hud_layout, &request, &texts, &notify, &cancel));
}

/// Runs the job; its events become window events.
fn run(kind: Kind, hud_layout: HudLayout, request: &Value, texts: &Texts, notify: &Notifier, cancel: &CancelToken) {
    let job = match forge_core::jobs::start(&forge_base::legacy_json::compact_line_bytes(request)) {
        Ok(job) => job,
        Err(error) => return notify.send(Event::InstallFailed(Box::new(explain(&error)))),
    };
    loop {
        if cancel.is_cancelled() {
            job.cancel();
        }
        let Some(event) = job.poll(100) else { continue };
        match job_update_in(&event, texts) {
            Some(JobUpdate::Stage(message)) => {
                let counted = counts(&message);
                notify.send(Event::Stage(message));
                if let Some((done, total)) = counted {
                    notify.send(Event::Progress { done, total: Some(total) });
                }
            }
            Some(JobUpdate::Progress { done, total }) => notify.send(Event::Progress { done, total }),
            Some(JobUpdate::Succeeded(notices)) => {
                return notify.send(outcome(kind, hud_layout, &event["result"], notices, texts));
            }
            Some(JobUpdate::Failed(explanation)) => return notify.send(Event::InstallFailed(explanation)),
            Some(JobUpdate::Cancelled) => return notify.send(Event::InstallCancelled),
            None => {}
        }
    }
}

/// The window event for a job's result.
fn outcome(kind: Kind, hud_layout: HudLayout, result: &Value, notices: Vec<Explanation>, texts: &Texts) -> Event {
    if result["status"] == "confirmation_required" {
        let field = |key: &str| result[key].as_str().unwrap_or_default().to_string();
        let ask = Ask::Downgrade { installed: field("installed_version"), present: field("present_version") };
        return Event::MaintenanceQuestion(Question { kind, ask, hud_layout });
    }
    Event::MaintenanceDone { message: summary(kind, result, texts), notices }
}

/// "Updated to 0.8.1. Your saves and settings were kept." and, when the
/// settings changed beyond creating missing files, "Update changed: ...".
fn summary(kind: Kind, result: &Value, texts: &Texts) -> String {
    let changes = result["config_changes"].as_array().map(Vec::as_slice).unwrap_or_default();
    let changed: Vec<String> = changes.iter().map(|change| change_text(change, texts)).collect();
    let kept = changes.iter().all(|change| change["change"] == "created");
    let version = result["present"]["version"].as_str();
    let mut message = match (kind, result["status"].as_str()) {
        (Kind::Update, Some("unchanged")) => texts.get(Msg::AlreadyCurrent),
        (Kind::Update, _) => texts.get(Msg::Updated { version: version.unwrap_or_default(), kept }),
        (Kind::Repair, _) => texts.get(Msg::Repaired { kept }),
    };
    if !changed.is_empty() {
        let list = changed.join("; ");
        message.push_str("\n\n");
        message.push_str(&texts.get(match kind {
            Kind::Update => Msg::UpdateChanged { list: &list },
            Kind::Repair => Msg::RepairChanged { list: &list },
        }));
    }
    message
}

fn change_text(change: &Value, texts: &Texts) -> String {
    let file = change["file"].as_str().unwrap_or_default();
    texts.get(match change["change"].as_str().unwrap_or_default() {
        "created" => Msg::ChangeCreated { file },
        "standard_hud_layout" => Msg::ChangeStandardLayout,
        "language_selected" => Msg::ChangeLanguage,
        "removed" => Msg::ChangeRemoved { file },
        _ => Msg::ChangeOther { file },
    })
}

/// A field value as the user reads it (strings without quotes).
fn shown(value: &Value) -> String {
    match value {
        Value::String(text) => text.clone(),
        other => other.to_string(),
    }
}

/// The question's modal; its answer starts, continues or drops the operation.
pub fn show(app: &mut ForgeApp, context: &egui::Context) {
    let Some(question) = app.maintenance.question.clone() else { return };
    if !app.dialogs.is_empty() {
        return;
    }
    let texts = app.texts.clone();
    let focus = std::mem::take(&mut app.maintenance.focus_pending);
    // None: no answer yet; Some(None): cancelled.
    let mut answer: Option<Option<(HudLayout, bool)>> = None;
    let width = (context.content_rect().width() - 48.0).clamp(200.0, 460.0);
    let modal = egui::Modal::new(egui::Id::new("forge-maintenance")).show(context, |ui| {
        ui.set_width(width);
        let (heading, body, choices) = match &question.ask {
            Ask::Layout(fields) => {
                let lines: Vec<String> = fields
                    .iter()
                    .map(|field| {
                        let (yours, standard) = (shown(&field.yours), shown(&field.standard));
                        texts.get(Msg::HudLayoutField { name: &field.name, yours: &yours, standard: &standard })
                    })
                    .collect();
                let body = format!("{}\n\n{}", texts.get(Msg::HudLayoutBody), lines.join("\n"));
                let choices = vec![
                    (texts.get(Msg::ButtonKeepMine), Some((HudLayout::Keep, false))),
                    (texts.get(Msg::ButtonUseStandard), Some((HudLayout::Standard, false))),
                    (texts.get(Msg::ButtonCancel), None),
                ];
                (texts.get(Msg::HudLayoutQuestion), body, choices)
            }
            Ask::Downgrade { installed, present } => {
                let body = texts.get(Msg::DowngradeBody { installed, present });
                let choices = vec![
                    (texts.get(Msg::ButtonCancel), None),
                    (texts.get(Msg::ButtonInstallOlder), Some((question.hud_layout, true))),
                ];
                (texts.get(Msg::DowngradeHeading), body, choices)
            }
        };
        ui.heading(heading);
        ui.add_space(6.0);
        let height = (context.content_rect().height() * 0.6).max(80.0);
        egui::ScrollArea::vertical().max_height(height).show(ui, |ui| {
            ui.add(Label::new(body).wrap());
        });
        ui.add_space(10.0);
        ui.horizontal_wrapped(|ui| {
            for (index, (label, choice)) in choices.into_iter().enumerate() {
                let text = if index == 0 { RichText::new(label).strong() } else { RichText::new(label) };
                let response = ui.add(Button::new(text));
                if index == 0 && focus {
                    response.request_focus();
                }
                if response.clicked() {
                    answer = Some(choice);
                }
            }
        });
    });
    // Esc or a click outside keeps everything as it is.
    if answer.is_none() && modal.should_close() {
        answer = Some(None);
    }
    if let Some(choice) = answer {
        app.maintenance.question = None;
        if let Some((hud_layout, allow_downgrade)) = choice {
            start(app, question.kind, hud_layout, allow_downgrade);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn results_become_the_summary_the_user_reads() {
        let texts = Texts::default();
        let updated = json!({"status": "updated", "present": {"version": "0.8.1"}, "config_changes": []});
        assert_eq!(summary(Kind::Update, &updated, &texts), "Updated to 0.8.1. Your saves and settings were kept.");
        let reset = json!({"status": "updated", "present": {"version": null}, "config_changes": [
            {"file": "config/topscreen_ui.json", "change": "standard_hud_layout", "fields": ["hud_margin_x"]},
            {"file": "config/TriAevum.json", "change": "created"}]});
        assert_eq!(
            summary(Kind::Update, &reset, &texts),
            "Updated. Your saves were kept.\n\nUpdate changed: the HUD layout was reset to the standard profile; \
             config/TriAevum.json was created with its defaults"
        );
        let repaired = json!({"status": "repaired", "config_changes": []});
        assert_eq!(summary(Kind::Repair, &repaired, &texts), "Repaired. Your saves and settings were kept.");
        let current = json!({"status": "unchanged", "reason": "current"});
        assert_eq!(summary(Kind::Update, &current, &texts), "This installation already uses this package.");
    }

    #[test]
    fn an_older_package_becomes_a_question() {
        let texts = Texts::default();
        let result = json!({"status": "confirmation_required", "reason": "downgrade",
                            "installed_version": "0.8.1", "present_version": "0.7.9"});
        let Event::MaintenanceQuestion(question) =
            outcome(Kind::Update, HudLayout::Standard, &result, Vec::new(), &texts)
        else {
            panic!("not a question")
        };
        assert_eq!(question.ask, Ask::Downgrade { installed: "0.8.1".into(), present: "0.7.9".into() });
        assert_eq!(question.hud_layout, HudLayout::Standard, "the layout choice is kept");
        assert_eq!(
            texts.get(Msg::DowngradeBody { installed: "0.8.1", present: "0.7.9" }),
            "This package (0.7.9) is older than the installed one (0.8.1). Install the older version anyway?"
        );
    }
}
