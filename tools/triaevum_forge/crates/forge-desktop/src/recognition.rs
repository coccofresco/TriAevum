// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The recognized-ROM card (a Rust addition): as soon as a ROM is chosen,
//! the forge-core `rom_recognize` job names its revision and the game
//! languages its RomFS offers, off the window thread. The language picked
//! there is passed to "Prepare and install"; once the title is installed the
//! same buttons change the saved setting (`language::select`).

use std::path::Path;

use forge_base::CancelToken;
use serde_json::{json, Value};

use crate::install::{job_update, Event, JobUpdate, Notifier};
use crate::texts;

/// What the card shows for one chosen ROM.
#[derive(Clone, Debug, PartialEq)]
pub enum Card {
    /// Recognition runs; the text is its current stage.
    Recognizing(String),
    Recognized(RecognizedRom),
    Failed(String),
}

#[derive(Clone, Debug, PartialEq)]
pub struct RecognizedRom {
    pub label: String,
    /// `(code, label)` in the runtime's order; empty when unreadable.
    pub languages: Vec<(String, String)>,
    pub selected: Option<usize>,
    /// Why the languages could not be read; the installation reads them again.
    pub language_error: Option<String>,
}

impl RecognizedRom {
    /// The `rom_recognize` result.
    pub fn from_result(result: &Value) -> Self {
        let text = |key: &str| result.get(key).and_then(Value::as_str).map(str::to_string);
        let (languages, selected) = match result.get("languages").and_then(Value::as_object) {
            Some(document) => forge_core::recognize::language_choices(document),
            None => (Vec::new(), None),
        };
        let selected = languages.iter().position(|(code, _)| Some(code) == selected.as_ref());
        Self { label: text("label").unwrap_or_default(), languages, selected, language_error: text("language_error") }
    }

    /// The code chosen for the installation.
    pub fn chosen(&self) -> Option<&str> {
        self.selected.and_then(|index| self.languages.get(index)).map(|(code, _)| code.as_str())
    }
}

/// The `rom_recognize` request for a ROM file or an extracted-data folder.
pub fn recognize_request(source: &Path, data_root: &Path, package_root: &Path) -> Value {
    let source = forge_prepare::paths::resolve(source).unwrap_or_else(|_| source.to_path_buf());
    json!({
        "format": forge_core::jobs::REQUEST_FORMAT,
        "job": "rom_recognize",
        "source": {"path": source.to_string_lossy()},
        "data_root": data_root.to_string_lossy(),
        "package_root": package_root.to_string_lossy(),
    })
}

/// Runs one recognition and reports the card for `source` after each stage.
/// `own` stops it when another ROM is chosen, `cancel` when Forge closes.
pub fn run_recognition(source: String, request: Value, notify: Notifier, cancel: CancelToken, own: CancelToken) {
    let send = |card: Card| notify.send(Event::Recognition { source: source.clone(), card });
    let job = match forge_core::jobs::start(&forge_base::legacy_json::compact_line_bytes(&request)) {
        Ok(job) => job,
        Err(error) => return send(Card::Failed(error.message)),
    };
    let mut result = None;
    loop {
        if cancel.is_cancelled() || own.is_cancelled() {
            job.cancel();
        }
        let Some(event) = job.poll(100) else { continue };
        if event.get("event").and_then(Value::as_str) == Some("succeeded") {
            result = event.get("result").cloned();
        }
        match job_update(&event) {
            Some(JobUpdate::Stage(message)) => send(Card::Recognizing(message)),
            Some(JobUpdate::Succeeded) => {
                return send(Card::Recognized(RecognizedRom::from_result(&result.unwrap_or(Value::Null))));
            }
            Some(JobUpdate::Failed(message)) => return send(Card::Failed(message)),
            Some(JobUpdate::Cancelled) => return,
            Some(JobUpdate::Progress { .. }) | None => {}
        }
    }
}

/// The card heading.
pub fn heading(card: &Card) -> String {
    match card {
        Card::Recognizing(_) => texts::RECOGNIZING.to_string(),
        Card::Recognized(rom) => texts::rom_recognized(&rom.label),
        Card::Failed(_) => texts::ROM_NOT_RECOGNIZED.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn results_become_language_choices() {
        let result = json!({"recipe": "r", "label": "Game (EUR)", "language_error": null, "languages": {
            "format": "triaevum_game_language_v1", "selected": "it", "available": [
                {"code": "en", "label": "English", "system_id": 1},
                {"code": "it", "label": "Italiano", "system_id": 4}]}});
        let rom = RecognizedRom::from_result(&result);
        assert_eq!(rom.label, "Game (EUR)");
        assert_eq!(rom.selected, Some(1));
        assert_eq!(rom.chosen(), Some("it"));
        assert_eq!(rom.language_error, None);
        let unreadable = RecognizedRom::from_result(&json!({"label": "G", "languages": null, "language_error": "x"}));
        assert!(unreadable.languages.is_empty());
        assert_eq!((unreadable.chosen(), unreadable.language_error.as_deref()), (None, Some("x")));
        assert_eq!(heading(&Card::Recognized(rom)), "ROM recognized: Game (EUR)");
    }

    #[test]
    fn request_follows_the_job_contract() {
        let request = recognize_request(Path::new("game.cci"), Path::new("d"), Path::new("p"));
        assert_eq!(request["job"], "rom_recognize");
        assert!(request["source"]["path"].as_str().unwrap().ends_with("game.cci"));
        assert_eq!(request.get("source_kind"), None);
    }

    #[test]
    fn an_unrecognized_rom_reports_a_failed_card() {
        let directory = tempfile::tempdir().unwrap();
        let rom = directory.path().join("missing.cci");
        let request = recognize_request(&rom, directory.path(), directory.path());
        let (sender, receiver) = std::sync::mpsc::channel();
        let source = rom.to_string_lossy().into_owned();
        run_recognition(source.clone(), request, Notifier::new(sender, None), CancelToken::new(), CancelToken::new());
        let events: Vec<Event> = receiver.try_iter().collect();
        assert!(
            matches!(events.last(), Some(Event::Recognition { source: s, card: Card::Failed(message) })
                if *s == source && message.starts_with("the selected ROM does not exist")),
            "{events:?}"
        );
    }
}
