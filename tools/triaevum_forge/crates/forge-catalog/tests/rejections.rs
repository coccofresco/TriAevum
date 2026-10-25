// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Behavior tests translated from test_oot3d_region_assets.py,
//! test_data_compatibility.py, test_input_adapters.py,
//! test_precompiled_titles.py, test_precompiled_variants.py and
//! test_qualified_input_coverage.py. They run without Python.

use std::path::{Path, PathBuf};

use forge_base::hashing::{sha256_bytes, sha256_hex};
use forge_base::{CancelToken, ErrorKind};
use forge_catalog::inputs::{InputFile, TitleInputs};
use forge_catalog::region_assets::{adapt_qm_table, name_hash, normalize_romfs, ALGORITHM, INVALID};
use forge_catalog::*;
use serde_json::{json, Map, Value};

fn utf16(text: &str) -> Vec<u8> {
    text.encode_utf16().flat_map(u16::to_le_bytes).collect()
}

fn put32(data: &mut [u8], offset: usize, value: u32) {
    data[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn get32(data: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(data[offset..offset + 4].try_into().unwrap())
}

/// test_oot3d_region_assets.qm_fixture.
fn qm_fixture() -> Vec<u8> {
    let mut data = vec![0u8; 16 + 96 * 2];
    data[..4].copy_from_slice(b"QM\0\0");
    put32(&mut data, 4, 4);
    put32(&mut data, 8, 2);
    for (slot, text) in [(1usize, &b"English"[..]), (5, b"French"), (7, b"Spanish")] {
        let length = data.len() as u32;
        put32(&mut data, 16 + 16 + slot * 8, length);
        put32(&mut data, 16 + 20 + slot * 8, text.len() as u32);
        data.extend_from_slice(text);
    }
    data
}

type DirectoryLinks = (usize, Option<usize>, Option<usize>, Option<usize>);

const USA_MOVIES: [&str; 3] = ["hint182.moflex", "hint186.moflex", "hint188.moflex"];

/// test_oot3d_region_assets.romfs_fixture: /message/us/us.qm,
/// /misc/us/texture.bin and the hint movies; one hash bucket, INVALID chains.
fn romfs_fixture(movies: &[&str]) -> Vec<u8> {
    let names = ["", "message", "us", "misc", "us", "hint", "movie"];
    let (mut dirs, mut offsets) = (Vec::new(), Vec::new());
    for name in names {
        offsets.push(dirs.len() as u32);
        dirs.extend_from_slice(&[0u8; 24]);
        dirs.extend_from_slice(&utf16(name));
        dirs.resize(dirs.len().div_ceil(4) * 4, 0);
    }
    let (mut files, mut payload, mut foffsets) = (Vec::new(), Vec::new(), Vec::new());
    let mut entries: Vec<(usize, String, Vec<u8>)> =
        vec![(2, "us.qm".into(), qm_fixture()), (4, "texture.bin".into(), b"unchanged texture".to_vec())];
    entries.extend(movies.iter().map(|movie| (6, movie.to_string(), format!("{movie} payload").into_bytes())));
    for (parent, name, data) in &entries {
        foffsets.push(files.len() as u32);
        let encoded = utf16(name);
        files.extend_from_slice(&offsets[*parent].to_le_bytes());
        files.extend_from_slice(&INVALID.to_le_bytes());
        files.extend_from_slice(&(payload.len() as u64).to_le_bytes());
        files.extend_from_slice(&(data.len() as u64).to_le_bytes());
        files.extend_from_slice(&INVALID.to_le_bytes());
        files.extend_from_slice(&(encoded.len() as u32).to_le_bytes());
        files.extend_from_slice(&encoded);
        files.resize(files.len().div_ceil(4) * 4, 0);
        payload.extend_from_slice(data);
    }
    for index in 2..entries.len() - 1 {
        let next = foffsets[index + 1];
        put32(&mut files, foffsets[index] as usize + 4, next);
    }
    // parent, sibling, first child directory, first child file
    let tree: [DirectoryLinks; 7] = [
        (0, None, Some(1), None),
        (0, Some(3), Some(2), None),
        (1, None, None, Some(0)),
        (0, None, Some(4), None),
        (3, Some(5), None, Some(1)),
        (3, None, Some(6), None),
        (5, None, None, Some(2)),
    ];
    for (index, (parent, sibling, child, file)) in tree.into_iter().enumerate() {
        let at = offsets[index] as usize;
        let link = |value: Option<usize>, table: &[u32]| value.map_or(INVALID, |i| table[i]);
        for (field, value) in [
            offsets[parent],
            link(sibling, &offsets),
            link(child, &offsets),
            link(file, &foffsets),
            INVALID,
            utf16(names[index]).len() as u32,
        ]
        .into_iter()
        .enumerate()
        {
            put32(&mut dirs, at + field * 4, value);
        }
    }
    let (dh, do_) = (40u32, 44u32);
    let (fh, fo) = (do_ + dirs.len() as u32, do_ + dirs.len() as u32 + 4);
    let content = (fo + files.len() as u32 + 15) & !15;
    let mut image = Vec::new();
    for value in [40, dh, 4, do_, dirs.len() as u32, fh, 4, fo, files.len() as u32, content] {
        image.extend_from_slice(&value.to_le_bytes());
    }
    image.extend_from_slice(&[255; 4]);
    image.extend_from_slice(&dirs);
    image.extend_from_slice(&[255; 4]);
    image.extend_from_slice(&files);
    image.resize(content as usize, 0);
    image.extend_from_slice(&payload);
    image
}

/// test_oot3d_region_assets.lookup: the hashed lookup contract.
fn lookup(image: &[u8], path: &str) -> Option<Vec<u8>> {
    let h: Vec<u32> = (0..10).map(|i| get32(image, i * 4)).collect();
    let mut parent = 0u32;
    let parts: Vec<&str> = path.split('/').collect();
    for (index, name) in parts.iter().enumerate() {
        let directory = index != parts.len() - 1;
        let encoded = utf16(name);
        let (hashes, size, table, fixed, chain) =
            if directory { (h[1], h[2], h[3], 24, 16) } else { (h[5], h[6], h[7], 32, 24) };
        let mut offset = get32(image, (hashes + 4 * (name_hash(parent, &encoded) % (size / 4))) as usize);
        let mut found = false;
        while offset != INVALID {
            let p = (table + offset) as usize;
            let length = get32(image, p + fixed - 4) as usize;
            if get32(image, p) == parent && image[p + fixed..p + fixed + length] == encoded[..] {
                if !directory {
                    let start = u64::from_le_bytes(image[p + 8..p + 16].try_into().unwrap()) as usize;
                    let length = u64::from_le_bytes(image[p + 16..p + 24].try_into().unwrap()) as usize;
                    let base = h[9] as usize + start;
                    return Some(image[base..base + length].to_vec());
                }
                parent = offset;
                found = true;
                break;
            }
            offset = get32(image, p + chain);
        }
        if !found {
            return None;
        }
    }
    None
}

fn cancel() -> CancelToken {
    CancelToken::new()
}

#[test]
fn qm_maps_references_without_replacing_payload() {
    let data = qm_fixture();
    let result = adapt_qm_table(&data[..208], data.len() as u64).unwrap();
    for (destination, source) in [(2usize, 1usize), (4, 5), (6, 7)] {
        assert_eq!(result[32 + destination * 8..40 + destination * 8], data[32 + source * 8..40 + source * 8]);
    }
    for slot in [0usize, 3, 8] {
        assert_eq!(result[32 + slot * 8..40 + slot * 8], [0u8; 8]);
    }
    assert_eq!(result[112..], data[112..208]);
}

#[test]
fn qm_rejects_conflicts_and_out_of_range_references() {
    for (offset, size) in [(1u32, 8u32), (10000, 3), (0, 1)] {
        let mut data = qm_fixture();
        put32(&mut data, 40, offset);
        put32(&mut data, 44, size);
        assert_eq!(adapt_qm_table(&data[..208], data.len() as u64).unwrap_err().message, "QM payload is outside file");
    }
    let mut data = qm_fixture();
    data.copy_within(72..80, 48);
    let error = adapt_qm_table(&data[..208], data.len() as u64).unwrap_err();
    assert_eq!(error.message, "QM destination language is already populated");
    assert_eq!(adapt_qm_table(&data[..15], 300).unwrap_err().message, "truncated QM header");
}

#[test]
fn service_view_hashes_paths_and_payload_conservation() {
    for prefix in [Vec::new(), [b"IVFC".to_vec(), vec![0u8; 4092]].concat()] {
        let directory = tempfile::tempdir().unwrap();
        let (source, output) = (directory.path().join("source"), directory.path().join("output"));
        let original = [prefix.clone(), romfs_fixture(&USA_MOVIES)].concat();
        std::fs::write(&source, &original).unwrap();
        let result = normalize_romfs(&source, &output, &cancel()).unwrap();
        let data = std::fs::read(&output).unwrap();
        assert_eq!(get32(&data, 0), 40);
        assert_eq!(std::fs::read(&source).unwrap(), original);
        assert_eq!(lookup(&data, "message/eu/eu.qm").unwrap()[208..], qm_fixture()[208..]);
        assert_eq!(lookup(&data, "misc/eu/texture.bin").unwrap(), b"unchanged texture");
        assert!(lookup(&data, "message/us/us.qm").is_none());
        for (usa, eur) in [(182, 183), (186, 185), (188, 187)] {
            assert_eq!(
                lookup(&data, &format!("misc/hint/movie/hint{eur}.moflex")).unwrap(),
                format!("hint{usa}.moflex payload").into_bytes()
            );
            assert!(lookup(&data, &format!("misc/hint/movie/hint{usa}.moflex")).is_none());
        }
        assert_eq!(result["files"], 5);
        assert_eq!(result["renamed_files"], 4);
        let error = normalize_romfs(&source, &output, &cancel()).unwrap_err();
        assert_eq!(error.message, "normalization requires a regular input and a new output");
        // The service view is a valid Level-3 image with a logical identity.
        fingerprint(&output, &cancel()).unwrap();
    }
}

#[test]
fn hint_movies_require_the_usa_names_only() {
    let extra = [USA_MOVIES[0], USA_MOVIES[1], USA_MOVIES[2], "hint183.moflex"];
    for (movies, message) in [(&USA_MOVIES[..2], "missing USA hint movies"), (&extra[..], "EU hint movies already exist")] {
        let directory = tempfile::tempdir().unwrap();
        let (source, output) = (directory.path().join("source"), directory.path().join("output"));
        std::fs::write(&source, romfs_fixture(movies)).unwrap();
        assert_eq!(normalize_romfs(&source, &output, &cancel()).unwrap_err().message, message);
        assert!(!output.exists());
    }
}

#[test]
fn malformed_romfs_does_not_publish_output() {
    let base = romfs_fixture(&USA_MOVIES);
    let content_dirs = get32(&base, 12) as usize;
    for (damage, message) in [
        ("cycle", "cyclic or duplicate RomFS directory"),
        ("overlap", "invalid or overlapping RomFS metadata extents"),
        ("data", "invalid RomFS data extent"),
        ("truncated", "invalid or overlapping RomFS metadata extents"),
    ] {
        let mut data = base.clone();
        match damage {
            "cycle" => put32(&mut data, content_dirs + 8, 0),
            "overlap" => put32(&mut data, 20, content_dirs as u32),
            "data" => put32(&mut data, 36, 16),
            _ => data.truncate(100),
        }
        let directory = tempfile::tempdir().unwrap();
        let (source, output) = (directory.path().join("source"), directory.path().join("output"));
        std::fs::write(&source, &data).unwrap();
        assert_eq!(normalize_romfs(&source, &output, &cancel()).unwrap_err().message, message, "{damage}");
        assert!(!output.exists());
        assert_eq!(std::fs::read_dir(directory.path()).unwrap().count(), 1, "no partial output");
    }
}

#[test]
fn cancellation_stops_normalization_and_fingerprint() {
    let directory = tempfile::tempdir().unwrap();
    let (source, output) = (directory.path().join("source"), directory.path().join("output"));
    std::fs::write(&source, romfs_fixture(&USA_MOVIES)).unwrap();
    let cancelled = CancelToken::new();
    cancelled.cancel();
    assert_eq!(normalize_romfs(&source, &output, &cancelled).unwrap_err().kind, ErrorKind::Cancelled);
    assert!(!output.exists());
    normalize_romfs(&source, &output, &cancel()).unwrap();
    assert_eq!(fingerprint(&output, &cancelled).unwrap_err().kind, ErrorKind::Cancelled);
}

struct FamilyFixture {
    _directory: tempfile::TempDir,
    code: PathBuf,
    exheader: PathBuf,
    romfs: PathBuf,
    recipe: Map<String, Value>,
}

impl FamilyFixture {
    fn new() -> Self {
        let directory = tempfile::tempdir().unwrap();
        let path = |name: &str| directory.path().join(name);
        let (code, exheader, romfs, source) = (path("code.bin"), path("exheader.bin"), path("romfs.bin"), path("source.romfs"));
        std::fs::write(&code, b"synthetic code and initialized data").unwrap();
        std::fs::write(&exheader, [0u8; 2048]).unwrap();
        std::fs::write(&source, romfs_fixture(&USA_MOVIES)).unwrap();
        normalize_romfs(&source, &romfs, &cancel()).unwrap();
        let identity = |p: &Path| {
            let data = std::fs::read(p).unwrap();
            json!({"bytes": data.len(), "sha256": sha256_hex(&data)})
        };
        let mut recipe = json!({"id": "synthetic", "inputs": {
            "code": identity(&code), "exheader": identity(&exheader), "romfs": identity(&romfs)}});
        let paths = InputPaths { code: &code, exheader: &exheader, romfs: &romfs };
        let family = family::identity(paths, &cancel()).unwrap();
        recipe["data_compatibility"] = json!({"format": CONTENT_FAMILY_FORMAT, "source": family, "execution": family});
        let recipe = recipe.as_object().unwrap().clone();
        Self { _directory: directory, code, exheader, romfs, recipe }
    }

    fn paths(&self) -> InputPaths<'_> {
        InputPaths { code: &self.code, exheader: &self.exheader, romfs: &self.romfs }
    }
}

#[test]
fn family_signature_flags_layout_and_payload() {
    let fixture = FamilyFixture::new();
    let mut header = vec![0u8; 2048];
    header[0x0d] = 3;
    header[0x400..0x600].fill(b'S');
    std::fs::write(&fixture.exheader, &header).unwrap();
    family::verify(&fixture.recipe, fixture.paths(), "source", &cancel()).unwrap();
    header[0x28] = 1;
    std::fs::write(&fixture.exheader, &header).unwrap();
    let error = family::verify(&fixture.recipe, fixture.paths(), "source", &cancel()).unwrap_err();
    assert_eq!(error.message, "Incompatible source data: exheader; no title was activated");
    std::fs::write(&fixture.exheader, [0u8; 2048]).unwrap();

    let before = fingerprint(&fixture.romfs, &cancel()).unwrap();
    let mut data = std::fs::read(&fixture.romfs).unwrap();
    let base = get32(&data, 36) as usize;
    data.splice(base..base, [0u8; 32]);
    put32(&mut data, 36, base as u32 + 32);
    std::fs::write(&fixture.romfs, [b"IVFC".to_vec(), vec![0; 4092], data.clone()].concat()).unwrap();
    assert_eq!(fingerprint(&fixture.romfs, &cancel()).unwrap(), before);

    let last = data.len() - 1;
    data[last] ^= 1;
    std::fs::write(&fixture.romfs, &data).unwrap();
    let error = family::verify(&fixture.recipe, fixture.paths(), "execution", &cancel()).unwrap_err();
    assert_eq!(error.message, "Incompatible execution data: romfs; no title was activated");
    std::fs::write(&fixture.code, b"other code").unwrap();
    let error = family::verify(&fixture.recipe, fixture.paths(), "execution", &cancel()).unwrap_err();
    assert!(error.message.contains("code"), "{}", error.message);
}

#[test]
fn lookup_corruption_rejected_even_with_identical_payload() {
    let fixture = FamilyFixture::new();
    let mut data = std::fs::read(&fixture.romfs).unwrap();
    let offset = get32(&data, 4) as usize;
    put32(&mut data, offset, INVALID);
    std::fs::write(&fixture.romfs, &data).unwrap();
    assert_eq!(fingerprint(&fixture.romfs, &cancel()).unwrap_err().message, "RomFS traversal and lookup differ");
    // The USA fixture itself has no valid lookup chains.
    let directory = tempfile::tempdir().unwrap();
    let usa = directory.path().join("usa");
    std::fs::write(&usa, romfs_fixture(&USA_MOVIES)).unwrap();
    assert!(fingerprint(&usa, &cancel()).is_err());
}

#[test]
fn byte_matching_falls_back_to_the_source_family() {
    let fixture = FamilyFixture::new();
    let file = |path: &Path| {
        let data = std::fs::read(path).unwrap();
        InputFile { path: path.to_path_buf(), bytes: data.len() as u64, sha256: sha256_hex(&data) }
    };
    let directory = tempfile::tempdir().unwrap();
    let wrapped = directory.path().join("romfs-ivfc.bin");
    std::fs::write(&wrapped, [b"IVFC".to_vec(), vec![0; 4092], std::fs::read(&fixture.romfs).unwrap()].concat()).unwrap();
    let inputs = TitleInputs { program_id: 7, code: file(&fixture.code), exheader: file(&fixture.exheader), romfs: file(&wrapped) };
    let recipes = directory.path().join("recipes.json");
    let write_recipes = |list: Value| {
        std::fs::write(&recipes, serde_json::to_vec(&json!({"format": "triaevum_supported_revisions_v1", "recipes": list})).unwrap()).unwrap();
    };
    write_recipes(json!([fixture.recipe]));
    assert_eq!(match_extracted_recipe(&inputs, &recipes, &cancel()).unwrap(), fixture.recipe);
    let mut second = fixture.recipe.clone();
    second.insert("id".into(), json!("second"));
    write_recipes(json!([fixture.recipe, second]));
    let error = match_extracted_recipe(&inputs, &recipes, &cancel()).unwrap_err();
    assert_eq!(error.message, "the decrypted ROM matches multiple revision recipes");
    write_recipes(json!([{"id": "x"}]));
    let error = match_extracted_recipe(&inputs, &recipes, &cancel()).unwrap_err();
    assert_eq!(error.message, "the decrypted ROM does not match any supported game revision (program ID 0000000000000007)");
    assert_eq!(error.kind, ErrorKind::UnsupportedRevision);
}

struct AdapterFixture {
    directory: tempfile::TempDir,
    extracted: TitleInputs,
    recipe: Map<String, Value>,
}

/// test_input_adapters.InputAdapterTests.setUp.
fn adapter_fixture() -> AdapterFixture {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let source = root.join("original");
    std::fs::create_dir(&source).unwrap();
    let mut files = Vec::new();
    for (kind, data) in [("code", b"0123456789abcdef".to_vec()), ("exheader", vec![0u8; 64]), ("romfs", romfs_fixture(&USA_MOVIES))] {
        let path = source.join(format!("{kind}.bin"));
        std::fs::write(&path, &data).unwrap();
        files.push(InputFile { path, bytes: data.len() as u64, sha256: sha256_hex(&data) });
    }
    let [code, exheader, romfs]: [InputFile; 3] = files.try_into().unwrap();
    let extracted = TitleInputs { program_id: 123, code, exheader, romfs };
    normalize_romfs(&extracted.romfs.path, &root.join("expected.romfs"), &cancel()).unwrap();
    let canonical = b"89abcdef01234567";
    let program = copy_adapter::build_program(b"0123456789abcdef", canonical).unwrap();
    let program_path = root.join("recipes/adapters/copies.json");
    forge_base::fsutil::atomic_write_json(&program_path, &program).unwrap();
    let program_bytes = std::fs::read(&program_path).unwrap();
    let expected_romfs = std::fs::read(root.join("expected.romfs")).unwrap();
    let recipe = json!({"id": "fixture-adapted", "inputs": {
        "code": copy_adapter::identity(canonical), "exheader": extracted.exheader.identity(),
        "romfs": copy_adapter::identity(&expected_romfs)},
        "input_adapter": {"format": adapters::FORMAT, "resource_algorithm": ALGORITHM,
            "source_inputs": {"code": extracted.code.identity(), "exheader": extracted.exheader.identity(),
                              "romfs": extracted.romfs.identity()},
            "code_copies": {"path": "recipes/adapters/copies.json", "bytes": program_bytes.len(),
                            "sha256": forge_base::hashing::hex(&sha256_bytes(&program_bytes))}}});
    AdapterFixture { directory, extracted, recipe: recipe.as_object().unwrap().clone() }
}

#[test]
fn adapter_publishes_verified_output_with_receipt() {
    let fixture = adapter_fixture();
    let root = fixture.directory.path();
    let native = json!({"id": "native"}).as_object().unwrap().clone();
    assert!(adapt_extracted_inputs(&fixture.extracted, &native, root, &root.join("unused"), &cancel()).unwrap().is_none());
    assert!(!root.join("unused").exists());
    let output = root.join("normalized");
    let adaptation = adapt_extracted_inputs(&fixture.extracted, &fixture.recipe, root, &output, &cancel()).unwrap().unwrap();
    assert_eq!(std::fs::read(&adaptation.inputs.code.path).unwrap(), b"89abcdef01234567");
    assert_eq!(std::fs::read(&fixture.extracted.code.path).unwrap(), b"0123456789abcdef");
    assert_eq!(adaptation.receipt["source_inputs"], fixture.recipe["input_adapter"]["source_inputs"]);
    assert_eq!(adaptation.receipt["execution_inputs"], fixture.recipe["inputs"]);
    assert_eq!(adaptation.receipt["objects_compiled"], 0);
    assert_eq!(adaptation.receipt["program_id"], "000000000000007B");
    let receipt = std::fs::read(output.join(adapters::RECEIPT)).unwrap();
    assert_eq!(receipt, forge_base::legacy_json::pretty_file_bytes(&adaptation.receipt));
    assert!(adaptation.to_value()["wall_seconds"].is_f64());
    // The existing output is never reused or replaced.
    let error = adapt_extracted_inputs(&fixture.extracted, &fixture.recipe, root, &output, &cancel()).unwrap_err();
    assert!(error.message.starts_with("Cannot create"), "{}", error.message);
    assert!(output.join(adapters::RECEIPT).is_file());
}

#[test]
fn adapter_rejections_leave_no_output() {
    let fixture = adapter_fixture();
    let root = fixture.directory.path();
    let output = root.join("normalized");
    std::fs::write(&fixture.extracted.code.path, [0u8; 16]).unwrap();
    let error = adapt_extracted_inputs(&fixture.extracted, &fixture.recipe, root, &output, &cancel()).unwrap_err();
    assert_eq!(error.message, "Adapter source identity mismatch");
    assert!(!output.exists());
    std::fs::write(&fixture.extracted.code.path, b"0123456789abcdef").unwrap();
    let mut recipe = Value::Object(fixture.recipe.clone());
    recipe["inputs"]["romfs"]["sha256"] = json!("0".repeat(64));
    let error = adapt_extracted_inputs(&fixture.extracted, recipe.as_object().unwrap(), root, &output, &cancel()).unwrap_err();
    assert_eq!(error.message, "Normalized input identity mismatch; title was not activated");
    assert!(!output.exists());
    let inside = root.join("original");
    let error = adapt_extracted_inputs(&fixture.extracted, &fixture.recipe, root, &inside, &cancel()).unwrap_err();
    assert_eq!(error.message, "Adapter output must not contain the source inputs");
    let mut recipe = Value::Object(fixture.recipe.clone());
    recipe["inputs"]["code"]["sha256"] = json!("0".repeat(64));
    let error = validate_adapter(root, recipe.as_object().unwrap(), &cancel()).unwrap_err();
    assert_eq!(error.message, "Code COPY artifact does not match revision contracts");
    std::fs::write(root.join("recipes/adapters/copies.json"), b"{}").unwrap();
    let error = validate_adapter(root, &fixture.recipe, &cancel()).unwrap_err();
    assert_eq!(error.message, "Precompiled artifact failed integrity validation: recipes/adapters/copies.json");
}

struct CatalogFixture {
    directory: tempfile::TempDir,
    recipe: Map<String, Value>,
    catalog: Value,
}

/// test_precompiled_titles.PrecompiledTitleTests.setUp for `platform`.
fn catalog_fixture(platform: &ReleasePlatform) -> CatalogFixture {
    let directory = tempfile::tempdir().unwrap();
    let artifact = |name: &str| {
        let path = forge_catalog::paths::join_posix(directory.path(), name);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(&path, name.as_bytes()).unwrap();
        json!({"path": name, "bytes": name.len(), "sha256": sha256_hex(name.as_bytes())})
    };
    let inputs: Map<String, Value> = ["code", "exheader", "romfs"]
        .iter()
        .enumerate()
        .map(|(i, kind)| (kind.to_string(), json!({"sha256": (i + 1).to_string().repeat(64), "bytes": i + 1})))
        .collect();
    let recipe = json!({"id": "test", "inputs": inputs}).as_object().unwrap().clone();
    let catalog = json!({"format": precompiled::FORMAT, "install_model": precompiled::MODEL, "target": platform.target,
        "runtime": artifact(platform.runtime), "native_module": artifact(platform.native_module),
        "titles": [{"recipe": "test", "inputs": inputs, "abi_version": 2, "target": platform.target,
                    "translator_identity_sha256": "a".repeat(64), "plugin": artifact("titles/test/contract.dll")}]});
    let fixture = CatalogFixture { directory, recipe, catalog };
    fixture.save();
    fixture
}

impl CatalogFixture {
    fn root(&self) -> &Path {
        self.directory.path()
    }

    fn save(&self) {
        forge_base::fsutil::atomic_write_json(&self.root().join(precompiled::CATALOG), &self.catalog).unwrap();
    }
}

#[test]
fn selection_binds_the_explicit_execution_target() {
    let fixture = catalog_fixture(&LINUX);
    let item = select_title(fixture.root(), &fixture.recipe, None, &LINUX, &cancel()).unwrap();
    assert_eq!(item["recipe"], "test");
    let error = select_title(fixture.root(), &fixture.recipe, None, &WINDOWS, &cancel()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::IncompatibleTarget);
    assert_eq!(error.message, "This package targets x86_64-unknown-linux-gnu, not this host");
    validate_title(fixture.root(), &fixture.recipe, None, &cancel()).unwrap();
}

#[test]
fn corruption_revision_and_family_rejected() {
    let mut fixture = catalog_fixture(&WINDOWS);
    for pointer in ["/runtime/path", "/native_module/path", "/titles/0/plugin/path"] {
        let name = fixture.catalog.pointer(pointer).unwrap().as_str().unwrap().to_string();
        let path = forge_catalog::paths::join_posix(fixture.root(), &name);
        let original = std::fs::read(&path).unwrap();
        std::fs::write(&path, [original.clone(), b"changed".to_vec()].concat()).unwrap();
        let error = select_title(fixture.root(), &fixture.recipe, None, &WINDOWS, &cancel()).unwrap_err();
        assert_eq!(error.message, format!("Precompiled artifact failed integrity validation: {name}"));
        std::fs::write(&path, original).unwrap();
    }
    let mut recipe = fixture.recipe.clone();
    recipe["inputs"]["code"]["sha256"] = json!("b".repeat(64));
    let error = select_title(fixture.root(), &recipe, None, &WINDOWS, &cancel()).unwrap_err();
    assert_eq!(error.message, "Precompiled title revision/ABI does not match the ROM recipe");
    let mut recipe = fixture.recipe.clone();
    recipe.insert("data_compatibility".into(), json!({"format": "unbound"}));
    let error = select_title(fixture.root(), &recipe, None, &WINDOWS, &cancel()).unwrap_err();
    assert_eq!(error.message, "Precompiled title content family differs from the recipe");
    let mut recipe = fixture.recipe.clone();
    recipe.insert("id".into(), json!("other"));
    let error = select_title(fixture.root(), &recipe, None, &WINDOWS, &cancel()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedRevision);

    let titles = fixture.catalog["titles"].as_array().unwrap().clone();
    fixture.catalog["titles"] = json!([titles[0], titles[0]]);
    fixture.save();
    assert_eq!(load_catalog(fixture.root()).unwrap_err().message, "Invalid or duplicate precompiled-title revisions");
    fixture.catalog["titles"] = json!([]);
    fixture.save();
    let error = load_catalog(fixture.root()).unwrap_err();
    assert_eq!(error.message, "This release contains no precompiled titles; obtain a complete package");
    std::fs::remove_file(fixture.root().join(precompiled::CATALOG)).unwrap();
    assert!(load_catalog(fixture.root()).is_err());
}

#[test]
fn checked_file_containment() {
    let fixture = catalog_fixture(&WINDOWS);
    let root = fixture.root();
    for path in ["../outside.dll", "/abs.dll", "C:x.dll", ""] {
        let error = checked_file(root, &json!({"path": path}), &cancel()).unwrap_err();
        assert!(error.message.starts_with("unsafe relative path: "), "{path}: {}", error.message);
    }
    let error = checked_file(root, &json!({"path": "titles/missing.dll"}), &cancel()).unwrap_err();
    assert_eq!(error.message, "Precompiled artifact is missing: titles/missing.dll");
    let record = json!({"path": "titles//test/./contract.dll", "bytes": 24, "sha256": sha256_hex(b"titles/test/contract.dll")});
    let path = checked_file(root, &record, &cancel()).unwrap();
    assert!(path.ends_with("titles/test/contract.dll"));
    let mut float_size = record.clone();
    float_size["bytes"] = json!(24.0);
    checked_file(root, &float_size, &cancel()).unwrap();
    let mut upper = record;
    upper["sha256"] = json!(sha256_hex(b"titles/test/contract.dll").to_uppercase());
    upper["bytes"] = json!(24);
    assert!(checked_file(root, &upper, &cancel()).unwrap_err().message.contains("integrity"));
}

#[test]
fn verified_variants_reuse_the_base_title() {
    let release = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../triaevum_release");
    let definitions = Value::Object(forge_base::fsutil::load_json_object(&release.join("supported_revisions.json")).unwrap());
    let base = definitions["recipes"][0].clone();
    let title = json!({"recipe": base["id"], "inputs": base["inputs"], "plugin": {"path": "titles/existing/x.dll"},
                       "abi_version": 2, "translator_identity_sha256": "a".repeat(64)});
    let (catalog, recipes) =
        variants::add_verified_variants(&json!({"titles": [title]}), &json!({"recipes": [base]}), &definitions).unwrap();
    assert_eq!(catalog["titles"].as_array().unwrap().len(), 2);
    assert_eq!(recipes["recipes"].as_array().unwrap().len(), 2);
    assert_eq!(catalog["titles"][1]["plugin"], title["plugin"]);
    assert_eq!(variants::add_verified_variants(&catalog, &recipes, &definitions).unwrap(), (catalog.clone(), recipes.clone()));
    let error = variants::add_verified_variants(&json!({"titles": []}), &json!({"recipes": [base]}), &definitions).unwrap_err();
    assert_eq!(error.message, "Verified variant requires its packaged base recipe and title");
    let mut conflict = catalog.clone();
    conflict["titles"][1]["plugin"]["path"] = json!("wrong.dll");
    let error = variants::add_verified_variants(&conflict, &recipes, &definitions).unwrap_err();
    assert_eq!(error.message, "Existing variant binding conflicts with its definition");

    let root = release.join("input_coverage");
    let (bound_catalog, bound_recipes, files) = coverage::bind(&catalog, &recipes, &root, &cancel()).unwrap();
    coverage::require_coverage(&bound_catalog, &bound_recipes, &root).unwrap();
    assert_eq!(bound_recipes["recipes"].as_array().unwrap().len(), 3);
    assert_eq!(files.len(), 1);
    assert_eq!(files[0]["role"], "input_copy_adapter");
    let error = coverage::require_coverage(&catalog, &recipes, &root).unwrap_err();
    assert!(error.message.starts_with("Release lost qualified input coverage: "), "{}", error.message);
    let mut lost = bound_catalog.clone();
    let last = lost["titles"].as_array().unwrap().len() - 1;
    lost["titles"][last].as_object_mut().unwrap().shift_remove("data_compatibility");
    let error = coverage::require_coverage(&lost, &bound_recipes, &root).unwrap_err();
    assert!(error.message.starts_with("Release changed qualified input coverage: "), "{}", error.message);
}
