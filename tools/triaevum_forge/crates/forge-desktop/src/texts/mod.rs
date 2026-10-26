// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The window's message table: every text the window shows is a [`Msg`],
//! keyed by id with typed arguments, resolved for a language by [`Texts`].
//! English (`en.rs`) is complete; another language returns the texts it has
//! and English fills the rest. The explanation catalogue of forge-core
//! (`forge_core::explain::TEXTS`) is resolved through the same language.
//!
//! The window's languages follow the owner's decision 8: the game languages
//! of the ROM in use (the recognized ROM, else the installed title); before
//! a ROM is known, the system language when a supported ROM provides it,
//! else English.

mod en;
mod pseudo;

use forge_core::explain::Explanation;

/// Declares [`Msg`] with its id (the variant name) and count.
macro_rules! messages {
    ($($name:ident $({ $($field:ident: $type:ty),* $(,)? })?),* $(,)?) => {
        /// A text of the window, with its arguments.
        #[derive(Clone, Copy, Debug, PartialEq, Eq)]
        pub enum Msg<'a> {
            $($name $({ $($field: $type),* })?),*
        }

        impl Msg<'_> {
            /// The number of ids.
            #[cfg(test)]
            pub const COUNT: usize = [$(stringify!($name)),*].len();

            /// The message's id, the key of every language's table.
            #[cfg(test)]
            pub fn id(&self) -> &'static str {
                match self {
                    $(Msg::$name { .. } => stringify!($name)),*
                }
            }
        }
    };
}

messages! {
    WindowTitle,
    DialogTitle,
    NavHome,
    NavEnhancements,
    NavData,
    NavDiagnostics,
    NavAbout,
    // Home.
    Checking,
    PickRom,
    RomLabel,
    Browse,
    UseExtracted,
    WhatForgeNeeds,
    Subtitle,
    PrivateData { path: &'a str },
    PreparingHeading,
    PreparingElapsed { minutes: u64, seconds: u64 },
    ProgressCount { done: u64, total: u64 },
    PreparingElsewhere,
    PreparingElsewhereBody,
    ReadyHeading { revision: &'a str, language: &'a str },
    ReadyHeadingNoLanguage { revision: &'a str },
    ReadyBody,
    PrepareAgain,
    UpdateHeading { installed: &'a str, present: &'a str },
    UpdateHeadingVersion { present: &'a str },
    UpdateHeadingPlain,
    UpdateBody,
    RepairBody,
    NeedsGameInputHeading,
    NeedsGameInputBody,
    ShowDetails,
    HideDetails,
    Dismiss,
    HomeHdDownload { megabytes: u64 },
    HomeHdReady,
    HomeHdOff,
    // Buttons.
    ButtonPrepare,
    ButtonPlay,
    ButtonUpdate,
    ButtonRepair,
    ButtonCancel,
    ButtonCheckInstallation,
    ButtonChooseRom,
    ButtonCopyDiagnostics,
    ButtonViewDiagnostics,
    ButtonSupportedRevisions,
    ButtonDataPage,
    ButtonRetry,
    ButtonContinue,
    ButtonOk,
    ButtonYes,
    ButtonNo,
    ButtonOpenFolder,
    // The footer's status line.
    StatusSelectRom,
    StatusChooseLanguage,
    StatusReady,
    StatusActionNeeded,
    StatusCancelled,
    StatusPreparingElsewhere,
    // The recognized-ROM card.
    LanguageSaved,
    NoLanguages,
    GameLanguage,
    Recognizing,
    RomNotRecognized,
    ChooseLanguage,
    LanguageForInstall,
    LanguageInstalled,
    RomRecognized { label: &'a str },
    LanguagesUnreadable { error: &'a str },
    LanguageChosen { label: &'a str },
    LanguageSettingsUnreadable { error: &'a str },
    // Preparation.
    Starting,
    DownloadingHdFirst,
    DownloadingModFirst,
    ReadyDialog,
    CloseWhileBusy,
    ConfirmCancel,
    // Enhancements.
    EnhancementsBody,
    HdHeading,
    HdDescription { title: &'a str, version: &'a str, author: &'a str, credits: &'a str, license: &'a str },
    HdDownload,
    SelectZip,
    SelectFolder,
    ModPage,
    License,
    HdAutoDownload,
    NotAvailable,
    CheckingStored,
    HdCheckFailed { error: &'a str },
    HdReady,
    HdArchive,
    HdMissingAuto { megabytes: u64 },
    HdMissing,
    ModHeading,
    ModDescription {
        title: &'a str,
        version: &'a str,
        author: &'a str,
        testing: &'a str,
        tools: &'a str,
        license: &'a str,
    },
    ModCheckbox { title: &'a str },
    ModDownload,
    SelectRar,
    ModSettingsUnreadable { error: &'a str },
    ModCheckFailed { error: &'a str },
    ModMissingEnabled,
    ModOff,
    ModOn,
    ModOffKept,
    ApplyModChange,
    HdDownloadFailed { error: &'a str },
    ModDownloadFailed { error: &'a str },
    ManualHdFailure { error: &'a str, archive: &'a str, page: &'a str },
    ManualModFailure { error: &'a str, archive: &'a str, page: &'a str },
    VerifyingArchive,
    VerifyingTextures,
    VerifyingFiles,
    DownloadingHd,
    DownloadingMod { title: &'a str },
    HdReadyReinstall,
    ModReadyReinstall { title: &'a str },
    HdArchiveApplied,
    ModSettingSaveFailed { error: &'a str },
    RevisionsUnreadable { error: &'a str },
    // File pickers.
    PickRomTitle,
    FilterRom,
    Filter3ds,
    FilterCci,
    PickExtractedTitle,
    PickArchive { name: &'a str },
    PickArchiveFallbackHd,
    PickArchiveFallbackMod,
    FilterTexturePack,
    PickFolder { title: &'a str },
    FilterModArchive,
    // Data & backups.
    DataBody,
    DataFolder,
    ForgeLog,
    DataYourFiles,
    DataEntryLabel { name: &'a str },
    DataCategoryHelp { category: &'a str },
    DataNotCreated,
    DataOriginDefault,
    DataOriginExplicit,
    DataOriginLegacy,
    DataBackupInclude,
    DataBackupOptional,
    DataBackupExclude,
    ButtonWhatIsThis,
    ButtonHideHelp,
    DataActionsHeading,
    ButtonBackUp,
    ButtonRestore,
    ButtonClearCache,
    ButtonRebuild,
    ButtonRemoveGame,
    ButtonUninstallHelp,
    ButtonChooseBackupFile,
    ButtonConfirmRestore,
    ButtonConfirmClear,
    ButtonConfirmRebuild,
    ButtonConfirmRemove,
    ButtonRecover,
    BackupPanelBody { files: u64, size: &'a str },
    BackupOptionalHeading,
    BackupExcludedNote,
    BackupDone { path: &'a str, size: &'a str },
    FilterBackup,
    SaveBackupTitle,
    OpenBackupTitle,
    RestoreHeading { created: &'a str, platform: &'a str },
    RestoreOtherPlatform { platform: &'a str },
    RestoreDifferentRevision,
    RestoreUnavailable { path: &'a str },
    RestoreKeptExisting { path: &'a str },
    RestoreGroupLine { label: &'a str, replaced: u64, added: u64, removed: u64 },
    RestoreKeptSettings { files: &'a str },
    RestoreSkipped { labels: &'a str },
    RestoreSnapshotNote { path: &'a str },
    RestoreDone { path: &'a str },
    ClearCacheBody { size: &'a str },
    ClearCacheEmpty,
    ClearCacheDone,
    RemoveGameBody { size: &'a str },
    RemoveGameKeep,
    RemoveGameDone,
    RebuildBody { size: &'a str },
    RebuildNoSource,
    RebuildDone,
    UninstallWindows { path: &'a str },
    UninstallPortable { path: &'a str },
    UninstallAppImage { appimage: &'a str, path: &'a str },
    UninstallFlatpak { id: &'a str, path: &'a str },
    UninstallAndroid { path: &'a str },
    UninstallExternal,
    UninstallKeep,
    PlatformName { code: &'a str },
    DataGameRunning,
    DataForgeRunning,
    DataRecoveryPending,
    RecoveryDone,
    DataLoading,
    DataBackingUp,
    DataCheckingBackup,
    DataRestoring,
    DataClearingCache,
    DataRemovingGame,
    DataRebuilding,
    DataRecovering,
    // Diagnostics.
    DiagnosticsBody,
    StateLabel { state: &'a str },
    Copied,
    LastGameRun { started: &'a str, seconds: u64, meaning: &'a str },
    GameRunning,
    NoGameRun,
    CollectingDiagnostics,
    DiagnosticsFailed { error: &'a str },
    GameLogFolder,
    BundleContents,
    BundleOmitted { name: &'a str, reason: &'a str },
    ButtonSaveBundle,
    SaveBundleTitle,
    FilterZip,
    BundleSaved { path: &'a str },
    BundleFailed { error: &'a str },
    // About.
    Version { version: &'a str },
    AboutBody,
    SupportedRevisions,
    NoSupportedRevisions,
    ProjectPage,
    ForgeLanguage,
    ForgeLanguageNote,
    GuiFailed { error: &'a str },
}

/// One message of every id, for the completeness tests.
#[cfg(test)]
pub const SAMPLES: &[Msg<'static>] = &{
    use Msg::*;
    [
        WindowTitle,
        DialogTitle,
        NavHome,
        NavEnhancements,
        NavData,
        NavDiagnostics,
        NavAbout,
        Checking,
        PickRom,
        RomLabel,
        Browse,
        UseExtracted,
        WhatForgeNeeds,
        Subtitle,
        PrivateData { path: "P" },
        PreparingHeading,
        PreparingElapsed { minutes: 1, seconds: 5 },
        ProgressCount { done: 1, total: 2 },
        PreparingElsewhere,
        PreparingElsewhereBody,
        ReadyHeading { revision: "R", language: "L" },
        ReadyHeadingNoLanguage { revision: "R" },
        ReadyBody,
        PrepareAgain,
        UpdateHeading { installed: "1", present: "2" },
        UpdateHeadingVersion { present: "2" },
        UpdateHeadingPlain,
        UpdateBody,
        RepairBody,
        NeedsGameInputHeading,
        NeedsGameInputBody,
        ShowDetails,
        HideDetails,
        Dismiss,
        HomeHdDownload { megabytes: 451 },
        HomeHdReady,
        HomeHdOff,
        ButtonPrepare,
        ButtonPlay,
        ButtonUpdate,
        ButtonRepair,
        ButtonCancel,
        ButtonCheckInstallation,
        ButtonChooseRom,
        ButtonCopyDiagnostics,
        ButtonViewDiagnostics,
        ButtonSupportedRevisions,
        ButtonDataPage,
        ButtonRetry,
        ButtonContinue,
        ButtonOk,
        ButtonYes,
        ButtonNo,
        ButtonOpenFolder,
        StatusSelectRom,
        StatusChooseLanguage,
        StatusReady,
        StatusActionNeeded,
        StatusCancelled,
        StatusPreparingElsewhere,
        LanguageSaved,
        NoLanguages,
        GameLanguage,
        Recognizing,
        RomNotRecognized,
        ChooseLanguage,
        LanguageForInstall,
        LanguageInstalled,
        RomRecognized { label: "L" },
        LanguagesUnreadable { error: "E" },
        LanguageChosen { label: "L" },
        LanguageSettingsUnreadable { error: "E" },
        Starting,
        DownloadingHdFirst,
        DownloadingModFirst,
        ReadyDialog,
        CloseWhileBusy,
        ConfirmCancel,
        EnhancementsBody,
        HdHeading,
        HdDescription { title: "T", version: "V", author: "A", credits: "C", license: "L" },
        HdDownload,
        SelectZip,
        SelectFolder,
        ModPage,
        License,
        HdAutoDownload,
        NotAvailable,
        CheckingStored,
        HdCheckFailed { error: "E" },
        HdReady,
        HdArchive,
        HdMissingAuto { megabytes: 451 },
        HdMissing,
        ModHeading,
        ModDescription { title: "T", version: "V", author: "A", testing: "T", tools: "O", license: "L" },
        ModCheckbox { title: "T" },
        ModDownload,
        SelectRar,
        ModSettingsUnreadable { error: "E" },
        ModCheckFailed { error: "E" },
        ModMissingEnabled,
        ModOff,
        ModOn,
        ModOffKept,
        ApplyModChange,
        HdDownloadFailed { error: "E" },
        ModDownloadFailed { error: "E" },
        ManualHdFailure { error: "E", archive: "A", page: "P" },
        ManualModFailure { error: "E", archive: "A", page: "P" },
        VerifyingArchive,
        VerifyingTextures,
        VerifyingFiles,
        DownloadingHd,
        DownloadingMod { title: "T" },
        HdReadyReinstall,
        ModReadyReinstall { title: "T" },
        HdArchiveApplied,
        ModSettingSaveFailed { error: "E" },
        RevisionsUnreadable { error: "E" },
        PickRomTitle,
        FilterRom,
        Filter3ds,
        FilterCci,
        PickExtractedTitle,
        PickArchive { name: "N" },
        PickArchiveFallbackHd,
        PickArchiveFallbackMod,
        FilterTexturePack,
        PickFolder { title: "T" },
        FilterModArchive,
        DataBody,
        DataFolder,
        ForgeLog,
        DataYourFiles,
        DataEntryLabel { name: "save_data" },
        DataCategoryHelp { category: "saves" },
        DataNotCreated,
        DataOriginDefault,
        DataOriginExplicit,
        DataOriginLegacy,
        DataBackupInclude,
        DataBackupOptional,
        DataBackupExclude,
        ButtonWhatIsThis,
        ButtonHideHelp,
        DataActionsHeading,
        ButtonBackUp,
        ButtonRestore,
        ButtonClearCache,
        ButtonRebuild,
        ButtonRemoveGame,
        ButtonUninstallHelp,
        ButtonChooseBackupFile,
        ButtonConfirmRestore,
        ButtonConfirmClear,
        ButtonConfirmRebuild,
        ButtonConfirmRemove,
        ButtonRecover,
        BackupPanelBody { files: 2, size: "S" },
        BackupOptionalHeading,
        BackupExcludedNote,
        BackupDone { path: "P", size: "S" },
        FilterBackup,
        SaveBackupTitle,
        OpenBackupTitle,
        RestoreHeading { created: "C", platform: "P" },
        RestoreOtherPlatform { platform: "P" },
        RestoreDifferentRevision,
        RestoreUnavailable { path: "P" },
        RestoreKeptExisting { path: "P" },
        RestoreGroupLine { label: "L", replaced: 1, added: 2, removed: 3 },
        RestoreKeptSettings { files: "F" },
        RestoreSkipped { labels: "L" },
        RestoreSnapshotNote { path: "P" },
        RestoreDone { path: "P" },
        ClearCacheBody { size: "S" },
        ClearCacheEmpty,
        ClearCacheDone,
        RemoveGameBody { size: "S" },
        RemoveGameKeep,
        RemoveGameDone,
        RebuildBody { size: "S" },
        RebuildNoSource,
        RebuildDone,
        UninstallWindows { path: "P" },
        UninstallPortable { path: "P" },
        UninstallAppImage { appimage: "A", path: "P" },
        UninstallFlatpak { id: "I", path: "P" },
        UninstallAndroid { path: "P" },
        UninstallExternal,
        UninstallKeep,
        PlatformName { code: "android" },
        DataGameRunning,
        DataForgeRunning,
        DataRecoveryPending,
        RecoveryDone,
        DataLoading,
        DataBackingUp,
        DataCheckingBackup,
        DataRestoring,
        DataClearingCache,
        DataRemovingGame,
        DataRebuilding,
        DataRecovering,
        DiagnosticsBody,
        StateLabel { state: "S" },
        Copied,
        LastGameRun { started: "T", seconds: 1, meaning: "M" },
        GameRunning,
        NoGameRun,
        CollectingDiagnostics,
        DiagnosticsFailed { error: "E" },
        GameLogFolder,
        BundleContents,
        BundleOmitted { name: "N", reason: "R" },
        ButtonSaveBundle,
        SaveBundleTitle,
        FilterZip,
        BundleSaved { path: "P" },
        BundleFailed { error: "E" },
        Version { version: "V" },
        AboutBody,
        SupportedRevisions,
        NoSupportedRevisions,
        ProjectPage,
        ForgeLanguage,
        ForgeLanguageNote,
        GuiFailed { error: "E" },
    ]
};

/// The language Forge shows before any ROM is known, and the code of the
/// pseudo-locale (developer use, `TRIAEVUM_FORGE_LANGUAGE=pseudo`).
pub const ENGLISH: &str = "en";
pub const PSEUDO: &str = "pseudo";

/// The game languages a supported ROM provides: the supported revisions are
/// the European cartridge's (`tools/triaevum_release/supported_revisions.json`),
/// whose languages the runtime reports as these codes
/// (`oot3d_game_language.cpp`).
pub const SUPPORTED_ROM_LANGUAGES: [&str; 5] = ["en", "fr", "de", "es", "it"];

/// The native name of a language Forge may show, for the language switch
/// when the ROM gave no label.
pub fn native_name(code: &str) -> &str {
    match code {
        "en" => "English",
        "fr" => "Fran\u{e7}ais",
        "de" => "Deutsch",
        "es" => "Espa\u{f1}ol",
        "it" => "Italiano",
        "ja" => "\u{65e5}\u{672c}\u{8a9e}",
        "nl" => "Nederlands",
        PSEUDO => "Pseudo",
        other => other,
    }
}

/// A language's own texts; None for a text it does not have yet.
type Table = fn(&Msg) -> Option<String>;

/// The translated tables. English is complete and is the fallback; the
/// other game languages are added here (UX plan WP-10).
fn table(code: &str) -> Option<Table> {
    match code {
        ENGLISH => Some(|msg| Some(en::text(msg))),
        _ => None,
    }
}

/// The texts of one language.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Texts {
    language: String,
}

impl Default for Texts {
    fn default() -> Self {
        Self::new(ENGLISH)
    }
}

impl Texts {
    pub fn new(language: &str) -> Self {
        Self { language: language.to_string() }
    }

    pub fn language(&self) -> &str {
        &self.language
    }

    /// The text of `msg`: this language's, else English.
    pub fn get(&self, msg: Msg) -> String {
        if self.language == PSEUDO {
            return pseudo::localize(&en::text(&msg));
        }
        table(&self.language).and_then(|table| table(&msg)).unwrap_or_else(|| en::text(&msg))
    }

    /// An explanation catalogue text (`<id>.title` and the like) in this
    /// language; `english` is the catalogue's own text.
    pub fn catalogue(&self, _key: &str, english: &str) -> String {
        // No translated catalogue yet: forge-core's English table is the
        // fallback for every language.
        if self.language == PSEUDO {
            return pseudo::localize(english);
        }
        english.to_string()
    }

    pub fn title(&self, explanation: &Explanation) -> String {
        self.catalogue(&format!("{}.title", explanation.id), explanation.title)
    }

    pub fn cause(&self, explanation: &Explanation) -> String {
        self.catalogue(&format!("{}.cause", explanation.id), explanation.cause)
    }

    pub fn next_action(&self, explanation: &Explanation) -> String {
        self.catalogue(&format!("{}.next_action", explanation.id), explanation.next_action)
    }

    /// The text a plain dialog shows under the title (`Explanation::body`).
    pub fn body(&self, explanation: &Explanation) -> String {
        let mut body = format!("{}\n\n{}", self.cause(explanation), self.next_action(explanation));
        if !explanation.details.is_empty() {
            body.push_str("\n\n");
            body.push_str(&explanation.details);
        }
        body
    }
}

/// The languages the window offers and the one it starts in (decision 8):
/// `rom` are the game languages of the ROM in use, empty before one is
/// known; `preferred` is the user's choice, then the system language.
pub fn choose_language(rom: &[String], preferred: &[&str]) -> String {
    let offered = |code: &str| {
        if rom.is_empty() {
            SUPPORTED_ROM_LANGUAGES.contains(&code)
        } else {
            rom.iter().any(|offered| offered == code)
        }
    };
    if let Some(code) = preferred.iter().find(|code| **code == PSEUDO || offered(code)) {
        return code.to_string();
    }
    if rom.is_empty() || offered(ENGLISH) {
        return ENGLISH.to_string();
    }
    rom[0].clone()
}

/// The system's language as a two-letter code (`it` for `it-IT` or
/// `it_IT.UTF-8`); `TRIAEVUM_FORGE_LANGUAGE` overrides it.
pub fn system_language() -> Option<String> {
    let normalize = |locale: &str| {
        let code: String = locale.chars().take_while(|c| c.is_ascii_alphabetic()).collect::<String>().to_lowercase();
        (code == PSEUDO || code.len() == 2).then_some(code)
    };
    if let Some(code) = std::env::var("TRIAEVUM_FORGE_LANGUAGE").ok().as_deref().and_then(normalize) {
        return Some(code);
    }
    platform_locale().as_deref().and_then(normalize)
}

#[cfg(windows)]
fn platform_locale() -> Option<String> {
    use windows_sys::Win32::Globalization::GetUserDefaultLocaleName;
    let mut buffer = [0u16; 85];
    // SAFETY: the buffer is LOCALE_NAME_MAX_LENGTH wide characters.
    let length = unsafe { GetUserDefaultLocaleName(buffer.as_mut_ptr(), buffer.len() as i32) };
    (length > 1).then(|| String::from_utf16_lossy(&buffer[..length as usize - 1]))
}

#[cfg(not(windows))]
fn platform_locale() -> Option<String> {
    ["LC_ALL", "LC_MESSAGES", "LANG"]
        .iter()
        .filter_map(|name| std::env::var(name).ok())
        .find(|value| !value.is_empty() && value != "C" && value != "POSIX")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    #[test]
    fn every_message_has_an_english_text_and_a_unique_id() {
        let mut ids = HashSet::new();
        for msg in SAMPLES {
            assert!(ids.insert(msg.id()), "{msg:?} listed twice");
            assert!(!en::text(msg).trim().is_empty(), "{msg:?} has no English text");
        }
        assert_eq!(ids.len(), Msg::COUNT, "SAMPLES misses a message");
    }

    #[test]
    fn missing_texts_fall_back_to_english() {
        for language in ["it", "ja", "xx"] {
            let texts = Texts::new(language);
            for msg in SAMPLES {
                assert_eq!(texts.get(*msg), en::text(msg), "{language} {msg:?}");
            }
        }
        let pseudo = Texts::new(PSEUDO);
        let english = Texts::default().get(Msg::ButtonPrepare);
        let long = pseudo.get(Msg::ButtonPrepare);
        assert_ne!(long, english);
        assert!(long.chars().count() > english.chars().count());
    }

    #[test]
    fn arguments_are_typed_into_the_text() {
        let texts = Texts::default();
        assert_eq!(texts.get(Msg::RomRecognized { label: "Game (EUR)" }), "ROM recognized: Game (EUR)");
        assert_eq!(texts.get(Msg::PreparingElapsed { minutes: 2, seconds: 7 }), "Elapsed: 2:07");
        assert_eq!(texts.get(Msg::ProgressCount { done: 120, total: 400 }), "120 of 400");
    }

    #[test]
    fn explanations_resolve_through_the_language() {
        let error = forge_base::ForgeError::invalid("bad ROM");
        let explanation = forge_core::explain::explain(&error);
        let texts = Texts::new("de");
        assert_eq!(texts.title(&explanation), explanation.title);
        assert_eq!(texts.body(&explanation), explanation.body());
        assert_ne!(Texts::new(PSEUDO).title(&explanation), explanation.title);
    }

    #[test]
    fn the_language_follows_the_rom_in_use() {
        let rom = |codes: &[&str]| codes.iter().map(|code| code.to_string()).collect::<Vec<_>>();
        // Before recognition: the system language when a supported ROM offers it.
        assert_eq!(choose_language(&[], &["it"]), "it");
        assert_eq!(choose_language(&[], &["ja"]), "en");
        assert_eq!(choose_language(&[], &[]), "en");
        // A recognized ROM's languages.
        assert_eq!(choose_language(&rom(&["en", "fr"]), &["it"]), "en");
        assert_eq!(choose_language(&rom(&["en", "it"]), &["it"]), "it");
        assert_eq!(choose_language(&rom(&["ja"]), &["it"]), "ja");
        // The user's choice first, then the system's.
        assert_eq!(choose_language(&rom(&["en", "de", "it"]), &["de", "it"]), "de");
        assert_eq!(choose_language(&rom(&["en"]), &[PSEUDO]), PSEUDO);
    }
}
