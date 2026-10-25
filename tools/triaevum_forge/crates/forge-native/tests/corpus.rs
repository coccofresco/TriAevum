// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Port of test_shader_corpus_layout.py, plus the release-audit check of a
//! bundled corpus against the shipped corresponding-source archive.

mod common;

use std::io::Write;
use std::path::{Path, PathBuf};

use common::encode_pack;
use forge_base::fsutil::atomic_write_json;
use forge_base::hashing::sha256_hex;
use forge_catalog::{checked_file, LINUX, WINDOWS};
use forge_native::corpus::{
    bind_shader_corpus, inputs_digest, read_portable_corpus, validate_bundled_corpus, INPUTS_FORMAT,
    QUALIFICATION_FORMAT,
};
use forge_native::{prepare_shader_seed, DesktopServices};
use serde_json::{json, Map, Value};

/// A recording runtime input inside the decision-policy set.
const RUNTIME_INPUT: &str = "runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp";

fn sha(path: &Path) -> String {
    sha256_hex(&std::fs::read(path).unwrap())
}

fn inputs(sources: &Path, names: &[&str]) -> Value {
    let mut files = Map::new();
    for name in names {
        files.insert(name.to_string(), json!(sha(&sources.join(name))));
    }
    json!({"format": INPUTS_FORMAT, "targets": ["fixture"], "sha256": inputs_digest(&files), "files": files})
}

/// `write_qualification`: the record `qualification_record` writes.
fn write_qualification(path: &Path, pack: &Path, manifests: &[PathBuf], sources: &Path) {
    let generator = inputs(sources, &["generator.cpp"]);
    let mut policy = inputs(sources, &[RUNTIME_INPUT]);
    policy["selection"] = json!("decision_policy_v2");
    policy["recorded_files"] = json!(1);
    let mut manifest_sha256: Vec<String> = manifests.iter().map(|path| sha(path)).collect();
    manifest_sha256.sort();
    atomic_write_json(
        path,
        &json!({"format": QUALIFICATION_FORMAT, "generator_inputs": generator, "policy_inputs": [policy],
            "pack_sha256": sha(pack), "pipeline_manifest_sha256": manifest_sha256, "modules": 1, "variant_requests": 1}),
    )
    .unwrap();
}

struct Fixture {
    _temp: tempfile::TempDir,
    root: PathBuf,
    pack: PathBuf,
    manifest: PathBuf,
    helper: PathBuf,
    sources: PathBuf,
    qualification: PathBuf,
}

fn pipeline_manifest(schema: u32) -> Value {
    json!({"format": "oot3d_pica_pipeline_manifest_v2", "schema_version": 2,
        "descriptor_schema_version": schema, "pipeline_count": 1, "pipelines": [{}]})
}

impl Fixture {
    fn new() -> Self {
        let temp = tempfile::tempdir().unwrap();
        let root = forge_prepare::paths::resolve(temp.path()).unwrap();
        let pack = root.join("input.o3ps");
        std::fs::write(&pack, encode_pack(3, &[((2, 1, 2, 4), vec![3, 2, 0x23, 7])])).unwrap();
        let manifest = root.join("input.json");
        atomic_write_json(&manifest, &pipeline_manifest(3)).unwrap();
        let helper = root.join("helper");
        std::fs::write(&helper, b"test helper").unwrap();
        let sources = root.join("checkout");
        std::fs::create_dir_all(sources.join(RUNTIME_INPUT).parent().unwrap()).unwrap();
        std::fs::write(sources.join("generator.cpp"), "generator").unwrap();
        std::fs::write(sources.join(RUNTIME_INPUT), "runtime").unwrap();
        let qualification = root.join("qualification.json");
        write_qualification(&qualification, &pack, std::slice::from_ref(&manifest), &sources);
        Self { _temp: temp, root, pack, manifest, helper, sources, qualification }
    }

    fn bind(&self, catalog: &Value, pack: &Path) -> forge_base::Result<(Map<String, Value>, Vec<Value>)> {
        bind_shader_corpus(
            catalog.as_object().unwrap(),
            pack,
            std::slice::from_ref(&self.manifest),
            &self.helper,
            &[],
            &self.qualification,
            &self.sources,
        )
    }
}

#[test]
fn every_recipe_both_platforms_cold_warm_and_relocation() {
    for platform in [WINDOWS, LINUX] {
        let fixture = Fixture::new();
        let titles: Vec<Value> = ["eur", "eur-catalogue", "usa"]
            .iter()
            .map(|name| json!({"recipe": name, "renderer_shader_preparation": {"compiler": {"fixture": true}}}))
            .collect();
        let original = json!({"target": platform.target, "titles": titles});
        let (catalog, items) = fixture.bind(&original, &fixture.pack).unwrap();
        let package = fixture.root.join("relocated Forge");
        for item in &items {
            let destination = package.join(item["path"].as_str().unwrap());
            std::fs::create_dir_all(destination.parent().unwrap()).unwrap();
            std::fs::copy(item["source"].as_str().unwrap(), destination).unwrap();
        }
        let suffix = if platform == WINDOWS { ".exe" } else { "" };
        assert!(items.iter().any(|item| item["path"] == format!("forge/oot3d_native_nri_pipeline_prepare{suffix}")));
        let cancel = forge_base::CancelToken::new();
        for title in catalog["titles"].as_array().unwrap() {
            let seed = &title["shader_preparation"];
            checked_file(&package, &seed["qualification"], &cancel).unwrap();
            assert_eq!(seed["compiler"], title["renderer_shader_preparation"]["compiler"]);
            checked_file(&package, &title["device_pipeline_preparation"]["helper"], &cancel).unwrap();
            assert_eq!(title["device_pipeline_preparation"]["startup_programs"], true);
            let mut events = Vec::new();
            let data = package.join("data");
            let title = title.as_object().unwrap();
            let cold = prepare_shader_seed(&DesktopServices, &package, &data, title, &mut |stage, _| {
                events.push(stage.to_string())
            })
            .unwrap()
            .unwrap();
            let warm = prepare_shader_seed(&DesktopServices, &package, &data, title, &mut |_, _| {}).unwrap().unwrap();
            assert_eq!(cold, warm);
            assert_eq!(std::fs::read(&cold).unwrap(), std::fs::read(&fixture.pack).unwrap());
            assert!(!events.is_empty() && events.iter().all(|stage| stage == "shaders"));
        }
        let first = catalog["titles"][0].as_object().unwrap();
        std::fs::write(package.join(first["shader_preparation"]["pack"]["path"].as_str().unwrap()), b"bad").unwrap();
        assert!(prepare_shader_seed(&DesktopServices, &package, &package.join("data"), first, &mut |_, _| {}).is_err());
    }
}

#[test]
fn driver_cache_and_mismatching_schema_rejected() {
    let fixture = Fixture::new();
    let catalog = json!({"target": WINDOWS.target, "titles": [{"recipe": "eur", "renderer_shader_preparation": {}}]});
    atomic_write_json(&fixture.manifest, &pipeline_manifest(4)).unwrap();
    assert_eq!(fixture.bind(&catalog, &fixture.pack).unwrap_err().message, "Incompatible observed pipeline manifest");
    std::fs::write(&fixture.pack, b"not a portable pack; GPU cache fixture").unwrap();
    assert!(fixture.bind(&catalog, &fixture.pack).is_err());
}

#[test]
fn unqualified_or_stale_corpus_rejected() {
    let fixture = Fixture::new();
    let catalog = json!({"target": WINDOWS.target, "titles": [{"recipe": "eur", "renderer_shader_preparation": {}}]});
    fixture.bind(&catalog, &fixture.pack).unwrap();
    std::fs::write(fixture.sources.join("generator.cpp"), "generator v2").unwrap();
    let error = fixture.bind(&catalog, &fixture.pack).unwrap_err().message;
    assert_eq!(
        error,
        "Stale shader corpus: 1 generator inputs changed since regeneration (first: generator.cpp); regenerate it"
    );
    std::fs::write(fixture.sources.join("generator.cpp"), "generator").unwrap();
    std::fs::write(fixture.sources.join(RUNTIME_INPUT), "runtime v2").unwrap();
    assert!(fixture
        .bind(&catalog, &fixture.pack)
        .unwrap_err()
        .message
        .starts_with("Stale variant recordings: 1 runtime inputs"));
    std::fs::write(fixture.sources.join(RUNTIME_INPUT), "runtime").unwrap();
    let other = fixture.root.join("other.o3ps");
    std::fs::write(&other, encode_pack(3, &[((2, 1, 2, 5), vec![3, 2, 0x23, 7])])).unwrap();
    assert_eq!(
        fixture.bind(&catalog, &other).unwrap_err().message,
        "Shader corpus differs from its qualification record"
    );
    let missing_renderer = json!({"target": WINDOWS.target, "titles": [{"recipe": "eur"}]});
    assert_eq!(
        fixture.bind(&missing_renderer, &fixture.pack).unwrap_err().message,
        "Bind the platform renderer compiler before the corpus"
    );
}

#[test]
fn non_canonical_corpus_is_refused() {
    let fixture = Fixture::new();
    let mut data = std::fs::read(&fixture.pack).unwrap();
    data.extend_from_slice(b"tail");
    std::fs::write(&fixture.pack, &data).unwrap();
    assert_eq!(
        read_portable_corpus(&fixture.pack).unwrap_err().message,
        "Public corpus must be canonical, without unclaimed data"
    );
    let mut corrupt = encode_pack(3, &[((2, 1, 2, 4), vec![3, 2, 0x23, 7, 1, 1, 1, 1])]);
    let last = corrupt.len() - 1;
    corrupt[last] ^= 1;
    std::fs::write(&fixture.pack, &corrupt).unwrap();
    assert_eq!(read_portable_corpus(&fixture.pack).unwrap_err().message, "Corrupt SPIR-V payload");
}

/// A package with the bound catalog, declared roles and the corresponding
/// source archive holding the qualified checkout files.
fn bundled_package(fixture: &Fixture) -> (PathBuf, Map<String, Value>, Map<String, Value>) {
    let catalog = json!({"target": WINDOWS.target, "titles": [{"recipe": "eur", "renderer_shader_preparation": {}}]});
    let (catalog, items) = fixture.bind(&catalog, &fixture.pack).unwrap();
    let package = fixture.root.join("package");
    let mut declared = Map::new();
    for item in &items {
        let path = item["path"].as_str().unwrap();
        let destination = package.join(path);
        std::fs::create_dir_all(destination.parent().unwrap()).unwrap();
        std::fs::copy(item["source"].as_str().unwrap(), destination).unwrap();
        declared.insert(path.to_string(), json!({"role": item["role"]}));
    }
    let archive = package.join("source/TriAevum-source.zip");
    std::fs::create_dir_all(archive.parent().unwrap()).unwrap();
    let mut writer = zip::ZipWriter::new(std::fs::File::create(&archive).unwrap());
    for name in ["generator.cpp", RUNTIME_INPUT] {
        writer.start_file(name, zip::write::SimpleFileOptions::default()).unwrap();
        writer.write_all(&std::fs::read(fixture.sources.join(name)).unwrap()).unwrap();
    }
    writer.finish().unwrap();
    declared.insert("source/TriAevum-source.zip".into(), json!({"role": "corresponding_source"}));
    (package, catalog, declared)
}

#[test]
fn bundled_corpus_is_checked_against_the_source_archive() {
    let fixture = Fixture::new();
    let (package, catalog, mut declared) = bundled_package(&fixture);
    validate_bundled_corpus(&package, &catalog, &declared).unwrap();
    declared.insert("forge/shader-corpus/extra.json".into(), json!({"role": "portable_pipeline_recipes"}));
    assert_eq!(
        validate_bundled_corpus(&package, &catalog, &declared).unwrap_err().message,
        "Uncatalogued portable shader artifact"
    );
    declared.remove("forge/shader-corpus/extra.json");
    declared.insert("forge/shader-corpus/portable.o3ps".into(), json!({"role": "data"}));
    assert_eq!(
        validate_bundled_corpus(&package, &catalog, &declared).unwrap_err().message,
        "Portable corpus lacks its explicit release role"
    );
    declared.insert("forge/shader-corpus/portable.o3ps".into(), json!({"role": "portable_shader_corpus"}));
    declared.remove("source/TriAevum-source.zip");
    assert_eq!(
        validate_bundled_corpus(&package, &catalog, &declared).unwrap_err().message,
        "Shader corpus qualification requires the corresponding source archive"
    );
    assert!(validate_bundled_corpus(&package, &catalog, &Map::new()).is_ok());
}
