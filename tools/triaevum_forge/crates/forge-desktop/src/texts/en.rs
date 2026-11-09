// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! English, the complete table and every other language's fallback. Texts
//! the Python Forge window showed keep its wording (`forge_gui.py`).

use super::Msg;

pub fn text(msg: &Msg) -> String {
    use Msg::*;
    let fixed = match *msg {
        WindowTitle => return format!("TriAevum Forge \u{2014} {}", super::TITLE_NAME),
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
        UpdateBody => {
            "Update installs the new package for your prepared game. Your ROM is not needed; your saves and \
             settings are kept."
        }
        RepairBody => {
            "Repair rebuilds what is missing from the game data kept from your ROM. Your saves and settings are \
             kept."
        }
        NeedsGameInputHeading => "Your game files are needed again to repair this installation",
        NeedsGameInputBody => "You can still play. Select your ROM again, then Repair.",
        Updating => "Installing the update...",
        Repairing => "Repairing the installation...",
        Updated { version, kept } => {
            let version = if version.is_empty() { String::new() } else { format!(" to {version}") };
            let kept = if kept { "Your saves and settings were kept." } else { "Your saves were kept." };
            return format!("Updated{version}. {kept}");
        }
        Repaired { kept } => {
            let kept = if kept { "Your saves and settings were kept." } else { "Your saves were kept." };
            return format!("Repaired. {kept}");
        }
        AlreadyCurrent => "This installation already uses this package.",
        UpdateChanged { list } => return format!("Update changed: {list}"),
        RepairChanged { list } => return format!("Repair changed: {list}"),
        ChangeCreated { file } => return format!("{file} was created with its defaults"),
        ChangeStandardLayout => "the HUD layout was reset to the standard profile",
        ChangeLanguage => "the game language you chose was selected",
        ChangeRemoved { file } => return format!("{file} was removed"),
        ChangeOther { file } => return format!("{file} was changed"),
        HudLayoutQuestion => "Reset the HUD layout to the standard profile?",
        HudLayoutBody => {
            "Your HUD layout differs from the standard profile. Keep mine leaves it as it is; Use standard changes \
             these settings:"
        }
        HudLayoutField { name, yours, standard } => return format!("{name}: yours {yours}, standard {standard}"),
        ButtonKeepMine => "Keep mine",
        ButtonUseStandard => "Use standard",
        DowngradeHeading => "Install an older package?",
        DowngradeBody { installed, present } => {
            return format!(
                "This package ({present}) is older than the installed one ({installed}). Install the older version \
                 anyway?"
            )
        }
        ButtonInstallOlder => "Install older version",
        ButtonUpdateFromZip => "Update from a downloaded ZIP...",
        PickUpdateZipTitle => "Choose the downloaded TriAevum ZIP",
        CheckingUpdatePackage => "Checking the update package...",
        ZipPreviewHeading { version } => return format!("Update to {version}?"),
        ZipPreviewBody { installed, version } => return format!("The TriAevum program files of this installation are replaced ({installed} becomes {version}). Your saves, settings, save states, mods and downloads are kept. Forge closes during the update and opens again when it is done."),
        ZipSigned { key } => return format!("Signed by the TriAevum publisher (key {key})."),
        ZipUnsigned => "Warning: this package is not signed by the TriAevum publisher. It is accepted only because the developer override is on.",
        ZipLocallyModified { list } => return format!("These package files were changed on this computer and are replaced: {list}"),
        ButtonUpdateNow => "Update",
        UpdateHandingOff => "Updating: Forge closes now and opens again when the update is finished.",
        UpdateRolledBack { error } => return format!("The update did not finish, so the previous version was restored. Your saves and settings were kept.\n\n{error}"),
        UpdateRefused { error } => return format!("The update did not start; nothing was changed.\n\n{error}"),
        UpdateRecoveryNeeded { error } => return format!("The interrupted update could not be undone automatically. Choose Repair to try again; Play stays blocked until then.\n\n{error}"),
        UpdateRecovered => "The interrupted update was undone; the previous version is back.",
        UpdateCompleted => "The interrupted update was completed.",
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
        ReadyDialog => return format!("The title is ready. You can launch TriAevum \u{2014} {} now.", super::TITLE_NAME),
        CloseWhileBusy => "Title preparation is still running. Close Forge?",
        ConfirmCancel => "Stop the preparation? What was installed before stays usable.",
        EnhancementsBody => "Choose the enhancements you want, then press Apply changes. Without them the game uses its native assets.",
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
        HdAutoDownload => "Use the HD interface pack (downloaded with Prepare or Apply changes when it is not stored yet)",
        NotAvailable => "Not available for the supported game revisions of this package.",
        CheckingStored => "Checking the stored files...",
        HdCheckFailed { error } => return format!("Cannot check the HD textures: {error}"),
        HdReady => "HD textures ready. They are applied by Apply changes and by the next preparation.",
        HdArchive => "HD texture archive stored. It is prepared and applied by Apply changes and by the next preparation.",
        HdMissingAuto { megabytes } => return format!("Not stored yet: the HD textures ({megabytes} MB) are downloaded when you prepare the title or apply changes. Without them the game uses the native textures."),
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
        ModCheckbox { title } => return format!("Use {title}"),
        ModDownload => "Download mod",
        SelectRar => "Select .rar...",
        ModSettingsUnreadable { error } => return format!("Cannot read the mod settings: {error}"),
        ModCheckFailed { error } => return format!("Cannot check the mod files: {error}"),
        ModMissingEnabled => "Not downloaded. It is downloaded when you apply changes or prepare the title.",
        ModOff => "Off: the game uses its native models.",
        ModOn => "On: applied by Apply changes and by the next preparation.",
        ModOffKept => "Off: the game uses its native models. The downloaded archive is kept.",
        ApplyModChange => " Press Apply changes to apply it.",
        HdDownloadFailed { error } => return format!("HD texture download failed: {error} The title is prepared with the native textures. Download the zip from the mod page and select it with \"Select .zip...\", then apply changes."),
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
        HdReadyReinstall => "HD textures ready. Press Apply changes to use them.",
        ModReadyReinstall { title } => return format!("{title} is ready. Press Apply changes to use it."),
        HdArchiveApplied => "HD texture archive ready. It is applied by this installation.",
        DownloadsFolderNote => {
            "Forge keeps the archives it downloads in this folder, and a checked copy of each archive you select. \
             Without a network, download the archive from the mod page on any device and select it with the \
             buttons above."
        }
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
            "Where TriAevum keeps your files, what a backup holds, and what is safe to clean up. Keep the data \
             folder when you move or update TriAevum."
        }
        DataFolder => "Data folder",
        ForgeLog => "Forge log",
        DataYourFiles => "Your files",
        DataEntryLabel { name } => {
            return match name {
                "save_data" => "Saves",
                "save_states" => "Save states",
                "save_states_default" => "Save states left in the earlier folder",
                "config" => "Settings",
                "runtime_config" => "Game settings file",
                "topscreen_config" => "HUD layout",
                "controls_config" => "Controls",
                "game_language" => "Game language",
                "romfs_mods" => "Mod choices",
                "savestates_index" => "Save-state list",
                "environments" => "Environments",
                "imgui_layout" => "F1 window layout",
                "imgui_layout_legacy" => "F1 window layout of an older version",
                "android_host_config" => "Android display settings",
                "touch_preferences" => "Touch controls",
                "texture_load" => "Your texture pack",
                "texture_dump" => "Dumped textures",
                "mods" => "Mods",
                "downloads" => "Downloads",
                "sources" => "Game data kept from your ROM",
                "titles" => "Prepared game",
                "cache" => "Prepared caches",
                "shader_seeds" => "Graphics preparation data",
                "interior_sun" => "Interior lighting data",
                "renderer_cache" => "Renderer cache",
                "translator_cache" => "Translator cache",
                "game_logs" => "Game logs",
                "forge_logs" => "Forge logs",
                "crash_dumps" => "Crash reports",
                "android_diagnostics" => "Startup reports",
                "texture_load_legacy" => "Texture pack in Azahar's folder",
                "save_states_legacy" => "Save states in the earlier folder",
                "game_logs_legacy" => "Game logs in the earlier folder",
                "crash_dumps_legacy" => "Crash reports in the earlier folder",
                "active_title" => "Installed game record",
                other => other,
            }
            .to_string()
        }
        DataCategoryHelp { category } => {
            return match category {
                "saves" => "The game's own save files, as on the cartridge. Always in your backups.",
                "save_states" => {
                    "Save states (F5/F8 and the save-state menu) with their pictures. Always in your backups."
                }
                "settings" => {
                    "Your settings: graphics, HUD layout, controls, game language and environments, and the layout of \
                     the F1 window. Always in your backups."
                }
                "textures" => {
                    "A texture pack you added yourself, and textures dumped from the game. Your pack can be added to \
                     a backup; dumps are never backed up."
                }
                "enhancements" => {
                    "Mods and downloaded archives. They can be downloaded again, so a backup includes them only if \
                     you choose it."
                }
                "generated" => {
                    "Made by Forge from your ROM. Never backed up: Forge makes it again from your ROM, and it must \
                     not be shared."
                }
                "logs" => "Logs and crash reports for troubleshooting. Never backed up; Diagnostics uses them.",
                _ => "Files TriAevum uses.",
            }
            .to_string()
        }
        DataNotCreated => "not created yet",
        DataOriginDefault => "Default location",
        DataOriginExplicit => "Set by you",
        DataOriginLegacy => "Older version",
        DataBackupInclude => "In every backup",
        DataBackupOptional => "In a backup if you choose it",
        DataBackupExclude => "Not backed up",
        ButtonWhatIsThis => "What is this?",
        ButtonHideHelp => "Hide help",
        DataActionsHeading => "Back up, restore and clean up",
        ButtonBackUp => "Back up your files...",
        ButtonRestore => "Restore...",
        ButtonClearCache => "Clear renderer cache",
        ButtonRebuild => "Rebuild prepared assets",
        ButtonRemoveGame => "Remove prepared game...",
        ButtonUninstallHelp => "Uninstall help",
        ButtonChooseBackupFile => "Choose where to save...",
        ButtonConfirmRestore => "Restore these files",
        ButtonConfirmClear => "Clear the cache",
        ButtonConfirmRebuild => "Rebuild now",
        ButtonConfirmRemove => "Remove the prepared game",
        ButtonRecover => "Finish the recovery",
        BackupPanelBody { files, size } => {
            return format!(
                "A backup holds your saves, save states and settings: {files} files, {size}. It is one .zip file you \
                 can keep anywhere."
            )
        }
        BackupOptionalHeading => "Also include (not selected by default):",
        BackupExcludedNote => {
            "Never included: the game data made from your ROM, caches, logs, crash reports and installation \
             records. Forge makes them again, or they are not needed."
        }
        BackupDone { path, size } => return format!("Backup saved ({size}): {path}"),
        FilterBackup => "TriAevum backup",
        SaveBackupTitle => "Save the backup",
        OpenBackupTitle => "Choose a backup to restore",
        RestoreHeading { created, platform } => return format!("Backup of {created}, made on {platform}"),
        RestoreOtherPlatform { platform } => {
            return format!(
                "This backup was made on {platform}. Saves, save states, the HUD layout, the game language and \
                 environments are restored; display, controls and window settings stay as they are here."
            )
        }
        RestoreDifferentRevision => {
            "This backup was made with another revision of the game than the one installed here."
        }
        RestoreUnavailable { path } => {
            return format!(
                "A setting in this backup names a folder that does not exist here; it stays as it is: {path}"
            )
        }
        RestoreKeptExisting { path } => {
            return format!("A file of this backup is not restored, the one here stays: {path}")
        }
        RestoreGroupLine { label, replaced, added, removed } => {
            return format!("{label}: {replaced} replaced, {added} added, {removed} removed")
        }
        RestoreKeptSettings { files } => return format!("These settings stay as they are here: {files}"),
        RestoreSkipped { labels } => return format!("Not restored here: {labels}"),
        RestoreSnapshotNote { path } => {
            return format!(
                "Before anything changes, Forge copies every file it replaces or removes into a restore folder in \
                 {path}. If the restore is interrupted, Forge puts your previous files back before you can play."
            )
        }
        RestoreDone { path } => return format!("Restored. The files it replaced are kept in {path}"),
        ClearCacheBody { size } => {
            return format!(
                "This deletes the renderer cache ({size}). The game builds it again; the next start can take longer."
            )
        }
        ClearCacheEmpty => "There is no renderer cache to clear.",
        ClearCacheDone => "The renderer cache was cleared.",
        RemoveGameBody { size } => {
            return format!(
                "This deletes the game data Forge generated from your ROM ({size}). Your saves and settings stay. \
                 You will need your ROM to install again."
            )
        }
        RemoveGameKeep => {
            "Your mods and downloads stay too. Keeping them is recommended: a new installation then does not \
             download them again."
        }
        RemoveGameDone => "The prepared game was removed. Choose your ROM on Home to install it again.",
        RebuildBody { size } => {
            return format!(
                "Forge prepares the game again from the ROM data it kept ({size}) and installs it. Your saves and \
                 settings stay. This can take several minutes."
            )
        }
        RebuildNoSource => "The ROM data Forge kept is missing. Choose your ROM on Home to prepare the game again.",
        RebuildDone => "The prepared game was rebuilt.",
        UninstallWindows { path } => {
            return format!(
                "Back up your files first, then delete this folder: {path}. Nothing else was installed on this \
                 computer."
            )
        }
        UninstallPortable { path } => return format!("Back up your files first, then delete this folder: {path}"),
        UninstallAppImage { appimage, path } => {
            return format!(
                "Back up your files first. Then delete the AppImage ({appimage}) and, if you do not want your saves \
                 any more, this folder: {path}"
            )
        }
        UninstallFlatpak { id, path } => {
            return format!(
                "Back up your files first. Then run: flatpak uninstall {id}. Your files stay in {path}; delete that \
                 folder yourself if you want."
            )
        }
        UninstallMacApp { path } => {
            return format!(
                "Back up your files first. Then move TriAevum.app to the Bin and, if you do not want your saves any \n                 more, delete this folder: {path}"
            )
        }
        UninstallAndroid { path } => {
            return format!(
                "Export your files first. Uninstalling the app removes its app-specific storage, including {path}."
            )
        }
        UninstallExternal => "These folders are yours, outside the installation; Forge never deletes them:",
        UninstallKeep => {
            "If you may install again, keep your mods and downloads: Forge then does not download them again."
        }
        PlatformName { code } => {
            return match code {
                "windows_portable" => "Windows",
                "linux_portable" => "Linux",
                "appimage" => "Linux (AppImage)",
                "flatpak" => "Linux (Flatpak)",
                "android" => "Android",
                other => other,
            }
            .to_string()
        }
        DataGameRunning => "TriAevum is running. Close the game to back up, restore or clean up.",
        DataForgeRunning => "Another Forge is working on this installation. Wait until it finishes.",
        DataRecoveryPending => {
            "A restore was interrupted. Finish the recovery to put your previous files back; Play stays blocked \
             until then."
        }
        RecoveryDone => "The interrupted restore was undone; your previous files are back.",
        DataLoading => "Reading where your files are...",
        DataBackingUp => "Backing up your files...",
        DataCheckingBackup => "Checking the backup...",
        DataRestoring => "Restoring your files...",
        DataClearingCache => "Clearing the renderer cache...",
        DataRemovingGame => "Removing the prepared game...",
        DataRebuilding => "Rebuilding the prepared game...",
        DataRecovering => "Undoing the interrupted restore...",
        DiagnosticsBody => {
            "What Forge knows about this installation. Copy it into a bug report; user names in paths are removed."
        }
        StateLabel { state } => return format!("Installation state: {state}"),
        Copied => "Copied to the clipboard.",
        LastGameRun { started, seconds, meaning } => {
            return format!("Last game run: started {started}, ran for {seconds} s, {meaning}.")
        }
        GameRunning => "The game is running.",
        NoGameRun => "No game run recorded by this Forge yet.",
        CollectingDiagnostics => "Collecting the diagnostics...",
        DiagnosticsFailed { error } => return format!("The diagnostics could not be collected: {error}"),
        GameLogFolder => "Game log",
        BundleContents => {
            "Save support bundle writes a zip with these files, with user names and installation folders removed. \
             It never contains saves, save states, game data or crash dump files."
        }
        BundleOmitted { name, reason } => return format!("Left out: {name} ({reason})"),
        ButtonSaveBundle => "Save support bundle...",
        SaveBundleTitle => "Save the support bundle",
        FilterZip => "Zip archive",
        BundleSaved { path } => return format!("Support bundle saved: {path}"),
        BundleFailed { error } => return format!("The support bundle could not be saved: {error}"),
        SteamDeckHeading => "Steam Deck and controllers",
        SteamDeckBody => {
            "Forge works without a mouse: Tab and Shift+Tab move between controls, Enter activates the highlighted \
             one and Esc closes a dialog. On Steam Deck, prepare the game in Desktop Mode, then add the AppImage to \
             Steam as a non-Steam game (without Proton). In its controller settings choose the Steam Input template \
             \"Gamepad with Mouse Trackpad\": the sticks and buttons stay a gamepad for the game, and the right \
             trackpad moves the pointer and clicks, so Forge can also be used in Gaming Mode."
        }
        Version { version } => return format!("Version {version}"),
        AboutBody => {
            return format!(
                "TriAevum Forge prepares TriAevum \u{2014} {} from your own decrypted ROM. It is free \
             software under the GNU General Public License, version 3 or later, and ships no game data.",
                super::TITLE_NAME
            )
        }
        SupportedRevisions => "Supported game revisions",
        NoSupportedRevisions => "This package lists no supported game revisions.",
        ProjectPage => "Project page",
        UserGuide => "User guide",
        ForgeLanguage => "Forge language",
        ForgeLanguageNote => {
            "The languages of your game. Texts not translated yet are shown in English. Your choice is remembered."
        }
        StateDownloaded { version } => return format!("Downloaded: version {version}"),
        StateNotDownloaded => "Not downloaded",
        StateOn => "Your choice: on",
        StateOff => "Your choice: off",
        StateApplied => "Applied: the installed game uses it",
        StateNotApplied => "Applied: no",
        StateApplyPending => "Not applied yet: press Apply changes",
        StateAppliedLater => "Applied when you prepare the game",
        HdPreferenceNotSaved { error } => return format!("The HD interface choice could not be saved: {error}"),
        TexturesHeading => "Your texture pack",
        TexturesDescription => "Your own Azahar or Citra texture pack (tex1_... .png files). Forge copies it into the folder below and checks it; the game draws it above the HD interface pack and its own textures. Import a folder or a .zip, with or without the game's title folder.",
        TexturesImported { files } => return format!("Imported: {files} files"),
        TexturesNone => "Imported: no texture pack yet",
        TexturesLoadCheckbox => "Load custom textures",
        TexturesApplied => "Applied: the game loads it when it starts",
        TexturesShadowedSettings => "Not loaded: F1 > Materials > Texture packs names another folder (below).",
        TexturesShadowedEnvironment => "Not loaded: the OOT3D_AZAHAR_LOAD_DIRECTORY environment variable names another folder (below).",
        TexturesShadowedAzahar => "Not loaded: the game still loads the pack in Azahar's folder (below). Import it to use your installation's folder.",
        TexturesOlderProfile => "Not loaded yet: press Apply changes so the game uses this folder.",
        TexturesLegacy => "A texture pack is in Azahar's own folder. Import it to keep it with this installation; the original stays where it is.",
        ButtonUseThisFolder => "Use the installation's folder",
        ButtonImportTextureFolder => "Import folder...",
        ButtonImportTextureZip => "Import .zip...",
        ButtonImportLegacy => "Import it",
        TexturesImporting => "Checking and copying your texture pack...",
        TexturesChoosing => "Choose your texture pack in the dialog...",
        TexturesImportDone { textures, files } => return format!("Texture pack imported: {textures} textures in {files} files. Load custom textures is on."),
        TexturesImportOtherTitle { title } => return format!("It was made for {title}; textures are matched by their content, so it works here too."),
        TexturesImportFailed { error } => return format!("The texture pack was not imported: {error}. Your previous pack is unchanged."),
        TexturesSettingsFailed { error } => return format!("The texture setting could not be changed: {error}"),
        TexturesFlatQuestion => "This pack has no folder named after a game. Place it under the installed game anyway?",
        ButtonPlaceIt => "Place it",
        PickTexturesFolder => "Select your texture pack folder",
        PickTexturesZip => "Select your texture pack (.zip)",
        CustomRomfsHeading => "Custom RomFS folder (unsupported)",
        CustomRomfsWarning => "Unsupported: may break the game; not covered by support.",
        CustomRomfsDescription => "For your own experiments: files in this folder replace the game's files with the same RomFS path, for example actor/zelda_link_boy_new.zar. Only files the game has are replaced; at most 4096 files of up to 64 MB each, 1 GB in total. An optional triaevum-custom-romfs.json can name the set. Forge never packages or shares these files.",
        CustomRomfsFiles { files } => return format!("In the folder: {files} files"),
        CustomRomfsNamed { name, files } => return format!("In the folder: {name}, {files} files"),
        CustomRomfsEmpty => "In the folder: nothing yet",
        CustomRomfsRejected { reason } => return format!("Not usable: {reason}"),
        CustomRomfsFlagged { reason } => return format!("Note: {reason}"),
        CustomRomfsCheckbox => "Use the custom RomFS folder",
        CustomRomfsConfirm => "Turn on the custom RomFS folder? Unsupported: may break the game; not covered by support. You can turn it off and apply changes again at any time.",
        ButtonTurnOn => "Turn it on",
        CustomRomfsApplied { files } => return format!("Applied: {files} files replace the game's"),
        ApplyHeading => "Apply changes",
        ApplyBody => "Forge applies your choices from the game data kept from your ROM, without asking for the ROM again. Only if that data is missing or damaged does it ask for your ROM.",
        ApplyPending => "Some choices are not applied yet.",
        ApplyCurrent => "The installed game uses your current choices.",
        ApplyNotInstalled => "Your choices are applied when you prepare the game on Home.",
        ButtonApplyChanges => "Apply changes",
        HudLayoutPolicyKeep => "HUD layout: yours is kept.",
        HudLayoutPolicyAsk => "HUD layout: yours differs from the standard profile; Apply changes asks first (Keep mine is the default).",
        ApplyingEnhancements => "Applying your enhancement choices...",
        ApplyChanged { list } => return format!("Apply changed: {list}"),
        EnhancementsLoading => "Reading the enhancement states...",
        EnhancementsGameRunning => "The game is running: close it to apply changes or change the texture setting.",
        EnhancementsStatusFailed { error } => return format!("Cannot read the enhancement states: {error}"),
        SettingNotSaved { error } => return format!("The setting could not be saved: {error}"),
        SavingSetting => "Saving the setting...",
        DataInactive => "Not in use",
        DataOldProfileNote => "Some files are still where an older version kept them. Run Update or Repair on Home (or Apply changes on Enhancements) to use the new folders; files are never moved without it.",
        EnhancementsApplied { kept } => {
            let kept = if kept { "Your saves and settings were kept." } else { "Your saves were kept." };
            return format!("Enhancements applied. {kept}");
        }
        ForgeLanguageNotSaved { error } => return format!("The Forge language could not be saved: {error}"),
        Size { value, power } => {
            let unit = match power {
                0 => "B",
                1 => "KB",
                2 => "MB",
                3 => "GB",
                _ => "TB",
            };
            return format!("{value} {unit}");
        }
        GuiFailed { error } => {
            return format!(
                "TriAevum Forge GUI failed: {error}\n\nThe command line tool triaevum-forge can still prepare titles."
            )
        }
    };
    fixed.to_string()
}
