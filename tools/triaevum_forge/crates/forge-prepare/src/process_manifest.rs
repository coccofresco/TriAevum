// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Port of `tools/oot3d/native_a32_runtime/build_process_manifest.py`
//! (`build_manifest`). The constants, the memory-mode table and the system
//! region initial values are part of the data contract: the manifest's
//! pretty JSON bytes are hashed into content.tap.

use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

use serde_json::{json, Value};

use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ForgeError, Result};

use crate::paths::{path_string, resolve};

pub const FORMAT: &str = "oot3d_native_process_manifest_v1";
pub const PAGE_SIZE: u64 = 0x1000;
const HEAP_VADDR_END: u64 = 0x1000_0000;
const TLS_AREA_VADDR: u64 = 0x1FF8_2000;
const TLS_PAGE_SIZE: u64 = 0x1000;
const CONFIG_MEMORY_VADDR: u64 = 0x1FF8_0000;
const SHARED_PAGE_VADDR: u64 = 0x1FF8_1000;
const DSP_RAM_VADDR: u64 = 0x1FF0_0000;
const DSP_RAM_SIZE: u64 = 0x0008_0000;
pub const LEGACY_LINEAR_HEAP_VADDR: u64 = 0x1400_0000;
const HEAP_VADDR: u64 = 0x0800_0000;
const HEAP_SIZE: u64 = 0x0800_0000;
const USER32_CPSR: u64 = 0x10;
const CTR_MAIN_FPSCR: u64 = 0x03C0_0010;
const NCCH_ROMFS_SUPERBLOCK_SIZE: u64 = 0x1000;
pub const CTR_VRAM_VADDR: u64 = 0x1F00_0000;
pub const CTR_VRAM_SIZE: u64 = 0x0060_0000;
const LAYOUT_AUTHORITY: &str = "CTR ExHeader; page/BSS/stack/TLS semantics verified against Azahar NCCH loader";

/// APPLICATION, SYSTEM and BASE region sizes indexed by the ExHeader memory
/// mode (the CTR kernel layout Azahar's KernelSystem also uses).
const CTR_MEMORY_REGION_SIZES: [(u64, u64, u64); 8] = [
    (0x0400_0000, 0x02C0_0000, 0x0140_0000),
    (0, 0, 0),
    (0x0600_0000, 0x00C0_0000, 0x0140_0000),
    (0x0500_0000, 0x01C0_0000, 0x0140_0000),
    (0x0480_0000, 0x0240_0000, 0x0140_0000),
    (0x0200_0000, 0x04C0_0000, 0x0140_0000),
    (0x07C0_0000, 0x0640_0000, 0x0200_0000),
    (0x0B20_0000, 0x02E0_0000, 0x0200_0000),
];

/// The Python ValueError cases. The importer reports them as
/// "cannot derive CTR process manifest: <message>".
fn invalid(message: impl Into<String>) -> ForgeError {
    ForgeError::invalid(message)
}

fn u32_at(data: &[u8], offset: usize) -> Result<u64> {
    data.get(offset..offset + 4)
        .map(|bytes| u64::from(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])))
        .ok_or_else(|| invalid(format!("u32 at {offset:#x} is outside ExHeader")))
}

fn align_up(value: u64, alignment: u64) -> u64 {
    (value + alignment - 1) & !(alignment - 1)
}

/// Raw NCCH IVFC images serve RomFS from 0x1000 (Level-3 header there); a
/// Level-3 service view serves from 0.
pub fn romfs_service_window(path: &Path, file_size: u64) -> Result<(u64, u64)> {
    let mut stream =
        File::open(path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    let prefix = read_up_to(&mut stream, 4, path)?;
    if prefix == b"IVFC" {
        if file_size <= NCCH_ROMFS_SUPERBLOCK_SIZE {
            return Err(invalid("raw NCCH RomFS is smaller than its IVFC superblock"));
        }
        stream
            .seek(SeekFrom::Start(NCCH_ROMFS_SUPERBLOCK_SIZE))
            .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
        if read_up_to(&mut stream, 4, path)? != 0x28u32.to_le_bytes() {
            return Err(invalid("raw NCCH RomFS has no Level-3 header after its IVFC superblock"));
        }
        return Ok((NCCH_ROMFS_SUPERBLOCK_SIZE, file_size - NCCH_ROMFS_SUPERBLOCK_SIZE));
    }
    if prefix == 0x28u32.to_le_bytes() {
        return Ok((0, file_size));
    }
    Err(invalid("RomFS image is neither raw NCCH IVFC nor a Level-3 service view"))
}

fn read_up_to(stream: &mut File, count: u64, path: &Path) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    stream
        .take(count)
        .read_to_end(&mut bytes)
        .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    Ok(bytes)
}

struct Segment {
    address: u64,
    physical_pages: u64,
    code_size: u64,
}

fn segment(exheader: &[u8], offset: usize) -> Result<Segment> {
    Ok(Segment {
        address: u32_at(exheader, offset)?,
        physical_pages: u32_at(exheader, offset + 4)?,
        code_size: u32_at(exheader, offset + 8)?,
    })
}

fn validate_segment(name: &str, item: &Segment) -> Result<()> {
    if !item.address.is_multiple_of(PAGE_SIZE) {
        return Err(invalid(format!("{name} address is not page aligned")));
    }
    if item.physical_pages == 0 {
        return Err(invalid(format!("{name} has no physical pages")));
    }
    if item.code_size > item.physical_pages * PAGE_SIZE {
        return Err(invalid(format!("{name} code size exceeds mapped pages")));
    }
    Ok(())
}

/// (memory mode, application, system, base) region sizes.
fn ctr_memory_layout(exheader: &[u8]) -> Result<(u64, u64, u64, u64)> {
    let memory_mode = usize::from(exheader[0x20D] & 0x0F);
    let Some(&(app, system, base)) = CTR_MEMORY_REGION_SIZES.get(memory_mode) else {
        return Err(invalid(format!("unsupported CTR memory mode {memory_mode}")));
    };
    if app == 0 {
        return Err(invalid(format!("unused CTR memory mode {memory_mode}")));
    }
    Ok((memory_mode as u64, app, system, base))
}

fn initial_value(offset: u64, size: u64, value: u64) -> Value {
    json!({"offset": offset, "size": size, "value": value})
}

fn ctr_system_regions(exheader: &[u8]) -> Result<Value> {
    let (memory_mode, app_memory, system_memory, base_memory) = ctr_memory_layout(exheader)?;
    let config_values = vec![
        initial_value(0x01, 1, 0x3A),
        initial_value(0x03, 1, 0x02),
        initial_value(0x08, 8, 0x0004_0130_0000_8002),
        initial_value(0x10, 4, 0x02),
        initial_value(0x14, 1, 0x01),
        initial_value(0x16, 1, 0x01),
        initial_value(0x18, 4, 0x0000_F450),
        initial_value(0x30, 4, memory_mode),
        initial_value(0x40, 4, app_memory),
        initial_value(0x44, 4, system_memory),
        initial_value(0x48, 4, base_memory),
        initial_value(0x62, 1, 0x3A),
        initial_value(0x63, 1, 0x02),
        initial_value(0x64, 4, 0x02),
        initial_value(0x68, 4, 0x0000_F450),
    ];
    let shared_values = vec![
        initial_value(0x04, 1, 0x01),
        initial_value(0x66, 1, 0x03),
        initial_value(0x67, 1, 0x02),
        initial_value(0x85, 1, 0x17),
        initial_value(0x86, 1, 0x01),
    ];
    Ok(json!([
        {"name": "ctr_vram", "address": CTR_VRAM_VADDR, "mapped_size": CTR_VRAM_SIZE,
         "writable": true, "executable": false, "initial_values": []},
        {"name": "ctr_dsp_ram", "address": DSP_RAM_VADDR, "mapped_size": DSP_RAM_SIZE,
         "writable": true, "executable": false, "initial_values": []},
        {"name": "ctr_config_memory", "address": CONFIG_MEMORY_VADDR, "mapped_size": PAGE_SIZE,
         "writable": false, "executable": false, "initial_values": config_values},
        {"name": "ctr_shared_page", "address": SHARED_PAGE_VADDR, "mapped_size": PAGE_SIZE,
         "writable": false, "executable": false, "initial_values": shared_values},
    ]))
}

/// Python's `bytes.decode("ascii", "strict")` failure text.
fn ascii_name(bytes: &[u8]) -> Result<String> {
    match bytes.iter().position(|byte| !byte.is_ascii()) {
        None => Ok(bytes.iter().map(|&byte| char::from(byte)).collect()),
        Some(position) => Err(invalid(format!(
            "'ascii' codec can't decode byte {:#04x} in position {position}: ordinal not in range(128)",
            bytes[position]
        ))),
    }
}

/// `build_manifest(exheader_path, code_path, romfs_image_path)`: the native
/// process layout derived from the ExHeader and decompressed code. The code
/// and RomFS are streamed, never loaded whole.
pub fn build_manifest(
    exheader_path: &Path,
    code_path: &Path,
    romfs_image_path: &Path,
    cancel: &CancelToken,
) -> Result<Value> {
    let exheader = std::fs::read(exheader_path)
        .map_err(|error| ForgeError::io(format!("Cannot read {}", exheader_path.display()), error))?;
    let (code_size, code_digest) = sha256_file(code_path, cancel)?;
    if exheader.len() < 0x400 {
        return Err(invalid("ExHeader is truncated"));
    }
    let romfs_metadata = std::fs::metadata(romfs_image_path).ok().filter(std::fs::Metadata::is_file);
    let Some(romfs_metadata) = romfs_metadata else {
        return Err(invalid(format!("RomFS image does not exist: {}", path_string(romfs_image_path)?)));
    };
    let romfs_image_size = romfs_metadata.len();
    if romfs_image_size == 0 {
        return Err(invalid("RomFS image is empty"));
    }
    let (romfs_service_offset, romfs_service_size) = romfs_service_window(romfs_image_path, romfs_image_size)?;

    let text = segment(&exheader, 0x10)?;
    let ro = segment(&exheader, 0x20)?;
    let data = segment(&exheader, 0x30)?;
    for (name, item) in [("text", &text), ("ro", &ro), ("data", &data)] {
        validate_segment(name, item)?;
    }

    let text_pages = text.physical_pages * PAGE_SIZE;
    let ro_pages = ro.physical_pages * PAGE_SIZE;
    let data_pages = data.physical_pages * PAGE_SIZE;
    let initialized_size = text_pages + ro_pages + data_pages;
    if code_size != initialized_size {
        return Err(invalid(format!(
            "decompressed code size {code_size:#x} does not match ExHeader page total {initialized_size:#x}"
        )));
    }
    if text.address + text_pages > ro.address {
        return Err(invalid("text and ro virtual ranges overlap"));
    }
    if ro.address + ro_pages > data.address {
        return Err(invalid("ro and data virtual ranges overlap"));
    }

    let stack_size = u32_at(&exheader, 0x1C)?;
    let bss_size = u32_at(&exheader, 0x3C)?;
    if stack_size == 0 || !stack_size.is_multiple_of(PAGE_SIZE) {
        return Err(invalid("main stack size is zero or not page aligned"));
    }
    let bss_pages = align_up(bss_size, PAGE_SIZE);
    let data_mapped_size = data_pages + bss_pages;
    // Python integers do not wrap; the stack sits below 0x10000000 and its
    // size is a u32, so the subtraction is evaluated in signed arithmetic.
    if i128::from(data.address + data_mapped_size) >= i128::from(HEAP_VADDR_END) - i128::from(stack_size) {
        return Err(invalid("process image collides with the main stack"));
    }

    let name_field = &exheader[0..8];
    let name_end = name_field.iter().position(|&byte| byte == 0).unwrap_or(name_field.len());
    let process_name = ascii_name(&name_field[..name_end])?;

    let segments = json!([
        {"name": "text", "address": text.address, "mapped_size": text_pages,
         "declared_code_size": text.code_size, "file_offset": 0, "file_size": text_pages,
         "writable": false, "executable": true},
        {"name": "rodata", "address": ro.address, "mapped_size": ro_pages,
         "declared_code_size": ro.code_size, "file_offset": text_pages, "file_size": ro_pages,
         "writable": false, "executable": false},
        {"name": "data_bss", "address": data.address, "mapped_size": data_mapped_size,
         "declared_code_size": data.code_size, "file_offset": text_pages + ro_pages, "file_size": data_pages,
         "writable": true, "executable": false},
    ]);
    let (_, app_memory, _, _) = ctr_memory_layout(&exheader)?;
    let initial_commit = text_pages + ro_pages + data_mapped_size + stack_size;
    let system_regions = ctr_system_regions(&exheader)?;

    Ok(json!({
        "format": FORMAT,
        "source": {
            "exheader_path": path_string(&resolve(exheader_path)?)?,
            "exheader_sha256": forge_base::hashing::sha256_hex(&exheader),
            "code_bin_path": path_string(&resolve(code_path)?)?,
            "code_bin_sha256": hex(&code_digest),
            "code_bin_size": code_size,
            "romfs_image_path": path_string(&resolve(romfs_image_path)?)?,
            "romfs_image_file_size": romfs_image_size,
            "romfs_service_offset": romfs_service_offset,
            "romfs_service_size": romfs_service_size,
            "layout_authority": LAYOUT_AUTHORITY,
        },
        "process": {
            "name": process_name,
            "entrypoint": text.address,
            "page_size": PAGE_SIZE,
            "bss_declared_size": bss_size,
            "bss_mapped_size": bss_pages,
            "segments": segments,
            "system_regions": system_regions,
            "resource_limit": {
                "limit_values": [0x18, app_memory, 0x20, 0x20, 0x20, 0x08, 0x08, 0x10, 0x02, 0],
                "initial_values": [0, initial_commit, 1, 0, 0, 0, 0, 0, 0, 0],
            },
            "linear_heap": {"base_address": LEGACY_LINEAR_HEAP_VADDR, "size": app_memory},
            "heap": {"base_address": HEAP_VADDR, "size": HEAP_SIZE},
        },
        "primary_thread": {
            "stack_base_address": HEAP_VADDR_END - stack_size,
            "stack_size": stack_size,
            "tls_base_address": TLS_AREA_VADDR,
            "tls_size": TLS_PAGE_SIZE,
            "thread_pointer": TLS_AREA_VADDR,
            "argument0": 0,
            "initial_cpsr": USER32_CPSR,
            "initial_fpscr": CTR_MAIN_FPSCR,
            "priority": exheader[0x20F],
        },
    }))
}

#[cfg(test)]
pub(crate) mod fixtures {
    /// A valid synthetic ExHeader: three one-page segments at 0x100000.
    pub fn exheader(memory_mode: u8) -> Vec<u8> {
        let mut bytes = vec![0u8; 0x400];
        bytes[0..7].copy_from_slice(b"fixture");
        let words: [(usize, u32); 11] = [
            (0x10, 0x0010_0000),
            (0x14, 1),
            (0x18, 0x0000_0800),
            (0x1C, 0x0000_4000),
            (0x20, 0x0010_1000),
            (0x24, 1),
            (0x28, 0x0000_0400),
            (0x30, 0x0010_2000),
            (0x34, 1),
            (0x38, 0x0000_0200),
            (0x3C, 0x0000_1234),
        ];
        for (offset, value) in words {
            bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        bytes[0x20D] = memory_mode;
        bytes[0x20F] = 0x30;
        bytes
    }

    pub fn code() -> Vec<u8> {
        (0..0x3000u32).map(|index| (index * 7 % 251) as u8).collect()
    }

    pub fn raw_romfs() -> Vec<u8> {
        let mut bytes = b"IVFC".to_vec();
        bytes.resize(0x1000, 0);
        bytes.extend_from_slice(&0x28u32.to_le_bytes());
        bytes.extend_from_slice(&[0x5A; 0x40]);
        bytes
    }
}

#[cfg(test)]
mod tests {
    use super::fixtures::*;
    use super::*;

    struct Inputs {
        _directory: tempfile::TempDir,
        exheader: std::path::PathBuf,
        code: std::path::PathBuf,
        romfs: std::path::PathBuf,
    }

    fn inputs(exheader: &[u8], code: &[u8], romfs: &[u8]) -> Inputs {
        let directory = tempfile::tempdir().unwrap();
        let paths = ["exheader.bin", "code.bin", "romfs.bin"].map(|name| directory.path().join(name));
        for (path, bytes) in paths.iter().zip([exheader, code, romfs]) {
            std::fs::write(path, bytes).unwrap();
        }
        let [exheader, code, romfs] = paths;
        Inputs { _directory: directory, exheader, code, romfs }
    }

    fn build(inputs: &Inputs) -> Result<Value> {
        build_manifest(&inputs.exheader, &inputs.code, &inputs.romfs, &CancelToken::new())
    }

    fn error(exheader: &[u8], code: &[u8], romfs: &[u8]) -> String {
        build(&inputs(exheader, code, romfs)).unwrap_err().message
    }

    #[test]
    fn derives_layout_from_the_exheader() {
        let manifest = build(&inputs(&exheader(0), &code(), &raw_romfs())).unwrap();
        assert_eq!(manifest["process"]["name"], "fixture");
        assert_eq!(manifest["process"]["entrypoint"], 0x0010_0000);
        assert_eq!(manifest["process"]["bss_mapped_size"], 0x2000);
        assert_eq!(manifest["process"]["segments"][2]["mapped_size"], 0x3000);
        assert_eq!(manifest["process"]["linear_heap"]["size"], 0x0400_0000);
        assert_eq!(manifest["process"]["resource_limit"]["initial_values"][1], 0x3000 + 0x2000 + 0x4000);
        assert_eq!(manifest["primary_thread"]["stack_base_address"], 0x1000_0000 - 0x4000);
        assert_eq!(manifest["source"]["romfs_service_offset"], 0x1000);
        assert_eq!(manifest["source"]["romfs_service_size"], 0x44);
        let level3 = build(&inputs(&exheader(7), &code(), &raw_romfs()[0x1000..])).unwrap();
        assert_eq!(level3["source"]["romfs_service_offset"], 0);
        assert_eq!(level3["process"]["linear_heap"]["size"], 0x0B20_0000);
    }

    #[test]
    fn rejects_malformed_inputs_with_python_messages() {
        let romfs = raw_romfs();
        assert_eq!(error(&exheader(0)[..0x3FF], &code(), &romfs), "ExHeader is truncated");
        assert_eq!(error(&exheader(0), &code(), b""), "RomFS image is empty");
        assert_eq!(
            error(&exheader(0), &code(), &romfs[..0x1000]),
            "raw NCCH RomFS is smaller than its IVFC superblock"
        );
        assert_eq!(
            error(&exheader(0), &code(), b"IVFC-and-nothing-at-0x1000-padded-out-to-4096-bytes-is-missing"),
            "raw NCCH RomFS is smaller than its IVFC superblock"
        );
        assert_eq!(
            error(&exheader(0), &code(), b"NOPE"),
            "RomFS image is neither raw NCCH IVFC nor a Level-3 service view"
        );
        assert_eq!(
            error(&exheader(0), &code()[..0x2000], &romfs),
            "decompressed code size 0x2000 does not match ExHeader page total 0x3000"
        );
        assert_eq!(error(&exheader(1), &code(), &romfs), "unused CTR memory mode 1");
        assert_eq!(error(&exheader(9), &code(), &romfs), "unsupported CTR memory mode 9");
        let mut misaligned = exheader(0);
        misaligned[0x10] = 1;
        assert_eq!(error(&misaligned, &code(), &romfs), "text address is not page aligned");
        let mut overlap = exheader(0);
        overlap[0x21..0x24].copy_from_slice(&[0x00, 0x10, 0x00]);
        assert_eq!(error(&overlap, &code(), &romfs), "text and ro virtual ranges overlap");
        let mut stack = exheader(0);
        stack[0x1C] = 1;
        assert_eq!(error(&stack, &code(), &romfs), "main stack size is zero or not page aligned");
        let mut name = exheader(0);
        name[2] = 0xFF;
        assert_eq!(
            error(&name, &code(), &romfs),
            "'ascii' codec can't decode byte 0xff in position 2: ordinal not in range(128)"
        );
    }
}
