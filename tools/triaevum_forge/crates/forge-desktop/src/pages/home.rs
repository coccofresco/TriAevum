// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Home: one card for the installation state (forge-core
//! `InstallationState`), the ROM field and the recognized-ROM card when the
//! state needs a ROM, and the primary and secondary actions the footer
//! shows. [`card`] is the pure mapping from state to card.

use std::time::Duration;

use eframe::egui::{self, Button, Label, RichText, Spinner, Stroke, TextEdit, Vec2};
use forge_core::explain::{Action as Remedy, Explanation};
use forge_core::state::InstallationState;

use crate::app::{Action, ForgeApp, PickTarget, Recorder};
use crate::recognition::Card;
use crate::texts::{Msg, Texts};

/// What a card button does.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    Prepare,
    Play,
    Update,
    Repair,
    Cancel,
    CheckInstallation,
    ChooseRom,
    CopyDiagnostics,
    ViewDiagnostics,
    SupportedRevisions,
    DataPage,
    Retry,
    Continue,
}

impl Command {
    pub fn label(self) -> Msg<'static> {
        match self {
            Self::Prepare => Msg::ButtonPrepare,
            Self::Play => Msg::ButtonPlay,
            Self::Update => Msg::ButtonUpdate,
            Self::Repair => Msg::ButtonRepair,
            Self::Cancel => Msg::ButtonCancel,
            Self::CheckInstallation => Msg::ButtonCheckInstallation,
            Self::ChooseRom => Msg::ButtonChooseRom,
            Self::CopyDiagnostics => Msg::ButtonCopyDiagnostics,
            Self::ViewDiagnostics => Msg::ButtonViewDiagnostics,
            Self::SupportedRevisions => Msg::ButtonSupportedRevisions,
            Self::DataPage => Msg::ButtonDataPage,
            Self::Retry => Msg::ButtonRetry,
            Self::Continue => Msg::ButtonContinue,
        }
    }

    /// Prepare, Update, Repair and Retry run the preparation with the
    /// recognized ROM (in-place update and repair are UX plan WP-04).
    fn needs_rom(self) -> bool {
        matches!(self, Self::Prepare | Self::Update | Self::Repair | Self::Retry)
    }
}

/// The button for an explanation's primary action.
pub fn remedy_command(remedy: Remedy) -> Command {
    match remedy {
        Remedy::CheckInput | Remedy::ChooseDecryptedInput | Remedy::ChooseSupportedInput | Remedy::SelectRom => {
            Command::ChooseRom
        }
        Remedy::ViewSupportedRevisions => Command::SupportedRevisions,
        Remedy::CheckInstallation | Remedy::ReturnToInstallation | Remedy::RetrySetting => Command::CheckInstallation,
        Remedy::ChooseCompatiblePackage
        | Remedy::ReviewDetails
        | Remedy::ViewDiagnostics
        | Remedy::OpenRecoveryOptions
        | Remedy::CheckPrerequisites
        | Remedy::ReviewGraphicsRequirements => Command::ViewDiagnostics,
        Remedy::ReviewSettings
        | Remedy::RestoreSettings
        | Remedy::FreeStorage
        | Remedy::ReviewAccess
        | Remedy::ChooseShorterLocation
        | Remedy::ChooseWritableDestination => Command::DataPage,
        Remedy::CloseAndRetry | Remedy::RetryStep => Command::Retry,
        Remedy::CopyDiagnostics => Command::CopyDiagnostics,
        Remedy::ContinueWithNativeAssets | Remedy::ReviewDisplaySettings | Remedy::RetryLanguageDetection => {
            Command::Continue
        }
        Remedy::Play => Command::Play,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CardButton {
    pub command: Command,
    pub enabled: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Checking,
    NotInstalled,
    Preparing,
    PreparingElsewhere,
    Ready,
    UpdateAvailable,
    RepairNeeded,
    NeedsGameInput,
    Incompatible,
    Failed,
}

impl Kind {
    pub fn code(self) -> &'static str {
        match self {
            Self::Checking => "checking",
            Self::NotInstalled => "not_installed",
            Self::Preparing => "preparing",
            Self::PreparingElsewhere => "preparing_elsewhere",
            Self::Ready => "ready",
            Self::UpdateAvailable => "update_available",
            Self::RepairNeeded => "repair_needed",
            Self::NeedsGameInput => "needs_game_input",
            Self::Incompatible => "incompatible",
            Self::Failed => "failed",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tone {
    Neutral,
    Accent,
    Warning,
    Error,
}

/// Where the ROM field is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RomField {
    Hidden,
    Shown,
    /// Behind "Prepare again from a ROM".
    Collapsed,
}

#[derive(Clone, Debug, PartialEq)]
pub struct HomeCard {
    pub kind: Kind,
    pub tone: Tone,
    pub heading: String,
    pub lines: Vec<String>,
    pub rom: RomField,
    /// The game-language card (the recognized ROM's or the installed title's).
    pub languages: bool,
    /// The HD texture line of a preparation still to come.
    pub hd_note: bool,
    /// Details expander and Copy diagnostics content.
    pub explanation: Option<Explanation>,
    /// A failure the user can dismiss to see the installation again.
    pub dismissable: bool,
    pub primary: CardButton,
    pub secondary: Option<CardButton>,
    /// The footer's status line.
    pub status: String,
}

/// A preparation this window runs.
#[derive(Clone, Debug, PartialEq)]
pub struct Preparing<'a> {
    pub stage: &'a str,
    pub elapsed: Duration,
    pub progress: Option<(u64, Option<u64>)>,
}

pub struct HomeInput<'a> {
    pub texts: &'a Texts,
    /// None while the installation is being checked.
    pub state: Option<&'a InstallationState>,
    /// The last operation's failure (install, launch, startup).
    pub failure: Option<&'a Explanation>,
    pub preparing: Option<Preparing<'a>>,
    /// The ROM in the field has a recognized card.
    pub recognized: bool,
    /// No task or picker runs.
    pub idle: bool,
    /// The installed revision's label (the recipe id when unknown).
    pub revision: Option<&'a str>,
    /// The installed language's label.
    pub language: Option<&'a str>,
}

fn button(command: Command, input: &HomeInput) -> CardButton {
    let enabled = match command {
        Command::Cancel => true,
        command if command.needs_rom() => input.recognized && input.idle,
        _ => input.idle,
    };
    CardButton { command, enabled }
}

/// The secondary of a card whose primary comes from an explanation.
fn explanation_secondary(primary: Command, input: &HomeInput) -> Option<CardButton> {
    let command =
        if primary == Command::CopyDiagnostics { Command::CheckInstallation } else { Command::CopyDiagnostics };
    Some(button(command, input))
}

pub fn card(input: &HomeInput) -> HomeCard {
    let t = |msg: Msg| input.texts.get(msg);
    let mut card = HomeCard {
        kind: Kind::Checking,
        tone: Tone::Neutral,
        heading: t(Msg::Checking),
        lines: Vec::new(),
        rom: RomField::Hidden,
        languages: false,
        hd_note: false,
        explanation: None,
        dismissable: false,
        primary: CardButton { command: Command::Prepare, enabled: false },
        secondary: None,
        status: t(Msg::Checking),
    };
    if let Some(preparing) = &input.preparing {
        card.kind = Kind::Preparing;
        card.tone = Tone::Accent;
        card.heading = t(Msg::PreparingHeading);
        let seconds = preparing.elapsed.as_secs();
        card.lines = vec![
            preparing.stage.to_string(),
            t(Msg::PreparingElapsed { minutes: seconds / 60, seconds: seconds % 60 }),
        ];
        // A stage text that carries its counts already says them.
        if let (Some((done, Some(total))), None) = (preparing.progress, crate::install::counts(preparing.stage)) {
            card.lines.push(t(Msg::ProgressCount { done, total }));
        }
        card.primary = button(Command::Cancel, input);
        card.status = preparing.stage.to_string();
        return card;
    }
    // The ROM field and secondary actions of the state below a failure.
    let state = state_card(input, &mut card);
    if let Some(failure) = input.failure.or(match input.state {
        Some(InstallationState::Failed { explanation }) => Some(explanation),
        _ => None,
    }) {
        let primary = remedy_command(failure.primary_action);
        card.kind = Kind::Failed;
        card.tone = Tone::Error;
        card.heading = input.texts.title(failure);
        card.lines = vec![input.texts.cause(failure), input.texts.next_action(failure)];
        card.explanation = Some(failure.clone());
        card.dismissable = input.failure.is_some() && state;
        card.languages &= card.rom == RomField::Shown;
        card.hd_note = false;
        card.primary = button(primary, input);
        card.secondary = explanation_secondary(primary, input);
        card.status = t(Msg::StatusActionNeeded);
    }
    card
}

/// Fills `card` for the installation state; false while it is unknown or
/// the state is itself a failure.
fn state_card(input: &HomeInput, card: &mut HomeCard) -> bool {
    let t = |msg: Msg| input.texts.get(msg);
    let Some(state) = input.state else { return false };
    let explained = |card: &mut HomeCard, explanation: Explanation, extra: Option<Msg>| {
        card.heading = input.texts.title(&explanation);
        card.lines = vec![input.texts.cause(&explanation), input.texts.next_action(&explanation)];
        card.lines.extend(extra.map(t));
        card.explanation = Some(explanation);
        card.status = t(Msg::StatusActionNeeded);
    };
    match state {
        InstallationState::NotInstalled => {
            card.kind = Kind::NotInstalled;
            card.tone = Tone::Accent;
            card.heading = t(Msg::PickRom);
            card.rom = RomField::Shown;
            card.languages = true;
            card.hd_note = true;
            card.primary = button(Command::Prepare, input);
            card.status = t(if input.recognized { Msg::StatusChooseLanguage } else { Msg::StatusSelectRom });
        }
        InstallationState::Preparing => {
            card.kind = Kind::PreparingElsewhere;
            card.tone = Tone::Neutral;
            card.heading = t(Msg::PreparingElsewhere);
            card.lines = vec![t(Msg::PreparingElsewhereBody)];
            card.primary = button(Command::CheckInstallation, input);
            card.status = t(Msg::StatusPreparingElsewhere);
        }
        InstallationState::Ready { recipe, .. } => {
            card.kind = Kind::Ready;
            card.tone = Tone::Accent;
            let revision = input.revision.unwrap_or(recipe);
            card.heading = match input.language {
                Some(language) => t(Msg::ReadyHeading { revision, language }),
                None => t(Msg::ReadyHeadingNoLanguage { revision }),
            };
            card.lines = vec![t(Msg::ReadyBody)];
            card.rom = RomField::Collapsed;
            card.languages = true;
            card.primary = button(Command::Play, input);
            card.secondary = input.recognized.then(|| button(Command::Prepare, input));
            card.status = t(Msg::StatusReady);
        }
        InstallationState::UpdateAvailable { installed_package_identity, present_package_identity, .. } => {
            card.kind = Kind::UpdateAvailable;
            card.tone = Tone::Accent;
            card.heading = match (&installed_package_identity.version, &present_package_identity.version) {
                (Some(installed), Some(present)) => t(Msg::UpdateHeading { installed, present }),
                (None, Some(present)) => t(Msg::UpdateHeadingVersion { present }),
                _ => t(Msg::UpdateHeadingPlain),
            };
            card.lines = vec![t(Msg::UpdateBody)];
            card.rom = RomField::Shown;
            card.languages = true;
            card.hd_note = true;
            card.primary = button(Command::Update, input);
            card.status = t(Msg::StatusActionNeeded);
        }
        InstallationState::RepairNeeded { .. } => {
            card.kind = Kind::RepairNeeded;
            card.tone = Tone::Warning;
            let explanation = state.explanation().expect("a repair state is explained");
            let remedy = remedy_command(explanation.primary_action);
            explained(card, explanation, Some(Msg::RepairBody));
            card.rom = RomField::Shown;
            card.languages = true;
            card.primary = button(Command::Repair, input);
            let secondary = if remedy == Command::CheckInstallation { Command::CopyDiagnostics } else { remedy };
            card.secondary = Some(button(secondary, input));
        }
        InstallationState::NeedsGameInput { .. } => {
            card.kind = Kind::NeedsGameInput;
            card.tone = Tone::Warning;
            card.heading = t(Msg::NeedsGameInputHeading);
            card.lines = vec![t(Msg::NeedsGameInputBody)];
            card.explanation = state.explanation();
            card.rom = RomField::Shown;
            card.languages = true;
            card.primary = button(Command::Repair, input);
            // Not after "Remove prepared game": the title itself is gone.
            card.secondary = state.can_play().then(|| button(Command::Play, input));
            card.status = t(Msg::StatusActionNeeded);
        }
        InstallationState::Incompatible { .. } => {
            card.kind = Kind::Incompatible;
            card.tone = Tone::Error;
            let explanation = state.explanation().expect("an incompatible state is explained");
            let primary = remedy_command(explanation.primary_action);
            explained(card, explanation, None);
            card.primary = button(primary, input);
            card.secondary = explanation_secondary(primary, input);
        }
        InstallationState::Failed { .. } => return false,
    }
    true
}

pub fn show(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>, card: &HomeCard) {
    let visuals = ui.visuals().clone();
    let accent = match card.tone {
        Tone::Neutral => visuals.weak_text_color(),
        Tone::Accent => visuals.selection.stroke.color,
        Tone::Warning => visuals.warn_fg_color,
        Tone::Error => visuals.error_fg_color,
    };
    let frame = egui::Frame::group(ui.style())
        .inner_margin(14.0)
        .fill(accent.gamma_multiply(0.08))
        .stroke(Stroke::new(1.5, accent));
    let response = frame.show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.horizontal_wrapped(|ui| {
            if matches!(card.kind, Kind::Checking | Kind::Preparing) {
                ui.add(Spinner::new());
            }
            let heading = RichText::new(&card.heading).size(20.0).strong().color(accent);
            record.add(ui, "home_heading", "label", Label::new(heading).wrap());
        });
        for line in &card.lines {
            record.add(ui, "home_line", "label", Label::new(line).wrap());
        }
        if card.kind == Kind::Ready {
            for notice in &app.notices {
                let text = RichText::new(app.texts.title(notice)).color(visuals.warn_fg_color);
                record.add(ui, "home_notice", "label", Label::new(text).wrap());
                record.add(ui, "home_notice_action", "label", Label::new(app.texts.next_action(notice)).wrap());
            }
        }
        if let Some(explanation) = &card.explanation {
            details(app, ui, record, actions, explanation);
        }
        if card.dismissable && record.button(ui, true, "home_dismiss", Button::new(app.t(Msg::Dismiss)).small()) {
            actions.push(Action::Dismiss);
        }
    });
    record.push("home_card", "group", &response.response);
    ui.add_space(10.0);
    match card.rom {
        RomField::Hidden => {}
        RomField::Shown => rom_section(app, ui, record, actions, card),
        RomField::Collapsed => {
            let label = format!(
                "{} {}",
                if app.show_prepare_again { "\u{25be}" } else { "\u{25b8}" },
                app.t(Msg::PrepareAgain)
            );
            if record.button(ui, true, "prepare_again", Button::new(label).frame(false)) {
                actions.push(Action::TogglePrepareAgain);
            }
            if app.show_prepare_again {
                rom_section(app, ui, record, actions, card);
            } else if card.languages {
                ui.add_space(6.0);
                rom_card(app, ui, record, actions);
            }
        }
    }
    ui.add_space(10.0);
    needs(app, ui, record, actions);
}

/// The ROM row, the recognized-ROM card and the HD line.
fn rom_section(
    app: &mut ForgeApp,
    ui: &mut egui::Ui,
    record: &mut Recorder,
    actions: &mut Vec<Action>,
    card: &HomeCard,
) {
    let idle = app.idle();
    record.add(ui, "rom_label", "label", Label::new(app.t(Msg::RomLabel)));
    ui.horizontal_wrapped(|ui| {
        let browse = app.t(Msg::Browse);
        let font = egui::TextStyle::Button.resolve(ui.style());
        let browse_width = ui.painter().layout_no_wrap(browse.clone(), font, egui::Color32::PLACEHOLDER).size().x
            + ui.spacing().button_padding.x * 2.0
            + ui.spacing().item_spacing.x;
        let width = (ui.available_width() - browse_width - 2.0).max(160.0);
        let entry = ui.add_enabled(idle, TextEdit::singleline(&mut app.rom).desired_width(width));
        record.push("rom", "entry", &entry);
        // A typed path is recognized once committed (Enter or leaving the field).
        if entry.lost_focus() {
            actions.push(Action::Recognize);
        }
        if record.button(ui, idle, "rom_browse", Button::new(browse)) {
            actions.push(Action::Pick(PickTarget::Rom));
        }
    });
    if card.languages {
        ui.add_space(6.0);
        rom_card(app, ui, record, actions);
    }
    ui.add_space(4.0);
    let link = Button::new(RichText::new(app.t(Msg::UseExtracted)).small().underline()).frame(false);
    if record.button(ui, idle, "use_extracted", link) {
        actions.push(Action::Pick(PickTarget::Extracted));
    }
    if card.hd_note {
        if let Some(note) = app.home_hd_note() {
            ui.add_space(4.0);
            record.add(ui, "hd_note", "label", Label::new(RichText::new(note).small()).wrap());
        }
    }
}

/// "What Forge needs": the former subtitle and the data location.
fn needs(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    let arrow = if app.show_needs { "\u{25be}" } else { "\u{25b8}" };
    let label = format!("{arrow} {}", app.t(Msg::WhatForgeNeeds));
    if record.button(ui, true, "what_forge_needs", Button::new(label).frame(false)) {
        actions.push(Action::ToggleNeeds);
    }
    if app.show_needs {
        record.add(ui, "subtitle", "label", Label::new(app.t(Msg::Subtitle)).wrap());
        let path = app.installation.data_root.display().to_string();
        record.add(ui, "private_data", "label", Label::new(app.t(Msg::PrivateData { path: &path })).wrap());
    }
}

/// The Details expander of an explanation, with Copy diagnostics.
pub fn details(
    app: &mut ForgeApp,
    ui: &mut egui::Ui,
    record: &mut Recorder,
    actions: &mut Vec<Action>,
    explanation: &Explanation,
) {
    if explanation.details.is_empty() {
        return;
    }
    let label = app.t(if app.show_details { Msg::HideDetails } else { Msg::ShowDetails });
    let arrow = if app.show_details { "\u{25be}" } else { "\u{25b8}" };
    if record.button(ui, true, "details_toggle", Button::new(format!("{arrow} {label}")).frame(false)) {
        actions.push(Action::ToggleDetails);
    }
    if app.show_details {
        let text = RichText::new(&explanation.details).monospace().small();
        record.add(ui, "details", "label", Label::new(text).wrap());
    }
}

/// The recognized-ROM card: the revision and the game languages as large
/// buttons; the installed title's languages when no ROM is being prepared.
pub fn rom_card(app: &mut ForgeApp, ui: &mut egui::Ui, record: &mut Recorder, actions: &mut Vec<Action>) {
    let visuals = ui.visuals().clone();
    let card = app.current_card().cloned();
    let (choices, selected) = app.language_options();
    let choices = choices.to_vec();
    // Highlighted once there is a ROM or a language to show.
    let accent = match &card {
        Some(Card::Failed(_)) => visuals.error_fg_color,
        None if choices.is_empty() => visuals.weak_text_color(),
        _ => visuals.selection.stroke.color,
    };
    let pending = app.pending_rom().is_some();
    let enabled = app.idle();
    let frame = egui::Frame::group(ui.style())
        .inner_margin(12.0)
        .fill(accent.gamma_multiply(0.06))
        .stroke(Stroke::new(1.0, accent));
    let response = frame.show(ui, |ui| {
        ui.set_width(ui.available_width());
        let heading =
            card.as_ref().map_or(app.t(Msg::GameLanguage), |card| crate::recognition::heading(card, &app.texts));
        ui.horizontal_wrapped(|ui| {
            if matches!(card, Some(Card::Recognizing(_))) {
                ui.add(Spinner::new());
            }
            let text = RichText::new(heading).size(17.0).strong().color(accent);
            record.add(ui, "rom_card_title", "label", Label::new(text).wrap());
        });
        match &card {
            Some(Card::Recognizing(stage)) => record.add(ui, "rom_card_status", "label", Label::new(stage).wrap()),
            Some(Card::Failed(explanation)) => {
                let message = crate::recognition::failure_text(explanation, &app.texts);
                let text = RichText::new(message).color(visuals.error_fg_color);
                record.add(ui, "rom_card_status", "label", Label::new(text).wrap());
            }
            Some(Card::Recognized(rom)) if pending && rom.languages.is_empty() => {
                let error = rom.language_error.as_deref().unwrap_or_default();
                let message = app.t(Msg::LanguagesUnreadable { error });
                record.add(ui, "rom_card_status", "label", Label::new(message).wrap());
            }
            _ if choices.is_empty() => {
                let message = match &app.language_error {
                    Some(error) => app.t(Msg::LanguageSettingsUnreadable { error }),
                    None => app.t(Msg::NoLanguages),
                };
                record.add(ui, "rom_card_status", "label", Label::new(message).wrap());
            }
            _ => {
                ui.add_space(4.0);
                record.add(ui, "language_label", "label", Label::new(app.t(Msg::ChooseLanguage)));
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
                let hint = app.t(if pending { Msg::LanguageForInstall } else { Msg::LanguageInstalled });
                record.add(ui, "language_hint", "label", Label::new(hint).wrap());
            }
        }
    });
    record.push("rom_card", "group", &response.response);
}

#[cfg(test)]
mod tests {
    use super::*;
    use forge_base::{ErrorKind, ForgeError};
    use forge_core::state::{IncompatibleReason, PackageIdentity, RepairReason};

    fn input<'a>(texts: &'a Texts, state: Option<&'a InstallationState>) -> HomeInput<'a> {
        HomeInput {
            texts,
            state,
            failure: None,
            preparing: None,
            recognized: false,
            idle: true,
            revision: Some("Ocarina of Time 3D (EUR)"),
            language: Some("Italiano"),
        }
    }

    fn identity(version: Option<&str>) -> PackageIdentity {
        PackageIdentity { runtime_sha256: "0".repeat(64), version: version.map(str::to_string) }
    }

    /// Every installation state, as the state matrix of WP-01 produces them.
    pub fn states() -> Vec<InstallationState> {
        vec![
            InstallationState::NotInstalled,
            InstallationState::Preparing,
            InstallationState::Ready {
                recipe: "r".into(),
                package_version: Some("0.8.0".into()),
                language: Some("it".into()),
            },
            InstallationState::UpdateAvailable {
                recipe: "r".into(),
                installed_package_identity: identity(Some("0.8.0")),
                present_package_identity: identity(Some("0.8.1")),
            },
            InstallationState::RepairNeeded { reason: RepairReason::InterruptedActivation, details: "journal".into() },
            InstallationState::RepairNeeded {
                reason: RepairReason::ConfigUnreadable { file: "config/TriAevum.json".into() },
                details: "bad json".into(),
            },
            InstallationState::NeedsGameInput { recipe: "r".into(), details: "The romfs data kept is missing".into() },
            InstallationState::Incompatible { reason: IncompatibleReason::TargetMismatch, details: "linux".into() },
            InstallationState::Incompatible {
                reason: IncompatibleReason::MissingCatalogue,
                details: "no catalog".into(),
            },
            InstallationState::Failed { explanation: forge_core::explain::explain(&ForgeError::invalid("boom")) },
        ]
    }

    #[test]
    fn every_state_maps_to_a_card_with_a_primary_action() {
        let texts = Texts::default();
        let expected = [
            (Kind::NotInstalled, Command::Prepare, RomField::Shown),
            (Kind::PreparingElsewhere, Command::CheckInstallation, RomField::Hidden),
            (Kind::Ready, Command::Play, RomField::Collapsed),
            (Kind::UpdateAvailable, Command::Update, RomField::Shown),
            (Kind::RepairNeeded, Command::Repair, RomField::Shown),
            (Kind::RepairNeeded, Command::Repair, RomField::Shown),
            (Kind::NeedsGameInput, Command::Repair, RomField::Shown),
            (Kind::Incompatible, Command::ViewDiagnostics, RomField::Hidden),
            (Kind::Incompatible, Command::ViewDiagnostics, RomField::Hidden),
            (Kind::Failed, Command::ChooseRom, RomField::Hidden),
        ];
        for (state, (kind, command, rom)) in states().iter().zip(expected) {
            let card = card(&input(&texts, Some(state)));
            assert_eq!((card.kind, card.primary.command, card.rom), (kind, command, rom), "{state:?}");
            assert!(!card.heading.is_empty() && !card.status.is_empty(), "{state:?}");
        }
        let checking = card(&input(&texts, None));
        assert_eq!((checking.kind, checking.primary.enabled), (Kind::Checking, false));
    }

    #[test]
    fn the_cards_say_what_the_plan_says() {
        let texts = Texts::default();
        let states = states();
        let ready = card(&input(&texts, Some(&states[2])));
        assert_eq!(ready.heading, "Ready to play: Ocarina of Time 3D (EUR), Italiano");
        assert_eq!(ready.secondary, None);
        let update = card(&input(&texts, Some(&states[3])));
        assert_eq!(update.heading, "A new TriAevum package is here (0.8.0 -> 0.8.1).");
        let config = card(&input(&texts, Some(&states[5])));
        assert_eq!(config.heading, "Settings could not be loaded");
        assert_eq!(config.secondary.map(|button| button.command), Some(Command::DataPage));
        let needs = card(&input(&texts, Some(&states[6])));
        assert_eq!(needs.heading, "Your game files are needed again to repair this installation");
        assert_eq!(needs.secondary.map(|button| button.command), Some(Command::Play));
        assert!(needs.explanation.is_some_and(|explanation| explanation.details.contains("romfs")));
    }

    #[test]
    fn prepare_needs_a_recognized_rom() {
        let texts = Texts::default();
        let state = InstallationState::NotInstalled;
        let mut view = input(&texts, Some(&state));
        assert!(!card(&view).primary.enabled, "a typed path is not enough");
        assert_eq!(card(&view).status, "Select your ROM to begin.");
        view.recognized = true;
        assert!(card(&view).primary.enabled);
        view.idle = false;
        assert!(!card(&view).primary.enabled);
        // Ready: a recognized ROM offers Prepare beside Play.
        let ready = InstallationState::Ready { recipe: "r".into(), package_version: None, language: None };
        let mut view = input(&texts, Some(&ready));
        view.recognized = true;
        let card = card(&view);
        assert_eq!(card.secondary, Some(CardButton { command: Command::Prepare, enabled: true }));
    }

    #[test]
    fn a_preparation_shows_its_stage_time_progress_and_cancel() {
        let texts = Texts::default();
        let state = InstallationState::NotInstalled;
        let mut view = input(&texts, Some(&state));
        view.idle = false;
        view.preparing =
            Some(Preparing { stage: "Extracting", elapsed: Duration::from_secs(75), progress: Some((120, Some(400))) });
        let shown = card(&view);
        assert_eq!(shown.kind, Kind::Preparing);
        assert_eq!(shown.primary, CardButton { command: Command::Cancel, enabled: true });
        assert_eq!(shown.lines, ["Extracting", "Elapsed: 1:15", "120 of 400"]);
        assert_eq!(shown.status, "Extracting");
        view.preparing.as_mut().unwrap().stage = "Preparing GPU pipelines: 120/400";
        assert_eq!(card(&view).lines, ["Preparing GPU pipelines: 120/400", "Elapsed: 1:15"]);
    }

    #[test]
    fn a_failure_replaces_the_card_until_dismissed() {
        let texts = Texts::default();
        let state = InstallationState::NotInstalled;
        let failure = forge_core::explain::explain(&ForgeError::new(ErrorKind::StorageFull, "disk full"));
        let mut view = input(&texts, Some(&state));
        view.failure = Some(&failure);
        let card = card(&view);
        assert_eq!((card.kind, card.heading.as_str()), (Kind::Failed, "Not enough storage"));
        assert_eq!(card.primary.command, Command::DataPage);
        assert_eq!(card.secondary.map(|button| button.command), Some(Command::CopyDiagnostics));
        assert!(card.dismissable && card.rom == RomField::Shown);
        assert_eq!(card.status, "Action needed: see Home.", "errors never reach the status line");
        // A startup failure before the state is known cannot be dismissed into nothing.
        let mut view = input(&texts, None);
        view.failure = Some(&failure);
        assert!(!super::card(&view).dismissable);
    }

    #[test]
    fn every_remedy_has_a_button() {
        use forge_core::explain::Action as A;
        let all = [
            A::CheckInput,
            A::CheckInstallation,
            A::ChooseDecryptedInput,
            A::ChooseSupportedInput,
            A::ViewSupportedRevisions,
            A::ChooseCompatiblePackage,
            A::ReviewDetails,
            A::ReviewSettings,
            A::FreeStorage,
            A::ReviewAccess,
            A::CloseAndRetry,
            A::ViewDiagnostics,
            A::ReturnToInstallation,
            A::CopyDiagnostics,
            A::RetrySetting,
            A::ChooseShorterLocation,
            A::RestoreSettings,
            A::SelectRom,
            A::ChooseWritableDestination,
            A::CheckPrerequisites,
            A::ReviewGraphicsRequirements,
            A::RetryStep,
            A::ContinueWithNativeAssets,
            A::OpenRecoveryOptions,
            A::Play,
            A::RetryLanguageDetection,
            A::ReviewDisplaySettings,
        ];
        let texts = Texts::default();
        for remedy in all {
            assert!(!texts.get(remedy_command(remedy).label()).is_empty(), "{remedy:?}");
        }
        assert_eq!(remedy_command(A::SelectRom), Command::ChooseRom);
        assert_eq!(remedy_command(A::FreeStorage), Command::DataPage);
    }
}
