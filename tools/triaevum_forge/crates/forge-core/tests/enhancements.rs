// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Enhancements page's engine (UX plan WP-07) on a synthetic
//! installation: Apply without the ROM while the kept ROM data verifies, the
//! ROM asked for once it does not, the HD interface pack choice, the user's
//! texture pack import and the unsupported custom RomFS folder.

mod common;

use std::io::Write;
use std::path::Path;

use common::{FakeRuntime, Fixture};
use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::{CancelToken, ErrorKind, Reason};
use forge_core::enhancements;
use forge_core::inventory::InventoryRequest;
use forge_core::repair::RepairRequest;
use serde_json::{json, Value};

const TITLE: &str = "0004000000033600";

fn installed() -> Fixture {
    let fixture = Fixture::build_with(false, Some(0x0004_0000_0003_3600));
    let services = FakeRuntime { managed_data_paths: true, ..Default::default() };
    fixture.install(&services, &mut Vec::new()).unwrap();
    // The runtime's own configuration (the fake product's has no schema).
    let config = fixture.data().join("config/TriAevum.json");
    let mut document = load_json_object(&config).unwrap();
    document.insert(
        "Graphics".into(),
        json!({"SchemaVersion": 15, "Preset": "Enhanced", "TexturePacks": {"Azahar": {
            "DumpTextures": false, "LoadCustomTextures": false, "LoadDirectory": "", "DumpDirectory": ""}}}),
    );
    atomic_write_json(&config, &Value::Object(document)).unwrap();
    fixture
}

fn inventory(fixture: &Fixture) -> InventoryRequest {
    InventoryRequest { sizes: false, ..InventoryRequest::package(&fixture.package) }
}

fn apply(fixture: &Fixture, stages: &mut Vec<String>) -> forge_base::Result<Value> {
    let services = FakeRuntime { managed_data_paths: true, ..Default::default() };
    let mut report = |_: &str, message: &str| stages.push(message.to_string());
    enhancements::apply(&RepairRequest::package(&fixture.package), &services, &mut report, &CancelToken::new())
}

fn status(fixture: &Fixture) -> Value {
    enhancements::status(&inventory(fixture), &CancelToken::new()).unwrap()
}

fn png() -> Vec<u8> {
    let mut bytes = vec![0x89, b'P', b'N', b'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13];
    bytes.extend(b"IHDR");
    bytes.extend(32u32.to_be_bytes());
    bytes.extend(32u32.to_be_bytes());
    bytes.extend([8, 6, 0, 0, 0, 0, 0, 0, 0]);
    bytes
}

fn zip(path: &Path, files: &[(&str, &[u8])]) {
    let mut writer = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
    for (name, bytes) in files {
        writer.start_file(*name, zip::write::SimpleFileOptions::default()).unwrap();
        writer.write_all(bytes).unwrap();
    }
    writer.finish().unwrap();
}

#[test]
fn apply_needs_no_rom_while_the_kept_data_verifies() {
    let fixture = installed();
    let before = status(&fixture);
    assert_eq!(before["installed"], true);
    assert_eq!(before["title_id"], TITLE);
    assert_eq!(before["retained_sources"], true);
    assert_eq!(before["hd_pack"]["enabled"], true, "decision 5: on by default");
    assert_eq!(before["custom_romfs"]["enabled"], false, "decision 6: off by default");
    assert_eq!(before["hud_layout"]["policy"], "keep");
    assert_eq!(before["apply_needed"], false);

    // The catalogued mod on: Apply runs from the kept data, no ROM.
    let change = enhancements::SettingsChange {
        mod_enabled: Some(("vanilla_plus_link".into(), true)),
        ..Default::default()
    };
    let after = enhancements::change_settings(&inventory(&fixture), &change, &CancelToken::new()).unwrap();
    assert_eq!(after["mods"][0]["enabled"], true);
    let mut stages = Vec::new();
    let result = apply(&fixture, &mut stages).unwrap();
    assert_eq!(result["status"], "applied");
    assert_eq!(result["route"], "in_place");
    // The fixture's revision has no contract for the mod: native files, no failure.
    assert!(stages.iter().any(|text| text.contains("it is not applied")), "{stages:?}");

    // The HD interface pack off: Apply leaves it out and says so.
    enhancements::set_hd_pack_enabled(&fixture.data(), false).unwrap();
    assert!(!enhancements::hd_pack_enabled(&fixture.data()));
    let preferences = load_json_object(&enhancements::preferences_path(&fixture.data())).unwrap();
    assert_eq!(preferences["format"], enhancements::PREFERENCES_FORMAT);
    stages.clear();
    apply(&fixture, &mut stages).unwrap();
    assert!(stages.iter().any(|text| text.contains("HD interface pack is turned off")), "{stages:?}");
}

#[test]
fn apply_asks_for_the_rom_once_the_kept_data_is_gone() {
    let fixture = installed();
    std::fs::remove_dir_all(fixture.data().join("sources")).unwrap();
    let error = apply(&fixture, &mut Vec::new()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::InvalidInput);
    assert_eq!(error.reason, Some(Reason::MissingRetainedSource));
    assert_eq!(forge_core::explain::explain(&error).id, "source.needed");
}

#[test]
fn a_texture_pack_zip_is_imported_and_turned_on() {
    let fixture = installed();
    let work = tempfile::tempdir().unwrap();
    let texture = png();
    let archive = work.path().join("pack.zip");
    let name = format!("textures/{TITLE}/UI/tex1_32x32_0123456789ABCDEF_13_mip0.png");
    zip(&archive, &[(&name, &texture), ("README.txt", b"by someone")]);
    let result = enhancements::import_textures(&inventory(&fixture), &archive, false, &CancelToken::new()).unwrap();
    assert_eq!(result["status"], "imported", "{result:#}");
    assert_eq!(result["import"]["layout"], "title");
    assert_eq!(result["import"]["textures"], 1);
    let folder = fixture.data().join("textures/load").join(TITLE);
    assert_eq!(std::fs::read(folder.join("UI/tex1_32x32_0123456789ABCDEF_13_mip0.png")).unwrap(), texture);
    let textures = &status(&fixture)["custom_textures"];
    assert_eq!((textures["enabled"].clone(), textures["files"].clone()), (json!(true), json!(1)));
    assert_eq!(textures["routed"], true, "{textures:#}");
    assert_eq!(textures["applied"], true);

    // A flat zip: asked first, then placed under the installed title.
    let flat = work.path().join("flat.zip");
    zip(&flat, &[("My pack/tex1_8x8_00000000000000AA_13_mip0.png", &texture)]);
    let asked = enhancements::import_textures(&inventory(&fixture), &flat, false, &CancelToken::new()).unwrap();
    assert_eq!((asked["status"].as_str(), asked["reason"].as_str()), (Some("confirmation_required"), Some("flat")));
    assert!(folder.join("UI").is_dir(), "nothing changed before the confirmation");
    let imported = enhancements::import_textures(&inventory(&fixture), &flat, true, &CancelToken::new()).unwrap();
    assert_eq!(imported["import"]["layout"], "flat");
    assert!(folder.join("tex1_8x8_00000000000000AA_13_mip0.png").is_file() && !folder.join("UI").exists());

    // An invalid pack keeps the previous one.
    let bad = work.path().join("bad.zip");
    zip(&bad, &[(&format!("{TITLE}/tex1_8x8_00000000000000BB_13_mip0.png"), b"not a png")]);
    let error = enhancements::import_textures(&inventory(&fixture), &bad, false, &CancelToken::new()).unwrap_err();
    assert!(error.message.contains("is not a PNG image"), "{}", error.message);
    assert!(folder.join("tex1_8x8_00000000000000AA_13_mip0.png").is_file());

    // The switch goes off through the settings service.
    let change = enhancements::SettingsChange { custom_textures: Some(false), ..Default::default() };
    let after = enhancements::change_settings(&inventory(&fixture), &change, &CancelToken::new()).unwrap();
    assert_eq!(after["custom_textures"]["enabled"], false);
    assert_eq!(after["custom_textures"]["applied"], false);
}

#[test]
fn the_custom_romfs_folder_is_reported_and_recorded_in_diagnostics() {
    let fixture = installed();
    let folder = forge_mods::custom_romfs::folder(&fixture.data());
    std::fs::create_dir_all(folder.join("actor")).unwrap();
    std::fs::write(folder.join("actor/zelda_test.zar"), b"payload").unwrap();
    let state = status(&fixture);
    assert_eq!(state["custom_romfs"]["present"], true);
    assert_eq!(state["custom_romfs"]["enabled"], false);
    assert_eq!(state["custom_romfs"]["applicable"], true);
    assert_eq!(state["custom_romfs"]["paths"], json!(["actor/zelda_test.zar"]));
    // The fixture's RomFS has no file tables to check against (forge-mods'
    // behaviour tests check a real Level-3 RomFS).
    assert_eq!(state["custom_romfs"]["checked_against_romfs"], false);
    assert!(enhancements::diagnostics_line(&fixture.data()).ends_with("custom RomFS folder off"));
    let change = enhancements::SettingsChange { custom_romfs: Some(true), ..Default::default() };
    let state = enhancements::change_settings(&inventory(&fixture), &change, &CancelToken::new()).unwrap();
    assert_eq!(state["custom_romfs"]["enabled"], true);
    let mut stages = Vec::new();
    apply(&fixture, &mut stages).unwrap();
    assert!(stages.iter().any(|text| text.contains("custom RomFS folder is not applied")), "{stages:?}");
    assert!(enhancements::diagnostics_line(&fixture.data()).ends_with("custom RomFS folder on, not applied"));
    // A rejected folder.
    std::fs::write(folder.join("actor/empty.zar"), b"").unwrap();
    let state = status(&fixture);
    assert_eq!(state["custom_romfs"]["applicable"], false);
    assert_eq!(state["custom_romfs"]["rejected"][0]["kind"], "empty_file");
}

#[test]
fn the_jobs_follow_the_contract() {
    let fixture = installed();
    let run = |request: Value| -> Vec<Value> {
        let job = forge_core::jobs::start(serde_json::to_string(&request).unwrap().as_bytes()).unwrap();
        let mut events = Vec::new();
        loop {
            let Some(event) = job.poll(5000) else { continue };
            let done = matches!(event["event"].as_str(), Some("succeeded" | "failed" | "cancelled"));
            events.push(event);
            if done {
                return events;
            }
        }
    };
    let base = |job: &str| {
        json!({"format": "triaevum_forge_request_v1", "job": job,
               "package_root": fixture.package.to_string_lossy()})
    };
    let events = run(base("enhancements_status"));
    assert_eq!(events.last().unwrap()["result"]["format"], enhancements::STATUS_FORMAT);
    let mut settings = base("enhancement_settings");
    settings["hd_interface_pack"] = json!(false);
    settings["custom_romfs"] = json!(true);
    let events = run(settings);
    let result = &events.last().unwrap()["result"];
    assert_eq!((result["hd_pack"]["enabled"].clone(), result["custom_romfs"]["enabled"].clone()), (json!(false), json!(true)));
    let mut bad = base("enhancement_settings");
    bad["mod"] = json!({"id": "vanilla_plus_link"});
    assert!(forge_core::jobs::start(serde_json::to_string(&bad).unwrap().as_bytes()).is_err());
    let capabilities = forge_core::jobs::capabilities();
    for job in ["enhancements_status", "apply_enhancements", "texture_import", "enhancement_settings"] {
        assert!(capabilities["jobs"].as_array().unwrap().contains(&json!(job)), "{job}");
    }
}
