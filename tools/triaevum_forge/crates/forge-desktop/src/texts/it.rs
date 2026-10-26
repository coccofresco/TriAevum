// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Italian (it): every message of `en.rs`, in the same order. Button and
//! page names quoted in sentences are this table's labels.

use super::Msg;

pub fn text(msg: &Msg) -> Option<String> {
    use Msg::*;
    #[allow(unreachable_patterns)]
    let fixed = match *msg {
        WindowTitle => "TriAevum Forge \u{2014} Echoes of Time",
        DialogTitle => "TriAevum Forge",
        NavHome => "Home",
        NavEnhancements => "Miglioramenti",
        NavData => "Dati e backup",
        NavDiagnostics => "Diagnostica",
        NavAbout => "Informazioni",
        Checking => "Controllo dell'installazione locale...",
        PickRom => "Scegli la tua ROM decriptata (.3ds o .cci)",
        RomLabel => "ROM decriptata",
        Browse => "Sfoglia...",
        UseExtracted => "Usa dati estratti...",
        WhatForgeNeeds => "Che cosa serve a Forge",
        Subtitle => {
            "Seleziona l'immagine decriptata della tua cartuccia Nintendo 3DS. Forge accetta ROM .3ds e .cci, ne \
             estrae i dati nativi necessari e non gestisce chiavi di crittografia. Puoi anche selezionare una \
             cartella che contiene code.bin, exheader.bin e romfs.bin decompressi."
        }
        PrivateData { path } => return Some(format!("Dati generati privati: {path}")),
        PreparingHeading => "Preparazione del gioco",
        PreparingElapsed { minutes, seconds } => return Some(format!("Tempo trascorso: {minutes}:{seconds:02}")),
        ProgressCount { done, total } => return Some(format!("{done} di {total}")),
        PreparingElsewhere => "Un'altra finestra di Forge sta preparando questa installazione",
        PreparingElsewhereBody => "Attendi che finisca, poi controlla di nuovo l'installazione.",
        ReadyHeading { revision, language } => return Some(format!("Pronto per giocare: {revision}, {language}")),
        ReadyHeadingNoLanguage { revision } => return Some(format!("Pronto per giocare: {revision}")),
        ReadyBody => "I tuoi salvataggi e le tue impostazioni restano nella cartella dei dati.",
        PrepareAgain => "Prepara di nuovo da una ROM",
        UpdateHeading { installed, present } => {
            return Some(format!("È disponibile un nuovo pacchetto di TriAevum ({installed} -> {present})."))
        }
        UpdateHeadingVersion { present } => {
            return Some(format!("È disponibile un nuovo pacchetto di TriAevum (versione {present})."))
        }
        UpdateHeadingPlain => "È disponibile un nuovo pacchetto di TriAevum.",
        UpdateBody => {
            "«Aggiorna» installa il nuovo pacchetto per il gioco già preparato. La ROM non serve; i salvataggi e \
             le impostazioni vengono mantenuti."
        }
        RepairBody => {
            "«Ripara» ricrea ciò che manca a partire dai dati del gioco conservati dalla tua ROM. I salvataggi e \
             le impostazioni vengono mantenuti."
        }
        NeedsGameInputHeading => "Per riparare questa installazione servono di nuovo i file del gioco",
        NeedsGameInputBody => "Puoi comunque giocare. Seleziona di nuovo la ROM, poi scegli «Ripara».",
        Updating => "Installazione dell'aggiornamento...",
        Repairing => "Riparazione dell'installazione...",
        Updated { version, kept } => {
            let version = if version.is_empty() { String::new() } else { format!(" alla versione {version}") };
            let kept = if kept {
                "I salvataggi e le impostazioni sono stati mantenuti."
            } else {
                "I salvataggi sono stati mantenuti."
            };
            return Some(format!("Aggiornato{version}. {kept}"));
        }
        Repaired { kept } => {
            let kept = if kept {
                "I salvataggi e le impostazioni sono stati mantenuti."
            } else {
                "I salvataggi sono stati mantenuti."
            };
            return Some(format!("Riparato. {kept}"));
        }
        AlreadyCurrent => "Questa installazione usa già questo pacchetto.",
        UpdateChanged { list } => return Some(format!("Modifiche dell'aggiornamento: {list}")),
        RepairChanged { list } => return Some(format!("Modifiche della riparazione: {list}")),
        ChangeCreated { file } => return Some(format!("{file} è stato creato con i valori predefiniti")),
        ChangeStandardLayout => "il layout dell'HUD è stato riportato al profilo standard",
        ChangeLanguage => "è stata impostata la lingua del gioco che hai scelto",
        ChangeRemoved { file } => return Some(format!("{file} è stato rimosso")),
        ChangeOther { file } => return Some(format!("{file} è stato modificato")),
        HudLayoutQuestion => "Riportare il layout dell'HUD al profilo standard?",
        HudLayoutBody => {
            "Il tuo layout dell'HUD è diverso dal profilo standard. «Mantieni il mio» lo lascia com'è; «Usa \
             standard» cambia queste impostazioni:"
        }
        HudLayoutField { name, yours, standard } => return Some(format!("{name}: tuo {yours}, standard {standard}")),
        ButtonKeepMine => "Mantieni il mio",
        ButtonUseStandard => "Usa standard",
        DowngradeHeading => "Installare un pacchetto meno recente?",
        DowngradeBody { installed, present } => {
            return Some(format!(
                "Questo pacchetto ({present}) è meno recente di quello installato ({installed}). Installare \
                 comunque la versione precedente?"
            ))
        }
        ButtonInstallOlder => "Installa versione precedente",
        ButtonUpdateFromZip => "Aggiorna da uno ZIP scaricato...",
        PickUpdateZipTitle => "Scegli lo ZIP di TriAevum scaricato",
        CheckingUpdatePackage => "Controllo del pacchetto di aggiornamento...",
        ZipPreviewHeading { version } => return Some(format!("Aggiornare alla versione {version}?")),
        ZipPreviewBody { installed, version } => return Some(format!("I file del programma TriAevum di questa installazione vengono sostituiti ({installed} diventa {version}). Salvataggi, impostazioni, stati salvati, mod e download vengono mantenuti. Forge si chiude durante l'aggiornamento e si riapre quando ha finito.")),
        ZipSigned { key } => return Some(format!("Firmato dall'editore di TriAevum (chiave {key}).")),
        ZipUnsigned => "Attenzione: questo pacchetto non è firmato dall'editore di TriAevum. Viene accettato solo perché l'eccezione per sviluppatori è attiva.",
        ZipLocallyModified { list } => return Some(format!("Questi file del pacchetto sono stati modificati su questo computer e vengono sostituiti: {list}")),
        ButtonUpdateNow => "Aggiorna",
        UpdateHandingOff => "Aggiornamento in corso: Forge si chiude ora e si riapre quando l'aggiornamento è finito.",
        UpdateRolledBack { error } => return Some(format!("L'aggiornamento non è terminato, quindi è stata ripristinata la versione precedente. I salvataggi e le impostazioni sono stati mantenuti.\n\n{error}")),
        UpdateRefused { error } => return Some(format!("L'aggiornamento non è partito; non è stato modificato nulla.\n\n{error}")),
        UpdateRecoveryNeeded { error } => return Some(format!("Non è stato possibile annullare automaticamente l'aggiornamento interrotto. Scegli «Ripara» per riprovare; fino ad allora «Gioca» resta bloccato.\n\n{error}")),
        UpdateRecovered => "L'aggiornamento interrotto è stato annullato; è tornata la versione precedente.",
        UpdateCompleted => "L'aggiornamento interrotto è stato completato.",
        ShowDetails => "Mostra dettagli",
        HideDetails => "Nascondi dettagli",
        Dismiss => "Chiudi",
        HomeHdDownload { megabytes } => {
            return Some(format!(
                "Le texture HD dell'interfaccia ({megabytes} MB) vengono scaricate durante la preparazione. Puoi \
                 cambiare questa scelta in Miglioramenti."
            ))
        }
        HomeHdReady => "Le texture HD dell'interfaccia sono già salvate e vengono applicate con la preparazione.",
        HomeHdOff => "Texture HD dell'interfaccia disattivate: il gioco usa le sue texture native.",
        ButtonPrepare => "Prepara",
        ButtonPlay => "Gioca",
        ButtonUpdate => "Aggiorna",
        ButtonRepair => "Ripara",
        ButtonCancel => "Annulla",
        ButtonCheckInstallation => "Controlla installazione",
        ButtonChooseRom => "Scegli la ROM...",
        ButtonCopyDiagnostics => "Copia diagnostica",
        ButtonViewDiagnostics => "Mostra diagnostica",
        ButtonSupportedRevisions => "Revisioni supportate",
        ButtonDataPage => "Dati e backup",
        ButtonRetry => "Riprova",
        ButtonContinue => "Continua",
        ButtonOk => "OK",
        ButtonYes => "Sì",
        ButtonNo => "No",
        ButtonOpenFolder => "Apri cartella",
        StatusSelectRom => "Seleziona la tua ROM per iniziare.",
        StatusChooseLanguage => "ROM riconosciuta: scegli la lingua del gioco, poi «Prepara».",
        StatusReady => "Pronto per giocare.",
        StatusActionNeeded => "Serve un intervento: vedi Home.",
        StatusCancelled => "Preparazione annullata. Ciò che era installato prima resta utilizzabile.",
        StatusPreparingElsewhere => "Un'altra finestra di Forge sta preparando questa installazione.",
        LanguageSaved => "Lingua del gioco salvata. Vale dal prossimo avvio completo del gioco.",
        NoLanguages => "Seleziona la tua ROM: qui compaiono le lingue del gioco che contiene.",
        GameLanguage => "Lingua del gioco",
        Recognizing => "Riconoscimento della ROM...",
        RomNotRecognized => "ROM non riconosciuta",
        ChooseLanguage => "Scegli la lingua del gioco:",
        LanguageForInstall => {
            "«Prepara» installa il gioco in questa lingua. Puoi cambiarla in seguito qui o nel gioco."
        }
        LanguageInstalled => "Le modifiche valgono dal prossimo avvio completo del gioco.",
        RomRecognized { label } => return Some(format!("ROM riconosciuta: {label}")),
        LanguagesUnreadable { error } => {
            return Some(format!("Le lingue del gioco vengono lette durante l'installazione. {error}"))
        }
        LanguageChosen { label } => return Some(format!("Lingua del gioco per questa installazione: {label}.")),
        LanguageSettingsUnreadable { error } => {
            return Some(format!("Impossibile leggere le impostazioni della lingua del gioco: {error}"))
        }
        Starting => "Avvio della preparazione del gioco...",
        DownloadingHdFirst => "Download delle texture HD prima della preparazione del gioco...",
        DownloadingModFirst => "Download della mod attivata prima della preparazione del gioco...",
        ReadyDialog => "Il gioco è pronto. Ora puoi avviare TriAevum \u{2014} Echoes of Time.",
        CloseWhileBusy => "La preparazione del gioco è ancora in corso. Chiudere Forge?",
        ConfirmCancel => "Interrompere la preparazione? Ciò che era installato prima resta utilizzabile.",
        EnhancementsBody => {
            "I miglioramenti facoltativi vengono applicati alla prossima preparazione. Senza di essi il gioco usa \
             le sue risorse native."
        }
        HdHeading => "Texture HD dell'interfaccia (consigliate)",
        HdDescription { title, version, author, credits, license } => {
            return Some(format!(
                "{title} {version} di {author} (riconoscimenti: {credits}): texture HD per l'HUD, i menu e \
                 TopScreen, nella lingua del gioco che scegli qui o nel gioco. Licenza {license}: TriAevum non la \
                 distribuisce. Forge scarica l'archivio originale dell'autore da GameBanana, lo verifica e lo \
                 conserva nella tua cartella dei dati privati. Se il download non riesce, scarica lo zip dalla \
                 pagina della mod e selezionalo qui. Grazie a RLG (M-1) per SingleScreen, la mod su cui si basa \
                 TopScreen di TriAevum; il suo font HD è incluso in TriAevum."
            ))
        }
        HdDownload => "Scarica le texture HD",
        SelectZip => "Seleziona .zip...",
        SelectFolder => "Seleziona cartella estratta...",
        ModPage => "Pagina della mod",
        License => "Licenza",
        HdAutoDownload => "Scarica le texture HD con «Prepara» se non sono ancora salvate",
        NotAvailable => "Non disponibile per le revisioni del gioco supportate da questo pacchetto.",
        CheckingStored => "Controllo dei file salvati...",
        HdCheckFailed { error } => return Some(format!("Impossibile controllare le texture HD: {error}")),
        HdReady => "Texture HD pronte. Vengono applicate quando prepari e installi il gioco.",
        HdArchive => {
            "Archivio delle texture HD salvato. Viene preparato e applicato quando prepari e installi il gioco."
        }
        HdMissingAuto { megabytes } => {
            return Some(format!(
                "Non ancora salvate: le texture HD ({megabytes} MB) vengono scaricate quando prepari e installi il \
                 gioco. Senza di esse il gioco usa le texture native."
            ))
        }
        HdMissing => "Non installate: il gioco usa le texture native.",
        ModHeading => "Mod facoltative",
        ModDescription { title, version, author, testing, tools, license } => {
            return Some(format!(
                "{title} {version} di {author}: modelli più fedeli per Link e i suoi oggetti. Test: {testing}. \
                 Strumenti: {tools}. Licenza {license}: TriAevum non la distribuisce. Forge scarica l'archivio \
                 originale dell'autore da GameBanana, lo verifica e lo applica solo ai dati del gioco sul tuo \
                 computer. Se il download non riesce, scarica l'archivio dalla pagina della mod e selezionalo qui."
            ))
        }
        ModCheckbox { title } => return Some(format!("Usa {title} (applicata da «Prepara»)")),
        ModDownload => "Scarica la mod",
        SelectRar => "Seleziona .rar...",
        ModSettingsUnreadable { error } => {
            return Some(format!("Impossibile leggere le impostazioni delle mod: {error}"))
        }
        ModCheckFailed { error } => return Some(format!("Impossibile controllare i file della mod: {error}")),
        ModMissingEnabled => "Non scaricata. Viene scaricata quando prepari e installi il gioco.",
        ModOff => "Disattivata: il gioco usa i suoi modelli nativi.",
        ModOn => "Attivata: viene applicata quando prepari e installi il gioco.",
        ModOffKept => "Disattivata: il gioco usa i suoi modelli nativi. L'archivio scaricato viene conservato.",
        ApplyModChange => " Prepara e installa di nuovo il gioco per applicare la modifica.",
        HdDownloadFailed { error } => {
            return Some(format!(
                "Download delle texture HD non riuscito: {error} Il gioco viene preparato con le texture native. \
                 Scarica lo zip dalla pagina della mod e selezionalo con «Seleziona .zip...», poi prepara e \
                 installa di nuovo."
            ))
        }
        ModDownloadFailed { error } => {
            return Some(format!(
                "Download della mod non riuscito: {error} Il gioco viene preparato con i modelli nativi. Scarica \
                 l'archivio dalla pagina della mod e selezionalo con «Seleziona .rar...»."
            ))
        }
        ManualHdFailure { error, archive, page } => {
            return Some(format!(
                "{error}\n\nScarica {archive} dalla pagina della mod ({page}) e selezionalo con «Seleziona .zip...»."
            ))
        }
        ManualModFailure { error, archive, page } => {
            return Some(format!(
                "{error}\n\nScarica {archive} dalla pagina della mod ({page}) e selezionalo con «Seleziona .rar...»."
            ))
        }
        VerifyingArchive => "Verifica dell'archivio selezionato...",
        VerifyingTextures => "Verifica delle texture estratte...",
        VerifyingFiles => "Verifica dei file estratti...",
        DownloadingHd => "Download delle texture HD...",
        DownloadingMod { title } => return Some(format!("Download di {title}...")),
        HdReadyReinstall => "Texture HD pronte. Prepara e installa di nuovo il gioco per applicarle.",
        ModReadyReinstall { title } => {
            return Some(format!("{title} è pronta. Prepara e installa di nuovo il gioco per applicarla."))
        }
        HdArchiveApplied => "Archivio delle texture HD pronto. Viene applicato da questa installazione.",
        DownloadsFolderNote => {
            "Forge conserva in questa cartella gli archivi che scarica e una copia verificata di ogni archivio che \
             selezioni. Senza rete, scarica l'archivio dalla pagina della mod da qualsiasi dispositivo e selezionalo \
             con i pulsanti qui sopra."
        }
        ModSettingSaveFailed { error } => {
            return Some(format!("Impossibile salvare l'impostazione della mod: {error}"))
        }
        RevisionsUnreadable { error } => {
            return Some(format!("Impossibile leggere le revisioni del gioco supportate: {error}"))
        }
        PickRomTitle => "Seleziona una ROM .3ds o .cci decriptata",
        FilterRom => "ROM Nintendo 3DS",
        Filter3ds => "Immagine 3DS",
        FilterCci => "Immagine CCI",
        PickExtractedTitle => "Seleziona i dati estratti del gioco",
        PickArchive { name } => return Some(format!("Seleziona {name}")),
        PickArchiveFallbackHd => "Seleziona lo zip del pacchetto di texture",
        PickArchiveFallbackMod => "Seleziona l'archivio",
        FilterTexturePack => "Archivio del pacchetto di texture",
        PickFolder { title } => return Some(format!("Seleziona la cartella estratta di {title}")),
        FilterModArchive => "Archivio della mod",
        DataBody => {
            "Dove TriAevum conserva i tuoi file, che cosa contiene un backup e che cosa si può eliminare senza \
             rischi. Conserva la cartella dei dati quando sposti o aggiorni TriAevum."
        }
        DataFolder => "Cartella dei dati",
        ForgeLog => "Log di Forge",
        DataYourFiles => "I tuoi file",
        DataEntryLabel { name } => {
            return Some(
                match name {
                    "save_data" => "Salvataggi",
                    "save_states" => "Stati salvati",
                    "save_states_default" => "Stati salvati rimasti nella cartella precedente",
                    "config" => "Impostazioni",
                    "runtime_config" => "File delle impostazioni del gioco",
                    "topscreen_config" => "Layout dell'HUD",
                    "controls_config" => "Controlli",
                    "game_language" => "Lingua del gioco",
                    "romfs_mods" => "Scelte delle mod",
                    "savestates_index" => "Elenco degli stati salvati",
                    "environments" => "Ambienti",
                    "imgui_layout" => "Layout della finestra F1",
                    "imgui_layout_legacy" => "Layout della finestra F1 di una versione precedente",
                    "android_host_config" => "Impostazioni di visualizzazione Android",
                    "touch_preferences" => "Controlli touch",
                    "texture_load" => "Il tuo pacchetto di texture",
                    "texture_dump" => "Texture estratte dal gioco",
                    "mods" => "Mod",
                    "downloads" => "Download",
                    "sources" => "Dati del gioco conservati dalla tua ROM",
                    "titles" => "Gioco preparato",
                    "cache" => "Cache preparate",
                    "shader_seeds" => "Dati di preparazione della grafica",
                    "interior_sun" => "Dati dell'illuminazione degli interni",
                    "renderer_cache" => "Cache del renderer",
                    "translator_cache" => "Cache del traduttore",
                    "game_logs" => "Log del gioco",
                    "forge_logs" => "Log di Forge",
                    "crash_dumps" => "Rapporti sui crash",
                    "android_diagnostics" => "Rapporti di avvio",
                    "active_title" => "Registro del gioco installato",
                    other => other,
                }
                .to_string(),
            )
        }
        DataCategoryHelp { category } => {
            return Some(
                match category {
                    "saves" => "I file di salvataggio del gioco, come sulla cartuccia. Sempre inclusi nei backup.",
                    "save_states" => {
                        "Gli stati salvati (F5/F8 e il menu degli stati salvati) con le loro immagini. Sempre \
                         inclusi nei backup."
                    }
                    "settings" => {
                        "Le tue impostazioni: grafica, layout dell'HUD, controlli, lingua del gioco e ambienti, e il \
                         layout della finestra F1. Sempre incluse nei backup."
                    }
                    "textures" => {
                        "Un pacchetto di texture che hai aggiunto tu e le texture estratte dal gioco. Il tuo \
                         pacchetto può essere incluso in un backup; le texture estratte non lo sono mai."
                    }
                    "enhancements" => {
                        "Mod e archivi scaricati. Si possono scaricare di nuovo, quindi un backup li include solo \
                         se lo scegli tu."
                    }
                    "generated" => {
                        "Creati da Forge a partire dalla tua ROM. Mai inclusi nei backup: Forge li ricrea dalla tua \
                         ROM, e non vanno condivisi."
                    }
                    "logs" => {
                        "Log e rapporti sui crash per la risoluzione dei problemi. Mai inclusi nei backup; li usa \
                         la Diagnostica."
                    }
                    _ => "File usati da TriAevum.",
                }
                .to_string(),
            )
        }
        DataNotCreated => "non ancora creato",
        DataOriginDefault => "Posizione predefinita",
        DataOriginExplicit => "Scelta da te",
        DataOriginLegacy => "Versione precedente",
        DataBackupInclude => "In ogni backup",
        DataBackupOptional => "In un backup, se lo scegli",
        DataBackupExclude => "Escluso dai backup",
        ButtonWhatIsThis => "Che cos'è?",
        ButtonHideHelp => "Nascondi aiuto",
        DataActionsHeading => "Backup, ripristino e pulizia",
        ButtonBackUp => "Fai il backup dei tuoi file...",
        ButtonRestore => "Ripristina...",
        ButtonClearCache => "Svuota la cache del renderer",
        ButtonRebuild => "Ricrea i dati preparati",
        ButtonRemoveGame => "Rimuovi il gioco preparato...",
        ButtonUninstallHelp => "Come disinstallare",
        ButtonChooseBackupFile => "Scegli dove salvare...",
        ButtonConfirmRestore => "Ripristina questi file",
        ButtonConfirmClear => "Svuota la cache",
        ButtonConfirmRebuild => "Ricrea ora",
        ButtonConfirmRemove => "Rimuovi il gioco preparato",
        ButtonRecover => "Completa il recupero",
        BackupPanelBody { files, size } => {
            return Some(format!(
                "Un backup contiene i tuoi salvataggi, gli stati salvati e le impostazioni: {files} file, {size}. \
                 È un unico file .zip che puoi conservare dove vuoi."
            ))
        }
        BackupOptionalHeading => "Includi anche (non selezionati di norma):",
        BackupExcludedNote => {
            "Mai inclusi: i dati del gioco creati dalla tua ROM, le cache, i log, i rapporti sui crash e i registri \
             dell'installazione. Forge li ricrea, oppure non servono."
        }
        BackupDone { path, size } => return Some(format!("Backup salvato ({size}): {path}")),
        FilterBackup => "Backup di TriAevum",
        SaveBackupTitle => "Salva il backup",
        OpenBackupTitle => "Scegli un backup da ripristinare",
        RestoreHeading { created, platform } => return Some(format!("Backup del {created}, creato su {platform}")),
        RestoreOtherPlatform { platform } => {
            return Some(format!(
                "Questo backup è stato creato su {platform}. Vengono ripristinati i salvataggi, gli stati salvati, \
                 il layout dell'HUD, la lingua del gioco e gli ambienti; le impostazioni di schermo, controlli e \
                 finestra restano quelle di qui."
            ))
        }
        RestoreDifferentRevision => {
            "Questo backup è stato creato con una revisione del gioco diversa da quella installata qui."
        }
        RestoreUnavailable { path } => {
            return Some(format!(
                "Un'impostazione di questo backup indica una cartella che qui non esiste; resta com'è: {path}"
            ))
        }
        RestoreKeptExisting { path } => {
            return Some(format!("Un file di questo backup non viene ripristinato, resta quello di qui: {path}"))
        }
        RestoreGroupLine { label, replaced, added, removed } => {
            return Some(format!("{label}: {replaced} sostituiti, {added} aggiunti, {removed} rimossi"))
        }
        RestoreKeptSettings { files } => return Some(format!("Queste impostazioni restano quelle di qui: {files}")),
        RestoreSkipped { labels } => return Some(format!("Non ripristinati qui: {labels}")),
        RestoreSnapshotNote { path } => {
            return Some(format!(
                "Prima di qualsiasi modifica, Forge copia ogni file che sostituisce o rimuove in una cartella di \
                 ripristino in {path}. Se il ripristino si interrompe, Forge rimette a posto i file precedenti prima \
                 che tu possa giocare."
            ))
        }
        RestoreDone { path } => {
            return Some(format!("Ripristino completato. I file sostituiti sono conservati in {path}"))
        }
        ClearCacheBody { size } => {
            return Some(format!(
                "Viene eliminata la cache del renderer ({size}). Il gioco la ricrea; il prossimo avvio può \
                 richiedere più tempo."
            ))
        }
        ClearCacheEmpty => "Non c'è nessuna cache del renderer da svuotare.",
        ClearCacheDone => "La cache del renderer è stata svuotata.",
        RemoveGameBody { size } => {
            return Some(format!(
                "Vengono eliminati i dati del gioco che Forge ha generato dalla tua ROM ({size}). I salvataggi e le \
                 impostazioni restano. Per installare di nuovo ti servirà la ROM."
            ))
        }
        RemoveGameKeep => {
            "Restano anche le tue mod e i download. È consigliabile conservarli: così una nuova installazione non \
             deve scaricarli di nuovo."
        }
        RemoveGameDone => "Il gioco preparato è stato rimosso. Scegli la tua ROM in Home per installarlo di nuovo.",
        RebuildBody { size } => {
            return Some(format!(
                "Forge prepara di nuovo il gioco dai dati della ROM che ha conservato ({size}) e lo installa. I \
                 salvataggi e le impostazioni restano. Può richiedere diversi minuti."
            ))
        }
        RebuildNoSource => {
            "Mancano i dati della ROM conservati da Forge. Scegli la tua ROM in Home per preparare di nuovo il gioco."
        }
        RebuildDone => "Il gioco preparato è stato ricreato.",
        UninstallWindows { path } => {
            return Some(format!(
                "Prima fai il backup dei tuoi file, poi elimina questa cartella: {path}. Non è stato installato \
                 nient'altro su questo computer."
            ))
        }
        UninstallPortable { path } => {
            return Some(format!("Prima fai il backup dei tuoi file, poi elimina questa cartella: {path}"))
        }
        UninstallAppImage { appimage, path } => {
            return Some(format!(
                "Prima fai il backup dei tuoi file. Poi elimina l'AppImage ({appimage}) e, se non vuoi più i tuoi \
                 salvataggi, questa cartella: {path}"
            ))
        }
        UninstallFlatpak { id, path } => {
            return Some(format!(
                "Prima fai il backup dei tuoi file. Poi esegui: flatpak uninstall {id}. I tuoi file restano in \
                 {path}; elimina tu quella cartella, se vuoi."
            ))
        }
        UninstallAndroid { path } => {
            return Some(format!(
                "Prima esporta i tuoi file. La disinstallazione dell'app rimuove la sua memoria riservata, compresa \
                 {path}."
            ))
        }
        UninstallExternal => "Queste cartelle sono tue, fuori dall'installazione; Forge non le elimina mai:",
        UninstallKeep => {
            "Se potresti installare di nuovo, conserva le tue mod e i download: così Forge non deve scaricarli di \
             nuovo."
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
        DataGameRunning => "TriAevum è in esecuzione. Chiudi il gioco per fare il backup, ripristinare o fare pulizia.",
        DataForgeRunning => "Un'altra finestra di Forge sta lavorando su questa installazione. Attendi che finisca.",
        DataRecoveryPending => {
            "Un ripristino è stato interrotto. Completa il recupero per rimettere a posto i file precedenti; fino ad \
             allora «Gioca» resta bloccato."
        }
        RecoveryDone => "Il ripristino interrotto è stato annullato; i file precedenti sono di nuovo al loro posto.",
        DataLoading => "Lettura della posizione dei tuoi file...",
        DataBackingUp => "Backup dei tuoi file in corso...",
        DataCheckingBackup => "Controllo del backup...",
        DataRestoring => "Ripristino dei tuoi file...",
        DataClearingCache => "Svuotamento della cache del renderer...",
        DataRemovingGame => "Rimozione del gioco preparato...",
        DataRebuilding => "Ricreazione del gioco preparato...",
        DataRecovering => "Annullamento del ripristino interrotto...",
        DiagnosticsBody => {
            "Che cosa sa Forge di questa installazione. Copialo in una segnalazione di bug; i nomi utente nei \
             percorsi vengono rimossi."
        }
        StateLabel { state } => return Some(format!("Stato dell'installazione: {state}")),
        Copied => "Copiato negli appunti.",
        LastGameRun { started, seconds, meaning } => {
            return Some(format!("Ultima sessione di gioco: avviata {started}, durata {seconds} s, {meaning}."))
        }
        GameRunning => "Il gioco è in esecuzione.",
        NoGameRun => "Questo Forge non ha ancora registrato nessuna sessione di gioco.",
        CollectingDiagnostics => "Raccolta della diagnostica...",
        DiagnosticsFailed { error } => return Some(format!("Impossibile raccogliere la diagnostica: {error}")),
        GameLogFolder => "Log del gioco",
        BundleContents => {
            "«Salva pacchetto per l'assistenza» scrive uno zip con questi file, senza nomi utente né cartelle \
             d'installazione. Non contiene mai salvataggi, stati salvati, dati del gioco o file di crash dump."
        }
        BundleOmitted { name, reason } => return Some(format!("Escluso: {name} ({reason})")),
        ButtonSaveBundle => "Salva pacchetto per l'assistenza...",
        SaveBundleTitle => "Salva il pacchetto per l'assistenza",
        FilterZip => "Archivio zip",
        BundleSaved { path } => return Some(format!("Pacchetto per l'assistenza salvato: {path}")),
        BundleFailed { error } => return Some(format!("Impossibile salvare il pacchetto per l'assistenza: {error}")),
        SteamDeckHeading => "Steam Deck e controller",
        SteamDeckBody => {
            "Forge funziona senza mouse: Tab e Maiusc+Tab passano da un controllo all'altro, Invio attiva quello \
             evidenziato ed Esc chiude una finestra di dialogo. Su Steam Deck prepara il gioco in modalità Desktop, \
             poi aggiungi l'AppImage a Steam come gioco non di Steam (senza Proton). Nelle impostazioni del \
             controller scegli il modello di Steam Input «Gamepad with Mouse Trackpad» (gamepad con trackpad come \
             mouse): levette e pulsanti restano un gamepad per il gioco, e il trackpad destro muove il puntatore e \
             fa clic, così Forge si usa anche in modalità Gioco."
        }
        Version { version } => return Some(format!("Versione {version}")),
        AboutBody => {
            "TriAevum Forge prepara TriAevum \u{2014} Echoes of Time dalla tua ROM decriptata. È software libero \
             distribuito con la GNU General Public License, versione 3 o successiva, e non contiene dati del gioco."
        }
        SupportedRevisions => "Revisioni del gioco supportate",
        NoSupportedRevisions => "Questo pacchetto non elenca nessuna revisione del gioco supportata.",
        ProjectPage => "Pagina del progetto",
        UserGuide => "Guida per l'utente",
        ForgeLanguage => "Lingua di Forge",
        ForgeLanguageNote => {
            "Le lingue del tuo gioco. I testi non ancora tradotti vengono mostrati in inglese. La scelta viene \
             ricordata."
        }
        ForgeLanguageNotSaved { error } => return Some(format!("Impossibile salvare la lingua di Forge: {error}")),
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
                "Impossibile avviare l'interfaccia di TriAevum Forge: {error}\n\nLo strumento a riga di comando \
                 triaevum-forge può comunque preparare i giochi."
            ))
        }
        _ => return None,
    };
    Some(fixed.to_string())
}
