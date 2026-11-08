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
