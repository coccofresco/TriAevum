// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! English, the complete table and every other language's fallback. Texts
//! the Python Forge window showed keep its wording (`forge_gui.py`).

use super::Msg;

pub fn text(msg: &Msg) -> String {
    use Msg::*;
    let fixed = match *msg {
        WindowTitle => "TriAevum Forge \u{2014} Echoes of Time",
        DialogTitle => "TriAevum Forge",
        NavHome => "Home",
        NavEnhancements => "Enhancements",
        NavData => "Data & backups",
        NavDiagnostics => "Diagnostics",
        NavAbout => "About",
        Checking => "Checking the local installation...",
        PickRom => "Pick your decrypted ROM (.3ds or .cci)",
        RomLabel => "Decrypted ROM",
        Browse => "Browse...",
        UseExtracted => "Use extracted data...",
        WhatForgeNeeds => "What Forge needs",
        Subtitle => {
            "Select your own decrypted Nintendo 3DS cartridge image. Forge accepts .3ds and .cci ROMs, extracts the \
             required native data, and does not handle encryption keys. You can also select a folder containing \
             decompressed code.bin, exheader.bin and romfs.bin."
        }
        PrivateData { path } => return format!("Private generated data: {path}"),
        PreparingHeading => "Preparing the game",
        PreparingElapsed { minutes, seconds } => return format!("Elapsed: {minutes}:{seconds:02}"),
        ProgressCount { done, total } => return format!("{done} of {total}"),
        PreparingElsewhere => "Another Forge is preparing this installation",
        PreparingElsewhereBody => "Wait until it finishes, then check the installation again.",
        ReadyHeading { revision, language } => return format!("Ready to play: {revision}, {language}"),
        ReadyHeadingNoLanguage { revision } => return format!("Ready to play: {revision}"),
        ReadyBody => "Your saves and settings stay in your data folder.",
        PrepareAgain => "Prepare again from a ROM",
        UpdateHeading { installed, present } => {
            return format!("A new TriAevum package is here ({installed} -> {present}).")
        }
        UpdateHeadingVersion { present } => return format!("A new TriAevum package is here (version {present})."),
        UpdateHeadingPlain => "A new TriAevum package is here.",
        UpdateBody => "Select your ROM again, then Update. Your saves are kept.",
        RepairBody => "Select your ROM again, then Repair. Your saves are kept.",
        NeedsGameInputHeading => "Your game files are needed again to repair this installation",
        NeedsGameInputBody => "You can still play. Select your ROM again, then Repair.",
        ShowDetails => "Show details",
        HideDetails => "Hide details",
        Dismiss => "Dismiss",
        HomeHdDownload { megabytes } => {
            return format!(
                "HD interface textures ({megabytes} MB) are downloaded with the preparation. You can change this \
                 under Enhancements."
            )
        }
        HomeHdReady => "HD interface textures are stored and applied with the preparation.",
        HomeHdOff => "HD interface textures are off: the game uses its native textures.",
        ButtonPrepare => "Prepare",
        ButtonPlay => "Play",
        ButtonUpdate => "Update",
        ButtonRepair => "Repair",
        ButtonCancel => "Cancel",
        ButtonCheckInstallation => "Check installation",
        ButtonChooseRom => "Choose ROM...",
        ButtonCopyDiagnostics => "Copy diagnostics",
        ButtonViewDiagnostics => "View diagnostics",
        ButtonSupportedRevisions => "Supported revisions",
        ButtonDataPage => "Data & backups",
        ButtonRetry => "Retry",
        ButtonContinue => "Continue",
        ButtonOk => "OK",
        ButtonYes => "Yes",
        ButtonNo => "No",
        ButtonOpenFolder => "Open folder",
        StatusSelectRom => "Select your ROM to begin.",
        StatusChooseLanguage => "ROM recognized: choose the game language, then Prepare.",
        StatusReady => "Ready to play.",
        StatusActionNeeded => "Action needed: see Home.",
        StatusCancelled => "Preparation cancelled. What was installed before stays usable.",
        StatusPreparingElsewhere => "Another Forge is preparing this installation.",
        LanguageSaved => "Game language saved. Applies on the next full game start.",
        NoLanguages => "Select your ROM: the game languages it offers appear here.",
        GameLanguage => "Game language",
        Recognizing => "Recognizing the ROM...",
        RomNotRecognized => "ROM not recognized",
        ChooseLanguage => "Choose the game language:",
        LanguageForInstall => "\"Prepare\" sets up the game in this language. You can change it later here or in game.",
        LanguageInstalled => "Changes apply on the next full game start.",
        RomRecognized { label } => return format!("ROM recognized: {label}"),
        LanguagesUnreadable { error } => {
            return format!("The game languages are read during the installation. {error}")
        }
        LanguageChosen { label } => return format!("Game language for this installation: {label}."),
        LanguageSettingsUnreadable { error } => return format!("Cannot read game language settings: {error}"),
        Starting => "Starting title preparation...",
        DownloadingHdFirst => "Downloading the HD textures before the title preparation...",
        DownloadingModFirst => "Downloading the enabled mod before the title preparation...",
        ReadyDialog => "The title is ready. You can launch TriAevum \u{2014} Echoes of Time now.",
        CloseWhileBusy => "Title preparation is still running. Close Forge?",
        ConfirmCancel => "Stop the preparation? What was installed before stays usable.",
        EnhancementsBody => {
            "Optional enhancements are applied by the next preparation. Without them the game uses its native \
             assets."
        }
        HdHeading => "HD UI textures (recommended)",
        HdDescription { title, version, author, credits, license } => {
            return format!(
                "{title} {version} by {author} (credits: {credits}): HD textures for the HUD, the menus and \
                 TopScreen, in the game language you choose here or in game. Licensed {license}: TriAevum does not \
                 distribute it. Forge downloads the author's original archive from GameBanana, verifies it and \
                 keeps it in your private data folder. If the download does not work, download the zip from the mod \
                 page and select it here. Thanks to RLG (M-1) for SingleScreen, the mod TriAevum's TopScreen is \
                 based on; its HD font is included with TriAevum."
            )
        }
        HdDownload => "Download HD textures",
        SelectZip => "Select .zip...",
        SelectFolder => "Select extracted folder...",
        ModPage => "Mod page",
        License => "License",
        HdAutoDownload => "Download the HD textures with \"Prepare\" when they are not stored yet",
        NotAvailable => "Not available for the supported game revisions of this package.",
        CheckingStored => "Checking the stored files...",
        HdCheckFailed { error } => return format!("Cannot check the HD textures: {error}"),
        HdReady => "HD textures ready. They are applied when you prepare and install the title.",
        HdArchive => "HD texture archive stored. It is prepared and applied when you prepare and install the title.",
        HdMissingAuto { megabytes } => {
            return format!(
                "Not stored yet: the HD textures ({megabytes} MB) are downloaded when you prepare and install the \
                 title. Without them the game uses the native textures."
            )
        }
        HdMissing => "Not installed: the game uses the native textures.",
        ModHeading => "Optional mods",
        ModDescription { title, version, author, testing, tools, license } => {
            return format!(
                "{title} {version} by {author}: higher-fidelity models for Link and his items. Testing: {testing}. \
                 Tools: {tools}. Licensed {license}: TriAevum does not distribute it. Forge downloads the author's \
                 original archive from GameBanana, verifies it and applies it to your local game data only. If the \
                 download does not work, download the archive from the mod page and select it here."
            )
        }
        ModCheckbox { title } => return format!("Use {title} (applied by \"Prepare\")"),
        ModDownload => "Download mod",
        SelectRar => "Select .rar...",
        ModSettingsUnreadable { error } => return format!("Cannot read the mod settings: {error}"),
        ModCheckFailed { error } => return format!("Cannot check the mod files: {error}"),
        ModMissingEnabled => "Not downloaded. It is downloaded when you prepare and install the title.",
        ModOff => "Off: the game uses its native models.",
        ModOn => "On: applied when you prepare and install the title.",
        ModOffKept => "Off: the game uses its native models. The downloaded archive is kept.",
        ApplyModChange => " Prepare and install the title again to apply the change.",
        HdDownloadFailed { error } => {
            return format!(
                "HD texture download failed: {error} The title is prepared with the native textures. Download the \
                 zip from the mod page and select it with \"Select .zip...\", then prepare and install again."
            )
        }
        ModDownloadFailed { error } => {
            return format!(
                "Mod download failed: {error} The title is prepared with the native models. Download the archive \
                 from the mod page and select it with \"Select .rar...\"."
            )
        }
        ManualHdFailure { error, archive, page } => {
            return format!(
                "{error}\n\nDownload {archive} from the mod page ({page}) and select it with \"Select .zip...\"."
            )
        }
        ManualModFailure { error, archive, page } => {
            return format!(
                "{error}\n\nDownload {archive} from the mod page ({page}) and select it with \"Select .rar...\"."
            )
        }
        VerifyingArchive => "Verifying the selected archive...",
        VerifyingTextures => "Verifying the extracted textures...",
        VerifyingFiles => "Verifying the extracted files...",
        DownloadingHd => "Downloading the HD textures...",
        DownloadingMod { title } => return format!("Downloading {title}..."),
        HdReadyReinstall => "HD textures ready. Prepare and install the title again to apply them.",
        ModReadyReinstall { title } => {
            return format!("{title} is ready. Prepare and install the title again to apply it.")
        }
        HdArchiveApplied => "HD texture archive ready. It is applied by this installation.",
        ModSettingSaveFailed { error } => return format!("Cannot save the mod setting: {error}"),
        RevisionsUnreadable { error } => return format!("Cannot read the supported game revisions: {error}"),
        PickRomTitle => "Select a decrypted .3ds or .cci ROM",
        FilterRom => "Nintendo 3DS ROM",
        Filter3ds => "3DS image",
        FilterCci => "CCI image",
        PickExtractedTitle => "Select extracted title data",
        PickArchive { name } => return format!("Select {name}"),
        PickArchiveFallbackHd => "Select the texture pack zip",
        PickArchiveFallbackMod => "Select the archive",
        FilterTexturePack => "Texture pack archive",
        PickFolder { title } => return format!("Select the extracted {title} folder"),
        FilterModArchive => "Mod archive",
        DataBody => {
            "Your saves, settings, downloads and the prepared game are kept in this folder. Keep it when you move \
             or update TriAevum."
        }
        DataFolder => "Data folder",
        ForgeLog => "Forge log",
        DiagnosticsBody => {
            "What Forge knows about this installation. Copy it into a bug report; user names in paths are removed."
        }
        StateLabel { state } => return format!("Installation state: {state}"),
        Copied => "Copied to the clipboard.",
        Version { version } => return format!("Version {version}"),
        AboutBody => {
            "TriAevum Forge prepares TriAevum \u{2014} Echoes of Time from your own decrypted ROM. It is free \
             software under the GNU General Public License, version 3 or later, and ships no game data."
        }
        SupportedRevisions => "Supported game revisions",
        NoSupportedRevisions => "This package lists no supported game revisions.",
        ProjectPage => "Project page",
        ForgeLanguage => "Forge language",
        ForgeLanguageNote => "The languages of your game. Texts not translated yet are shown in English.",
        GuiFailed { error } => {
            return format!(
                "TriAevum Forge GUI failed: {error}\n\nThe command line tool triaevum-forge can still prepare titles."
            )
        }
    };
    fixed.to_string()
}
