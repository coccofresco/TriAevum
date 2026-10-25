// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Local-only check against a user's own RomFS, never part of the suite:
//!
//! ```text
//! TRIAEVUM_FORGE_REAL_ROMFS=<extracted romfs directory or romfs.bin> \
//! TRIAEVUM_FORGE_PYTHON=python cargo test --release -p forge-assets --test real_romfs -- --ignored --nocapture
//! ```
//!
//! Builds the water and scene-light catalogs (and, for an image, the font
//! pack) with Rust and, when TRIAEVUM_FORGE_PYTHON is set, with the Python
//! Forge, and compares the bytes. Only digests are printed; outputs stay in a
//! temporary directory.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

use forge_assets::{font_pack, scene_lights, water};
use forge_base::hashing::sha256_hex;
use forge_base::CancelToken;

const PYTHON_BUILDERS: &str = r#"
import sys
from pathlib import Path
repository, romfs, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
sys.path[:0] = [str(repository / "tools" / "triaevum_release"), str(repository)]
import scene_lights, water_textures
(out / "water.txt").write_bytes(water_textures.build_catalog(romfs)[0])
(out / "scene_lights.json").write_bytes(scene_lights.build_catalog(romfs)[0])
if romfs.is_file():
    from tools.oot3d.decomp_support.scripts.prepare_qbf_font_pack import prepare_font_pack
    prepare_font_pack(font=repository / "runtime/triaevum_resources/topscreen/singlescreen-3.1/ltn16.qbf",
                      romfs=romfs, output=out / "font_coverage.zip")
"#;

fn repository() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../..")
}

#[test]
#[ignore = "needs a local RomFS (TRIAEVUM_FORGE_REAL_ROMFS)"]
fn real_romfs_catalogs_match_python() {
    let Ok(romfs) = std::env::var("TRIAEVUM_FORGE_REAL_ROMFS") else {
        eprintln!("skipped: TRIAEVUM_FORGE_REAL_ROMFS is not set");
        return;
    };
    let romfs = PathBuf::from(romfs);
    let cancel = CancelToken::new();
    let directory = tempfile::tempdir().unwrap();
    let mut outputs = Vec::new();
    let started = Instant::now();
    outputs.push(("water.txt", water::build_catalog(&romfs, &cancel).unwrap().0));
    println!("rust water: {:.1}s", started.elapsed().as_secs_f64());
    let started = Instant::now();
    outputs.push(("scene_lights.json", scene_lights::build_catalog(&romfs, None, &cancel).unwrap().0));
    println!("rust scene lights: {:.1}s", started.elapsed().as_secs_f64());
    if romfs.is_file() {
        let font = repository().join("runtime/triaevum_resources/topscreen/singlescreen-3.1/ltn16.qbf");
        let zip = directory.path().join("rust_font_coverage.zip");
        font_pack::prepare_font_pack(&font, &romfs, &zip).unwrap();
        outputs.push(("font_coverage.zip", std::fs::read(zip).unwrap()));
    }
    for (name, bytes) in &outputs {
        println!("rust {name}: {} bytes, sha256 {}", bytes.len(), sha256_hex(bytes));
    }
    let Ok(python) = std::env::var("TRIAEVUM_FORGE_PYTHON") else { return };
    let script = directory.path().join("builders.py");
    std::fs::write(&script, PYTHON_BUILDERS).unwrap();
    let started = Instant::now();
    let status =
        Command::new(python).arg("-I").arg(&script).arg(repository()).arg(&romfs).arg(directory.path()).status().unwrap();
    assert!(status.success());
    println!("python: {:.1}s", started.elapsed().as_secs_f64());
    for (name, bytes) in &outputs {
        let expected = std::fs::read(directory.path().join(name)).unwrap();
        println!("python {name}: {} bytes, sha256 {}", expected.len(), sha256_hex(&expected));
        assert!(bytes == &expected, "{name} differs from Python");
    }
}
