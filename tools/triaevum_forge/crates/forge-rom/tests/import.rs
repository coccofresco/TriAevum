// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! ROM extraction, extracted-directory staging and content-addressed
//! publication, mirroring test_ctr_rom.py and test_extracted_inputs.py.

mod common;

use std::fs;
use std::io::Cursor;
use std::path::{Path, PathBuf};

use common::*;
use forge_base::hashing::{sha256_bytes, sha256_hex};
use forge_base::{CancelToken, ErrorKind, ForgeError};
use forge_rom::*;

fn cancel() -> CancelToken {
    CancelToken::new()
}

fn canonical(path: &Path) -> PathBuf {
    let text = fs::canonicalize(path).unwrap().to_string_lossy().into_owned();
    PathBuf::from(text.strip_prefix(r"\\?\").unwrap_or(&text))
}

fn write_rom(root: &Path, name: &str, image: &[u8]) -> PathBuf {
    let path = root.join(name);
    fs::write(&path, image).unwrap();
    path
}

fn extract(rom: &Path, output: &Path) -> Result<ExtractedTitleInputs, ForgeError> {
    extract_decrypted_rom(rom, output, true, &cancel())
}

#[test]
fn accepts_3ds_and_cci_as_the_same_ncsd_container() {
    let root = tempfile::tempdir().unwrap();
    for (index, suffix) in [".3ds", ".cci", ".CCI"].into_iter().enumerate() {
        let rom = write_rom(root.path(), &format!("game{index}{suffix}"), &build_rom(true));
        let output = root.path().join(format!("out-{index}"));
        let extracted = extract(&rom, &output).unwrap();
        assert_eq!(extracted.container_kind, ContainerKind::Ncsd);
        assert_eq!(extracted.partition_index, 0);
        assert_eq!(extracted.program_id, PROGRAM_ID);
        assert_eq!(fs::read(&extracted.code.path).unwrap(), CODE);
        assert_eq!(fs::read(&extracted.exheader.path).unwrap(), expected_exheader());
        assert_eq!(extracted.romfs.bytes, 0x2000);
        let mut romfs = b"IVFC".to_vec();
        romfs.resize(0x2000, b'R');
        assert_eq!(extracted.romfs.sha256_hex(), sha256_hex(&romfs));
        assert_eq!(extracted.code.path, canonical(&output).join("code.bin"));
    }
}

#[test]
fn accepts_plaintext_when_legacy_crypto_flag_is_retained() {
    let root = tempfile::tempdir().unwrap();
    let rom = write_rom(root.path(), "legacy-flag.cci", &build_rom(false));
    let extracted = extract(&rom, &root.path().join("output")).unwrap();
    assert_eq!(fs::read(extracted.code.path).unwrap(), CODE);
}

#[test]
fn public_import_requires_a_rom_extension() {
    let root = tempfile::tempdir().unwrap();
    let rom = write_rom(root.path(), "game.bin", &build_rom(true));
    let error = extract(&rom, &root.path().join("output")).unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedFormat);
    assert_eq!(error.message, "Forge accepts decrypted ROMs with .3ds or .cci extension");
    assert!(!root.path().join("output").exists());
    let extracted = extract_decrypted_rom(&rom, &root.path().join("output"), false, &cancel()).unwrap();
    assert_eq!(extracted.code.bytes, CODE.len() as u64);
}

#[test]
fn supported_suffix_check_is_case_insensitive() {
    assert!(is_supported_rom_path(Path::new("game.3DS")));
    assert!(is_supported_rom_path(Path::new("game.CCI")));
    assert!(!is_supported_rom_path(Path::new("game.cia")));
    assert!(!is_supported_rom_path(Path::new(".3ds")));
    assert!(!is_supported_rom_path(Path::new("game.")));
}

#[test]
fn missing_rom_directory_rom_and_symlink_rom_are_rejected() {
    let root = tempfile::tempdir().unwrap();
    let missing = root.path().join("missing.3ds");
    let error = extract(&missing, &root.path().join("output")).unwrap_err();
    assert_eq!(
        error.message,
        format!("the selected ROM does not exist: {}", canonical(root.path()).join("missing.3ds").display())
    );
    let directory = root.path().join("folder.3ds");
    fs::create_dir(&directory).unwrap();
    assert!(extract(&directory, &root.path().join("output"))
        .unwrap_err()
        .message
        .starts_with("the selected ROM does not exist"));

    let rom = write_rom(root.path(), "game.3ds", &build_rom(true));
    let link = root.path().join("link.3ds");
    #[cfg(windows)]
    let linked = std::os::windows::fs::symlink_file(&rom, &link);
    #[cfg(unix)]
    let linked = std::os::unix::fs::symlink(&rom, &link);
    if linked.is_ok() {
        let error = extract(&link, &root.path().join("output")).unwrap_err();
        assert_eq!(error.message, "the selected ROM must be a regular local file");
    } else {
        eprintln!("symlink case skipped: cannot create symlinks here");
    }
    assert!(!root.path().join("output").exists());
}

#[test]
fn existing_output_directory_is_untouched() {
    let root = tempfile::tempdir().unwrap();
    let rom = write_rom(root.path(), "game.3ds", &build_rom(true));
    let output = root.path().join("output");
    fs::create_dir(&output).unwrap();
    fs::write(output.join("keep"), b"keep").unwrap();
    let error = extract(&rom, &output).unwrap_err();
    assert_eq!(error.message, format!("ROM extraction directory already exists: {}", canonical(&output).display()));
    assert_eq!(fs::read(output.join("keep")).unwrap(), b"keep");
}

#[test]
fn failures_remove_only_the_new_directory() {
    let root = tempfile::tempdir().unwrap();
    let mut image = build_rom(true);
    image[0x100] = b'X';
    let rom = write_rom(root.path(), "bad.3ds", &image);
    let output = root.path().join("parent").join("output");
    assert_eq!(extract(&rom, &output).unwrap_err().kind, ErrorKind::UnsupportedFormat);
    assert!(!output.exists());
    assert!(root.path().join("parent").is_dir());

    let mut invalid_code = CODE.to_vec();
    invalid_code.extend_from_slice(&[0, 0, 0, 0x07, 0, 0, 0, 0]);
    let rom = write_rom(root.path(), "badcode.3ds", &ncsd(&[ncch(&invalid_code, true, true)]));
    let error = extract(&rom, &output).unwrap_err();
    assert_eq!(error.message, "compressed ExeFS .code has an invalid footer");
    assert!(!output.exists());

    let rom = write_rom(root.path(), "good.3ds", &build_rom(true));
    let cancelled = CancelToken::new();
    cancelled.cancel();
    assert_eq!(extract_decrypted_rom(&rom, &output, true, &cancelled).unwrap_err().kind, ErrorKind::Cancelled);
    assert!(!output.exists());
}

#[test]
fn compressed_code_is_decompressed() {
    let root = tempfile::tempdir().unwrap();
    let mut rng = Rng(11);
    let plain = compressible(&mut rng, 6000);
    let packed = blz_compress(&plain, 16, 3, 0x1002).unwrap();
    let rom = write_rom(root.path(), "compressed.cci", &ncsd(&[ncch(&packed, true, true)]));
    let extracted = extract(&rom, &root.path().join("output")).unwrap();
    assert_eq!(fs::read(&extracted.code.path).unwrap(), plain);
    assert_eq!(extracted.code.sha256, sha256_bytes(&plain));
    assert_eq!(fs::read(&extracted.exheader.path).unwrap()[0x0D], 1);
}

#[test]
fn reader_source_matches_path_source() {
    let root = tempfile::tempdir().unwrap();
    let image = ncch(CODE, false, true);
    let rom = write_rom(root.path(), "bare.cxi", &image);
    let from_path = extract_decrypted_rom(&rom, &root.path().join("a"), false, &cancel()).unwrap();
    let from_reader =
        extract_decrypted_rom_from(&mut Cursor::new(&image), image.len() as u64, &root.path().join("b"), &cancel())
            .unwrap();
    assert_eq!(from_path.container_kind, ContainerKind::Ncch);
    assert_eq!(input_identity(&from_path), input_identity(&from_reader));
}

#[test]
fn identity_preimage_uses_raw_digests() {
    let root = tempfile::tempdir().unwrap();
    let rom = write_rom(root.path(), "game.3ds", &build_rom(true));
    let extracted = extract(&rom, &root.path().join("output")).unwrap();
    let mut preimage = b"triaevum-extracted-title-inputs-v1\0".to_vec();
    for (kind, item) in [("code", CODE.to_vec()), ("exheader", expected_exheader())] {
        preimage.extend_from_slice(kind.as_bytes());
        preimage.push(0);
        preimage.extend_from_slice(&sha256_bytes(&item));
    }
    preimage.extend_from_slice(b"romfs\0");
    preimage.extend_from_slice(&extracted.romfs.sha256);
    assert_eq!(input_identity(&extracted), sha256_hex(&preimage));
}

#[test]
fn publishes_inputs_under_a_stable_content_identity() {
    let root = tempfile::tempdir().unwrap();
    let sources = root.path().join("sources");
    let first_rom = write_rom(root.path(), "first.cci", &build_rom(true));
    let first_staging = root.path().join("first-staging");
    let first = publish_extracted_inputs(&extract(&first_rom, &first_staging).unwrap(), &sources, &cancel()).unwrap();
    assert!(!first_staging.exists());

    let second_rom = write_rom(root.path(), "second.3ds", &build_rom(true));
    let second_staging = root.path().join("second-staging");
    let second =
        publish_extracted_inputs(&extract(&second_rom, &second_staging).unwrap(), &sources, &cancel()).unwrap();
    assert!(!second_staging.exists());
    assert_eq!(first, second);
    assert_eq!(first.code.path.parent().unwrap().file_name().unwrap().to_string_lossy(), input_identity(&first));
}

#[test]
fn publish_rejects_mismatched_or_invalid_destinations() {
    let root = tempfile::tempdir().unwrap();
    let sources = root.path().join("sources");
    let rom = write_rom(root.path(), "game.cci", &build_rom(true));
    let published =
        publish_extracted_inputs(&extract(&rom, &root.path().join("s1")).unwrap(), &sources, &cancel()).unwrap();

    fs::write(&published.exheader.path, vec![0u8; 0x800]).unwrap();
    let staging = root.path().join("s2");
    let error = publish_extracted_inputs(&extract(&rom, &staging).unwrap(), &sources, &cancel()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::ArtifactIntegrity);
    assert_eq!(error.message, "existing private exheader input does not match its content identity");
    assert!(staging.is_dir());

    let other = root.path().join("other-sources");
    fs::create_dir(&other).unwrap();
    let extracted = extract(&rom, &root.path().join("s3")).unwrap();
    let blocker = other.join(input_identity(&extracted));
    fs::write(&blocker, b"").unwrap();
    let error = publish_extracted_inputs(&extracted, &other, &cancel()).unwrap_err();
    assert_eq!(error.message, format!("private title source path is invalid: {}", canonical(&blocker).display()));

    let mut split = extracted.clone();
    split.romfs.path = root.path().join("elsewhere").join("romfs.bin");
    let error = publish_extracted_inputs(&split, &other, &cancel()).unwrap_err();
    assert_eq!(error.message, "extracted title inputs do not share one staging directory");
}

fn extracted_source(root: &Path) -> ExtractedTitleInputs {
    let rom = write_rom(root, "test.cci", &build_rom(true));
    extract(&rom, &root.join("source")).unwrap()
}

#[test]
fn staged_directory_has_the_extraction_identity_and_preserves_source() {
    let root = tempfile::tempdir().unwrap();
    let original = extracted_source(root.path());
    let staged = stage_directory(&root.path().join("source"), &root.path().join("staging"), &cancel()).unwrap();
    assert_eq!(staged.container_kind, ContainerKind::Extracted);
    assert_eq!(staged.program_id, u64::from_le_bytes(expected_exheader()[0x200..0x208].try_into().unwrap()));
    assert_eq!(input_identity(&original), input_identity(&staged));
    publish_extracted_inputs(&staged, &root.path().join("published"), &cancel()).unwrap();
    for (_, item) in original.by_kind() {
        assert_eq!(sha256_bytes(&fs::read(&item.path).unwrap()), item.sha256);
    }
}

#[test]
fn missing_and_invalid_inputs_are_rejected_without_output() {
    let cases: [(&str, &[u8], &str); 3] = [
        ("code", b"", "Extracted code.bin has an invalid size"),
        ("exheader", b"bad", "Extracted ExHeader must contain exactly 2048 bytes"),
        ("romfs", b"encrypted", "Extracted romfs.bin must be a decrypted IVFC image"),
    ];
    for (kind, data, message) in cases {
        let root = tempfile::tempdir().unwrap();
        extracted_source(root.path());
        let input = root.path().join("source").join(format!("{kind}.bin"));
        let staging = root.path().join("staging");
        fs::write(&input, data).unwrap();
        assert_eq!(stage_directory(&root.path().join("source"), &staging, &cancel()).unwrap_err().message, message);
        assert!(!staging.exists());
        fs::remove_file(&input).unwrap();
        assert_eq!(
            stage_directory(&root.path().join("source"), &staging, &cancel()).unwrap_err().message,
            format!("Extracted data requires a local {kind}.bin file")
        );
        fs::create_dir(&input).unwrap();
        assert_eq!(
            stage_directory(&root.path().join("source"), &staging, &cancel()).unwrap_err().message,
            format!("Extracted data requires a local {kind}.bin file")
        );
    }
}

#[test]
fn existing_nested_and_non_directory_outputs_are_refused() {
    let root = tempfile::tempdir().unwrap();
    extracted_source(root.path());
    let source = root.path().join("source");
    for output in [source.clone(), source.join("nested")] {
        assert_eq!(
            stage_directory(&source, &output, &cancel()).unwrap_err().message,
            "Staging must be outside the source directory"
        );
    }
    let staging = root.path().join("staging");
    fs::create_dir(&staging).unwrap();
    fs::write(staging.join("keep"), b"keep").unwrap();
    assert!(stage_directory(&source, &staging, &cancel()).is_err());
    assert_eq!(fs::read(staging.join("keep")).unwrap(), b"keep");

    assert_eq!(
        stage_directory(&source.join("code.bin"), &root.path().join("x"), &cancel()).unwrap_err().message,
        "Select a regular extracted-data directory"
    );
    assert_eq!(
        stage_directory(&root.path().join("absent"), &root.path().join("x"), &cancel()).unwrap_err().message,
        "Select a regular extracted-data directory"
    );
    let cancelled = CancelToken::new();
    cancelled.cancel();
    assert_eq!(stage_directory(&source, &root.path().join("x"), &cancelled).unwrap_err().kind, ErrorKind::Cancelled);
    assert!(!root.path().join("x").exists());
}
