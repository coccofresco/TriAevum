// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Every title's local supported-revision recipe (`titles/<game>/forge/
//! supported_revisions.json`) goes through the SAME Forge path as the OoT3D
//! embedded recipes: `load_recipes`/`load_recipe`, `recognize_recipes` and
//! `verify_input`. When the extracted inputs of a title are available
//! (TRIAEVUM_<GAME>_EXTRACT=dir with code.bin, exheader.bin, romfs.bin) they
//! must be recognized by exactly that title's recipe and rejected by every
//! other title's recipes.

use std::path::{Path, PathBuf};

use forge_base::CancelToken;
use forge_catalog::recipes::{self, FreshDigest};
use serde_json::Value;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).ancestors().nth(4).unwrap().to_path_buf()
}

fn title_recipes() -> Vec<(String, PathBuf)> {
    let mut found = Vec::new();
    for entry in std::fs::read_dir(repo().join("titles")).unwrap().flatten() {
        let path = entry.path().join("forge").join("supported_revisions.json");
        if path.is_file() {
            found.push((entry.file_name().to_string_lossy().into_owned(), path));
        }
    }
    if !found.iter().any(|(game, _)| game == "oot3d") {
        found.push(("oot3d".into(), repo().join(recipes::EMBEDDED_RECIPES)));
    }
    found.sort();
    found
}

#[test]
fn title_recipes_share_the_supported_revision_contract() {
    let titles = title_recipes();
    assert!(titles.iter().any(|(game, _)| game == "mm3d"), "no local MM3D recipe registered");
    let mut ids = std::collections::BTreeSet::new();
    for (game, path) in &titles {
        let list = recipes::load_recipes(path).unwrap();
        assert!(!list.is_empty(), "{game}: empty recipe list");
        for recipe in &list {
            let id = recipe["id"].as_str().unwrap().to_string();
            assert!(ids.insert(id.clone()), "duplicate revision id {id}");
            assert!(id.starts_with(&format!("{game}-")), "{id} is not owned by {game}");
            if let Some(declared) = recipe.get("game") {
                assert_eq!(declared.as_str(), Some(game.as_str()), "{id}: wrong game");
            }
            for kind in ["code", "exheader", "romfs"] {
                let contract = &recipe["inputs"][kind];
                assert!(contract["bytes"].as_u64().unwrap_or(0) > 0, "{id}: {kind} size");
                assert_eq!(contract["sha256"].as_str().map(str::len), Some(64), "{id}: {kind} digest");
            }
            let process = &recipe["process"];
            for field in ["base", "entrypoint", "executable_size"] {
                assert!(process[field].as_u64().unwrap_or(0) > 0, "{id}: process.{field}");
            }
            recipes::load_recipe(path, &id).unwrap();
        }
    }
}

#[test]
fn extracted_inputs_select_only_their_own_title() {
    let titles = title_recipes();
    let cancel = CancelToken::new();
    let mut checked = 0;
    for (game, own) in &titles {
        let Ok(dir) = std::env::var(format!("TRIAEVUM_{}_EXTRACT", game.to_uppercase())) else { continue };
        let dir = PathBuf::from(dir);
        let code = std::fs::read(dir.join("code.bin")).unwrap();
        let exheader = std::fs::read(dir.join("exheader.bin")).unwrap();
        let matched = recipes::recognize_recipes(0, &code, &exheader, own).unwrap();
        assert_eq!(matched.len(), 1, "{game}: recognition must be unique");
        let recipe = &matched[0];
        let contracts = recipe["inputs"].as_object().unwrap();
        for kind in ["code", "exheader", "romfs"] {
            let contract = contracts[kind].as_object().unwrap();
            recipes::verify_input(kind, &dir.join(format!("{kind}.bin")), contract, &mut FreshDigest(&cancel))
                .unwrap_or_else(|e| panic!("{game}: {kind} does not satisfy its recipe: {e:?}"));
        }
        // Wrong-title rejection: no other title's recipes accept these bytes.
        for (other, path) in titles.iter().filter(|(other, _)| other != game) {
            assert!(recipes::recognize_recipes(0, &code, &exheader, path).is_err(), "{other} accepted {game} inputs");
        }
        // A one-byte change of the executable is an unsupported revision.
        let mut tampered = code.clone();
        tampered[0x1000] ^= 1;
        assert!(recipes::recognize_recipes(0, &tampered, &exheader, own).is_err());
        // The process contract is the ExHeader text segment.
        let entry = u64::from(u32::from_le_bytes(exheader[0x10..0x14].try_into().unwrap()));
        let pages = u64::from(u32::from_le_bytes(exheader[0x14..0x18].try_into().unwrap()));
        assert_eq!(recipe["process"]["entrypoint"], Value::from(entry));
        assert_eq!(recipe["process"]["executable_size"], Value::from(pages * 0x1000));
        checked += 1;
    }
    eprintln!("extracted titles checked: {checked}");
}
