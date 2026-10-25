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

use forge_assets::interior_sun::{prepare_interior_sun, InteriorSunService, NativeRun, PrebuildCommand};
use forge_assets::precomputed_tables::title_tables;
use forge_assets::scene_lights::prepare_scene_lights;
use forge_assets::topscreen::{prepare_topscreen_assets, topscreen_link_animations, topscreen_ui_atlas_hd};
use forge_assets::water::prepare_water_textures;
use forge_base::{CancelToken, ForgeError, Result};
use forge_catalog::family::{self, InputPaths};
use forge_catalog::precompiled::{checked_file, load_catalog, runtime_record, select_title, MODEL};
use forge_catalog::recipes::load_recipe_options;
use forge_catalog::{adapt_extracted_inputs, default_recipes, match_extracted_recipe, InputFile, TitleInputs};
use forge_mods::{prepare_romfs_mods, prepare_ui_texture_pack, romfs_service_view, ui_atlas_hd};
use forge_mods::{Catalog, PrepareContext, SystemBsdtar, UiTexturePack};
use forge_native::{
    adopt_existing_cache, installation_cache_directory, prepare_device_pipelines, prepare_renderer_shader_cache,
    prepare_shader_seed, query_product, InteriorSunRequest, NativeServices,
};
use forge_prepare::paths::{expanduser, path_string, resolve};
use forge_prepare::{
    activate_prepared_title, check_title_path_length, load_prepared_content, package_private_module, prepare_content,
    verify_sources, HashCache, SourcePaths,
};
use forge_rom::{ExtractedFile, ExtractedTitleInputs};
use serde_json::{json, Map, Value};

use crate::layout::{self, for_package, installation_state, installed_runtime_path, HostOs};
use crate::publish::{publish_private_runtime, PublishRequest, RuntimeAssets};
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
        if expanduser(&path).is_dir() {
            Self::ExtractedDirectory(path)
        } else {
            Self::Rom(path)
        }
    }
}

pub struct InstallRequest {
    pub source: InstallSource,
    /// The private data folder; the package's `data` folder by default.
    pub data_root: Option<PathBuf>,
    /// The package: catalog, recipes, runtime, plugins and resources.
    pub package_root: PathBuf,
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

/// `forge_gui.install_private_title`: imports the user's ROM (or extracted
/// data) and installs the matching precompiled title. Returns Python's
/// `{status, recipe, prepared, build, adaptation, install_wall_seconds}`.
pub fn install_private_title(
    request: InstallRequest,
    services: &dyn NativeServices,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    let started = Instant::now();
    let package_root = resolve(&expanduser(&request.package_root))?;
    let data_root = data_root(request.data_root.as_deref(), &package_root)?;
    std::fs::create_dir_all(&data_root)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", data_root.display()), error))?;
    let output_root = data_root.join("titles");
    let staging = data_root.join(staging_name());
    let mut stage = "preflight";
    let outcome = (|| -> Result<(Value, Value, Value, Value)> {
        report(stage, "Checking the playable runtime and precompiled title catalog...");
        let runtime = runtime_path(&package_root)?;
        query_product(services, &runtime, "", None)?;
        let recipes = recipes_path(&package_root);
        let ids: Vec<String> = load_recipe_options(&recipes)?.into_iter().map(|option| option.id).collect();
        let ids: Vec<&str> = ids.iter().map(String::as_str).collect();
        check_title_path_length(&output_root, &ids, cfg!(windows))?;
        let root = package_root.clone();
        let catalog = load_catalog(&root)?;
        let target = forge_catalog::host_platform()?;

        stage = "extract";
        report(stage, "Extracting native title data from the decrypted ROM...");
        let mut extracted = extract(request.source, &staging, cancel)?;

        stage = "verify";
        report(stage, "Identifying and verifying the supported game revision...");
        let recipe = match_extracted_recipe(&title_inputs(&extracted), &recipes, cancel)?;
        select_title(&root, &recipe, Some(&catalog), &target, cancel)?;
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
        let built = install_precompiled_title(
            &PrecompiledRequest {
                prepared_directory: &prepared_directory,
                root: &root,
                recipe: &recipe,
                data_root: &data_root,
                runtime_plugin: &default_runtime_plugin_path(&package_root)?,
                launch_profile: &default_runtime_launch_profile_path(&package_root)?,
                active_title_state: &data_root.join("active-title.json"),
            },
            services,
            report,
            cancel,
        )?;
        forge_native::language::install(&data_root, &Value::Object(languages))?;
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

/// `install_precompiled_title`: reverifies the catalog and the prepared
/// inputs, prepares the assets and shader caches, then publishes the TAM,
/// the runtime and the active title in one activation transaction.
pub fn install_precompiled_title(
    request: &PrecompiledRequest<'_>,
    services: &dyn NativeServices,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Value> {
    let PrecompiledRequest { prepared_directory, root, recipe, data_root, .. } = *request;
    let catalog = load_catalog(root)?;
    let item = select_title(root, recipe, Some(&catalog), &forge_catalog::host_platform()?, cancel)?;
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
    let plugin = checked_file(root, &item["plugin"], cancel)?;
    let native_module = checked_file(root, &catalog["native_module"], cancel)?;
    let item_value = Value::Object(item.clone());
    let tables = title_tables(&item_value, &mut |record| checked_file(root, record, cancel))?;
    let recipe_value = Value::Object(recipe.clone());
    let romfs = input("romfs")?;

    // The HD UI texture pack, when the user has obtained it: the runtime loads
    // it beneath the user's own texture pack, and TopScreen takes its HD atlas.
    let ui_textures =
        prepare_ui_texture_pack(root, data_root, &recipe_value, &UiTexturePack::vanilla_ui_hud(), report, cancel)?;
    let texture_pack = prepare_topscreen_assets(
        root,
        data_root,
        &recipe_value,
        &romfs.path,
        ui_atlas_hd(ui_textures.as_deref()).as_deref(),
        report,
        cancel,
    )?;
    let water_textures = prepare_water_textures(data_root, &recipe_value, &romfs.path, report, cancel)?;
    let scene_lights =
        prepare_scene_lights(data_root, &recipe_value, &romfs.path, tables.scene_lights.as_deref(), report, cancel)?;
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
    let romfs_overlay = prepare_romfs_mods(&context, &mut || romfs_service_view(&manifest), report)?;
    let shader_pack = prepare_shader_seed(services, root, data_root, &item, report)?;
    let renderer_cache = installation_cache_directory(data_root)?;
    adopt_existing_cache(&renderer_cache, None, report);
    let bridge = InteriorSunBridge {
        services,
        root,
        runtime_record: runtime_record(root, &catalog)?,
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
    )?;
    let renderer_preparation =
        prepare_renderer_shader_cache(services, root, data_root, &item, &renderer_cache, report)?;
    let pipeline_preparation = prepare_device_pipelines(
        services,
        root,
        data_root,
        &item,
        shader_pack.as_deref(),
        Some(&renderer_cache),
        report,
        cancel,
    )?;
    cancel.check()?;

    let assets = RuntimeAssets {
        topscreen_link_animations: topscreen_link_animations(texture_pack.as_deref()),
        topscreen_ui_atlas_hd: topscreen_ui_atlas_hd(texture_pack.as_deref()),
        topscreen_texture_pack: texture_pack,
        ui_texture_pack: ui_textures,
        water_texture_catalog: water_textures,
        scene_light_catalog: scene_lights,
        romfs_overlay,
        pica_shader_pack: shader_pack,
        renderer_cache_directory: Some(renderer_cache),
    };
    let runtime_plugin = resolve(request.runtime_plugin)?;
    let installation = runtime_plugin.parent().map(Path::to_path_buf).unwrap_or_default();
    let targets = [
        request.runtime_plugin.to_path_buf(),
        request.launch_profile.to_path_buf(),
        request.active_title_state.to_path_buf(),
        prepared.directory.join("forge-state.json"),
        data_root.join("config").join("TriAevum.json"),
        data_root.join("config").join("topscreen_ui.json"),
    ];
    let target = item["target"].as_str().unwrap_or_default();
    let translator = item["translator_identity_sha256"].as_str().unwrap_or_default();
    let (packaged, runtime, activation) = activation_transaction(&installation, &targets, HostOs::current(), || {
        let packaged = package_private_module(
            &prepared.directory,
            &native_module,
            Some(&plugin),
            target,
            translator,
            None,
            cancel,
        )?;
        let runtime = publish_private_runtime(
            &PublishRequest {
                prepared_directory: &prepared.directory,
                plugin: &plugin,
                runtime_plugin: request.runtime_plugin,
                launch_profile: request.launch_profile,
                data_root,
                package_root: Some(root),
                assets: &assets,
            },
            services,
            cancel,
        )?;
        let activation = activate_prepared_title(&prepared.directory, request.active_title_state, cancel)?;
        Ok((packaged.as_json()?, runtime, activation))
    })?;
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
