// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! ROM recognition against a synthetic cartridge and a fake runtime: the
//! revision from the executable, the languages from the RomFS read in place.

use std::cell::RefCell;
use std::path::{Path, PathBuf};

use forge_base::hashing::sha256_hex;
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_core::install::{InstallSource, InstallTarget};
use forge_core::recognize::{language_choices, recognize_rom, RecognizeRequest};
use forge_native::{
    DeviceJob, InteriorSunRequest, LanguageQuery, NativeOutput, NativeServices, PortableCompile, RendererPassRequest,
    Report, TransferableImport,
};
use serde_json::{json, Value};

const ROMFS_OFFSET: usize = 0x2000;

fn put_u32(data: &mut [u8], offset: usize, value: u32) {
    data[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn exheader() -> Vec<u8> {
    let mut bytes: Vec<u8> = (0..0x800).map(|index| ((index * 3) & 0xFF) as u8).collect();
    bytes[0x0D] = 0;
    bytes[0x200..0x208].copy_from_slice(&0x0004_0000_0003_3500u64.to_le_bytes());
    bytes
}

fn code() -> Vec<u8> {
    b"recognized executable".to_vec()
}

/// A bare decrypted NCCH: ExHeader, ExeFS with an uncompressed `.code`, an
/// IVFC RomFS of 0x1000 bytes at 0x2000.
fn cartridge(code: &[u8]) -> Vec<u8> {
    let mut image = vec![0u8; ROMFS_OFFSET + 0x1000];
    image[0x100..0x104].copy_from_slice(b"NCCH");
    image[0x118..0x120].copy_from_slice(&0x0004_0000_0003_3500u64.to_le_bytes());
    put_u32(&mut image, 0x180, 0x400);
    image[0x18D] = 0x02;
    image[0x200..0xA00].copy_from_slice(&exheader());
    put_u32(&mut image, 0x1A0, 0xC00 / 0x200);
    put_u32(&mut image, 0x1A4, 2);
    image[0xC00..0xC08].copy_from_slice(b".code\0\0\0");
    put_u32(&mut image, 0xC0C, code.len() as u32);
    image[0xE00..0xE00 + code.len()].copy_from_slice(code);
    put_u32(&mut image, 0x1B0, (ROMFS_OFFSET / 0x200) as u32);
    put_u32(&mut image, 0x1B4, 0x1000 / 0x200);
    image[ROMFS_OFFSET..ROMFS_OFFSET + 4].copy_from_slice(b"IVFC");
    image
}

fn identity(bytes: &[u8]) -> Value {
    json!({"bytes": bytes.len(), "sha256": sha256_hex(bytes)})
}

struct Fixture {
    _directory: tempfile::TempDir,
    package: PathBuf,
    data: PathBuf,
    rom: PathBuf,
    extracted: PathBuf,
}

impl Fixture {
    fn new() -> Self {
        let directory = tempfile::tempdir().unwrap();
        let root = forge_prepare::paths::resolve(directory.path()).unwrap();
        let package = root.join("package");
        let inputs = json!({"code": identity(&code()), "exheader": identity(&exheader()),
                            "romfs": {"bytes": 0x1000, "sha256": "e".repeat(64)}});
        let recipes = json!({"format": "triaevum_supported_revisions_v1", "recipes": [
            {"id": "fixture-eur", "title": "Fixture", "region": "EUR", "inputs": inputs}]});
        std::fs::create_dir_all(package.join("recipes")).unwrap();
        std::fs::write(package.join("recipes/oot3d.json"), recipes.to_string()).unwrap();
        let rom = root.join("game.cci");
        std::fs::write(&rom, cartridge(&code())).unwrap();
        let extracted = root.join("extracted");
        std::fs::create_dir_all(&extracted).unwrap();
        std::fs::write(extracted.join("code.bin"), code()).unwrap();
        std::fs::write(extracted.join("exheader.bin"), exheader()).unwrap();
        std::fs::write(extracted.join("romfs.bin"), b"IVFC").unwrap();
        Self { _directory: directory, package, data: root.join("data"), rom, extracted }
    }

    fn recognize(&self, source: InstallSource, services: &FakeRuntime) -> Result<Value> {
        let request = RecognizeRequest {
            source,
            data_root: Some(self.data.clone()),
            package_root: self.package.clone(),
            target: InstallTarget::Package,
        };
        let mut stages = Vec::new();
        let mut report = |stage: &str, _: &str| stages.push(stage.to_string());
        let result = recognize_rom(request, services, &mut report, &CancelToken::new());
        if result.is_ok() {
            assert_eq!(stages, ["identify", "verify", "languages"]);
        }
        result
    }
}

/// Reports English and Italian, or fails like a runtime without the mode.
#[derive(Default)]
struct FakeRuntime {
    fail: bool,
    queries: RefCell<Vec<LanguageQuery>>,
}

impl NativeServices for FakeRuntime {
    fn product_info(&self, _: &Path, _: Option<&Path>) -> Result<NativeOutput> {
        Err(ForgeError::internal("recognition needs no product query"))
    }

    fn game_language_info(&self, _runtime: &Path, query: &LanguageQuery) -> Result<NativeOutput> {
        self.queries.borrow_mut().push(query.clone());
        if self.fail {
            return Ok(NativeOutput { status: 1, stdout: String::new(), stderr: "unknown option".into() });
        }
        let document = json!({"format": "triaevum_game_language_v1", "selected": "en", "available": [
            {"code": "en", "label": "English", "system_id": 1}, {"code": "it", "label": "Italiano", "system_id": 4}]});
        Ok(NativeOutput { status: 0, stdout: document.to_string(), stderr: String::new() })
    }

    fn prebuild_interior_sun(&self, _: &InteriorSunRequest, _: &CancelToken) -> Result<NativeOutput> {
        Err(ForgeError::internal("unexpected"))
    }

    fn prepare_renderer_passes(&self, _: &RendererPassRequest) -> Result<NativeOutput> {
        Err(ForgeError::internal("unexpected"))
    }

    fn import_transferable_shaders(&self, _: &TransferableImport) -> Result<NativeOutput> {
        Err(ForgeError::internal("unexpected"))
    }

    fn compile_portable_shaders(&self, _: &PortableCompile) -> Result<NativeOutput> {
        Err(ForgeError::internal("unexpected"))
    }

    fn prepare_device_pipelines(&self, _: &DeviceJob, _: &CancelToken, _: Report<'_>) -> Result<i64> {
        Err(ForgeError::internal("unexpected"))
    }
}

#[test]
fn a_cartridge_is_recognized_in_place_with_its_languages() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime::default();
    let result = fixture.recognize(InstallSource::Rom(fixture.rom.clone()), &runtime).unwrap();
    assert_eq!(result["recipe"], "fixture-eur");
    assert_eq!(result["label"], "Fixture (EUR)");
    assert_eq!(result["program_id"], "0004000000033500");
    assert_eq!(result["language_error"], Value::Null);
    let (choices, selected) = language_choices(result["languages"].as_object().unwrap());
    assert_eq!(choices, [("en".into(), "English".into()), ("it".into(), "Italiano".into())]);
    assert_eq!(selected.as_deref(), Some("en"));
    // The runtime reads the RomFS extent of the cartridge; nothing is extracted.
    assert_eq!(
        runtime.queries.borrow()[0],
        LanguageQuery { romfs: fixture.rom.clone(), extent: Some((0x2000, 0x1000)) }
    );
    assert!(!fixture.data.exists());
}

#[test]
fn the_current_language_setting_is_the_default() {
    let fixture = Fixture::new();
    let installed = json!({"format": "triaevum_game_language_v1", "selected": "it", "available": [
        {"code": "it", "label": "Italiano", "system_id": 4}]});
    forge_native::language::install(&fixture.data, &installed).unwrap();
    let result = fixture.recognize(InstallSource::Rom(fixture.rom.clone()), &FakeRuntime::default()).unwrap();
    assert_eq!(result["languages"]["selected"], "it");
    assert_eq!(result["languages"]["available"].as_array().unwrap().len(), 2);
}

#[test]
fn extracted_data_is_recognized_from_its_files() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime::default();
    let result = fixture.recognize(InstallSource::ExtractedDirectory(fixture.extracted.clone()), &runtime).unwrap();
    assert_eq!(result["recipe"], "fixture-eur");
    assert_eq!(runtime.queries.borrow()[0], LanguageQuery::romfs(fixture.extracted.join("romfs.bin")));
}

#[test]
fn unreadable_languages_keep_the_recognized_revision() {
    let fixture = Fixture::new();
    let runtime = FakeRuntime { fail: true, ..FakeRuntime::default() };
    let result = fixture.recognize(InstallSource::Rom(fixture.rom.clone()), &runtime).unwrap();
    assert_eq!(result["label"], "Fixture (EUR)");
    assert_eq!(result["languages"], Value::Null);
    assert_eq!(result["language_error"], "Cannot inspect ROM languages: unknown option");
}

#[test]
fn unsupported_and_unreadable_roms_are_errors() {
    let fixture = Fixture::new();
    std::fs::write(&fixture.rom, cartridge(b"another executable")).unwrap();
    let error = fixture.recognize(InstallSource::Rom(fixture.rom.clone()), &FakeRuntime::default()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedRevision);
    assert_eq!(
        error.message,
        "the decrypted ROM does not match any supported game revision (program ID 0004000000033500)"
    );
    std::fs::write(&fixture.rom, vec![0u8; 0x400]).unwrap();
    let error = fixture.recognize(InstallSource::Rom(fixture.rom.clone()), &FakeRuntime::default()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::UnsupportedFormat);
    let other = fixture.rom.with_extension("bin");
    std::fs::write(&other, cartridge(&code())).unwrap();
    let error = fixture.recognize(InstallSource::Rom(other), &FakeRuntime::default()).unwrap_err();
    assert_eq!(error.message, "Forge accepts decrypted ROMs with .3ds or .cci extension");
}
