// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `forge.publish_private_runtime`: the immutable title plugin generation,
//! the launch profile, the user's configuration files and the runtime
//! receipt in forge-state.json, with the Python Forge's bytes.
//!
//! Each file is written atomically; grouping them into one recoverable
//! publication is the caller's activation transaction.

use std::path::{Path, PathBuf};

use forge_base::fsutil::{self, atomic_write_json, file_error, load_json_object};
use forge_base::hashing::{hex, sha256_file};
use forge_base::{legacy_json, CancelToken, ErrorKind, ForgeError, Result};
use forge_native::{ensure_runtime_config, query_product_at, NativeServices};
use forge_prepare::paths::{expanduser, path_string, resolve};
use forge_prepare::pyvalue::{objects_eq, py_eq};
use forge_prepare::{load_prepared_content, InstallationContext};
use serde_json::{json, Map, Value};

use crate::layout::{host_platform, place_installed_runtime};

pub const LAUNCH_PROFILE_FORMAT: &str = "oot3d_native_game_launch_profile_v1";
pub const RUNTIME_BACKEND: &str = "generated_cpp_whole_aot_plugin_v2";

/// The standard TopScreen profile (`config/topscreen_ui.example.json`),
/// which the frozen Python Forge carried in its bundle.
const STANDARD_TOPSCREEN: &[u8] = include_bytes!("../../../../../config/topscreen_ui.example.json");

/// The TopScreen layout F1 no longer offers: "Use standard" (decision 4)
/// sets the standard profile's values. The HUD size keeps the user's choice
/// when it is one of the two F1 sizes.
pub const TOPSCREEN_STANDARD_LAYOUT_FIELDS: [&str; 13] = [
    "hud_layout",
    "hud_margin_x",
    "hud_margin_y",
    "magic_bar_y",
    "dpad_offset_x",
    "dpad_offset_y",
    "dpad_size_percent",
    "button_group_offset_x",
    "button_group_offset_y",
    "button_group_size_percent",
    "render_hud",
    "render_dpad_icons",
    "render_items_hint",
];
pub const TOPSCREEN_HUD_SIZES: [f64; 2] = [1.0, 0.7];

/// Launch-profile options whose value is a file or directory.
const PATH_OPTIONS: [&str; 21] = [
    "--title-plugin",
    "--a32-process-manifest",
    "--resource-root",
    "--config",
    "--topscreen-config",
    "--save-data",
    "--output",
    "--topscreen-texture-overrides",
    "--topscreen-link-animations",
    "--topscreen-ui-atlas-hd",
    "--ui-texture-pack",
    "--water-texture-catalog",
    "--scene-light-catalog",
    "--romfs-overlay",
    "--pica-aot-shader-pack",
    "--renderer-cache-directory",
    "--log-directory",
    "--crash-directory",
    "--savestate-directory",
    "--texture-load-directory",
    "--texture-dump-directory",
];

/// The product capability of runtimes that take the managed data tree's
/// routing options (`triaevum_data_routing.h`, UX plan WP-05C).
pub const MANAGED_DATA_PATHS: &str = "managed_data_paths";

/// The managed data folders a launch profile routes (UX plan WP-05C):
/// `(receipt field, option, folder under D)`. Mutable folders: routed, never
/// hashed. The texture folders also need the installed title's id.
const DATA_ROUTES: [(&str, &str, &[&str]); 3] = [
    ("log_directory", "--log-directory", &["logs"]),
    ("crash_directory", "--crash-directory", &["logs", "crashes"]),
    ("savestate_directory", "--savestate-directory", &["savestates"]),
];
const TEXTURE_ROUTES: [(&str, &str, &str); 2] = [
    ("texture_load_directory", "--texture-load-directory", "load"),
    ("texture_dump_directory", "--texture-dump-directory", "dump"),
];

/// Whether the queried runtime advertises [`MANAGED_DATA_PATHS`].
pub fn takes_managed_data_paths(product: &Map<String, Value>) -> bool {
    product.get("capabilities").and_then(|capabilities| capabilities.get(MANAGED_DATA_PATHS))
        == Some(&Value::Bool(true))
}

/// The installed game's program ID from its ExHeader (offset 0x200, the
/// ARM11 local system capabilities' program ID): Azahar names texture
/// folders by it, and a USA ROM keeps its own (0004000000033500) after its
/// code is adapted to the EUR title module. None when unreadable or zero.
pub fn exheader_title_id(exheader: &Path) -> Option<u64> {
    use std::io::{Read, Seek, SeekFrom};
    let mut file = std::fs::File::open(exheader).ok()?;
    file.seek(SeekFrom::Start(0x200)).ok()?;
    let mut bytes = [0u8; 8];
    file.read_exact(&mut bytes).ok()?;
    Some(u64::from_le_bytes(bytes)).filter(|id| *id != 0)
}

/// `0004000000033500`: Azahar's spelling of a title id.
pub fn title_id_text(title_id: u64) -> String {
    format!("{title_id:016X}")
}

/// The program ID of the prepared title's ExHeader input (`content.tap`).
fn prepared_title_id(prepared: &forge_prepare::content::PreparedContent) -> Option<u64> {
    let path = prepared.index.get("inputs")?.get("exheader")?.get("path")?.as_str()?;
    exheader_title_id(&forge_prepare::paths::resolve_reference(path, &prepared.directory).ok()?)
}

/// The standard TopScreen profile as a JSON object.
pub fn standard_topscreen() -> Result<Map<String, Value>> {
    legacy_json::load_object(STANDARD_TOPSCREEN)
        .map_err(|error| ForgeError::internal(format!("default TopScreen configuration is invalid: {error}")))
}

/// `standard_topscreen_layout`: the user's TopScreen file with the standard
/// profile's layout. A file of another schema is returned unchanged.
pub fn standard_topscreen_layout(
    user: &Map<String, Value>,
    standard: &Map<String, Value>,
) -> Result<Map<String, Value>> {
    let schema = |map: &Map<String, Value>| map.get("schema").cloned().unwrap_or(Value::Null);
    if !py_eq(&schema(user), &schema(standard)) {
        return Ok(user.clone());
    }
    let field = |name: &str| {
        standard.get(name).cloned().ok_or_else(|| {
            ForgeError::internal(format!("default TopScreen configuration is invalid: missing {name:?}"))
        })
    };
    let mut migrated = user.clone();
    for name in TOPSCREEN_STANDARD_LAYOUT_FIELDS {
        migrated.insert(name.to_string(), field(name)?);
    }
    // isinstance(scale, (int, float)): a bool is an int in Python.
    let scale = match user.get("hud_scale") {
        Some(Value::Number(number)) => number.as_f64(),
        Some(Value::Bool(flag)) => Some(if *flag { 1.0 } else { 0.0 }),
        _ => None,
    };
    if !scale.is_some_and(|scale| TOPSCREEN_HUD_SIZES.iter().any(|size| (scale - size).abs() < 1.0e-6)) {
        migrated.insert("hud_scale".to_string(), field("hud_scale")?);
    }
    Ok(migrated)
}

/// The optional verified files a launch profile routes to the runtime.
#[derive(Clone, Debug, Default)]
pub struct RuntimeAssets {
    pub topscreen_texture_pack: Option<PathBuf>,
    pub topscreen_link_animations: Option<PathBuf>,
    pub topscreen_ui_atlas_hd: Option<PathBuf>,
    /// A prepared UI texture pack directory (with `source.json`, `common/`).
    pub ui_texture_pack: Option<PathBuf>,
    pub water_texture_catalog: Option<PathBuf>,
    pub scene_light_catalog: Option<PathBuf>,
    pub romfs_overlay: Option<PathBuf>,
    pub pica_shader_pack: Option<PathBuf>,
    /// Mutable renderer cache: routed, never pinned by hash.
    pub renderer_cache_directory: Option<PathBuf>,
}

/// Android publication: the APK's runtime library runs the title library
/// where Android installed it. Nothing is copied; the activity passes
/// `--title-plugin` itself, with the library named in `title_file`.
pub struct AndroidRuntime<'a> {
    /// `libTriAevum.so` in the native library directory.
    pub runtime: &'a Path,
    /// `TriAevum.android.title.json`, read by the activity.
    pub title_file: &'a Path,
}

/// The Android title file's format.
pub const ANDROID_TITLE_FORMAT: &str = "triaevum_android_title_v1";
/// The Android runtime configuration (the developer staging's name, so an
/// existing configuration on the device is kept).
pub const ANDROID_CONFIG: &str = "TriAevum.android.json";

/// What publication does with an existing TopScreen file (owner decision 4).
/// A missing file is always created from the standard profile.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum HudLayout {
    /// The user's file keeps its bytes (the default).
    #[default]
    Keep,
    /// The user chose "Use standard": the standard profile's layout fields
    /// replace theirs (`standard_topscreen_layout`, the Python Forge's
    /// behaviour on every run).
    Standard,
}

impl HudLayout {
    /// `"keep"` or `"standard"` (job requests); absent means keep.
    pub fn parse(value: Option<&Value>) -> Result<Self> {
        match value {
            None | Some(Value::Null) => Ok(Self::Keep),
            Some(Value::String(text)) if text == "keep" => Ok(Self::Keep),
            Some(Value::String(text)) if text == "standard" => Ok(Self::Standard),
            Some(_) => Err(ForgeError::invalid("hud_layout must be \"keep\" or \"standard\"")),
        }
    }

    pub fn code(self) -> &'static str {
        match self {
            Self::Keep => "keep",
            Self::Standard => "standard",
        }
    }
}

/// `publish_private_runtime`'s arguments.
pub struct PublishRequest<'a> {
    pub prepared_directory: &'a Path,
    /// The verified title plugin in the package.
    pub plugin: &'a Path,
    /// The legacy root plugin path; its folder is the activation directory.
    pub runtime_plugin: &'a Path,
    pub launch_profile: &'a Path,
    pub data_root: &'a Path,
    /// The package (resources, runtime); defaults to the activation directory.
    pub package_root: Option<&'a Path>,
    pub assets: &'a RuntimeAssets,
    /// Set for an Android installation.
    pub android: Option<&'a AndroidRuntime<'a>>,
    pub hud_layout: HudLayout,
}

fn sha256_hex(path: &Path, cancel: &CancelToken) -> Result<String> {
    Ok(hex(&sha256_file(path, cancel)?.1))
}

fn is_regular_file(path: &Path) -> bool {
    path.is_file() && !std::fs::symlink_metadata(path).is_ok_and(|metadata| metadata.file_type().is_symlink())
}

fn create_dir(path: &Path) -> Result<()> {
    std::fs::create_dir_all(path).map_err(|error| ForgeError::io(format!("Cannot create {}", path.display()), error))
}

fn missing(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::PreparationFailed, message)
}

/// A required optional file, or the Python message that it vanished.
fn existing_file(path: &Option<PathBuf>, message: &str) -> Result<Option<PathBuf>> {
    match path {
        Some(path) if !path.is_file() => Err(missing(message)),
        Some(path) => Ok(Some(path.clone())),
        None => Ok(None),
    }
}

/// Copies the plugin to its immutable generation through a verified
/// temporary file, unless an identical generation already exists.
fn publish_plugin(source: &Path, destination: &Path, digest: &str, cancel: &CancelToken) -> Result<()> {
    if destination.is_file() && sha256_hex(destination, cancel)? == digest {
        return Ok(());
    }
    let name = destination.file_name().unwrap_or_default().to_string_lossy();
    let temporary = destination.with_file_name(format!(".{name}.{}.installing", std::process::id()));
    let outcome = (|| {
        std::fs::copy(source, &temporary)
            .map_err(|error| ForgeError::io(format!("Cannot copy {}", source.display()), error))?;
        if sha256_hex(&temporary, cancel)? != digest {
            return Err(ForgeError::integrity("private whole-AOT plugin copy verification failed"));
        }
        fsutil::replace_file(&temporary, destination).map_err(|error| file_error("Cannot replace", destination, error))
    })();
    let _ = fsutil::try_remove_file(&temporary);
    outcome
}

/// The standard profile on a touch screen, where the screen is the whole
/// controller: the game's own HUD size (one of the two F1 sizes), the HUD's
/// buttons drawn in the touch controls, and a camera a finger drags. Only a
/// new file starts from it; an existing file keeps the user's choices.
pub fn android_topscreen(mut standard: Map<String, Value>) -> Map<String, Value> {
    standard.insert("hud_scale".to_string(), Value::from(1.0));
    standard.insert("touch_layout".to_string(), Value::Bool(true));
    standard.insert("free_camera_enabled".to_string(), Value::Bool(true));
    standard
}

/// Creates the TopScreen file from the standard profile. An existing file
/// keeps its bytes unless the user chose the standard layout, which then
/// rewrites it only when that changes it.
fn publish_topscreen_config(path: &Path, android: bool, layout: HudLayout) -> Result<()> {
    let standard = standard_topscreen()?;
    if !path.is_file() {
        let created = if android { android_topscreen(standard) } else { standard };
        return atomic_write_json(path, &Value::Object(created));
    }
    if layout == HudLayout::Keep {
        return Ok(());
    }
    let user = load_json_object(path)?;
    let migrated = standard_topscreen_layout(&user, &standard)?;
    if !objects_eq(&migrated, &user) {
        atomic_write_json(path, &Value::Object(migrated))?;
    }
    Ok(())
}

/// `forge.publish_private_runtime`. Returns Python's result dictionary.
pub fn publish_private_runtime(
    request: &PublishRequest<'_>,
    services: &dyn NativeServices,
    cancel: &CancelToken,
) -> Result<Value> {
    let assets = request.assets;
    let mut prepared = load_prepared_content(request.prepared_directory, &[], cancel)?;
    let process_manifest = resolve(&prepared.directory.join("process-manifest.json"))?;
    if !is_regular_file(&process_manifest) {
        return Err(ForgeError::invalid("prepared title process manifest is unavailable"));
    }
    let source_plugin = resolve(&expanduser(request.plugin))?;
    if !is_regular_file(&source_plugin) {
        return Err(ForgeError::integrity(format!(
            "private whole-AOT plugin is invalid: {}",
            path_string(&source_plugin)?
        )));
    }

    let plugin_hash = sha256_hex(&source_plugin, cancel)?;
    let runtime_plugin = resolve(&expanduser(request.runtime_plugin))?;
    let installation = runtime_plugin.parent().map(Path::to_path_buf).unwrap_or_default();
    let package = resolve(&expanduser(request.package_root.unwrap_or(&installation)))?;
    let platform = if request.android.is_some() { crate::layout::ANDROID } else { host_platform()? };
    let destination = match request.android {
        None => installation.join("private-plugins").join(&plugin_hash).join(platform.title_module),
        Some(_) => source_plugin.clone(),
    };
    let profile_path = resolve(&expanduser(request.launch_profile))?;
    let private_root = resolve(&expanduser(request.data_root))?;
    // Installing a title places the runtime in the installation root.
    let runtime_executable = match request.android {
        None => place_installed_runtime(&package, &platform)?,
        Some(android) => resolve(android.runtime)?,
    };
    let diagnostic = crate::log::preflight_error_path(&private_root);
    let product = query_product_at(services, &runtime_executable, "", None, Some(&diagnostic))?;
    let profile_directory = profile_path.parent().map(Path::to_path_buf).unwrap_or_default();
    // The Android title library stays in the read-only native library folder.
    let plugin_directory = request.android.is_none().then(|| destination.parent().unwrap_or(&installation));
    for directory in plugin_directory.into_iter().chain([profile_directory.as_path(), private_root.as_path()]) {
        create_dir(directory)?;
    }
    let config_root = private_root.join("config");
    create_dir(&config_root)?;
    let topscreen_config = config_root.join("topscreen_ui.json");
    let runtime_config = config_root.join(if request.android.is_some() { ANDROID_CONFIG } else { "TriAevum.json" });
    // Validate both existing user files before changing any installed title file.
    for user_config in [&topscreen_config, &runtime_config] {
        if user_config.exists() {
            load_json_object(user_config)?;
        }
    }
    publish_topscreen_config(&topscreen_config, request.android.is_some(), request.hud_layout)?;
    ensure_runtime_config(&runtime_config, &product.product)?;
    let savedata = private_root.join("savedata");
    create_dir(&savedata)?;
    let output = private_root.join("runtime-state.json");

    let text = |path: &Path| path_string(path);
    let mut arguments: Vec<String> = Vec::new();
    if request.android.is_none() {
        arguments.extend(["--title-plugin".into(), text(&destination)?]);
    }
    arguments.extend([
        "--a32-process-manifest".into(),
        text(&process_manifest)?,
        "--resource-root".into(),
        text(&resolve(&package.join("resources"))?)?,
        "--renderer".into(),
        "nri".into(),
        "--ui-profile".into(),
        "topscreen".into(),
        "--config".into(),
        text(&runtime_config)?,
        "--topscreen-config".into(),
        text(&topscreen_config)?,
        "--gameplay-timing".into(),
        "native30_interpolated".into(),
        "--presentation-rate".into(),
        "60".into(),
        "--save-data".into(),
        text(&savedata)?,
        "--output".into(),
        text(&output)?,
    ]);
    match request.android {
        None => arguments.extend(["--width".into(), "1280".into(), "--height".into(), "720".into()]),
        // The phone's measured presentation: the guest refresh is admitted
        // separately when one 60 Hz period cannot fit both (input latency).
        Some(_) => arguments.extend([
            "--input-admission".into(),
            "auto".into(),
            "--width".into(),
            "640".into(),
            "--height".into(),
            "360".into(),
        ]),
    }
    let mut route = |option: &str, path: &Path| -> Result<()> {
        arguments.push(option.into());
        arguments.push(text(&resolve(path)?)?);
        Ok(())
    };
    let texture_pack =
        existing_file(&assets.topscreen_texture_pack, "TopScreen texture pack is missing before activation")?;
    if let Some(path) = &texture_pack {
        route("--topscreen-texture-overrides", path)?;
    }
    let link_animations = existing_file(
        &assets.topscreen_link_animations,
        "SingleScreen 3.0 Link animations are missing before activation",
    )?;
    if let Some(path) = &link_animations {
        route("--topscreen-link-animations", path)?;
    }
    let ui_atlas_hd = existing_file(&assets.topscreen_ui_atlas_hd, "HD UI atlas is missing before activation")?;
    if let Some(path) = &ui_atlas_hd {
        route("--topscreen-ui-atlas-hd", path)?;
    }
    let mut ui_texture_receipt = None;
    if let Some(pack) = &assets.ui_texture_pack {
        let receipt = pack.join("source.json");
        if !receipt.is_file() || !pack.join("common").is_dir() {
            return Err(missing("HD UI texture pack is missing before activation"));
        }
        route("--ui-texture-pack", pack)?;
        ui_texture_receipt = Some(receipt);
    }
    let water = existing_file(&assets.water_texture_catalog, "Water texture catalog is missing before activation")?;
    if let Some(path) = &water {
        route("--water-texture-catalog", path)?;
    }
    let lights = existing_file(&assets.scene_light_catalog, "Scene light catalog is missing before activation")?;
    if let Some(path) = &lights {
        route("--scene-light-catalog", path)?;
    }
    let overlay = existing_file(&assets.romfs_overlay, "RomFS mod overlay is missing before activation")?;
    if let Some(path) = &overlay {
        route("--romfs-overlay", path)?;
    }
    let context = InstallationContext::new(&installation);
    let shader_pack =
        existing_file(&assets.pica_shader_pack, "Prepared PICA shader pack is missing before activation")?;
    if let Some(path) = &shader_pack {
        route("--pica-aot-shader-pack", path)?;
    }
    if let Some(directory) = &assets.renderer_cache_directory {
        route("--renderer-cache-directory", directory)?;
    }
    // The managed data tree (UX plan WP-05C), only for a runtime that takes
    // it: logs, crash dumps and save states in D, the custom texture folders
    // under the installed game's own title id.
    let mut data_routes: Vec<(&str, PathBuf)> = Vec::new();
    let mut title_id = None;
    if takes_managed_data_paths(&product.product) {
        for (field, option, folder) in DATA_ROUTES {
            let path = folder.iter().fold(private_root.clone(), |path, part| path.join(part));
            route(option, &path)?;
            data_routes.push((field, path));
        }
        title_id = prepared_title_id(&prepared).map(title_id_text);
        if let Some(text) = &title_id {
            for (field, option, kind) in TEXTURE_ROUTES {
                let path = private_root.join("textures").join(kind).join(text);
                route(option, &path)?;
                data_routes.push((field, path));
            }
        }
    }
    if let Some(text) = &title_id {
        arguments.extend(["--title-id".into(), text.clone()]);
    }

    // File arguments become ${profile_dir}/${runtime_dir} placeholders, or
    // explicit absolute paths outside the installation. Android has no
    // runtime folder: the package's files keep absolute paths.
    let runtime_dir = if request.android.is_none() { Some(package.as_path()) } else { None };
    let mut scopes = Map::new();
    let options = arguments.clone();
    for (index, option) in options[..options.len() - 1].iter().enumerate() {
        if PATH_OPTIONS.contains(&option.as_str()) {
            let target = PathBuf::from(&options[index + 1]);
            arguments[index + 1] = context.profile_argument(&target, &profile_path, runtime_dir)?;
            scopes.insert(option.clone(), json!(context.scope(&target, Some(&profile_directory))?.as_str()));
        }
    }
    let profile =
        json!({"format": LAUNCH_PROFILE_FORMAT, "arguments": arguments, "path_scopes": Value::Object(scopes)});
    match request.android {
        None => publish_plugin(&source_plugin, &destination, &plugin_hash, cancel)?,
        Some(android) => {
            let library = destination.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
            atomic_write_json(
                android.title_file,
                &json!({"format": ANDROID_TITLE_FORMAT, "plugin_library": library, "plugin_sha256": plugin_hash}),
            )?;
        }
    }
    // Query the exact immutable generation in a short-lived process before the
    // single launch-profile replacement makes it visible to direct launches.
    query_product_at(services, &runtime_executable, "", Some(&destination), Some(&diagnostic))?;
    atomic_write_json(&profile_path, &profile)?;

    let title = prepared.directory.clone();
    let reference = |path: &Path| context.reference(path, &title);
    let pinned =
        |path: &Path| -> Result<Value> { Ok(json!({"path": reference(path)?, "sha256": sha256_hex(path, cancel)?})) };
    let mut runtime = Map::new();
    runtime.insert("status".into(), json!("ready"));
    runtime.insert("backend".into(), json!(RUNTIME_BACKEND));
    runtime.insert("target".into(), json!(platform.target));
    runtime.insert("runtime_sha256".into(), json!(product.runtime_sha256));
    runtime.insert("plugin".into(), json!(reference(&destination)?));
    runtime.insert("plugin_scope".into(), json!(context.scope(&destination, Some(&title))?.as_str()));
    runtime.insert("plugin_sha256".into(), json!(plugin_hash));
    runtime.insert("launch_profile".into(), json!(reference(&profile_path)?));
    runtime.insert("launch_profile_scope".into(), json!(context.scope(&profile_path, Some(&title))?.as_str()));
    runtime.insert("launch_profile_sha256".into(), json!(sha256_hex(&profile_path, cancel)?));
    runtime.insert("ui_profile".into(), json!("topscreen"));
    runtime.insert("renderer".into(), json!("nri"));
    for (key, path) in [
        ("topscreen_textures", &texture_pack),
        ("topscreen_link_animations", &link_animations),
        ("topscreen_ui_atlas_hd", &ui_atlas_hd),
    ] {
        if let Some(path) = path {
            runtime.insert(key.into(), pinned(path)?);
        }
    }
    if let (Some(pack), Some(receipt)) = (&assets.ui_texture_pack, &ui_texture_receipt) {
        // A directory: its receipt is pinned; the textures were verified when
        // the pack was prepared.
        runtime.insert(
            "ui_texture_pack".into(),
            json!({"path": reference(pack)?, "receipt_sha256": sha256_hex(receipt, cancel)?}),
        );
    }
    for (key, path) in [
        ("water_textures", &water),
        ("scene_lights", &lights),
        ("romfs_overlay", &overlay),
        ("pica_shader_pack", &shader_pack),
    ] {
        if let Some(path) = path {
            runtime.insert(key.into(), pinned(path)?);
        }
    }
    if let Some(directory) = &assets.renderer_cache_directory {
        runtime.insert("renderer_cache".into(), json!({"path": reference(directory)?}));
    }
    if !data_routes.is_empty() {
        // Routed folders, validated against the profile, never hashed.
        let mut routing = Map::new();
        for (field, path) in &data_routes {
            routing.insert((*field).into(), json!(reference(path)?));
        }
        if let Some(text) = &title_id {
            routing.insert("title_id".into(), json!(text));
        }
        runtime.insert("data_routing".into(), Value::Object(routing));
    }
    // Unknown fields of the state survive; only the runtime receipt changes.
    prepared.state.insert("runtime".into(), Value::Object(runtime));
    atomic_write_json(&prepared.directory.join("forge-state.json"), &Value::Object(prepared.state))?;
    Ok(json!({
        "status": "ready",
        "plugin": text(&destination)?,
        "plugin_sha256": plugin_hash,
        "launch_profile": text(&profile_path)?,
        "topscreen_config": text(&topscreen_config)?,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn standard_profile_has_every_layout_field() {
        let standard = standard_topscreen().unwrap();
        assert_eq!(standard_topscreen_layout(&standard, &standard).unwrap(), standard);
        // The touch-screen start keeps its choices through the layout migration.
        let android = android_topscreen(standard.clone());
        assert_eq!(android.get("hud_scale"), Some(&Value::from(1.0)));
        assert_eq!(android.get("touch_layout"), Some(&Value::Bool(true)));
        assert_eq!(android.get("free_camera_enabled"), Some(&Value::Bool(true)));
        assert_eq!(standard_topscreen_layout(&android, &standard).unwrap(), android);
    }

    #[test]
    fn layout_is_standard_and_only_f1_hud_sizes_survive() {
        let standard = standard_topscreen().unwrap();
        let mut user = standard.clone();
        user.insert("hud_layout".into(), json!("wide"));
        user.insert("minimap_visible".into(), json!(false));
        for (scale, kept) in
            [(json!(1), true), (json!(0.7000001), true), (json!(true), true), (json!(0.85), false), (json!("1"), false)]
        {
            user.insert("hud_scale".into(), scale.clone());
            let migrated = standard_topscreen_layout(&user, &standard).unwrap();
            assert_eq!(migrated["hud_layout"], standard["hud_layout"]);
            assert_eq!(migrated["minimap_visible"], false);
            assert_eq!(migrated["hud_scale"], if kept { scale } else { standard["hud_scale"].clone() });
        }
        let mut other = user.clone();
        other.insert("schema".into(), json!("oot3d_topscreen_ui_v1"));
        assert_eq!(standard_topscreen_layout(&other, &standard).unwrap(), other);
    }
}
