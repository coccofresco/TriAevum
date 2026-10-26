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
//!
//! Update from ZIP (UX plan WP-04B, Windows): Home's "Update from a
//! downloaded ZIP..." plans the update (`update_plan`: signature, versions,
//! what changes), shows the preview, asks decision 4 when it matters, then
//! runs `update`, which hands off to the update helper; the window closes
//! and the helper reopens Forge, which shows the result the helper left.
//! Repair of an interrupted update runs `update_recover`.

use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use eframe::egui::{self, Button, Label, RichText};
use forge_base::CancelToken;
use forge_core::config_report::{hud_layout_preview, LayoutField};
use forge_core::explain::{explain, Explanation};
use forge_core::publish::HudLayout;
use serde_json::{json, Value};

use crate::app::{DialogKind, ForgeApp};
use crate::install::{counts, job_update_in, Event, JobUpdate, Notifier};
use crate::texts::{Msg, Texts};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Update,
    Repair,
    /// Update from a downloaded ZIP (WP-04B): planned, then handed off.
    ZipUpdate,
    /// Finish or undo an interrupted ZIP update.
    RecoverUpdate,
    /// The Enhancements page's "Apply changes" (WP-07): the repair in place
    /// with the current choices, after the downloads the choices need.
    ApplyEnhancements,
}

impl Kind {
    fn job(self, planned: bool) -> &'static str {
        match self {
            Self::Update => "refresh",
            Self::Repair => "repair",
            Self::ZipUpdate if planned => "update",
            Self::ZipUpdate => "update_plan",
            Self::RecoverUpdate => "update_recover",
            Self::ApplyEnhancements => "apply_enhancements",
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
    /// WP-04B: the planned ZIP update's preview (`update_plan`'s summary).
    ZipPreview(Value),
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
    /// The ZIP (or package folder) of the ZIP update in progress.
    zip_source: Option<PathBuf>,
    /// The user confirmed the preview: the next ZIP job is `update`.
    zip_planned: bool,
    /// Decision 10 answered for this ZIP.
    zip_allow_downgrade: bool,
    /// Set by the job when the helper took over: the window closes.
    handed_off: Arc<AtomicBool>,
    /// The first frame's checks ran (the last update's result, leftovers).
    started: bool,
}

/// `--update-target OLD`: the package this Forge runs from updates OLD.
static UPDATE_FROM_OWN_PACKAGE: std::sync::OnceLock<PathBuf> = std::sync::OnceLock::new();

/// Called by `main` before the window opens for `--update-target`.
pub fn update_from_package(package: PathBuf) {
    let _ = UPDATE_FROM_OWN_PACKAGE.set(package);
}

/// The job request for `kind`.
fn request(kind: Kind, app: &ForgeApp, hud_layout: HudLayout, allow_downgrade: bool) -> Value {
    let mut request = json!({
        "format": forge_core::jobs::REQUEST_FORMAT,
        "job": kind.job(app.maintenance.zip_planned),
        "data_root": app.installation.data_root.to_string_lossy(),
        "package_root": app.installation.root.to_string_lossy(),
        "hud_layout": hud_layout.code(),
    });
    match kind {
        Kind::Update => request["allow_downgrade"] = json!(allow_downgrade),
        Kind::ZipUpdate => {
            let source = app.maintenance.zip_source.as_deref().map(|path| path.to_string_lossy().into_owned());
            request["source"] = json!({"path": source});
            request["allow_downgrade"] = json!(allow_downgrade);
            request["relaunch"] = json!(true);
        }
        Kind::Repair | Kind::RecoverUpdate | Kind::ApplyEnhancements => {}
    }
    request
}

/// Home's Update or Repair: asks decision 4 first when the layout differs.
/// Repair of an interrupted ZIP update finishes or undoes that update.
pub fn begin(app: &mut ForgeApp, kind: Kind) {
    if !app.idle() {
        return;
    }
    if kind == Kind::Repair && forge_core::update_gate::pending(&app.installation.layout.activation) {
        return start(app, Kind::RecoverUpdate, HudLayout::Keep, false);
    }
    if kind == Kind::ZipUpdate {
        return start(app, kind, HudLayout::Keep, app.maintenance.zip_allow_downgrade);
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

/// Home's "Update from a downloaded ZIP...": the file, then the plan.
pub fn begin_zip(app: &mut ForgeApp) {
    if !app.idle() {
        return;
    }
    let picked =
        rfd::FileDialog::new().set_title(app.t(Msg::PickUpdateZipTitle)).add_filter("ZIP", &["zip"]).pick_file();
    if let Some(zip) = picked {
        choose_source(app, zip);
    }
}

fn choose_source(app: &mut ForgeApp, source: PathBuf) {
    app.maintenance.zip_source = Some(source);
    app.maintenance.zip_planned = false;
    app.maintenance.zip_allow_downgrade = false;
    begin(app, Kind::ZipUpdate);
}

/// The first frame: the result the update helper left for this window,
/// leftovers of an earlier update, and `--update-target`.
fn startup(app: &mut ForgeApp) {
    app.maintenance.started = true;
    if !cfg!(windows) || app.freeze_state {
        return;
    }
    let activation = app.installation.layout.activation.clone();
    if !forge_core::update_gate::pending(&activation) {
        forge_core::update::cleanup_stale(&activation);
    }
    if let Some(result) = forge_core::update::take_result(&activation) {
        let (kind, message) = result_message(&Value::Object(result), &app.texts);
        app.push_dialog(kind, message);
    }
    if let Some(package) = UPDATE_FROM_OWN_PACKAGE.get() {
        choose_source(app, package.clone());
    }
}

/// What the helper's result (`triaevum_update_result_v1`) tells the user.
fn result_message(result: &Value, texts: &Texts) -> (DialogKind, String) {
    let error = result["error"].as_str().unwrap_or_default();
    match result["status"].as_str() {
        Some("updated") => {
            let summary = json!({"status": "updated", "present": {"version": result["version"]},
                                 "config_changes": result["config_changes"]});
            (DialogKind::Info, summary_text(Kind::Update, &summary, texts))
        }
        Some("rolled_back") => (DialogKind::Error, texts.get(Msg::UpdateRolledBack { error })),
        Some("refused") => (DialogKind::Error, texts.get(Msg::UpdateRefused { error })),
        _ => (DialogKind::Error, texts.get(Msg::UpdateRecoveryNeeded { error })),
    }
}

fn start(app: &mut ForgeApp, kind: Kind, hud_layout: HudLayout, allow_downgrade: bool) {
    let request = request(kind, app, hud_layout, allow_downgrade);
    let texts = app.texts.clone();
    let stage = app.t(match kind {
        Kind::Update => Msg::Updating,
        Kind::Repair | Kind::RecoverUpdate => Msg::Repairing,
        Kind::ZipUpdate if app.maintenance.zip_planned => Msg::Updating,
        Kind::ZipUpdate => Msg::CheckingUpdatePackage,
        Kind::ApplyEnhancements => Msg::ApplyingEnhancements,
    });
    app.maintenance.last = Some(kind);
    let handed_off = Arc::clone(&app.maintenance.handed_off);
    let (hd_enabled, mod_enabled) = (app.hd_auto_download, app.mod_enabled);
    app.start_task(stage, move |optional, notify, cancel| {
        if kind == Kind::ApplyEnhancements {
            crate::install::download_missing(&optional, hd_enabled, mod_enabled, &texts, &notify, &cancel);
            if cancel.is_cancelled() {
                return notify.send(Event::InstallCancelled);
            }
        }
        run(kind, hud_layout, &request, &texts, &notify, &cancel, &handed_off)
    });
}

/// Runs the job; its events become window events.
fn run(
    kind: Kind,
    hud_layout: HudLayout,
    request: &Value,
    texts: &Texts,
    notify: &Notifier,
    cancel: &CancelToken,
    handed_off: &AtomicBool,
) {
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
                if event["result"]["status"] == "handed_off" {
                    // The helper waits for this process to end.
                    handed_off.store(true, Ordering::SeqCst);
                    return notify.send(Event::Stage(texts.get(Msg::UpdateHandingOff)));
                }
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
    if kind == Kind::ZipUpdate && result["status"] == "ready" {
        return Event::MaintenanceQuestion(Question { kind, ask: Ask::ZipPreview(result.clone()), hud_layout });
    }
    if kind == Kind::RecoverUpdate {
        let message = match result["recovered"]["status"].as_str() {
            None => texts.get(Msg::AlreadyCurrent),
            Some("updated") => texts.get(Msg::UpdateCompleted),
            Some(_) => texts.get(Msg::UpdateRecovered),
        };
        return Event::MaintenanceDone { message, notices };
    }
    Event::MaintenanceDone { message: summary_text(kind, result, texts), notices }
}

/// "Updated to 0.8.1. Your saves and settings were kept." and, when the
/// settings changed beyond creating missing files, "Update changed: ...".
fn summary_text(kind: Kind, result: &Value, texts: &Texts) -> String {
    let changes = result["config_changes"].as_array().map(Vec::as_slice).unwrap_or_default();
    let changed: Vec<String> = changes.iter().map(|change| change_text(change, texts)).collect();
    let kept = changes.iter().all(|change| change["change"] == "created");
    let version = result["present"]["version"].as_str();
    let repair = matches!(kind, Kind::Repair | Kind::RecoverUpdate);
    let mut message = match (kind, repair, result["status"].as_str()) {
        (Kind::ApplyEnhancements, _, _) => texts.get(Msg::EnhancementsApplied { kept }),
        (_, false, Some("unchanged")) => texts.get(Msg::AlreadyCurrent),
        (_, false, _) => texts.get(Msg::Updated { version: version.unwrap_or_default(), kept }),
        (_, true, _) => texts.get(Msg::Repaired { kept }),
    };
    if !changed.is_empty() {
        let list = changed.join("; ");
        message.push_str("\n\n");
        message.push_str(&texts.get(match kind {
            Kind::ApplyEnhancements => Msg::ApplyChanged { list: &list },
            _ if repair => Msg::RepairChanged { list: &list },
            _ => Msg::UpdateChanged { list: &list },
        }));
    }
    message
}

/// The ZIP preview: versions, signature, locally changed package files.
fn preview_text(summary: &Value, texts: &Texts) -> String {
    let field = |key: &str| summary[key].as_str().unwrap_or("?").to_string();
    let (installed, version) = (field("previous_version"), field("version"));
    let mut lines = vec![texts.get(Msg::ZipPreviewBody { installed: &installed, version: &version })];
    lines.push(match summary["authenticity"]["status"].as_str() {
        Some("signed") => texts.get(Msg::ZipSigned { key: summary["authenticity"]["key_id"].as_str().unwrap_or("?") }),
        _ => texts.get(Msg::ZipUnsigned),
    });
    let modified: Vec<&str> = summary["locally_modified"]
        .as_array()
        .map(|items| items.iter().filter_map(Value::as_str).collect())
        .unwrap_or_default();
    if !modified.is_empty() {
        lines.push(texts.get(Msg::ZipLocallyModified { list: &modified.join(", ") }));
    }
    lines.join("\n\n")
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
/// Also the window's ZIP-update duties: the first frame's checks, and
/// closing once the update helper took over.
pub fn show(app: &mut ForgeApp, context: &egui::Context) {
    if !app.maintenance.started {
        startup(app);
    }
    if app.maintenance.handed_off.swap(false, Ordering::SeqCst) {
        app.close(context);
        return;
    }
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
            Ask::ZipPreview(summary) => {
                let version = summary["version"].as_str().unwrap_or("?");
                let choices = vec![
                    (texts.get(Msg::ButtonUpdateNow), Some((question.hud_layout, app.maintenance.zip_allow_downgrade))),
                    (texts.get(Msg::ButtonCancel), None),
                ];
                (texts.get(Msg::ZipPreviewHeading { version }), preview_text(summary, &texts), choices)
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
        let Some((hud_layout, allow_downgrade)) = choice else { return };
        if question.kind == Kind::ZipUpdate {
            match question.ask {
                // Decision 10 answered: plan again with the older package.
                Ask::Downgrade { .. } => {
                    app.maintenance.zip_allow_downgrade = true;
                    return start(app, Kind::ZipUpdate, hud_layout, true);
                }
                // The preview confirmed: decision 4, then the update itself.
                Ask::ZipPreview(_) => {
                    app.maintenance.zip_planned = true;
                    let fields = hud_layout_preview(&app.installation.data_root).unwrap_or_default();
                    if !fields.is_empty() {
                        let ask = Ask::Layout(fields);
                        return self::ask(app, Question { kind: Kind::ZipUpdate, ask, hud_layout });
                    }
                }
                Ask::Layout(_) => {}
            }
            return start(app, Kind::ZipUpdate, hud_layout, app.maintenance.zip_allow_downgrade);
        }
        start(app, question.kind, hud_layout, allow_downgrade);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn results_become_the_summary_the_user_reads() {
        let texts = Texts::default();
        let updated = json!({"status": "updated", "present": {"version": "0.8.1"}, "config_changes": []});
        assert_eq!(
            summary_text(Kind::Update, &updated, &texts),
            "Updated to 0.8.1. Your saves and settings were kept."
        );
        let reset = json!({"status": "updated", "present": {"version": null}, "config_changes": [
            {"file": "config/topscreen_ui.json", "change": "standard_hud_layout", "fields": ["hud_margin_x"]},
            {"file": "config/TriAevum.json", "change": "created"}]});
        assert_eq!(
            summary_text(Kind::Update, &reset, &texts),
            "Updated. Your saves were kept.\n\nUpdate changed: the HUD layout was reset to the standard profile; \
             config/TriAevum.json was created with its defaults"
        );
        let repaired = json!({"status": "repaired", "config_changes": []});
        assert_eq!(summary_text(Kind::Repair, &repaired, &texts), "Repaired. Your saves and settings were kept.");
        let current = json!({"status": "unchanged", "reason": "current"});
        assert_eq!(summary_text(Kind::Update, &current, &texts), "This installation already uses this package.");
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

    #[test]
    fn a_zip_update_is_planned_then_installed_and_its_result_is_told() {
        assert_eq!((Kind::ZipUpdate.job(false), Kind::ZipUpdate.job(true)), ("update_plan", "update"));
        assert_eq!(Kind::RecoverUpdate.job(false), "update_recover");
        let texts = Texts::default();
        let ready = json!({"status": "ready", "previous_version": "0.8.0", "version": "0.8.1",
                           "authenticity": {"status": "signed", "key_id": "0123456789abcdef"},
                           "locally_modified": ["docs/shared.md"]});
        let Event::MaintenanceQuestion(question) =
            outcome(Kind::ZipUpdate, HudLayout::Keep, &ready, Vec::new(), &texts)
        else {
            panic!("the plan becomes the preview")
        };
        assert_eq!(question.ask, Ask::ZipPreview(ready.clone()));
        let preview = preview_text(&ready, &texts);
        assert!(preview.contains("(0.8.0 becomes 0.8.1)"), "{preview}");
        assert!(preview.contains("(key 0123456789abcdef)"), "{preview}");
        assert!(preview.ends_with("changed on this computer and are replaced: docs/shared.md"), "{preview}");
        let unsigned = json!({"version": "0.8.1", "authenticity": {"status": "unsigned_developer_override"}});
        assert!(preview_text(&unsigned, &texts).contains("not signed by the TriAevum publisher"));

        let updated = json!({"status": "updated", "version": "0.8.1", "config_changes": []});
        assert_eq!(
            result_message(&updated, &texts),
            (DialogKind::Info, "Updated to 0.8.1. Your saves and settings were kept.".to_string())
        );
        let rolled_back =
            json!({"status": "rolled_back", "error": "The new version could not bind your prepared game"});
        let (kind, message) = result_message(&rolled_back, &texts);
        assert_eq!(kind, DialogKind::Error);
        assert!(message.starts_with("The update did not finish, so the previous version was restored."));
        assert!(message.ends_with("could not bind your prepared game"));
        let refused = json!({"status": "refused", "error": "Close TriAevum before updating"});
        assert!(result_message(&refused, &texts).1.starts_with("The update did not start; nothing was changed."));
        let Event::MaintenanceDone { message, .. } = outcome(
            Kind::RecoverUpdate,
            HudLayout::Keep,
            &json!({"recovered": {"status": "rolled_back"}}),
            Vec::new(),
            &texts,
        ) else {
            panic!("a recovery ends")
        };
        assert_eq!(message, "The interrupted update was undone; the previous version is back.");
    }
}
