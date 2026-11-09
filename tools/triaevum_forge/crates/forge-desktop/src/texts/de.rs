// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! German (de): every message of `en.rs`, in the same order. Button and
//! page names quoted in sentences are this table's labels.

use super::Msg;

pub fn text(msg: &Msg) -> Option<String> {
    use Msg::*;
    #[allow(unreachable_patterns)]
    let fixed = match *msg {
        WindowTitle => return Some(format!("TriAevum Forge \u{2014} {}", super::TITLE_NAME)),
        DialogTitle => "TriAevum Forge",
        NavHome => "Start",
        NavEnhancements => "Verbesserungen",
        NavData => "Daten & Sicherungen",
        NavDiagnostics => "Diagnose",
        NavAbout => "Info",
        Checking => "Die lokale Installation wird geprüft...",
        PickRom => "Wähle dein entschlüsseltes ROM (.3ds oder .cci)",
        RomLabel => "Entschlüsseltes ROM",
        Browse => "Durchsuchen...",
        UseExtracted => "Extrahierte Daten verwenden...",
        WhatForgeNeeds => "Was Forge braucht",
        Subtitle => {
            "Wähle das Abbild deiner eigenen entschlüsselten Nintendo 3DS-Spielkarte. Forge akzeptiert .3ds- und \
             .cci-ROMs, extrahiert die benötigten nativen Daten und verarbeitet keine Verschlüsselungsschlüssel. Du \
             kannst auch einen Ordner mit dekomprimierten code.bin, exheader.bin und romfs.bin auswählen."
        }
        PrivateData { path } => return Some(format!("Erzeugte private Daten: {path}")),
        PreparingHeading => "Das Spiel wird vorbereitet",
        PreparingElapsed { minutes, seconds } => return Some(format!("Verstrichen: {minutes}:{seconds:02}")),
        ProgressCount { done, total } => return Some(format!("{done} von {total}")),
        PreparingElsewhere => "Eine andere Forge-Instanz bereitet diese Installation vor",
        PreparingElsewhereBody => "Warte, bis sie fertig ist, und prüfe die Installation dann erneut.",
        ReadyHeading { revision, language } => return Some(format!("Bereit zum Spielen: {revision}, {language}")),
        ReadyHeadingNoLanguage { revision } => return Some(format!("Bereit zum Spielen: {revision}")),
        ReadyBody => "Deine Spielstände und Einstellungen bleiben in deinem Datenordner.",
        PrepareAgain => "Erneut aus einem ROM vorbereiten",
        UpdateHeading { installed, present } => {
            return Some(format!("Ein neues TriAevum-Paket ist da ({installed} -> {present})."))
        }
        UpdateHeadingVersion { present } => {
            return Some(format!("Ein neues TriAevum-Paket ist da (Version {present})."))
        }
        UpdateHeadingPlain => "Ein neues TriAevum-Paket ist da.",
        UpdateBody => {
            "„Aktualisieren“ installiert das neue Paket für dein vorbereitetes Spiel. Dein ROM wird nicht benötigt; \
             deine Spielstände und Einstellungen bleiben erhalten."
        }
        RepairBody => {
            "„Reparieren“ baut Fehlendes aus den Spieldaten neu auf, die aus deinem ROM aufbewahrt wurden. Deine \
             Spielstände und Einstellungen bleiben erhalten."
        }
        NeedsGameInputHeading => "Zum Reparieren dieser Installation werden deine Spieldateien erneut benötigt",
        NeedsGameInputBody => "Du kannst weiterhin spielen. Wähle dein ROM erneut aus, dann „Reparieren“.",
        Updating => "Die Aktualisierung wird installiert...",
        Repairing => "Die Installation wird repariert...",
        Updated { version, kept } => {
            let version = if version.is_empty() { String::new() } else { format!(" auf {version}") };
            let kept = if kept {
                "Deine Spielstände und Einstellungen wurden beibehalten."
            } else {
                "Deine Spielstände wurden beibehalten."
            };
            return Some(format!("Aktualisiert{version}. {kept}"));
        }
        Repaired { kept } => {
            let kept = if kept {
                "Deine Spielstände und Einstellungen wurden beibehalten."
            } else {
                "Deine Spielstände wurden beibehalten."
            };
            return Some(format!("Repariert. {kept}"));
        }
        AlreadyCurrent => "Diese Installation verwendet bereits dieses Paket.",
        UpdateChanged { list } => return Some(format!("Durch die Aktualisierung geändert: {list}")),
        RepairChanged { list } => return Some(format!("Durch die Reparatur geändert: {list}")),
        ChangeCreated { file } => return Some(format!("{file} wurde mit Standardwerten erstellt")),
        ChangeStandardLayout => "das HUD-Layout wurde auf das Standardprofil zurückgesetzt",
        ChangeLanguage => "die von dir gewählte Spielsprache wurde eingestellt",
        ChangeRemoved { file } => return Some(format!("{file} wurde entfernt")),
        ChangeOther { file } => return Some(format!("{file} wurde geändert")),
        HudLayoutQuestion => "HUD-Layout auf das Standardprofil zurücksetzen?",
        HudLayoutBody => {
            "Dein HUD-Layout weicht vom Standardprofil ab. „Meins behalten“ lässt es, wie es ist; „Standard \
             verwenden“ ändert diese Einstellungen:"
        }
        HudLayoutField { name, yours, standard } => return Some(format!("{name}: deins {yours}, Standard {standard}")),
        ButtonKeepMine => "Meins behalten",
        ButtonUseStandard => "Standard verwenden",
        DowngradeHeading => "Älteres Paket installieren?",
        DowngradeBody { installed, present } => {
            return Some(format!(
                "Dieses Paket ({present}) ist älter als das installierte ({installed}). Die ältere Version trotzdem \
                 installieren?"
            ))
        }
        ButtonInstallOlder => "Ältere Version installieren",
        ButtonUpdateFromZip => "Mit heruntergeladener ZIP-Datei aktualisieren...",
        PickUpdateZipTitle => "Heruntergeladene TriAevum-ZIP-Datei auswählen",
        CheckingUpdatePackage => "Aktualisierungspaket wird geprüft...",
        ZipPreviewHeading { version } => return Some(format!("Auf {version} aktualisieren?")),
        ZipPreviewBody { installed, version } => return Some(format!("Die TriAevum-Programmdateien dieser Installation werden ersetzt ({installed} wird zu {version}). Spielstände, Einstellungen, Speicherstände, Mods und Downloads bleiben erhalten. Forge schließt sich während der Aktualisierung und öffnet sich danach wieder.")),
        ZipSigned { key } => return Some(format!("Vom TriAevum-Herausgeber signiert (Schlüssel {key}).")),
        ZipUnsigned => "Achtung: Dieses Paket ist nicht vom TriAevum-Herausgeber signiert. Es wird nur angenommen, weil die Entwickler-Ausnahme aktiv ist.",
        ZipLocallyModified { list } => return Some(format!("Diese Paketdateien wurden auf diesem Computer geändert und werden ersetzt: {list}")),
        ButtonUpdateNow => "Aktualisieren",
        UpdateHandingOff => "Aktualisierung läuft: Forge schließt sich jetzt und öffnet sich wieder, wenn die Aktualisierung fertig ist.",
        UpdateRolledBack { error } => return Some(format!("Die Aktualisierung wurde nicht abgeschlossen, daher wurde die vorherige Version wiederhergestellt. Spielstände und Einstellungen sind erhalten geblieben.\n\n{error}")),
        UpdateRefused { error } => return Some(format!("Die Aktualisierung hat nicht begonnen; nichts wurde geändert.\n\n{error}")),
        UpdateRecoveryNeeded { error } => return Some(format!("Die unterbrochene Aktualisierung konnte nicht automatisch rückgängig gemacht werden. Wähle „Reparieren“, um es erneut zu versuchen; „Spielen“ bleibt bis dahin gesperrt.\n\n{error}")),
        UpdateRecovered => "Die unterbrochene Aktualisierung wurde rückgängig gemacht; die vorherige Version ist wieder da.",
        UpdateCompleted => "Die unterbrochene Aktualisierung wurde abgeschlossen.",
        ShowDetails => "Details anzeigen",
        HideDetails => "Details ausblenden",
        Dismiss => "Schließen",
        HomeHdDownload { megabytes } => {
            return Some(format!(
                "HD-Oberflächentexturen ({megabytes} MB) werden mit der Vorbereitung heruntergeladen. Du kannst das \
                 unter „Verbesserungen“ ändern."
            ))
        }
        HomeHdReady => "HD-Oberflächentexturen sind gespeichert und werden mit der Vorbereitung angewendet.",
        HomeHdOff => "HD-Oberflächentexturen sind aus: Das Spiel verwendet seine nativen Texturen.",
        ButtonPrepare => "Vorbereiten",
        ButtonPlay => "Spielen",
        ButtonUpdate => "Aktualisieren",
        ButtonRepair => "Reparieren",
        ButtonCancel => "Abbrechen",
        ButtonCheckInstallation => "Installation prüfen",
        ButtonChooseRom => "ROM wählen...",
        ButtonCopyDiagnostics => "Diagnose kopieren",
        ButtonViewDiagnostics => "Diagnose anzeigen",
        ButtonSupportedRevisions => "Unterstützte Revisionen",
        ButtonDataPage => "Daten & Sicherungen",
        ButtonRetry => "Erneut versuchen",
        ButtonContinue => "Weiter",
        ButtonOk => "OK",
        ButtonYes => "Ja",
        ButtonNo => "Nein",
        ButtonOpenFolder => "Ordner öffnen",
        StatusSelectRom => "Wähle dein ROM, um zu beginnen.",
        StatusChooseLanguage => "ROM erkannt: Wähle die Spielsprache, dann „Vorbereiten“.",
        StatusReady => "Bereit zum Spielen.",
        StatusActionNeeded => "Handlung nötig: siehe „Start“.",
        StatusCancelled => "Vorbereitung abgebrochen. Was vorher installiert war, bleibt nutzbar.",
        StatusPreparingElsewhere => "Eine andere Forge-Instanz bereitet diese Installation vor.",
        LanguageSaved => "Spielsprache gespeichert. Gilt ab dem nächsten vollständigen Spielstart.",
        NoLanguages => "Wähle dein ROM: Die Spielsprachen, die es bietet, erscheinen hier.",
        GameLanguage => "Spielsprache",
        Recognizing => "Das ROM wird erkannt...",
        RomNotRecognized => "ROM nicht erkannt",
        ChooseLanguage => "Wähle die Spielsprache:",
        LanguageForInstall => {
            "„Vorbereiten“ richtet das Spiel in dieser Sprache ein. Du kannst sie später hier oder im Spiel ändern."
        }
        LanguageInstalled => "Änderungen gelten ab dem nächsten vollständigen Spielstart.",
        RomRecognized { label } => return Some(format!("ROM erkannt: {label}")),
        LanguagesUnreadable { error } => {
            return Some(format!("Die Spielsprachen werden während der Installation gelesen. {error}"))
        }
        LanguageChosen { label } => return Some(format!("Spielsprache für diese Installation: {label}.")),
        LanguageSettingsUnreadable { error } => {
            return Some(format!("Die Einstellungen der Spielsprache können nicht gelesen werden: {error}"))
        }
        Starting => "Die Vorbereitung des Spiels wird gestartet...",
        DownloadingHdFirst => "Die HD-Texturen werden vor der Vorbereitung des Spiels heruntergeladen...",
        DownloadingModFirst => "Die aktivierte Mod wird vor der Vorbereitung des Spiels heruntergeladen...",
        ReadyDialog => return Some(format!("Das Spiel ist bereit. Du kannst TriAevum \u{2014} {} jetzt starten.", super::TITLE_NAME)),
        CloseWhileBusy => "Die Vorbereitung des Spiels läuft noch. Forge schließen?",
        ConfirmCancel => "Vorbereitung beenden? Was vorher installiert war, bleibt nutzbar.",
        EnhancementsBody => "Wähle die gewünschten Verbesserungen und drücke dann „Änderungen anwenden“. Ohne sie verwendet das Spiel seine nativen Inhalte.",
        HdHeading => "HD-Oberflächentexturen (empfohlen)",
        HdDescription { title, version, author, credits, license } => {
            return Some(format!(
                "{title} {version} von {author} (Credits: {credits}): HD-Texturen für das HUD, die Menüs und \
                 TopScreen, in der Spielsprache, die du hier oder im Spiel wählst. Lizenz: {license}; TriAevum liefert \
                 sie nicht mit. Forge lädt das Originalarchiv des Autors von GameBanana herunter, prüft es und \
                 bewahrt es in deinem privaten Datenordner auf. Falls der Download nicht funktioniert, lade die \
                 ZIP-Datei von der Mod-Seite herunter und wähle sie hier aus. Danke an RLG (M-1) für SingleScreen, \
                 die Mod, auf der TopScreen von TriAevum basiert; ihre HD-Schrift ist in TriAevum enthalten."
            ))
        }
        HdDownload => "HD-Texturen herunterladen",
        SelectZip => ".zip auswählen...",
        SelectFolder => "Extrahierten Ordner wählen...",
        ModPage => "Mod-Seite",
        License => "Lizenz",
        HdAutoDownload => "HD-Oberflächenpaket verwenden (wird mit „Vorbereiten“ oder „Änderungen anwenden“ heruntergeladen, wenn es noch nicht gespeichert ist)",
        NotAvailable => "Für die unterstützten Spielrevisionen dieses Pakets nicht verfügbar.",
        CheckingStored => "Die gespeicherten Dateien werden geprüft...",
        HdCheckFailed { error } => return Some(format!("Die HD-Texturen können nicht geprüft werden: {error}")),
        HdReady => "HD-Texturen bereit. Sie werden mit „Änderungen anwenden“ und bei der nächsten Vorbereitung angewendet.",
        HdArchive => "Archiv der HD-Texturen gespeichert. Es wird mit „Änderungen anwenden“ und bei der nächsten Vorbereitung vorbereitet und angewendet.",
        HdMissingAuto { megabytes } => return Some(format!("Noch nicht gespeichert: Die HD-Texturen ({megabytes} MB) werden heruntergeladen, wenn du das Spiel vorbereitest oder Änderungen anwendest. Ohne sie verwendet das Spiel die nativen Texturen.")),
        HdMissing => "Nicht installiert: Das Spiel verwendet die nativen Texturen.",
        ModHeading => "Optionale Mods",
        ModDescription { title, version, author, testing, tools, license } => {
            return Some(format!(
                "{title} {version} von {author}: detailliertere Modelle für Link und seine Gegenstände. Tests: \
                 {testing}. Werkzeuge: {tools}. Lizenz: {license}; TriAevum liefert sie nicht mit. Forge lädt das \
                 Originalarchiv des Autors von GameBanana herunter, prüft es und wendet es nur auf deine lokalen \
                 Spieldaten an. Falls der Download nicht funktioniert, lade das Archiv von der Mod-Seite herunter und \
                 wähle es hier aus."
            ))
        }
        ModCheckbox { title } => return Some(format!("{title} verwenden")),
        ModDownload => "Mod herunterladen",
        SelectRar => ".rar auswählen...",
        ModSettingsUnreadable { error } => {
            return Some(format!("Die Mod-Einstellungen können nicht gelesen werden: {error}"))
        }
        ModCheckFailed { error } => return Some(format!("Die Mod-Dateien können nicht geprüft werden: {error}")),
        ModMissingEnabled => "Nicht heruntergeladen. Er wird heruntergeladen, wenn du Änderungen anwendest oder das Spiel vorbereitest.",
        ModOff => "Aus: Das Spiel verwendet seine nativen Modelle.",
        ModOn => "An: wird mit „Änderungen anwenden“ und bei der nächsten Vorbereitung angewendet.",
        ModOffKept => "Aus: Das Spiel verwendet seine nativen Modelle. Das heruntergeladene Archiv bleibt erhalten.",
        ApplyModChange => " Drücke „Änderungen anwenden“, um sie anzuwenden.",
        HdDownloadFailed { error } => return Some(format!("Download der HD-Texturen fehlgeschlagen: {error} Das Spiel wird mit den nativen Texturen vorbereitet. Lade die ZIP-Datei von der Mod-Seite herunter, wähle sie mit „.zip auswählen...“ aus und wende dann die Änderungen an.")),
        ModDownloadFailed { error } => {
            return Some(format!(
                "Download der Mod fehlgeschlagen: {error} Das Spiel wird mit den nativen Modellen vorbereitet. Lade \
                 das Archiv von der Mod-Seite herunter und wähle es mit „.rar auswählen...“ aus."
            ))
        }
        ManualHdFailure { error, archive, page } => {
            return Some(format!(
                "{error}\n\nLade {archive} von der Mod-Seite ({page}) herunter und wähle es mit „.zip \
                 auswählen...“ aus."
            ))
        }
        ManualModFailure { error, archive, page } => {
            return Some(format!(
                "{error}\n\nLade {archive} von der Mod-Seite ({page}) herunter und wähle es mit „.rar \
                 auswählen...“ aus."
            ))
        }
        VerifyingArchive => "Das ausgewählte Archiv wird geprüft...",
        VerifyingTextures => "Die extrahierten Texturen werden geprüft...",
        VerifyingFiles => "Die extrahierten Dateien werden geprüft...",
        DownloadingHd => "Die HD-Texturen werden heruntergeladen...",
        DownloadingMod { title } => return Some(format!("{title} wird heruntergeladen...")),
        HdReadyReinstall => "HD-Texturen bereit. Drücke „Änderungen anwenden“, um sie zu verwenden.",
        ModReadyReinstall { title } => return Some(format!("{title} ist bereit. Drücke „Änderungen anwenden“, um es zu verwenden.")),
        HdArchiveApplied => "HD-Texturarchiv bereit. Es wird von dieser Installation angewendet.",
        DownloadsFolderNote => {
            "Forge bewahrt in diesem Ordner die heruntergeladenen Archive auf und eine geprüfte Kopie jedes \
             Archivs, das du auswählst. Ohne Netzwerk lade das Archiv auf einem beliebigen Gerät von der Mod-Seite \
             herunter und wähle es mit den Schaltflächen oben aus."
        }
        ModSettingSaveFailed { error } => {
            return Some(format!("Die Mod-Einstellung kann nicht gespeichert werden: {error}"))
        }
        RevisionsUnreadable { error } => {
            return Some(format!("Die unterstützten Spielrevisionen können nicht gelesen werden: {error}"))
        }
        PickRomTitle => "Entschlüsseltes .3ds- oder .cci-ROM auswählen",
        FilterRom => "Nintendo 3DS-ROM",
        Filter3ds => "3DS-Abbild",
        FilterCci => "CCI-Abbild",
        PickExtractedTitle => "Extrahierte Spieldaten auswählen",
        PickArchive { name } => return Some(format!("{name} auswählen")),
        PickArchiveFallbackHd => "ZIP-Datei des Texturpakets auswählen",
        PickArchiveFallbackMod => "Archiv auswählen",
        FilterTexturePack => "Texturpaket-Archiv",
        PickFolder { title } => return Some(format!("Extrahierten Ordner von {title} auswählen")),
        FilterModArchive => "Mod-Archiv",
        DataBody => {
            "Wo TriAevum deine Dateien aufbewahrt, was eine Sicherung enthält und was du gefahrlos aufräumen kannst. \
             Behalte den Datenordner, wenn du TriAevum verschiebst oder aktualisierst."
        }
        DataFolder => "Datenordner",
        ForgeLog => "Forge-Protokoll",
        DataYourFiles => "Deine Dateien",
        DataEntryLabel { name } => {
            return Some(
                match name {
                    "save_data" => "Spielstände",
                    "save_states" => "Speicherstände",
                    "save_states_default" => "Im früheren Ordner verbliebene Speicherstände",
                    "config" => "Einstellungen",
                    "runtime_config" => "Datei der Spieleinstellungen",
                    "topscreen_config" => "HUD-Layout",
                    "controls_config" => "Steuerung",
                    "game_language" => "Spielsprache",
                    "romfs_mods" => "Mod-Auswahl",
                    "savestates_index" => "Liste der Speicherstände",
                    "environments" => "Umgebungen",
                    "imgui_layout" => "Layout des F1-Fensters",
                    "imgui_layout_legacy" => "Layout des F1-Fensters einer älteren Version",
                    "android_host_config" => "Android-Anzeigeeinstellungen",
                    "touch_preferences" => "Touch-Steuerung",
                    "texture_load" => "Dein Texturpaket",
                    "texture_dump" => "Ausgelesene Texturen",
                    "mods" => "Mods",
                    "downloads" => "Downloads",
                    "sources" => "Aus deinem ROM aufbewahrte Spieldaten",
                    "titles" => "Vorbereitetes Spiel",
                    "cache" => "Vorbereitete Caches",
                    "shader_seeds" => "Daten der Grafikvorbereitung",
                    "interior_sun" => "Daten der Innenraumbeleuchtung",
                    "renderer_cache" => "Renderer-Cache",
                    "translator_cache" => "Übersetzer-Cache",
                    "game_logs" => "Spielprotokolle",
                    "forge_logs" => "Forge-Protokolle",
                    "crash_dumps" => "Absturzberichte",
                    "android_diagnostics" => "Startberichte",
                    "texture_load_legacy" => "Texturpaket im Azahar-Ordner",
                    "save_states_legacy" => "Speicherstände im früheren Ordner",
                    "game_logs_legacy" => "Spielprotokolle im früheren Ordner",
                    "crash_dumps_legacy" => "Absturzberichte im früheren Ordner",
                    "active_title" => "Eintrag des installierten Spiels",
                    other => other,
                }
                .to_string(),
            )
        }
        DataCategoryHelp { category } => {
            return Some(
                match category {
                    "saves" => {
                        "Die eigenen Spielstanddateien des Spiels, wie auf der Spielkarte. Immer in deinen Sicherungen."
                    }
                    "save_states" => {
                        "Speicherstände (F5/F8 und das Speicherstand-Menü) mit ihren Bildern. Immer in deinen \
                         Sicherungen."
                    }
                    "settings" => {
                        "Deine Einstellungen: Grafik, HUD-Layout, Steuerung, Spielsprache und Umgebungen sowie das \
                         Layout des F1-Fensters. Immer in deinen Sicherungen."
                    }
                    "textures" => {
                        "Ein Texturpaket, das du selbst hinzugefügt hast, und aus dem Spiel ausgelesene Texturen. Dein \
                         Paket kann in eine Sicherung aufgenommen werden; ausgelesene Texturen werden nie gesichert."
                    }
                    "enhancements" => {
                        "Mods und heruntergeladene Archive. Sie können erneut heruntergeladen werden, daher enthält \
                         eine Sicherung sie nur, wenn du das auswählst."
                    }
                    "generated" => {
                        "Von Forge aus deinem ROM erstellt. Wird nie gesichert: Forge erstellt es erneut aus deinem \
                         ROM, und es darf nicht weitergegeben werden."
                    }
                    "logs" => {
                        "Protokolle und Absturzberichte zur Fehlersuche. Werden nie gesichert; „Diagnose“ verwendet \
                         sie."
                    }
                    _ => "Dateien, die TriAevum verwendet.",
                }
                .to_string(),
            )
        }
        DataNotCreated => "noch nicht erstellt",
        DataOriginDefault => "Standardort",
        DataOriginExplicit => "Von dir festgelegt",
        DataOriginLegacy => "Ältere Version",
        DataBackupInclude => "In jeder Sicherung",
        DataBackupOptional => "In einer Sicherung, wenn du es auswählst",
        DataBackupExclude => "Wird nicht gesichert",
        ButtonWhatIsThis => "Was ist das?",
        ButtonHideHelp => "Hilfe ausblenden",
        DataActionsHeading => "Sichern, wiederherstellen und aufräumen",
        ButtonBackUp => "Dateien sichern...",
        ButtonRestore => "Wiederherstellen...",
        ButtonClearCache => "Renderer-Cache leeren",
        ButtonRebuild => "Ressourcen neu erstellen",
        ButtonRemoveGame => "Vorbereitetes Spiel entfernen...",
        ButtonUninstallHelp => "Hilfe zur Deinstallation",
        ButtonChooseBackupFile => "Speicherort wählen...",
        ButtonConfirmRestore => "Diese Dateien wiederherstellen",
        ButtonConfirmClear => "Cache leeren",
        ButtonConfirmRebuild => "Jetzt neu erstellen",
        ButtonConfirmRemove => "Vorbereitetes Spiel entfernen",
        ButtonRecover => "Dateien zurückholen",
        BackupPanelBody { files, size } => {
            return Some(format!(
                "Eine Sicherung enthält deine Spielstände, Speicherstände und Einstellungen: {files} Dateien, {size}. \
                 Sie ist eine einzige .zip-Datei, die du überall aufbewahren kannst."
            ))
        }
        BackupOptionalHeading => "Zusätzlich aufnehmen (standardmäßig nicht ausgewählt):",
        BackupExcludedNote => {
            "Nie enthalten: die aus deinem ROM erstellten Spieldaten, Caches, Protokolle, Absturzberichte und \
             Installationseinträge. Forge erstellt sie erneut, oder sie werden nicht benötigt."
        }
        BackupDone { path, size } => return Some(format!("Sicherung gespeichert ({size}): {path}")),
        FilterBackup => "TriAevum-Sicherung",
        SaveBackupTitle => "Sicherung speichern",
        OpenBackupTitle => "Sicherung zum Wiederherstellen wählen",
        RestoreHeading { created, platform } => {
            return Some(format!("Sicherung vom {created}, erstellt auf {platform}"))
        }
        RestoreOtherPlatform { platform } => {
            return Some(format!(
                "Diese Sicherung wurde auf {platform} erstellt. Spielstände, Speicherstände, das HUD-Layout, die \
                 Spielsprache und die Umgebungen werden wiederhergestellt; Anzeige-, Steuerungs- und \
                 Fenstereinstellungen bleiben, wie sie hier sind."
            ))
        }
        RestoreDifferentRevision => {
            "Diese Sicherung wurde mit einer anderen Spielrevision erstellt als der hier installierten."
        }
        RestoreUnavailable { path } => {
            return Some(format!(
                "Eine Einstellung in dieser Sicherung nennt einen Ordner, den es hier nicht gibt; sie bleibt, wie sie \
                 ist: {path}"
            ))
        }
        RestoreKeptExisting { path } => {
            return Some(format!(
                "Eine Datei dieser Sicherung wird nicht wiederhergestellt, die hiesige bleibt: {path}"
            ))
        }
        RestoreGroupLine { label, replaced, added, removed } => {
            return Some(format!("{label}: {replaced} ersetzt, {added} hinzugefügt, {removed} entfernt"))
        }
        RestoreKeptSettings { files } => {
            return Some(format!("Diese Einstellungen bleiben, wie sie hier sind: {files}"))
        }
        RestoreSkipped { labels } => return Some(format!("Hier nicht wiederhergestellt: {labels}")),
        RestoreSnapshotNote { path } => {
            return Some(format!(
                "Bevor sich etwas ändert, kopiert Forge jede Datei, die ersetzt oder entfernt wird, in einen \
                 Wiederherstellungsordner in {path}. Wird die Wiederherstellung unterbrochen, legt Forge deine \
                 vorherigen Dateien zurück, bevor du spielen kannst."
            ))
        }
        RestoreDone { path } => {
            return Some(format!("Wiederhergestellt. Die ersetzten Dateien werden aufbewahrt in {path}"))
        }
        ClearCacheBody { size } => {
            return Some(format!(
                "Damit wird der Renderer-Cache ({size}) gelöscht. Das Spiel erstellt ihn neu; der nächste Start kann \
                 länger dauern."
            ))
        }
        ClearCacheEmpty => "Es gibt keinen Renderer-Cache zum Leeren.",
        ClearCacheDone => "Der Renderer-Cache wurde geleert.",
        RemoveGameBody { size } => {
            return Some(format!(
                "Damit werden die Spieldaten gelöscht, die Forge aus deinem ROM erzeugt hat ({size}). Deine \
                 Spielstände und Einstellungen bleiben. Für eine erneute Installation brauchst du dein ROM."
            ))
        }
        RemoveGameKeep => {
            "Deine Mods und Downloads bleiben ebenfalls. Wir empfehlen, sie zu behalten: Eine neue Installation lädt \
             sie dann nicht erneut herunter."
        }
        RemoveGameDone => {
            "Das vorbereitete Spiel wurde entfernt. Wähle dein ROM unter „Start“, um es erneut zu installieren."
        }
        RebuildBody { size } => {
            return Some(format!(
                "Forge bereitet das Spiel erneut aus den aufbewahrten ROM-Daten ({size}) vor und installiert es. \
                 Deine Spielstände und Einstellungen bleiben. Das kann mehrere Minuten dauern."
            ))
        }
        RebuildNoSource => {
            "Die von Forge aufbewahrten ROM-Daten fehlen. Wähle dein ROM unter „Start“, um das Spiel erneut \
             vorzubereiten."
        }
        RebuildDone => "Das vorbereitete Spiel wurde neu erstellt.",
        UninstallWindows { path } => {
            return Some(format!(
                "Sichere zuerst deine Dateien und lösche dann diesen Ordner: {path}. Sonst wurde nichts auf diesem \
                 Computer installiert."
            ))
        }
        UninstallPortable { path } => {
            return Some(format!("Sichere zuerst deine Dateien und lösche dann diesen Ordner: {path}"))
        }
        UninstallAppImage { appimage, path } => {
            return Some(format!(
                "Sichere zuerst deine Dateien. Lösche dann das AppImage ({appimage}) und, wenn du deine Spielstände \
                 nicht mehr brauchst, diesen Ordner: {path}"
            ))
        }
        UninstallFlatpak { id, path } => {
            return Some(format!(
                "Sichere zuerst deine Dateien. Führe dann aus: flatpak uninstall {id}. Deine Dateien bleiben in \
                 {path}; lösche diesen Ordner selbst, wenn du willst."
            ))
        }
        UninstallMacApp { path } => {
            return Some(format!(
                "Sichere zuerst deine Dateien. Lege dann TriAevum.app in den Papierkorb und lösche, wenn du deine \n                 Spielstände nicht mehr brauchst, diesen Ordner: {path}"
            ))
        }
        UninstallAndroid { path } => {
            return Some(format!(
                "Exportiere zuerst deine Dateien. Beim Deinstallieren der App wird ihr App-Speicher entfernt, \
                 einschließlich {path}."
            ))
        }
        UninstallExternal => "Diese Ordner gehören dir und liegen außerhalb der Installation; Forge löscht sie nie:",
        UninstallKeep => {
            "Wenn du eventuell erneut installierst, behalte deine Mods und Downloads: Forge lädt sie dann nicht \
             erneut herunter."
        }
        PlatformName { code } => {
            return Some(
                match code {
                    "windows_portable" => "Windows",
                    "linux_portable" => "Linux",
                    "appimage" => "Linux (AppImage)",
                    "flatpak" => "Linux (Flatpak)",
                    "android" => "Android",
                    other => other,
                }
                .to_string(),
            )
        }
        DataGameRunning => "TriAevum läuft. Schließe das Spiel, um zu sichern, wiederherzustellen oder aufzuräumen.",
        DataForgeRunning => "Eine andere Forge-Instanz arbeitet an dieser Installation. Warte, bis sie fertig ist.",
        DataRecoveryPending => {
            "Eine Wiederherstellung wurde unterbrochen. „Dateien zurückholen“ legt deine vorherigen Dateien zurück; \
             „Spielen“ bleibt bis dahin gesperrt."
        }
        RecoveryDone => {
            "Die unterbrochene Wiederherstellung wurde rückgängig gemacht; deine vorherigen Dateien sind zurück."
        }
        DataLoading => "Die Speicherorte deiner Dateien werden gelesen...",
        DataBackingUp => "Deine Dateien werden gesichert...",
        DataCheckingBackup => "Die Sicherung wird geprüft...",
        DataRestoring => "Deine Dateien werden wiederhergestellt...",
        DataClearingCache => "Der Renderer-Cache wird geleert...",
        DataRemovingGame => "Das vorbereitete Spiel wird entfernt...",
        DataRebuilding => "Das vorbereitete Spiel wird neu erstellt...",
        DataRecovering => "Die unterbrochene Wiederherstellung wird rückgängig gemacht...",
        DiagnosticsBody => {
            "Was Forge über diese Installation weiß. Kopiere es in einen Fehlerbericht; Benutzernamen in Pfaden werden \
             entfernt."
        }
        StateLabel { state } => return Some(format!("Installationszustand: {state}")),
        Copied => "In die Zwischenablage kopiert.",
        LastGameRun { started, seconds, meaning } => {
            return Some(format!("Letzter Spielstart: gestartet {started}, Laufzeit {seconds} s, {meaning}."))
        }
        GameRunning => "Das Spiel läuft.",
        NoGameRun => "Diese Forge-Instanz hat noch keinen Spielstart aufgezeichnet.",
        CollectingDiagnostics => "Die Diagnose wird gesammelt...",
        DiagnosticsFailed { error } => return Some(format!("Die Diagnose konnte nicht gesammelt werden: {error}")),
        GameLogFolder => "Spielprotokoll",
        BundleContents => {
            "„Support-Paket speichern...“ schreibt eine ZIP-Datei mit diesen Dateien, ohne Benutzernamen und \
             Installationsordner. Sie enthält nie Spielstände, Speicherstände, Spieldaten oder Absturzabbilder."
        }
        BundleOmitted { name, reason } => return Some(format!("Ausgelassen: {name} ({reason})")),
        ButtonSaveBundle => "Support-Paket speichern...",
        SaveBundleTitle => "Support-Paket speichern",
        FilterZip => "ZIP-Archiv",
        BundleSaved { path } => return Some(format!("Support-Paket gespeichert: {path}")),
        BundleFailed { error } => return Some(format!("Das Support-Paket konnte nicht gespeichert werden: {error}")),
        SteamDeckHeading => "Steam Deck und Controller",
        SteamDeckBody => {
            "Forge funktioniert ohne Maus: Tab und Umschalt+Tab wechseln zwischen den Bedienelementen, Eingabe \
             aktiviert das hervorgehobene und Esc schließt einen Dialog. Bereite das Spiel auf dem Steam Deck im \
             Desktop-Modus vor und füge das AppImage dann als Nicht-Steam-Spiel zu Steam hinzu (ohne Proton). \
             Wähle in seinen Controller-Einstellungen die Steam-Input-Vorlage „Gamepad with Mouse Trackpad“ \
             (Gamepad mit Trackpad als Maus): Sticks und Tasten bleiben für das Spiel ein Gamepad, und das rechte \
             Trackpad bewegt den Zeiger und klickt, sodass Forge auch im Spielmodus bedienbar ist."
        }
        Version { version } => return Some(format!("Version {version}")),
        AboutBody => {
            return Some(format!(
                "TriAevum Forge bereitet TriAevum \u{2014} {} aus deinem eigenen entschlüsselten ROM vor. Es \
             ist freie Software unter der GNU General Public License, Version 3 oder neuer, und enthält keine \
             Spieldaten.",
                super::TITLE_NAME
            ))
        }
        SupportedRevisions => "Unterstützte Spielrevisionen",
        NoSupportedRevisions => "Dieses Paket führt keine unterstützten Spielrevisionen auf.",
        ProjectPage => "Projektseite",
        UserGuide => "Benutzerhandbuch",
        ForgeLanguage => "Forge-Sprache",
        ForgeLanguageNote => {
            "Die Sprachen deines Spiels. Noch nicht übersetzte Texte werden auf Englisch angezeigt. Deine Auswahl \
             wird gespeichert."
        }
        StateDownloaded { version } => return Some(format!("Heruntergeladen: Version {version}")),
        StateNotDownloaded => "Nicht heruntergeladen",
        StateOn => "Deine Wahl: an",
        StateOff => "Deine Wahl: aus",
        StateApplied => "Angewendet: Das installierte Spiel verwendet es",
        StateNotApplied => "Angewendet: nein",
        StateApplyPending => "Noch nicht angewendet: Drücke „Änderungen anwenden“",
        StateAppliedLater => "Wird angewendet, wenn du das Spiel vorbereitest",
        HdPreferenceNotSaved { error } => return Some(format!("Die Wahl der HD-Oberfläche konnte nicht gespeichert werden: {error}")),
        TexturesHeading => "Dein Texturpaket",
        TexturesDescription => "Dein eigenes Azahar- oder Citra-Texturpaket (Dateien tex1_... .png). Forge kopiert es in den Ordner unten und prüft es; das Spiel zeichnet es über dem HD-Oberflächenpaket und seinen eigenen Texturen. Importiere einen Ordner oder eine .zip-Datei, mit oder ohne den Titelordner des Spiels.",
        TexturesImported { files } => return Some(format!("Importiert: {files} Dateien")),
        TexturesNone => "Importiert: noch kein Texturpaket",
        TexturesLoadCheckbox => "Eigene Texturen laden",
        TexturesApplied => "Angewendet: Das Spiel lädt es beim Start",
        TexturesShadowedSettings => "Nicht geladen: F1 > Materials > Texture packs nennt einen anderen Ordner (unten).",
        TexturesShadowedEnvironment => "Nicht geladen: Die Umgebungsvariable OOT3D_AZAHAR_LOAD_DIRECTORY nennt einen anderen Ordner (unten).",
        TexturesShadowedAzahar => "Nicht geladen: Das Spiel lädt noch das Paket im Azahar-Ordner (unten). Importiere es, um den Ordner deiner Installation zu verwenden.",
        TexturesOlderProfile => "Noch nicht geladen: Drücke „Änderungen anwenden“, damit das Spiel diesen Ordner verwendet.",
        TexturesLegacy => "Im eigenen Ordner von Azahar liegt ein Texturpaket. Importiere es, um es bei dieser Installation zu behalten; das Original bleibt, wo es ist.",
        ButtonUseThisFolder => "Ordner der Installation verwenden",
        ButtonImportTextureFolder => "Ordner importieren...",
        ButtonImportTextureZip => ".zip importieren...",
        ButtonImportLegacy => "Importieren",
        TexturesImporting => "Dein Texturpaket wird geprüft und kopiert...",
        TexturesChoosing => "Wähle dein Texturpaket im Dialog aus...",
        TexturesImportDone { textures, files } => return Some(format!("Texturpaket importiert: {textures} Texturen in {files} Dateien. „Eigene Texturen laden“ ist an.")),
        TexturesImportOtherTitle { title } => return Some(format!("Es wurde für {title} erstellt; Texturen werden an ihrem Inhalt erkannt, daher funktioniert es auch hier.")),
        TexturesImportFailed { error } => return Some(format!("Das Texturpaket wurde nicht importiert: {error}. Dein bisheriges Paket ist unverändert.")),
        TexturesSettingsFailed { error } => return Some(format!("Die Textureinstellung konnte nicht geändert werden: {error}")),
        TexturesFlatQuestion => "Dieses Paket hat keinen nach einem Spiel benannten Ordner. Trotzdem beim installierten Spiel ablegen?",
        ButtonPlaceIt => "Ablegen",
        PickTexturesFolder => "Ordner deines Texturpakets auswählen",
        PickTexturesZip => "Dein Texturpaket auswählen (.zip)",
        CustomRomfsHeading => "Eigener RomFS-Ordner (nicht unterstützt)",
        CustomRomfsWarning => "Nicht unterstützt: kann das Spiel beschädigen; nicht vom Support abgedeckt.",
        CustomRomfsDescription => "Für eigene Experimente: Dateien in diesem Ordner ersetzen die Dateien des Spiels mit demselben RomFS-Pfad, zum Beispiel actor/zelda_link_boy_new.zar. Ersetzt werden nur Dateien, die das Spiel hat; höchstens 4096 Dateien mit je bis zu 64 MB, 1 GB insgesamt. Eine optionale triaevum-custom-romfs.json kann die Sammlung benennen. Forge verpackt oder teilt diese Dateien nie.",
        CustomRomfsFiles { files } => return Some(format!("Im Ordner: {files} Dateien")),
        CustomRomfsNamed { name, files } => return Some(format!("Im Ordner: {name}, {files} Dateien")),
        CustomRomfsEmpty => "Im Ordner: noch nichts",
        CustomRomfsRejected { reason } => return Some(format!("Nicht verwendbar: {reason}")),
        CustomRomfsFlagged { reason } => return Some(format!("Hinweis: {reason}")),
        CustomRomfsCheckbox => "Eigenen RomFS-Ordner verwenden",
        CustomRomfsConfirm => "Eigenen RomFS-Ordner einschalten? Nicht unterstützt: kann das Spiel beschädigen; nicht vom Support abgedeckt. Du kannst ihn jederzeit ausschalten und die Änderungen erneut anwenden.",
        ButtonTurnOn => "Einschalten",
        CustomRomfsApplied { files } => return Some(format!("Angewendet: {files} Dateien ersetzen die des Spiels")),
        ApplyHeading => "Änderungen anwenden",
        ApplyBody => "Forge wendet deine Auswahl aus den von deinem ROM aufbewahrten Spieldaten an, ohne erneut nach dem ROM zu fragen. Nur wenn diese Daten fehlen oder beschädigt sind, fragt es nach deinem ROM.",
        ApplyPending => "Einige Einstellungen sind noch nicht angewendet.",
        ApplyCurrent => "Das installierte Spiel verwendet deine aktuelle Auswahl.",
        ApplyNotInstalled => "Deine Auswahl wird angewendet, wenn du das Spiel unter „Start“ vorbereitest.",
        ButtonApplyChanges => "Änderungen anwenden",
        HudLayoutPolicyKeep => "HUD-Anordnung: Deine bleibt erhalten.",
        HudLayoutPolicyAsk => "HUD-Anordnung: Deine weicht vom Standardprofil ab; „Änderungen anwenden“ fragt zuerst („Meins behalten“ ist die Vorgabe).",
        ApplyingEnhancements => "Deine Auswahl an Verbesserungen wird angewendet...",
        ApplyChanged { list } => return Some(format!("Durch das Anwenden geändert: {list}")),
        EnhancementsLoading => "Der Stand der Verbesserungen wird gelesen...",
        EnhancementsGameRunning => "Das Spiel läuft: Schließe es, um Änderungen anzuwenden oder die Textureinstellung zu ändern.",
        EnhancementsStatusFailed { error } => return Some(format!("Der Stand der Verbesserungen kann nicht gelesen werden: {error}")),
        SettingNotSaved { error } => return Some(format!("Die Einstellung konnte nicht gespeichert werden: {error}")),
        SavingSetting => "Die Einstellung wird gespeichert...",
        DataInactive => "Nicht in Gebrauch",
        DataOldProfileNote => "Einige Dateien liegen noch dort, wo eine ältere Version sie abgelegt hat. Führe „Aktualisieren“ oder „Reparieren“ unter „Start“ aus (oder „Änderungen anwenden“ unter „Verbesserungen“), um die neuen Ordner zu verwenden; ohne das werden keine Dateien verschoben.",
        EnhancementsApplied { kept } => {
            let kept = if kept { "Deine Spielstände und Einstellungen wurden beibehalten." } else { "Deine Spielstände wurden beibehalten." };
            return Some(format!("Verbesserungen angewendet. {kept}"));
        }
        ForgeLanguageNotSaved { error } => {
            return Some(format!("Die Forge-Sprache konnte nicht gespeichert werden: {error}"))
        }
        Size { value, power } => {
            let unit = match power {
                0 => "B",
                1 => "KB",
                2 => "MB",
                3 => "GB",
                _ => "TB",
            };
            return Some(format!("{} {unit}", value.replace('.', ",")));
        }
        GuiFailed { error } => {
            return Some(format!(
                "Die Oberfläche von TriAevum Forge ist fehlgeschlagen: {error}\n\nDas Befehlszeilenwerkzeug \
                 triaevum-forge kann Spiele weiterhin vorbereiten."
            ))
        }
        _ => return None,
    };
    Some(fixed.to_string())
}
