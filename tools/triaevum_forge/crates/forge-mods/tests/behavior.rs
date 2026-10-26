// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Behavior tests ported from test_ui_texture_pack.py and test_romfs_mods.py,
//! on synthetic fixtures with test-only contracts.

mod common;

use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;

use common::*;
use forge_base::{CancelToken, ErrorKind};
use forge_mods::catalog::{LANGUAGE_FOLDERS, UI_ATLAS_HD_TEXTURE};
use forge_mods::{archive, overlay, romfs_mods, ui_pack, ArchiveContract, PrepareContext, SystemBsdtar, UiTexturePack};
use serde_json::Value;

fn fixtures() -> (tempfile::TempDir, Fixtures) {
    let directory = tempfile::tempdir().unwrap();
    let fixtures = Fixtures::create(&directory.path().join("fixtures"));
    (directory, fixtures)
}

#[test]
fn language_folders_and_common_textures_are_sorted_apart() {
    assert_eq!(ui_pack::prepared_path("UI/08_EU_ITALIAN/a.png"), "languages/08_EU_ITALIAN/a.png");
    assert_eq!(ui_pack::prepared_path("UI/GERMAN/a.png"), "languages/GERMAN/a.png");
    assert_eq!(ui_pack::prepared_path("UI/a.png"), "common/UI/a.png");
    assert_eq!(ui_pack::prepared_path("BOSH RUSH/a.png"), "common/BOSH RUSH/a.png");
    assert_eq!(ui_pack::prepared_path("UI/Other/b/a.png"), "common/UI/Other/b/a.png");
    // The runtime's language mapping names exactly the catalogued folders.
    let catalogued: std::collections::BTreeSet<&str> = UiTexturePack::vanilla_ui_hud()
        .members
        .iter()
        .filter_map(|member| {
            let parts: Vec<&str> = member.path.split('/').collect();
            (parts.len() == 3).then(|| LANGUAGE_FOLDERS.into_iter().find(|folder| *folder == parts[1]).unwrap())
        })
        .collect();
    assert_eq!(catalogued, LANGUAGE_FOLDERS.into_iter().collect());
}

#[test]
fn archive_extracts_only_the_catalogued_textures() {
    let (directory, fixtures) = fixtures();
    let pack = fixtures.ui_pack();
    let contract = fixtures.contract(&pack, "ui.zip");
    let data = directory.path().join("data");
    let cancel = CancelToken::new();
    let prepared = ui_pack::extract_archive(&fixtures.dir.join("ui.zip"), &data, &contract, &pack, &cancel).unwrap();
    assert_eq!(ui_pack::prepared_pack(&data, &pack, &cancel).unwrap(), Some(prepared.clone()));
    assert_eq!(
        fs::read(prepared.join("languages/01_US_ENGLISH/tex1_512x256_BB00B25B152582B6_4_mip0.png")).unwrap(),
        b"\x89PNG us english"
    );
    assert_eq!(
        fs::read(prepared.join("common/UI/tex1_512x256_BB00B25B152582B6_4_mip0.png")).unwrap(),
        b"\x89PNG common"
    );
    assert_eq!(fs::read(ui_pack::ui_atlas_hd(Some(&prepared)).unwrap()).unwrap(), b"\x89PNG atlas");
    let tree = tree(&prepared);
    assert!(!tree.keys().any(|name| name.ends_with(".qbf") || name.ends_with(".txt")));
    let receipt: Value = serde_json::from_slice(&fs::read(prepared.join("source.json")).unwrap()).unwrap();
    assert_eq!(receipt["origin"], format!("archive:{}", contract.sha256));
    assert_eq!(receipt["redistributable"], false);
    assert_eq!(receipt["license"], "CC BY-NC-ND 4.0");
    // The receipt uses the legacy writer: sorted keys, two-space indent, final LF.
    let text = fs::read_to_string(prepared.join("source.json")).unwrap();
    assert!(text.starts_with("{\n  \"attribution\": ") && text.ends_with("\n}\n"));
    // A changed texture invalidates the prepared pack.
    fs::write(prepared.join("common/UI/tex1_512x256_BB00B25B152582B6_4_mip0.png"), b"tampered").unwrap();
    assert_eq!(ui_pack::prepared_pack(&data, &pack, &cancel).unwrap(), None);
}

#[test]
fn a_different_archive_is_rejected_and_cancellation_propagates() {
    let (directory, fixtures) = fixtures();
    let pack = fixtures.ui_pack();
    let contract = fixtures.contract(&pack, "ui.zip");
    let data = directory.path().join("data");
    let zip = fixtures.dir.join("ui.zip");
    let cancel = CancelToken::new();
    let mut wrong = contract.clone();
    wrong.sha256 = "0".repeat(64);
    let error = ui_pack::extract_archive(&zip, &data, &wrong, &pack, &cancel).unwrap_err();
    assert!(error.message.contains("failed verification"));
    let mut changed = pack.clone();
    changed.members[0].bytes = 3;
    let error = ui_pack::extract_archive(&zip, &data, &contract, &changed, &cancel).unwrap_err();
    assert!(error.message.contains("differs"));
    assert_eq!(ui_pack::prepared_pack(&data, &pack, &cancel).unwrap(), None);
    let cancelled = CancelToken::new();
    cancelled.cancel();
    let error = ui_pack::extract_archive(&zip, &data, &contract, &pack, &cancelled).unwrap_err();
    assert_eq!(error.kind, ErrorKind::Cancelled);
    // No staging folder is left behind.
    let leftovers: Vec<_> = fs::read_dir(data.join("mods/ui-textures")).unwrap().collect();
    assert!(leftovers.is_empty());
}

#[test]
fn extracted_folder_is_verified_at_any_level() {
    let (directory, fixtures) = fixtures();
    let pack = fixtures.ui_pack();
    let cancel = CancelToken::new();
    for (index, level) in fixtures.ui_levels().iter().enumerate() {
        let data = directory.path().join(format!("data{index}"));
        let prepared = ui_pack::import_extracted(&fixtures.dir.join(level), &data, &pack, &cancel).unwrap();
        let receipt: Value = serde_json::from_slice(&fs::read(prepared.join("source.json")).unwrap()).unwrap();
        assert_eq!(receipt["origin"], "extracted_folder", "{level}");
    }
    let data = directory.path().join("bad");
    let error = ui_pack::import_extracted(&fixtures.dir.join("ui_folder_bad"), &data, &pack, &cancel).unwrap_err();
    assert!(error.message.contains("differs"));
    let error = ui_pack::import_extracted(&fixtures.dir, &data, &pack, &cancel).unwrap_err();
    assert!(error.message.contains("does not contain"));
}

#[test]
fn prepare_uses_a_stored_archive_and_reports_failures() {
    let (directory, fixtures) = fixtures();
    let pack = fixtures.ui_pack();
    let contract = fixtures.contract(&pack, "ui.zip");
    let recipe = Fixtures::recipe(&contract);
    let data = directory.path().join("data");
    let package = directory.path().join("package");
    fs::create_dir_all(&package).unwrap();
    let cancel = CancelToken::new();
    let mut reports: Vec<String> = Vec::new();
    let run = |recipe: &Value, reports: &mut Vec<String>| {
        ui_pack::prepare_ui_texture_pack(
            &package,
            &data,
            recipe,
            &pack,
            &mut |_, text| reports.push(text.into()),
            &cancel,
        )
        .unwrap()
    };
    assert_eq!(run(&serde_json::json!({}), &mut reports), None);
    assert_eq!(run(&recipe, &mut reports), None);
    assert_eq!(ui_pack::pack_status(&package, &data, Some(&contract), &pack, &cancel).unwrap().as_str(), "missing");
    fs::create_dir_all(data.join("downloads")).unwrap();
    fs::copy(fixtures.dir.join("ui.zip"), data.join("downloads/ui.zip")).unwrap();
    assert_eq!(ui_pack::pack_status(&package, &data, Some(&contract), &pack, &cancel).unwrap().as_str(), "archive");
    let prepared = run(&recipe, &mut reports).unwrap();
    assert_eq!(ui_pack::pack_status(&package, &data, Some(&contract), &pack, &cancel).unwrap().as_str(), "ready");
    assert!(reports.iter().any(|text| text.contains("from the original archive")));
    // A corrupt stored archive keeps the native textures and says why.
    fs::remove_file(prepared.join("source.json")).unwrap();
    fs::write(data.join("downloads/ui.zip"), vec![b'x'; contract.bytes as usize]).unwrap();
    reports.clear();
    assert_eq!(run(&recipe, &mut reports), None);
    assert!(reports.iter().any(|text| text.contains("native UI textures")));
}

#[test]
fn catalogue_atlas_is_the_hd_ui_atlas() {
    assert_eq!(UI_ATLAS_HD_TEXTURE, "UI/tex1_512x256_1FAF9B5B2ADF6038_13_mip0.png");
    assert_eq!(ui_pack::prepared_path(UI_ATLAS_HD_TEXTURE), format!("common/{UI_ATLAS_HD_TEXTURE}"));
}

fn replacements(fixtures: &Fixtures, paths: &[&str]) -> BTreeMap<String, PathBuf> {
    paths
        .iter()
        .map(|path| {
            (path.to_string(), path.split('/').fold(fixtures.dir.join("mod_folder"), |base, part| base.join(part)))
        })
        .collect()
}

#[test]
fn overlay_appends_payloads_and_repoints_only_replaced_records() {
    let (directory, fixtures) = fixtures();
    let cancel = CancelToken::new();
    let output = directory.path().join("overlay/romfs_overlay.o3ro");
    let paths = ["actor/a.zar", "misc/menu.zar"];
    let summary = overlay::build_romfs_overlay(
        &fixtures.dir.join("romfs.bin"),
        PREFIX,
        fixtures.view.len() as u64,
        &replacements(&fixtures, &paths),
        &output,
        &cancel,
    )
    .unwrap();
    assert_eq!(summary.files, 2);
    assert_eq!(summary.base_view_bytes, fixtures.view.len() as u64);
    let composed = compose(&fs::read(&output).unwrap(), &fixtures.view);
    let records = overlay::romfs_file_records(&mut std::io::Cursor::new(&composed), 0, composed.len() as u64).unwrap();
    let data_offset = u64::from(records.header[9]);
    for (name, expected) in fixtures.base_files.iter().map(|(n, b)| (n.as_str(), b.as_slice())).chain(
        fixtures.mod_files.iter().filter(|(n, _)| paths.contains(&n.as_str())).map(|(n, b)| (n.as_str(), b.as_slice())),
    ) {
        let record = records.files[name];
        let replaced = fixtures.mod_files.iter().find(|(n, _)| n == name && paths.contains(&name));
        let want = replaced.map_or(expected, |(_, bytes)| bytes.as_slice());
        let start = (data_offset + record.payload) as usize;
        assert_eq!(&composed[start..start + record.length as usize], want, "{name}");
        if replaced.is_some() {
            assert_eq!(start % 16, 0);
            assert!(start >= fixtures.view.len());
        }
    }
    // Base bytes stay in place.
    let untouched = &fixtures.base_files["actor/b.zar"];
    assert_eq!(
        composed[..fixtures.view.len()].windows(untouched.len()).filter(|w| w == &untouched.as_slice()).count(),
        1
    );
    let error = overlay::build_romfs_overlay(
        &fixtures.dir.join("romfs.bin"),
        PREFIX,
        fixtures.view.len() as u64,
        &BTreeMap::from([("actor/missing.zar".to_string(), fixtures.dir.join("mod_folder/actor/a.zar"))]),
        &output,
        &cancel,
    )
    .unwrap_err();
    assert!(error.message.contains("no actor/missing.zar"));
}

#[test]
fn prepare_is_inert_when_disabled_and_cached_when_enabled() {
    let (directory, fixtures) = fixtures();
    let cancel = CancelToken::new();
    let data = directory.path().join("data");
    let package = directory.path().join("package");
    let catalog = fixtures.catalog();
    let mod_ = fixtures.mod_();
    let contract = ArchiveContract {
        kind: mod_.archive_kind.clone(),
        name: "fixture.rar".into(),
        bytes: 1,
        sha256: "0".repeat(64),
    };
    let recipe = Fixtures::recipe(&contract);
    let romfs = fixtures.dir.join("romfs.bin");
    let romfs_sha = forge_base::hashing::sha256_hex(&fs::read(&romfs).unwrap());
    let extractor = SystemBsdtar::default();
    let mut reports: Vec<String> = Vec::new();
    let mut calls = 0;
    let prepare = |reports: &mut Vec<String>, calls: &mut i32| {
        let context = PrepareContext {
            catalog: &catalog,
            root: &package,
            data_root: &data,
            recipe: &recipe,
            romfs: &romfs,
            romfs_sha256: &romfs_sha,
            extractor: &extractor,
            cancel: &cancel,
        };
        overlay::prepare_romfs_mods(
            &context,
            &mut || {
                *calls += 1;
                Ok((PREFIX, fixtures.view.len() as u64))
            },
            &mut |_, text| reports.push(text.into()),
        )
        .unwrap()
    };
    assert_eq!(prepare(&mut reports, &mut calls), None);
    assert_eq!(calls, 0);
    romfs_mods::set_mod_enabled(&data, &catalog, "fixture", true).unwrap();
    assert_eq!(prepare(&mut reports, &mut calls), None);
    assert!(reports.last().unwrap().contains("not downloaded"));
    romfs_mods::import_extracted(&fixtures.dir.join("mod_folder"), &data, &mod_, &cancel).unwrap();
    let overlay_path = prepare(&mut reports, &mut calls).unwrap();
    assert_eq!(overlay_path.file_name().unwrap(), "romfs_overlay.o3ro");
    let receipt: Value =
        serde_json::from_slice(&fs::read(overlay_path.parent().unwrap().join("import.json")).unwrap()).unwrap();
    assert_eq!(receipt["redistributable"], false);
    assert!(receipt["mods"][0]["attribution"].as_str().unwrap().contains("HugAMuffin"));
    assert_eq!(receipt["source"]["mods"][0][0], "fixture");
    let stamp = fs::metadata(&overlay_path).unwrap().modified().unwrap();
    assert_eq!(prepare(&mut reports, &mut calls).as_deref(), Some(overlay_path.as_path()));
    assert_eq!(fs::metadata(&overlay_path).unwrap().modified().unwrap(), stamp);
    // A damaged receipt or overlay is rebuilt.
    fs::write(&overlay_path, b"damaged").unwrap();
    let rebuilt = prepare(&mut reports, &mut calls).unwrap();
    assert!(fs::read(&rebuilt).unwrap().starts_with(b"O3RO"));
    let composed = compose(&fs::read(&rebuilt).unwrap(), &fixtures.view);
    let records = overlay::romfs_file_records(&mut std::io::Cursor::new(&composed), 0, composed.len() as u64).unwrap();
    let record = records.files["actor/a.zar"];
    let start = (u64::from(records.header[9]) + record.payload) as usize;
    assert_eq!(&composed[start..start + record.length as usize], fixtures.mod_files[0].1.as_slice());
}

/// The unsupported custom RomFS folder (owner decision 6): off by default,
/// applied through the same overlay when on, files the RomFS lacks left out,
/// a catalogued mod's file overridden (reported), a rejected folder left out
/// whole, and off again exactly the catalogue-only result.
#[test]
fn the_custom_romfs_folder_is_inert_until_enabled_and_wins_over_mods() {
    use forge_mods::custom_romfs;
    let (directory, fixtures) = fixtures();
    let cancel = CancelToken::new();
    let data = directory.path().join("data");
    let catalog = fixtures.catalog();
    let mod_ = fixtures.mod_();
    let contract =
        ArchiveContract { kind: mod_.archive_kind.clone(), name: "f.rar".into(), bytes: 1, sha256: "0".repeat(64) };
    let recipe = Fixtures::recipe(&contract);
    let romfs = fixtures.dir.join("romfs.bin");
    let extractor = SystemBsdtar::default();
    let prepare = |reports: &mut Vec<String>| {
        let context = PrepareContext {
            catalog: &catalog,
            root: directory.path(),
            data_root: &data,
            recipe: &recipe,
            romfs: &romfs,
            romfs_sha256: &"e".repeat(64),
            extractor: &extractor,
            cancel: &cancel,
        };
        overlay::prepare_romfs_mods(&context, &mut || Ok((PREFIX, fixtures.view.len() as u64)), &mut |_, text| {
            reports.push(text.into())
        })
        .unwrap()
    };
    let folder = custom_romfs::folder(&data);
    write_file(&folder.join("misc/other.zar"), b"custom other payload");
    write_file(&folder.join("actor/a.zar"), b"custom wins over the mod");
    write_file(&folder.join("actor/brand_new.zar"), b"not in the RomFS");
    let mut reports = Vec::new();
    assert_eq!(prepare(&mut reports), None, "the folder is off by default");
    // The catalogue-only overlay, for the comparison after turning it off.
    romfs_mods::set_mod_enabled(&data, &catalog, "fixture", true).unwrap();
    romfs_mods::import_extracted(&fixtures.dir.join("mod_folder"), &data, &mod_, &cancel).unwrap();
    let catalogue_only = prepare(&mut reports).unwrap();
    let catalogue_bytes = fs::read(&catalogue_only).unwrap();

    custom_romfs::set_enabled(&data, true).unwrap();
    reports.clear();
    let with_custom = prepare(&mut reports).unwrap();
    assert_ne!(with_custom, catalogue_only, "a new identity, a new overlay folder");
    assert!(reports.iter().any(|text| text.contains("has no actor/brand_new.zar")), "{reports:?}");
    assert!(reports.iter().any(|text| text.contains("actor/a.zar is also replaced by")), "{reports:?}");
    let composed = compose(&fs::read(&with_custom).unwrap(), &fixtures.view);
    let records = overlay::romfs_file_records(&mut std::io::Cursor::new(&composed), 0, composed.len() as u64).unwrap();
    let payload = |name: &str| {
        let record = records.files[name];
        let start = (u64::from(records.header[9]) + record.payload) as usize;
        composed[start..start + record.length as usize].to_vec()
    };
    assert_eq!(payload("actor/a.zar"), b"custom wins over the mod");
    assert_eq!(payload("misc/other.zar"), b"custom other payload");
    assert_eq!(payload("misc/menu.zar"), fixtures.mod_files.iter().find(|(n, _)| n == "misc/menu.zar").unwrap().1);
    let receipt: Value =
        serde_json::from_slice(&fs::read(with_custom.parent().unwrap().join("import.json")).unwrap()).unwrap();
    assert_eq!(receipt["custom_romfs"]["unsupported"], true);
    assert_eq!(receipt["custom_romfs"]["paths"], serde_json::json!(["actor/a.zar", "misc/other.zar"]));
    assert_eq!(receipt["source"]["custom_romfs"].as_array().unwrap().len(), 2);

    // A rejected folder is left out whole: the catalogue-only overlay.
    write_file(&folder.join("actor/empty.zar"), b"");
    reports.clear();
    assert_eq!(prepare(&mut reports), Some(catalogue_only.clone()));
    assert!(reports.iter().any(|text| text.contains("custom RomFS folder is not applied")), "{reports:?}");
    fs::remove_file(folder.join("actor/empty.zar")).unwrap();

    // Off again: byte-identical to never having turned it on.
    custom_romfs::set_enabled(&data, false).unwrap();
    let off = prepare(&mut reports).unwrap();
    assert_eq!(off, catalogue_only);
    assert_eq!(fs::read(&off).unwrap(), catalogue_bytes);
    // Only the custom folder, without mods.
    romfs_mods::set_mod_enabled(&data, &catalog, "fixture", false).unwrap();
    assert_eq!(prepare(&mut reports), None);
    custom_romfs::set_enabled(&data, true).unwrap();
    let alone = prepare(&mut reports).unwrap();
    let receipt: Value =
        serde_json::from_slice(&fs::read(alone.parent().unwrap().join("import.json")).unwrap()).unwrap();
    assert_eq!(receipt["mods"], serde_json::json!([]));
    assert_eq!(receipt["custom_romfs"]["files"], 2);
}

#[test]
fn a_romfs_without_a_replaced_file_keeps_the_native_files() {
    let (directory, fixtures) = fixtures();
    let cancel = CancelToken::new();
    let data = directory.path().join("data");
    let catalog = fixtures.catalog();
    let mod_ = fixtures.mod_();
    let mut base = fixtures.base_files.clone();
    base.remove("misc/menu.zar");
    let view = build_level3(&base);
    let mut image = b"IVFC".to_vec();
    image.resize(PREFIX as usize, 0);
    image.extend(&view);
    let romfs = directory.path().join("romfs.bin");
    fs::write(&romfs, &image).unwrap();
    romfs_mods::set_mod_enabled(&data, &catalog, "fixture", true).unwrap();
    romfs_mods::import_extracted(&fixtures.dir.join("mod_folder"), &data, &mod_, &cancel).unwrap();
    let contract =
        ArchiveContract { kind: mod_.archive_kind.clone(), name: "f.rar".into(), bytes: 1, sha256: "0".repeat(64) };
    let recipe = Fixtures::recipe(&contract);
    let extractor = SystemBsdtar::default();
    let mut reports: Vec<String> = Vec::new();
    let context = PrepareContext {
        catalog: &catalog,
        root: directory.path(),
        data_root: &data,
        recipe: &recipe,
        romfs: &romfs,
        romfs_sha256: &"f".repeat(64),
        extractor: &extractor,
        cancel: &cancel,
    };
    let result = overlay::prepare_romfs_mods(&context, &mut || Ok((PREFIX, view.len() as u64)), &mut |_, text| {
        reports.push(text.into())
    })
    .unwrap();
    assert_eq!(result, None);
    let last = reports.last().unwrap();
    assert!(last.contains("no misc/menu.zar") && last.contains("native files"), "{last}");
}

#[test]
fn archive_extraction_uses_libarchive_and_takes_only_catalogued_members() {
    let Some(tool) = forge_mods::find_bsdtar() else {
        eprintln!("skipped: no libarchive bsdtar on this host");
        return;
    };
    let (directory, fixtures) = fixtures();
    let cancel = CancelToken::new();
    let mod_ = fixtures.mod_();
    let contract = fixtures.contract(&mod_, "mod.zip");
    let data = directory.path().join("data");
    let extractor = SystemBsdtar { tool: Some(tool) };
    let stored =
        romfs_mods::extract_archive(&fixtures.dir.join("mod.zip"), &data, &mod_, &contract, &extractor, &cancel)
            .unwrap();
    for (name, bytes) in &fixtures.mod_files {
        assert_eq!(&fs::read(&stored[name]).unwrap(), bytes);
    }
    assert!(!romfs_mods::source_directory(&data, &mod_).join("readme.txt").exists());
    let mut wrong = contract.clone();
    wrong.sha256 = "0".repeat(64);
    let error = romfs_mods::extract_archive(&fixtures.dir.join("mod.zip"), &data, &mod_, &wrong, &extractor, &cancel)
        .unwrap_err();
    assert!(error.message.contains("failed verification"));
    // A member the archive lacks is an extraction error with the folder hint.
    let mut missing = mod_.clone();
    missing.members.push(forge_mods::Member::new("actor/absent.zar", 1, "0".repeat(64)));
    let error =
        romfs_mods::extract_archive(&fixtures.dir.join("mod.zip"), &data, &missing, &contract, &extractor, &cancel)
            .unwrap_err();
    assert!(error.message.contains("Select extracted folder"), "{}", error.message);
}

#[test]
fn rust_scenarios_run_end_to_end() {
    let (directory, fixtures) = fixtures();
    let out = directory.path().join("out");
    let result = run_rust(&fixtures, &out);
    let results = &result["results"];
    assert_eq!(results["mod_inert"]["calls"], 0);
    assert_eq!(results["mod_reuse"]["same"], true);
    assert_eq!(results["mod_reuse"]["untouched"], true);
    assert_eq!(results["ui_prepare"]["status"], "ready");
    assert_eq!(results["import_archive"]["bytes"], forge_base::hashing::hex(b"original archive bytes"));
    assert!(result["errors"]["settings_malformed"].as_str().unwrap().starts_with("Invalid RomFS mod settings"));
    let _ = archive::downloads_directory(&out);
}

#[test]
fn an_unreadable_zip_method_is_reported_with_the_folder_hint() {
    let (directory, fixtures) = fixtures();
    let mut pack = fixtures.ui_pack();
    pack.members.truncate(1);
    let (name, bytes) = &fixtures.ui_files[0];
    let path = directory.path().join("odd.zip");
    let mut writer = zip::ZipWriter::new(fs::File::create(&path).unwrap());
    let options = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored);
    std::io::Write::write_all(
        &mut {
            writer
                .start_file(format!("{}/{}/{name}", pack.archive_root, forge_mods::catalog::TEXTURE_FOLDER), options)
                .unwrap();
            &mut writer
        },
        bytes,
    )
    .unwrap();
    writer.finish().unwrap();
    // Patch the compression method to an unknown value in both headers.
    let mut data = fs::read(&path).unwrap();
    data[8..10].copy_from_slice(&77u16.to_le_bytes());
    let central = data.windows(4).rposition(|window| window == b"PK\x01\x02").unwrap();
    data[central + 10..central + 12].copy_from_slice(&77u16.to_le_bytes());
    fs::write(&path, &data).unwrap();
    let contract = ArchiveContract {
        kind: pack.archive_kind.clone(),
        name: "odd.zip".into(),
        bytes: data.len() as u64,
        sha256: forge_base::hashing::sha256_hex(&data),
    };
    let error = ui_pack::extract_archive(&path, &directory.path().join("data"), &contract, &pack, &CancelToken::new())
        .unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedFormat);
    assert!(error.message.contains("Select extracted folder"), "{}", error.message);
}
