// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The window's message table: every text the window shows is a [`Msg`],
//! keyed by id with typed arguments, resolved for a language by [`Texts`].
//! English (`en.rs`) is complete; the other game languages of the supported
//! ROMs (`fr.rs`, `de.rs`, `es.rs`, `it.rs`) return the texts they have and
//! English fills the rest (the tests require every id in every language).
//! The explanation catalogue of forge-core (`forge_core::explain::TEXTS`) is
//! resolved through the same language (`forge_core::explain::translated_text`).
//!
//! The window's languages follow the owner's decision 8: the game languages
//! of the ROM in use (the recognized ROM, else the installed title); before
//! a ROM is known, the system language when a supported ROM provides it,
//! else English. The user's choice is kept in Forge's own settings
//! ([`preference`]).

mod de;
mod en;
mod es;
mod fr;
mod it;
pub mod preference;
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
    // Update and Repair in place (WP-04A).
    Updating,
    Repairing,
    Updated { version: &'a str, kept: bool },
    Repaired { kept: bool },
    AlreadyCurrent,
    UpdateChanged { list: &'a str },
    RepairChanged { list: &'a str },
    ChangeCreated { file: &'a str },
    ChangeStandardLayout,
    ChangeLanguage,
    ChangeRemoved { file: &'a str },
    ChangeOther { file: &'a str },
    HudLayoutQuestion,
    HudLayoutBody,
    HudLayoutField { name: &'a str, yours: &'a str, standard: &'a str },
    ButtonKeepMine,
    ButtonUseStandard,
    DowngradeHeading,
    DowngradeBody { installed: &'a str, present: &'a str },
    ButtonInstallOlder,
    // Update from ZIP (WP-04B).
    ButtonUpdateFromZip,
    PickUpdateZipTitle,
    CheckingUpdatePackage,
    ZipPreviewHeading { version: &'a str },
    ZipPreviewBody { installed: &'a str, version: &'a str },
    ZipSigned { key: &'a str },
    ZipUnsigned,
    ZipLocallyModified { list: &'a str },
    ButtonUpdateNow,
    UpdateHandingOff,
    UpdateRolledBack { error: &'a str },
    UpdateRefused { error: &'a str },
    UpdateRecoveryNeeded { error: &'a str },
    UpdateRecovered,
    UpdateCompleted,
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
    DownloadsFolderNote,
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
    UninstallMacApp { path: &'a str },
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
    SteamDeckHeading,
    SteamDeckBody,
    // About.
    Version { version: &'a str },
    AboutBody,
    SupportedRevisions,
    NoSupportedRevisions,
    ProjectPage,
    UserGuide,
    ForgeLanguage,
    ForgeLanguageNote,
    // Enhancements (WP-07).
    StateDownloaded { version: &'a str },
    StateNotDownloaded,
    StateOn,
    StateOff,
    StateApplied,
    StateNotApplied,
    StateApplyPending,
    StateAppliedLater,
    HdPreferenceNotSaved { error: &'a str },
    TexturesHeading,
    TexturesDescription,
    TexturesImported { files: u64 },
    TexturesNone,
    TexturesLoadCheckbox,
    TexturesApplied,
    TexturesShadowedSettings,
    TexturesShadowedEnvironment,
    TexturesShadowedAzahar,
    TexturesOlderProfile,
    TexturesLegacy,
    ButtonUseThisFolder,
    ButtonImportTextureFolder,
    ButtonImportTextureZip,
    ButtonImportLegacy,
    TexturesImporting,
    TexturesChoosing,
    TexturesImportDone { textures: u64, files: u64 },
    TexturesImportOtherTitle { title: &'a str },
    TexturesImportFailed { error: &'a str },
    TexturesSettingsFailed { error: &'a str },
    TexturesFlatQuestion,
    ButtonPlaceIt,
    PickTexturesFolder,
    PickTexturesZip,
    CustomRomfsHeading,
    CustomRomfsWarning,
    CustomRomfsDescription,
    CustomRomfsFiles { files: u64 },
    CustomRomfsNamed { name: &'a str, files: u64 },
    CustomRomfsEmpty,
    CustomRomfsRejected { reason: &'a str },
    CustomRomfsFlagged { reason: &'a str },
    CustomRomfsCheckbox,
    CustomRomfsConfirm,
    ButtonTurnOn,
    CustomRomfsApplied { files: u64 },
    ApplyHeading,
    ApplyBody,
    ApplyPending,
    ApplyCurrent,
    ApplyNotInstalled,
    ButtonApplyChanges,
    HudLayoutPolicyKeep,
    HudLayoutPolicyAsk,
    ApplyingEnhancements,
    ApplyChanged { list: &'a str },
    EnhancementsLoading,
    EnhancementsGameRunning,
    EnhancementsStatusFailed { error: &'a str },
    SettingNotSaved { error: &'a str },
    SavingSetting,
    DataInactive,
    DataOldProfileNote,
    EnhancementsApplied { kept: bool },
    ForgeLanguageNotSaved { error: &'a str },
    // Units.
    Size { value: &'a str, power: u64 },
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
        PrivateData { path: "<P>" },
        PreparingHeading,
        PreparingElapsed { minutes: 1, seconds: 5 },
        ProgressCount { done: 1, total: 2 },
        PreparingElsewhere,
        PreparingElsewhereBody,
        ReadyHeading { revision: "<R>", language: "<L>" },
        ReadyHeadingNoLanguage { revision: "<R>" },
        ReadyBody,
        PrepareAgain,
        UpdateHeading { installed: "<1>", present: "<2>" },
        UpdateHeadingVersion { present: "<2>" },
        UpdateHeadingPlain,
        UpdateBody,
        RepairBody,
        NeedsGameInputHeading,
        NeedsGameInputBody,
        Updating,
        Repairing,
        Updated { version: "<1>", kept: true },
        Repaired { kept: false },
        AlreadyCurrent,
        UpdateChanged { list: "<L>" },
        RepairChanged { list: "<L>" },
        ChangeCreated { file: "<F>" },
        ChangeStandardLayout,
        ChangeLanguage,
        ChangeRemoved { file: "<F>" },
        ChangeOther { file: "<F>" },
        HudLayoutQuestion,
        HudLayoutBody,
        HudLayoutField { name: "<N>", yours: "<Y>", standard: "<S>" },
        ButtonKeepMine,
        ButtonUseStandard,
        DowngradeHeading,
        DowngradeBody { installed: "<2>", present: "<1>" },
        ButtonInstallOlder,
        ButtonUpdateFromZip,
        PickUpdateZipTitle,
        CheckingUpdatePackage,
        ZipPreviewHeading { version: "<V>" },
        ZipPreviewBody { installed: "<I>", version: "<V>" },
        ZipSigned { key: "<K>" },
        ZipUnsigned,
        ZipLocallyModified { list: "<L>" },
        ButtonUpdateNow,
        UpdateHandingOff,
        UpdateRolledBack { error: "<E>" },
        UpdateRefused { error: "<E>" },
        UpdateRecoveryNeeded { error: "<E>" },
        UpdateRecovered,
        UpdateCompleted,
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
        RomRecognized { label: "<L>" },
        LanguagesUnreadable { error: "<E>" },
        LanguageChosen { label: "<L>" },
        LanguageSettingsUnreadable { error: "<E>" },
        Starting,
        DownloadingHdFirst,
        DownloadingModFirst,
        ReadyDialog,
        CloseWhileBusy,
        ConfirmCancel,
        EnhancementsBody,
        HdHeading,
        HdDescription { title: "<T>", version: "<V>", author: "<A>", credits: "<C>", license: "<L>" },
        HdDownload,
        SelectZip,
        SelectFolder,
        ModPage,
        License,
        HdAutoDownload,
        NotAvailable,
        CheckingStored,
        HdCheckFailed { error: "<E>" },
        HdReady,
        HdArchive,
        HdMissingAuto { megabytes: 451 },
        HdMissing,
        ModHeading,
        ModDescription { title: "<T>", version: "<V>", author: "<A>", testing: "<T>", tools: "<O>", license: "<L>" },
        ModCheckbox { title: "<T>" },
        ModDownload,
        SelectRar,
        ModSettingsUnreadable { error: "<E>" },
        ModCheckFailed { error: "<E>" },
        ModMissingEnabled,
        ModOff,
        ModOn,
        ModOffKept,
        ApplyModChange,
        HdDownloadFailed { error: "<E>" },
        ModDownloadFailed { error: "<E>" },
        ManualHdFailure { error: "<E>", archive: "<A>", page: "<P>" },
        ManualModFailure { error: "<E>", archive: "<A>", page: "<P>" },
        VerifyingArchive,
        VerifyingTextures,
        VerifyingFiles,
        DownloadingHd,
        DownloadingMod { title: "<T>" },
        HdReadyReinstall,
        ModReadyReinstall { title: "<T>" },
        HdArchiveApplied,
        DownloadsFolderNote,
        ModSettingSaveFailed { error: "<E>" },
        RevisionsUnreadable { error: "<E>" },
        PickRomTitle,
        FilterRom,
        Filter3ds,
        FilterCci,
        PickExtractedTitle,
        PickArchive { name: "<N>" },
        PickArchiveFallbackHd,
        PickArchiveFallbackMod,
        FilterTexturePack,
        PickFolder { title: "<T>" },
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
        BackupPanelBody { files: 2, size: "<S>" },
        BackupOptionalHeading,
        BackupExcludedNote,
        BackupDone { path: "<P>", size: "<S>" },
        FilterBackup,
        SaveBackupTitle,
        OpenBackupTitle,
        RestoreHeading { created: "<C>", platform: "<P>" },
        RestoreOtherPlatform { platform: "<P>" },
        RestoreDifferentRevision,
        RestoreUnavailable { path: "<P>" },
        RestoreKeptExisting { path: "<P>" },
        RestoreGroupLine { label: "<L>", replaced: 1, added: 2, removed: 3 },
        RestoreKeptSettings { files: "<F>" },
        RestoreSkipped { labels: "<L>" },
        RestoreSnapshotNote { path: "<P>" },
        RestoreDone { path: "<P>" },
        ClearCacheBody { size: "<S>" },
        ClearCacheEmpty,
        ClearCacheDone,
        RemoveGameBody { size: "<S>" },
        RemoveGameKeep,
        RemoveGameDone,
        RebuildBody { size: "<S>" },
        RebuildNoSource,
        RebuildDone,
        UninstallWindows { path: "<P>" },
        UninstallPortable { path: "<P>" },
        UninstallAppImage { appimage: "<A>", path: "<P>" },
        UninstallFlatpak { id: "<I>", path: "<P>" },
        UninstallAndroid { path: "<P>" },
        UninstallMacApp { path: "<P>" },
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
        StateLabel { state: "<S>" },
        Copied,
        LastGameRun { started: "<T>", seconds: 1, meaning: "<M>" },
        GameRunning,
        NoGameRun,
        CollectingDiagnostics,
        DiagnosticsFailed { error: "<E>" },
        GameLogFolder,
        BundleContents,
        BundleOmitted { name: "<N>", reason: "<R>" },
        ButtonSaveBundle,
        SaveBundleTitle,
        FilterZip,
        BundleSaved { path: "<P>" },
        BundleFailed { error: "<E>" },
        SteamDeckHeading,
        SteamDeckBody,
        Version { version: "<V>" },
        AboutBody,
        SupportedRevisions,
        NoSupportedRevisions,
        ProjectPage,
        UserGuide,
        ForgeLanguage,
        ForgeLanguageNote,
        StateDownloaded { version: "<V>" },
        StateNotDownloaded,
        StateOn,
        StateOff,
        StateApplied,
        StateNotApplied,
        StateApplyPending,
        StateAppliedLater,
        HdPreferenceNotSaved { error: "<E>" },
        TexturesHeading,
        TexturesDescription,
        TexturesImported { files: 2 },
        TexturesNone,
        TexturesLoadCheckbox,
        TexturesApplied,
        TexturesShadowedSettings,
        TexturesShadowedEnvironment,
        TexturesShadowedAzahar,
        TexturesOlderProfile,
        TexturesLegacy,
        ButtonUseThisFolder,
        ButtonImportTextureFolder,
        ButtonImportTextureZip,
        ButtonImportLegacy,
        TexturesImporting,
        TexturesChoosing,
        TexturesImportDone { textures: 3, files: 4 },
        TexturesImportOtherTitle { title: "<T>" },
        TexturesImportFailed { error: "<E>" },
        TexturesSettingsFailed { error: "<E>" },
        TexturesFlatQuestion,
        ButtonPlaceIt,
        PickTexturesFolder,
        PickTexturesZip,
        CustomRomfsHeading,
        CustomRomfsWarning,
        CustomRomfsDescription,
        CustomRomfsFiles { files: 2 },
        CustomRomfsNamed { name: "<N>", files: 2 },
        CustomRomfsEmpty,
        CustomRomfsRejected { reason: "<R>" },
        CustomRomfsFlagged { reason: "<R>" },
        CustomRomfsCheckbox,
        CustomRomfsConfirm,
        ButtonTurnOn,
        CustomRomfsApplied { files: 2 },
        ApplyHeading,
        ApplyBody,
        ApplyPending,
        ApplyCurrent,
        ApplyNotInstalled,
        ButtonApplyChanges,
        HudLayoutPolicyKeep,
        HudLayoutPolicyAsk,
        ApplyingEnhancements,
        ApplyChanged { list: "<L>" },
        EnhancementsLoading,
        EnhancementsGameRunning,
        EnhancementsStatusFailed { error: "<E>" },
        SettingNotSaved { error: "<E>" },
        SavingSetting,
        DataInactive,
        DataOldProfileNote,
        EnhancementsApplied { kept: true },
        ForgeLanguageNotSaved { error: "<E>" },
        Size { value: "<V>", power: 2 },
        GuiFailed { error: "<E>" },
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

/// The label of a game language: its native name with its accents
/// ("Fran\u{e7}ais"), whatever ASCII label the runtime reported.
pub fn language_label(code: &str, reported: &str) -> String {
    let name = native_name(code);
    if name == code {
        reported.to_string()
    } else {
        name.to_string()
    }
}

/// A language's own texts; None for a text it does not have yet.
type Table = fn(&Msg) -> Option<String>;

/// The languages with a translated table (besides English).
#[cfg(test)]
pub const TRANSLATED: [&str; 4] = ["fr", "de", "es", "it"];

/// The translated tables. English is complete and is the fallback.
fn table(code: &str) -> Option<Table> {
    match code {
        ENGLISH => Some(|msg| Some(en::text(msg))),
        "fr" => Some(fr::text),
        "de" => Some(de::text),
        "es" => Some(es::text),
        "it" => Some(it::text),
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
    pub fn catalogue(&self, key: &str, english: &str) -> String {
        if self.language == PSEUDO {
            return pseudo::localize(english);
        }
        forge_core::explain::translated_text(&self.language, key).unwrap_or(english).to_string()
    }

    /// The label of a stage of the preparation (`stage.<name>`), when the
    /// catalogue knows it.
    pub fn stage(&self, stage: &str) -> Option<String> {
        let key = format!("stage.{stage}");
        forge_core::explain::text(&key).map(|english| self.catalogue(&key, english))
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
        // The detail labels (stage, operation) in this language; the
        // engine's own message stays as it was written.
        let details = if self.language == PSEUDO {
            explanation.details.clone()
        } else {
            explanation.localized(&self.language).details
        };
        if !details.is_empty() {
            body.push_str("\n\n");
            body.push_str(&details);
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

    /// The arguments a text shows (`<P>`, the samples' string arguments) and
    /// its numbers (digit runs: sample numbers, F1, 3DS), in order of value.
    fn placeholders(text: &str) -> Vec<String> {
        let mut found = Vec::new();
        let mut rest = text;
        while let Some(start) = rest.find('<') {
            match rest[start..].find('>') {
                Some(end) => {
                    found.push(rest[start..start + end + 1].to_string());
                    rest = &rest[start + end + 1..];
                }
                None => break,
            }
        }
        let mut digits = String::new();
        for c in text.chars().chain(std::iter::once(' ')) {
            if c.is_ascii_digit() {
                digits.push(c);
            } else if !digits.is_empty() {
                found.push(std::mem::take(&mut digits));
            }
        }
        found.sort();
        found
    }

    #[test]
    fn every_language_translates_every_message() {
        for language in TRANSLATED {
            let table = table(language).unwrap();
            for msg in SAMPLES {
                let english = en::text(msg);
                let text = table(msg).unwrap_or_else(|| panic!("{language} has no text for {}", msg.id()));
                assert!(!text.trim().is_empty(), "{language} {msg:?} is empty");
                assert_eq!(placeholders(&text), placeholders(&english), "{language} {msg:?}: {text}");
                assert_eq!(
                    text.matches('\n').count(),
                    english.matches('\n').count(),
                    "{language} {msg:?} changes the line breaks"
                );
            }
        }
    }

    #[test]
    fn every_language_resolves_the_whole_catalogue() {
        for language in TRANSLATED {
            let texts = Texts::new(language);
            for (key, english) in forge_core::explain::TEXTS {
                assert!(
                    forge_core::explain::translated_text(language, key).is_some(),
                    "{language} has no catalogue text {key}"
                );
                assert!(!texts.catalogue(key, english).trim().is_empty(), "{language} {key}");
            }
        }
    }

    #[test]
    fn language_labels_are_native_names() {
        assert_eq!(language_label("fr", "Francais"), "Fran\u{e7}ais");
        assert_eq!(language_label("es", "Espanol"), "Espa\u{f1}ol");
        assert_eq!(language_label("xx", "Other"), "Other");
    }

    #[test]
    fn missing_texts_fall_back_to_english() {
        for language in ["ja", "xx"] {
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
        assert_eq!(Texts::default().title(&explanation), explanation.title);
        assert_eq!(Texts::default().body(&explanation), explanation.body());
        assert_eq!(Texts::new("ja").title(&explanation), explanation.title);
        let texts = Texts::new("de");
        assert_ne!(texts.title(&explanation), explanation.title);
        assert_eq!(texts.title(&explanation), explanation.localized("de").title);
        assert_ne!(Texts::new(PSEUDO).title(&explanation), explanation.title);
        assert_eq!(
            Texts::new("it").stage("verify").as_deref(),
            forge_core::explain::translated_text("it", "stage.verify")
        );
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
