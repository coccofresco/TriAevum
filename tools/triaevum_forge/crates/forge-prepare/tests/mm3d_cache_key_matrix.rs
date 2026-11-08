// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! F05 cache-key matrix for the local MM3D recipe
//! (titles/mm3d/forge/supported_revisions.json), driven through the real
//! verify_sources / content_key / prepare_content path: reuse, wrong-title
//! rejection, stale / missing / corrupt output, cancellation and atomic
//! publication. Input bytes are small stand-ins (the recipe's own id, game
//! and process contract are kept); recognition of the real extracted MM
//! inputs is covered by forge-catalog/tests/title_recipes.rs.

use std::path::{Path, PathBuf};

use forge_base::hashing::sha256_hex;
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_prepare::content::{
    content_key, prepare_content, verify_sources, PrepareResult, SourcePaths, VerifiedInputs,
};
use forge_prepare::HashCache;
use serde_json::{json, Map, Value};

const MM: &str = "titles/mm3d/forge/supported_revisions.json";
const OOT: &str = "tools/triaevum_release/supported_revisions.json";
const MM_ID: &str = "mm3d-eur-local-6eec27b1";

fn repo_file(relative: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../..").join(relative)
}

fn first_recipe(relative: &str) -> Map<String, Value> {
    let text = std::fs::read_to_string(repo_file(relative)).unwrap();
    let value: Value = serde_json::from_str(&text).unwrap();
    value["recipes"][0].as_object().unwrap().clone()
}

struct Fixture {
    _dir: tempfile::TempDir,
    code: PathBuf,
    exheader: PathBuf,
    romfs: PathBuf,
    output: PathBuf,
    recipe: Map<String, Value>,
}

fn fixture() -> Fixture {
    let dir = tempfile::tempdir().unwrap();
    let root = forge_prepare::paths::resolve(dir.path()).unwrap();
    let mut recipe = first_recipe(MM);
    let mut inputs = Map::new();
    let mut paths = Vec::new();
    for (kind, bytes) in [("code", &b"mm-code"[..]), ("exheader", b"mm-exheader"), ("romfs", b"mm-romfs")] {
        let path = root.join(format!("{kind}.bin"));
        std::fs::write(&path, bytes).unwrap();
        inputs.insert(kind.into(), json!({"bytes": bytes.len(), "sha256": sha256_hex(bytes)}));
        paths.push(path);
    }
    recipe.insert("inputs".into(), Value::Object(inputs));
    let [code, exheader, romfs]: [PathBuf; 3] = paths.try_into().unwrap();
    Fixture { output: root.join("titles"), _dir: dir, code, exheader, romfs, recipe }
}

impl Fixture {
    fn verify_with(&self, recipe: &Map<String, Value>) -> Result<VerifiedInputs> {
        let mut cache = HashCache::new(self.output.join(".hash-cache.json"), true);
        let paths = SourcePaths { code: &self.code, exheader: &self.exheader, romfs: &self.romfs };
        verify_sources(recipe, &paths, &mut cache, None, None, &CancelToken::new()).map(|v| v.0)
    }

    fn prepare_with(&self, cancel: &CancelToken, entry: u64) -> Result<PrepareResult> {
        let verified = self.verify_with(&self.recipe)?;
        let builder = move |exh: &Path, code: &Path, romfs: &Path, c: &CancelToken| -> Result<Value> {
            if c.is_cancelled() {
                return Err(ForgeError::cancelled());
            }
            Ok(json!({
                "format": "oot3d_native_process_manifest_v1",
                "source": {
                    "exheader_path": exh.to_string_lossy(),
                    "exheader_sha256": sha256_hex(&std::fs::read(exh).unwrap()),
                    "code_bin_path": code.to_string_lossy(),
                    "code_bin_sha256": sha256_hex(&std::fs::read(code).unwrap()),
                    "romfs_image_path": romfs.to_string_lossy(),
                },
                "process": {
                    "entrypoint": entry,
                    "linear_heap": {"base_address": 0x1400_0000u64, "size": 0x0400_0000u64},
                    "system_regions": [{"name": "ctr_vram", "address": 0x1F00_0000u64, "mapped_size": 0x0060_0000u64}],
                },
            }))
        };
        prepare_content(&self.recipe, &verified, &self.output, None, Some(&builder), cancel)
    }

    fn entry(&self) -> u64 {
        self.recipe["process"]["entrypoint"].as_u64().unwrap()
    }

    fn prepare(&self) -> Result<PrepareResult> {
        self.prepare_with(&CancelToken::new(), self.entry())
    }

    fn title_is_empty(&self) -> bool {
        let title = self.output.join(MM_ID);
        !title.exists() || title.read_dir().unwrap().next().is_none()
    }
}

#[test]
fn mm_recipe_is_title_bound() {
    let f = fixture();
    assert_eq!(f.recipe["id"], MM_ID);
    assert_eq!(f.recipe["game"], "mm3d");
    assert_eq!(f.entry(), 0x0010_0000);
}

#[test]
fn reuse_yields_same_folder() {
    let f = fixture();
    let first = f.prepare().unwrap();
    assert_eq!(first.status, "prepared");
    assert!(first.directory.starts_with(f.output.join(MM_ID)));
    let again = f.prepare().unwrap();
    assert_eq!((again.status, again.directory), ("reused", first.directory));
}

#[test]
fn wrong_title_is_rejected_and_keys_never_collide() {
    let f = fixture();
    let verified = f.verify_with(&f.recipe).unwrap();
    let oot = first_recipe(OOT);
    let err = f.verify_with(&oot).unwrap_err();
    assert_eq!(err.kind, ErrorKind::UnsupportedRevision, "{}", err.message);
    let oot_id = oot["id"].as_str().unwrap();
    assert_ne!(oot_id, MM_ID);
    assert_ne!(content_key(MM_ID, &verified), content_key(oot_id, &verified));
    let err = f.prepare_with(&CancelToken::new(), 0x0020_0000).unwrap_err();
    assert!(err.message.contains("entrypoint does not match recipe"), "{}", err.message);
    assert!(f.title_is_empty());
}

#[test]
fn stale_input_is_rejected_by_contract() {
    let f = fixture();
    let before = f.prepare().unwrap();
    std::fs::write(&f.code, b"mm-codX").unwrap();
    let err = f.verify_with(&f.recipe).unwrap_err();
    assert_eq!(err.kind, ErrorKind::UnsupportedRevision, "{}", err.message);
    assert!(before.directory.is_dir());
}

#[test]
fn missing_output_member_is_reported_incomplete() {
    let f = fixture();
    let r = f.prepare().unwrap();
    std::fs::remove_file(r.directory.join("forge-state.json")).unwrap();
    let err = f.prepare().unwrap_err();
    assert!(err.message.contains("incomplete"), "{}", err.message);
}

#[test]
fn corrupt_or_tampered_output_is_refused() {
    let f = fixture();
    let r = f.prepare().unwrap();
    let index = r.directory.join("content.tap");
    let mut value: Value = serde_json::from_str(&std::fs::read_to_string(&index).unwrap()).unwrap();
    value["content_key"] = json!("0".repeat(64));
    std::fs::write(&index, serde_json::to_vec(&value).unwrap()).unwrap();
    let err = f.prepare().unwrap_err();
    assert!(err.message.contains("does not match inputs"), "{}", err.message);
    std::fs::write(&index, b"{not json").unwrap();
    let err = f.prepare().unwrap_err();
    assert!(err.message.contains("incomplete"), "{}", err.message);
}

#[test]
fn cancellation_publishes_nothing() {
    let f = fixture();
    let cancel = CancelToken::new();
    cancel.cancel();
    let err = f.prepare_with(&cancel, f.entry()).unwrap_err();
    assert_eq!(err.kind, ErrorKind::Cancelled);
    assert!(f.title_is_empty());
    assert_eq!(f.prepare().unwrap().status, "prepared");
}

#[test]
fn publication_is_complete() {
    let f = fixture();
    let r = f.prepare().unwrap();
    let mut names: Vec<String> =
        std::fs::read_dir(&r.directory).unwrap().map(|e| e.unwrap().file_name().into_string().unwrap()).collect();
    names.sort();
    assert_eq!(names, ["content.tap", "forge-state.json", "process-manifest.json"]);
}
