// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Backup and restore on synthetic installations (UX plan WP-05B): the zip
//! holds exactly the Include set; tampered backups are refused before
//! anything is written; a restore killed after two categories recovers the
//! previous version and keeps the snapshot; nothing runs while the game
//! holds its session lock; another platform's backup restores only the
//! portable subset. And, on installations the real import made: removing
//! the prepared game, clearing the renderer cache, rebuilding from the kept
//! ROM data and the interrupted-restore state.

mod common;

use std::collections::BTreeSet;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use common::{FakeRuntime, Fixture, RECIPE};
use forge_base::hashing::sha256_hex;
use forge_base::{CancelToken, ErrorKind, Reason};
use forge_core::backup::{backup_plan, create_backup, open_backup, BackupRequest, MANIFEST};
use forge_core::data_actions::{
    clear_renderer_cache, prepared_game_plan, rebuild_prepared_assets, remove_prepared_game, renderer_cache_plan,
};
use forge_core::explain::explain;
use forge_core::install::InstallTarget;
use forge_core::inventory::{resolve_inventory, Installation, Inventory};
use forge_core::layout::{for_package, HostOs};
use forge_core::restore::{pending_restore, recover_pending, restore, restore_preview, RestoreOptions};
use forge_core::session::{session_lock_path, ACTIVATION_DIRECTORY};
use forge_core::state::{inspect, InspectRequest, InstallationState, RepairReason, PREPARED_GAME_REMOVED};
use forge_prepare::paths::resolve;
use serde_json::{json, Value};

const CHILD: &str = "TRIAEVUM_RESTORE_ABORT_CHILD";
const CHILD_BACKUP: &str = "TRIAEVUM_RESTORE_ABORT_BACKUP";

fn write(path: &Path, bytes: &[u8]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, bytes).unwrap();
}

fn write_json(path: &Path, value: &Value) {
    write(path, value.to_string().as_bytes());
}

fn text(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

/// Every file and folder below `root` with its digest.
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

/// A launch profile as Forge writes it.
fn profile(path: &Path, data_reference: &str, config: &str) {
    let at = |relative: &str| format!("${{profile_dir}}/{data_reference}{relative}");
    let arguments = vec![
        "--config".to_string(),
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
    write_json(path, &json!({"format": "oot3d_native_game_launch_profile_v1", "arguments": arguments}));
}

/// A synthetic installation on disk.
struct Site {
    root: PathBuf,
    package: PathBuf,
    activation: PathBuf,
    data: PathBuf,
    os: HostOs,
    env: Vec<(&'static str, String)>,
}

impl Site {
    fn inventory(&self) -> Inventory {
        let pairs = self.env.clone();
        let env = move |name: &str| pairs.iter().find(|(key, _)| *key == name).map(|(_, value)| value.clone());
        let installation = Installation {
            os: self.os,
            layout: for_package(&self.package, &env, self.os).unwrap(),
            data_root: None,
            runtime_dir: self.package.clone(),
            android: None,
            env: &env,
            sizes: true,
        };
        let inventory = resolve_inventory(&installation).unwrap();
        assert_eq!(inventory.roots.activation, self.activation);
        inventory
    }

    fn recipes(&self) {
        let recipe = json!({"id": RECIPE, "title": "Fixture", "region": "EUR"});
        write_json(
            &self.package.join("recipes/oot3d.json"),
            &json!({"format": "triaevum_supported_revisions_v1", "recipes": [recipe]}),
        );
    }
}

/// A Windows portable installation with a file in every category.
fn windows_site_at(root: &Path) -> Site {
    let package = root.join("TriAevum");
    let data = package.join("data");
    Site {
        root: root.to_path_buf(),
        activation: package.clone(),
        data,
        package,
        os: HostOs::Windows,
        env: vec![("APPDATA", text(&root.join("AppData/Roaming")))],
    }
}

fn populate_windows(site: &Site) {
    let (package, data) = (&site.package, &site.data);
    site.recipes();
    profile(&data.join(common::launch_profile_name()), "", "TriAevum.json");
    write(&data.join("savedata/system/save00.bin"), b"save-a");
    write(&data.join("savedata/system/system.dat"), b"system");
    write(&data.join("savedata/crashes/one.dmp"), b"dump");
    write(&package.join("savestates/state-1.oot3dsav"), b"OOT3DSV\0one");
    write(&package.join("savestates/state-1.png"), b"thumbnail");
    write_json(&data.join("config/TriAevum.json"), &json!({"Graphics": {"SchemaVersion": 9}, "windows": true}));
    write(&data.join("config/topscreen_ui.json"), b"{\"layout\": \"windows\"}");
    write(&data.join("config/oot3d_controls.json"), b"{\"keyboard\": true}");
    write(&data.join("config/game_language.json"), b"{\"selected\": \"it\"}");
    write(&data.join("config/romfs_mods.json"), b"{\"enabled\": []}");
    let last = package.join("savestates").join("state-1.oot3dsav");
    write_json(&data.join("config/savestates.json"), &json!({"Directory": "", "LastState": text(&last)}));
    write(&data.join("config/environments/kokiri.json"), b"{\"sun\": 1}");
    write(&data.join("config/TriAevum.json.launching"), b"TriAevum launch in progress\n");
    write(&data.join("imgui.ini"), b"[Window][F1]");
    write(&package.join("logs/oot3d_native_game.log"), b"game log");
    write(&data.join("logs/forge.log"), b"forge log");
    write(&data.join("titles/t/module.tam"), b"title");
    write(&data.join("sources/s/code.bin"), b"code");
    write(&data.join("cache/renderer/pipelines.bin"), b"cache");
    write(&data.join("shader-seeds/seed.bin"), b"seed");
    write(&data.join("interior-sun/sun.bin"), b"sun");
    write(&data.join("mods/mod.bin"), b"mod");
    write(&data.join("downloads/pack.zip"), b"pack");
    let textures = site.root.join("AppData/Roaming/Azahar");
    write(&textures.join("load/textures/0004000000033600/tex.png"), b"texture");
    write(&textures.join("dump/textures/0004000000033600/dump.png"), b"dumped");
    write_json(&data.join("active-title.json"), &json!({"format": "triaevum_active_title_v1", "recipe": RECIPE}));
    write(&data.join("runtime-state.json"), b"{}");
    write(&package.join("private-plugins/x/plugin.dll"), b"plugin");
    std::fs::create_dir_all(data.join(ACTIVATION_DIRECTORY)).unwrap();
}

fn windows_site(directory: &tempfile::TempDir) -> Site {
    let site = windows_site_at(&resolve(directory.path()).unwrap());
    populate_windows(&site);
    site
}

/// A Linux AppImage installation with its own settings.
fn appimage_site(directory: &tempfile::TempDir) -> Site {
    let root = resolve(directory.path()).unwrap();
    let package = root.join(".mount_TriAevum/usr/lib/triaevum");
    let home = root.join("home/player");
    let activation = home.join(".local/share/TriAevum");
    let data = activation.join("data");
    let site = Site { root, package, activation, data, os: HostOs::Linux, env: vec![("HOME", text(&home))] };
    std::fs::create_dir_all(&site.package).unwrap();
    site.recipes();
    let data = &site.data;
    profile(&site.activation.join(common::launch_profile_name()), "data/", "TriAevum.json");
    write(&data.join("savedata/system/save00.bin"), b"linux save");
    write_json(&data.join("config/TriAevum.json"), &json!({"linux": true}));
    write(&data.join("config/oot3d_controls.json"), b"{\"gamepad\": true}");
    write(&data.join("config/topscreen_ui.json"), b"{\"layout\": \"linux\"}");
    write_json(&data.join("config/savestates.json"), &json!({"Directory": "", "LastState": ""}));
    write(&data.join("imgui.ini"), b"[Window][linux]");
    site
}

fn members(zip: &Path) -> BTreeSet<String> {
    let archive = zip::ZipArchive::new(std::fs::File::open(zip).unwrap()).unwrap();
    archive.file_names().map(|name| name.unwrap().into_owned()).collect()
}

fn backup_of(site: &Site, destination: &Path, optional: &[&str]) -> Value {
    let request = BackupRequest {
        destination: destination.to_path_buf(),
        optional: optional.iter().map(|name| name.to_string()).collect(),
        touch_preferences: None,
    };
    create_backup(&site.inventory(), &request, &mut |_, _, _| {}, &CancelToken::new()).unwrap()
}

fn restore_into(site: &Site, backup: &Path) -> forge_base::Result<Value> {
    restore(&site.inventory(), backup, &RestoreOptions::default(), &mut |_, _, _| {}, &CancelToken::new())
}

const WINDOWS_INCLUDE: &[&str] = &[
    "files/save_data/system/save00.bin",
    "files/save_data/system/system.dat",
    "files/save_states/state-1.oot3dsav",
    "files/save_states/state-1.png",
    "files/config/TriAevum.json",
    "files/config/topscreen_ui.json",
    "files/config/oot3d_controls.json",
    "files/config/game_language.json",
    "files/config/romfs_mods.json",
    "files/config/savestates.json",
    "files/config/environments/kokiri.json",
    "files/imgui_layout",
];

#[test]
fn a_backup_holds_exactly_the_include_set() {
    let directory = tempfile::tempdir().unwrap();
    let site = windows_site(&directory);
    let out = tempfile::tempdir().unwrap();
    let zip = out.path().join("backup.zip");
    let before = tree(&site.root);
    let result = backup_of(&site, &zip, &[]);
    assert_eq!(result["files"], WINDOWS_INCLUDE.len());

    let mut expected: BTreeSet<String> = WINDOWS_INCLUDE.iter().map(|name| name.to_string()).collect();
    expected.insert(MANIFEST.to_string());
    assert_eq!(members(&zip), expected);
    // Every excluded or unchosen file of the installation stays out.
    let included_sources: BTreeSet<String> = [
        "TriAevum/data/savedata/system/save00.bin",
        "TriAevum/data/savedata/system/system.dat",
        "TriAevum/savestates/state-1.oot3dsav",
        "TriAevum/savestates/state-1.png",
        "TriAevum/data/config/TriAevum.json",
        "TriAevum/data/config/topscreen_ui.json",
        "TriAevum/data/config/oot3d_controls.json",
        "TriAevum/data/config/game_language.json",
        "TriAevum/data/config/romfs_mods.json",
        "TriAevum/data/config/savestates.json",
        "TriAevum/data/config/environments/kokiri.json",
        "TriAevum/data/imgui.ini",
    ]
    .iter()
    .map(|name| name.to_string())
    .collect();
    let left_out: Vec<String> = before
        .iter()
        .filter(|(_, digest)| digest != "dir")
        .map(|(name, _)| name.clone())
        .filter(|name| !included_sources.contains(name))
        .collect();
    for name in [
        "TriAevum/data/savedata/crashes/one.dmp",
        "TriAevum/data/config/TriAevum.json.launching",
        "TriAevum/logs/oot3d_native_game.log",
        "TriAevum/data/logs/forge.log",
        "TriAevum/data/titles/t/module.tam",
        "TriAevum/data/sources/s/code.bin",
        "TriAevum/data/cache/renderer/pipelines.bin",
        "TriAevum/data/mods/mod.bin",
        "TriAevum/data/downloads/pack.zip",
        "AppData/Roaming/Azahar/load/textures/0004000000033600/tex.png",
        "AppData/Roaming/Azahar/dump/textures/0004000000033600/dump.png",
        "TriAevum/data/active-title.json",
        "TriAevum/private-plugins/x/plugin.dll",
    ] {
        assert!(left_out.contains(&name.to_string()), "{name} should be a file left out");
    }
    let opened = open_backup(&zip, None, &CancelToken::new()).unwrap();
    let digests: BTreeSet<String> = opened.manifest.entries.iter().map(|entry| entry.sha256.clone()).collect();
    for name in &left_out {
        let bytes = std::fs::read(site.root.join(name)).unwrap();
        // The lock files hold a single byte; no backed-up file is that small here.
        assert!(bytes.len() < 2 || !digests.contains(&sha256_hex(&bytes)) || name.ends_with(".lock"), "{name} leaked");
    }
    assert_eq!(opened.manifest.recipe.as_deref(), Some(RECIPE));
    assert_eq!(opened.manifest.platform.code(), "windows_portable");
    // Only the session locks changed (quiescence takes them).
    let after: Vec<_> = tree(&site.root).into_iter().filter(|(name, _)| !name.contains(".forge-activation")).collect();
    let before: Vec<_> = before.into_iter().filter(|(name, _)| !name.contains(".forge-activation")).collect();
    assert_eq!(after, before, "a backup writes nothing into the installation");

    // Decision 2: mods, downloads and the texture pack only when chosen.
    let plan = backup_plan(&site.inventory(), &[], None).unwrap();
    let offered: BTreeSet<&str> = plan.optional.iter().map(|(name, ..)| *name).collect();
    assert!(offered.is_superset(&["mods", "downloads", "texture_load"].into_iter().collect()));
    assert!(plan.optional.iter().all(|(.., chosen)| !chosen));
    let zip = out.path().join("with-mods.zip");
    backup_of(&site, &zip, &["mods", "downloads", "texture_load"]);
    let with = members(&zip);
    for name in ["files/mods/mod.bin", "files/downloads/pack.zip", "files/texture_load/tex.png"] {
        assert!(with.contains(name), "{name}");
    }
    assert!(!with.iter().any(|name| name.contains("dump.png") || name.contains("crashes")));
}

#[test]
fn android_backups_carry_the_touch_preferences_the_app_serializes() {
    let directory = tempfile::tempdir().unwrap();
    let root = resolve(directory.path()).unwrap();
    let activation = root.join("external/files");
    let app_data = root.join("app-data");
    profile(&activation.join("TriAevum.android.launch.json"), "data/", "TriAevum.android.json");
    write(&activation.join("data/savedata/system/save00.bin"), b"save");
    write(&activation.join("data/config/TriAevum.android.json"), b"{}");
    write(&activation.join("data/imgui.ini"), b"[Window]");
    write(&app_data.join("shared_prefs/org.triaevum.android_preferences.xml"), b"<map/>");
    let no_env = |_: &str| None;
    let installation = Installation {
        os: HostOs::Android,
        layout: forge_core::layout::HostLayout {
            package: root.join("package"),
            activation: activation.clone(),
            use_file_portal: false,
        },
        data_root: None,
        runtime_dir: root.join("lib"),
        android: Some(forge_core::inventory::AndroidApp {
            app_data_dir: app_data,
            package_name: "org.triaevum.android".into(),
        }),
        env: &no_env,
        sizes: false,
    };
    let inventory = resolve_inventory(&installation).unwrap();
    assert!(backup_plan(&inventory, &[], None).unwrap().needs_touch_preferences);
    let touch = json!({"touch_opacity": 0.6, "touch_layout": "compact"});
    let request = BackupRequest {
        destination: root.join("backup.zip"),
        optional: Vec::new(),
        touch_preferences: touch.as_object().cloned(),
    };
    create_backup(&inventory, &request, &mut |_, _, _| {}, &CancelToken::new()).unwrap();
    let names = members(&root.join("backup.zip"));
    for name in ["files/touch_preferences", "files/imgui_layout", "files/config/TriAevum.android.json"] {
        assert!(names.contains(name), "{name}: {names:?}");
    }
    assert!(!names.iter().any(|name| name.contains("shared_prefs")), "the XML itself is the app's");
    let mut archive = zip::ZipArchive::new(std::fs::File::open(root.join("backup.zip")).unwrap()).unwrap();
    let mut stored = String::new();
    archive.by_name("files/touch_preferences").unwrap().read_to_string(&mut stored).unwrap();
    assert_eq!(serde_json::from_str::<Value>(&stored).unwrap(), touch);
    // Restoring on the same platform hands them back to the app.
    let result = restore(
        &inventory,
        &root.join("backup.zip"),
        &RestoreOptions::default(),
        &mut |_, _, _| {},
        &CancelToken::new(),
    )
    .unwrap();
    assert_eq!(result["touch_preferences"], touch);
}

fn read_text(path: &Path) -> String {
    std::fs::read_to_string(path).unwrap()
}

fn read_json(path: &Path) -> Value {
    serde_json::from_str(&read_text(path)).unwrap()
}

/// No `.restore-*` sibling is left anywhere.
fn assert_no_siblings(root: &Path) {
    for (name, _) in tree(root) {
        assert!(!name.contains(".restore-"), "left behind: {name}");
    }
}

#[test]
fn a_restore_brings_back_the_backup_and_keeps_the_snapshot_and_excluded_files() {
    let directory = tempfile::tempdir().unwrap();
    let site = windows_site(&directory);
    let out = tempfile::tempdir().unwrap();
    let zip = out.path().join("backup.zip");
    backup_of(&site, &zip, &[]);
    let original = tree(&site.root);
    let (package, data) = (&site.package, &site.data);
    write(&data.join("savedata/system/save00.bin"), b"save-b");
    write(&data.join("savedata/system/save01.bin"), b"new slot");
    write(&data.join("savedata/crashes/one.dmp"), b"dump2");
    std::fs::remove_file(package.join("savestates/state-1.png")).unwrap();
    write(&data.join("config/topscreen_ui.json"), b"{\"layout\": \"changed\"}");

    let preview = restore_preview(&site.inventory(), &zip, &RestoreOptions::default(), &CancelToken::new()).unwrap();
    let value = preview.to_value();
    assert_eq!(value["same_platform"], true);
    let group = |name: &str| value["groups"].as_array().unwrap().iter().find(|g| g["name"] == name).cloned().unwrap();
    assert_eq!((group("save_data")["replaced"].as_u64(), group("save_data")["removed"].as_u64()), (Some(1), Some(1)));
    assert_eq!(group("save_data")["kept"], 1, "the crash dump is kept");
    assert_eq!(group("save_states")["added"], 1);
    assert_eq!(tree(&site.root).len(), original.len(), "a preview writes nothing");

    let result = restore_into(&site, &zip).unwrap();
    assert_eq!(read_text(&data.join("savedata/system/save00.bin")), "save-a");
    assert!(!data.join("savedata/system/save01.bin").exists());
    assert_eq!(read_text(&data.join("savedata/crashes/one.dmp")), "dump2", "excluded files stay as they are");
    assert_eq!(read_text(&package.join("savestates/state-1.png")), "thumbnail");
    assert_eq!(read_text(&data.join("config/topscreen_ui.json")), "{\"layout\": \"windows\"}");
    assert!(data.join("config/TriAevum.json.launching").exists(), "the launch marker is kept");
    let states = read_json(&data.join("config/savestates.json"));
    assert_eq!(states["Directory"], "");
    assert_eq!(states["LastState"], text(&package.join("savestates").join("state-1.oot3dsav")));
    // Everything else is as it was when the backup was made.
    let now: Vec<_> = tree(&site.root)
        .into_iter()
        .filter(|(name, _)| !name.starts_with("TriAevum/data/backups") && !name.contains(".forge-activation"))
        .filter(|(name, _)| !name.ends_with("savestates.json") && !name.ends_with("one.dmp"))
        .collect();
    let then: Vec<_> = original
        .into_iter()
        .filter(|(name, _)| !name.contains(".forge-activation"))
        .filter(|(name, _)| !name.ends_with("savestates.json") && !name.ends_with("one.dmp"))
        .collect();
    assert_eq!(now, then);
    // The snapshot holds what the restore replaced or removed.
    let snapshot = PathBuf::from(result["snapshot"].as_str().unwrap());
    assert_eq!(read_text(&snapshot.join("save_data/system/save00.bin")), "save-b");
    assert_eq!(read_text(&snapshot.join("save_data/system/save01.bin")), "new slot");
    assert_eq!(read_text(&snapshot.join("config/topscreen_ui.json")), "{\"layout\": \"changed\"}");
    let journal = read_json(&snapshot.parent().unwrap().join("journal.json"));
    assert_eq!(journal["state"], "complete");
    assert!(pending_restore(data).is_none());
    assert_no_siblings(&site.root);
}

/// Rewrites a valid backup through `edit` (manifest and members).
fn tampered(source: &Path, target: &Path, edit: impl FnOnce(&mut Value, &mut Vec<(String, Vec<u8>)>)) {
    let mut archive = zip::ZipArchive::new(std::fs::File::open(source).unwrap()).unwrap();
    let mut manifest = Value::Null;
    let mut files = Vec::new();
    for index in 0..archive.len() {
        let mut member = archive.by_index(index).unwrap();
        let mut bytes = Vec::new();
        member.read_to_end(&mut bytes).unwrap();
        let name = member.name().unwrap().into_owned();
        if name == MANIFEST {
            manifest = serde_json::from_slice(&bytes).unwrap();
        } else {
            files.push((name, bytes));
        }
    }
    edit(&mut manifest, &mut files);
    let mut writer = zip::ZipWriter::new(std::fs::File::create(target).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    for (name, bytes) in &files {
        writer.start_file(name.as_str(), options).unwrap();
        writer.write_all(bytes).unwrap();
    }
    writer.start_file(MANIFEST, options).unwrap();
    writer.write_all(manifest.to_string().as_bytes()).unwrap();
    writer.finish().unwrap();
}

fn smuggle(manifest: &mut Value, files: &mut Vec<(String, Vec<u8>)>, unit: &str, path: &str) {
    let bytes = b"evil".to_vec();
    manifest["entries"].as_array_mut().unwrap().push(json!({
        "unit": unit, "path": path, "size": bytes.len(), "sha256": sha256_hex(&bytes)}));
    files.push((format!("files/{unit}/{path}"), bytes));
}

#[test]
fn tampered_backups_are_refused_before_anything_is_written() {
    let directory = tempfile::tempdir().unwrap();
    let site = windows_site(&directory);
    let out = tempfile::tempdir().unwrap();
    let zip = out.path().join("backup.zip");
    backup_of(&site, &zip, &[]);
    type Edit = Box<dyn FnOnce(&mut Value, &mut Vec<(String, Vec<u8>)>)>;
    let cases: Vec<(&str, Edit, ErrorKind, Reason, &str)> = vec![
        (
            "path traversal",
            Box::new(|m, f| smuggle(m, f, "save_data", "../evil.bin")),
            ErrorKind::ArtifactIntegrity,
            Reason::InvalidBackup,
            "backup.refused",
        ),
        (
            "nested traversal",
            Box::new(|m, f| smuggle(m, f, "save_data", "system/../../evil.bin")),
            ErrorKind::ArtifactIntegrity,
            Reason::InvalidBackup,
            "backup.refused",
        ),
        (
            "absolute path",
            Box::new(|m, f| smuggle(m, f, "save_data", "C:/evil.bin")),
            ErrorKind::ArtifactIntegrity,
            Reason::InvalidBackup,
            "backup.refused",
        ),
        (
            "device name",
            Box::new(|m, f| smuggle(m, f, "save_data", "con")),
            ErrorKind::ArtifactIntegrity,
            Reason::InvalidBackup,
            "backup.refused",
        ),
        (
            "case collision",
            Box::new(|m, f| smuggle(m, f, "save_data", "system/SAVE00.bin")),
            ErrorKind::ArtifactIntegrity,
            Reason::InvalidBackup,
            "backup.refused",
        ),
        (
            "unlisted member",
            Box::new(|_, f| f.push(("files/save_data/extra.bin".into(), b"x".to_vec()))),
            ErrorKind::ArtifactIntegrity,
            Reason::InvalidBackup,
            "backup.refused",
        ),
        (
            "changed content",
            Box::new(|_, f| {
                let save = f.iter_mut().find(|(name, _)| name.ends_with("save00.bin")).unwrap();
                save.1 = b"save-x".to_vec();
            }),
            ErrorKind::ArtifactIntegrity,
            Reason::InvalidBackup,
            "backup.refused",
        ),
        (
            "another game",
            Box::new(|m, _| m["game"] = json!("mm3d")),
            ErrorKind::IncompatibleTarget,
            Reason::OtherGame,
            "backup.other_game",
        ),
        (
            "unsupported revision",
            Box::new(|m, _| m["recipe"] = json!("oot3d-usa-unknown")),
            ErrorKind::IncompatibleTarget,
            Reason::OtherGame,
            "backup.other_game",
        ),
        (
            "newer schema",
            Box::new(|m, _| m["schema"] = json!(2)),
            ErrorKind::UnsupportedFormat,
            Reason::NewerSchema,
            "backup.refused",
        ),
        (
            "another format",
            Box::new(|m, _| m["format"] = json!("triaevum_android_saves_v1")),
            ErrorKind::UnsupportedFormat,
            Reason::InvalidBackup,
            "backup.refused",
        ),
    ];
    let before = tree(&site.root);
    for (index, (name, edit, kind, reason, id)) in cases.into_iter().enumerate() {
        let bad = out.path().join(format!("bad-{index}.zip"));
        tampered(&zip, &bad, edit);
        let error = restore_into(&site, &bad).expect_err(name);
        assert_eq!((error.kind, error.reason), (kind, Some(reason)), "{name}: {}", error.message);
        assert_eq!(explain(&error).id, id, "{name}");
        assert_eq!(tree(&site.root), before, "{name}: the refused restore wrote something");
    }
    let garbage = out.path().join("garbage.zip");
    write(&garbage, b"not a zip at all");
    let error = restore_into(&site, &garbage).unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedFormat);
    assert_eq!(tree(&site.root), before);
    assert!(!site.data.join("backups").exists());
}

#[test]
fn an_interrupted_restore_after_two_categories_recovers_the_previous_version() {
    if let Ok(root) = std::env::var(CHILD) {
        // The child exits without unwinding right after the second category.
        let site = windows_site_at(Path::new(&root));
        let options = RestoreOptions { units: None, abort_after_category: Some(2) };
        let backup = PathBuf::from(std::env::var(CHILD_BACKUP).unwrap());
        let _ = restore(&site.inventory(), &backup, &options, &mut |_, _, _| {}, &CancelToken::new());
        std::process::exit(3);
    }
    let directory = tempfile::tempdir().unwrap();
    let site = windows_site(&directory);
    let out = tempfile::tempdir().unwrap();
    let zip = out.path().join("backup.zip");
    backup_of(&site, &zip, &[]);
    // A later version, changed in every category.
    write(&site.data.join("savedata/system/save00.bin"), b"later save");
    write(&site.package.join("savestates/state-2.oot3dsav"), b"OOT3DSV\0two");
    write(&site.data.join("config/topscreen_ui.json"), b"{\"layout\": \"later\"}");
    write(&site.data.join("imgui.ini"), b"[Window][later]");
    let previous = tree(&site.root);

    let status = std::process::Command::new(std::env::current_exe().unwrap())
        .args(["an_interrupted_restore_after_two_categories_recovers_the_previous_version", "--exact", "--nocapture"])
        .env(CHILD, text(&site.root))
        .env(CHILD_BACKUP, text(&zip))
        .status()
        .unwrap();
    assert_eq!(status.code(), Some(86), "the child must reach the injected restore boundary: {status:?}");

    // Killed: saves and save states committed, settings not.
    let journal_path = pending_restore(&site.data).expect("the journal survives the kill");
    let journal = read_json(&journal_path);
    assert_eq!(journal["state"], "applying");
    let categories: Vec<(String, String)> = journal["categories"]
        .as_array()
        .unwrap()
        .iter()
        .map(|item| (item["category"].as_str().unwrap().into(), item["status"].as_str().unwrap().into()))
        .collect();
    eprintln!("journal after the kill: {categories:?}");
    assert_eq!(
        categories,
        [
            ("saves".to_string(), "committed".to_string()),
            ("save_states".into(), "committed".into()),
            ("settings".into(), "pending".into())
        ]
    );
    assert_eq!(read_text(&site.data.join("savedata/system/save00.bin")), "save-a", "category 1 was applied");
    assert!(!site.package.join("savestates/state-2.oot3dsav").exists(), "category 2 was applied");
    assert_eq!(read_text(&site.data.join("config/topscreen_ui.json")), "{\"layout\": \"later\"}");
    // Play is blocked until recovery.
    let error = restore_into(&site, &zip).unwrap_err();
    assert_eq!((error.kind, error.reason), (ErrorKind::RecoveryRequired, Some(Reason::InterruptedRestore)));
    assert_eq!(explain(&error).id, "restore.interrupted");

    let settled = recover_pending(&site.activation, site.os, &site.data, &CancelToken::new()).unwrap().unwrap();
    assert_eq!(settled["outcome"], "rolled_back");
    let recovered: Vec<_> = tree(&site.root)
        .into_iter()
        .filter(|(name, _)| !name.starts_with("TriAevum/data/backups") && !name.contains(".forge-activation"))
        .collect();
    let previous: Vec<_> = previous.into_iter().filter(|(name, _)| !name.contains(".forge-activation")).collect();
    assert_eq!(recovered, previous, "the whole previous version is back");
    assert_no_siblings(&site.root);
    let snapshot = journal_path.parent().unwrap().join("snapshot");
    assert_eq!(read_text(&snapshot.join("save_data/system/save00.bin")), "later save");
    assert_eq!(read_text(&snapshot.join("save_states/state-2.oot3dsav")), "OOT3DSV\0two");
    assert_eq!(read_json(&journal_path)["state"], "rolled_back");
    assert!(pending_restore(&site.data).is_none());
    // Afterwards the restore runs to the end.
    restore_into(&site, &zip).unwrap();
    assert_eq!(read_text(&site.data.join("config/topscreen_ui.json")), "{\"layout\": \"windows\"}");
}

#[test]
fn backup_restore_and_cleanup_refuse_while_the_game_holds_its_session_lock() {
    let directory = tempfile::tempdir().unwrap();
    let site = windows_site(&directory);
    let out = tempfile::tempdir().unwrap();
    let zip = out.path().join("backup.zip");
    backup_of(&site, &zip, &[]);
    let before = tree(&site.root);
    // A fake game: an exclusive OS lock on the session file, like the runtime's.
    let game = std::fs::File::options()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(session_lock_path(&site.activation, site.os))
        .unwrap();
    game.lock().unwrap();
    let request = BackupRequest { destination: out.path().join("second.zip"), ..BackupRequest::default() };
    let refusals = [
        create_backup(&site.inventory(), &request, &mut |_, _, _| {}, &CancelToken::new()).unwrap_err(),
        restore_into(&site, &zip).unwrap_err(),
        clear_renderer_cache(&site.inventory()).unwrap_err(),
        remove_prepared_game(&site.inventory()).unwrap_err(),
    ];
    for error in refusals {
        assert_eq!((error.kind, error.reason), (ErrorKind::Busy, Some(Reason::GameRunning)), "{}", error.message);
        assert_eq!(explain(&error).id, "session.game_running");
    }
    assert!(!out.path().join("second.zip").exists());
    drop(game);
    assert_eq!(tree(&site.root), before, "nothing changed while the game ran");
    assert!(restore_into(&site, &zip).is_ok());
}

#[test]
fn another_platforms_backup_restores_only_the_portable_subset() {
    let windows_dir = tempfile::tempdir().unwrap();
    let windows = windows_site(&windows_dir);
    let out = tempfile::tempdir().unwrap();
    let zip = out.path().join("windows.zip");
    backup_of(&windows, &zip, &[]);
    let linux_dir = tempfile::tempdir().unwrap();
    let linux = appimage_site(&linux_dir);
    let data = &linux.data;

    let preview = restore_preview(&linux.inventory(), &zip, &RestoreOptions::default(), &CancelToken::new()).unwrap();
    let value = preview.to_value();
    assert_eq!(value["same_platform"], false);
    assert_eq!(value["warnings"][0]["kind"], "other_platform");
    let kept: BTreeSet<&str> = value["kept_settings"].as_array().unwrap().iter().filter_map(Value::as_str).collect();
    assert_eq!(kept, ["TriAevum.json", "oot3d_controls.json", "romfs_mods.json"].into_iter().collect());
    let skipped: Vec<&Value> = value["skipped"].as_array().unwrap().iter().collect();
    assert!(skipped.iter().any(|item| item["unit"] == "imgui_layout" && item["reason"] == "target_specific"));

    restore_into(&linux, &zip).unwrap();
    assert_eq!(read_text(&data.join("savedata/system/save00.bin")), "save-a");
    assert_eq!(read_text(&data.join("savestates/state-1.oot3dsav")), "OOT3DSV\0one");
    assert_eq!(read_text(&data.join("config/topscreen_ui.json")), "{\"layout\": \"windows\"}");
    assert_eq!(read_text(&data.join("config/game_language.json")), "{\"selected\": \"it\"}");
    assert_eq!(read_text(&data.join("config/environments/kokiri.json")), "{\"sun\": 1}");
    // Target-specific settings stay this platform's.
    assert_eq!(read_json(&data.join("config/TriAevum.json")), json!({"linux": true}));
    assert_eq!(read_text(&data.join("config/oot3d_controls.json")), "{\"gamepad\": true}");
    assert_eq!(read_text(&data.join("imgui.ini")), "[Window][linux]");
    // The save-state folder stays this installation's; LastState follows the states.
    let states = read_json(&data.join("config/savestates.json"));
    assert_eq!(states["Directory"], "");
    assert_eq!(states["LastState"], text(&resolve(&data.join("savestates").join("state-1.oot3dsav")).unwrap()));
    assert_no_siblings(&linux.root);
}

// ---- Installations made by the real import ----

fn installed() -> Fixture {
    let fixture = Fixture::new();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    fixture
}

fn state_of(fixture: &Fixture) -> InstallationState {
    inspect(&InspectRequest::package(&fixture.package))
}

fn installed_inventory(fixture: &Fixture) -> Inventory {
    // A synthetic environment: no user folder of the host is consulted.
    let root = fixture.package.parent().unwrap().to_path_buf();
    let pairs = [("APPDATA", text(&root.join("AppData"))), ("HOME", text(&root.join("home")))];
    let env = move |name: &str| pairs.iter().find(|(key, _)| *key == name).map(|(_, value)| value.clone());
    let os = HostOs::current();
    let installation = Installation {
        os,
        layout: for_package(&fixture.package, &env, os).unwrap(),
        data_root: None,
        runtime_dir: fixture.package.clone(),
        android: None,
        env: &env,
        sizes: true,
    };
    resolve_inventory(&installation).unwrap()
}

#[test]
fn removing_the_prepared_game_keeps_saves_settings_mods_and_downloads() {
    let fixture = installed();
    let data = fixture.data();
    write(&data.join("savedata/system/save00.bin"), b"save");
    write(&data.join("mods/mod.bin"), b"mod");
    write(&data.join("downloads/pack.zip"), b"pack");
    write(&data.join("cache/renderer/pipelines.bin"), b"cache");
    assert!(matches!(state_of(&fixture), InstallationState::Ready { .. }));

    let inventory = installed_inventory(&fixture);
    let plan = prepared_game_plan(&inventory);
    let names: BTreeSet<&str> = plan.targets.iter().map(|(name, ..)| *name).collect();
    assert!(names.is_superset(&["titles", "sources", "cache", "active_title"].into_iter().collect()), "{names:?}");
    assert!(!names.contains("renderer_cache"), "the cache folder holds it");
    assert!(plan.bytes() > 0);
    remove_prepared_game(&inventory).unwrap();
    for name in ["titles", "sources", "cache", "active-title.json"] {
        assert!(!data.join(name).exists(), "{name}");
    }
    for name in ["savedata/system/save00.bin", "mods/mod.bin", "downloads/pack.zip", "config/game_language.json"] {
        assert!(data.join(name).is_file(), "{name}");
    }
    let state = state_of(&fixture);
    assert_eq!(
        state,
        InstallationState::NeedsGameInput { recipe: RECIPE.into(), details: PREPARED_GAME_REMOVED.into() }
    );
    assert!(!state.can_play());
    // Frontends read the same answer from the inspection JSON (Android's installer).
    assert_eq!(state.to_value()["can_play"], false);
    assert_eq!(state.explanation().unwrap().id, "source.needed");
}

#[test]
fn clearing_the_renderer_cache_keeps_the_installation_ready() {
    let fixture = installed();
    let cache = fixture.data().join("cache/renderer");
    write(&cache.join("pipelines.bin"), b"cache");
    let inventory = installed_inventory(&fixture);
    assert!(renderer_cache_plan(&inventory).bytes() >= 5);
    clear_renderer_cache(&inventory).unwrap();
    assert!(!cache.exists());
    assert!(matches!(state_of(&fixture), InstallationState::Ready { .. }));
}

#[test]
fn rebuilding_reinstalls_from_the_kept_rom_data() {
    let fixture = installed();
    let data = fixture.data();
    std::fs::remove_dir_all(data.join("titles")).unwrap();
    assert!(matches!(state_of(&fixture), InstallationState::RepairNeeded { .. }));
    let mut stages = Vec::new();
    let mut report = |stage: &str, _: &str| stages.push(stage.to_string());
    let result = rebuild_prepared_assets(
        &fixture.package,
        None,
        InstallTarget::Package,
        &FakeRuntime::default(),
        &mut report,
        &CancelToken::new(),
    )
    .unwrap();
    assert_eq!((result["status"].as_str(), result["route"].as_str()), (Some("repaired"), Some("kept_rom_data")));
    assert!(stages.contains(&"activate".to_string()));
    assert!(matches!(state_of(&fixture), InstallationState::Ready { .. }));
    // Without the kept ROM data the ROM is needed again.
    std::fs::remove_dir_all(data.join("sources")).unwrap();
    std::fs::remove_dir_all(data.join("titles")).unwrap();
    let error = rebuild_prepared_assets(
        &fixture.package,
        None,
        InstallTarget::Package,
        &FakeRuntime::default(),
        &mut |_, _| {},
        &CancelToken::new(),
    )
    .unwrap_err();
    assert_eq!(error.reason, Some(Reason::MissingRetainedSource));
    assert_eq!(explain(&error).id, "source.needed");
}

#[test]
fn a_pending_restore_journal_blocks_play_until_recovered() {
    let fixture = installed();
    let data = fixture.data();
    let journal = data.join("backups/restore-20261026-120000/journal.json");
    write_json(
        &journal,
        &json!({"format": "triaevum_restore_journal_v1", "state": "staging", "groups": [], "categories": []}),
    );
    let state = state_of(&fixture);
    assert!(matches!(state, InstallationState::RepairNeeded { reason: RepairReason::InterruptedRestore, .. }));
    assert!(!state.can_play());
    assert_eq!(state.explanation().unwrap().id, "restore.interrupted");
    let settled = recover_pending(&fixture.package, HostOs::current(), &data, &CancelToken::new()).unwrap().unwrap();
    assert_eq!(settled["outcome"], "rolled_back");
    assert!(matches!(state_of(&fixture), InstallationState::Ready { .. }));
}

#[test]
fn the_jobs_back_up_and_preview_through_json_requests() {
    let fixture = installed();
    write(&fixture.data().join("savedata/system/save00.bin"), b"save");
    let out = tempfile::tempdir().unwrap();
    let zip = out.path().join("backup.zip");
    let run = |request: Value| -> Vec<Value> {
        let mut job = forge_core::jobs::start(request.to_string().as_bytes()).unwrap();
        job.join();
        std::iter::from_fn(|| job.poll(0)).collect()
    };
    let package = text(&fixture.package);
    let events = run(json!({"format": "triaevum_forge_request_v1", "job": "backup",
                            "package_root": package, "destination": text(&zip)}));
    let last = events.last().unwrap();
    assert_eq!(last["event"], "succeeded", "{last}");
    assert!(last["result"]["units"].as_array().unwrap().contains(&json!("save_data")));
    let events = run(json!({"format": "triaevum_forge_request_v1", "job": "restore_preview",
                            "package_root": package, "backup": text(&zip)}));
    let last = events.last().unwrap();
    assert_eq!(last["result"]["format"], "triaevum_restore_preview_v1", "{last}");
    assert_eq!(last["result"]["same_platform"], true);
    let events = run(json!({"format": "triaevum_forge_request_v1", "job": "restore_preview",
                            "package_root": package, "backup": text(&out.path().join("missing.zip"))}));
    assert_eq!(events.last().unwrap()["event"], "failed");
    let error = forge_core::jobs::start(br#"{"format":"triaevum_forge_request_v1","job":"backup"}"#).err().unwrap();
    assert_eq!(error.message, "destination must be a path");
    let capabilities = forge_core::jobs::capabilities();
    for job in ["backup", "restore", "restore_recover", "rebuild_prepared_assets"] {
        assert!(capabilities["jobs"].as_array().unwrap().contains(&json!(job)), "{job}");
    }
}
