// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Synthetic fixtures (a small ZIP pack, extracted folders, a Level-3 RomFS and
//! mod members) and the scenario runner shared by the behavior tests and the
//! Python oracle test. Fixtures use test-only contracts, never real archives.

#![allow(dead_code)]

use std::collections::BTreeMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use forge_base::hashing::sha256_hex;
use forge_base::CancelToken;
use forge_mods::catalog::{Member, UI_ATLAS_HD_TEXTURE};
use forge_mods::{ArchiveContract, Catalog, CataloguedArchive, PrepareContext, RomFsMod, SystemBsdtar, UiTexturePack};
use serde_json::{json, Map, Value};

pub const PREFIX: u64 = 0x1000;
const INVALID: u32 = 0xFFFF_FFFF;

/// Deterministic pseudo-random bytes.
pub fn pseudo(seed: u64, length: usize) -> Vec<u8> {
    let mut state = seed.wrapping_mul(0x9E37_79B9_7F4A_7C15) | 1;
    (0..length)
        .map(|_| {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            (state >> 24) as u8
        })
        .collect()
}

pub fn write_file(path: &Path, bytes: &[u8]) {
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(path, bytes).unwrap();
}

// --- Level-3 RomFS builder (port of test_romfs_mods.build_level3) ---------------

fn entry(fixed: &[u8], name: &str) -> Vec<u8> {
    let encoded: Vec<u8> = name.encode_utf16().flat_map(u16::to_le_bytes).collect();
    let mut data = fixed.to_vec();
    data.extend((encoded.len() as u32).to_le_bytes());
    data.extend(encoded);
    while !data.len().is_multiple_of(4) {
        data.push(0);
    }
    data
}

fn leaf(path: &str) -> &str {
    path.rsplit('/').next().unwrap()
}

/// A minimal Level-3 RomFS: the root and one level of directories.
pub fn build_level3(files: &BTreeMap<String, Vec<u8>>) -> Vec<u8> {
    let mut directories: Vec<String> = files
        .keys()
        .filter(|path| path.contains('/'))
        .map(|path| path.split('/').next().unwrap().to_string())
        .collect();
    directories.sort();
    directories.dedup();
    let root_files: Vec<String> = files.keys().filter(|path| !path.contains('/')).cloned().collect();
    let mut dir_offsets: BTreeMap<String, u32> = BTreeMap::from([(String::new(), 0)]);
    let mut offset = entry(&[0; 20], "").len() as u32;
    for name in &directories {
        dir_offsets.insert(name.clone(), offset);
        offset += entry(&[0; 20], name).len() as u32;
    }
    let mut owners = vec![String::new()];
    owners.extend(directories.iter().cloned());
    let group = |owner: &str| -> Vec<String> {
        if owner.is_empty() {
            root_files.clone()
        } else {
            files.keys().filter(|path| path.starts_with(&format!("{owner}/"))).cloned().collect()
        }
    };
    let mut file_offsets: BTreeMap<String, u32> = BTreeMap::new();
    let mut offset = 0u32;
    for owner in &owners {
        for path in group(owner) {
            file_offsets.insert(path.clone(), offset);
            offset += entry(&[0; 28], leaf(&path)).len() as u32;
        }
    }
    let mut payload_offsets = BTreeMap::new();
    let mut data = Vec::new();
    for (path, bytes) in files {
        while !data.len().is_multiple_of(16) {
            data.push(0);
        }
        payload_offsets.insert(path.clone(), data.len() as u64);
        data.extend(bytes);
    }
    let mut file_table = Vec::new();
    for owner in &owners {
        let paths = group(owner);
        for (index, path) in paths.iter().enumerate() {
            let following = paths.get(index + 1).map_or(INVALID, |next| file_offsets[next]);
            let mut fixed = Vec::new();
            fixed.extend(dir_offsets[owner].to_le_bytes());
            fixed.extend(following.to_le_bytes());
            fixed.extend(payload_offsets[path].to_le_bytes());
            fixed.extend((files[path].len() as u64).to_le_bytes());
            fixed.extend(INVALID.to_le_bytes());
            file_table.extend(entry(&fixed, leaf(path)));
        }
    }
    let five = |values: [u32; 5]| -> Vec<u8> { values.into_iter().flat_map(u32::to_le_bytes).collect() };
    let mut dir_table = entry(
        &five([
            0,
            INVALID,
            directories.first().map_or(INVALID, |name| dir_offsets[name]),
            root_files.first().map_or(INVALID, |path| file_offsets[path]),
            INVALID,
        ]),
        "",
    );
    for (index, name) in directories.iter().enumerate() {
        let sibling = directories.get(index + 1).map_or(INVALID, |next| dir_offsets[next]);
        let first = group(name).first().map_or(INVALID, |path| file_offsets[path]);
        dir_table.extend(entry(&five([0, sibling, INVALID, first, INVALID]), name));
    }
    let hash_table = INVALID.to_le_bytes().to_vec();
    let mut layout = Vec::new();
    let mut cursor = 40u32;
    for block in [&hash_table, &dir_table, &hash_table, &file_table] {
        layout.push((cursor, block.len() as u32));
        cursor += block.len() as u32;
    }
    let data_offset = cursor + (16 - cursor % 16) % 16;
    let mut header = 40u32.to_le_bytes().to_vec();
    for (offset, length) in layout {
        header.extend(offset.to_le_bytes());
        header.extend(length.to_le_bytes());
    }
    header.extend(data_offset.to_le_bytes());
    let mut body = header;
    body.extend(&hash_table);
    body.extend(&dir_table);
    body.extend(&hash_table);
    body.extend(&file_table);
    body.resize(data_offset as usize, 0);
    body.extend(data);
    body
}

/// Applies an overlay to a base view the way the runtime does (port of
/// `compose` in test_romfs_mods.py).
pub fn compose(overlay: &[u8], base_view: &[u8]) -> Vec<u8> {
    let u32_at = |at: usize| u32::from_le_bytes(overlay[at..at + 4].try_into().unwrap());
    let u64_at = |at: usize| u64::from_le_bytes(overlay[at..at + 8].try_into().unwrap());
    assert_eq!(&overlay[..4], b"O3RO");
    assert_eq!(u32_at(4), 1);
    assert_eq!(u64_at(8), base_view.len() as u64);
    let view = u64_at(16);
    let count = u32_at(24) as usize;
    let tail = u64_at(32) as usize;
    let mut composed = base_view.to_vec();
    for index in 0..count {
        let at = 64 + 32 * index;
        let (offset, length) = (u64_at(at) as usize, u32_at(at + 8) as usize);
        let (expected, replacement) = (u64_at(at + 16) as usize, u64_at(at + 24) as usize);
        assert_eq!(composed[offset..offset + length], overlay[expected..expected + length]);
        composed[offset..offset + length].copy_from_slice(&overlay[replacement..replacement + length]);
    }
    composed.extend(&overlay[tail..]);
    assert_eq!(composed.len() as u64, view);
    composed
}

// --- Fixtures ---------------------------------------------------------------------

pub struct Fixtures {
    pub dir: PathBuf,
    pub ui_files: Vec<(String, Vec<u8>)>,
    pub mod_files: Vec<(String, Vec<u8>)>,
    pub base_files: BTreeMap<String, Vec<u8>>,
    pub view: Vec<u8>,
    pub archive_root: String,
}

fn members(files: &[(String, Vec<u8>)]) -> Vec<Member> {
    files.iter().map(|(name, bytes)| Member::new(name.clone(), bytes.len() as u64, sha256_hex(bytes))).collect()
}

fn zip_of(path: &Path, entries: &[(String, Vec<u8>)]) {
    let file = fs::File::create(path).unwrap();
    let mut writer = zip::ZipWriter::new(file);
    let options = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
    for (name, bytes) in entries {
        writer.start_file(name.as_str(), options).unwrap();
        writer.write_all(bytes).unwrap();
    }
    writer.finish().unwrap();
}

impl Fixtures {
    pub fn create(dir: &Path) -> Self {
        let pack = UiTexturePack::vanilla_ui_hud();
        let archive_root = pack.archive_root.clone();
        let ui_files: Vec<(String, Vec<u8>)> = vec![
            (UI_ATLAS_HD_TEXTURE.into(), b"\x89PNG atlas".to_vec()),
            ("UI/tex1_512x256_BB00B25B152582B6_4_mip0.png".into(), b"\x89PNG common".to_vec()),
            ("UI/01_US_ENGLISH/tex1_512x256_BB00B25B152582B6_4_mip0.png".into(), b"\x89PNG us english".to_vec()),
            ("UI/ITALIAN/tex1_512x256_8FE4BEB04BC6FBFE_4_mip0.png".into(), b"\x89PNG italian".to_vec()),
            ("BOSH RUSH/tex1_1024x256_20E7AFFF1BE07C10_12_mip0.png".into(), b"\x89PNG boss".to_vec()),
            ("UI/tex1_512x512_792B78634E49D46D_4_mip0.png".into(), pseudo(7, 4_500_000)),
        ];
        let mod_files: Vec<(String, Vec<u8>)> = vec![
            ("actor/a.zar".into(), b"new A payload, longer than before".to_vec()),
            ("misc/menu.zar".into(), b"m".to_vec()),
            ("actor/zelda_big.zar".into(), pseudo(11, 100_003)),
            ("actor/zelda_keep.zar".into(), pseudo(13, 65_537)),
        ];
        // The ZIP: the catalogued textures under the archive root plus decoys.
        let folder = format!("{archive_root}/{}", forge_mods::catalog::TEXTURE_FOLDER);
        let mut zip_entries: Vec<(String, Vec<u8>)> =
            ui_files.iter().map(|(name, bytes)| (format!("{folder}/{name}"), bytes.clone())).collect();
        zip_entries
            .push((format!("{archive_root}/load/mods/0004000000033500/romfs/message/sys8.qbf"), b"not used".to_vec()));
        zip_entries.push((format!("{archive_root}/READ ME [INSTRUCTIONS].txt"), b"readme".to_vec()));
        fs::create_dir_all(dir).unwrap();
        zip_of(&dir.join("ui.zip"), &zip_entries);
        for (name, bytes) in &ui_files {
            write_file(
                &dir.join("ui_folder").join(&archive_root).join(forge_mods::catalog::TEXTURE_FOLDER).join(name),
                bytes,
            );
            let other = if name == UI_ATLAS_HD_TEXTURE { b"\x89PNG other".to_vec() } else { bytes.clone() };
            write_file(
                &dir.join("ui_folder_bad").join(&archive_root).join(forge_mods::catalog::TEXTURE_FOLDER).join(name),
                &other,
            );
        }
        let mut mod_entries = mod_files.clone();
        mod_entries.push(("readme.txt".into(), b"not a RomFS member".to_vec()));
        zip_of(&dir.join("mod.zip"), &mod_entries);
        for (name, bytes) in &mod_files {
            write_file(&dir.join("mod_folder").join(name), bytes);
        }
        write_file(&dir.join("empty.bin"), b"");
        let mut base_files: BTreeMap<String, Vec<u8>> = BTreeMap::new();
        for (index, name) in [
            "actor/a.zar",
            "actor/b.zar",
            "actor/zelda_big.zar",
            "actor/zelda_keep.zar",
            "misc/menu.zar",
            "misc/other.zar",
            "scene/x.zar",
            "root.bin",
        ]
        .into_iter()
        .enumerate()
        {
            base_files.insert(name.into(), pseudo(100 + index as u64, 3 + index * 41));
        }
        let view = build_level3(&base_files);
        let mut image = b"IVFC".to_vec();
        image.resize(PREFIX as usize, 0);
        image.extend(&view);
        image.extend(b"trailing");
        write_file(&dir.join("romfs.bin"), &image);
        Self { dir: dir.to_path_buf(), ui_files, mod_files, base_files, view, archive_root }
    }

    pub fn ui_pack(&self) -> UiTexturePack {
        UiTexturePack { members: members(&self.ui_files), ..UiTexturePack::vanilla_ui_hud() }
    }

    pub fn mod_(&self) -> RomFsMod {
        RomFsMod {
            mod_id: "fixture".into(),
            archive_kind: "fixture_archive".into(),
            version: "0.1".into(),
            downloads: vec!["https://example.invalid/primary".into()],
            members: members(&self.mod_files),
            ..RomFsMod::vanilla_plus_link()
        }
    }

    pub fn catalog(&self) -> Catalog {
        let mut catalog = Catalog::builtin();
        catalog.mods.push(self.mod_());
        catalog
    }

    pub fn contract(&self, archive: &dyn CataloguedArchive, file: &str) -> ArchiveContract {
        let bytes = fs::read(self.dir.join(file)).unwrap();
        ArchiveContract {
            kind: archive.archive_kind().to_string(),
            name: file.to_string(),
            bytes: bytes.len() as u64,
            sha256: sha256_hex(&bytes),
        }
    }

    pub fn recipe(contract: &ArchiveContract) -> Value {
        json!({"optional_inputs": {contract.kind.clone(): {
            "name": contract.name, "bytes": contract.bytes, "sha256": contract.sha256}}})
    }

    pub fn ui_levels(&self) -> Vec<String> {
        let root = &self.archive_root;
        vec![
            "ui_folder".into(),
            format!("ui_folder/{root}"),
            format!("ui_folder/{root}/load"),
            format!("ui_folder/{root}/load/textures"),
            format!("ui_folder/{root}/load/textures/0004000000033500"),
        ]
    }

    pub fn overlay_variants() -> BTreeMap<&'static str, Vec<&'static str>> {
        BTreeMap::from([
            ("all", vec!["actor/a.zar", "actor/zelda_big.zar", "actor/zelda_keep.zar", "misc/menu.zar"]),
            ("one", vec!["actor/a.zar"]),
            ("odd", vec!["misc/menu.zar", "actor/zelda_keep.zar"]),
        ])
    }

    /// The scenario spec the Python oracle script reads.
    pub fn spec(&self, out: &Path) -> Value {
        let list = |files: &[(String, Vec<u8>)]| -> Vec<Value> {
            files.iter().map(|(name, bytes)| json!([name, bytes.len(), sha256_hex(bytes)])).collect()
        };
        json!({
            "fixtures": self.dir.to_string_lossy(), "out": out.to_string_lossy(),
            "prefix": PREFIX, "view_size": self.view.len(),
            "ui_members": list(&self.ui_files), "mod_members": list(&self.mod_files),
            "ui_levels": self.ui_levels(), "overlay_variants": Self::overlay_variants(),
        })
    }
}

// --- Trees ------------------------------------------------------------------------

/// Every file (relative path to SHA-256) and directory below `root`, sorted.
pub fn tree(root: &Path) -> BTreeMap<String, String> {
    fn walk(base: &Path, directory: &Path, into: &mut BTreeMap<String, String>) {
        for entry in fs::read_dir(directory).unwrap() {
            let path = entry.unwrap().path();
            let relative = path.strip_prefix(base).unwrap().to_string_lossy().replace('\\', "/");
            if path.is_dir() {
                into.insert(relative, "<dir>".into());
                walk(base, &path, into);
            } else {
                into.insert(relative, sha256_hex(&fs::read(&path).unwrap()));
            }
        }
    }
    let mut into = BTreeMap::new();
    walk(root, root, &mut into);
    into.remove("results.json");
    into
}

// --- The Rust scenario runner (mirrors tests/oracle/mods_cases.py) -----------------

fn relative(path: Option<&Path>, base: &Path) -> Value {
    match path {
        Some(path) => json!(path.strip_prefix(base).unwrap().to_string_lossy().replace('\\', "/")),
        None => Value::Null,
    }
}

fn pairs(reports: &[(String, String)]) -> Value {
    Value::Array(reports.iter().map(|(stage, text)| json!([stage, text])).collect())
}

pub fn run_rust(fixtures: &Fixtures, out: &Path) -> Value {
    use forge_mods::{archive, overlay, romfs_mods, ui_pack};
    let cancel = CancelToken::new();
    let dir = &fixtures.dir;
    fs::create_dir_all(out).unwrap();
    let mut results = Map::new();
    let mut errors = Map::new();
    let mut guarded = |name: &str, result: forge_base::Result<()>| {
        if let Err(error) = result {
            errors.insert(name.to_string(), json!(error.message));
        }
    };

    let pack = fixtures.ui_pack();
    let ui_zip = dir.join("ui.zip");
    let ui_contract = fixtures.contract(&pack, "ui.zip");
    let ui_recipe = Fixtures::recipe(&ui_contract);
    let mod_ = fixtures.mod_();
    let catalog = fixtures.catalog();
    let mod_contract = fixtures.contract(&mod_, "mod.zip");
    let mod_zip = dir.join("mod.zip");
    let mod_recipe = Fixtures::recipe(&mod_contract);
    let romfs = dir.join("romfs.bin");
    let view = fixtures.view.len() as u64;
    let romfs_sha = sha256_hex(&fs::read(&romfs).unwrap());

    // --- UI pack ---
    let base = out.join("ui_archive");
    let directory = ui_pack::extract_archive(&ui_zip, &base, &ui_contract, &pack, &cancel);
    let prepared = ui_pack::prepared_pack(&base, &pack, &cancel).unwrap();
    let value = relative(directory.as_deref().ok(), &base);
    let atlas = ui_pack::ui_atlas_hd(directory.as_deref().ok());
    results.insert(
        "ui_archive".into(),
        json!({
        "value": value, "prepared": relative(prepared.as_deref(), &base), "atlas": relative(atlas.as_deref(), &base)}),
    );
    guarded("ui_archive", directory.map(|_| ()));

    for (index, level) in fixtures.ui_levels().iter().enumerate() {
        let base = out.join(format!("ui_folder_{index}"));
        let directory = ui_pack::import_extracted(&dir.join(level), &base, &pack, &cancel);
        results.insert(format!("ui_folder_{index}"), json!({"value": relative(directory.as_deref().ok(), &base)}));
        guarded(&format!("ui_folder_{index}"), directory.map(|_| ()));
    }

    let base = out.join("ui_prepare");
    fs::create_dir_all(base.join("downloads")).unwrap();
    fs::copy(&ui_zip, base.join("downloads/ui.zip")).unwrap();
    let package = out.join("ui_prepare_package");
    fs::create_dir_all(&package).unwrap();
    let mut reports = Vec::new();
    let directory = ui_pack::prepare_ui_texture_pack(
        &package,
        &base,
        &ui_recipe,
        &pack,
        &mut |stage, text| reports.push((stage.to_string(), text.to_string())),
        &cancel,
    )
    .unwrap();
    results.insert(
        "ui_prepare".into(),
        json!({
        "value": relative(directory.as_deref(), &base), "reports": pairs(&reports),
        "status": ui_pack::pack_status(&package, &base, Some(&ui_contract), &pack, &cancel).unwrap().as_str()}),
    );
    let none_base = out.join("ui_prepare_none");
    let none =
        ui_pack::prepare_ui_texture_pack(&package, &none_base, &json!({}), &pack, &mut |_, _| {}, &cancel).unwrap();
    results.insert(
        "ui_prepare_none".into(),
        json!({
        "value": none.is_none(),
        "status": ui_pack::pack_status(&package, &none_base, Some(&ui_contract), &pack, &cancel).unwrap().as_str()}),
    );

    let mut wrong = ui_contract.clone();
    wrong.sha256 = "0".repeat(64);
    guarded(
        "ui_wrong_archive",
        ui_pack::extract_archive(&ui_zip, &out.join("ui_err1"), &wrong, &pack, &cancel).map(|_| ()),
    );
    let atlas_name = UI_ATLAS_HD_TEXTURE;
    let with_member = |edit: &dyn Fn(&mut Vec<Member>)| {
        let mut changed = pack.clone();
        edit(&mut changed.members);
        changed
    };
    let changed = with_member(&|members| {
        let member = members.iter_mut().find(|member| member.path == atlas_name).unwrap();
        member.bytes = 3;
        member.sha256 = "0".repeat(64);
    });
    guarded(
        "ui_wrong_member",
        ui_pack::extract_archive(&ui_zip, &out.join("ui_err2"), &ui_contract, &changed, &cancel).map(|_| ()),
    );
    let extra = with_member(&|members| members.push(Member::new("UI/missing.png", 1, "0".repeat(64))));
    guarded(
        "ui_missing_member",
        ui_pack::extract_archive(&ui_zip, &out.join("ui_err3"), &ui_contract, &extra, &cancel).map(|_| ()),
    );
    let wrong_hash = with_member(&|members| {
        members.iter_mut().find(|member| member.path == atlas_name).unwrap().sha256 = "0".repeat(64);
    });
    guarded(
        "ui_wrong_hash",
        ui_pack::extract_archive(&ui_zip, &out.join("ui_err4"), &ui_contract, &wrong_hash, &cancel).map(|_| ()),
    );
    guarded("ui_folder_missing", ui_pack::import_extracted(dir, &out.join("ui_err5"), &pack, &cancel).map(|_| ()));
    guarded(
        "ui_folder_differs",
        ui_pack::import_extracted(&dir.join("ui_folder_bad"), &out.join("ui_err6"), &pack, &cancel).map(|_| ()),
    );

    // --- RomFS mod ---
    let base = out.join("mod_folder");
    let package = out.join("mod_package");
    fs::create_dir_all(&package).unwrap();
    let mut reports: Vec<(String, String)> = Vec::new();
    let mut calls = 0usize;
    let extractor = SystemBsdtar::default();
    let prepare = |base: &Path, calls: &mut usize, reports: &mut Vec<(String, String)>| {
        let context = PrepareContext {
            catalog: &catalog,
            root: &package,
            data_root: base,
            recipe: &mod_recipe,
            romfs: &romfs,
            romfs_sha256: &romfs_sha,
            extractor: &extractor,
            cancel: &cancel,
        };
        overlay::prepare_romfs_mods(
            &context,
            &mut || {
                *calls += 1;
                Ok((PREFIX, view))
            },
            &mut |stage, text| reports.push((stage.to_string(), text.to_string())),
        )
        .unwrap()
    };
    let first = prepare(&base, &mut calls, &mut reports);
    results.insert("mod_inert".into(), json!({"value": first.is_none(), "calls": calls, "reports": pairs(&reports)}));
    results.insert(
        "mod_set".into(),
        json!({"enabled": romfs_mods::set_mod_enabled(&base, &catalog, "fixture", true).unwrap()}),
    );
    results.insert(
        "mod_set_twice".into(),
        json!({"enabled": romfs_mods::set_mod_enabled(&base, &catalog, "fixture", true).unwrap()}),
    );
    reports.clear();
    let second = prepare(&base, &mut calls, &mut reports);
    results.insert("mod_missing".into(), json!({"value": second.is_none(), "reports": pairs(&reports)}));
    let members = romfs_mods::import_extracted(&dir.join("mod_folder"), &base, &mod_, &cancel).unwrap();
    results.insert(
        "mod_import".into(),
        json!({
        "members": members.keys().collect::<Vec<_>>(),
        "status": romfs_mods::mod_status(&package, &base, &mod_, Some(&mod_contract), &cancel).unwrap().as_str()}),
    );
    reports.clear();
    let overlay_path = prepare(&base, &mut calls, &mut reports);
    results.insert(
        "mod_prepare".into(),
        json!({"value": relative(overlay_path.as_deref(), &base), "reports": pairs(&reports)}),
    );
    let overlay_path = overlay_path.unwrap();
    let stamp = fs::metadata(&overlay_path).unwrap().modified().unwrap();
    reports.clear();
    let again = prepare(&base, &mut calls, &mut reports);
    results.insert(
        "mod_reuse".into(),
        json!({
        "same": again.as_deref() == Some(overlay_path.as_path()),
        "untouched": fs::metadata(&overlay_path).unwrap().modified().unwrap() == stamp,
        "reports": pairs(&reports)}),
    );

    let tool = forge_mods::find_bsdtar();
    results.insert("bsdtar".into(), json!({"available": tool.is_some()}));
    if tool.is_some() {
        let base = out.join("mod_archive");
        fs::create_dir_all(base.join("downloads")).unwrap();
        fs::copy(&mod_zip, base.join("downloads/mod.zip")).unwrap();
        romfs_mods::set_mod_enabled(&base, &catalog, "fixture", true).unwrap();
        let mut reports = Vec::new();
        let overlay_path = prepare(&base, &mut calls, &mut reports);
        results.insert(
            "mod_archive".into(),
            json!({"value": relative(overlay_path.as_deref(), &base), "reports": pairs(&reports)}),
        );
        let mut wrong = mod_contract.clone();
        wrong.sha256 = "0".repeat(64);
        guarded(
            "mod_wrong_archive",
            romfs_mods::extract_archive(&mod_zip, &out.join("mod_err1"), &mod_, &wrong, &extractor, &cancel)
                .map(|_| ()),
        );
    }

    let sources = dir.join("mod_folder");
    let replacement_map = |paths: &[&str]| -> BTreeMap<String, PathBuf> {
        paths.iter().map(|path| (path.to_string(), forge_mods_join(&sources, path))).collect()
    };
    for (name, paths) in Fixtures::overlay_variants() {
        let target = out.join("overlays").join(format!("{name}.o3ro"));
        let summary = overlay::build_romfs_overlay(&romfs, PREFIX, view, &replacement_map(&paths), &target, &cancel);
        let value = summary.as_ref().ok().map(|summary| {
            json!({"files": summary.files, "base_view_bytes": summary.base_view_bytes, "appended_bytes": summary.appended_bytes})
        });
        results.insert(format!("overlay_{name}"), json!({"summary": value}));
        guarded(&format!("overlay_{name}"), summary.map(|_| ()));
    }
    let a_zar = replacement_map(&["actor/a.zar"]);
    let output = |name: &str| out.join("overlays").join(name);
    guarded(
        "overlay_missing_path",
        overlay::build_romfs_overlay(
            &romfs,
            PREFIX,
            view,
            &BTreeMap::from([("actor/missing.zar".to_string(), a_zar["actor/a.zar"].clone())]),
            &output("missing.o3ro"),
            &cancel,
        )
        .map(|_| ()),
    );
    guarded(
        "overlay_empty",
        overlay::build_romfs_overlay(&romfs, PREFIX, view, &BTreeMap::new(), &output("e.o3ro"), &cancel).map(|_| ()),
    );
    guarded(
        "overlay_outside",
        overlay::build_romfs_overlay(&romfs, PREFIX, 1_000_000_000, &a_zar, &output("o.o3ro"), &cancel).map(|_| ()),
    );
    let zip_size = fs::metadata(&mod_zip).unwrap().len();
    guarded(
        "overlay_not_romfs",
        overlay::build_romfs_overlay(&mod_zip, 0, zip_size, &a_zar, &output("n.o3ro"), &cancel).map(|_| ()),
    );
    guarded(
        "overlay_zero_payload",
        overlay::build_romfs_overlay(
            &romfs,
            PREFIX,
            view,
            &BTreeMap::from([("actor/a.zar".to_string(), dir.join("empty.bin"))]),
            &output("z.o3ro"),
            &cancel,
        )
        .map(|_| ()),
    );

    // Settings and contract errors.
    let base = out.join("settings");
    guarded("set_unknown", romfs_mods::set_mod_enabled(&base, &catalog, "unknown", true).map(|_| ()));
    let settings = romfs_mods::settings_path(&base);
    fs::create_dir_all(settings.parent().unwrap()).unwrap();
    fs::write(&settings, r#"{"enabled": "yes"}"#).unwrap();
    guarded("settings_malformed", romfs_mods::enabled_mods(&base, &catalog).map(|_| ()));
    fs::write(&settings, r#"{"format":"triaevum_romfs_mods_v1","enabled":["x","fixture"]}"#).unwrap();
    results.insert("settings_filtered".into(), json!({"enabled": romfs_mods::enabled_mods(&base, &catalog).unwrap()}));
    fs::write(&settings, "{").unwrap();
    guarded("settings_not_json", romfs_mods::enabled_mods(&base, &catalog).map(|_| ()));
    let disagree = json!({"recipes": [mod_recipe, {"optional_inputs": {"fixture_archive": {"name": "b.rar", "bytes": 1, "sha256": "AB"}}}]});
    guarded("recipes_disagree", archive::archive_contract(&disagree, &mod_).map(|_| ()));
    let lower = archive::archive_contract(
        &json!({"optional_inputs": {"fixture_archive": {"name": "x", "bytes": 7, "sha256": "ABCDEF"}}}),
        &mod_,
    )
    .unwrap()
    .unwrap();
    results.insert("contract_lower".into(), json!({"value": [lower.kind, lower.name, lower.bytes, lower.sha256]}));

    // import_archive.
    let base = out.join("import_archive");
    let picked = out.join("picked.bin");
    fs::write(&picked, b"original archive bytes").unwrap();
    let contract = ArchiveContract {
        kind: mod_.archive_kind.clone(),
        name: "fixture.rar".into(),
        bytes: 22,
        sha256: sha256_hex(b"original archive bytes"),
    };
    guarded(
        "import_archive_missing",
        archive::import_archive(&out.join("nope.bin"), &base, &mod_, &contract, &cancel).map(|_| ()),
    );
    fs::write(&picked, b"original archive byteX").unwrap();
    guarded("import_archive_wrong", archive::import_archive(&picked, &base, &mod_, &contract, &cancel).map(|_| ()));
    fs::write(&picked, b"original archive bytes").unwrap();
    let stored = archive::import_archive(&picked, &base, &mod_, &contract, &cancel).unwrap();
    results.insert(
        "import_archive".into(),
        json!({
        "value": relative(Some(&stored), &base), "bytes": forge_base::hashing::hex(&fs::read(&stored).unwrap())}),
    );

    json!({"results": results, "errors": errors})
}

fn forge_mods_join(base: &Path, relative: &str) -> PathBuf {
    relative.split('/').fold(base.to_path_buf(), |path, part| path.join(part))
}

/// Normalizes the one root directory out of path-bearing messages (a targeted
/// replacement, not a general path scrub).
pub fn normalize(value: &Value, out: &Path) -> Value {
    let root = out.to_string_lossy().replace('\\', "/");
    fn walk(value: &Value, root: &str) -> Value {
        match value {
            Value::String(text) => {
                json!(text.replace("\\\\?\\", "").replace('\\', "/").replace(root, "<OUT>"))
            }
            Value::Array(items) => Value::Array(items.iter().map(|item| walk(item, root)).collect()),
            Value::Object(map) => Value::Object(map.iter().map(|(k, v)| (k.clone(), walk(v, root))).collect()),
            other => other.clone(),
        }
    }
    walk(value, &root)
}
