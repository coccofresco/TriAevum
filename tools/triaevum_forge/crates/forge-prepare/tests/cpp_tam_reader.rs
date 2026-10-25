// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Feeds Rust-written TAM files to the runtime's own C++ reader test
//! (`triaevum_module_tests`, built from runtime/triaevum_module; it parses
//! one external TAM given as its argument). Runs when TRIAEVUM_TAM_READER_TEST
//! names that executable.

use std::process::Command;

use forge_base::CancelToken;
use forge_prepare::{write_tam_file, TamRequest};

#[test]
fn cpp_reader_accepts_rust_written_modules() {
    let Ok(reader) = std::env::var("TRIAEVUM_TAM_READER_TEST") else {
        eprintln!("skipped: TRIAEVUM_TAM_READER_TEST is not set");
        return;
    };
    let directory = tempfile::tempdir().unwrap();
    let native = directory.path().join("native.dll");
    let title_aot = directory.path().join("title.dll");
    std::fs::write(&native, b"MZ native module image").unwrap();
    std::fs::write(&title_aot, vec![0x90u8; 100_003]).unwrap();
    let source = "a".repeat(64);
    let translator = "b".repeat(64);
    let services = [0x4143_4950, 0x4944_5541, 0x5359_5346, 0x3344_4948];
    let regions = [(0x2000_0000, 0x1400_0000, 0x0400_0000), (0x1800_0000, 0x1F00_0000, 0x0060_0000)];
    for (index, aot) in [None, Some(title_aot.as_path())].into_iter().enumerate() {
        let output = directory.path().join(format!("module{index}.tam"));
        let request = TamRequest {
            native_image: &native,
            title_aot_image: aot,
            recipe: "fixture",
            target_triple: "x86_64-pc-windows-msvc",
            source_identity: &source,
            translator_identity: &translator,
            required_services: &services,
            physical_memory_regions: &regions,
            runtime_abi: 1,
        };
        write_tam_file(&output, &request, &CancelToken::new()).unwrap();
        let result = Command::new(&reader).arg(&output).output().unwrap();
        assert!(
            result.status.success(),
            "C++ reader refused {}: {}{}",
            output.display(),
            String::from_utf8_lossy(&result.stdout),
            String::from_utf8_lossy(&result.stderr)
        );
    }
}
