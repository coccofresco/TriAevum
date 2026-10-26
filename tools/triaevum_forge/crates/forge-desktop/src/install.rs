// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! "Prepare and install" off the window thread (`ForgeWindow._install`): the
//! optional downloads the window promised, then the forge-core `install` job
//! (FORGE_JOBS.md), whose events become window events.

use std::path::{Path, PathBuf};
use std::sync::mpsc::Sender;
use std::sync::Arc;

use forge_base::CancelToken;
use forge_core::explain::{explain, explain_value, Explanation};
use forge_mods::Status;
use serde_json::{json, Value};

use crate::mods::Optional;
use crate::texts::{Msg, Texts};

pub type Checked = std::result::Result<Status, String>;

/// What a background task tells the window.
#[derive(Debug, PartialEq)]
pub enum Event {
    Selection(crate::app::PickTarget, Option<PathBuf>),
    Stage(String),
    Progress {
        done: u64,
        total: Option<u64>,
    },
    HdStage(String),
    ModStage(String),
    /// The automatic download before a preparation failed; the native files are used.
    HdFailed(String),
    ModFailed(String),
    HdChecked(Checked),
    ModChecked(Checked),
    HdDone {
        error: Option<String>,
        state: Checked,
    },
    ModDone {
        error: Option<String>,
        state: Checked,
    },
    InstallFailed(Box<Explanation>),
    InstallComplete {
        hd: Checked,
        mod_: Checked,
        /// Non-error outcomes of the installation (graphics preparation incomplete).
        notices: Vec<Explanation>,
    },
    InstallCancelled,
    /// The recognized-ROM card for the ROM path `source`.
    Recognition {
        source: String,
        card: crate::recognition::Card,
    },
    /// The inspected installation state.
    State(Box<forge_core::state::InstallationState>),
    /// A game started from Play ended; the explanation of an error exit.
    GameExited(Option<Box<Explanation>>),
    /// The Diagnostics page's facts, or why they could not be collected.
    Diagnostics(Result<Box<crate::pages::diagnostics::DiagnosticsView>, String>),
    /// Where the support bundle was saved, or why it was not.
    BundleSaved(Result<PathBuf, String>),
}

/// Sends events to the window and wakes it.
#[derive(Clone)]
pub struct Notifier {
    sender: Sender<Event>,
    repaint: Option<eframe::egui::Context>,
}

impl Notifier {
    pub fn new(sender: Sender<Event>, repaint: Option<eframe::egui::Context>) -> Self {
        Self { sender, repaint }
    }

    pub fn send(&self, event: Event) {
        let _ = self.sender.send(event);
        if let Some(context) = &self.repaint {
            context.request_repaint();
        }
    }
}

/// What the window decided when the user pressed "Prepare and install".
pub struct InstallPlan {
    pub source: PathBuf,
    pub data_root: PathBuf,
    pub package_root: PathBuf,
    pub hd_auto_download: bool,
    pub mod_enabled: bool,
    /// The language chosen on the recognized-ROM card.
    pub game_language: Option<String>,
    /// The window's language, for the texts the task reports.
    pub texts: Texts,
}

/// The `install` job request. The window downloads the HD textures itself
/// first (with its own status line), so the job is told not to.
pub fn install_request(source: &Path, data_root: &Path, package_root: &Path, game_language: Option<&str>) -> Value {
    let source = forge_prepare::paths::resolve(source).unwrap_or_else(|_| source.to_path_buf());
    let kind = if source.is_dir() { "extracted_directory" } else { "rom" };
    let mut request = json!({
        "format": forge_core::jobs::REQUEST_FORMAT,
        "job": "install",
        "source": {"path": source.to_string_lossy()},
        "source_kind": kind,
        "data_root": data_root.to_string_lossy(),
        "package_root": package_root.to_string_lossy(),
        "download_hd_textures": false,
    });
    if let Some(code) = game_language {
        request["game_language"] = json!(code);
    }
    request
}

/// A job event as the window shows it.
#[derive(Debug, PartialEq)]
pub enum JobUpdate {
    Stage(String),
    Progress {
        done: u64,
        total: Option<u64>,
    },
    /// With the explanations of the event's notices.
    Succeeded(Vec<Explanation>),
    Failed(Box<Explanation>),
    Cancelled,
}

/// The counts of a stage text that ends with `done/total`: the device
/// pipeline helper reports "Preparing GPU pipelines: 120/400" as text
/// (`forge-native/src/desktop.rs`); the window shows it as progress.
pub fn counts(message: &str) -> Option<(u64, u64)> {
    let tail = message.rsplit([' ', ':']).next()?;
    let (done, total) = tail.split_once('/')?;
    let (done, total) = (done.parse().ok()?, total.parse().ok()?);
    (total > 0 && done <= total).then_some((done, total))
}

pub fn job_update(event: &Value) -> Option<JobUpdate> {
    let message = || event.get("message").and_then(Value::as_str).unwrap_or_default().to_string();
    match event.get("event")?.as_str()? {
        // Stage texts and non-fatal notes both went to the status line.
        "stage" | "report" => Some(JobUpdate::Stage(message())),
        "progress" => Some(JobUpdate::Progress {
            done: event.get("done").and_then(Value::as_u64).unwrap_or(0),
            total: event.get("total").and_then(Value::as_u64),
        }),
        "succeeded" => {
            let notices = event.get("notices").and_then(Value::as_array).map(Vec::as_slice).unwrap_or_default();
            Some(JobUpdate::Succeeded(notices.iter().filter_map(explain_value).collect()))
        }
        "failed" => explain_value(event).map(|explanation| JobUpdate::Failed(Box::new(explanation))),
        "cancelled" => Some(JobUpdate::Cancelled),
        _ => None,
    }
}

pub fn run_install(optional: Arc<Optional>, plan: InstallPlan, notify: Notifier, cancel: CancelToken) {
    let download_hd =
        plan.hd_auto_download && optional.hd_contract.is_some() && optional.hd_state(&cancel) == Ok(Status::Missing);
    let download_mod =
        plan.mod_enabled && optional.mod_contract.is_some() && optional.mod_state(&cancel) == Ok(Status::Missing);
    let texts = &plan.texts;
    if download_hd {
        notify.send(Event::Stage(texts.get(Msg::DownloadingHdFirst)));
    } else if download_mod {
        notify.send(Event::Stage(texts.get(Msg::DownloadingModFirst)));
    }
    // A failed download keeps the native files and never stops the installation.
    if download_hd && !cancel.is_cancelled() {
        notify.send(Event::HdStage(texts.get(Msg::DownloadingHd)));
        let mut report = |text: String| notify.send(Event::HdStage(text));
        match optional.download_hd(&mut report, &cancel) {
            Ok(()) => notify.send(Event::HdStage(texts.get(Msg::HdArchiveApplied))),
            Err(error) => notify.send(Event::HdFailed(texts.get(Msg::HdDownloadFailed { error: &error.message }))),
        }
        notify.send(Event::Stage(texts.get(Msg::Starting)));
    }
    if download_mod && !cancel.is_cancelled() {
        let mut report = |text: String| notify.send(Event::ModStage(text));
        if let Err(error) = optional.download_mod(&mut report, &cancel) {
            notify.send(Event::ModFailed(texts.get(Msg::ModDownloadFailed { error: &error.message })));
        }
        notify.send(Event::Stage(texts.get(Msg::Starting)));
    }
    if cancel.is_cancelled() {
        notify.send(Event::InstallCancelled);
        return;
    }
    let request = install_request(&plan.source, &plan.data_root, &plan.package_root, plan.game_language.as_deref());
    let job = match forge_core::jobs::start(&forge_base::legacy_json::compact_line_bytes(&request)) {
        Ok(job) => job,
        Err(error) => {
            notify.send(Event::InstallFailed(Box::new(explain(&error))));
            return;
        }
    };
    loop {
        if cancel.is_cancelled() {
            job.cancel();
        }
        let Some(update) = job.poll(100).as_ref().and_then(job_update) else { continue };
        match update {
            JobUpdate::Stage(message) => {
                let counted = counts(&message);
                notify.send(Event::Stage(message));
                if let Some((done, total)) = counted {
                    notify.send(Event::Progress { done, total: Some(total) });
                }
            }
            JobUpdate::Progress { done, total } => notify.send(Event::Progress { done, total }),
            JobUpdate::Succeeded(notices) => {
                let check = CancelToken::new();
                let (hd, mod_) = (optional.hd_state(&check), optional.mod_state(&check));
                notify.send(Event::InstallComplete { hd, mod_, notices });
                return;
            }
            JobUpdate::Failed(explanation) => {
                notify.send(Event::InstallFailed(explanation));
                return;
            }
            JobUpdate::Cancelled => {
                notify.send(Event::InstallCancelled);
                return;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::installation::Installation;

    #[test]
    fn request_follows_the_job_contract() {
        let directory = tempfile::tempdir().unwrap();
        let rom = directory.path().join("game.cci");
        std::fs::write(&rom, b"").unwrap();
        let request = install_request(&rom, Path::new("D:/data"), Path::new("D:/pkg"), None);
        assert_eq!(request["format"], "triaevum_forge_request_v1");
        assert_eq!(request["job"], "install");
        assert_eq!(request["source_kind"], "rom");
        assert_eq!(request["download_hd_textures"], false);
        assert_eq!(request["data_root"], Path::new("D:/data").to_string_lossy().as_ref());
        assert_eq!(request.get("game_language"), None);
        let folder = install_request(directory.path(), Path::new("d"), Path::new("p"), Some("it"));
        assert_eq!(folder["source_kind"], "extracted_directory");
        assert_eq!(folder["game_language"], "it");
    }

    #[test]
    fn pipeline_counts_in_stage_texts_become_progress() {
        assert_eq!(counts("Preparing GPU pipelines: 120/400"), Some((120, 400)));
        assert_eq!(counts("Preparing GPU pipelines: 0/0"), None);
        assert_eq!(counts("Extracting the ROM"), None);
        assert_eq!(counts("Copied 3/x"), None);
    }

    #[test]
    fn job_events_map_to_window_updates() {
        assert_eq!(
            job_update(&json!({"event": "stage", "stage": "verify", "message": "Identifying"})),
            Some(JobUpdate::Stage("Identifying".into()))
        );
        assert_eq!(
            job_update(&json!({"event": "progress", "stage": "extract", "done": 5, "total": null})),
            Some(JobUpdate::Progress { done: 5, total: None })
        );
        let Some(JobUpdate::Failed(failed)) =
            job_update(&json!({"event": "failed", "error": {"code": "invalid_input", "message": "bad ROM"}}))
        else {
            panic!("not a failure")
        };
        assert_eq!((failed.id, failed.details.as_str()), ("invalid_input", "bad ROM"));
        assert_eq!(job_update(&json!({"event": "succeeded", "result": {}})), Some(JobUpdate::Succeeded(Vec::new())));
        let notice =
            json!({"outcome": "graphics_preparation_incomplete", "details": "GPU pipeline preparation: failed"});
        let Some(JobUpdate::Succeeded(notices)) =
            job_update(&json!({"event": "succeeded", "result": {}, "notices": [notice]}))
        else {
            panic!("not a success")
        };
        assert_eq!(notices[0].id, "outcome.graphics_preparation_incomplete");
        assert_eq!(job_update(&json!({"event": "other"})), None);
    }

    /// The shared explanation fixtures, as the window receives them: a failed
    /// event becomes the dialog's explanation, a notice a line of the ready dialog.
    #[test]
    fn shared_fixtures_reach_the_window_with_their_explanations() {
        let document: Value =
            serde_json::from_str(include_str!("../../forge-core/tests/fixtures/explain_events.json")).unwrap();
        for case in document["cases"].as_array().unwrap() {
            let input = &case["input"];
            let expected = case["id"].as_str();
            let shown = match input.get("event").and_then(Value::as_str) {
                Some("failed") => match job_update(input) {
                    Some(JobUpdate::Failed(explanation)) => Some(*explanation),
                    other => panic!("{}: {other:?}", case["name"]),
                },
                Some("cancelled") => {
                    assert_eq!(job_update(input), Some(JobUpdate::Cancelled));
                    continue;
                }
                Some(_) => {
                    assert!(!matches!(job_update(input), Some(JobUpdate::Failed(_))));
                    None
                }
                None if input.get("outcome").is_some() => {
                    let event = json!({"event": "succeeded", "result": {}, "notices": [input]});
                    match job_update(&event) {
                        Some(JobUpdate::Succeeded(mut notices)) => notices.pop(),
                        other => panic!("{}: {other:?}", case["name"]),
                    }
                }
                None => continue,
            };
            assert_eq!(shown.as_ref().map(|explanation| explanation.id), expected, "{}", case["name"]);
            if let Some(explanation) = shown {
                let reference = forge_core::explain::explain_value(input).unwrap();
                assert_eq!(explanation, reference, "{}", case["name"]);
                assert!(explanation.body().contains(explanation.next_action));
            }
        }
    }

    /// Until forge-core accepts `install`, the refusal reaches the window as
    /// a failure; afterwards the job's own terminal event does.
    #[test]
    fn install_always_ends_with_one_terminal_event() {
        let directory = tempfile::tempdir().unwrap();
        let installation = Installation::at(directory.path()).unwrap();
        let (optional, _) = Optional::load(&installation);
        let (sender, receiver) = std::sync::mpsc::channel();
        let plan = InstallPlan {
            source: directory.path().join("missing.cci"),
            data_root: installation.data_root.clone(),
            package_root: installation.root.clone(),
            hd_auto_download: true,
            mod_enabled: false,
            game_language: None,
            texts: Texts::default(),
        };
        run_install(Arc::new(optional), plan, Notifier::new(sender, None), CancelToken::new());
        let events: Vec<Event> = receiver.try_iter().collect();
        let terminal = events.last().unwrap();
        assert!(
            matches!(terminal, Event::InstallFailed(explanation) if !explanation.details.is_empty())
                || matches!(terminal, Event::InstallComplete { .. }),
            "{events:?}"
        );
    }
}
