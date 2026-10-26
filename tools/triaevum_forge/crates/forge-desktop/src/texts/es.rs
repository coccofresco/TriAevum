// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Spanish (es): every message of `en.rs`, in the same order. Button and
//! page names quoted in sentences are this table's labels.

use super::Msg;

pub fn text(msg: &Msg) -> Option<String> {
    use Msg::*;
    #[allow(unreachable_patterns)]
    let fixed = match *msg {
        WindowTitle => "TriAevum Forge \u{2014} Echoes of Time",
        DialogTitle => "TriAevum Forge",
        NavHome => "Inicio",
        NavEnhancements => "Mejoras",
        NavData => "Datos y copias",
        NavDiagnostics => "Diagnóstico",
        NavAbout => "Acerca de",
        Checking => "Comprobando la instalación local...",
        PickRom => "Elige tu ROM descifrada (.3ds o .cci)",
        RomLabel => "ROM descifrada",
        Browse => "Examinar...",
        UseExtracted => "Usar datos extraídos...",
        WhatForgeNeeds => "Qué necesita Forge",
        Subtitle => {
            "Selecciona la imagen descifrada de tu propio cartucho de Nintendo 3DS. Forge acepta ROM .3ds y .cci, \
             extrae los datos nativos necesarios y no maneja claves de cifrado. También puedes seleccionar una \
             carpeta que contenga code.bin, exheader.bin y romfs.bin descomprimidos."
        }
        PrivateData { path } => return Some(format!("Datos generados privados: {path}")),
        PreparingHeading => "Preparando el juego",
        PreparingElapsed { minutes, seconds } => return Some(format!("Tiempo transcurrido: {minutes}:{seconds:02}")),
        ProgressCount { done, total } => return Some(format!("{done} de {total}")),
        PreparingElsewhere => "Otro Forge está preparando esta instalación",
        PreparingElsewhereBody => "Espera a que termine y vuelve a comprobar la instalación.",
        ReadyHeading { revision, language } => return Some(format!("Listo para jugar: {revision}, {language}")),
        ReadyHeadingNoLanguage { revision } => return Some(format!("Listo para jugar: {revision}")),
        ReadyBody => "Tus partidas guardadas y tus ajustes se quedan en tu carpeta de datos.",
        PrepareAgain => "Volver a preparar desde una ROM",
        UpdateHeading { installed, present } => {
            return Some(format!("Hay un paquete nuevo de TriAevum ({installed} -> {present})."))
        }
        UpdateHeadingVersion { present } => {
            return Some(format!("Hay un paquete nuevo de TriAevum (versión {present})."))
        }
        UpdateHeadingPlain => "Hay un paquete nuevo de TriAevum.",
        UpdateBody => {
            "«Actualizar» instala el paquete nuevo para tu juego preparado. No necesitas la ROM; se conservan tus \
             partidas guardadas y tus ajustes."
        }
        RepairBody => {
            "«Reparar» reconstruye lo que falta a partir de los datos del juego conservados de tu ROM. Se conservan \
             tus partidas guardadas y tus ajustes."
        }
        NeedsGameInputHeading => "Se necesitan de nuevo los archivos del juego para reparar esta instalación",
        NeedsGameInputBody => "Aún puedes jugar. Vuelve a seleccionar tu ROM y pulsa «Reparar».",
        Updating => "Instalando la actualización...",
        Repairing => "Reparando la instalación...",
        Updated { version, kept } => {
            let version = if version.is_empty() { String::new() } else { format!(" a la versión {version}") };
            let kept = if kept {
                "Se han conservado tus partidas guardadas y tus ajustes."
            } else {
                "Se han conservado tus partidas guardadas."
            };
            return Some(format!("Actualizado{version}. {kept}"));
        }
        Repaired { kept } => {
            let kept = if kept {
                "Se han conservado tus partidas guardadas y tus ajustes."
            } else {
                "Se han conservado tus partidas guardadas."
            };
            return Some(format!("Reparado. {kept}"));
        }
        AlreadyCurrent => "Esta instalación ya usa este paquete.",
        UpdateChanged { list } => return Some(format!("Cambios de la actualización: {list}")),
        RepairChanged { list } => return Some(format!("Cambios de la reparación: {list}")),
        ChangeCreated { file } => return Some(format!("{file} se ha creado con sus valores predeterminados")),
        ChangeStandardLayout => "se ha restablecido el diseño del HUD al perfil estándar",
        ChangeLanguage => "se ha seleccionado el idioma del juego que elegiste",
        ChangeRemoved { file } => return Some(format!("{file} se ha eliminado")),
        ChangeOther { file } => return Some(format!("{file} se ha modificado")),
        HudLayoutQuestion => "¿Restablecer el diseño del HUD al perfil estándar?",
        HudLayoutBody => {
            "Tu diseño del HUD es distinto del perfil estándar. «Conservar el mío» lo deja como está; «Usar el \
             estándar» cambia estos ajustes:"
        }
        HudLayoutField { name, yours, standard } => {
            return Some(format!("{name}: el tuyo {yours}, el estándar {standard}"))
        }
        ButtonKeepMine => "Conservar el mío",
        ButtonUseStandard => "Usar el estándar",
        DowngradeHeading => "¿Instalar un paquete más antiguo?",
        DowngradeBody { installed, present } => {
            return Some(format!(
                "Este paquete ({present}) es más antiguo que el instalado ({installed}). ¿Instalar la versión más \
                 antigua de todos modos?"
            ))
        }
        ButtonInstallOlder => "Instalar versión antigua",
        ButtonUpdateFromZip => "Actualizar desde un ZIP descargado...",
        PickUpdateZipTitle => "Elige el ZIP de TriAevum descargado",
        CheckingUpdatePackage => "Comprobando el paquete de actualización...",
        ZipPreviewHeading { version } => return Some(format!("¿Actualizar a {version}?")),
        ZipPreviewBody { installed, version } => return Some(format!("Se sustituyen los archivos del programa TriAevum de esta instalación ({installed} pasa a ser {version}). Tus partidas guardadas, ajustes, estados guardados, mods y descargas se conservan. Forge se cierra durante la actualización y se vuelve a abrir al terminar.")),
        ZipSigned { key } => return Some(format!("Firmado por el editor de TriAevum (clave {key}).")),
        ZipUnsigned => "Atención: este paquete no está firmado por el editor de TriAevum. Solo se acepta porque la excepción para desarrolladores está activada.",
        ZipLocallyModified { list } => return Some(format!("Estos archivos del paquete se modificaron en este equipo y se sustituyen: {list}")),
        ButtonUpdateNow => "Actualizar",
        UpdateHandingOff => "Actualizando: Forge se cierra ahora y se vuelve a abrir cuando termine la actualización.",
        UpdateRolledBack { error } => return Some(format!("La actualización no terminó, así que se restauró la versión anterior. Tus partidas guardadas y tus ajustes se conservaron.\n\n{error}")),
        UpdateRefused { error } => return Some(format!("La actualización no empezó; no se cambió nada.\n\n{error}")),
        UpdateRecoveryNeeded { error } => return Some(format!("La actualización interrumpida no se pudo deshacer automáticamente. Elige «Reparar» para intentarlo de nuevo; «Jugar» sigue bloqueado hasta entonces.\n\n{error}")),
        UpdateRecovered => "La actualización interrumpida se deshizo; la versión anterior ha vuelto.",
        UpdateCompleted => "La actualización interrumpida se completó.",
        ShowDetails => "Mostrar detalles",
        HideDetails => "Ocultar detalles",
        Dismiss => "Descartar",
        HomeHdDownload { megabytes } => {
            return Some(format!(
                "Las texturas HD de la interfaz ({megabytes} MB) se descargan con la preparación. Puedes cambiarlo \
                 en «Mejoras»."
            ))
        }
        HomeHdReady => "Las texturas HD de la interfaz están guardadas y se aplican con la preparación.",
        HomeHdOff => "Las texturas HD de la interfaz están desactivadas: el juego usa sus texturas nativas.",
        ButtonPrepare => "Preparar",
        ButtonPlay => "Jugar",
        ButtonUpdate => "Actualizar",
        ButtonRepair => "Reparar",
        ButtonCancel => "Cancelar",
        ButtonCheckInstallation => "Comprobar instalación",
        ButtonChooseRom => "Elegir ROM...",
        ButtonCopyDiagnostics => "Copiar diagnóstico",
        ButtonViewDiagnostics => "Ver diagnóstico",
        ButtonSupportedRevisions => "Revisiones compatibles",
        ButtonDataPage => "Datos y copias",
        ButtonRetry => "Reintentar",
        ButtonContinue => "Continuar",
        ButtonOk => "Aceptar",
        ButtonYes => "Sí",
        ButtonNo => "No",
        ButtonOpenFolder => "Abrir carpeta",
        StatusSelectRom => "Selecciona tu ROM para empezar.",
        StatusChooseLanguage => "ROM reconocida: elige el idioma del juego y pulsa «Preparar».",
        StatusReady => "Listo para jugar.",
        StatusActionNeeded => "Acción necesaria: consulta «Inicio».",
        StatusCancelled => "Preparación cancelada. Lo que estaba instalado antes sigue siendo utilizable.",
        StatusPreparingElsewhere => "Otro Forge está preparando esta instalación.",
        LanguageSaved => "Idioma del juego guardado. Se aplica en el próximo inicio completo del juego.",
        NoLanguages => "Selecciona tu ROM: aquí aparecerán los idiomas del juego que ofrece.",
        GameLanguage => "Idioma del juego",
        Recognizing => "Reconociendo la ROM...",
        RomNotRecognized => "ROM no reconocida",
        ChooseLanguage => "Elige el idioma del juego:",
        LanguageForInstall => {
            "«Preparar» instala el juego en este idioma. Puedes cambiarlo más tarde aquí o en el juego."
        }
        LanguageInstalled => "Los cambios se aplican en el próximo inicio completo del juego.",
        RomRecognized { label } => return Some(format!("ROM reconocida: {label}")),
        LanguagesUnreadable { error } => {
            return Some(format!("Los idiomas del juego se leen durante la instalación. {error}"))
        }
        LanguageChosen { label } => return Some(format!("Idioma del juego para esta instalación: {label}.")),
        LanguageSettingsUnreadable { error } => {
            return Some(format!("No se pueden leer los ajustes de idioma del juego: {error}"))
        }
        Starting => "Iniciando la preparación del título...",
        DownloadingHdFirst => "Descargando las texturas HD antes de preparar el título...",
        DownloadingModFirst => "Descargando el mod activado antes de preparar el título...",
        ReadyDialog => "El título está listo. Ya puedes iniciar TriAevum \u{2014} Echoes of Time.",
        CloseWhileBusy => "La preparación del título sigue en curso. ¿Cerrar Forge?",
        ConfirmCancel => "¿Detener la preparación? Lo que estaba instalado antes sigue siendo utilizable.",
        EnhancementsBody => {
            "Las mejoras opcionales se aplican en la próxima preparación. Sin ellas, el juego usa sus recursos \
             nativos."
        }
        HdHeading => "Texturas HD de la interfaz (recomendadas)",
        HdDescription { title, version, author, credits, license } => {
            return Some(format!(
                "{title} {version} de {author} (créditos: {credits}): texturas HD para el HUD, los menús y \
                 TopScreen, en el idioma del juego que elijas aquí o en el juego. Con licencia {license}: TriAevum \
                 no lo distribuye. Forge descarga el archivo original del autor desde GameBanana, lo verifica y lo \
                 guarda en tu carpeta de datos privada. Si la descarga no funciona, descarga el zip desde la página \
                 del mod y selecciónalo aquí. Gracias a RLG (M-1) por SingleScreen, el mod en el que se basa el \
                 TopScreen de TriAevum; su fuente HD se incluye con TriAevum."
            ))
        }
        HdDownload => "Descargar texturas HD",
        SelectZip => "Seleccionar .zip...",
        SelectFolder => "Seleccionar carpeta extraída...",
        ModPage => "Página del mod",
        License => "Licencia",
        HdAutoDownload => "Descargar las texturas HD con «Preparar» si aún no están guardadas",
        NotAvailable => "No disponible para las revisiones del juego compatibles con este paquete.",
        CheckingStored => "Comprobando los archivos guardados...",
        HdCheckFailed { error } => return Some(format!("No se pueden comprobar las texturas HD: {error}")),
        HdReady => "Texturas HD listas. Se aplican al preparar e instalar el título.",
        HdArchive => "Archivo de texturas HD guardado. Se prepara y se aplica al preparar e instalar el título.",
        HdMissingAuto { megabytes } => {
            return Some(format!(
                "Aún no guardadas: las texturas HD ({megabytes} MB) se descargan al preparar e instalar el título. \
                 Sin ellas, el juego usa las texturas nativas."
            ))
        }
        HdMissing => "No instaladas: el juego usa las texturas nativas.",
        ModHeading => "Mods opcionales",
        ModDescription { title, version, author, testing, tools, license } => {
            return Some(format!(
                "{title} {version} de {author}: modelos de mayor fidelidad para Link y sus objetos. Pruebas: \
                 {testing}. Herramientas: {tools}. Con licencia {license}: TriAevum no lo distribuye. Forge \
                 descarga el archivo original del autor desde GameBanana, lo verifica y lo aplica solo a tus datos \
                 locales del juego. Si la descarga no funciona, descarga el archivo desde la página del mod y \
                 selecciónalo aquí."
            ))
        }
        ModCheckbox { title } => return Some(format!("Usar {title} (lo aplica «Preparar»)")),
        ModDownload => "Descargar mod",
        SelectRar => "Seleccionar .rar...",
        ModSettingsUnreadable { error } => return Some(format!("No se pueden leer los ajustes del mod: {error}")),
        ModCheckFailed { error } => return Some(format!("No se pueden comprobar los archivos del mod: {error}")),
        ModMissingEnabled => "No descargado. Se descarga al preparar e instalar el título.",
        ModOff => "Desactivado: el juego usa sus modelos nativos.",
        ModOn => "Activado: se aplica al preparar e instalar el título.",
        ModOffKept => "Desactivado: el juego usa sus modelos nativos. Se conserva el archivo descargado.",
        ApplyModChange => " Vuelve a preparar e instalar el título para aplicar el cambio.",
        HdDownloadFailed { error } => {
            return Some(format!(
                "Error al descargar las texturas HD: {error} El título se prepara con las texturas nativas. \
                 Descarga el zip desde la página del mod, selecciónalo con «Seleccionar .zip...» y vuelve a \
                 preparar e instalar."
            ))
        }
        ModDownloadFailed { error } => {
            return Some(format!(
                "Error al descargar el mod: {error} El título se prepara con los modelos nativos. Descarga el \
                 archivo desde la página del mod y selecciónalo con «Seleccionar .rar...»."
            ))
        }
        ManualHdFailure { error, archive, page } => {
            return Some(format!(
                "{error}\n\nDescarga {archive} desde la página del mod ({page}) y selecciónalo con «Seleccionar \
                 .zip...»."
            ))
        }
        ManualModFailure { error, archive, page } => {
            return Some(format!(
                "{error}\n\nDescarga {archive} desde la página del mod ({page}) y selecciónalo con «Seleccionar \
                 .rar...»."
            ))
        }
        VerifyingArchive => "Verificando el archivo seleccionado...",
        VerifyingTextures => "Verificando las texturas extraídas...",
        VerifyingFiles => "Verificando los archivos extraídos...",
        DownloadingHd => "Descargando las texturas HD...",
        DownloadingMod { title } => return Some(format!("Descargando {title}...")),
        HdReadyReinstall => "Texturas HD listas. Vuelve a preparar e instalar el título para aplicarlas.",
        ModReadyReinstall { title } => {
            return Some(format!("{title} está listo. Vuelve a preparar e instalar el título para aplicarlo."))
        }
        HdArchiveApplied => "Archivo de texturas HD listo. Lo aplica esta instalación.",
        DownloadsFolderNote => {
            "Forge guarda en esta carpeta los archivos que descarga y una copia comprobada de cada archivo que \
             selecciones. Sin conexión, descarga el archivo desde la página del mod en cualquier dispositivo y \
             selecciónalo con los botones de arriba."
        }
        ModSettingSaveFailed { error } => return Some(format!("No se puede guardar el ajuste del mod: {error}")),
        RevisionsUnreadable { error } => {
            return Some(format!("No se pueden leer las revisiones del juego compatibles: {error}"))
        }
        PickRomTitle => "Selecciona una ROM .3ds o .cci descifrada",
        FilterRom => "ROM de Nintendo 3DS",
        Filter3ds => "Imagen 3DS",
        FilterCci => "Imagen CCI",
        PickExtractedTitle => "Selecciona los datos extraídos del título",
        PickArchive { name } => return Some(format!("Selecciona {name}")),
        PickArchiveFallbackHd => "Selecciona el zip del paquete de texturas",
        PickArchiveFallbackMod => "Selecciona el archivo",
        FilterTexturePack => "Archivo de paquete de texturas",
        PickFolder { title } => return Some(format!("Selecciona la carpeta extraída de {title}")),
        FilterModArchive => "Archivo del mod",
        DataBody => {
            "Dónde guarda TriAevum tus archivos, qué contiene una copia de seguridad y qué se puede limpiar sin \
             riesgo. Conserva la carpeta de datos cuando muevas o actualices TriAevum."
        }
        DataFolder => "Carpeta de datos",
        ForgeLog => "Registro de Forge",
        DataYourFiles => "Tus archivos",
        DataEntryLabel { name } => {
            return Some(
                match name {
                    "save_data" => "Partidas guardadas",
                    "save_states" => "Estados guardados",
                    "save_states_default" => "Estados guardados que quedaron en la carpeta anterior",
                    "config" => "Ajustes",
                    "runtime_config" => "Archivo de ajustes del juego",
                    "topscreen_config" => "Diseño del HUD",
                    "controls_config" => "Controles",
                    "game_language" => "Idioma del juego",
                    "romfs_mods" => "Selección de mods",
                    "savestates_index" => "Lista de estados guardados",
                    "environments" => "Entornos",
                    "imgui_layout" => "Diseño de la ventana F1",
                    "imgui_layout_legacy" => "Diseño de la ventana F1 de una versión anterior",
                    "android_host_config" => "Ajustes de pantalla de Android",
                    "touch_preferences" => "Controles táctiles",
                    "texture_load" => "Tu paquete de texturas",
                    "texture_dump" => "Texturas volcadas",
                    "mods" => "Mods",
                    "downloads" => "Descargas",
                    "sources" => "Datos del juego conservados de tu ROM",
                    "titles" => "Juego preparado",
                    "cache" => "Cachés preparadas",
                    "shader_seeds" => "Datos de preparación gráfica",
                    "interior_sun" => "Datos de iluminación de interiores",
                    "renderer_cache" => "Caché del renderizador",
                    "translator_cache" => "Caché del traductor",
                    "game_logs" => "Registros del juego",
                    "forge_logs" => "Registros de Forge",
                    "crash_dumps" => "Informes de fallos",
                    "android_diagnostics" => "Informes de inicio",
                    "active_title" => "Registro del juego instalado",
                    other => other,
                }
                .to_string(),
            )
        }
        DataCategoryHelp { category } => {
            return Some(
                match category {
                    "saves" => {
                        "Los archivos de guardado del propio juego, como en el cartucho. Siempre en tus copias de \
                         seguridad."
                    }
                    "save_states" => {
                        "Estados guardados (F5/F8 y el menú de estados guardados) con sus imágenes. Siempre en tus \
                         copias de seguridad."
                    }
                    "settings" => {
                        "Tus ajustes: gráficos, diseño del HUD, controles, idioma del juego y entornos, y el diseño \
                         de la ventana F1. Siempre en tus copias de seguridad."
                    }
                    "textures" => {
                        "Un paquete de texturas que añadiste tú y las texturas volcadas del juego. Tu paquete puede \
                         incluirse en una copia de seguridad; los volcados nunca se incluyen."
                    }
                    "enhancements" => {
                        "Mods y archivos descargados. Se pueden volver a descargar, así que una copia de seguridad \
                         solo los incluye si tú lo eliges."
                    }
                    "generated" => {
                        "Creado por Forge a partir de tu ROM. Nunca se incluye en una copia de seguridad: Forge lo \
                         vuelve a crear a partir de tu ROM, y no debe compartirse."
                    }
                    "logs" => {
                        "Registros e informes de fallos para solucionar problemas. Nunca se incluyen en una copia \
                         de seguridad; los usa «Diagnóstico»."
                    }
                    _ => "Archivos que usa TriAevum.",
                }
                .to_string(),
            )
        }
        DataNotCreated => "aún no creado",
        DataOriginDefault => "Ubicación predeterminada",
        DataOriginExplicit => "Elegida por ti",
        DataOriginLegacy => "Versión anterior",
        DataBackupInclude => "En todas las copias",
        DataBackupOptional => "En la copia si lo eliges",
        DataBackupExclude => "Sin copia de seguridad",
        ButtonWhatIsThis => "¿Qué es esto?",
        ButtonHideHelp => "Ocultar ayuda",
        DataActionsHeading => "Copiar, restaurar y limpiar",
        ButtonBackUp => "Crear copia de seguridad...",
        ButtonRestore => "Restaurar...",
        ButtonClearCache => "Borrar caché del renderizador",
        ButtonRebuild => "Reconstruir recursos preparados",
        ButtonRemoveGame => "Eliminar juego preparado...",
        ButtonUninstallHelp => "Ayuda para desinstalar",
        ButtonChooseBackupFile => "Elegir dónde guardar...",
        ButtonConfirmRestore => "Restaurar estos archivos",
        ButtonConfirmClear => "Borrar la caché",
        ButtonConfirmRebuild => "Reconstruir ahora",
        ButtonConfirmRemove => "Eliminar el juego preparado",
        ButtonRecover => "Terminar la recuperación",
        BackupPanelBody { files, size } => {
            return Some(format!(
                "Una copia de seguridad contiene tus partidas guardadas, estados guardados y ajustes: {files} \
                 archivos, {size}. Es un único archivo .zip que puedes guardar donde quieras."
            ))
        }
        BackupOptionalHeading => "Incluir también (no seleccionado de forma predeterminada):",
        BackupExcludedNote => {
            "Nunca se incluyen: los datos del juego creados a partir de tu ROM, las cachés, los registros, los \
             informes de fallos ni los datos de control de la instalación. Forge los vuelve a crear, o no son \
             necesarios."
        }
        BackupDone { path, size } => return Some(format!("Copia de seguridad guardada ({size}): {path}")),
        FilterBackup => "Copia de seguridad de TriAevum",
        SaveBackupTitle => "Guardar la copia de seguridad",
        OpenBackupTitle => "Elige una copia de seguridad para restaurar",
        RestoreHeading { created, platform } => {
            return Some(format!("Copia de seguridad del {created}, hecha en {platform}"))
        }
        RestoreOtherPlatform { platform } => {
            return Some(format!(
                "Esta copia de seguridad se hizo en {platform}. Se restauran las partidas guardadas, los estados \
                 guardados, el diseño del HUD, el idioma del juego y los entornos; los ajustes de pantalla, de \
                 controles y de ventana se quedan como están aquí."
            ))
        }
        RestoreDifferentRevision => {
            "Esta copia de seguridad se hizo con una revisión del juego distinta de la instalada aquí."
        }
        RestoreUnavailable { path } => {
            return Some(format!(
                "Un ajuste de esta copia de seguridad indica una carpeta que no existe aquí; se queda como está: \
                 {path}"
            ))
        }
        RestoreKeptExisting { path } => {
            return Some(format!(
                "Un archivo de esta copia de seguridad no se restaura; se mantiene el que hay aquí: {path}"
            ))
        }
        RestoreGroupLine { label, replaced, added, removed } => {
            return Some(format!("{label}: {replaced} reemplazados, {added} añadidos, {removed} eliminados"))
        }
        RestoreKeptSettings { files } => return Some(format!("Estos ajustes se quedan como están aquí: {files}")),
        RestoreSkipped { labels } => return Some(format!("No se restaura aquí: {labels}")),
        RestoreSnapshotNote { path } => {
            return Some(format!(
                "Antes de cambiar nada, Forge copia cada archivo que reemplaza o elimina en una carpeta de \
                 restauración en {path}. Si la restauración se interrumpe, Forge vuelve a poner tus archivos \
                 anteriores antes de que puedas jugar."
            ))
        }
        RestoreDone { path } => return Some(format!("Restaurado. Los archivos reemplazados se conservan en {path}")),
        ClearCacheBody { size } => {
            return Some(format!(
                "Esto borra la caché del renderizador ({size}). El juego la vuelve a crear; el próximo inicio puede \
                 tardar más."
            ))
        }
        ClearCacheEmpty => "No hay caché del renderizador que borrar.",
        ClearCacheDone => "Se ha borrado la caché del renderizador.",
        RemoveGameBody { size } => {
            return Some(format!(
                "Esto borra los datos del juego que Forge generó a partir de tu ROM ({size}). Tus partidas \
                 guardadas y tus ajustes se quedan. Necesitarás tu ROM para volver a instalarlo."
            ))
        }
        RemoveGameKeep => {
            "Tus mods y descargas también se quedan. Se recomienda conservarlos: así una nueva instalación no \
             tiene que volver a descargarlos."
        }
        RemoveGameDone => "Se ha eliminado el juego preparado. Elige tu ROM en «Inicio» para volver a instalarlo.",
        RebuildBody { size } => {
            return Some(format!(
                "Forge vuelve a preparar el juego a partir de los datos de la ROM que conservó ({size}) y lo \
                 instala. Tus partidas guardadas y tus ajustes se quedan. Puede tardar varios minutos."
            ))
        }
        RebuildNoSource => {
            "Faltan los datos de la ROM que Forge conservó. Elige tu ROM en «Inicio» para volver a preparar el \
             juego."
        }
        RebuildDone => "Se ha reconstruido el juego preparado.",
        UninstallWindows { path } => {
            return Some(format!(
                "Primero haz una copia de seguridad de tus archivos y después borra esta carpeta: {path}. No se \
                 instaló nada más en este equipo."
            ))
        }
        UninstallPortable { path } => {
            return Some(format!(
                "Primero haz una copia de seguridad de tus archivos y después borra esta carpeta: {path}"
            ))
        }
        UninstallAppImage { appimage, path } => {
            return Some(format!(
                "Primero haz una copia de seguridad de tus archivos. Después borra el AppImage ({appimage}) y, si \
                 ya no quieres tus partidas guardadas, esta carpeta: {path}"
            ))
        }
        UninstallFlatpak { id, path } => {
            return Some(format!(
                "Primero haz una copia de seguridad de tus archivos. Después ejecuta: flatpak uninstall {id}. Tus \
                 archivos se quedan en {path}; borra esa carpeta tú mismo si quieres."
            ))
        }
        UninstallAndroid { path } => {
            return Some(format!(
                "Primero exporta tus archivos. Al desinstalar la aplicación se elimina su almacenamiento propio, \
                 incluido {path}."
            ))
        }
        UninstallExternal => "Estas carpetas son tuyas, fuera de la instalación; Forge nunca las borra:",
        UninstallKeep => {
            "Si quizá vuelvas a instalar, conserva tus mods y descargas: así Forge no tiene que volver a \
             descargarlos."
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
            "TriAevum se está ejecutando. Cierra el juego para hacer copias de seguridad, restaurar o limpiar."
        }
        DataForgeRunning => "Otro Forge está trabajando en esta instalación. Espera a que termine.",
        DataRecoveryPending => {
            "Se interrumpió una restauración. Pulsa «Terminar la recuperación» para volver a poner tus archivos \
             anteriores; «Jugar» sigue bloqueado hasta entonces."
        }
        RecoveryDone => "Se ha deshecho la restauración interrumpida; tus archivos anteriores han vuelto.",
        DataLoading => "Buscando dónde están tus archivos...",
        DataBackingUp => "Haciendo copia de seguridad de tus archivos...",
        DataCheckingBackup => "Comprobando la copia de seguridad...",
        DataRestoring => "Restaurando tus archivos...",
        DataClearingCache => "Borrando la caché del renderizador...",
        DataRemovingGame => "Eliminando el juego preparado...",
        DataRebuilding => "Reconstruyendo el juego preparado...",
        DataRecovering => "Deshaciendo la restauración interrumpida...",
        DiagnosticsBody => {
            "Lo que Forge sabe de esta instalación. Cópialo en un informe de errores; los nombres de usuario de las \
             rutas se eliminan."
        }
        StateLabel { state } => return Some(format!("Estado de la instalación: {state}")),
        Copied => "Copiado al portapapeles.",
        LastGameRun { started, seconds, meaning } => {
            return Some(format!("Última ejecución del juego: inicio {started}, duración {seconds} s, {meaning}."))
        }
        GameRunning => "El juego se está ejecutando.",
        NoGameRun => "Este Forge aún no ha registrado ninguna ejecución del juego.",
        CollectingDiagnostics => "Recopilando el diagnóstico...",
        DiagnosticsFailed { error } => return Some(format!("No se pudo recopilar el diagnóstico: {error}")),
        GameLogFolder => "Registro del juego",
        BundleContents => {
            "«Guardar paquete de soporte...» escribe un zip con estos archivos, sin nombres de usuario ni carpetas \
             de instalación. Nunca contiene partidas guardadas, estados guardados, datos del juego ni archivos de \
             volcado de fallos."
        }
        BundleOmitted { name, reason } => return Some(format!("Omitido: {name} ({reason})")),
        ButtonSaveBundle => "Guardar paquete de soporte...",
        SaveBundleTitle => "Guardar el paquete de soporte",
        FilterZip => "Archivo zip",
        BundleSaved { path } => return Some(format!("Paquete de soporte guardado: {path}")),
        BundleFailed { error } => return Some(format!("No se pudo guardar el paquete de soporte: {error}")),
        SteamDeckHeading => "Steam Deck y mandos",
        SteamDeckBody => {
            "Forge funciona sin ratón: Tab y Mayús+Tab pasan de un control a otro, Intro activa el resaltado y Esc \
             cierra un diálogo. En Steam Deck, prepara el juego en el modo Escritorio y después añade el AppImage a \
             Steam como juego que no es de Steam (sin Proton). En su configuración del mando elige la plantilla de \
             Steam Input «Gamepad with Mouse Trackpad» (mando con trackpad como ratón): los joysticks y los botones \
             siguen siendo un mando para el juego, y el trackpad derecho mueve el puntero y hace clic, así que Forge \
             también se puede usar en el modo Juego."
        }
        Version { version } => return Some(format!("Versión {version}")),
        AboutBody => {
            "TriAevum Forge prepara TriAevum \u{2014} Echoes of Time a partir de tu propia ROM descifrada. Es \
             software libre bajo la GNU General Public License, versión 3 o posterior, y no incluye datos del \
             juego."
        }
        SupportedRevisions => "Revisiones del juego compatibles",
        NoSupportedRevisions => "Este paquete no indica ninguna revisión del juego compatible.",
        ProjectPage => "Página del proyecto",
        ForgeLanguage => "Idioma de Forge",
        ForgeLanguageNote => {
            "Los idiomas de tu juego. Los textos aún no traducidos se muestran en inglés. Tu elección se \
             recuerda."
        }
        ForgeLanguageNotSaved { error } => return Some(format!("No se ha podido guardar el idioma de Forge: {error}")),
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
                "Error en la interfaz de TriAevum Forge: {error}\n\nLa herramienta de línea de comandos \
                 triaevum-forge aún puede preparar títulos."
            ))
        }
        _ => return None,
    };
    Some(fixed.to_string())
}
