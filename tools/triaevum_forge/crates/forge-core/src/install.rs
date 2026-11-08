// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The public import: `forge_gui.install_private_title` and
//! `precompiled_titles.install_precompiled_title` (FORGE_SPEC.md 2.4), with
//! the Python Forge's stage names, messages, order and results.
//!
//! The expensive preparation runs outside the activation lock; the
//! transaction covers exactly Python's six publication targets. Language
//! settings are written after the commit, as in Python (a failure there
//! reports an error although the title is active: spec 2.4 step 8).

use std::fs::File;
use std::hash::{BuildHasher, Hasher};
use std::path::{Path, PathBuf};
use std::time::Instant;

use forge_assets::interior_sun::{InteriorSunService, NativeRun, PrebuildCommand, prepare_interior_sun};
use forge_assets::precomputed_tables::title_tables;
use forge_assets::scene_lights::prepare_scene_lights;
use forge_assets::topscreen::{prepare_topscreen_assets, topscreen_link_animations, topscreen_ui_atlas_hd};
use forge_assets::water::prepare_water_textures;
use forge_base::fsutil::atomic_write_json;
use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Operation, Reason, Result};
use forge_catalog::ReleasePlatform;
use forge_catalog::family::{self, InputPaths};
use forge_catalog::precompiled::{
    MODEL, artifact_path, checked_file, load_catalog, runtime_record, select_title, set_native_library_directory,
};
use forge_catalog::recipes::load_recipe_options;
use forge_catalog::{InputFile, TitleInputs, adapt_extracted_inputs, default_recipes, match_extracted_recipe};
use forge_mods::{Catalog, PrepareContext, SystemBsdtar, UiTexturePack};
use forge_mods::{prepare_romfs_mods, prepare_ui_texture_pack, romfs_service_view, ui_atlas_hd};
use forge_native::{
    InteriorSunRequest, NativeServices, adopt_existing_cache, installation_cache_directory, prepare_device_pipelines,
    prepare_renderer_shader_cache, prepare_shader_seed, query_product_at,
};
use forge_prepare::paths::{expanduser, path_string, resolve};
use forge_prepare::{
    HashCache, SourcePaths, activate_prepared_title, check_title_path_length, load_prepared_content,
    package_private_module, prepare_content, verify_sources,
};
use forge_rom::{ExtractedFile, ExtractedTitleInputs};
use serde_json::{Map, Value, json};

use crate::layout::{self, HostOs, for_package, installation_state, installed_runtime_path};
use crate::publish::{AndroidRuntime, HudLayout, PublishRequest, RuntimeAssets, publish_private_runtime};
use crate::transaction::activation_transaction;

/// Progress callback `(stage, message)`, Python's `report`.
pub type Report<'a> = &'a mut dyn FnMut(&str, &str);

/// What the user selected.
pub enum InstallSource {
    /// A decrypted `.3ds`/`.cci` file.
    Rom(PathBuf),
    /// A folder with `code.bin`, `exheader.bin` and `romfs.bin`.
    ExtractedDirectory(PathBuf),
    /// An already opened seekable ROM (an Android document descriptor).
    RomFile(File),
}

impl InstallSource {
    /// `InstallRequest.rom` as the Python worker received it: a folder is
    /// extracted data, anything else a ROM file.
    pub fn from_path(path: PathBuf) -> Self {
        if expanduser(&path).is_dir() { Self::ExtractedDirectory(path) } else { Self::Rom(path) }
    }
}

pub struct InstallRequest {
    pub source: InstallSource,
    /// The private data folder; the package's `data` folder by default
    /// (Android: `data` in the profile folder).
    pub data_root: Option<PathBuf>,
    /// The package: catalog, recipes, runtime, plugins and resources.
    pub package_root: PathBuf,
    pub target: InstallTarget,
    /// Rust addition: the game language chosen before installing; it is
    /// selected when the ROM offers it (`language::install_preferring`).
    pub game_language: Option<String>,
}

/// Where the installed title runs.
#[derive(Clone)]
pub enum InstallTarget {
    /// The desktop package this process belongs to.
    Package,
    /// The Android application.
    Android(AndroidTarget),
}

/// The Android installation (`FORGE_SPEC.md` 4.4). The package folder holds
/// the APK's data files (catalog, recipes, resources, shader corpus); the
/// libraries stay in `native_library_dir`, where Android installed them. The
/// launch profile, the title file and the private data are in `profile_root`,
/// the folder the game activity reads (its external files folder).
#[derive(Clone)]
pub struct AndroidTarget {
    pub native_library_dir: PathBuf,
    pub profile_root: PathBuf,
}

/// The Android launch profile, read by the game activity.
pub const ANDROID_LAUNCH_PROFILE: &str = "TriAevum.android.launch.json";
/// The title library the activity passes as `--title-plugin`.
pub const ANDROID_TITLE_FILE: &str = "TriAevum.android.title.json";

impl AndroidTarget {
    pub fn launch_profile(&self) -> PathBuf {
        self.profile_root.join(ANDROID_LAUNCH_PROFILE)
    }

    pub fn title_file(&self) -> PathBuf {
        self.profile_root.join(ANDROID_TITLE_FILE)
    }

    /// The private data folder when the request names none.
    pub fn data_root(&self) -> PathBuf {
        self.profile_root.join("data")
    }
}

impl InstallTarget {
    /// The release platform the catalog must bind.
    pub fn platform(&self) -> Result<ReleasePlatform> {
        match self {
            Self::Package => forge_catalog::host_platform(),
            Self::Android(_) => Ok(forge_catalog::ANDROID),
        }
    }
}

/// The installation of a package (`bundle_paths.installation_layout`).
pub fn package_layout(package_root: &Path) -> Result<layout::HostLayout> {
    for_package(package_root, &layout::process_env, HostOs::current())
}

/// `(request.data_root or default_gui_data_root()).expanduser().resolve()`.
pub fn data_root(request_data_root: Option<&Path>, package_root: &Path) -> Result<PathBuf> {
    match request_data_root {
        Some(path) => resolve(&expanduser(path)),
        None => resolve(&package_layout(package_root)?.data()),
    }
}

/// `forge.default_runtime_plugin_path()`.
pub fn default_runtime_plugin_path(package_root: &Path) -> Result<PathBuf> {
    Ok(package_layout(package_root)?.activation.join(layout::host_platform()?.title_module))
}

/// `forge.default_runtime_launch_profile_path()`: an installation made before
/// the profile moved to data/ keeps its root profile (the runtime prefers a
/// profile beside it, so a second one would go stale).
pub fn default_runtime_launch_profile_path(package_root: &Path) -> Result<PathBuf> {
    let activation = package_layout(package_root)?.activation;
    let legacy = activation.join("TriAevum.launch.json");
    if legacy.is_file() {
        return Ok(legacy);
    }
    Ok(installation_state(&activation, HostOs::current()).join("TriAevum.launch.json"))
}

/// `forge.DEFAULT_RECIPES`. The Rust Forge has no separate distribution
/// folder: the package's recipes are the only ones.
pub fn recipes_path(package_root: &Path) -> PathBuf {
    default_recipes(package_root, package_root)
}

/// `forge_gui.runtime_path()`.
pub fn runtime_path(package_root: &Path) -> Result<PathBuf> {
    resolve(&installed_runtime_path(package_root, &layout::host_platform()?))
}

/// The Android runtime library in the native library directory.
fn android_runtime_path(package_root: &Path) -> PathBuf {
    artifact_path(package_root, layout::ANDROID.runtime)
}

/// The resolved package and private data folder of a request; an Android
/// target first binds the application's native library folder.
pub(crate) fn installation_paths(
    package_root: &Path,
    data_root_request: Option<&Path>,
    target: &InstallTarget,
) -> Result<(PathBuf, PathBuf)> {
    let package_root = resolve(&expanduser(package_root))?;
    let data_root = match (target, data_root_request) {
        (InstallTarget::Android(android), requested) => {
            set_native_library_directory(&android.native_library_dir)?;
            match requested {
                None => resolve(&android.data_root())?,
                Some(requested) => data_root(Some(requested), &package_root)?,
            }
        }
        (InstallTarget::Package, requested) => data_root(requested, &package_root)?,
    };
    Ok((package_root, data_root))
}

/// The runtime the target runs titles with.
pub(crate) fn target_runtime(package_root: &Path, target: &InstallTarget) -> Result<PathBuf> {
    match target {
        InstallTarget::Package => runtime_path(package_root),
        InstallTarget::Android(_) => Ok(android_runtime_path(package_root)),
    }
}

/// Names the install step an error came from (`stage`, `operation`); a
/// failed step without a more specific cause is `step_failed`.
fn step(stage: &'static str, operation: Operation) -> impl Fn(ForgeError) -> ForgeError {
    move |error| {
        let failed = error.kind == ErrorKind::PreparationFailed;
        let error = error.at_stage(stage).during(operation);
        if failed { error.because(Reason::StepFailed) } else { error }
    }
}

/// A unique `.rom-import-<32 hex>` staging folder name.
fn staging_name() -> String {
    let mut value = 0u128;
    for salt in 0..2u64 {
        let mut hasher = std::collections::hash_map::RandomState::new().build_hasher();
        hasher.write_u64(salt);
        hasher.write_u128(
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos(),
        );
        hasher.write_u32(std::process::id());
        value = (value << 64) | u128::from(hasher.finish());
    }
    format!(".rom-import-{value:032x}")
}

fn title_inputs(extracted: &ExtractedTitleInputs) -> TitleInputs {
    let file =
        |item: &ExtractedFile| InputFile { path: item.path.clone(), bytes: item.bytes, sha256: item.sha256_hex() };
    TitleInputs {
        program_id: extracted.program_id,
        code: file(&extracted.code),
        exheader: file(&extracted.exheader),
        romfs: file(&extracted.romfs),
    }
}

fn digest_bytes(text: &str) -> Result<[u8; 32]> {
    let mut digest = [0u8; 32];
    if text.len() != 64 {
        return Err(ForgeError::internal(format!("invalid SHA-256 digest: {text}")));
    }
    for (index, byte) in digest.iter_mut().enumerate() {
        *byte = u8::from_str_radix(&text[index * 2..index * 2 + 2], 16)
            .map_err(|_| ForgeError::internal(format!("invalid SHA-256 digest: {text}")))?;
    }
    Ok(digest)
}

/// Adapted inputs keep the container fields of the extraction they replace.
fn extracted_inputs(original: &ExtractedTitleInputs, adapted: &TitleInputs) -> Result<ExtractedTitleInputs> {
    let file = |item: &InputFile| -> Result<ExtractedFile> {
        Ok(ExtractedFile { path: item.path.clone(), bytes: item.bytes, sha256: digest_bytes(&item.sha256)? })
    };
    Ok(ExtractedTitleInputs {
        container_kind: original.container_kind,
        partition_index: original.partition_index,
        program_id: adapted.program_id,
        code: file(&adapted.code)?,
        exheader: file(&adapted.exheader)?,
        romfs: file(&adapted.romfs)?,
    })
}

fn extract(source: InstallSource, staging: &Path, cancel: &CancelToken) -> Result<ExtractedTitleInputs> {
    match source {
        InstallSource::ExtractedDirectory(path) => forge_rom::stage_directory(&path, staging, cancel),
        InstallSource::Rom(path) => forge_rom::extract_decrypted_rom(&path, staging, true, cancel),
        InstallSource::RomFile(mut file) => {
            let size = file.metadata().map_err(|error| ForgeError::io("Cannot inspect the selected ROM", error))?.len();
            forge_rom::extract_decrypted_rom_from(&mut file, size, staging, cancel)
        }
    }
}

/// Choices of an installation beyond Python's request (Rust additions).
#[derive(Clone, Copy, Debug, Default)]
pub struct InstallOptions {
    /// What happens to an existing TopScreen layout (decision 4).
    pub hud_layout: HudLayout,
}

/// `forge_gui.install_private_title`: imports the user's ROM (or extracted
/// data) and installs the matching precompiled title. Returns Python's
/// `{status, recipe, prepared, build, adaptation, install_wall_seconds}`.
pub fn install_private_title(
    request: InstallRequest,
    services: &dyn NativeServices,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    install_private_title_with(request, InstallOptions::default(), services, report, cancel)
}

/// [`install_private_title`] with the Rust additions' choices.
pub fn install_private_title_with(
    request: InstallRequest,
    options: InstallOptions,
    services: &dyn NativeServices,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    let started = Instant::now();
    let (package_root, data_root) =
        installation_paths(&request.package_root, request.data_root.as_deref(), &request.target)?;
    let android = match &request.target {
        InstallTarget::Package => None,
        InstallTarget::Android(android) => Some(android),
    };
    let game_language = request.game_language;
    // The update gate (UX plan WP-04B): no install while an update runs or
    // an interrupted one is pending.
    let activation = match android {
        None => package_layout(&package_root)?.activation,
        Some(android) => resolve(&android.profile_root)?,
    };
    let _gate = crate::update_gate::enter(&activation, HostOs::current(), Operation::Install)?;
    std::fs::create_dir_all(&data_root)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", data_root.display()), error))?;
    let output_root = data_root.join("titles");
    let staging = data_root.join(staging_name());
    let mut stage = "preflight";
    let outcome = (|| -> Result<(Value, Value, Value, Value)> {
        report(stage, "Checking the playable runtime and precompiled title catalog...");
        if android.is_none() {
            layout::ensure_runtime_closed(&package_root, &layout::host_platform()?)?;
        }
        let runtime = target_runtime(&package_root, &request.target)?;
        let diagnostic = crate::log::preflight_error_path(&data_root);
        query_product_at(services, &runtime, "", None, Some(&diagnostic))?;
        let recipes = recipes_path(&package_root);
        let ids: Vec<String> = load_recipe_options(&recipes)?.into_iter().map(|option| option.id).collect();
        let ids: Vec<&str> = ids.iter().map(String::as_str).collect();
        check_title_path_length(&output_root, &ids, cfg!(windows))?;
        let root = package_root.clone();
        let catalog = load_catalog(&root)?;
        let target = request.target.platform()?;
        let activation = match android {
            None => package_layout(&package_root)?.activation,
            Some(android) => android.profile_root.clone(),
        };
        crate::preflight::check_writable(&activation, "installation folder")?;
        crate::preflight::check_writable(&data_root, "data folder")?;
        let estimate = crate::preflight::StorageEstimate {
            game_data: crate::preflight::input_bytes(&request.source).unwrap_or(0),
            shader_corpus: crate::preflight::shader_corpus_bytes(&catalog, &root),
            margin: crate::preflight::MARGIN,
        };
        crate::preflight::check_storage(&data_root, &estimate, forge_native::system::available_space(&data_root))?;
        if let Some(warning) = crate::preflight::vulkan_loader_warning() {
            report(stage, &warning);
        }

        stage = "extract";
        report(stage, "Extracting native title data from the decrypted ROM...");
        let mut extracted = extract(request.source, &staging, cancel)?;

        stage = "verify";
        report(stage, "Identifying and verifying the supported game revision...");
        let recipe = match_extracted_recipe(&title_inputs(&extracted), &recipes, cancel)
            .map_err(|error| error.during(Operation::Verify))?;
        select_title(&root, &recipe, Some(&catalog), &target, cancel)
            .map_err(|error| error.during(Operation::PackageVerify))?;
        let mut adaptation = Value::Null;
        if !recipe.get("input_adapter").unwrap_or(&Value::Null).is_null() {
            stage = "adapt";
            report(stage, "Adapting this ROM for the existing title module (no compilation)...");
            if let Some(adapted) =
                adapt_extracted_inputs(&title_inputs(&extracted), &recipe, &root, &staging.join("normalized"), cancel)?
            {
                extracted = extracted_inputs(&extracted, &adapted.inputs)?;
                adaptation = adapted.to_value();
            }
        }
        let languages = forge_native::language::discover(services, &runtime, &extracted.romfs.path)?;
        let extracted = forge_rom::publish_extracted_inputs(&extracted, &data_root.join("sources"), cancel)?;
        let mut cache = HashCache::new(output_root.join(".hash-cache.json"), true);
        for (_, item) in extracted.by_kind() {
            cache.remember(&item.path, &item.sha256_hex())?;
        }
        let family_verifier = |recipe: &Map<String, Value>, paths: &SourcePaths<'_>| -> Result<()> {
            let paths = InputPaths { code: paths.code, exheader: paths.exheader, romfs: paths.romfs };
            family::verify(recipe, paths, "execution", cancel).map(|_| ())
        };
        let paths = SourcePaths {
            code: &extracted.code.path,
            exheader: &extracted.exheader.path,
            romfs: &extracted.romfs.path,
        };
        let (verified, verified_mod) =
            verify_sources(&recipe, &paths, &mut cache, Some(&family_verifier), None, cancel)?;

        stage = "prepare";
        report(stage, "Preparing the private local title index...");
        let prepared = prepare_content(&recipe, &verified, &output_root, verified_mod.as_ref(), None, cancel)?;
        let prepared_directory = resolve(&prepared.directory)?;

        stage = "activate";
        report(stage, "Installing the verified precompiled title...");
        let (runtime_plugin, launch_profile) = match android {
            None => (default_runtime_plugin_path(&package_root)?, default_runtime_launch_profile_path(&package_root)?),
            Some(android) => (android.title_file(), android.launch_profile()),
        };
        let android_runtime = android.map(|_| (runtime.clone(), runtime_plugin.clone()));
        let android_runtime =
            android_runtime.as_ref().map(|(runtime, title_file)| AndroidRuntime { runtime, title_file });
        let built = install_precompiled_title(
            &PrecompiledRequest {
                prepared_directory: &prepared_directory,
                root: &root,
                recipe: &recipe,
                data_root: &data_root,
                runtime_plugin: &runtime_plugin,
                launch_profile: &launch_profile,
                active_title_state: &data_root.join("active-title.json"),
                android: android_runtime.as_ref(),
                hud_layout: options.hud_layout,
            },
            services,
            report,
            cancel,
        )?;
        // The activation is committed: a failure here leaves a playable title.
        forge_native::language::install_preferring(&data_root, &Value::Object(languages), game_language.as_deref())
            .map_err(|error| error.during(Operation::SettingsSave).because(Reason::AfterActivationCommitted))?;
        Ok((recipe.get("id").cloned().unwrap_or(Value::Null), prepared.as_json()?, built, adaptation))
    })();
    if staging.exists() {
        let _ = std::fs::remove_dir_all(&staging);
    }
    let (recipe, prepared, built, adaptation) = outcome.map_err(|error| error.at_stage(stage))?;

    report("ready", "The title is ready to play.");
    Ok(json!({
        "status": "ready",
        "recipe": recipe,
        "prepared": prepared,
        "build": built,
        "adaptation": adaptation,
        "install_wall_seconds": started.elapsed().as_secs_f64(),
    }))
}

/// `install_precompiled_title`'s keyword arguments.
pub struct PrecompiledRequest<'a> {
    pub prepared_directory: &'a Path,
    /// The package root (catalog and artifacts).
    pub root: &'a Path,
    pub recipe: &'a Map<String, Value>,
    pub data_root: &'a Path,
    pub runtime_plugin: &'a Path,
    pub launch_profile: &'a Path,
    pub active_title_state: &'a Path,
    /// Android: the runtime library and the title file (`runtime_plugin`).
    pub android: Option<&'a AndroidRuntime<'a>>,
    /// What happens to an existing TopScreen layout (decision 4).
    pub hud_layout: HudLayout,
}

/// The catalog-verified runtime as the interior-sun job's executor.
struct InteriorSunBridge<'a> {
    services: &'a dyn NativeServices,
    root: &'a Path,
    runtime_record: Value,
    runtime: std::cell::OnceCell<PathBuf>,
}

impl InteriorSunBridge<'_> {
    fn runtime(&self) -> Result<&PathBuf> {
        if let Some(runtime) = self.runtime.get() {
            return Ok(runtime);
        }
        let runtime = checked_file(self.root, &self.runtime_record, &CancelToken::new())?;
        Ok(self.runtime.get_or_init(|| runtime))
    }
}

impl InteriorSunService for InteriorSunBridge<'_> {
    fn runtime_sha256(&self) -> Result<String> {
        self.runtime()?;
        // checked_file matched the file's digest to the record exactly.
        Ok(self.runtime_record["sha256"].as_str().unwrap_or_default().to_string())
    }

    fn prebuild(&self, command: &PrebuildCommand, cancel: &CancelToken) -> Result<NativeRun> {
        let request = InteriorSunRequest {
            runtime: self.runtime()?.clone(),
            root: self.root.to_path_buf(),
            romfs: command.romfs.clone(),
            cache_directory: command.cache_directory.clone(),
            manifest: command.manifest.clone(),
            mask_table: command.mask_table.clone(),
        };
        let output = self.services.prebuild_interior_sun(&request, cancel)?;
        // Only zero means success; a wide nonzero status stays nonzero.
        let status = if output.status == 0 { 0 } else { output.status.clamp(i64::from(i32::MIN), -1) as i32 };
        Ok(NativeRun { status, stdout: output.stdout, stderr: output.stderr })
    }
}

fn optional(value: Option<Map<String, Value>>) -> Value {
    value.map_or(Value::Null, Value::Object)
}

/// Android has no module container: the title library runs where Android
/// installed it. forge-state.json records that library as the title's ready
/// module, which activation requires.
fn record_library_module(
    prepared_directory: &Path,
    library: &Path,
    target: &str,
    translator: &str,
    cancel: &CancelToken,
) -> Result<Value> {
    let mut prepared = load_prepared_content(prepared_directory, &["code"], cancel)?;
    let source_identity = prepared.input("code").expect("code input was required").sha256.clone();
    let (bytes, digest) = sha256_file(library, cancel)?;
    let digest = hex(&digest);
    let name = library.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
    prepared.state.insert(
        "module".into(),
        json!({
            "status": "ready",
            "container": Value::Null,
            "library": name,
            "bytes": bytes,
            "sha256": digest,
            "source_identity_sha256": source_identity,
            "translator_identity_sha256": translator,
            "title_aot_image_sha256": digest,
            "target_triple": target,
        }),
    );
    atomic_write_json(&prepared.directory.join("forge-state.json"), &Value::Object(prepared.state))?;
    Ok(json!({
        "status": "library",
        "directory": path_string(&prepared.directory)?,
        "module": path_string(library)?,
        "module_sha256": digest,
    }))
}

/// `install_precompiled_title`: reverifies the catalog and the prepared
/// inputs, prepares the assets and shader caches, then publishes the TAM,
/// the runtime and the active title in one activation transaction.
pub fn install_precompiled_title(
    request: &PrecompiledRequest<'_>,
    services: &dyn NativeServices,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    let PrecompiledRequest { prepared_directory, root, recipe, data_root, android, .. } = *request;
    let verify = |error: ForgeError| error.during(Operation::PackageVerify);
    let catalog = load_catalog(root).map_err(verify)?;
    let platform = if android.is_some() { forge_catalog::ANDROID } else { forge_catalog::host_platform()? };
    let item = select_title(root, recipe, Some(&catalog), &platform, cancel).map_err(verify)?;
    let prepared = load_prepared_content(prepared_directory, &["code", "exheader", "romfs"], cancel)?;
    if !forge_prepare::pyvalue::py_eq(prepared.index.get("recipe").unwrap_or(&Value::Null), &item["recipe"]) {
        return Err(ForgeError::invalid("Prepared title differs from the precompiled revision"));
    }
    let input = |kind: &str| prepared.input(kind).ok_or_else(|| ForgeError::internal(format!("missing {kind} input")));
    let mismatch = ["code", "exheader", "romfs"].into_iter().map(|kind| -> Result<bool> {
        let actual = input(kind)?;
        let expected = &item["inputs"][kind];
        Ok(expected["sha256"].as_str() != Some(actual.sha256.as_str())
            || expected["bytes"].as_u64() != Some(actual.bytes))
    });
    if mismatch.collect::<Result<Vec<bool>>>()?.contains(&true) {
        let paths =
            InputPaths { code: &input("code")?.path, exheader: &input("exheader")?.path, romfs: &input("romfs")?.path };
        family::verify(recipe, paths, "execution", cancel)?;
    }
    let plugin = checked_file(root, &item["plugin"], cancel).map_err(verify)?;
    // Android binds no native module (load_catalog checked the binding).
    let native_module = match android {
        None => Some(checked_file(root, &catalog["native_module"], cancel).map_err(verify)?),
        Some(_) => None,
    };
    let item_value = Value::Object(item.clone());
    let tables = title_tables(&item_value, &mut |record| checked_file(root, record, cancel)).map_err(verify)?;
    let recipe_value = Value::Object(recipe.clone());
    let romfs = input("romfs")?;

    // The HD UI texture pack, when the user has obtained it: the runtime loads
    // it beneath the user's own texture pack, and TopScreen takes its HD atlas.
    let assets = |error: ForgeError| error.at_stage("assets");
    let required_assets = step("assets", Operation::RequiredAssets);
    // The user's choice (UX plan WP-07, decision 5): a stored pack is applied
    // only while the HD interface pack is on.
    let ui_textures = if crate::enhancements::hd_pack_enabled(data_root) {
        prepare_ui_texture_pack(root, data_root, &recipe_value, &UiTexturePack::vanilla_ui_hud(), report, cancel)
            .map_err(assets)?
    } else {
        report("assets", "The HD interface pack is turned off: the game uses its native interface textures.");
        None
    };
    let texture_pack = prepare_topscreen_assets(
        root,
        data_root,
        &recipe_value,
        &romfs.path,
        ui_atlas_hd(ui_textures.as_deref()).as_deref(),
        report,
        cancel,
    )
    .map_err(&required_assets)?;
    let water_textures =
        prepare_water_textures(data_root, &recipe_value, &romfs.path, report, cancel).map_err(&required_assets)?;
    let scene_lights =
        prepare_scene_lights(data_root, &recipe_value, &romfs.path, tables.scene_lights.as_deref(), report, cancel)
            .map_err(&required_assets)?;
    // The user's enabled RomFS mods (none by default): a local overlay over the
    // verified RomFS, never a change to the prepared inputs.
    let manifest = prepared.directory.join("process-manifest.json");
    let mods = Catalog::builtin();
    let extractor = SystemBsdtar::default();
    let context = PrepareContext {
        catalog: &mods,
        root,
        data_root,
        recipe: &recipe_value,
        romfs: &romfs.path,
        romfs_sha256: &romfs.sha256,
        extractor: &extractor,
        cancel,
    };
    let romfs_overlay = prepare_romfs_mods(&context, &mut || romfs_service_view(&manifest), report).map_err(assets)?;
    let helper = step("shaders", Operation::NativeHelper);
    let shader_pack = prepare_shader_seed(services, root, data_root, &item, report).map_err(&helper)?;
    let renderer_cache = installation_cache_directory(data_root).map_err(&helper)?;
    adopt_existing_cache(&renderer_cache, None, report);
    let bridge = InteriorSunBridge {
        services,
        root,
        runtime_record: runtime_record(root, &catalog).map_err(verify)?,
        runtime: std::cell::OnceCell::new(),
    };
    let interior_sun = prepare_interior_sun(
        &bridge,
        data_root,
        &romfs.path,
        &romfs.sha256,
        &renderer_cache,
        tables.interior_sun_masks.as_deref(),
        report,
        cancel,
    )
    .map_err(step("shaders", Operation::InteriorSun))?;
    let renderer_preparation =
        prepare_renderer_shader_cache(services, root, data_root, &item, &renderer_cache, report).map_err(&helper)?;
    let pipeline_preparation = prepare_device_pipelines(
        services,
        root,
        data_root,
        &item,
        shader_pack.as_deref(),
        Some(&renderer_cache),
        report,
        cancel,
    )
    .map_err(&helper)?;
    cancel.check()?;

    let title_ui = forge_assets::mm3d_ui::prepare(
        root,
        data_root,
        &romfs.path,
        &input("code")?.sha256,
        &romfs.sha256,
        report,
        cancel,
    )
    .map_err(&required_assets)?;
    let assets = RuntimeAssets {
        title_romfs_layer: title_ui.as_ref().map(|ui| ui.layer.clone()),
        title_message_edits: title_ui.map(|ui| ui.messages),
        topscreen_link_animations: topscreen_link_animations(texture_pack.as_deref()),
        topscreen_ui_atlas_hd: topscreen_ui_atlas_hd(texture_pack.as_deref()),
        topscreen_texture_pack: texture_pack,
        ui_texture_pack: ui_textures,
        water_texture_catalog: water_textures,
        scene_light_catalog: scene_lights,
        romfs_overlay,
        pica_shader_pack: shader_pack,
        title_program_pack: item.get("title_program_pack")
            .map(|record| checked_file(root, record, cancel)).transpose().map_err(verify)?,
        renderer_cache_directory: Some(renderer_cache),
    };
    let runtime_plugin = resolve(request.runtime_plugin)?;
    let installation = runtime_plugin.parent().map(Path::to_path_buf).unwrap_or_default();
    let targets = [
        request.runtime_plugin.to_path_buf(),
        request.launch_profile.to_path_buf(),
        request.active_title_state.to_path_buf(),
        prepared.directory.join("forge-state.json"),
        data_root.join("config").join(if android.is_some() { crate::publish::ANDROID_CONFIG } else { "TriAevum.json" }),
        data_root.join("config").join("topscreen_ui.json"),
    ];
    let target = item["target"].as_str().unwrap_or_default();
    let translator = item["translator_identity_sha256"].as_str().unwrap_or_default();
    let (packaged, runtime, activation) = activation_transaction(&installation, &targets, HostOs::current(), || {
        let packaged = match &native_module {
            Some(native_module) => package_private_module(
                &prepared.directory,
                native_module,
                Some(&plugin),
                target,
                translator,
                None,
                cancel,
            )?
            .as_json()?,
            None => record_library_module(&prepared.directory, &plugin, target, translator, cancel)?,
        };
        let runtime = publish_private_runtime(
            &PublishRequest {
                prepared_directory: &prepared.directory,
                plugin: &plugin,
                runtime_plugin: request.runtime_plugin,
                launch_profile: request.launch_profile,
                data_root,
                package_root: Some(root),
                assets: &assets,
                android,
                hud_layout: request.hud_layout,
            },
            services,
            cancel,
        )?;
        let activation = activate_prepared_title(&prepared.directory, request.active_title_state, cancel)?;
        Ok((packaged, runtime, activation))
    })
    .map_err(|error| error.during(Operation::Activate))?;
    // Committed: the package version beside the receipts it belongs to.
    crate::refresh::record_installed_package(root, &prepared.directory);
    Ok(json!({
        "status": "ready",
        "install_model": MODEL,
        "objects_compiled": 0,
        "interior_sun": Value::Object(interior_sun),
        "renderer_shader_preparation": optional(renderer_preparation),
        "device_pipeline_preparation": optional(pipeline_preparation),
        "package": packaged,
        "runtime": runtime,
        "active_title": path_string(&activation.active_title)?,
    }))
}
