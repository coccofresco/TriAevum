// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Where an installation keeps its files (UX plan WP-05A): every location
//! the game runtime and Forge use, resolved the way they resolve it, with
//! its origin, size and backup class. One read-only answer for the desktop
//! window, the CLI (`inventory`) and the Android installer
//! (`TriAevumForgeInventory`).
//!
//! The runtime's rules this mirrors (re-read them before changing one):
//!
//! - The game's working directory is the activation root `A` (Forge launches
//!   it there; Android sets it to the external files folder,
//!   `ports/android/runtime/android_host.cpp`). `imgui.ini` is relative to
//!   it (`ship/window/gui/Gui.cpp`: `data/imgui.ini` when `data/` exists,
//!   else `imgui.ini`).
//! - Saves, settings and the state file come from the launch profile
//!   (`--save-data`, `--config`, `--topscreen-config`, `--controls-config`,
//!   `--output`, `--renderer-cache-directory`); `savestates.json`,
//!   `migrations.json`, `game_language.json`, `oot3d_controls.json` and
//!   `environments/` sit beside `--config` (`oot3d_native_a32_window_setup.cpp`,
//!   `triaevum_title_adapter.h`, `oot3d_native_game_bootstrap.cpp`).
//! - The managed data tree (UX plan WP-05C, `triaevum_data_routing.h`): a
//!   profile Forge wrote for a runtime with `managed_data_paths` routes the
//!   logs (`--log-directory`, `D/logs`), crash dumps (`--crash-directory`,
//!   `D/logs/crashes`), the default save-state folder
//!   (`--savestate-directory`, `D/savestates`), the custom texture folders'
//!   defaults (`--texture-{load,dump}-directory`,
//!   `D/textures/{load,dump}/<title id>`) and the installed title id
//!   (`--title-id`). An older profile keeps the earlier locations, shown with
//!   origin `legacy`; a routed installation lists the earlier ones that still
//!   hold files as inactive `legacy` rows.
//! - Logs: `--log-directory`, else `logs/` in the working directory
//!   (`ship/Context.cpp` `GetLogDirectory`: `SHIP_HOME` on Linux). Android's
//!   runtime always writes `D/logs` (stdout and stderr too).
//! - Save states: `savestates.json` `Directory`, else the managed default
//!   once the earlier folder's states are copied there
//!   (`oot3d_savestate_migration.h`: a completed record in `migrations.json`;
//!   until then the earlier folder holds them), else beside the executable
//!   on Windows and beside the save data elsewhere
//!   (`oot3d_savestate_library.cpp` `DefaultSavestateDirectory`).
//! - Crash dumps (Windows product builds): `--crash-directory`, else
//!   `<save data>/crashes`.
//! - Custom textures, per folder: `OOT3D_AZAHAR_LOAD_DIRECTORY`/
//!   `_DUMP_DIRECTORY`, else the configuration's
//!   `Graphics.TexturePacks.Azahar.LoadDirectory`/`DumpDirectory` (schema 9
//!   and later), else `<OOT3D_AZAHAR_USER_DIRECTORY>/{load,dump}/textures/
//!   <title id>`, else the routed managed default (except that an existing
//!   pack in Azahar's own folder keeps loading while the managed folder holds
//!   no file; once a pack is imported there, the Azahar folder is an inactive
//!   `texture_load_legacy` row, UX plan WP-07), else
//!   `<Azahar user folder>/{load,dump}/textures/<title id>`. The title id:
//!   `OOT3D_AZAHAR_TITLE_ID`, else `--title-id`, else EUR
//!   (`fast/extension/azahar_texture_pack.cpp`).
//! - Android touch preferences: the activity's default shared preferences,
//!   `<app data>/shared_prefs/<package>_preferences.xml` (`TouchControls.kt`).
//!
//! Entries may nest (crash dumps inside the save data, `environments/`
//! inside the settings folder): `parent` names the innermost entry whose
//! folder contains another, and a parent's size includes its children. A
//! nested entry with another backup class refines its parent: a backup of
//! an included folder leaves out its excluded children
//! ([`Inventory::exclusions_within`]).
//!
//! Paths are real paths (the window opens folders with them). Anything that
//! writes an inventory to a log or a diagnostic report uses
//! [`Inventory::to_redacted_value`].

use std::path::{Path, PathBuf};

use forge_base::fsutil::load_json_object;
use forge_base::{ForgeError, Operation, Result};
use forge_prepare::paths::{expand_profile_argument, expanduser, resolve};
use serde_json::{json, Map, Value};

use crate::explain::explain;
use crate::install::{AndroidTarget, ANDROID_LAUNCH_PROFILE, ANDROID_TITLE_FILE};
use crate::layout::{self, installation_state, HostLayout, HostOs};
use crate::session::{self, Activity, ACTIVATION_DIRECTORY, INSTALLATION_LOCK};

pub const FORMAT: &str = "triaevum_inventory_v1";
pub const REQUEST_FORMAT: &str = "triaevum_forge_inventory_v1";

const DESKTOP_LAUNCH_PROFILE: &str = "TriAevum.launch.json";
const DESKTOP_CONFIG: &str = "TriAevum.json";
const ANDROID_HOST_CONFIG: &str = "TriAevum.android.host.json";
/// The texture title the runtime uses unless `OOT3D_AZAHAR_TITLE_ID` names
/// another (`kOot3dEuropeanTitleId`).
const DEFAULT_TEXTURE_TITLE: &str = "0004000000033600";
/// The first graphics schema that stores the texture folders.
const TEXTURE_FOLDERS_SCHEMA: u64 = 9;

macro_rules! codes {
    ($(#[$meta:meta])* $name:ident { $($(#[$item:meta])* $variant:ident = $code:literal,)+ }) => {
        $(#[$meta])*
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
        pub enum $name {
            $($(#[$item])* $variant,)+
        }

        impl $name {
            pub fn code(self) -> &'static str {
                match self {
                    $(Self::$variant => $code,)+
                }
            }
        }
    };
}

codes! {
    /// The rows of the Data & backups page (UX plan section 3.1).
    Category {
        Saves = "saves",
        SaveStates = "save_states",
        Settings = "settings",
        /// The user's texture pack and texture dumps.
        Textures = "textures",
        /// Mods and downloaded archives.
        Enhancements = "enhancements",
        /// Generated from the user's ROM: reproducible, never backed up.
        Generated = "generated",
        Logs = "logs",
        /// Installation records: launch profile, journals, locks, receipts.
        State = "state",
    }
}

codes! {
    Origin {
        /// The location this version uses when nothing else is chosen.
        Default = "default",
        /// Chosen by the user (a setting, an environment variable, a data folder).
        Explicit = "explicit",
        /// Written by an older version, still honoured or left behind.
        Legacy = "legacy",
    }
}

codes! {
    Backup {
        /// Part of every backup.
        Include = "include",
        /// Offered, not selected by default (decision 2: mods and downloads).
        Optional = "optional",
        Exclude = "exclude",
    }
}

codes! {
    EntryKind {
        File = "file",
        Directory = "directory",
    }
}

codes! {
    /// The installation model, for the uninstall story (section 3.2).
    Platform {
        WindowsPortable = "windows_portable",
        LinuxPortable = "linux_portable",
        AppImage = "appimage",
        Flatpak = "flatpak",
        Android = "android",
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Entry {
    pub category: Category,
    /// Stable identifier of the location (`save_data`, `texture_load`, ...).
    pub logical_name: &'static str,
    /// The innermost other entry whose folder contains this one.
    pub parent: Option<&'static str>,
    pub resolved_path: PathBuf,
    pub kind: EntryKind,
    pub origin: Origin,
    /// The runtime or Forge uses this location now; false for a location an
    /// older version or an earlier choice left behind.
    pub active: bool,
    pub present: bool,
    /// Inside the activation root (or, on Android, the app's private
    /// storage). Paths outside it are the user's: never moved or deleted.
    pub managed: bool,
    /// Bytes of the file, or of every file below the folder (symbolic links
    /// are not followed); 0 when absent or not measured.
    pub size: u64,
    pub files: u64,
    pub backup: Backup,
}

impl Entry {
    fn to_value(&self, path: &dyn Fn(&Path) -> String) -> Value {
        json!({
            "category": self.category.code(),
            "logical_name": self.logical_name,
            "parent": self.parent,
            "path": path(&self.resolved_path),
            "kind": self.kind.code(),
            "origin": self.origin.code(),
            "active": self.active,
            "present": self.present,
            "managed": self.managed,
            "size": self.size,
            "files": self.files,
            "backup": self.backup.code(),
        })
    }
}

/// The roots every entry is resolved from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Roots {
    pub platform: Platform,
    /// `P`, read-only on AppImage, Flatpak and Android.
    pub package: PathBuf,
    /// `A`, the writable activation root (`P` on Windows and plain Linux).
    pub activation: PathBuf,
    /// `D`, the private data folder.
    pub data: PathBuf,
    /// Where the launch profile, journal and locks live (`A/data` on Windows).
    pub state: PathBuf,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Inventory {
    pub roots: Roots,
    pub activity: Activity,
    pub entries: Vec<Entry>,
}

impl Inventory {
    pub fn entry(&self, logical_name: &str) -> Option<&Entry> {
        self.entries.iter().find(|entry| entry.logical_name == logical_name)
    }

    /// The descendants of `logical_name` a backup of it leaves out: nested
    /// entries classed `exclude` (crash dumps in the save data, the launch
    /// marker in the settings folder).
    pub fn exclusions_within(&self, logical_name: &str) -> Vec<&Entry> {
        self.entries
            .iter()
            .filter(|entry| entry.backup == Backup::Exclude && self.descends_from(entry, logical_name))
            .collect()
    }

    fn descends_from(&self, entry: &Entry, ancestor: &str) -> bool {
        let mut parent = entry.parent;
        while let Some(name) = parent {
            if name == ancestor {
                return true;
            }
            parent = self.entry(name).and_then(|entry| entry.parent);
        }
        false
    }

    /// `{"format", "status": "ok", "platform", "roots", "activity", "entries"}`.
    pub fn to_value(&self) -> Value {
        self.value_with(&|path| path.to_string_lossy().into_owned())
    }

    /// [`Inventory::to_value`] with user names removed from every path
    /// ([`crate::redact`]), for logs and diagnostic reports.
    pub fn to_redacted_value(&self) -> Value {
        self.value_with(&|path| crate::redact::redact(&path.to_string_lossy()))
    }

    fn value_with(&self, path: &dyn Fn(&Path) -> String) -> Value {
        let operation = Operation::Backup;
        let activity = self.activity.to_value_for(&self.roots.activation, os_of(self.roots.platform), operation);
        json!({
            "format": FORMAT,
            "status": "ok",
            "platform": self.roots.platform.code(),
            "roots": {
                "package": path(&self.roots.package),
                "activation": path(&self.roots.activation),
                "data": path(&self.roots.data),
                "state": path(&self.roots.state),
            },
            "activity": activity,
            "entries": self.entries.iter().map(|entry| entry.to_value(path)).collect::<Vec<_>>(),
        })
    }
}

fn os_of(platform: Platform) -> HostOs {
    match platform {
        Platform::WindowsPortable => HostOs::Windows,
        Platform::Android => HostOs::Android,
        _ => HostOs::Linux,
    }
}

/// The Android application's private folders, which only the app knows.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AndroidApp {
    /// `Context.getDataDir()`: holds `files/` and `shared_prefs/`.
    pub app_data_dir: PathBuf,
    /// The application id (`Context.getPackageName()`).
    pub package_name: String,
}

/// Everything resolution depends on. Tests build it for any platform.
pub struct Installation<'a> {
    pub os: HostOs,
    pub layout: HostLayout,
    /// The private data folder; None for the layout's default (`A/data`).
    pub data_root: Option<PathBuf>,
    /// The folder of the runtime executable (Android: the native libraries).
    pub runtime_dir: PathBuf,
    pub android: Option<AndroidApp>,
    /// The environment the game inherits (Forge launches it with its own).
    pub env: &'a dyn Fn(&str) -> Option<String>,
    /// Measure sizes (walks every folder; generated trees are large).
    pub sizes: bool,
}

/// What a frontend asks for.
#[derive(Clone)]
pub struct InventoryRequest {
    pub package_root: PathBuf,
    pub data_root: Option<PathBuf>,
    /// The Android installation (its profile root is `A`).
    pub android: Option<AndroidTarget>,
    pub android_app: Option<AndroidApp>,
    pub sizes: bool,
}

impl InventoryRequest {
    pub fn package(package_root: impl Into<PathBuf>) -> Self {
        Self { package_root: package_root.into(), data_root: None, android: None, android_app: None, sizes: true }
    }

    /// `{"format": "triaevum_forge_inventory_v1", "package_root"?,
    /// "data_root"?, "android"?: {"native_library_dir", "profile_root",
    /// "app_data_dir"?, "package_name"?}, "sizes"?}`.
    pub fn parse(request: &Map<String, Value>) -> Result<Self> {
        if request.get("format").and_then(Value::as_str) != Some(REQUEST_FORMAT) {
            return Err(ForgeError::invalid(format!("Inventory request format must be {REQUEST_FORMAT}")));
        }
        let path_in = |map: &Map<String, Value>, key: &str| {
            map.get(key).and_then(Value::as_str).filter(|text| !text.is_empty()).map(PathBuf::from)
        };
        let package_root = match path_in(request, "package_root") {
            Some(root) => root,
            None => layout::executable_package_root()?,
        };
        let (android, android_app) = match request.get("android") {
            None | Some(Value::Null) => (None, None),
            Some(Value::Object(android)) => {
                let required = |key: &str| {
                    path_in(android, key).ok_or_else(|| ForgeError::invalid(format!("android.{key} must be a path")))
                };
                let target = AndroidTarget {
                    native_library_dir: required("native_library_dir")?,
                    profile_root: required("profile_root")?,
                };
                let package_name = android.get("package_name").and_then(Value::as_str).filter(|name| !name.is_empty());
                let app = match (path_in(android, "app_data_dir"), package_name) {
                    (Some(app_data_dir), Some(name)) => {
                        Some(AndroidApp { app_data_dir, package_name: name.to_string() })
                    }
                    (None, None) => None,
                    _ => return Err(ForgeError::invalid("android.app_data_dir and android.package_name go together")),
                };
                (Some(target), app)
            }
            Some(_) => return Err(ForgeError::invalid("android must be an object")),
        };
        let sizes = match request.get("sizes") {
            None | Some(Value::Null) => true,
            Some(Value::Bool(flag)) => *flag,
            Some(_) => return Err(ForgeError::invalid("sizes must be a boolean")),
        };
        Ok(Self { package_root, data_root: path_in(request, "data_root"), android, android_app, sizes })
    }
}

/// The inventory of the package at `root` on this host, with sizes.
pub fn inventory(root: &Path) -> Result<Inventory> {
    inventory_of(&InventoryRequest::package(root))
}

/// The inventory a request names, on this host.
pub fn inventory_of(request: &InventoryRequest) -> Result<Inventory> {
    let package = resolve(&expanduser(&request.package_root))?;
    let data_root = request.data_root.as_ref().map(|path| expanduser(path));
    let installation = match &request.android {
        None => {
            let os = HostOs::current();
            Installation {
                os,
                layout: layout::for_package(&package, &layout::process_env, os)?,
                data_root,
                runtime_dir: package.clone(),
                android: None,
                env: &layout::process_env,
                sizes: request.sizes,
            }
        }
        Some(android) => Installation {
            os: HostOs::Android,
            layout: HostLayout {
                package: package.clone(),
                activation: resolve(&android.profile_root)?,
                use_file_portal: false,
            },
            data_root,
            runtime_dir: resolve(&android.native_library_dir)?,
            android: match &request.android_app {
                Some(app) => Some(AndroidApp {
                    app_data_dir: resolve(&app.app_data_dir)?,
                    package_name: app.package_name.clone(),
                }),
                None => None,
            },
            env: &no_environment,
            sizes: request.sizes,
        },
    };
    resolve_inventory(&installation)
}

/// [`inventory_of`] for a JSON request (the ABI and JNI entry), as JSON:
/// the inventory, or `{"format", "status": "failed", "explanation"}`.
pub fn inventory_bytes(request: &[u8]) -> Vec<u8> {
    let parsed = crate::legacy_json::load_object(request)
        .map_err(|error| ForgeError::invalid(format!("Invalid inventory request: {error}")))
        .and_then(|request| InventoryRequest::parse(&request))
        .map_err(|error| error.during(Operation::RequestValidation));
    let value = match parsed.and_then(|request| inventory_of(&request).map_err(|e| e.during(Operation::Inspect))) {
        Ok(inventory) => inventory.to_value(),
        Err(error) => json!({"format": FORMAT, "status": "failed", "explanation": explain(&error).to_value()}),
    };
    crate::legacy_json::compact_line_bytes(&value)
}

/// pathlib equality: case-insensitive on Windows.
fn same_path(left: &Path, right: &Path) -> bool {
    if cfg!(windows) {
        left.to_string_lossy().to_lowercase() == right.to_string_lossy().to_lowercase()
    } else {
        left == right
    }
}

/// A folder holding at least one regular file, at any depth: a texture pack
/// (the runtime's `ResolveFolder` uses the same test, UX plan WP-07).
pub fn holds_texture_files(folder: &Path) -> bool {
    forge_mods::user_textures::count_files(folder, 1) > 0
}

fn is_within(path: &Path, base: &Path) -> bool {
    if cfg!(windows) {
        let lower = |path: &Path| PathBuf::from(path.to_string_lossy().to_lowercase());
        lower(path).starts_with(lower(base))
    } else {
        path.starts_with(base)
    }
}

/// The arguments of a launch profile, expanded as the runtime expands them.
struct Profile {
    arguments: Vec<String>,
}

impl Profile {
    fn load(path: &Path, runtime_dir: &Path) -> Option<Self> {
        let payload = load_json_object(path).ok()?;
        if payload.get("format").and_then(Value::as_str) != Some("oot3d_native_game_launch_profile_v1") {
            return None;
        }
        let arguments = payload
            .get("arguments")?
            .as_array()?
            .iter()
            .map(|item| item.as_str().and_then(|text| expand_profile_argument(text, path, Some(runtime_dir)).ok()))
            .collect::<Option<Vec<String>>>()?;
        Some(Self { arguments })
    }

    fn value(&self, option: &str) -> Option<&str> {
        let index = self.arguments.iter().position(|argument| argument == option)?;
        self.arguments.get(index + 1).map(String::as_str)
    }
}

struct Resolver<'a> {
    installation: &'a Installation<'a>,
    roots: Roots,
    data_explicit: bool,
    profile: Option<Profile>,
    /// The game's working directory.
    working: PathBuf,
    entries: Vec<Entry>,
}

/// One location before measurement.
struct Location {
    category: Category,
    name: &'static str,
    path: PathBuf,
    kind: EntryKind,
    origin: Origin,
    active: bool,
    backup: Backup,
}

impl Location {
    fn new(category: Category, name: &'static str, path: PathBuf, kind: EntryKind, backup: Backup) -> Self {
        Self { category, name, path, kind, origin: Origin::Default, active: true, backup }
    }

    fn origin(mut self, origin: Origin) -> Self {
        self.origin = origin;
        self
    }

    fn inactive(mut self) -> Self {
        self.active = false;
        self
    }
}

use Backup::{Exclude, Include, Optional};
use Category as C;
use EntryKind::{Directory, File};

/// Resolves every location of `installation`. Read-only: creates and
/// writes nothing.
pub fn resolve_inventory(installation: &Installation) -> Result<Inventory> {
    let os = installation.os;
    let activation = resolve(&installation.layout.activation)?;
    let default_data = resolve(&installation.layout.data())?;
    let data = match &installation.data_root {
        Some(path) => resolve(path)?,
        None => default_data.clone(),
    };
    let platform = match os {
        HostOs::Windows => Platform::WindowsPortable,
        HostOs::Android => Platform::Android,
        _ if installation.layout.use_file_portal => Platform::Flatpak,
        _ if same_path(&activation, &resolve(&installation.layout.package)?) => Platform::LinuxPortable,
        _ => Platform::AppImage,
    };
    let roots = Roots {
        platform,
        package: resolve(&installation.layout.package)?,
        state: installation_state(&activation, os),
        activation: activation.clone(),
        data: data.clone(),
    };
    let mut resolver = Resolver {
        installation,
        data_explicit: !same_path(&data, &default_data),
        profile: None,
        working: activation.clone(),
        roots,
        entries: Vec::new(),
    };
    resolver.resolve_all()?;
    let mut entries = std::mem::take(&mut resolver.entries);
    assign_parents(&mut entries);
    Ok(Inventory { activity: session::activity(&activation, os), roots: resolver.roots, entries })
}

/// The innermost folder entry containing each entry, by path.
fn assign_parents(entries: &mut [Entry]) {
    let folders: Vec<(&'static str, PathBuf)> = entries
        .iter()
        .filter(|entry| entry.kind == Directory)
        .map(|entry| (entry.logical_name, entry.resolved_path.clone()))
        .collect();
    for entry in entries.iter_mut() {
        entry.parent = folders
            .iter()
            .filter(|(name, path)| {
                *name != entry.logical_name
                    && is_within(&entry.resolved_path, path)
                    && !same_path(&entry.resolved_path, path)
            })
            .max_by_key(|(_, path)| path.components().count())
            .map(|(name, _)| *name);
    }
}

fn size_of(path: &Path) -> (u64, u64) {
    let Ok(metadata) = std::fs::symlink_metadata(path) else { return (0, 0) };
    if !metadata.is_dir() {
        return (if metadata.is_file() { metadata.len() } else { 0 }, u64::from(metadata.is_file()));
    }
    let (mut bytes, mut files) = (0u64, 0u64);
    let mut pending = vec![path.to_path_buf()];
    while let Some(directory) = pending.pop() {
        let Ok(children) = std::fs::read_dir(&directory) else { continue };
        for child in children.flatten() {
            let Ok(metadata) = child.path().symlink_metadata() else { continue };
            if metadata.is_dir() {
                pending.push(child.path());
            } else if metadata.is_file() {
                bytes += metadata.len();
                files += 1;
            }
        }
    }
    (bytes, files)
}

impl Resolver<'_> {
    fn env(&self, name: &str) -> Option<String> {
        (self.installation.env)(name).filter(|value| !value.is_empty())
    }

    /// A path the runtime reads relative to its working directory.
    fn runtime_path(&self, value: &str) -> Result<PathBuf> {
        let path = PathBuf::from(value);
        resolve(&if path.is_absolute() { path } else { self.working.join(path) })
    }

    /// The origin of a path that defaults to `default` inside the data folder.
    fn data_origin(&self, path: &Path, default: &Path) -> Origin {
        if same_path(path, default) && !self.data_explicit {
            Origin::Default
        } else {
            Origin::Explicit
        }
    }

    /// A profile-routed path, its default under the data folder otherwise.
    fn routed(&self, option: &str, default: PathBuf) -> Result<(PathBuf, Origin)> {
        match self.profile.as_ref().and_then(|profile| profile.value(option)) {
            Some(value) => {
                let path = self.runtime_path(value)?;
                let origin = self.data_origin(&path, &default);
                Ok((path, origin))
            }
            None => Ok((default, if self.data_explicit { Origin::Explicit } else { Origin::Default })),
        }
    }

    fn push(&mut self, location: Location) {
        let present = match location.kind {
            File => location.path.is_file(),
            Directory => location.path.is_dir(),
        };
        let (size, files) = if present && self.installation.sizes { size_of(&location.path) } else { (0, 0) };
        let app_private =
            self.installation.android.as_ref().is_some_and(|app| is_within(&location.path, &app.app_data_dir));
        self.entries.push(Entry {
            category: location.category,
            logical_name: location.name,
            parent: None,
            managed: is_within(&location.path, &self.roots.activation) || app_private,
            resolved_path: location.path,
            kind: location.kind,
            origin: location.origin,
            active: location.active,
            present,
            size,
            files,
            backup: location.backup,
        });
    }

    fn resolve_all(&mut self) -> Result<()> {
        let os = self.installation.os;
        let data = self.roots.data.clone();
        let activation = self.roots.activation.clone();
        let android = os == HostOs::Android;

        // The launch profile the game starts from.
        let (profile_path, profile_origin) = if android {
            (activation.join(ANDROID_LAUNCH_PROFILE), Origin::Default)
        } else {
            // An installation made before the profile moved to data/ keeps
            // its root profile, which the runtime prefers.
            let legacy = activation.join(DESKTOP_LAUNCH_PROFILE);
            let current = self.roots.state.join(DESKTOP_LAUNCH_PROFILE);
            if legacy.is_file() && !same_path(&legacy, &current) {
                (legacy, Origin::Legacy)
            } else {
                (current, Origin::Default)
            }
        };
        self.profile = Profile::load(&profile_path, &self.installation.runtime_dir);

        // The game's working directory (Linux honours SHIP_HOME).
        if os == HostOs::Linux {
            if let Some(home) = self.env("SHIP_HOME") {
                self.working = resolve(Path::new(&home))?;
            }
        }
        let app_directory_origin =
            if same_path(&self.working, &activation) { Origin::Default } else { Origin::Explicit };

        // Saves.
        let (save_data, save_origin) = self.routed("--save-data", data.join("savedata"))?;
        self.push(Location::new(C::Saves, "save_data", save_data.clone(), Directory, Include).origin(save_origin));
        if os == HostOs::Windows {
            // Dumps in the save data stay listed (and out of save backups).
            let earlier = save_data.join("crashes");
            match self.managed("--crash-directory", crate::log::logs_directory(&data).join("crashes"))? {
                Some((dumps, origin)) => {
                    let moved = !same_path(&dumps, &earlier);
                    self.push(Location::new(C::Logs, "crash_dumps", dumps, Directory, Exclude).origin(origin));
                    if moved && earlier.is_dir() {
                        let left = Location::new(C::Logs, "crash_dumps_legacy", earlier, Directory, Exclude);
                        self.push(left.origin(Origin::Legacy).inactive());
                    }
                }
                None => self.push(
                    Location::new(C::Logs, "crash_dumps", earlier, Directory, Exclude).origin(Origin::Legacy),
                ),
            }
        }

        // Settings beside the runtime configuration.
        let config_name = if android { crate::publish::ANDROID_CONFIG } else { DESKTOP_CONFIG };
        let (runtime_config, config_origin) = self.routed("--config", data.join("config").join(config_name))?;
        let config_directory = runtime_config.parent().map(Path::to_path_buf).unwrap_or_else(|| data.clone());
        let beside = |name: &str| config_directory.join(name);

        // Save states.
        let savestates_index = beside("savestates.json");
        let earlier_states = if os == HostOs::Windows {
            self.installation.runtime_dir.join("savestates")
        } else {
            save_data.parent().map(|parent| parent.join("savestates")).unwrap_or_else(|| data.join("savestates"))
        };
        let earlier_states = resolve(&earlier_states)?;
        let managed_states = self.managed("--savestate-directory", data.join("savestates"))?;
        // The earlier folder holds states the game has not copied yet: it
        // stays the default until its next start completes the move.
        let pending_move = managed_states.as_ref().is_some_and(|(managed, _)| {
            !same_path(managed, &earlier_states)
                && holds_files(&earlier_states)
                && !savestates_moved(&beside(SAVESTATE_MIGRATIONS), &earlier_states, managed)
        });
        let default_states = match &managed_states {
            Some((managed, _)) if !pending_move => managed.clone(),
            _ => earlier_states.clone(),
        };
        let chosen = load_json_object(&savestates_index)
            .ok()
            .and_then(|index| index.get("Directory").and_then(Value::as_str).map(str::to_string))
            .filter(|directory| !directory.is_empty());
        let active_states = match chosen {
            Some(directory) => {
                let chosen = self.runtime_path(&directory)?;
                self.push(
                    Location::new(C::SaveStates, "save_states", chosen.clone(), Directory, Include)
                        .origin(Origin::Explicit),
                );
                if !same_path(&chosen, &default_states) && default_states.is_dir() {
                    let unused = Location::new(
                        C::SaveStates,
                        "save_states_default",
                        default_states.clone(),
                        Directory,
                        Optional,
                    );
                    self.push(unused.inactive());
                }
                chosen
            }
            None => {
                let origin = match &managed_states {
                    Some((_, origin)) if !pending_move => *origin,
                    // An older profile's, or the earlier folder until the move.
                    _ if os == HostOs::Windows || pending_move => Origin::Legacy,
                    _ => save_origin,
                };
                self.push(
                    Location::new(C::SaveStates, "save_states", default_states.clone(), Directory, Include)
                        .origin(origin),
                );
                default_states.clone()
            }
        };
        // The earlier folder after the move (the game keeps it, with
        // README-moved.txt): offered in a backup, never removed by Forge.
        if managed_states.is_some()
            && !same_path(&earlier_states, &active_states)
            && !same_path(&earlier_states, &default_states)
            && earlier_states.is_dir()
        {
            let left = Location::new(C::SaveStates, "save_states_legacy", earlier_states, Directory, Optional);
            self.push(left.origin(Origin::Legacy).inactive());
        }

        self.push(
            Location::new(C::Settings, "config", config_directory.clone(), Directory, Include).origin(config_origin),
        );
        self.push(
            Location::new(C::Settings, "runtime_config", runtime_config.clone(), File, Include).origin(config_origin),
        );
        let (topscreen, topscreen_origin) =
            self.routed("--topscreen-config", data.join("config").join("topscreen_ui.json"))?;
        self.push(Location::new(C::Settings, "topscreen_config", topscreen, File, Include).origin(topscreen_origin));
        let controls = match self.profile.as_ref().and_then(|profile| profile.value("--controls-config")) {
            Some(value) => (self.runtime_path(value)?, Origin::Explicit),
            None => (beside("oot3d_controls.json"), config_origin),
        };
        self.push(Location::new(C::Settings, "controls_config", controls.0, File, Include).origin(controls.1));
        self.push(
            Location::new(C::Settings, "game_language", beside("game_language.json"), File, Include)
                .origin(config_origin),
        );
        // Forge's own settings stay in the data folder.
        let data_origin = if self.data_explicit { Origin::Explicit } else { Origin::Default };
        let romfs_mods = data.join("config").join(forge_mods::romfs_mods::SETTINGS_NAME);
        self.push(Location::new(C::Settings, "romfs_mods", romfs_mods, File, Include).origin(data_origin));
        self.push(
            Location::new(C::Settings, "savestates_index", savestates_index, File, Include).origin(config_origin),
        );
        self.push(
            Location::new(C::Settings, "migrations", beside(SAVESTATE_MIGRATIONS), File, Include)
                .origin(config_origin),
        );
        self.push(
            Location::new(C::Settings, "environments", beside("environments"), Directory, Include)
                .origin(config_origin),
        );
        let mut marker = runtime_config.clone().into_os_string();
        marker.push(".launching");
        self.push(Location::new(C::State, "launch_marker", PathBuf::from(marker), File, Exclude));

        // imgui.ini: data/imgui.ini when the working directory has data/.
        let current_imgui = self.working.join("data").join("imgui.ini");
        let root_imgui = self.working.join("imgui.ini");
        if self.working.join("data").is_dir() {
            self.push(
                Location::new(C::Settings, "imgui_layout", current_imgui, File, Include).origin(app_directory_origin),
            );
            if root_imgui.is_file() {
                let left = Location::new(C::Settings, "imgui_layout_legacy", root_imgui, File, Exclude);
                self.push(left.origin(Origin::Legacy).inactive());
            }
        } else {
            self.push(
                Location::new(C::Settings, "imgui_layout", root_imgui, File, Include).origin(app_directory_origin),
            );
        }
        if android {
            self.push(Location::new(
                C::Settings,
                "android_host_config",
                activation.join(ANDROID_HOST_CONFIG),
                File,
                Optional,
            ));
            if let Some(app) = self.installation.android.clone() {
                let preferences =
                    app.app_data_dir.join("shared_prefs").join(format!("{}_preferences.xml", app.package_name));
                self.push(Location::new(C::Settings, "touch_preferences", preferences, File, Include));
            }
        }

        // Custom textures.
        let (texture_load, texture_dump, load_origin, dump_origin) = self.texture_folders(&runtime_config)?;
        self.push(Location::new(C::Textures, "texture_load", texture_load.clone(), Directory, Optional).origin(load_origin));
        self.push(Location::new(C::Textures, "texture_dump", texture_dump, Directory, Exclude).origin(dump_origin));
        // A pack imported into the managed folder outranks the one left in
        // Azahar's folder (UX plan WP-07), which stays listed, never touched.
        if let Some(earlier) = self.azahar_load_folder()?.filter(|earlier| !same_path(earlier, &texture_load)) {
            if load_origin == Origin::Default && holds_texture_files(&earlier) {
                let left = Location::new(C::Textures, "texture_load_legacy", earlier, Directory, Exclude);
                self.push(left.origin(Origin::Legacy).inactive());
            }
        }

        // Enhancements (decision 2: never in the default backup).
        self.push(Location::new(C::Enhancements, "mods", data.join("mods"), Directory, Optional).origin(data_origin));
        self.push(
            Location::new(C::Enhancements, "downloads", data.join("downloads"), Directory, Optional)
                .origin(data_origin),
        );

        // Generated from the ROM.
        for (name, folder) in [
            ("sources", "sources"),
            ("titles", "titles"),
            ("cache", "cache"),
            ("shader_seeds", "shader-seeds"),
            ("interior_sun", "interior-sun"),
        ] {
            self.push(Location::new(C::Generated, name, data.join(folder), Directory, Exclude).origin(data_origin));
        }
        let (renderer_cache, cache_origin) =
            self.routed("--renderer-cache-directory", data.join("cache").join("renderer"))?;
        self.push(
            Location::new(C::Generated, "renderer_cache", renderer_cache, Directory, Exclude).origin(cache_origin),
        );
        let translator = data.join("translator-cache");
        if translator.is_dir() {
            self.push(
                Location::new(C::Generated, "translator_cache", translator, Directory, Exclude).origin(data_origin),
            );
        }

        // Logs: routed (Android's runtime always writes D/logs), else the
        // working directory's logs/ of an older profile.
        let earlier_logs = self.working.join("logs");
        let managed_logs = match self.managed("--log-directory", crate::log::logs_directory(&data))? {
            Some(routed) => Some(routed),
            None if android => Some((crate::log::logs_directory(&data), data_origin)),
            None => None,
        };
        match managed_logs {
            Some((logs, origin)) => {
                let moved = !same_path(&logs, &earlier_logs);
                self.push(Location::new(C::Logs, "game_logs", logs, Directory, Exclude).origin(origin));
                if moved && earlier_logs.is_dir() {
                    let left = Location::new(C::Logs, "game_logs_legacy", earlier_logs, Directory, Exclude);
                    self.push(left.origin(Origin::Legacy).inactive());
                }
            }
            None => {
                let origin = if app_directory_origin == Origin::Default { Origin::Legacy } else { Origin::Explicit };
                self.push(Location::new(C::Logs, "game_logs", earlier_logs, Directory, Exclude).origin(origin));
            }
        }
        self.push(
            Location::new(C::Logs, "forge_logs", crate::log::logs_directory(&data), Directory, Exclude)
                .origin(data_origin),
        );
        if let Some(app) = self.installation.android.clone() {
            let diagnostics = app.app_data_dir.join("files").join("diagnostics");
            self.push(Location::new(C::Logs, "android_diagnostics", diagnostics, Directory, Exclude));
        }

        // Installation records.
        self.push(Location::new(C::State, "launch_profile", profile_path, File, Exclude).origin(profile_origin));
        if android {
            self.push(Location::new(
                C::State,
                "android_title_file",
                activation.join(ANDROID_TITLE_FILE),
                File,
                Exclude,
            ));
        } else {
            self.push(Location::new(C::State, "title_plugins", activation.join("private-plugins"), Directory, Exclude));
        }
        let state = self.roots.state.clone();
        self.push(Location::new(C::State, "activation_records", state.join(ACTIVATION_DIRECTORY), Directory, Exclude));
        self.push(Location::new(C::State, "installation_lock", state.join(INSTALLATION_LOCK), File, Exclude));
        self.push(
            Location::new(C::State, "active_title", data.join("active-title.json"), File, Exclude).origin(data_origin),
        );
        let (runtime_state, state_origin) = self.routed("--output", data.join("runtime-state.json"))?;
        self.push(Location::new(C::State, "runtime_state", runtime_state, File, Exclude).origin(state_origin));
        self.push(Location::new(C::State, "backups", data.join("backups"), Directory, Exclude).origin(data_origin));
        Ok(())
    }

    /// The installed title (`--title-id`, written by Forge from the ROM's
    /// ExHeader: a USA installation is 0004000000033500), unless the
    /// environment names one; EUR for an older profile.
    fn texture_title(&self) -> String {
        self.env("OOT3D_AZAHAR_TITLE_ID")
            .or_else(|| self.profile.as_ref().and_then(|profile| profile.value("--title-id")).map(str::to_string))
            .unwrap_or_else(|| DEFAULT_TEXTURE_TITLE.to_string())
            .to_uppercase()
    }

    /// Azahar's own load folder for the installed title.
    fn azahar_load_folder(&self) -> Result<Option<PathBuf>> {
        let folder = self.azahar_user_directory()?.join("load").join("textures").join(self.texture_title());
        Ok(Some(resolve(&folder)?))
    }

    /// The custom texture folders and their origins, as the runtime picks
    /// them: environment, then the configuration, then the Azahar user folder.
    fn texture_folders(&self, runtime_config: &Path) -> Result<(PathBuf, PathBuf, Origin, Origin)> {
        let configured = |key: &str| -> Option<String> {
            let config = load_json_object(runtime_config).ok()?;
            let graphics = config.get("Graphics")?.as_object()?;
            if graphics.get("SchemaVersion")?.as_u64()? < TEXTURE_FOLDERS_SCHEMA {
                return None;
            }
            let value = graphics.get("TexturePacks")?.get("Azahar")?.get(key)?.as_str()?;
            (!value.is_empty()).then(|| value.to_string())
        };
        let title = self.texture_title();
        let user_root = self.env("OOT3D_AZAHAR_USER_DIRECTORY");
        let azahar = self.azahar_user_directory()?;
        let textures = self.roots.data.join("textures");
        let folder = |environment: &str, key: &str, kind: &str, option: &str| -> Result<(PathBuf, Origin)> {
            if let Some(value) = self.env(environment).or_else(|| configured(key)) {
                return Ok((self.runtime_path(&value)?, Origin::Explicit));
            }
            if let Some(root) = &user_root {
                return Ok((resolve(&self.runtime_path(root)?.join(kind).join("textures").join(&title))?, Origin::Explicit));
            }
            let earlier = resolve(&azahar.join(kind).join("textures").join(&title))?;
            match self.managed(option, textures.join(kind).join(&title))? {
                // An existing pack in Azahar's folder keeps loading from there
                // until a pack is imported into the managed folder (WP-07).
                Some((managed, _)) if kind == "load" && earlier.is_dir() && !holds_texture_files(&managed) => {
                    Ok((earlier, Origin::Legacy))
                }
                Some(managed) => Ok(managed),
                None => Ok((earlier, Origin::Legacy)),
            }
        };
        let (load, load_origin) =
            folder("OOT3D_AZAHAR_LOAD_DIRECTORY", "LoadDirectory", "load", "--texture-load-directory")?;
        let (dump, dump_origin) =
            folder("OOT3D_AZAHAR_DUMP_DIRECTORY", "DumpDirectory", "dump", "--texture-dump-directory")?;
        Ok((load, dump, load_origin, dump_origin))
    }

    /// A folder only a launch profile of the managed data tree routes: its
    /// path and origin, or None for an older profile (UX plan WP-05C).
    fn managed(&self, option: &str, default: PathBuf) -> Result<Option<(PathBuf, Origin)>> {
        match self.profile.as_ref().and_then(|profile| profile.value(option)) {
            Some(value) => {
                let path = self.runtime_path(value)?;
                let origin = self.data_origin(&path, &resolve(&default)?);
                Ok(Some((path, origin)))
            }
            None => Ok(None),
        }
    }

    /// `ResolveDefaultUserDirectory` without `OOT3D_AZAHAR_USER_DIRECTORY`
    /// (a named root ranks above the managed defaults): Android's game
    /// process has neither `XDG_DATA_HOME` nor `HOME`, so it falls back to its
    /// working directory.
    fn azahar_user_directory(&self) -> Result<PathBuf> {
        let default = match self.installation.os {
            HostOs::Windows => self.env("APPDATA").map(|app_data| PathBuf::from(app_data).join("Azahar")),
            _ => self
                .env("XDG_DATA_HOME")
                .map(|data_home| PathBuf::from(data_home).join("azahar"))
                .or_else(|| self.env("HOME").map(|home| PathBuf::from(home).join(".local/share/azahar"))),
        };
        resolve(&default.unwrap_or_else(|| self.working.join("Azahar")))
    }
}

/// The Android game process does not share Forge's environment.
fn no_environment(_: &str) -> Option<String> {
    None
}

/// The runtime's migration record (`oot3d_savestate_migration.h`), beside
/// the runtime configuration.
pub const SAVESTATE_MIGRATIONS: &str = "migrations.json";
const SAVESTATE_MIGRATION_ID: &str = "savestates_to_data_folder";

/// Whether the runtime recorded the completed move from `earlier` to `managed`.
fn savestates_moved(record: &Path, earlier: &Path, managed: &Path) -> bool {
    let Ok(document) = load_json_object(record) else { return false };
    let same = |value: Option<&Value>, path: &Path| {
        value.and_then(Value::as_str).is_some_and(|text| resolve(Path::new(text)).is_ok_and(|p| same_path(&p, path)))
    };
    document.get("migrations").and_then(Value::as_array).is_some_and(|records| {
        records.iter().any(|entry| {
            entry.get("id").and_then(Value::as_str) == Some(SAVESTATE_MIGRATION_ID)
                && entry.get("status").and_then(Value::as_str) == Some("completed")
                && same(entry.get("source"), earlier)
                && same(entry.get("destination"), managed)
        })
    })
}

/// Whether a folder holds any file the runtime's move would copy.
fn holds_files(folder: &Path) -> bool {
    let mut pending = vec![folder.to_path_buf()];
    while let Some(directory) = pending.pop() {
        let Ok(children) = std::fs::read_dir(&directory) else { continue };
        for child in children.flatten() {
            let Ok(metadata) = child.path().symlink_metadata() else { continue };
            if metadata.is_dir() {
                pending.push(child.path());
            } else if metadata.is_file() && !(directory == folder && child.file_name() == "README-moved.txt") {
                return true;
            }
        }
    }
    false
}
