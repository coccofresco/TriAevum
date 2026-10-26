// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Diagnostics on installations the real import made: the support bundle
//! holds only allowlisted files, redacted (no saves, game sources or dump
//! files; installation folders and user names replaced), within its size
//! caps, written atomically; the report stays below 16 KB; a failed game run
//! becomes the "game closed with an error" explanation with its log tail.

mod common;

use std::io::Read;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use common::{FakeRuntime, Fixture};
use forge_core::abi::{TriAevumForgeDiagnostics, TriAevumForgeFreeBuffer};
use forge_core::diagnostics::{
    self, collect, diagnostics_bytes, game_exit_notice, plan_bundle, record_game_run, write_bundle, GameRun, Sources,
    MEMBER_LIMIT, TEXT_LIMIT,
};
use forge_core::inventory::InventoryRequest;
use serde_json::{json, Value};

/// A path component no bundle may contain.
const PERSONAL: &str = "Jane Q Private";

fn write(path: &Path, bytes: &[u8]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, bytes).unwrap();
}

fn text(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

fn members(zip_path: &Path) -> Vec<(String, String)> {
    let mut archive = zip::ZipArchive::new(std::fs::File::open(zip_path).unwrap()).unwrap();
    (0..archive.len())
        .map(|index| {
            let mut file = archive.by_index(index).unwrap();
            let mut content = String::new();
            file.read_to_string(&mut content).unwrap();
            (file.name().unwrap().to_string(), content)
        })
        .collect()
}

/// Files a bundle must never read, planted beside the allowed ones.
fn plant_private_files(fixture: &Fixture, sources: &Sources) -> Vec<PathBuf> {
    let data = fixture.data();
    let personal_home = format!(r"C:\Users\{PERSONAL}\Pictures\pack");
    let planted = vec![
        data.join("savedata").join("x.sav"),
        data.join("sources").join("code.bin"),
        data.join("savestates").join("slot1.state"),
        data.join("savedata").join("crashes").join("oot3d_crash_20261026_120405_1_2.dmp"),
    ];
    for path in &planted {
        if !path.exists() {
            write(path, format!("PRIVATE {PERSONAL}").as_bytes());
        }
    }
    // A configuration with an absolute personal path and the package's own.
    let config = sources.runtime_config.clone().unwrap();
    let mut settings: Value = serde_json::from_slice(&std::fs::read(&config).unwrap_or_default()).unwrap_or(json!({}));
    settings["Graphics"]["TexturePacks"] = json!({"Azahar": {"LoadDirectory": personal_home,
        "DumpDirectory": format!("/home/{PERSONAL}/dumps")}});
    settings["Probe"] = json!(text(&fixture.package.join("runtime")));
    write(&config, serde_json::to_vec_pretty(&settings).unwrap().as_slice());
    planted
}

fn sources_of(fixture: &Fixture) -> Sources {
    let request = InventoryRequest { sizes: false, ..InventoryRequest::package(&fixture.package) };
    Sources::from_inventory(&forge_core::inventory::inventory_of(&request).unwrap())
}

#[test]
fn the_bundle_holds_only_redacted_allowlisted_files() {
    let fixture = Fixture::new();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    let sources = sources_of(&fixture);
    let planted = plant_private_files(&fixture, &sources);
    let package = text(&fixture.package);
    write(
        &sources.game_log(),
        format!(
            "[x] [warning] OOT3D Vulkan device [0]: Fake GPU (api 1.3.0, vendor 0x1002, driver 0x00000001)\n\
             [x] [info] opened {package}/data/savedata and C:\\Users\\{PERSONAL}\\Desktop\\rom.3ds\n"
        )
        .as_bytes(),
    );
    write(
        &sources.game_stderr(),
        format!("{}could not create the Vulkan device in {package}\n", diagnostics::FATAL_PREFIX).as_bytes(),
    );
    write(
        &sources.forge_logs.join("forge.log"),
        format!("2026-10-26T12:00:00Z [install] failed: {package}\n").as_bytes(),
    );
    write(&sources.game_logs.join("unlisted.txt"), b"not allowlisted");

    let destination = fixture.package.parent().unwrap().join("export").join("bundle.zip");
    write(&destination, b"an older export");
    let request =
        json!({"format": "triaevum_forge_diagnostics_v1", "package_root": package, "bundle": text(&destination)});
    let bytes = serde_json::to_vec(&request).unwrap();
    let buffer = unsafe { TriAevumForgeDiagnostics(bytes.as_ptr(), bytes.len()) };
    let answer: Value = serde_json::from_slice(unsafe { std::slice::from_raw_parts(buffer.data, buffer.len) }).unwrap();
    unsafe { TriAevumForgeFreeBuffer(buffer) };
    assert_eq!(answer["status"], "ok", "{answer}");
    assert_eq!(answer["bundle"]["path"], text(&destination));

    // Atomic export: the old file was replaced and no temporary stays.
    let leftovers: Vec<String> = std::fs::read_dir(destination.parent().unwrap())
        .unwrap()
        .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
        .collect();
    assert_eq!(leftovers, ["bundle.zip"]);

    let contents = members(&destination);
    let names: Vec<&str> = contents.iter().map(|(name, _)| name.as_str()).collect();
    for required in [
        "diagnostics.txt",
        "manifest.json",
        "game-logs/oot3d_native_game.log",
        "forge-logs/game-stderr.log",
        "forge-logs/forge.log",
        "settings/TriAevum.json",
    ] {
        assert!(names.contains(&required), "{required} missing from {names:?}");
    }
    let allowed_prefixes =
        ["diagnostics.txt", "manifest.json", "crash-dumps.txt", "forge-logs/", "game-logs/", "settings/", "state/"];
    for name in &names {
        assert!(allowed_prefixes.iter().any(|prefix| name.starts_with(prefix)), "unexpected member {name}");
        assert!(
            !name.ends_with(".dmp")
                && !name.ends_with(".sav")
                && !name.ends_with(".state")
                && !name.contains("code.bin")
        );
        assert!(!name.contains("unlisted"));
    }
    for (name, content) in &contents {
        assert!(!content.contains(PERSONAL), "{name} leaks a personal path component:\n{content}");
        assert!(!content.contains("PRIVATE"), "{name} holds a private file's bytes");
        let spellings = [package.clone(), package.replace('\\', "/"), package.replace('\\', r"\\")];
        for spelling in spellings {
            assert!(!content.to_lowercase().contains(&spelling.to_lowercase()), "{name} names the package folder");
        }
    }
    let config = &contents.iter().find(|(name, _)| name == "settings/TriAevum.json").unwrap().1;
    assert!(config.contains(r"C:\\Users\\<user>\\Pictures\\pack"), "{config}");
    assert!(config.contains("<package>"), "{config}");
    let log = &contents.iter().find(|(name, _)| name == "game-logs/oot3d_native_game.log").unwrap().1;
    assert!(log.contains("opened <data>/savedata and C:\\Users\\<user>\\Desktop\\rom.3ds"), "{log}");
    if cfg!(windows) {
        let dumps = &contents.iter().find(|(name, _)| name == "crash-dumps.txt").expect("dump names").1;
        assert!(dumps.contains("oot3d_crash_20261026_120405_1_2.dmp"));
    }
    let report = &contents.iter().find(|(name, _)| name == "diagnostics.txt").unwrap().1;
    assert!(report.len() <= TEXT_LIMIT);
    assert!(report.contains("Graphics device (game log): [0]: Fake GPU"), "{report}");
    assert!(
        report.contains("Last error reported by the game: could not create the Vulkan device in <package>"),
        "{report}"
    );
    assert_eq!(answer["text"].as_str().unwrap(), report);
    // The planted files are untouched.
    for path in planted {
        assert!(path.exists());
    }
}

#[test]
fn bundle_members_are_capped() {
    let fixture = Fixture::new();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    let sources = sources_of(&fixture);
    let line = format!("{}\n", "x".repeat(99));
    write(&sources.game_log(), line.repeat(50_000).as_bytes());
    let state = sources.runtime_state.clone().unwrap();
    write(&state, format!("{{\"padding\": \"{}\"}}", "y".repeat(3 << 20)).as_bytes());
    let plan = plan_bundle(&sources, "report");
    assert!(plan.omitted.iter().any(|(name, _)| name == "state/runtime-state.json"), "{:?}", plan.omitted);
    let log = plan.members.iter().find(|member| member.name == "game-logs/oot3d_native_game.log").unwrap();
    assert_eq!(log.size, MEMBER_LIMIT);
    let destination = fixture.package.parent().unwrap().join("capped.zip");
    write_bundle(&plan, &destination).unwrap();
    let contents = members(&destination);
    let log = &contents.iter().find(|(name, _)| name == "game-logs/oot3d_native_game.log").unwrap().1;
    assert!(log.starts_with('[') && log.contains("earlier bytes left out"), "{}", &log[..80]);
    assert!(log.len() as u64 <= MEMBER_LIMIT + 64);
    let manifest: Value =
        serde_json::from_str(&contents.iter().find(|(name, _)| name == "manifest.json").unwrap().1).unwrap();
    assert_eq!(manifest["omitted"][0]["name"], "state/runtime-state.json");
}

#[test]
fn a_failed_game_run_explains_itself_with_its_log_tail() {
    let fixture = Fixture::new();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    let sources = sources_of(&fixture);
    let started = SystemTime::now() - Duration::from_secs(5);
    let lines: String = (1..=60).map(|index| format!("line {index}\n")).collect();
    write(&sources.game_log(), lines.as_bytes());
    write(&sources.game_stderr(), format!("{}the session lock is held\n", diagnostics::FATAL_PREFIX).as_bytes());

    assert_eq!(game_exit_notice(&sources, &GameRun { started, duration: Duration::from_secs(4), exit_code: 0 }), None);
    let run = GameRun { started, duration: Duration::from_secs(4), exit_code: 1 };
    let notice = game_exit_notice(&sources, &run).unwrap();
    let explanation = forge_core::explain::explain_value(&notice).unwrap();
    assert_eq!(explanation.id, "outcome.game_exited_with_error");
    assert_eq!(explanation.title, "The game closed with an error");
    assert!(
        explanation.details.starts_with("Exit code: 1 (the game stopped with an error)"),
        "{}",
        explanation.details
    );
    assert!(explanation.details.contains("Error reported by the game: the session lock is held"));
    assert!(explanation.details.contains("line 60") && explanation.details.contains("line 21"));
    assert!(!explanation.details.contains("line 20\n"), "only the last 40 lines");

    // A log from before the run is not this run's tail.
    let later = GameRun { started: SystemTime::now() + Duration::from_secs(60), ..run.clone() };
    let details = forge_core::explain::explain_value(&game_exit_notice(&sources, &later).unwrap()).unwrap().details;
    assert!(!details.contains("line 60"), "{details}");

    record_game_run(&sources, &run);
    let collected = collect(&InventoryRequest { sizes: false, ..InventoryRequest::package(&fixture.package) }).unwrap();
    assert_eq!(collected.last_run.as_ref().map(|run| run.exit_code), Some(1));
    let value = collected.to_value();
    assert_eq!(value["game_exit"]["id"], "outcome.game_exited_with_error");
    assert_eq!(value["last_game_run"]["exit_code"], 1);
    assert!(value["text"].as_str().unwrap().contains("exit code 1 (the game stopped with an error)"));
}

#[test]
fn the_display_recovery_record_is_shown() {
    let fixture = Fixture::new();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    let sources = sources_of(&fixture);
    write(
        &sources.recovery_path(),
        json!({"format": "triaevum_display_recovery_v1", "time": "2026-10-26T12:00:00Z",
               "reason": "the previous launch ended within its first minute",
               "fields": ["Window mode", "Output size", "Upscaler"]})
        .to_string()
        .as_bytes(),
    );
    let value: Value = serde_json::from_slice(&diagnostics_bytes(
        &serde_json::to_vec(
            &json!({"format": "triaevum_forge_diagnostics_v1", "package_root": text(&fixture.package)}),
        )
        .unwrap(),
    ))
    .unwrap();
    assert_eq!(value["recovery"]["id"], "outcome.display_settings_reset");
    assert!(value["recovery"]["details"].as_str().unwrap().contains("Reset: Window mode, Output size, Upscaler"));
    assert!(value["text"].as_str().unwrap().contains("Display recovery: 2026-10-26T12:00:00Z reset Window mode"));
    assert!(value["bundle_plan"]["members"]
        .as_array()
        .unwrap()
        .iter()
        .any(|member| member["name"] == "game-logs/last-recovery.json"));
}

#[test]
fn an_android_installation_bundles_its_native_error_output() {
    let fixture = Fixture::android();
    fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
    let (native, profile) = fixture.android.clone().unwrap();
    let app_data = profile.parent().unwrap().join("app-data");
    // Android's runtime writes its error output in D/logs (UX plan WP-05C).
    write(
        &profile.join("data").join("logs").join("native-stderr.log"),
        format!("{}no Vulkan device\n", diagnostics::FATAL_PREFIX).as_bytes(),
    );
    write(&app_data.join("files/diagnostics/game-stages.txt"), b"session 1\nstage window\n");
    write(&app_data.join("files/diagnostics/other.txt"), b"not allowlisted");
    let destination = profile.parent().unwrap().join("cache").join("share").join("TriAevum-diagnostics.zip");
    let request = json!({"format": "triaevum_forge_diagnostics_v1", "package_root": text(&fixture.package),
        "android": {"native_library_dir": text(&native), "profile_root": text(&profile),
                    "app_data_dir": text(&app_data), "package_name": "org.triaevum.android"},
        "bundle": text(&destination)});
    let value: Value = serde_json::from_slice(&diagnostics_bytes(&serde_json::to_vec(&request).unwrap())).unwrap();
    assert_eq!(value["status"], "ok", "{value}");
    assert!(value["text"].as_str().unwrap().contains("Last error reported by the game: no Vulkan device"));
    let names: Vec<String> = members(&destination).into_iter().map(|(name, _)| name).collect();
    assert!(names.contains(&"game-logs/native-stderr.log".to_string()), "{names:?}");
    assert!(names.contains(&"android/game-stages.txt".to_string()), "{names:?}");
    assert!(!names.iter().any(|name| name.contains("other.txt")));
}

#[test]
fn a_malformed_request_is_explained() {
    let value: Value = serde_json::from_slice(&diagnostics_bytes(b"{\"format\": \"other\"}")).unwrap();
    assert_eq!(value["status"], "failed");
    assert_eq!(value["explanation"]["id"], "request.invalid");
}
