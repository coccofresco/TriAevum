// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The resolved inventory on synthetic installations of every platform
//! layout (Windows portable, Linux AppImage, Flatpak, Android), with
//! explicit external paths and legacy locations, and on installations made
//! by the real import. Resolution never writes.

mod common;

use std::path::{Path, PathBuf};

use common::{FakeRuntime, Fixture};
use forge_base::hashing::sha256_hex;
use forge_core::abi::{TriAevumForgeFreeBuffer, TriAevumForgeInventory};
use forge_core::inventory::{
    inventory_bytes, inventory_of, resolve_inventory, AndroidApp, Backup, Category, Entry, Installation, Inventory,
    InventoryRequest, Origin, Platform,
};
use forge_core::layout::{for_package, HostLayout, HostOs, FLATPAK_ID};
use forge_core::session::{session_lock_path, ACTIVATION_DIRECTORY};
use forge_prepare::paths::resolve;
use serde_json::{json, Value};

fn write(path: &Path, bytes: &[u8]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, bytes).unwrap();
}

fn write_json(path: &Path, value: &Value) {
    write(path, value.to_string().as_bytes());
}

fn tree(root: &Path) -> Vec<(String, String)> {
    fn walk(root: &Path, directory: &Path, out: &mut Vec<(String, String)>) {
        for entry in std::fs::read_dir(directory).unwrap() {
            let path = entry.unwrap().path();
            let relative = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
            if path.is_dir() {
                out.push((relative, "dir".into()));
                walk(root, &path, out);
            } else {
                let digest = std::fs::read(&path).map_or_else(|_| "locked".to_string(), |bytes| sha256_hex(&bytes));
                out.push((relative, digest));
            }
        }
    }
    let mut out = Vec::new();
    walk(root, root, &mut out);
    out.sort();
    out
}

fn env_of(pairs: Vec<(&'static str, String)>) -> impl Fn(&str) -> Option<String> {
    move |name| pairs.iter().find(|(key, _)| *key == name).map(|(_, value)| value.clone())
}

fn text(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

/// A launch profile as Forge writes it: paths relative to `${profile_dir}`.
fn profile(path: &Path, data_reference: &str, config: &str, extra: &[String]) {
    let at = |relative: &str| format!("${{profile_dir}}/{data_reference}{relative}");
    let mut arguments = vec![
        "--title-plugin".to_string(),
        "${profile_dir}/private-plugins/x/triaevum_title_aot".to_string(),
        "--config".into(),
        at(&format!("config/{config}")),
        "--topscreen-config".into(),
        at("config/topscreen_ui.json"),
        "--save-data".into(),
        at("savedata"),
        "--output".into(),
        at("runtime-state.json"),
        "--renderer-cache-directory".into(),
        at("cache/renderer"),
    ];
    arguments.extend(extra.iter().cloned());
    write_json(path, &json!({"format": "oot3d_native_game_launch_profile_v1", "arguments": arguments}));
}

/// Resolves and proves that nothing under `root` changed.
fn resolved(root: &Path, installation: &Installation) -> Inventory {
    let before = tree(root);
    let inventory = resolve_inventory(installation).unwrap();
    assert_eq!(tree(root), before, "the inventory wrote to the installation");
    inventory
}

fn entry<'a>(inventory: &'a Inventory, name: &str) -> &'a Entry {
    inventory.entry(name).unwrap_or_else(|| panic!("no entry {name}"))
}

fn assert_entry(inventory: &Inventory, name: &str, path: &Path, origin: Origin, backup: Backup) {
    let found = entry(inventory, name);
    assert_eq!(found.resolved_path, resolve(path).unwrap(), "{name}");
    assert_eq!(found.origin, origin, "{name}");
    assert_eq!(found.backup, backup, "{name}");
}

/// The classes every platform shares (UX plan section 3.1, decision 2).
fn assert_backup_classes(inventory: &Inventory) {
    for (name, backup) in [
        ("save_data", Backup::Include),
        ("save_states", Backup::Include),
        ("config", Backup::Include),
        ("runtime_config", Backup::Include),
        ("environments", Backup::Include),
        ("imgui_layout", Backup::Include),
        ("texture_load", Backup::Optional),
        ("texture_dump", Backup::Exclude),
        ("mods", Backup::Optional),
        ("downloads", Backup::Optional),
        ("sources", Backup::Exclude),
        ("titles", Backup::Exclude),
        ("cache", Backup::Exclude),
        ("renderer_cache", Backup::Exclude),
        ("game_logs", Backup::Exclude),
        ("forge_logs", Backup::Exclude),
        ("launch_profile", Backup::Exclude),
        ("activation_records", Backup::Exclude),
        ("launch_marker", Backup::Exclude),
    ] {
        assert_eq!(entry(inventory, name).backup, backup, "{name}");
    }
    assert_eq!(entry(inventory, "mods").category, Category::Enhancements);
    assert_eq!(entry(inventory, "sources").category, Category::Generated);
    let names: Vec<_> = inventory.entries.iter().map(|entry| entry.logical_name).collect();
    let mut unique = names.clone();
    unique.sort();
    unique.dedup();
    assert_eq!(unique.len(), names.len(), "duplicate logical names: {names:?}");
}

#[test]
fn windows_portable_resolves_beside_the_executable_and_in_data() {
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let package = root.join("TriAevum");
    let data = package.join("data");
    profile(&data.join(common::launch_profile_name()), "", "TriAevum.json", &[]);
    write(&data.join("savedata/system/save.bin"), b"0123456789");
    write(&data.join("savedata/crashes/one.dmp"), b"dump");
    write(&data.join("config/TriAevum.json"), b"{}");
    write(&data.join("config/environments/kokiri.json"), b"{}");
    write(&data.join("config/TriAevum.json.launching"), b"TriAevum launch in progress\n");
    write(&package.join("savestates/state-1.oot3dsav"), b"state");
    write(&data.join("imgui.ini"), b"[Window]");
    write(&package.join("logs/oot3d_native_game.log"), b"log");
    std::fs::create_dir_all(data.join(ACTIVATION_DIRECTORY)).unwrap();
    let env = env_of(vec![("APPDATA", text(&root.join("AppData/Roaming")))]);
    let installation = Installation {
        os: HostOs::Windows,
        layout: for_package(&package, &env, HostOs::Windows).unwrap(),
        data_root: None,
        runtime_dir: package.clone(),
        android: None,
        env: &env,
        sizes: true,
    };
    let inventory = resolved(&root, &installation);
    assert_eq!(inventory.roots.platform, Platform::WindowsPortable);
    assert_eq!(inventory.roots.activation, package);
    assert_eq!(inventory.roots.state, data);
    assert_backup_classes(&inventory);
    assert_entry(&inventory, "save_data", &data.join("savedata"), Origin::Default, Backup::Include);
    // An older profile (without the managed data tree's routing, WP-05C):
    // save states beside the executable, logs in the working directory.
    assert_entry(&inventory, "save_states", &package.join("savestates"), Origin::Legacy, Backup::Include);
    assert_entry(&inventory, "imgui_layout", &data.join("imgui.ini"), Origin::Default, Backup::Include);
    assert_entry(&inventory, "game_logs", &package.join("logs"), Origin::Legacy, Backup::Exclude);
    assert_entry(&inventory, "forge_logs", &data.join("logs"), Origin::Default, Backup::Exclude);
    assert_entry(&inventory, "launch_profile", &data.join(common::launch_profile_name()), Origin::Default, Backup::Exclude);
    assert_entry(&inventory, "title_plugins", &package.join("private-plugins"), Origin::Default, Backup::Exclude);
    let textures = root.join("AppData/Roaming/Azahar");
    assert_entry(
        &inventory,
        "texture_load",
        &textures.join("load/textures/0004000000033600"),
        Origin::Legacy,
        Backup::Optional,
    );
    assert!(!entry(&inventory, "texture_load").managed, "the Azahar folder is the user's, not Forge's");
    assert!(entry(&inventory, "save_data").managed);

    // Crash dumps nest in the save data and stay out of its backup.
    let crashes = entry(&inventory, "crash_dumps");
    assert_eq!(
        (crashes.parent, crashes.backup, crashes.category),
        (Some("save_data"), Backup::Exclude, Category::Logs)
    );
    let save_data = entry(&inventory, "save_data");
    assert_eq!((save_data.size, save_data.files), (14, 2));
    assert_eq!(crashes.size, 4);
    let excluded: Vec<_> = inventory.exclusions_within("save_data").iter().map(|entry| entry.logical_name).collect();
    assert_eq!(excluded, ["crash_dumps"]);
    let excluded: Vec<_> = inventory.exclusions_within("config").iter().map(|entry| entry.logical_name).collect();
    assert_eq!(excluded, ["launch_marker"]);
    assert_eq!(entry(&inventory, "environments").parent, Some("config"));
    assert!(entry(&inventory, "environments").present);
    assert!(!entry(&inventory, "mods").present);
    assert!(inventory.activity.idle());
}

#[test]
fn a_windows_root_profile_is_a_legacy_location_the_runtime_still_reads() {
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let package = root.join("TriAevum");
    // An installation from before the profile moved to data/.
    profile(&package.join(common::launch_profile_name()), "data/", "TriAevum.json", &[]);
    write(&package.join("data/config/TriAevum.json"), b"{}");
    write(&package.join("imgui.ini"), b"[Window] old");
    let env = env_of(vec![]);
    let installation = Installation {
        os: HostOs::Windows,
        layout: for_package(&package, &env, HostOs::Windows).unwrap(),
        data_root: None,
        runtime_dir: package.clone(),
        android: None,
        env: &env,
        sizes: false,
    };
    let inventory = resolved(&root, &installation);
    let launch = entry(&inventory, "launch_profile");
    assert_eq!((launch.origin, launch.active), (Origin::Legacy, true));
    assert_eq!(launch.resolved_path, package.join(common::launch_profile_name()));
    assert_entry(&inventory, "save_data", &package.join("data/savedata"), Origin::Default, Backup::Include);
    // data/ exists, so the runtime writes data/imgui.ini and ignores the old one.
    assert_entry(&inventory, "imgui_layout", &package.join("data/imgui.ini"), Origin::Default, Backup::Include);
    let old = entry(&inventory, "imgui_layout_legacy");
    assert_eq!((old.origin, old.active, old.backup), (Origin::Legacy, false, Backup::Exclude));
    // Without APPDATA the runtime falls back to its working directory.
    assert_entry(
        &inventory,
        "texture_load",
        &package.join("Azahar/load/textures/0004000000033600"),
        Origin::Legacy,
        Backup::Optional,
    );
}

#[test]
fn appimage_activates_in_xdg_data_with_explicit_external_choices() {
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let package = root.join(".mount_TriAevum/usr/lib/triaevum");
    std::fs::create_dir_all(&package).unwrap();
    let home = root.join("home/jdoe");
    let activation = home.join(".local/share").join(forge_catalog::title::ACTIVE.data_namespace);
    let data = activation.join("data");
    profile(&activation.join(common::launch_profile_name()), "data/", "TriAevum.json", &[]);
    let external_states = root.join("external/states");
    let external_pack = root.join("external/pack");
    write_json(&data.join("config/savestates.json"), &json!({"Directory": text(&external_states), "LastState": ""}));
    write_json(
        &data.join("config/TriAevum.json"),
        &json!({"Graphics": {"SchemaVersion": 9, "TexturePacks": {"Azahar": {
            "LoadDirectory": text(&external_pack), "DumpDirectory": ""}}}}),
    );
    write(&external_states.join("state-1.oot3dsav"), b"state");
    write(&data.join("savestates/state-0.oot3dsav"), b"old state");
    let env = env_of(vec![("HOME", text(&home))]);
    let layout = for_package(&package, &env, HostOs::Linux).unwrap();
    assert_eq!(layout.activation, activation);
    let installation = Installation {
        os: HostOs::Linux,
        layout,
        data_root: None,
        runtime_dir: package.clone(),
        android: None,
        env: &env,
        sizes: true,
    };
    let inventory = resolved(&root, &installation);
    assert_eq!(inventory.roots.platform, Platform::AppImage);
    assert_eq!(inventory.roots.state, activation);
    assert_backup_classes(&inventory);
    assert!(inventory.entry("crash_dumps").is_none(), "only Windows product builds write crash dumps");
    assert!(inventory.entry("title_plugins").is_some());
    assert_entry(&inventory, "save_states", &external_states, Origin::Explicit, Backup::Include);
    assert!(!entry(&inventory, "save_states").managed, "an external folder is never Forge's to move");
    let unused = entry(&inventory, "save_states_default");
    assert_eq!(unused.resolved_path, data.join("savestates"));
    assert_eq!((unused.active, unused.backup, unused.files), (false, Backup::Optional, 1));
    assert_entry(&inventory, "texture_load", &external_pack, Origin::Explicit, Backup::Optional);
    // An empty setting keeps the default dump folder.
    assert_entry(
        &inventory,
        "texture_dump",
        &home.join(".local/share/azahar/dump/textures/0004000000033600"),
        Origin::Legacy,
        Backup::Exclude,
    );
    assert_entry(&inventory, "game_logs", &activation.join("logs"), Origin::Legacy, Backup::Exclude);
    assert_entry(&inventory, "imgui_layout", &data.join("imgui.ini"), Origin::Default, Backup::Include);
    assert_entry(
        &inventory,
        "activation_records",
        &activation.join(".forge-activation"),
        Origin::Default,
        Backup::Exclude,
    );
    // Logs and reports get the redacted form.
    let redacted = inventory.to_redacted_value();
    let shown = redacted["roots"]["activation"].as_str().unwrap();
    assert!(shown.contains("<user>") && !shown.contains("jdoe"), "{shown}");
    assert!(inventory.to_value()["roots"]["activation"].as_str().unwrap().contains("jdoe"));
}

#[test]
fn flatpak_activates_in_its_sandbox_and_honours_ship_home_and_environment_overrides() {
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let package = root.join("app/lib/triaevum");
    std::fs::create_dir_all(&package).unwrap();
    let sandbox = root.join("var/app").join(FLATPAK_ID).join("data");
    let activation = sandbox.join(forge_catalog::title::ACTIVE.data_namespace);
    let data = activation.join("data");
    let controls = root.join("elsewhere/controls.json");
    let extra = ["--controls-config".to_string(), text(&controls)];
    profile(&activation.join(common::launch_profile_name()), "data/", "TriAevum.json", &extra);
    let ship_home = root.join("ship-home");
    std::fs::create_dir_all(&ship_home).unwrap();
    let dumps = root.join("dumps");
    let env = env_of(vec![
        ("FLATPAK_ID", FLATPAK_ID.to_string()),
        ("XDG_DATA_HOME", text(&sandbox)),
        ("SHIP_HOME", text(&ship_home)),
        ("OOT3D_AZAHAR_DUMP_DIRECTORY", text(&dumps)),
        ("OOT3D_AZAHAR_TITLE_ID", "0004000000033500".to_string()),
    ]);
    let installation = Installation {
        os: HostOs::Linux,
        layout: for_package(&package, &env, HostOs::Linux).unwrap(),
        data_root: None,
        runtime_dir: package.clone(),
        android: None,
        env: &env,
        sizes: false,
    };
    let inventory = resolved(&root, &installation);
    assert_eq!(inventory.roots.platform, Platform::Flatpak);
    assert_eq!(inventory.roots.activation, activation);
    assert_backup_classes(&inventory);
    assert_entry(&inventory, "save_data", &data.join("savedata"), Origin::Default, Backup::Include);
    assert_entry(&inventory, "save_states", &data.join("savestates"), Origin::Default, Backup::Include);
    // SHIP_HOME moves the runtime's application folder (logs, imgui.ini).
    assert_entry(&inventory, "game_logs", &ship_home.join("logs"), Origin::Explicit, Backup::Exclude);
    assert_entry(&inventory, "imgui_layout", &ship_home.join("imgui.ini"), Origin::Explicit, Backup::Include);
    assert!(!entry(&inventory, "game_logs").managed);
    assert_entry(&inventory, "texture_dump", &dumps, Origin::Explicit, Backup::Exclude);
    assert_entry(
        &inventory,
        "texture_load",
        &sandbox.join("azahar/load/textures/0004000000033500"),
        Origin::Legacy,
        Backup::Optional,
    );
    let controls = entry(&inventory, "controls_config");
    assert_eq!(controls.origin, Origin::Explicit);
    assert_eq!(controls.resolved_path, resolve(&root.join("elsewhere/controls.json")).unwrap());
}

fn android_installation<'a>(
    root: &Path,
    app: Option<AndroidApp>,
    env: &'a dyn Fn(&str) -> Option<String>,
) -> Installation<'a> {
    Installation {
        os: HostOs::Android,
        layout: HostLayout {
            package: root.join("data/org.triaevum.android/files/forge-package"),
            activation: root.join("external/Android/data/org.triaevum.android/files"),
            use_file_portal: false,
        },
        data_root: None,
        runtime_dir: root.join("app/lib/arm64"),
        android: app,
        env,
        sizes: true,
    }
}

#[test]
fn android_resolves_app_storage_and_touch_preferences() {
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let activation = root.join("external/Android/data/org.triaevum.android/files");
    let data = activation.join("data");
    let app_data = root.join("data/org.triaevum.android");
    profile(&activation.join("TriAevum.android.launch.json"), "data/", "TriAevum.android.json", &[]);
    write(&app_data.join("shared_prefs/org.triaevum.android_preferences.xml"), b"<map/>");
    write(&app_data.join("files/diagnostics/game-stages.txt"), b"stage");
    write(&activation.join("logs/native-stderr.log"), b"err");
    let app = AndroidApp { app_data_dir: app_data.clone(), package_name: "org.triaevum.android".into() };
    let no_env = |_: &str| None;
    let inventory = resolved(&root, &android_installation(&root, Some(app), &no_env));
    assert_eq!(inventory.roots.platform, Platform::Android);
    assert_eq!(inventory.roots.state, activation);
    assert_backup_classes(&inventory);
    assert_entry(
        &inventory,
        "runtime_config",
        &data.join("config/TriAevum.android.json"),
        Origin::Default,
        Backup::Include,
    );
    assert_entry(&inventory, "save_states", &data.join("savestates"), Origin::Default, Backup::Include);
    assert_entry(
        &inventory,
        "launch_profile",
        &activation.join("TriAevum.android.launch.json"),
        Origin::Default,
        Backup::Exclude,
    );
    assert_entry(
        &inventory,
        "touch_preferences",
        &app_data.join("shared_prefs/org.triaevum.android_preferences.xml"),
        Origin::Default,
        Backup::Include,
    );
    let touch = entry(&inventory, "touch_preferences");
    assert!(touch.present && touch.managed);
    assert_entry(
        &inventory,
        "android_diagnostics",
        &app_data.join("files/diagnostics"),
        Origin::Default,
        Backup::Exclude,
    );
    // Android's runtime writes D/logs; the earlier A/logs stays listed.
    assert_entry(&inventory, "game_logs", &data.join("logs"), Origin::Default, Backup::Exclude);
    let earlier = entry(&inventory, "game_logs_legacy");
    assert_eq!(earlier.resolved_path, activation.join("logs"));
    assert_eq!((earlier.origin, earlier.active, earlier.backup), (Origin::Legacy, false, Backup::Exclude));
    assert!(inventory.entry("title_plugins").is_none());
    assert!(inventory.entry("android_title_file").is_some());
    // The game process has no XDG_DATA_HOME or HOME: its working directory.
    assert_entry(
        &inventory,
        "texture_load",
        &activation.join("Azahar/load/textures/0004000000033600"),
        Origin::Legacy,
        Backup::Optional,
    );
    // Without the app's folders the inventory still resolves the rest.
    let inventory = resolved(&root, &android_installation(&root, None, &no_env));
    assert!(inventory.entry("touch_preferences").is_none());
    assert!(inventory.entry("save_data").is_some());
}

// UX plan WP-05C: a profile of the managed data tree routes logs, dumps,
// save states and texture folders into D; the earlier locations that still
// hold files are inactive legacy rows; save states stay where they are until
// the runtime records their move; an existing Azahar pack keeps loading.
#[test]
fn a_managed_profile_routes_the_data_tree_and_lists_earlier_locations() {
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let package = root.join("TriAevum");
    let data = package.join("data");
    let at = |relative: &str| format!("${{profile_dir}}/{relative}");
    let extra = [
        "--log-directory".to_string(),
        at("logs"),
        "--crash-directory".into(),
        at("logs/crashes"),
        "--savestate-directory".into(),
        at("savestates"),
        "--texture-load-directory".into(),
        at("textures/load/0004000000033500"),
        "--texture-dump-directory".into(),
        at("textures/dump/0004000000033500"),
        "--title-id".into(),
        "0004000000033500".into(),
    ];
    profile(&data.join(common::launch_profile_name()), "", "TriAevum.json", &extra);
    write(&data.join("config/TriAevum.json"), b"{}");
    write(&data.join("savedata/crashes/one.dmp"), b"dump");
    write(&package.join("savestates/state-1.oot3dsav"), b"state");
    write(&package.join("logs/oot3d_native_game.log"), b"old log");
    let app_data = root.join("AppData/Roaming");
    let env = env_of(vec![("APPDATA", text(&app_data))]);
    let installation = Installation {
        os: HostOs::Windows,
        layout: for_package(&package, &env, HostOs::Windows).unwrap(),
        data_root: None,
        runtime_dir: package.clone(),
        android: None,
        env: &env,
        sizes: true,
    };

    // Before the game's first start with the new profile: the states are
    // still only in the earlier folder, which a backup must take.
    let inventory = resolved(&root, &installation);
    assert_backup_classes(&inventory);
    assert_entry(&inventory, "save_states", &package.join("savestates"), Origin::Legacy, Backup::Include);
    assert!(entry(&inventory, "save_states").active);
    assert!(inventory.entry("save_states_legacy").is_none());
    assert_entry(&inventory, "game_logs", &data.join("logs"), Origin::Default, Backup::Exclude);
    let earlier = entry(&inventory, "game_logs_legacy");
    assert_eq!((earlier.origin, earlier.active), (Origin::Legacy, false));
    assert_eq!(earlier.resolved_path, package.join("logs"));
    assert_entry(&inventory, "crash_dumps", &data.join("logs/crashes"), Origin::Default, Backup::Exclude);
    let earlier = entry(&inventory, "crash_dumps_legacy");
    assert_eq!((earlier.origin, earlier.active, earlier.backup), (Origin::Legacy, false, Backup::Exclude));
    let excluded: Vec<_> = inventory.exclusions_within("save_data").iter().map(|entry| entry.logical_name).collect();
    assert_eq!(excluded, ["crash_dumps_legacy"], "earlier dumps stay out of save backups");
    // The installed USA title names the texture folders.
    let managed = data.join("textures");
    assert_entry(
        &inventory,
        "texture_load",
        &managed.join("load/0004000000033500"),
        Origin::Default,
        Backup::Optional,
    );
    assert_entry(
        &inventory,
        "texture_dump",
        &managed.join("dump/0004000000033500"),
        Origin::Default,
        Backup::Exclude,
    );
    assert!(entry(&inventory, "texture_load").managed);

    // After the runtime recorded the move: the managed folder is active and
    // the earlier one is an inactive legacy row, offered in a backup.
    write(&data.join("savestates/state-1.oot3dsav"), b"state");
    write_json(
        &data.join("config/migrations.json"),
        &json!({"format": "triaevum_migrations_v1", "migrations": [{
            "id": "savestates_to_data_folder", "status": "completed",
            "source": text(&package.join("savestates")), "destination": text(&data.join("savestates"))}]}),
    );
    let inventory = resolved(&root, &installation);
    assert_entry(&inventory, "save_states", &data.join("savestates"), Origin::Default, Backup::Include);
    let earlier = entry(&inventory, "save_states_legacy");
    assert_eq!(earlier.resolved_path, package.join("savestates"));
    assert_eq!((earlier.origin, earlier.active, earlier.backup), (Origin::Legacy, false, Backup::Optional));
    assert_entry(&inventory, "migrations", &data.join("config/migrations.json"), Origin::Default, Backup::Include);

    // A pack already in Azahar's folder under the installed title keeps loading.
    let azahar = app_data.join("Azahar/load/textures/0004000000033500");
    std::fs::create_dir_all(&azahar).unwrap();
    let inventory = resolved(&root, &installation);
    assert_entry(&inventory, "texture_load", &azahar, Origin::Legacy, Backup::Optional);
    assert_entry(
        &inventory,
        "texture_dump",
        &managed.join("dump/0004000000033500"),
        Origin::Default,
        Backup::Exclude,
    );
    assert!(inventory.entry("texture_load_legacy").is_none());

    // UX plan WP-07: once a pack is imported into the managed folder it wins;
    // the Azahar pack stays listed as an inactive legacy row, untouched.
    write(&azahar.join("tex1_8x8_00000000000000AA_13_mip0.png"), b"png");
    write(&managed.join("load/0004000000033500/UI/tex1_8x8_00000000000000AA_13_mip0.png"), b"png");
    let inventory = resolved(&root, &installation);
    assert_backup_classes(&inventory);
    let imported = managed.join("load/0004000000033500");
    assert_entry(&inventory, "texture_load", &imported, Origin::Default, Backup::Optional);
    let earlier = entry(&inventory, "texture_load_legacy");
    assert_eq!((earlier.origin, earlier.active, earlier.backup), (Origin::Legacy, false, Backup::Exclude));
    assert_eq!(earlier.resolved_path, azahar);
}

#[test]
fn an_explicit_data_folder_makes_its_locations_explicit_and_external() {
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let package = root.join("TriAevum");
    let elsewhere = root.join("SavedGames/TriAevum-data");
    std::fs::create_dir_all(&package).unwrap();
    let env = env_of(vec![]);
    let installation = Installation {
        os: HostOs::Linux,
        layout: for_package(&package, &env, HostOs::Linux).unwrap(),
        data_root: Some(elsewhere.clone()),
        runtime_dir: package.clone(),
        android: None,
        env: &env,
        sizes: false,
    };
    let inventory = resolved(&root, &installation);
    assert_eq!(inventory.roots.platform, Platform::LinuxPortable);
    assert_entry(&inventory, "save_data", &elsewhere.join("savedata"), Origin::Explicit, Backup::Include);
    assert_entry(&inventory, "mods", &elsewhere.join("mods"), Origin::Explicit, Backup::Optional);
    assert!(!entry(&inventory, "save_data").managed);
    assert_entry(&inventory, "launch_profile", &package.join(common::launch_profile_name()), Origin::Default, Backup::Exclude);
}

#[test]
fn a_running_game_shows_in_the_inventory_with_its_explanation() {
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let package = root.join("TriAevum");
    std::fs::create_dir_all(package.join(ACTIVATION_DIRECTORY)).unwrap();
    let env = env_of(vec![]);
    let installation = Installation {
        os: HostOs::Linux,
        layout: for_package(&package, &env, HostOs::Linux).unwrap(),
        data_root: None,
        runtime_dir: package.clone(),
        android: None,
        env: &env,
        sizes: false,
    };
    let game = std::fs::File::create(session_lock_path(&package, HostOs::Linux)).unwrap();
    game.lock().unwrap();
    let inventory = resolve_inventory(&installation).unwrap();
    assert!(inventory.activity.game_running && !inventory.activity.forge_running);
    let value = inventory.to_value();
    assert_eq!(value["activity"]["game_running"], true);
    assert_eq!(value["activity"]["explanation"]["id"], "session.game_running");
    drop(game);
    let value = resolve_inventory(&installation).unwrap().to_value();
    assert_eq!(value["activity"], json!({"game_running": false, "forge_running": false}));
}

/// The inventory of an installation the real import made resolves the
/// launch profile Forge wrote, through the C ABI too.
#[test]
fn a_real_installation_resolves_its_launch_profile_through_the_abi() {
    let fixture = Fixture::new();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    let inventory = inventory_of(&InventoryRequest::package(&fixture.package)).unwrap();
    let data = fixture.data();
    assert_entry(&inventory, "save_data", &data.join("savedata"), Origin::Default, Backup::Include);
    assert_entry(&inventory, "runtime_config", &data.join("config/TriAevum.json"), Origin::Default, Backup::Include);
    assert_entry(&inventory, "launch_profile", &fixture.launch_profile(), Origin::Default, Backup::Exclude);
    assert!(entry(&inventory, "launch_profile").present);
    assert!(entry(&inventory, "save_data").present);
    assert!(entry(&inventory, "active_title").present);
    assert!(entry(&inventory, "titles").present && entry(&inventory, "titles").size > 0);
    assert!(entry(&inventory, "sources").present);

    let request = json!({"format": "triaevum_forge_inventory_v1", "package_root": text(&fixture.package)});
    let bytes = serde_json::to_vec(&request).unwrap();
    let buffer = unsafe { TriAevumForgeInventory(bytes.as_ptr(), bytes.len()) };
    let answer: Value = serde_json::from_slice(unsafe { std::slice::from_raw_parts(buffer.data, buffer.len) }).unwrap();
    unsafe { TriAevumForgeFreeBuffer(buffer) };
    assert_eq!(answer, inventory.to_value());
    assert_eq!(answer["status"], "ok");
    assert_eq!(answer["entries"][0]["logical_name"], "save_data");
}

#[test]
fn a_real_android_installation_through_the_json_request() {
    let fixture = Fixture::android();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    let (native, profile_root) = fixture.android.clone().unwrap();
    let app_data = profile_root.parent().unwrap().join("app-data");
    let request = json!({"format": "triaevum_forge_inventory_v1", "package_root": text(&fixture.package),
        "android": {"native_library_dir": text(&native), "profile_root": text(&profile_root),
                    "app_data_dir": text(&app_data), "package_name": "org.triaevum.android"},
        "sizes": false});
    let answer: Value = serde_json::from_slice(&inventory_bytes(&serde_json::to_vec(&request).unwrap())).unwrap();
    assert_eq!(answer["status"], "ok", "{answer}");
    assert_eq!(answer["platform"], "android");
    let find = |name: &str| -> Value {
        answer["entries"].as_array().unwrap().iter().find(|entry| entry["logical_name"] == name).cloned().unwrap()
    };
    let data = fixture.data();
    let resolved_text = |path: PathBuf| text(&resolve(&path).unwrap());
    assert_eq!(find("runtime_config")["path"], resolved_text(data.join("config").join("TriAevum.android.json")));
    assert_eq!(find("runtime_config")["origin"], "default");
    assert_eq!(find("save_data")["present"], true);
    let preferences = app_data.join("shared_prefs").join("org.triaevum.android_preferences.xml");
    assert_eq!(find("touch_preferences")["path"], resolved_text(preferences));
    assert_eq!(find("save_data")["size"], 0, "sizes were not asked for");
}

#[test]
fn unusable_requests_fail_with_an_explanation() {
    for request in [
        json!({"format": "other"}),
        json!({"format": "triaevum_forge_inventory_v1", "android": 3}),
        json!({"format": "triaevum_forge_inventory_v1", "android": {"native_library_dir": "x", "profile_root": "y",
               "app_data_dir": "z"}}),
        json!({"format": "triaevum_forge_inventory_v1", "sizes": "yes"}),
    ] {
        let answer: Value = serde_json::from_slice(&inventory_bytes(&serde_json::to_vec(&request).unwrap())).unwrap();
        assert_eq!(answer["status"], "failed", "{request}");
        assert_eq!(answer["explanation"]["id"], "request.invalid", "{request}");
    }
    let answer: Value = serde_json::from_slice(&inventory_bytes(b"not json")).unwrap();
    assert_eq!(answer["explanation"]["id"], "request.invalid");
}

#[test]
fn the_entry_paths_are_absolute_and_every_category_is_present() {
    let directory = tempfile::tempdir().unwrap();
    let root: PathBuf = resolve(directory.path()).unwrap();
    let package = root.join("TriAevum");
    std::fs::create_dir_all(&package).unwrap();
    let env = env_of(vec![]);
    let installation = Installation {
        os: HostOs::Windows,
        layout: for_package(&package, &env, HostOs::Windows).unwrap(),
        data_root: None,
        runtime_dir: package.clone(),
        android: None,
        env: &env,
        sizes: false,
    };
    let inventory = resolved(&root, &installation);
    for entry in &inventory.entries {
        assert!(entry.resolved_path.is_absolute(), "{}", entry.logical_name);
    }
    for category in [
        Category::Saves,
        Category::SaveStates,
        Category::Settings,
        Category::Textures,
        Category::Enhancements,
        Category::Generated,
        Category::Logs,
        Category::State,
    ] {
        assert!(inventory.entries.iter().any(|entry| entry.category == category), "{}", category.code());
    }
}
