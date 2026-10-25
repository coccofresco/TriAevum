// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Reuse, invalidation and failure behavior of the preparation stages,
//! translated from test_topscreen_assets.py, test_qbf_font.py,
//! test_water_textures.py, test_scene_lights.py and test_interior_sun.py.

mod common;

use std::cell::{Cell, RefCell};
use std::path::{Path, PathBuf};

use common::{font_bytes, romfs_image};
use forge_assets::interior_sun::{prepare_interior_sun, InteriorSunService, NativeRun, PrebuildCommand};
use forge_assets::{font_pack, qbf, scene_lights, topscreen, water};
use forge_base::{CancelToken, ForgeError, Result};
use serde_json::{json, Value};

fn repository() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../..")
}

fn records() -> Vec<(u16, u16, [u8; 4])> {
    vec![(32, 0, [6, 4, 0, 0]), (42, 1, [5, 6, 0, 0])]
}

#[test]
fn font_pack_is_reproducible_regional_and_published_only_when_valid() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let font = root.join("ltn16.qbf");
    std::fs::write(&font, font_bytes(64, &records())).unwrap();
    let native = font_bytes(16, &records());
    for (region, path) in [("eu", "message/eu/ltn16.qbf"), ("us", "message/us/ltn16.qbf")] {
        let image = root.join(format!("{region}.bin"));
        std::fs::write(&image, romfs_image(&[(path, &native)])).unwrap();
        let output = root.join(format!("{region}.zip"));
        let manifest = font_pack::prepare_font_pack(&font, &image, &output).unwrap();
        let first = std::fs::read(&output).unwrap();
        font_pack::prepare_font_pack(&font, &image, &output).unwrap();
        assert_eq!(first, std::fs::read(&output).unwrap());
        assert_eq!(manifest["fonts"][0]["romfs_path"], path);
        assert_eq!(manifest["minimum_output_height"], 480);
    }
    let output = root.join("existing.zip");
    std::fs::write(&output, b"existing").unwrap();
    std::fs::write(&font, b"broken").unwrap();
    assert!(font_pack::prepare_font_pack(&font, &root.join("eu.bin"), &output).is_err());
    assert_eq!(std::fs::read(&output).unwrap(), b"existing");
    assert!(!root.join("existing.zip.partial").exists());
}

struct Package {
    _directory: tempfile::TempDir,
    root: PathBuf,
    data: PathBuf,
    romfs: PathBuf,
}

fn package() -> Package {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("package");
    let shipped = repository().join("runtime/triaevum_resources/topscreen");
    for (folder, name) in [
        ("singlescreen-3.0", topscreen::UI_ATLAS),
        ("singlescreen-3.0", topscreen::LINK_ANIMATIONS),
        ("singlescreen-3.1", topscreen::HD_FONT),
    ] {
        let target = root.join("resources/topscreen").join(folder).join(name);
        std::fs::create_dir_all(target.parent().unwrap()).unwrap();
        std::fs::copy(shipped.join(folder).join(name), target).unwrap();
    }
    // A native font the shipped HD font covers: its first characters.
    let hd =
        qbf::parse_qbf(&std::fs::read(shipped.join("singlescreen-3.1").join(topscreen::HD_FONT)).unwrap()).unwrap();
    let characters: Vec<(u16, u16, [u8; 4])> = hd
        .characters
        .iter()
        .take(40)
        .enumerate()
        .map(|(index, c)| (c.code, index as u16, [index as u8, 0, 0, 0]))
        .collect();
    let romfs = directory.path().join("romfs.bin");
    std::fs::write(&romfs, romfs_image(&[("message/eu/ltn16.qbf", &font_bytes(16, &characters))])).unwrap();
    Package { data: directory.path().join("data"), root, romfs, _directory: directory }
}

fn recipe() -> Value {
    json!({"title": topscreen::TITLE, "inputs": {"romfs": {"sha256": "a".repeat(64)}}})
}

fn png(width: u32, height: u32) -> Vec<u8> {
    let mut data = b"\x89PNG\r\n\x1a\n".to_vec();
    data.extend(13u32.to_be_bytes());
    data.extend(b"IHDR");
    data.extend(width.to_be_bytes());
    data.extend(height.to_be_bytes());
    data.extend([8, 6, 0, 0, 0]);
    data.extend(b"fixture");
    data
}

fn prepare(package: &Package, romfs: &Path, hd: Option<&Path>, messages: &mut Vec<String>) -> Result<Option<PathBuf>> {
    topscreen::prepare_topscreen_assets(
        &package.root,
        &package.data,
        &recipe(),
        romfs,
        hd,
        &mut |_, text| messages.push(text.to_string()),
        &CancelToken::new(),
    )
}

#[test]
fn topscreen_assets_install_reuse_and_repair() {
    let package = package();
    let mut messages = Vec::new();
    let pack = prepare(&package, &package.romfs, None, &mut messages).unwrap().unwrap();
    assert_eq!(&std::fs::read(&pack).unwrap()[..16], b"O3TU\x02\0\0\0\0\0\0\0\x01\0\0\0");
    assert!(pack.with_file_name("font_coverage.zip").is_file());
    assert!(topscreen::topscreen_ui_atlas_hd(Some(&pack)).is_none());
    let receipt: Value = serde_json::from_slice(&std::fs::read(pack.with_file_name("import.json")).unwrap()).unwrap();
    assert_eq!(receipt["source"]["ui_atlas_hd_sha256"], Value::Null);
    assert_eq!(messages.len(), 2);

    // Reused while the receipts match.
    assert_eq!(prepare(&package, &package.romfs, None, &mut messages).unwrap().unwrap(), pack);
    assert_eq!(messages.len(), 2);

    // Tampered animations are reinstalled; the font stays.
    let animations = topscreen::topscreen_link_animations(Some(&pack)).unwrap();
    std::fs::write(&animations, b"ZAR\x01 tampered").unwrap();
    prepare(&package, &package.romfs, None, &mut messages).unwrap();
    assert_eq!(messages[2..], ["Installing the SingleScreen UI textures and Link animations...".to_string()]);
    assert_eq!(std::fs::metadata(&animations).unwrap().len(), topscreen::LINK_ANIMATIONS_IDENTITY.0);

    // A corrupt font pack is prepared again; the textures stay.
    std::fs::write(pack.with_file_name("font_coverage.zip"), b"corrupt").unwrap();
    prepare(&package, &package.romfs, None, &mut messages).unwrap();
    assert_eq!(messages[3..], ["Preparing the SingleScreen HD font...".to_string()]);

    // The HD UI atlas from the texture pack makes another folder.
    let atlas = package.root.join("pack/ui.png");
    std::fs::create_dir_all(atlas.parent().unwrap()).unwrap();
    std::fs::write(&atlas, png(4096, 2048)).unwrap();
    let hd_pack = prepare(&package, &package.romfs, Some(&atlas), &mut messages).unwrap().unwrap();
    assert_ne!(hd_pack, pack);
    assert_eq!(std::fs::read(topscreen::topscreen_ui_atlas_hd(Some(&hd_pack)).unwrap()).unwrap(), png(4096, 2048));
    assert!(messages.iter().any(|text| text.contains("HD UI texture pack")));
    std::fs::write(&atlas, png(2048, 1024)).unwrap();
    let changed = prepare(&package, &package.romfs, Some(&atlas), &mut messages).unwrap().unwrap();
    assert_eq!(std::fs::read(topscreen::topscreen_ui_atlas_hd(Some(&changed)).unwrap()).unwrap(), png(2048, 1024));
}

#[test]
fn topscreen_font_failure_is_not_cached_and_package_damage_is_fatal() {
    let package = package();
    let empty = package.root.join("empty.bin");
    std::fs::write(&empty, romfs_image(&[("misc/x.bin", b"x")])).unwrap();
    let error = prepare(&package, &empty, None, &mut Vec::new()).unwrap_err();
    assert_eq!(error.message, "ROM contains no supported Latin QBF font");
    let mut receipts = Vec::new();
    collect(&package.data, &mut receipts);
    assert!(!receipts.iter().any(|path| path.ends_with("fonts_import.json")));
    prepare(&package, &package.romfs, None, &mut Vec::new()).unwrap();

    let atlas = package.root.join("resources/topscreen/singlescreen-3.0").join(topscreen::UI_ATLAS);
    std::fs::write(&atlas, b"tampered").unwrap();
    let error = prepare(&package, &package.romfs, None, &mut Vec::new()).unwrap_err();
    assert!(error.message.contains("package is incomplete or modified"));
    std::fs::remove_file(&atlas).unwrap();
    assert!(prepare(&package, &package.romfs, None, &mut Vec::new())
        .unwrap_err()
        .message
        .contains(topscreen::UI_ATLAS));
}

fn collect(directory: &Path, out: &mut Vec<PathBuf>) {
    for entry in std::fs::read_dir(directory).into_iter().flatten().flatten() {
        if entry.path().is_dir() {
            collect(&entry.path(), out);
        } else {
            out.push(entry.path());
        }
    }
}

#[test]
fn catalogs_are_reused_while_their_receipts_match() {
    let directory = tempfile::tempdir().unwrap();
    let romfs = directory.path().join("romfs");
    std::fs::create_dir_all(romfs.join("scene")).unwrap();
    let data = directory.path().join("data");
    let recipe = json!({"inputs": {"romfs": {"sha256": "ab".repeat(32)}}});
    let cancel = CancelToken::new();
    let reports = Cell::new(0);
    let mut count = |_: &str, _: &str| reports.set(reports.get() + 1);
    let catalog = water::prepare_water_textures(&data, &recipe, &romfs, &mut count, &cancel).unwrap().unwrap();
    assert_eq!(std::fs::read(&catalog).unwrap(), b"triaevum-water-textures 1\n");
    water::prepare_water_textures(&data, &recipe, &romfs, &mut count, &cancel).unwrap();
    assert_eq!(reports.get(), 1);
    std::fs::write(&catalog, b"tampered").unwrap();
    water::prepare_water_textures(&data, &recipe, &romfs, &mut count, &cancel).unwrap();
    assert_eq!(reports.get(), 2);
    assert_eq!(std::fs::read(&catalog).unwrap(), b"triaevum-water-textures 1\n");
    assert!(water::prepare_water_textures(&data, &json!({}), &romfs, &mut count, &cancel).unwrap().is_none());

    let lights =
        scene_lights::prepare_scene_lights(&data, &recipe, &romfs, None, &mut count, &cancel).unwrap().unwrap();
    assert_eq!(std::fs::read(&lights).unwrap(), br#"{"schema":"triaevum.scene-lights","version":1,"rooms":[]}"#);
    scene_lights::prepare_scene_lights(&data, &recipe, &romfs, None, &mut count, &cancel).unwrap();
    assert_eq!(reports.get(), 3);
    let receipt: Value =
        serde_json::from_slice(&std::fs::read(lights.with_file_name("receipt.json")).unwrap()).unwrap();
    assert_eq!(receipt["source"]["generator_revision"], scene_lights::GENERATOR_REVISION);
    assert!(scene_lights::prepare_scene_lights(&data, &json!({}), &romfs, None, &mut count, &cancel)
        .unwrap()
        .is_none());
}

/// A runtime double that writes a valid empty RSSH shell and its result.
struct FakeRuntime {
    sha256: RefCell<String>,
    calls: RefCell<Vec<PrebuildCommand>>,
    status: i32,
    mutate: Box<dyn Fn(&mut Value)>,
}

impl FakeRuntime {
    fn new() -> Self {
        Self {
            sha256: RefCell::new("1".repeat(64)),
            calls: RefCell::new(Vec::new()),
            status: 0,
            mutate: Box::new(|_| {}),
        }
    }
}

impl InteriorSunService for FakeRuntime {
    fn runtime_sha256(&self) -> Result<String> {
        Ok(self.sha256.borrow().clone())
    }

    fn prebuild(&self, command: &PrebuildCommand, _: &CancelToken) -> Result<NativeRun> {
        self.calls.borrow_mut().push(command.clone());
        if self.status != 0 {
            return Ok(NativeRun { status: self.status, stdout: String::new(), stderr: "failure".into() });
        }
        let relative = "shadow-shells/0000000000000001.bin";
        let path = command.cache_directory.join(relative);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        let mut shell = b"RSSH".to_vec();
        shell.extend(3u32.to_le_bytes());
        shell.extend(1u64.to_le_bytes());
        for value in [64u32, 512, 0, 0, 1, 0] {
            shell.extend(value.to_le_bytes());
        }
        shell.extend(0u64.to_le_bytes());
        std::fs::write(&path, shell).unwrap();
        let mut result = json!({"format": "triaevum_interior_sun_v1", "criterion_version": 3, "game_booted": false,
            "cache_directory": command.cache_directory.to_string_lossy(), "rooms": 1, "fallback": 0,
            "entries": [{"status": "built", "fingerprint": "0000000000000001", "interior": true, "path": relative}]});
        (self.mutate)(&mut result);
        std::fs::write(&command.manifest, result.to_string()).unwrap();
        Ok(NativeRun::default())
    }
}

fn sun(
    runtime: &FakeRuntime,
    root: &Path,
    identity: &str,
    table: Option<&Path>,
) -> Result<serde_json::Map<String, Value>> {
    prepare_interior_sun(
        runtime,
        &root.join("data"),
        &root.join("user-romfs"),
        identity,
        &root.join("renderer-cache"),
        table,
        &mut |_, _| {},
        &CancelToken::new(),
    )
}

fn receipts(root: &Path) -> usize {
    std::fs::read_dir(root.join("data/interior-sun"))
        .map(|entries| entries.flatten().filter(|e| e.path().extension().is_some_and(|x| x == "json")).count())
        .unwrap_or(0)
}

#[test]
fn interior_sun_reuses_and_regenerates() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let runtime = FakeRuntime::new();
    let a = "a".repeat(64);
    assert_eq!(sun(&runtime, root, &a, None).unwrap()["receipt_reused"], false);
    assert_eq!(sun(&runtime, root, &a, None).unwrap()["receipt_reused"], true);
    assert_eq!(runtime.calls.borrow().len(), 1);
    let shell = root.join("renderer-cache/shadow-shells/0000000000000001.bin");
    let mut data = std::fs::read(&shell).unwrap();
    *data.last_mut().unwrap() ^= 1;
    std::fs::write(&shell, &data).unwrap();
    assert_eq!(sun(&runtime, root, &a, None).unwrap()["receipt_reused"], false);
    std::fs::remove_file(&shell).unwrap();
    assert_eq!(sun(&runtime, root, &a, None).unwrap()["receipt_reused"], false);
    assert_eq!(runtime.calls.borrow().len(), 3);
    let arguments = runtime.calls.borrow()[0].arguments();
    assert_eq!(arguments[0], "--prebuild-interior-sun");
    assert!(!arguments.contains(&"--mask-table".into()));

    // Another RomFS or runtime is another identity.
    let b = "b".repeat(64);
    assert_eq!(sun(&runtime, root, &b, None).unwrap()["receipt_reused"], false);
    *runtime.sha256.borrow_mut() = "2".repeat(64);
    assert_eq!(sun(&runtime, root, &b, None).unwrap()["receipt_reused"], false);
    assert!(sun(&runtime, root, "not-hex", None).is_err());

    // A release mask table is passed and bound to the receipt.
    let table = root.join("interior-sun.masks");
    std::fs::write(&table, b"first table").unwrap();
    assert_eq!(sun(&runtime, root, &a, Some(&table)).unwrap()["receipt_reused"], false);
    assert_eq!(sun(&runtime, root, &a, Some(&table)).unwrap()["receipt_reused"], true);
    std::fs::write(&table, b"second table").unwrap();
    assert_eq!(sun(&runtime, root, &a, Some(&table)).unwrap()["receipt_reused"], false);
    let last = runtime.calls.borrow().last().unwrap().arguments();
    assert!(last.contains(&"--mask-table".into()));
}

#[test]
fn interior_sun_rejects_invalid_results_without_a_receipt() {
    type Mutation = Box<dyn Fn(&mut Value)>;
    let mutations: Vec<Mutation> = vec![
        Box::new(|r| r["game_booted"] = json!(true)),
        Box::new(|r| r["criterion_version"] = json!(2)),
        Box::new(|r| r["entries"][0]["interior"] = json!(false)),
        Box::new(|r| r["entries"][0]["path"] = json!("../outside.bin")),
        Box::new(|r| r["entries"] = json!([])),
        Box::new(|r| r["rooms"] = json!(true)),
    ];
    for mutate in mutations {
        let directory = tempfile::tempdir().unwrap();
        let runtime = FakeRuntime { mutate, ..FakeRuntime::new() };
        let error: ForgeError = sun(&runtime, directory.path(), &"a".repeat(64), None).unwrap_err();
        assert!(error.message.starts_with("Invalid interior-sun"), "{}", error.message);
        assert_eq!(receipts(directory.path()), 0);
    }
    let directory = tempfile::tempdir().unwrap();
    let failing = FakeRuntime { status: 1, ..FakeRuntime::new() };
    let error = sun(&failing, directory.path(), &"a".repeat(64), None).unwrap_err();
    assert_eq!(error.message, "Interior-sun preparation failed: failure");
    assert_eq!(receipts(directory.path()), 0);

    // A documented native fallback is a valid, reusable result.
    let fallback = FakeRuntime {
        mutate: Box::new(|r| {
            r["fallback"] = json!(1);
            r["entries"] = json!([{"status": "fallback", "reason": "unsupported CMB geometry"}]);
        }),
        ..FakeRuntime::new()
    };
    let directory = tempfile::tempdir().unwrap();
    assert_eq!(sun(&fallback, directory.path(), &"a".repeat(64), None).unwrap()["fallback"], 1);
    assert_eq!(sun(&fallback, directory.path(), &"a".repeat(64), None).unwrap()["receipt_reused"], true);
}
