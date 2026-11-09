// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! B2 host proof: the Moonfall Curse APK's Forge (forge-android with
//! `title-mm3d`, the crate graph libtriaevum_forge.so links) reconstructs the
//! Project Restoration layout byte for byte as the desktop Forge's Python
//! reference (tools/mm3d/restoration_layout.py) does, on a synthetic case in
//! the EUR dump layout. Run with the Python oracle:
//!
//! ```text
//! TRIAEVUM_FORGE_PYTHON=python cargo test -p forge-android --features title-mm3d \
//!     --test mm3d_produce_at_install -- --include-ignored
//! ```

#![cfg(feature = "title-mm3d")]

use std::path::{Path, PathBuf};
use std::process::Command;

use forge_assets::mm3d_restoration_layout::reconstruct;
use forge_base::hashing::sha256_hex;

fn repository() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../..").canonicalize().unwrap()
}

fn files(root: &Path, base: &Path, out: &mut Vec<(String, Vec<u8>)>) {
    for entry in std::fs::read_dir(root).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            files(&path, base, out);
        } else {
            let relative = path.strip_prefix(base).unwrap().to_string_lossy().replace('\\', "/");
            out.push((relative, std::fs::read(&path).unwrap()));
        }
    }
}

#[test]
#[ignore = "needs the Python desktop oracle (TRIAEVUM_FORGE_PYTHON)"]
fn restoration_layout_matches_the_desktop_forge() {
    let python = std::env::var("TRIAEVUM_FORGE_PYTHON").expect("TRIAEVUM_FORGE_PYTHON");
    let case = tempfile::tempdir().unwrap();
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/oracle/restoration_case.py");
    let status = Command::new(python).arg("-I").arg(&script).arg(repository()).arg(case.path()).status().unwrap();
    assert!(status.success(), "the Python oracle failed");
    let root = case.path();
    let recipe: serde_json::Value = serde_json::from_slice(&std::fs::read(root.join("layout.recipe.json")).unwrap()).unwrap();
    let produced = reconstruct(
        &recipe,
        &mut |name| Ok(std::fs::read(root.join("dump").join(name)).ok()),
        &mut |relative| Ok(std::fs::read(root.join("resources").join(relative)).unwrap()),
    )
    .unwrap();
    let mut expected = Vec::new();
    files(&root.join("expected"), &root.join("expected"), &mut expected);
    expected.sort();
    assert_eq!(expected.len(), 2);
    let actual: Vec<(String, Vec<u8>)> = produced.into_iter().collect();
    for ((path, bytes), (want_path, want)) in actual.iter().zip(&expected) {
        assert_eq!(path, want_path);
        assert_eq!(sha256_hex(bytes), sha256_hex(want), "{path}");
        println!("{path} {}", sha256_hex(bytes));
    }
    assert_eq!(actual.len(), expected.len());
}

#[test]
fn a_changed_eur_base_is_refused() {
    // The same recipe shape against a dump whose base member differs.
    let recipe = serde_json::json!({"format": "triaevum_restoration_layout_v1", "outputs": [{
        "path": "layout/Joker.mfpj", "sha256": "0".repeat(64),
        "source": {"patch": "p.bps", "base": "layout/Joker.mfpj", "base_sha256": sha256_hex(b"eur"), "sha256": "0".repeat(64)}}]});
    let error = reconstruct(&recipe, &mut |_| Ok(Some(b"usa".to_vec())), &mut |_| Ok(Vec::new())).unwrap_err();
    assert!(error.to_string().contains("EUR"), "{error}");
}
