// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! What went wrong, for people: the explanation catalogue shared by the
//! desktop window, the CLI and the Android installer (through the ABI).
//!
//! `explain` turns an error (or a failed event) into an [`Explanation`]
//! `{id, title, cause, next_action, primary_action, details}`. The table is
//! total over the 16 error kinds and any stage (known, unknown or none) and
//! is refined by the structured operation, subject and reason when they are
//! known (UX plan, Appendix B). The engine's own message, the Python-identical
//! text users saw before, becomes the redacted `details`.
//!
//! Every user-visible string lives in [`TEXTS`], keyed by `<id>.title`,
//! `<id>.cause` and `<id>.next_action` (and `stage.<name>`, `details.<name>`),
//! so a translation replaces that table alone.

use serde_json::{json, Map, Value};

use crate::error::{ErrorKind, ForgeError, Operation, Reason, Subject};

pub const FORMAT: &str = "triaevum_explanation_v1";

/// The English text of every explanation, by key.
pub const TEXTS: &[(&str, &str)] = &[
    ("invalid_input.title", "A required input could not be read"),
    ("invalid_input.cause", "The selected file or a required installation input is missing, incomplete or unreadable. The details name which one."),
    ("invalid_input.next_action", "Check the input: select the source again, or check the installation when the details name installed data."),
    ("encrypted_input.title", "This input could not be read as decrypted game data"),
    ("encrypted_input.cause", "Forge found no readable decrypted application data in it. This does not prove that the file is encrypted."),
    ("encrypted_input.next_action", "Choose a decrypted .3ds or .cci ROM, or a folder of extracted data."),
    ("missing_key.title", "Encryption support is unavailable"),
    ("missing_key.cause", "Forge works only with decrypted input and manages no encryption keys."),
    ("missing_key.next_action", "Choose a decrypted ROM instead; Forge cannot use encryption keys."),
    ("unsupported_format.title", "This input or operation is not supported by this build"),
    ("unsupported_format.cause", "This operation cannot accept this format, or this build lacks the capability it needs (for example downloads)."),
    ("unsupported_format.next_action", "Choose a supported input (a .3ds or .cci ROM, or an extracted-data folder), or import the file manually."),
    ("unsupported_revision.title", "This game revision is not supported"),
    ("unsupported_revision.cause", "The verified identity of this input matches no game revision this package supports."),
    ("unsupported_revision.next_action", "View the supported revisions, then select a matching input."),
    ("incompatible_target.title", "This package is not compatible"),
    ("incompatible_target.cause", "The package's target, runtime or title contract does not match this installation or device."),
    ("incompatible_target.next_action", "Choose a package made for this system. Your saves and settings are kept."),
    ("artifact_integrity.title", "A file did not pass verification"),
    ("artifact_integrity.cause", "A file's size, hash or structure differs from the recorded contract."),
    ("artifact_integrity.next_action", "Review the details: get a fresh package or archive for a publisher file, or select your input again for your own file."),
    ("invalid_settings.title", "Settings need attention"),
    ("invalid_settings.cause", "A setting or path is not valid for this operation. The existing file was kept."),
    ("invalid_settings.next_action", "Review the setting named in the details, or restore the previous valid settings."),
    ("storage_full.title", "Not enough storage"),
    ("storage_full.cause", "A destination ran out of space or quota."),
    ("storage_full.next_action", "Free some storage, then retry; Forge continues from a safe point."),
    ("permission_denied.title", "Access to a required location was denied"),
    ("permission_denied.cause", "The system or the document provider refused access: the location is read-only or no longer authorized."),
    ("permission_denied.next_action", "Review access: select the location again, grant access or choose a writable destination, then retry."),
    ("busy.title", "TriAevum files are in use"),
    ("busy.cause", "Another operation or program holds the installation or one of its files."),
    ("busy.next_action", "Close the game or the other Forge operation named in the details, then retry."),
    ("recovery_required.title", "The installation needs repair or recovery"),
    ("recovery_required.cause", "The installed runtime, launch profile or activation record is not valid."),
    ("recovery_required.next_action", "Check the installation: Forge recovers an interrupted activation, otherwise repair it."),
    ("native_preflight.title", "The game runtime could not be checked"),
    ("native_preflight.cause", "The runtime helper did not start or returned no valid result. This alone does not show a graphics problem."),
    ("native_preflight.next_action", "View the diagnostics; if they name no cause, copy the report and verify the package."),
    ("preparation_failed.title", "Preparation did not finish"),
    ("preparation_failed.cause", "A required step failed. What was installed before stays usable."),
    ("preparation_failed.next_action", "Check the installation, then retry or repair it."),
    ("cancelled.title", "Operation cancelled"),
    ("cancelled.cause", "The work stopped at a safe point."),
    ("cancelled.next_action", "Return to the installation, or start the operation again."),
    ("internal.title", "Forge could not complete this operation"),
    ("internal.cause", "An unexpected internal error happened; no problem with your input or drivers was found."),
    ("internal.next_action", "Copy the diagnostics and reopen Forge to check the installation."),
    // Refinements (structured reason, subject or operation).
    ("settings_save.after_activation.title", "The game is installed, but this setting was not saved"),
    ("settings_save.after_activation.cause", "The installation succeeded; writing the setting named in the details failed."),
    ("settings_save.after_activation.next_action", "Retry saving the setting or review access to the settings folder; you can play."),
    ("request.invalid.title", "Forge could not process this request"),
    ("request.invalid.cause", "The window sent Forge a request it could not read."),
    ("request.invalid.next_action", "Copy the diagnostics and reopen Forge; your ROM is not the problem."),
    ("lock.legacy.title", "This installation uses an older lock format"),
    ("lock.legacy.cause", "An older Forge left a lock file in the installation."),
    ("lock.legacy.next_action", "Close every Forge, then check the installation from the recovery route; Forge does not delete the lock itself."),
    ("storage.insufficient_space.title", "Not enough storage to prepare the game"),
    ("storage.insufficient_space.cause", "Before writing anything, Forge estimated the space preparation needs on the data folder's drive; the details give the required and free amounts."),
    ("storage.insufficient_space.next_action", "Free at least the amount shown in the details, or choose a data folder on another drive, then retry."),
    ("path.too_long.title", "The installation path is too long"),
    ("path.too_long.cause", "The files Forge generates would exceed the system's path length limit in this folder."),
    ("path.too_long.next_action", "Move TriAevum to a shorter folder (the details say how many characters to remove), then try again."),
    ("settings_load.malformed.title", "Settings could not be loaded"),
    ("settings_load.malformed.cause", "A settings file could not be read as valid settings. The original file is kept unchanged."),
    ("settings_load.malformed.next_action", "Restore a known-good copy, or reset only that file after previewing the change."),
    ("settings_load.newer.title", "Settings could not be loaded"),
    ("settings_load.newer.cause", "A settings file was written by a newer version. It is kept unchanged."),
    ("settings_load.newer.next_action", "Use the newer version, or reset only that file after previewing the change; Forge never downgrades it."),
    ("package.component_missing.title", "A compatible package component is missing or invalid"),
    ("package.component_missing.cause", "The package's title module, catalog or runtime is missing or does not match its records."),
    ("package.component_missing.next_action", "Choose a clean, complete package for this system; your data is kept."),
    ("source.needed.title", "The game source is needed again"),
    ("source.needed.cause", "The game data Forge kept from your ROM is missing or failed verification."),
    ("source.needed.next_action", "Select your ROM or extracted data again; saves and settings are kept."),
    ("revision.identity_mismatch.title", "This ROM revision is not supported by this package"),
    ("revision.identity_mismatch.cause", "The ROM's verified identity is not one of the supported revisions."),
    ("revision.identity_mismatch.next_action", "View the supported revisions or choose another ROM; renaming the file does not change its revision."),
    ("provider.grant_lost.title", "Access to the selected ROM expired"),
    ("provider.grant_lost.cause", "The document provider no longer allows Forge to read the selected ROM."),
    ("provider.grant_lost.next_action", "Select the ROM again."),
    ("package_replace.file_holder.title", "Close TriAevum before updating"),
    ("package_replace.file_holder.cause", "A running program named in the details holds a file Forge must replace."),
    ("package_replace.file_holder.next_action", "Close that program, then retry; nothing was lost."),
    ("destination.read_only.title", "This destination cannot accept installation writes"),
    ("destination.read_only.cause", "The destination is read-only, or the installation is routed to a location that cannot be written."),
    ("destination.read_only.next_action", "Choose a writable destination; Forge does not switch locations on its own."),
    ("runtime.missing_dependency.title", "The runtime could not start"),
    ("runtime.missing_dependency.cause", "The system loader reported a missing or incompatible library before the runtime started."),
    ("runtime.missing_dependency.next_action", "Check that the package is complete and the system prerequisites are installed; the details name the library or code."),
    ("device.unsupported.title", "The graphics device cannot run this configuration"),
    ("device.unsupported.cause", "The runtime reported that no compatible graphics device or capability is available."),
    ("device.unsupported.next_action", "Review the graphics requirements and keep the report for support."),
    ("preparation.required_step.title", "A required preparation step failed"),
    ("preparation.required_step.cause", "The step named in the details did not finish; the previous working installation is kept."),
    ("preparation.required_step.next_action", "Retry this step or repair the installation."),
    ("enhancement.not_ready.title", "The optional enhancement is not ready"),
    ("enhancement.not_ready.cause", "Downloading, verifying or unpacking an optional enhancement failed; the game uses its native assets."),
    ("enhancement.not_ready.next_action", "Continue with the native assets, or choose the archive yourself and retry."),
    ("recovery.rollback_failure.title", "Recovery needs more information"),
    ("recovery.rollback_failure.cause", "The backup or journal files needed to undo an interrupted change are missing or invalid."),
    ("recovery.rollback_failure.next_action", "Open the recovery options and save the diagnostics; Play stays blocked and the journal is kept."),
    ("session.game_running.title", "Close the game first"),
    ("session.game_running.cause", "TriAevum is running from this installation. Backing up, restoring, moving or deleting its files while the game runs could lose progress."),
    ("session.game_running.next_action", "Close the game, then retry; Forge never closes it for you."),
    ("session.forge_running.title", "Another Forge is working on this installation"),
    ("session.forge_running.cause", "Another Forge window or operation is changing this installation's files."),
    ("session.forge_running.next_action", "Wait until the other Forge finishes or close it, then retry."),
    ("backup.other_game.title", "This backup is for another game"),
    ("backup.other_game.cause", "The backup was made for another game, or for a game revision this package does not support. Nothing was changed."),
    ("backup.other_game.next_action", "Choose a backup that TriAevum made for this game."),
    ("backup.refused.title", "This backup cannot be restored"),
    ("backup.refused.cause", "The file is not a TriAevum backup, or its contents do not match its list of files, or it names files outside its folders. Nothing was changed."),
    ("backup.refused.next_action", "Choose another backup file; the details name the problem."),
    ("restore.interrupted.title", "A restore was interrupted"),
    ("restore.interrupted.cause", "Forge stopped while it was restoring a backup. Your previous files are kept in the restore's snapshot folder."),
    ("restore.interrupted.next_action", "Open Data & backups to finish the recovery; Play stays blocked until then."),
    // Outcomes that are not errors.
    ("outcome.graphics_preparation_incomplete.title", "Ready: graphics preparation incomplete"),
    ("outcome.graphics_preparation_incomplete.cause", "The game is installed. An optional graphics preparation step did not complete; the details name it."),
    ("outcome.graphics_preparation_incomplete.next_action", "Play now, or retry the graphics preparation."),
    ("outcome.languages_unreadable.title", "ROM recognized: language choices could not be read"),
    ("outcome.languages_unreadable.cause", "The game revision was recognized, but its languages could not be read now."),
    ("outcome.languages_unreadable.next_action", "Continue: the installation reads the languages again. You can also retry."),
    ("outcome.display_settings_reset.title", "Display settings were reset after an early exit"),
    ("outcome.display_settings_reset.cause", "The game closed during startup, so its display settings were reset; the details list the fields."),
    ("outcome.display_settings_reset.next_action", "Review the output settings before changing them again."),
    ("outcome.game_exited_with_error.title", "The game closed with an error"),
    ("outcome.game_exited_with_error.cause", "TriAevum exited with an error code; the details hold the code and the end of its log."),
    ("outcome.game_exited_with_error.next_action", "Copy the diagnostics, then start the game again or check the installation."),
    // Stage names and detail labels.
    ("stage.preflight", "Checking the runtime and package"),
    ("stage.extract", "Extracting the game data"),
    ("stage.verify", "Verifying the game revision"),
    ("stage.adapt", "Adapting the game data"),
    ("stage.prepare", "Preparing the title index"),
    ("stage.assets", "Preparing assets"),
    ("stage.shaders", "Preparing graphics"),
    ("stage.activate", "Installing the title"),
    ("stage.ready", "Finishing"),
    ("stage.spool", "Copying the selected file"),
    ("stage.identify", "Reading the cartridge"),
    ("stage.languages", "Reading the game languages"),
    ("details.stage", "Stage"),
    ("details.operation", "Operation"),
    ("details.os_code", "System error code"),
];

/// The English text for `key`.
pub fn text(key: &str) -> Option<&'static str> {
    TEXTS.iter().find(|(name, _)| *name == key).map(|(_, value)| *value)
}

fn required(key: &str) -> &'static str {
    text(key).unwrap_or_else(|| panic!("explanation text {key} is missing"))
}

/// What the frontend offers as the main button.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    CheckInput,
    CheckInstallation,
    ChooseDecryptedInput,
    ChooseSupportedInput,
    ViewSupportedRevisions,
    ChooseCompatiblePackage,
    ReviewDetails,
    ReviewSettings,
    FreeStorage,
    ReviewAccess,
    CloseAndRetry,
    ViewDiagnostics,
    ReturnToInstallation,
    CopyDiagnostics,
    RetrySetting,
    ChooseShorterLocation,
    RestoreSettings,
    SelectRom,
    ChooseWritableDestination,
    CheckPrerequisites,
    ReviewGraphicsRequirements,
    RetryStep,
    ContinueWithNativeAssets,
    OpenRecoveryOptions,
    Play,
    RetryLanguageDetection,
    ReviewDisplaySettings,
}

impl Action {
    pub fn code(self) -> &'static str {
        match self {
            Self::CheckInput => "check_input",
            Self::CheckInstallation => "check_installation",
            Self::ChooseDecryptedInput => "choose_decrypted_input",
            Self::ChooseSupportedInput => "choose_supported_input",
            Self::ViewSupportedRevisions => "view_supported_revisions",
            Self::ChooseCompatiblePackage => "choose_compatible_package",
            Self::ReviewDetails => "review_details",
            Self::ReviewSettings => "review_settings",
            Self::FreeStorage => "free_storage",
            Self::ReviewAccess => "review_access",
            Self::CloseAndRetry => "close_and_retry",
            Self::ViewDiagnostics => "view_diagnostics",
            Self::ReturnToInstallation => "return_to_installation",
            Self::CopyDiagnostics => "copy_diagnostics",
            Self::RetrySetting => "retry_setting",
            Self::ChooseShorterLocation => "choose_shorter_location",
            Self::RestoreSettings => "restore_settings",
            Self::SelectRom => "select_rom",
            Self::ChooseWritableDestination => "choose_writable_destination",
            Self::CheckPrerequisites => "check_prerequisites",
            Self::ReviewGraphicsRequirements => "review_graphics_requirements",
            Self::RetryStep => "retry_step",
            Self::ContinueWithNativeAssets => "continue_with_native_assets",
            Self::OpenRecoveryOptions => "open_recovery_options",
            Self::Play => "play",
            Self::RetryLanguageDetection => "retry_language_detection",
            Self::ReviewDisplaySettings => "review_display_settings",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Severity {
    Error,
    Warning,
    Info,
}

impl Severity {
    pub fn code(self) -> &'static str {
        match self {
            Self::Error => "error",
            Self::Warning => "warning",
            Self::Info => "info",
        }
    }
}

/// The record of each error kind (Appendix B, first table).
fn base_record(kind: ErrorKind) -> (&'static str, Action, Severity) {
    let action = match kind {
        ErrorKind::InvalidInput => Action::CheckInput,
        ErrorKind::EncryptedInput | ErrorKind::MissingKey => Action::ChooseDecryptedInput,
        ErrorKind::UnsupportedFormat => Action::ChooseSupportedInput,
        ErrorKind::UnsupportedRevision => Action::ViewSupportedRevisions,
        ErrorKind::IncompatibleTarget => Action::ChooseCompatiblePackage,
        ErrorKind::ArtifactIntegrity => Action::ReviewDetails,
        ErrorKind::InvalidSettings => Action::ReviewSettings,
        ErrorKind::StorageFull => Action::FreeStorage,
        ErrorKind::PermissionDenied => Action::ReviewAccess,
        ErrorKind::Busy => Action::CloseAndRetry,
        ErrorKind::RecoveryRequired | ErrorKind::PreparationFailed => Action::CheckInstallation,
        ErrorKind::NativePreflight => Action::ViewDiagnostics,
        ErrorKind::Cancelled => Action::ReturnToInstallation,
        ErrorKind::Internal => Action::CopyDiagnostics,
    };
    let severity = if kind == ErrorKind::Cancelled { Severity::Info } else { Severity::Error };
    (kind.code(), action, severity)
}

/// What selects a refinement: a reason, a subject or an operation.
enum Key {
    Reason(&'static [Reason]),
    Subject(&'static [Subject]),
    Operation(&'static [Operation]),
}

/// A structured refinement (Appendix B, second table). `kinds` empty means
/// every kind except `cancelled`. `operations` filters only errors that name
/// an operation: an error without one still matches by its key.
pub struct Refinement {
    pub id: &'static str,
    kinds: &'static [ErrorKind],
    operations: &'static [Operation],
    key: Key,
    action: Action,
    severity: Severity,
}

use ErrorKind as K;
use Operation as O;

/// First match wins.
pub const REFINEMENTS: &[Refinement] = &[
    Refinement {
        id: "settings_save.after_activation",
        kinds: &[],
        operations: &[],
        key: Key::Reason(&[Reason::AfterActivationCommitted]),
        action: Action::RetrySetting,
        severity: Severity::Warning,
    },
    Refinement {
        id: "request.invalid",
        kinds: &[K::InvalidInput],
        operations: &[],
        key: Key::Operation(&[O::RequestValidation]),
        action: Action::CopyDiagnostics,
        severity: Severity::Error,
    },
    Refinement {
        id: "lock.legacy",
        kinds: &[K::InvalidInput],
        operations: &[O::LockInspection, O::Install, O::Activate, O::Launch, O::Inspect],
        key: Key::Reason(&[Reason::LegacyLock]),
        action: Action::CheckInstallation,
        severity: Severity::Error,
    },
    Refinement {
        id: "storage.insufficient_space",
        kinds: &[K::StorageFull],
        operations: &[],
        key: Key::Reason(&[Reason::InsufficientSpace]),
        action: Action::FreeStorage,
        severity: Severity::Error,
    },
    Refinement {
        id: "path.too_long",
        kinds: &[K::InvalidSettings, K::PreparationFailed, K::InvalidInput],
        operations: &[],
        key: Key::Reason(&[Reason::PathTooLong]),
        action: Action::ChooseShorterLocation,
        severity: Severity::Error,
    },
    Refinement {
        id: "settings_load.malformed",
        kinds: &[K::InvalidSettings, K::InvalidInput],
        operations: &[O::SettingsLoad],
        key: Key::Reason(&[Reason::MalformedSettings]),
        action: Action::RestoreSettings,
        severity: Severity::Error,
    },
    Refinement {
        id: "settings_load.newer",
        kinds: &[K::InvalidSettings, K::InvalidInput],
        operations: &[O::SettingsLoad],
        key: Key::Reason(&[Reason::NewerSchema]),
        action: Action::RestoreSettings,
        severity: Severity::Error,
    },
    Refinement {
        id: "package.component_missing",
        kinds: &[K::ArtifactIntegrity, K::InvalidInput, K::UnsupportedRevision],
        operations: &[O::PackageVerify],
        key: Key::Reason(&[Reason::MissingModule, Reason::MissingCatalogue, Reason::MissingRuntime]),
        action: Action::ChooseCompatiblePackage,
        severity: Severity::Error,
    },
    Refinement {
        id: "source.needed",
        kinds: &[K::InvalidInput, K::ArtifactIntegrity],
        operations: &[O::Verify, O::Repair, O::Inspect],
        key: Key::Reason(&[Reason::MissingRetainedSource]),
        action: Action::SelectRom,
        severity: Severity::Error,
    },
    Refinement {
        id: "revision.identity_mismatch",
        kinds: &[K::UnsupportedRevision],
        operations: &[O::Recognize, O::Verify],
        key: Key::Reason(&[Reason::IdentityMismatch]),
        action: Action::ViewSupportedRevisions,
        severity: Severity::Error,
    },
    Refinement {
        id: "provider.grant_lost",
        kinds: &[K::PermissionDenied],
        operations: &[O::Spool, O::Identify, O::Extract, O::Recognize],
        key: Key::Reason(&[Reason::ProviderGrantLost]),
        action: Action::SelectRom,
        severity: Severity::Error,
    },
    Refinement {
        id: "package_replace.file_holder",
        kinds: &[K::Busy, K::PermissionDenied],
        operations: &[O::PackageReplace, O::Update, O::Install, O::Activate, O::Launch],
        key: Key::Reason(&[Reason::VerifiedFileHolder]),
        action: Action::CloseAndRetry,
        severity: Severity::Error,
    },
    Refinement {
        id: "session.game_running",
        kinds: &[K::Busy],
        operations: &[],
        key: Key::Reason(&[Reason::GameRunning]),
        action: Action::CloseAndRetry,
        severity: Severity::Error,
    },
    Refinement {
        id: "session.forge_running",
        kinds: &[K::Busy],
        operations: &[],
        key: Key::Reason(&[Reason::ForgeRunning]),
        action: Action::CloseAndRetry,
        severity: Severity::Error,
    },
    Refinement {
        id: "destination.read_only",
        kinds: &[K::PermissionDenied, K::PreparationFailed, K::InvalidInput],
        operations: &[],
        key: Key::Reason(&[Reason::ReadOnlyDestination]),
        action: Action::ChooseWritableDestination,
        severity: Severity::Error,
    },
    Refinement {
        id: "runtime.missing_dependency",
        kinds: &[K::NativePreflight],
        operations: &[O::RuntimeQuery, O::Preflight],
        key: Key::Reason(&[Reason::MissingLoaderDependency]),
        action: Action::CheckPrerequisites,
        severity: Severity::Error,
    },
    Refinement {
        id: "device.unsupported",
        kinds: &[K::NativePreflight],
        operations: &[O::DeviceQuery, O::RuntimeQuery, O::Preflight],
        key: Key::Reason(&[Reason::NoCompatibleDevice]),
        action: Action::ReviewGraphicsRequirements,
        severity: Severity::Error,
    },
    Refinement {
        id: "preparation.required_step",
        kinds: &[K::PreparationFailed],
        operations: &[O::RequiredAssets, O::InteriorSun, O::NativeHelper],
        key: Key::Reason(&[Reason::StepFailed, Reason::TimedOut]),
        action: Action::RetryStep,
        severity: Severity::Error,
    },
    Refinement {
        id: "enhancement.not_ready",
        kinds: &[K::PreparationFailed, K::ArtifactIntegrity, K::UnsupportedFormat],
        operations: &[O::OptionalDownload, O::OptionalImport],
        key: Key::Subject(&[Subject::OptionalEnhancement]),
        action: Action::ContinueWithNativeAssets,
        severity: Severity::Warning,
    },
    Refinement {
        id: "recovery.rollback_failure",
        kinds: &[K::RecoveryRequired],
        operations: &[O::Update, O::Restore, O::Activate, O::Install],
        key: Key::Reason(&[Reason::RollbackFailure]),
        action: Action::OpenRecoveryOptions,
        severity: Severity::Error,
    },
    Refinement {
        id: "backup.other_game",
        kinds: &[K::IncompatibleTarget],
        operations: &[O::Restore],
        key: Key::Reason(&[Reason::OtherGame]),
        action: Action::ChooseSupportedInput,
        severity: Severity::Error,
    },
    Refinement {
        id: "backup.refused",
        kinds: &[K::ArtifactIntegrity, K::UnsupportedFormat, K::InvalidInput],
        operations: &[O::Restore],
        key: Key::Reason(&[Reason::InvalidBackup, Reason::NewerSchema]),
        action: Action::ChooseSupportedInput,
        severity: Severity::Error,
    },
    Refinement {
        id: "restore.interrupted",
        kinds: &[K::RecoveryRequired],
        operations: &[],
        key: Key::Reason(&[Reason::InterruptedRestore]),
        action: Action::RestoreSettings,
        severity: Severity::Error,
    },
];

/// Non-error outcomes with their own records.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Outcome {
    GraphicsPreparationIncomplete,
    LanguagesUnreadable,
    DisplaySettingsReset,
    GameExitedWithError,
}

impl Outcome {
    pub const ALL: [Outcome; 4] = [
        Self::GraphicsPreparationIncomplete,
        Self::LanguagesUnreadable,
        Self::DisplaySettingsReset,
        Self::GameExitedWithError,
    ];

    pub fn code(self) -> &'static str {
        match self {
            Self::GraphicsPreparationIncomplete => "graphics_preparation_incomplete",
            Self::LanguagesUnreadable => "languages_unreadable",
            Self::DisplaySettingsReset => "display_settings_reset",
            Self::GameExitedWithError => "game_exited_with_error",
        }
    }

    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|outcome| outcome.code() == code)
    }

    fn record(self) -> (Action, Severity) {
        match self {
            Self::GraphicsPreparationIncomplete => (Action::Play, Severity::Warning),
            Self::LanguagesUnreadable => (Action::RetryLanguageDetection, Severity::Warning),
            Self::DisplaySettingsReset => (Action::ReviewDisplaySettings, Severity::Warning),
            Self::GameExitedWithError => (Action::CopyDiagnostics, Severity::Error),
        }
    }
}

/// The facts an explanation is built from: a [`ForgeError`] or the `error`
/// object of a failed event (whose unknown codes count as absent).
#[derive(Clone, Debug, PartialEq)]
pub struct ErrorFacts {
    pub kind: ErrorKind,
    pub stage: Option<String>,
    pub message: String,
    pub os_code: Option<i64>,
    pub operation: Option<Operation>,
    pub subject: Option<Subject>,
    pub reason: Option<Reason>,
}

impl ErrorFacts {
    pub fn from_error(error: &ForgeError) -> Self {
        Self {
            kind: error.kind,
            stage: error.stage.map(str::to_string),
            message: error.message.clone(),
            os_code: error.os_code.map(i64::from),
            operation: error.operation,
            subject: error.subject,
            reason: error.reason,
        }
    }

    /// An event's `error` object. A code outside the 16 kinds is `internal`.
    pub fn from_object(error: &Map<String, Value>) -> Self {
        let text = |key: &str| error.get(key).and_then(Value::as_str);
        Self {
            kind: text("code").and_then(ErrorKind::from_code).unwrap_or(ErrorKind::Internal),
            stage: text("stage").map(str::to_string),
            message: text("message").unwrap_or_default().to_string(),
            os_code: error.get("os_code").and_then(Value::as_i64),
            operation: text("operation").and_then(Operation::from_code),
            subject: text("subject").and_then(Subject::from_code),
            reason: text("reason").and_then(Reason::from_code),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Explanation {
    /// The record id; texts are `<id>.title`, `<id>.cause`, `<id>.next_action`.
    pub id: &'static str,
    pub severity: Severity,
    pub title: &'static str,
    pub cause: &'static str,
    pub next_action: &'static str,
    pub primary_action: Action,
    /// The engine's message and context, with user names removed.
    pub details: String,
    pub facts: Option<ErrorFacts>,
}

impl Explanation {
    fn from_record(id: &'static str, action: Action, severity: Severity, details: String) -> Self {
        Self {
            id,
            severity,
            title: required(&format!("{id}.title")),
            cause: required(&format!("{id}.cause")),
            next_action: required(&format!("{id}.next_action")),
            primary_action: action,
            details,
            facts: None,
        }
    }

    /// `{format, id, severity, title, cause, next_action, primary_action,
    /// details, error?}`.
    pub fn to_value(&self) -> Value {
        let mut value = json!({
            "format": FORMAT,
            "id": self.id,
            "severity": self.severity.code(),
            "title": self.title,
            "cause": self.cause,
            "next_action": self.next_action,
            "primary_action": self.primary_action.code(),
            "details": self.details,
        });
        if let Some(facts) = &self.facts {
            value["error"] = json!({
                "code": facts.kind.code(),
                "stage": facts.stage,
                "operation": facts.operation.map(Operation::code),
                "subject": facts.subject.map(Subject::code),
                "reason": facts.reason.map(Reason::code),
                "os_code": facts.os_code,
            });
        }
        value
    }

    /// The text a plain dialog shows under the title.
    pub fn body(&self) -> String {
        let mut body = format!("{}\n\n{}", self.cause, self.next_action);
        if !self.details.is_empty() {
            body.push_str("\n\n");
            body.push_str(&self.details);
        }
        body
    }
}

/// The label of a known stage, or None.
pub fn stage_label(stage: &str) -> Option<&'static str> {
    text(&format!("stage.{stage}"))
}

fn refinement(facts: &ErrorFacts) -> Option<&'static Refinement> {
    if facts.kind == ErrorKind::Cancelled {
        return None;
    }
    REFINEMENTS.iter().find(|refinement| {
        let kind = refinement.kinds.is_empty() || refinement.kinds.contains(&facts.kind);
        let operation = match facts.operation {
            Some(operation) => refinement.operations.is_empty() || refinement.operations.contains(&operation),
            None => true,
        };
        let key = match refinement.key {
            Key::Reason(reasons) => facts.reason.is_some_and(|reason| reasons.contains(&reason)),
            Key::Subject(subjects) => facts.subject.is_some_and(|subject| subjects.contains(&subject)),
            Key::Operation(operations) => facts.operation.is_some_and(|operation| operations.contains(&operation)),
        };
        kind && operation && key
    })
}

fn details(facts: &ErrorFacts) -> String {
    let mut lines = Vec::new();
    if !facts.message.is_empty() {
        lines.push(crate::redact::redact(&facts.message));
    }
    if let Some(stage) = &facts.stage {
        let label = required("details.stage");
        lines.push(match stage_label(stage) {
            Some(name) => format!("{label}: {name} ({stage})"),
            None => format!("{label}: {stage}"),
        });
    }
    if let Some(operation) = facts.operation {
        lines.push(format!("{}: {}", required("details.operation"), operation.code()));
    }
    if let Some(code) = facts.os_code {
        lines.push(format!("{}: {code}", required("details.os_code")));
    }
    lines.join("\n")
}

/// The explanation of an error.
pub fn explain_facts(facts: &ErrorFacts) -> Explanation {
    let (id, mut action, severity) = match refinement(facts) {
        Some(refinement) => (refinement.id, refinement.action, refinement.severity),
        None => base_record(facts.kind),
    };
    // The base records name two remedies; the subject picks one.
    match (facts.kind, facts.subject, id == facts.kind.code()) {
        (ErrorKind::InvalidInput, Some(Subject::InstalledData), true) => action = Action::CheckInstallation,
        (ErrorKind::ArtifactIntegrity, Some(Subject::Package), true) => action = Action::ChooseCompatiblePackage,
        (ErrorKind::ArtifactIntegrity, Some(Subject::SelectedInput), true) => action = Action::CheckInput,
        _ => {}
    }
    let mut explanation = Explanation::from_record(id, action, severity, details(facts));
    explanation.facts = Some(facts.clone());
    explanation
}

pub fn explain(error: &ForgeError) -> Explanation {
    explain_facts(&ErrorFacts::from_error(error))
}

/// The explanation of a non-error outcome.
pub fn explain_outcome(outcome: Outcome, details: &str) -> Explanation {
    let (action, severity) = outcome.record();
    let id = match outcome {
        Outcome::GraphicsPreparationIncomplete => "outcome.graphics_preparation_incomplete",
        Outcome::LanguagesUnreadable => "outcome.languages_unreadable",
        Outcome::DisplaySettingsReset => "outcome.display_settings_reset",
        Outcome::GameExitedWithError => "outcome.game_exited_with_error",
    };
    Explanation::from_record(id, action, severity, crate::redact::redact(details))
}

/// Explains what a consumer received: a `failed` or `cancelled` event, an
/// event's `error` object, or a notice `{"outcome", "details"}`. Anything
/// else (a succeeded event, a progress event) has no explanation.
pub fn explain_value(value: &Value) -> Option<Explanation> {
    let object = value.as_object()?;
    match object.get("event").and_then(Value::as_str) {
        Some("failed") => {
            let error = object.get("error").and_then(Value::as_object).cloned().unwrap_or_default();
            Some(explain_facts(&ErrorFacts::from_object(&error)))
        }
        Some("cancelled") => Some(explain(&ForgeError::cancelled())),
        Some(_) => None,
        None if object.contains_key("code") => Some(explain_facts(&ErrorFacts::from_object(object))),
        None => {
            let outcome = Outcome::from_code(object.get("outcome")?.as_str()?)?;
            Some(explain_outcome(outcome, object.get("details").and_then(Value::as_str).unwrap_or_default()))
        }
    }
}

/// [`explain_value`] over JSON bytes (the ABI and JNI entry): the
/// explanation's JSON, or `null`.
pub fn explain_bytes(input: &[u8]) -> Vec<u8> {
    let explanation = serde_json::from_slice::<Value>(input).ok().as_ref().and_then(explain_value);
    let value = explanation.map_or(Value::Null, |explanation| explanation.to_value());
    crate::legacy_json::compact_line_bytes(&value)
}

fn notice(outcome: Outcome, details: String) -> Value {
    json!({"outcome": outcome.code(), "details": crate::redact::redact(&details)})
}

/// The non-error outcomes a job result carries: an installation whose
/// optional graphics preparation did not complete, a recognition whose
/// languages could not be read. Added to the `succeeded` event as `notices`.
pub fn result_notices(result: &Value) -> Vec<Value> {
    let mut notices = Vec::new();
    let mut incomplete = Vec::new();
    for (receipt, status_key, label) in [
        ("renderer_shader_preparation", "renderer_shader_preparation", "Renderer shader preparation"),
        ("device_pipeline_preparation", "device_pipeline_prewarm", "GPU pipeline preparation"),
    ] {
        let Some(receipt) = result.pointer(&format!("/build/{receipt}")).and_then(Value::as_object) else {
            continue;
        };
        let status = receipt.get(status_key).and_then(Value::as_str).unwrap_or("unknown");
        if status != "complete" {
            let error = receipt.get("error").and_then(Value::as_str).map(|error| format!(": {error}"));
            incomplete.push(format!("{label}: {status}{}", error.unwrap_or_default()));
        }
    }
    if !incomplete.is_empty() {
        notices.push(notice(Outcome::GraphicsPreparationIncomplete, incomplete.join("\n")));
    }
    if let Some(error) = result.get("language_error").and_then(Value::as_str) {
        notices.push(notice(Outcome::LanguagesUnreadable, error.to_string()));
    }
    notices
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_record_has_its_texts_and_every_text_is_used() {
        let mut ids: Vec<&str> = ErrorKind::ALL.iter().map(|kind| kind.code()).collect();
        ids.extend(REFINEMENTS.iter().map(|refinement| refinement.id));
        let outcomes: Vec<String> = Outcome::ALL.iter().map(|outcome| format!("outcome.{}", outcome.code())).collect();
        ids.extend(outcomes.iter().map(String::as_str));
        let mut used = std::collections::HashSet::new();
        for id in &ids {
            for part in ["title", "cause", "next_action"] {
                let key = format!("{id}.{part}");
                assert!(text(&key).is_some_and(|value| !value.is_empty()), "{key}");
                used.insert(key);
            }
        }
        for (key, _) in TEXTS {
            assert!(used.contains(*key) || key.starts_with("stage.") || key.starts_with("details."), "unused {key}");
        }
        let mut keys = std::collections::HashSet::new();
        assert!(TEXTS.iter().all(|(key, _)| keys.insert(*key)), "duplicate keys");
    }

    #[test]
    fn redacts_details() {
        let error = ForgeError::invalid(r"Cannot read C:\Users\Someone\x.cci: missing");
        assert_eq!(explain(&error).details, r"Cannot read C:\Users\<user>\x.cci: missing");
    }

    #[test]
    fn graphics_and_language_notices() {
        let result = json!({"status": "ready", "build": {
            "renderer_shader_preparation": {"renderer_shader_preparation": "complete"},
            "device_pipeline_preparation": {"device_pipeline_prewarm": "failed", "error": "helper exited 3"}}});
        let notices = result_notices(&result);
        assert_eq!(
            notices,
            [json!({"outcome": "graphics_preparation_incomplete",
            "details": "GPU pipeline preparation: failed: helper exited 3"})]
        );
        assert!(result_notices(&json!({"status": "ready", "build": {"device_pipeline_preparation": null}})).is_empty());
        let recognized = json!({"recipe": "r", "languages": null, "language_error": "no runtime"});
        assert_eq!(result_notices(&recognized)[0]["outcome"], "languages_unreadable");
        assert_eq!(explain_value(&notices[0]).unwrap().id, "outcome.graphics_preparation_incomplete");
    }
}
