// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Per-title Forge (owner decision 2026-11-08): the build's title decides the
//! supported revisions it loads; the other title's dump is rejected with a
//! diagnostic that names that title. Run once per build:
//!   cargo test -p forge-catalog --test per_title_forge                 (OoT3D)
//!   cargo test -p forge-catalog --test per_title_forge --features title-mm3d
//! With TRIAEVUM_OOT3D_EXTRACT / TRIAEVUM_MM3D_EXTRACT (code.bin, exheader.bin)
//! the real dumps are used; without them the recipe-level checks still run.

use std::path::{Path, PathBuf};

use forge_base::ErrorKind;
use forge_catalog::recipes;
use forge_catalog::title::{self, ACTIVE, MM3D, OOT3D};

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).ancestors().nth(4).unwrap().to_path_buf()
}

fn active_recipes() -> PathBuf {
    // No package beside the test: the active title's source-tree recipes.
    let empty = tempfile::tempdir().unwrap();
    recipes::default_recipes(empty.path(), &repo())
}

#[test]
fn the_build_loads_only_its_own_title() {
    let path = active_recipes();
    assert!(path.ends_with(ACTIVE.embedded_recipes), "{path:?}");
    let list = recipes::load_recipes(&path).unwrap();
    assert!(!list.is_empty());
    for recipe in &list {
        let id = recipe["id"].as_str().unwrap();
        assert!(id.starts_with(&format!("{}-", ACTIVE.id)), "{} Forge carries foreign recipe {id}", ACTIVE.id);
        if let Some(game) = recipe.get("game") {
            assert_eq!(game.as_str(), Some(ACTIVE.id));
        }
        // The MM3D recipe's ONLY inputs are the dump (no dekui/Restoration).
        let inputs: Vec<_> = recipe["inputs"].as_object().unwrap().keys().cloned().collect();
        assert_eq!(inputs, ["code", "exheader", "romfs"], "{id}: inputs beyond the dump");
    }
    let text = std::fs::read_to_string(&path).unwrap().to_ascii_lowercase();
    assert!(!text.contains("dekui") && !text.contains("restoration"), "mod input notion in {path:?}");
}

#[test]
fn a_package_recipe_file_is_title_named() {
    let package = tempfile::tempdir().unwrap();
    let packaged = package.path().join(ACTIVE.packaged_recipes);
    std::fs::create_dir_all(packaged.parent().unwrap()).unwrap();
    std::fs::write(&packaged, "{}").unwrap();
    assert_eq!(recipes::default_recipes(package.path(), &repo()), packaged);
    // Negative: the other title's packaged file is never picked up.
    let other = if ACTIVE == OOT3D { MM3D } else { OOT3D };
    let foreign = tempfile::tempdir().unwrap();
    let foreign_file = foreign.path().join(other.packaged_recipes);
    std::fs::create_dir_all(foreign_file.parent().unwrap()).unwrap();
    std::fs::write(&foreign_file, "{}").unwrap();
    assert_ne!(recipes::default_recipes(foreign.path(), &repo()), foreign_file);
}

fn extract(game: &str) -> Option<(Vec<u8>, Vec<u8>)> {
    let dir = PathBuf::from(std::env::var(format!("TRIAEVUM_{}_EXTRACT", game.to_uppercase())).ok()?);
    Some((std::fs::read(dir.join("code.bin")).unwrap(), std::fs::read(dir.join("exheader.bin")).unwrap()))
}

#[test]
fn the_other_titles_dump_is_rejected_by_name() {
    let path = active_recipes();
    let mut checked = 0;
    for game in [OOT3D, MM3D] {
        let Some((code, exheader)) = extract(game.id) else { continue };
        let result = recipes::recognize_recipes(0, &code, &exheader, &path);
        if game == ACTIVE {
            let matched = result.unwrap();
            assert_eq!(matched.len(), 1, "own title must be recognized uniquely");
        } else {
            let error = result.unwrap_err();
            assert_eq!(error.kind, ErrorKind::UnsupportedRevision);
            let message = error.to_string();
            assert!(message.contains(game.display_name) && message.contains(ACTIVE.display_name), "{message}");
            eprintln!("{} Forge rejects {}: {message}", ACTIVE.id, game.id);
        }
        // Negative: a tampered executable of any title names no title.
        let mut tampered = code.clone();
        tampered[0x1000] ^= 1;
        let error = recipes::recognize_recipes(0, &tampered, &exheader, &path).unwrap_err();
        assert!(!error.to_string().contains("this is a dump of"), "tampered dump was named");
        checked += 1;
    }
    eprintln!("per-title dumps checked: {checked}");
    assert!(title::foreign_title(&ACTIVE, ACTIVE.code_sha256[0]).is_none());
}

/// validate_title bound to the MM3D title-module ARTIFACT CONTRACT
/// (W/analysis/parity/CATALOGS/RESULTS.md). FIXTURE: the MM3D module is not
/// fixture (stand-in bytes; see mm3d_real_title_catalog_* for the built module); the binding
/// (recipe id, dump-only inputs, abi_version 2, target, translator identity,
/// sizes/digests) is the real validate_title path.
#[test]
fn mm3d_title_module_contract_binds_through_validate_title() {
    use forge_base::hashing::sha256_hex;
    use forge_catalog::precompiled;
    use serde_json::{json, Value};

    let mm_path = repo().join(MM3D.embedded_recipes);
    let recipe = recipes::load_recipe(&mm_path, "mm3d-eur-local-6eec27b1").unwrap();
    let oot = recipes::load_recipes(&repo().join(OOT3D.embedded_recipes)).unwrap();
    let oot_recipe = oot[0].as_object().unwrap().clone();
    let platform = forge_catalog::WINDOWS;
    let directory = tempfile::tempdir().unwrap();
    let artifact = |name: &str, bytes: &[u8]| {
        let path = forge_catalog::paths::join_posix(directory.path(), name);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(&path, bytes).unwrap();
        json!({"path": name, "bytes": bytes.len(), "sha256": sha256_hex(bytes)})
    };
    let plugin_name = format!("titles/mm3d-eur-local-6eec27b1/{}", platform.title_module);
    let item = |abi: u64, identity: &str| {
        json!({"recipe": "mm3d-eur-local-6eec27b1", "inputs": recipe["inputs"], "abi_version": abi,
               "target": platform.target, "translator_identity_sha256": identity,
               "plugin": artifact(&plugin_name, b"mm3d title module fixture")})
    };
    let write = |title: Value| {
        let catalog = json!({"format": precompiled::FORMAT, "install_model": precompiled::MODEL,
            "target": platform.target, "runtime": artifact(platform.runtime, b"runtime fixture"),
            "native_module": artifact(platform.native_module, b"native module fixture"), "titles": [title]});
        forge_base::fsutil::atomic_write_json(&directory.path().join(precompiled::CATALOG), &catalog).unwrap();
    };
    let cancel = forge_base::CancelToken::new();
    write(item(2, &"c".repeat(64)));
    let bound = forge_catalog::validate_title(directory.path(), &recipe, None, &cancel).unwrap();
    assert_eq!(bound["recipe"], "mm3d-eur-local-6eec27b1");
    // Negatives (recorded): the OoT3D recipe finds no module in the MM catalog;
    // ABI 1, a missing translator identity and a tampered module are refused.
    let error = forge_catalog::validate_title(directory.path(), &oot_recipe, None, &cancel).unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedRevision);
    write(item(1, &"c".repeat(64)));
    assert!(forge_catalog::validate_title(directory.path(), &recipe, None, &cancel).is_err(), "ABI 1 accepted");
    write(item(2, ""));
    assert!(forge_catalog::validate_title(directory.path(), &recipe, None, &cancel).is_err(), "no translator identity");
    write(item(2, &"c".repeat(64)));
    std::fs::write(forge_catalog::paths::join_posix(directory.path(), &plugin_name), b"tampered module bytes!!!!").unwrap();
    assert!(forge_catalog::validate_title(directory.path(), &recipe, None, &cancel).is_err(), "tampered module");
}

/// The REAL MM3D catalog (module window): opt-in with TRIAEVUM_MM3D_TITLE_PACKAGE
/// = a package root laid out by precompiled_title_layout.title_layout. Binds the
/// built module through validate_title, then records the cross-title and
/// tampered-identity negatives on the same catalog.
#[test]
fn mm3d_real_title_catalog_binds_through_validate_title() {
    use forge_catalog::precompiled;
    use serde_json::Value;
    let Some(root) = std::env::var_os("TRIAEVUM_MM3D_TITLE_PACKAGE").map(PathBuf::from) else {
        eprintln!("skip: TRIAEVUM_MM3D_TITLE_PACKAGE not set");
        return;
    };
    let recipe = recipes::load_recipe(&repo().join(MM3D.embedded_recipes), "mm3d-eur-local-6eec27b1").unwrap();
    let oot = recipes::load_recipes(&repo().join(OOT3D.embedded_recipes)).unwrap();
    let oot_recipe = oot[0].as_object().unwrap().clone();
    let cancel = forge_base::CancelToken::new();
    let bound = forge_catalog::validate_title(&root, &recipe, None, &cancel).unwrap();
    assert_eq!(bound["recipe"], "mm3d-eur-local-6eec27b1");
    assert_eq!(bound["abi_version"], 2);
    let identity = bound["translator_identity_sha256"].as_str().unwrap().to_owned();
    eprintln!("bound mm3d translator_identity_sha256={identity}");
    // Optional pin: the identity of the module generation the catalog must carry
    // (round-6 window 2: generation 2020030e, translator dd2ff99a...).
    if let Some(expected) = std::env::var_os("TRIAEVUM_MM3D_TITLE_IDENTITY") {
        assert_eq!(identity, expected.to_string_lossy(), "MM3D catalog carries a different module generation");
    }
    let error = forge_catalog::validate_title(&root, &oot_recipe, None, &cancel).unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedRevision, "OoT3D recipe bound to the MM3D catalog");
    let text = std::fs::read_to_string(root.join(precompiled::CATALOG)).unwrap();
    let mut catalog: serde_json::Map<String, Value> = serde_json::from_str(&text).unwrap();
    catalog["titles"][0]["abi_version"] = Value::from(1);
    assert!(forge_catalog::validate_title(&root, &recipe, Some(&catalog), &cancel).is_err(), "ABI 1 accepted");
    catalog["titles"][0]["abi_version"] = Value::from(2);
    catalog["titles"][0]["plugin"]["sha256"] = Value::from("0".repeat(64));
    assert!(forge_catalog::validate_title(&root, &recipe, Some(&catalog), &cancel).is_err(), "wrong module digest accepted");
}
