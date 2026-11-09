// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! French (fr): every message of `en.rs`, in the same order.

use super::Msg;

pub fn text(msg: &Msg) -> Option<String> {
    use Msg::*;
    #[allow(unreachable_patterns)]
    let fixed = match *msg {
        WindowTitle => return Some(format!("TriAevum Forge \u{2014} {}", super::TITLE_NAME)),
        DialogTitle => "TriAevum Forge",
        NavHome => "Accueil",
        NavEnhancements => "Améliorations",
        NavData => "Données et copies de sauvegarde",
        NavDiagnostics => "Diagnostic",
        NavAbout => "À propos",
        Checking => "Vérification de l'installation locale...",
        PickRom => "Choisissez votre ROM déchiffrée (.3ds ou .cci)",
        RomLabel => "ROM déchiffrée",
        Browse => "Parcourir...",
        UseExtracted => "Utiliser des données extraites...",
        WhatForgeNeeds => "Ce dont Forge a besoin",
        Subtitle => {
            "Sélectionnez votre propre image de cartouche Nintendo 3DS déchiffrée. Forge accepte les ROM .3ds et .cci, \
             en extrait les données natives nécessaires et ne gère aucune clé de chiffrement. Vous pouvez aussi \
             sélectionner un dossier contenant code.bin, exheader.bin et romfs.bin décompressés."
        }
        PrivateData { path } => return Some(format!("Données privées générées : {path}")),
        PreparingHeading => "Préparation du jeu",
        PreparingElapsed { minutes, seconds } => return Some(format!("Temps écoulé : {minutes}:{seconds:02}")),
        ProgressCount { done, total } => return Some(format!("{done} sur {total}")),
        PreparingElsewhere => "Un autre Forge prépare cette installation",
        PreparingElsewhereBody => "Attendez la fin, puis vérifiez à nouveau l'installation.",
        ReadyHeading { revision, language } => return Some(format!("Prêt à jouer : {revision}, {language}")),
        ReadyHeadingNoLanguage { revision } => return Some(format!("Prêt à jouer : {revision}")),
        ReadyBody => "Vos sauvegardes et paramètres restent dans votre dossier de données.",
        PrepareAgain => "Préparer à nouveau depuis une ROM",
        UpdateHeading { installed, present } => {
            return Some(format!("Un nouveau paquet TriAevum est disponible ({installed} -> {present})."))
        }
        UpdateHeadingVersion { present } => {
            return Some(format!("Un nouveau paquet TriAevum est disponible (version {present})."))
        }
        UpdateHeadingPlain => "Un nouveau paquet TriAevum est disponible.",
        UpdateBody => {
            "« Mettre à jour » installe le nouveau paquet pour votre jeu préparé. Votre ROM n'est pas nécessaire ; \
             vos sauvegardes et paramètres sont conservés."
        }
        RepairBody => {
            "« Réparer » reconstruit ce qui manque à partir des données de jeu conservées depuis votre ROM. Vos \
             sauvegardes et paramètres sont conservés."
        }
        NeedsGameInputHeading => "Vos fichiers de jeu sont de nouveau nécessaires pour réparer cette installation",
        NeedsGameInputBody => {
            "Vous pouvez toujours jouer. Sélectionnez à nouveau votre ROM, puis choisissez « Réparer »."
        }
        Updating => "Installation de la mise à jour...",
        Repairing => "Réparation de l'installation...",
        Updated { version, kept } => {
            let version = if version.is_empty() { String::new() } else { format!(" vers {version}") };
            let kept = if kept {
                "Vos sauvegardes et paramètres ont été conservés."
            } else {
                "Vos sauvegardes ont été conservées."
            };
            return Some(format!("Mise à jour{version} terminée. {kept}"));
        }
        Repaired { kept } => {
            let kept = if kept {
                "Vos sauvegardes et paramètres ont été conservés."
            } else {
                "Vos sauvegardes ont été conservées."
            };
            return Some(format!("Réparation terminée. {kept}"));
        }
        AlreadyCurrent => "Cette installation utilise déjà ce paquet.",
        UpdateChanged { list } => return Some(format!("Modifications de la mise à jour : {list}")),
        RepairChanged { list } => return Some(format!("Modifications de la réparation : {list}")),
        ChangeCreated { file } => return Some(format!("{file} a été créé avec ses valeurs par défaut")),
        ChangeStandardLayout => "la disposition du HUD a été réinitialisée au profil standard",
        ChangeLanguage => "la langue de jeu choisie a été sélectionnée",
        ChangeRemoved { file } => return Some(format!("{file} a été supprimé")),
        ChangeOther { file } => return Some(format!("{file} a été modifié")),
        HudLayoutQuestion => "Réinitialiser la disposition du HUD au profil standard ?",
        HudLayoutBody => {
            "Votre disposition du HUD diffère du profil standard. « Garder la mienne » la laisse telle quelle ; \
             « Utiliser le standard » modifie ces paramètres :"
        }
        HudLayoutField { name, yours, standard } => {
            return Some(format!("{name} : votre valeur {yours}, standard {standard}"))
        }
        ButtonKeepMine => "Garder la mienne",
        ButtonUseStandard => "Utiliser le standard",
        DowngradeHeading => "Installer un paquet plus ancien ?",
        DowngradeBody { installed, present } => {
            return Some(format!(
                "Ce paquet ({present}) est plus ancien que celui qui est installé ({installed}). Installer quand même \
                 l'ancienne version ?"
            ))
        }
        ButtonInstallOlder => "Installer l'ancienne version",
        ButtonUpdateFromZip => "Mettre à jour depuis un ZIP téléchargé...",
        PickUpdateZipTitle => "Choisissez le ZIP TriAevum téléchargé",
        CheckingUpdatePackage => "Vérification du paquet de mise à jour...",
        ZipPreviewHeading { version } => return Some(format!("Mettre à jour vers {version} ?")),
        ZipPreviewBody { installed, version } => return Some(format!("Les fichiers du programme TriAevum de cette installation sont remplacés ({installed} devient {version}). Vos sauvegardes, paramètres, états de sauvegarde, mods et téléchargements sont conservés. Forge se ferme pendant la mise à jour et se rouvre quand elle est terminée.")),
        ZipSigned { key } => return Some(format!("Signé par l'éditeur de TriAevum (clé {key}).")),
        ZipUnsigned => "Attention : ce paquet n'est pas signé par l'éditeur de TriAevum. Il n'est accepté que parce que l'exception pour développeurs est active.",
        ZipLocallyModified { list } => return Some(format!("Ces fichiers du paquet ont été modifiés sur cet ordinateur et sont remplacés : {list}")),
        ButtonUpdateNow => "Mettre à jour",
        UpdateHandingOff => "Mise à jour en cours : Forge se ferme maintenant et se rouvre quand la mise à jour est terminée.",
        UpdateRolledBack { error } => return Some(format!("La mise à jour n'a pas abouti, la version précédente a donc été restaurée. Vos sauvegardes et paramètres ont été conservés.\n\n{error}")),
        UpdateRefused { error } => return Some(format!("La mise à jour n'a pas démarré ; rien n'a été modifié.\n\n{error}")),
        UpdateRecoveryNeeded { error } => return Some(format!("La mise à jour interrompue n'a pas pu être annulée automatiquement. Choisissez « Réparer » pour réessayer ; « Jouer » reste bloqué jusque-là.\n\n{error}")),
        UpdateRecovered => "La mise à jour interrompue a été annulée ; la version précédente est revenue.",
        UpdateCompleted => "La mise à jour interrompue a été terminée.",
        ShowDetails => "Afficher les détails",
        HideDetails => "Masquer les détails",
        Dismiss => "Fermer",
        HomeHdDownload { megabytes } => {
            return Some(format!(
                "Les textures HD de l'interface ({megabytes} Mo) sont téléchargées avec la préparation. Vous pouvez \
                 modifier ce choix dans « Améliorations »."
            ))
        }
        HomeHdReady => "Les textures HD de l'interface sont stockées et appliquées avec la préparation.",
        HomeHdOff => "Les textures HD de l'interface sont désactivées : le jeu utilise ses textures natives.",
        ButtonPrepare => "Préparer",
        ButtonPlay => "Jouer",
        ButtonUpdate => "Mettre à jour",
        ButtonRepair => "Réparer",
        ButtonCancel => "Annuler",
        ButtonCheckInstallation => "Vérifier l'installation",
        ButtonChooseRom => "Choisir la ROM...",
        ButtonCopyDiagnostics => "Copier le diagnostic",
        ButtonViewDiagnostics => "Afficher le diagnostic",
        ButtonSupportedRevisions => "Révisions prises en charge",
        ButtonDataPage => "Données et copies de sauvegarde",
        ButtonRetry => "Réessayer",
        ButtonContinue => "Continuer",
        ButtonOk => "OK",
        ButtonYes => "Oui",
        ButtonNo => "Non",
        ButtonOpenFolder => "Ouvrir le dossier",
        StatusSelectRom => "Sélectionnez votre ROM pour commencer.",
        StatusChooseLanguage => "ROM reconnue : choisissez la langue du jeu, puis « Préparer ».",
        StatusReady => "Prêt à jouer.",
        StatusActionNeeded => "Action requise : voir « Accueil ».",
        StatusCancelled => "Préparation annulée. Ce qui était installé auparavant reste utilisable.",
        StatusPreparingElsewhere => "Un autre Forge prépare cette installation.",
        LanguageSaved => "Langue du jeu enregistrée. Elle s'applique au prochain démarrage complet du jeu.",
        NoLanguages => "Sélectionnez votre ROM : les langues de jeu qu'elle propose apparaissent ici.",
        GameLanguage => "Langue du jeu",
        Recognizing => "Reconnaissance de la ROM...",
        RomNotRecognized => "ROM non reconnue",
        ChooseLanguage => "Choisissez la langue du jeu :",
        LanguageForInstall => {
            "« Préparer » configure le jeu dans cette langue. Vous pourrez la modifier plus tard ici ou en jeu."
        }
        LanguageInstalled => "Les modifications s'appliquent au prochain démarrage complet du jeu.",
        RomRecognized { label } => return Some(format!("ROM reconnue : {label}")),
        LanguagesUnreadable { error } => {
            return Some(format!("Les langues du jeu sont lues pendant l'installation. {error}"))
        }
        LanguageChosen { label } => return Some(format!("Langue du jeu pour cette installation : {label}.")),
        LanguageSettingsUnreadable { error } => {
            return Some(format!("Impossible de lire les paramètres de langue du jeu : {error}"))
        }
        Starting => "Démarrage de la préparation du jeu...",
        DownloadingHdFirst => "Téléchargement des textures HD avant la préparation du jeu...",
        DownloadingModFirst => "Téléchargement du mod activé avant la préparation du jeu...",
        ReadyDialog => return Some(format!("Le jeu est prêt. Vous pouvez lancer TriAevum \u{2014} {} maintenant.", super::TITLE_NAME)),
        CloseWhileBusy => "La préparation du jeu est toujours en cours. Fermer Forge ?",
        ConfirmCancel => "Arrêter la préparation ? Ce qui était installé auparavant reste utilisable.",
        EnhancementsBody => "Choisissez les améliorations voulues, puis cliquez sur « Appliquer les modifications ». Sans elles, le jeu utilise ses ressources natives.",
        HdHeading => "Textures HD de l'interface (recommandé)",
        HdDescription { title, version, author, credits, license } => {
            return Some(format!(
                "{title} {version} par {author} (crédits : {credits}) : textures HD pour le HUD, les menus et \
                 TopScreen, dans la langue de jeu choisie ici ou en jeu. Sous licence {license} : TriAevum ne le \
                 distribue pas. Forge télécharge l'archive originale de l'auteur depuis GameBanana, la vérifie et la \
                 conserve dans votre dossier de données privé. Si le téléchargement ne fonctionne pas, téléchargez \
                 le zip depuis la page du mod et sélectionnez-le ici. Merci à RLG (M-1) pour SingleScreen, le mod \
                 sur lequel repose le TopScreen de TriAevum ; sa police HD est incluse avec TriAevum."
            ))
        }
        HdDownload => "Télécharger les textures HD",
        SelectZip => "Sélectionner un .zip...",
        SelectFolder => "Sélectionner le dossier extrait...",
        ModPage => "Page du mod",
        License => "Licence",
        HdAutoDownload => "Utiliser le pack d'interface HD (téléchargé avec « Préparer » ou « Appliquer les modifications » s'il n'est pas encore stocké)",
        NotAvailable => "Non disponible pour les révisions du jeu prises en charge par ce paquet.",
        CheckingStored => "Vérification des fichiers stockés...",
        HdCheckFailed { error } => return Some(format!("Impossible de vérifier les textures HD : {error}")),
        HdReady => "Textures HD prêtes. Elles sont appliquées par « Appliquer les modifications » et par la prochaine préparation.",
        HdArchive => "Archive des textures HD stockée. Elle est préparée et appliquée par « Appliquer les modifications » et par la prochaine préparation.",
        HdMissingAuto { megabytes } => return Some(format!("Pas encore stockées : les textures HD ({megabytes} Mo) sont téléchargées lorsque vous préparez le jeu ou appliquez les modifications. Sans elles, le jeu utilise les textures natives.")),
        HdMissing => "Non installées : le jeu utilise les textures natives.",
        ModHeading => "Mods facultatifs",
        ModDescription { title, version, author, testing, tools, license } => {
            return Some(format!(
                "{title} {version} par {author} : modèles plus fidèles pour Link et ses objets. Tests : {testing}. \
                 Outils : {tools}. Sous licence {license} : TriAevum ne le distribue pas. Forge télécharge l'archive \
                 originale de l'auteur depuis GameBanana, la vérifie et l'applique uniquement à vos données de jeu \
                 locales. Si le téléchargement ne fonctionne pas, téléchargez l'archive depuis la page du mod et \
                 sélectionnez-la ici."
            ))
        }
        ModCheckbox { title } => return Some(format!("Utiliser {title}")),
        ModDownload => "Télécharger le mod",
        SelectRar => "Sélectionner un .rar...",
        ModSettingsUnreadable { error } => return Some(format!("Impossible de lire les paramètres du mod : {error}")),
        ModCheckFailed { error } => return Some(format!("Impossible de vérifier les fichiers du mod : {error}")),
        ModMissingEnabled => "Non téléchargé. Il est téléchargé lorsque vous appliquez les modifications ou préparez le jeu.",
        ModOff => "Désactivé : le jeu utilise ses modèles natifs.",
        ModOn => "Activé : appliqué par « Appliquer les modifications » et par la prochaine préparation.",
        ModOffKept => "Désactivé : le jeu utilise ses modèles natifs. L'archive téléchargée est conservée.",
        ApplyModChange => " Cliquez sur « Appliquer les modifications » pour l'appliquer.",
        HdDownloadFailed { error } => return Some(format!("Échec du téléchargement des textures HD : {error} Le jeu est préparé avec les textures natives. Téléchargez le zip depuis la page du mod et sélectionnez-le avec « Sélectionner un .zip... », puis appliquez les modifications.")),
        ModDownloadFailed { error } => {
            return Some(format!(
                "Échec du téléchargement du mod : {error} Le jeu est préparé avec les modèles natifs. Téléchargez \
                 l'archive depuis la page du mod et sélectionnez-la avec « Sélectionner un .rar... »."
            ))
        }
        ManualHdFailure { error, archive, page } => {
            return Some(format!(
                "{error}\n\nTéléchargez {archive} depuis la page du mod ({page}) et sélectionnez-le avec \
                 « Sélectionner un .zip... »."
            ))
        }
        ManualModFailure { error, archive, page } => {
            return Some(format!(
                "{error}\n\nTéléchargez {archive} depuis la page du mod ({page}) et sélectionnez-le avec \
                 « Sélectionner un .rar... »."
            ))
        }
        VerifyingArchive => "Vérification de l'archive sélectionnée...",
        VerifyingTextures => "Vérification des textures extraites...",
        VerifyingFiles => "Vérification des fichiers extraits...",
        DownloadingHd => "Téléchargement des textures HD...",
        DownloadingMod { title } => return Some(format!("Téléchargement de {title}...")),
        HdReadyReinstall => "Textures HD prêtes. Cliquez sur « Appliquer les modifications » pour les utiliser.",
        ModReadyReinstall { title } => return Some(format!("{title} est prêt. Cliquez sur « Appliquer les modifications » pour l'utiliser.")),
        HdArchiveApplied => "Archive de textures HD prête. Elle est appliquée par cette installation.",
        DownloadsFolderNote => {
            "Forge conserve dans ce dossier les archives qu'il télécharge, ainsi qu'une copie vérifiée de chaque \
             archive que vous sélectionnez. Sans réseau, téléchargez l'archive depuis la page du mod sur n'importe \
             quel appareil et sélectionnez-la avec les boutons ci-dessus."
        }
        ModSettingSaveFailed { error } => {
            return Some(format!("Impossible d'enregistrer le paramètre du mod : {error}"))
        }
        RevisionsUnreadable { error } => {
            return Some(format!("Impossible de lire les révisions du jeu prises en charge : {error}"))
        }
        PickRomTitle => "Sélectionnez une ROM .3ds ou .cci déchiffrée",
        FilterRom => "ROM Nintendo 3DS",
        Filter3ds => "Image 3DS",
        FilterCci => "Image CCI",
        PickExtractedTitle => "Sélectionnez les données de jeu extraites",
        PickArchive { name } => return Some(format!("Sélectionnez {name}")),
        PickArchiveFallbackHd => "Sélectionnez le zip du pack de textures",
        PickArchiveFallbackMod => "Sélectionnez l'archive",
        FilterTexturePack => "Archive de pack de textures",
        PickFolder { title } => return Some(format!("Sélectionnez le dossier extrait de {title}")),
        FilterModArchive => "Archive de mod",
        DataBody => {
            "Où TriAevum conserve vos fichiers, ce que contient une copie de sauvegarde et ce qui peut être nettoyé \
             sans risque. Conservez le dossier de données lorsque vous déplacez ou mettez à jour TriAevum."
        }
        DataFolder => "Dossier de données",
        ForgeLog => "Journal de Forge",
        DataYourFiles => "Vos fichiers",
        DataEntryLabel { name } => {
            return Some(
                match name {
                    "save_data" => "Sauvegardes",
                    "save_states" => "États de sauvegarde",
                    "save_states_default" => "États de sauvegarde restés dans l'ancien dossier",
                    "config" => "Paramètres",
                    "runtime_config" => "Fichier de paramètres du jeu",
                    "topscreen_config" => "Disposition du HUD",
                    "controls_config" => "Commandes",
                    "game_language" => "Langue du jeu",
                    "romfs_mods" => "Choix des mods",
                    "savestates_index" => "Liste des états de sauvegarde",
                    "environments" => "Environnements",
                    "imgui_layout" => "Disposition de la fenêtre F1",
                    "imgui_layout_legacy" => "Disposition de la fenêtre F1 d'une ancienne version",
                    "android_host_config" => "Paramètres d'affichage Android",
                    "touch_preferences" => "Commandes tactiles",
                    "texture_load" => "Votre pack de textures",
                    "texture_dump" => "Textures exportées",
                    "mods" => "Mods",
                    "downloads" => "Téléchargements",
                    "sources" => "Données de jeu conservées depuis votre ROM",
                    "titles" => "Jeu préparé",
                    "cache" => "Caches préparés",
                    "shader_seeds" => "Données de préparation graphique",
                    "interior_sun" => "Données d'éclairage intérieur",
                    "renderer_cache" => "Cache du moteur de rendu",
                    "translator_cache" => "Cache du traducteur",
                    "game_logs" => "Journaux du jeu",
                    "forge_logs" => "Journaux de Forge",
                    "crash_dumps" => "Rapports de plantage",
                    "android_diagnostics" => "Rapports de démarrage",
                    "texture_load_legacy" => "Pack de textures dans le dossier d'Azahar",
                    "save_states_legacy" => "États sauvegardés dans l'ancien dossier",
                    "game_logs_legacy" => "Journaux du jeu dans l'ancien dossier",
                    "crash_dumps_legacy" => "Rapports de plantage dans l'ancien dossier",
                    "active_title" => "Fiche du jeu installé",
                    other => other,
                }
                .to_string(),
            )
        }
        DataCategoryHelp { category } => {
            return Some(
                match category {
                    "saves" => {
                        "Les fichiers de sauvegarde du jeu lui-même, comme sur la cartouche. Toujours inclus dans vos \
                         copies de sauvegarde."
                    }
                    "save_states" => {
                        "États de sauvegarde (F5/F8 et menu des états de sauvegarde) avec leurs images. Toujours \
                         inclus dans vos copies de sauvegarde."
                    }
                    "settings" => {
                        "Vos paramètres : graphismes, disposition du HUD, commandes, langue du jeu et environnements, \
                         ainsi que la disposition de la fenêtre F1. Toujours inclus dans vos copies de sauvegarde."
                    }
                    "textures" => {
                        "Un pack de textures que vous avez ajouté vous-même, et les textures exportées depuis le jeu. \
                         Votre pack peut être ajouté à une copie de sauvegarde ; les textures exportées n'y sont \
                         jamais incluses."
                    }
                    "enhancements" => {
                        "Mods et archives téléchargées. Comme ils peuvent être téléchargés à nouveau, une copie de \
                         sauvegarde ne les inclut que si vous le choisissez."
                    }
                    "generated" => {
                        "Créé par Forge à partir de votre ROM. Jamais inclus dans une copie de sauvegarde : Forge le \
                         recrée à partir de votre ROM, et il ne doit pas être partagé."
                    }
                    "logs" => {
                        "Journaux et rapports de plantage pour le dépannage. Jamais inclus dans une copie de \
                         sauvegarde ; « Diagnostic » les utilise."
                    }
                    _ => "Fichiers utilisés par TriAevum.",
                }
                .to_string(),
            )
        }
        DataNotCreated => "pas encore créé",
        DataOriginDefault => "Emplacement par défaut",
        DataOriginExplicit => "Défini par vous",
        DataOriginLegacy => "Ancienne version",
        DataBackupInclude => "Dans chaque copie de sauvegarde",
        DataBackupOptional => "Dans une copie de sauvegarde si vous le choisissez",
        DataBackupExclude => "Exclu des copies de sauvegarde",
        ButtonWhatIsThis => "Qu'est-ce que c'est ?",
        ButtonHideHelp => "Masquer l'aide",
        DataActionsHeading => "Copie de sauvegarde, restauration et nettoyage",
        ButtonBackUp => "Créer une copie de sauvegarde...",
        ButtonRestore => "Restaurer...",
        ButtonClearCache => "Vider le cache du moteur de rendu",
        ButtonRebuild => "Reconstruire les ressources préparées",
        ButtonRemoveGame => "Supprimer le jeu préparé...",
        ButtonUninstallHelp => "Aide à la désinstallation",
        ButtonChooseBackupFile => "Choisir l'emplacement...",
        ButtonConfirmRestore => "Restaurer ces fichiers",
        ButtonConfirmClear => "Vider le cache",
        ButtonConfirmRebuild => "Reconstruire maintenant",
        ButtonConfirmRemove => "Supprimer le jeu préparé",
        ButtonRecover => "Terminer la récupération",
        BackupPanelBody { files, size } => {
            return Some(format!(
                "Une copie de sauvegarde contient vos sauvegardes, états de sauvegarde et paramètres : {files} \
                 fichiers, {size}. C'est un seul fichier .zip que vous pouvez conserver n'importe où."
            ))
        }
        BackupOptionalHeading => "Inclure aussi (non sélectionné par défaut) :",
        BackupExcludedNote => {
            "Jamais inclus : les données de jeu créées à partir de votre ROM, les caches, les journaux, les rapports \
             de plantage et les fiches d'installation. Forge les recrée, ou ils ne sont pas nécessaires."
        }
        BackupDone { path, size } => return Some(format!("Copie de sauvegarde enregistrée ({size}) : {path}")),
        FilterBackup => "Copie de sauvegarde TriAevum",
        SaveBackupTitle => "Enregistrer la copie de sauvegarde",
        OpenBackupTitle => "Choisir une copie de sauvegarde à restaurer",
        RestoreHeading { created, platform } => {
            return Some(format!("Copie de sauvegarde du {created}, créée sur {platform}"))
        }
        RestoreOtherPlatform { platform } => {
            return Some(format!(
                "Cette copie de sauvegarde a été créée sur {platform}. Les sauvegardes, les états de sauvegarde, la \
                 disposition du HUD, la langue du jeu et les environnements sont restaurés ; les paramètres \
                 d'affichage, de commandes et de fenêtre restent tels qu'ils sont ici."
            ))
        }
        RestoreDifferentRevision => {
            "Cette copie de sauvegarde a été créée avec une autre révision du jeu que celle installée ici."
        }
        RestoreUnavailable { path } => {
            return Some(format!(
                "Un paramètre de cette copie de sauvegarde désigne un dossier qui n'existe pas ici ; il reste \
                 inchangé : {path}"
            ))
        }
        RestoreKeptExisting { path } => {
            return Some(format!(
                "Un fichier de cette copie de sauvegarde n'est pas restauré, celui d'ici est conservé : {path}"
            ))
        }
        RestoreGroupLine { label, replaced, added, removed } => {
            return Some(format!("{label} : {replaced} remplacés, {added} ajoutés, {removed} supprimés"))
        }
        RestoreKeptSettings { files } => return Some(format!("Ces paramètres restent tels qu'ils sont ici : {files}")),
        RestoreSkipped { labels } => return Some(format!("Non restauré ici : {labels}")),
        RestoreSnapshotNote { path } => {
            return Some(format!(
                "Avant toute modification, Forge copie chaque fichier qu'il remplace ou supprime dans un dossier de \
                 restauration dans {path}. Si la restauration est interrompue, Forge remet vos fichiers précédents \
                 en place avant que vous puissiez jouer."
            ))
        }
        RestoreDone { path } => {
            return Some(format!("Restauration terminée. Les fichiers remplacés sont conservés dans {path}"))
        }
        ClearCacheBody { size } => {
            return Some(format!(
                "Cela supprime le cache du moteur de rendu ({size}). Le jeu le reconstruit ; le prochain démarrage \
                 peut être plus long."
            ))
        }
        ClearCacheEmpty => "Il n'y a aucun cache du moteur de rendu à vider.",
        ClearCacheDone => "Le cache du moteur de rendu a été vidé.",
        RemoveGameBody { size } => {
            return Some(format!(
                "Cela supprime les données de jeu que Forge a générées à partir de votre ROM ({size}). Vos \
                 sauvegardes et paramètres sont conservés. Vous aurez besoin de votre ROM pour réinstaller le jeu."
            ))
        }
        RemoveGameKeep => {
            "Vos mods et téléchargements sont également conservés. Il est recommandé de les garder : une nouvelle \
             installation n'a alors pas besoin de les télécharger à nouveau."
        }
        RemoveGameDone => "Le jeu préparé a été supprimé. Choisissez votre ROM dans « Accueil » pour le réinstaller.",
        RebuildBody { size } => {
            return Some(format!(
                "Forge prépare à nouveau le jeu à partir des données de ROM qu'il a conservées ({size}) et l'installe. \
                 Vos sauvegardes et paramètres sont conservés. Cela peut prendre plusieurs minutes."
            ))
        }
        RebuildNoSource => {
            "Les données de ROM conservées par Forge sont manquantes. Choisissez votre ROM dans « Accueil » pour \
             préparer à nouveau le jeu."
        }
        RebuildDone => "Le jeu préparé a été reconstruit.",
        UninstallWindows { path } => {
            return Some(format!(
                "Faites d'abord une copie de sauvegarde de vos fichiers, puis supprimez ce dossier : {path}. Rien \
                 d'autre n'a été installé sur cet ordinateur."
            ))
        }
        UninstallPortable { path } => {
            return Some(format!(
                "Faites d'abord une copie de sauvegarde de vos fichiers, puis supprimez ce dossier : {path}"
            ))
        }
        UninstallAppImage { appimage, path } => {
            return Some(format!(
                "Faites d'abord une copie de sauvegarde de vos fichiers. Supprimez ensuite l'AppImage ({appimage}) \
                 et, si vous ne voulez plus de vos sauvegardes, ce dossier : {path}"
            ))
        }
        UninstallFlatpak { id, path } => {
            return Some(format!(
                "Faites d'abord une copie de sauvegarde de vos fichiers. Exécutez ensuite : flatpak uninstall {id}. \
                 Vos fichiers restent dans {path} ; supprimez vous-même ce dossier si vous le souhaitez."
            ))
        }
        UninstallMacApp { path } => {
            return Some(format!(
                "Sauvegardez d'abord vos fichiers. Placez ensuite TriAevum.app dans la Corbeille et, si vous ne voulez \n                 plus vos sauvegardes, supprimez ce dossier : {path}"
            ))
        }
        UninstallAndroid { path } => {
            return Some(format!(
                "Exportez d'abord vos fichiers. La désinstallation de l'application supprime son stockage propre, y \
                 compris {path}."
            ))
        }
        UninstallExternal => {
            "Ces dossiers vous appartiennent, en dehors de l'installation ; Forge ne les supprime jamais :"
        }
        UninstallKeep => {
            "Si vous risquez de réinstaller, conservez vos mods et téléchargements : Forge n'aura alors pas besoin de \
             les télécharger à nouveau."
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
        DataGameRunning => {
            "TriAevum est en cours d'exécution. Fermez le jeu pour faire une copie de sauvegarde, restaurer ou \
             nettoyer."
        }
        DataForgeRunning => "Un autre Forge travaille sur cette installation. Attendez qu'il ait terminé.",
        DataRecoveryPending => {
            "Une restauration a été interrompue. Terminez la récupération pour remettre vos fichiers précédents en \
             place ; « Jouer » reste bloqué jusque-là."
        }
        RecoveryDone => "La restauration interrompue a été annulée ; vos fichiers précédents sont de retour.",
        DataLoading => "Recherche de l'emplacement de vos fichiers...",
        DataBackingUp => "Copie de sauvegarde de vos fichiers...",
        DataCheckingBackup => "Vérification de la copie de sauvegarde...",
        DataRestoring => "Restauration de vos fichiers...",
        DataClearingCache => "Vidage du cache du moteur de rendu...",
        DataRemovingGame => "Suppression du jeu préparé...",
        DataRebuilding => "Reconstruction du jeu préparé...",
        DataRecovering => "Annulation de la restauration interrompue...",
        DiagnosticsBody => {
            "Ce que Forge sait de cette installation. Copiez-le dans un rapport de bug ; les noms d'utilisateur sont \
             retirés des chemins."
        }
        StateLabel { state } => return Some(format!("État de l'installation : {state}")),
        Copied => "Copié dans le presse-papiers.",
        LastGameRun { started, seconds, meaning } => {
            return Some(format!("Dernière session de jeu : démarrée {started}, durée {seconds} s, {meaning}."))
        }
        GameRunning => "Le jeu est en cours d'exécution.",
        NoGameRun => "Aucune session de jeu enregistrée par ce Forge pour l'instant.",
        CollectingDiagnostics => "Collecte du diagnostic...",
        DiagnosticsFailed { error } => return Some(format!("Le diagnostic n'a pas pu être collecté : {error}")),
        GameLogFolder => "Journal du jeu",
        BundleContents => {
            "« Enregistrer l'archive d'assistance... » écrit un zip contenant ces fichiers, sans noms d'utilisateur ni \
             dossiers d'installation. Il ne contient jamais de sauvegardes, d'états de sauvegarde, de données de jeu \
             ni de fichiers de vidage de plantage."
        }
        BundleOmitted { name, reason } => return Some(format!("Exclu : {name} ({reason})")),
        ButtonSaveBundle => "Enregistrer l'archive d'assistance...",
        SaveBundleTitle => "Enregistrer l'archive d'assistance",
        FilterZip => "Archive zip",
        BundleSaved { path } => return Some(format!("Archive d'assistance enregistrée : {path}")),
        BundleFailed { error } => return Some(format!("L'archive d'assistance n'a pas pu être enregistrée : {error}")),
        SteamDeckHeading => "Steam Deck et manettes",
        SteamDeckBody => {
            "Forge fonctionne sans souris : Tab et Maj+Tab passent d'une commande à l'autre, Entrée active celle qui \
             est en surbrillance et Échap ferme une boîte de dialogue. Sur Steam Deck, préparez le jeu en mode \
             Bureau, puis ajoutez l'AppImage à Steam comme jeu non-Steam (sans Proton). Dans ses paramètres de \
             manette, choisissez le modèle Steam Input « Gamepad with Mouse Trackpad » (manette avec pavé tactile \
             en souris) : les sticks et les boutons restent une manette pour le jeu, et le pavé tactile droit \
             déplace le pointeur et clique, si bien que Forge s'utilise aussi en mode Jeu."
        }
        Version { version } => return Some(format!("Version {version}")),
        AboutBody => {
            return Some(format!(
                "TriAevum Forge prépare TriAevum \u{2014} {} à partir de votre propre ROM déchiffrée. C'est \
             un logiciel libre distribué sous la GNU General Public License, version 3 ou ultérieure, et il ne \
             contient aucune donnée de jeu.",
                super::TITLE_NAME
            ))
        }
        SupportedRevisions => "Révisions du jeu prises en charge",
        NoSupportedRevisions => "Ce paquet n'indique aucune révision du jeu prise en charge.",
        ProjectPage => "Page du projet",
        UserGuide => "Guide de l'utilisateur",
        ForgeLanguage => "Langue de Forge",
        ForgeLanguageNote => {
            "Les langues de votre jeu. Les textes pas encore traduits s'affichent en anglais. Votre choix est \
             mémorisé."
        }
        StateDownloaded { version } => return Some(format!("Téléchargé : version {version}")),
        StateNotDownloaded => "Non téléchargé",
        StateOn => "Votre choix : activé",
        StateOff => "Votre choix : désactivé",
        StateApplied => "Appliqué : le jeu installé l'utilise",
        StateNotApplied => "Appliqué : non",
        StateApplyPending => "Pas encore appliqué : cliquez sur « Appliquer les modifications »",
        StateAppliedLater => "Appliqué quand vous préparez le jeu",
        HdPreferenceNotSaved { error } => return Some(format!("Le choix de l'interface HD n'a pas pu être enregistré : {error}")),
        TexturesHeading => "Votre pack de textures",
        TexturesDescription => "Votre propre pack de textures Azahar ou Citra (fichiers tex1_... .png). Forge le copie dans le dossier ci-dessous et le vérifie ; le jeu l'affiche par-dessus le pack d'interface HD et ses propres textures. Importez un dossier ou un .zip, avec ou sans le dossier du titre du jeu.",
        TexturesImported { files } => return Some(format!("Importé : {files} fichiers")),
        TexturesNone => "Importé : aucun pack de textures pour l'instant",
        TexturesLoadCheckbox => "Charger les textures personnalisées",
        TexturesApplied => "Appliqué : le jeu le charge au démarrage",
        TexturesShadowedSettings => "Non chargé : F1 > Materials > Texture packs indique un autre dossier (ci-dessous).",
        TexturesShadowedEnvironment => "Non chargé : la variable d'environnement OOT3D_AZAHAR_LOAD_DIRECTORY indique un autre dossier (ci-dessous).",
        TexturesShadowedAzahar => "Non chargé : le jeu charge encore le pack du dossier d'Azahar (ci-dessous). Importez-le pour utiliser le dossier de votre installation.",
        TexturesOlderProfile => "Pas encore chargé : cliquez sur « Appliquer les modifications » pour que le jeu utilise ce dossier.",
        TexturesLegacy => "Un pack de textures se trouve dans le dossier d'Azahar. Importez-le pour le garder avec cette installation ; l'original reste à sa place.",
        ButtonUseThisFolder => "Utiliser le dossier de l'installation",
        ButtonImportTextureFolder => "Importer un dossier...",
        ButtonImportTextureZip => "Importer un .zip...",
        ButtonImportLegacy => "L'importer",
        TexturesImporting => "Vérification et copie de votre pack de textures...",
        TexturesChoosing => "Choisissez votre pack de textures dans la fenêtre...",
        TexturesImportDone { textures, files } => return Some(format!("Pack de textures importé : {textures} textures dans {files} fichiers. « Charger les textures personnalisées » est activé.")),
        TexturesImportOtherTitle { title } => return Some(format!("Il a été fait pour {title} ; les textures sont reconnues par leur contenu, il fonctionne donc ici aussi.")),
        TexturesImportFailed { error } => return Some(format!("Le pack de textures n'a pas été importé : {error}. Votre pack précédent est inchangé.")),
        TexturesSettingsFailed { error } => return Some(format!("Le réglage des textures n'a pas pu être modifié : {error}")),
        TexturesFlatQuestion => "Ce pack n'a pas de dossier au nom d'un jeu. Le placer quand même sous le jeu installé ?",
        ButtonPlaceIt => "Le placer",
        PickTexturesFolder => "Sélectionnez le dossier de votre pack de textures",
        PickTexturesZip => "Sélectionnez votre pack de textures (.zip)",
        CustomRomfsHeading => "Dossier RomFS personnalisé (non pris en charge)",
        CustomRomfsWarning => "Non pris en charge : peut casser le jeu ; non couvert par l'assistance.",
        CustomRomfsDescription => "Pour vos propres expériences : les fichiers de ce dossier remplacent les fichiers du jeu ayant le même chemin RomFS, par exemple actor/zelda_link_boy_new.zar. Seuls les fichiers que le jeu possède sont remplacés ; au plus 4096 fichiers de 64 Mo chacun au maximum, 1 Go au total. Un fichier facultatif triaevum-custom-romfs.json peut nommer l'ensemble. Forge n'empaquette ni ne partage jamais ces fichiers.",
        CustomRomfsFiles { files } => return Some(format!("Dans le dossier : {files} fichiers")),
        CustomRomfsNamed { name, files } => return Some(format!("Dans le dossier : {name}, {files} fichiers")),
        CustomRomfsEmpty => "Dans le dossier : rien pour l'instant",
        CustomRomfsRejected { reason } => return Some(format!("Inutilisable : {reason}")),
        CustomRomfsFlagged { reason } => return Some(format!("Remarque : {reason}")),
        CustomRomfsCheckbox => "Utiliser le dossier RomFS personnalisé",
        CustomRomfsConfirm => "Activer le dossier RomFS personnalisé ? Non pris en charge : peut casser le jeu ; non couvert par l'assistance. Vous pouvez le désactiver et appliquer à nouveau les modifications à tout moment.",
        ButtonTurnOn => "L'activer",
        CustomRomfsApplied { files } => return Some(format!("Appliqué : {files} fichiers remplacent ceux du jeu")),
        ApplyHeading => "Appliquer les modifications",
        ApplyBody => "Forge applique vos choix à partir des données du jeu conservées de votre ROM, sans redemander la ROM. Il ne la demande que si ces données manquent ou sont endommagées.",
        ApplyPending => "Certains choix ne sont pas encore appliqués.",
        ApplyCurrent => "Le jeu installé utilise vos choix actuels.",
        ApplyNotInstalled => "Vos choix sont appliqués quand vous préparez le jeu dans « Accueil ».",
        ButtonApplyChanges => "Appliquer les modifications",
        HudLayoutPolicyKeep => "Disposition du HUD : la vôtre est conservée.",
        HudLayoutPolicyAsk => "Disposition du HUD : la vôtre diffère du profil standard ; « Appliquer les modifications » demande d'abord (« Garder la mienne » par défaut).",
        ApplyingEnhancements => "Application de vos choix d'améliorations...",
        ApplyChanged { list } => return Some(format!("Modifications de l'application : {list}")),
        EnhancementsLoading => "Lecture de l'état des améliorations...",
        EnhancementsGameRunning => "Le jeu est en cours d'exécution : fermez-le pour appliquer les modifications ou changer le réglage des textures.",
        EnhancementsStatusFailed { error } => return Some(format!("Impossible de lire l'état des améliorations : {error}")),
        SettingNotSaved { error } => return Some(format!("Le réglage n'a pas pu être enregistré : {error}")),
        SavingSetting => "Enregistrement du réglage...",
        DataInactive => "Inutilisé",
        DataOldProfileNote => "Certains fichiers sont encore là où une ancienne version les rangeait. Lancez « Mettre à jour » ou « Réparer » dans « Accueil » (ou « Appliquer les modifications » dans « Améliorations ») pour utiliser les nouveaux dossiers ; les fichiers ne sont jamais déplacés sans cela.",
        EnhancementsApplied { kept } => {
            let kept = if kept { "Vos sauvegardes et paramètres ont été conservés." } else { "Vos sauvegardes ont été conservées." };
            return Some(format!("Améliorations appliquées. {kept}"));
        }
        ForgeLanguageNotSaved { error } => {
            return Some(format!("Impossible d'enregistrer la langue de Forge : {error}"))
        }
        Size { value, power } => {
            let unit = match power {
                0 => "o",
                1 => "Ko",
                2 => "Mo",
                3 => "Go",
                _ => "To",
            };
            return Some(format!("{} {unit}", value.replace('.', ",")));
        }
        GuiFailed { error } => {
            return Some(format!(
                "Échec de l'interface graphique de TriAevum Forge : {error}\n\nL'outil en ligne de commande \
                 triaevum-forge peut toujours préparer des jeux."
            ))
        }
        _ => return None,
    };
    Some(fixed.to_string())
}
