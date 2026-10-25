// Copyright Citra Emulator Project / Azahar Emulator Project
// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! NCSD/NCCH title layout discovery (`ctr_rom._find_title_layout` and
//! `_parse_ncch`). Offsets are u64 so no media-unit product can overflow.

use std::io::{Read, Seek, SeekFrom};

use forge_base::hashing::CHUNK_BYTES;
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};

use crate::{encrypted_error, rom_error, ContainerKind, EXHEADER_SIZE, MEDIA_UNIT};

const NCSD_HEADER_SIZE: u64 = 0x200;
const NCCH_HEADER_SIZE: u64 = 0x200;
const EXEFS_HEADER_SIZE: u64 = 0x200;
const EXEFS_SECTION_COUNT: usize = 8;
const NCSD_PARTITION_COUNT: u32 = 8;

/// `ctr_rom._NcchLayout`: absolute file offsets of the selected partition.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TitleLayout {
    pub container_kind: ContainerKind,
    pub partition_index: u32,
    pub partition_base: u64,
    pub partition_end: u64,
    pub program_id: u64,
    pub compressed_code: bool,
    pub code_offset: u64,
    pub code_size: u64,
    pub exheader_offset: u64,
    pub romfs_offset: u64,
    pub romfs_size: u64,
}

/// A partition that fails with `Rom` is skipped while scanning an NCSD
/// table (Python catches only `CtrRomError` there); `Fatal` I/O errors and
/// cancellation propagate.
enum Failure {
    Rom(ForgeError),
    Fatal(ForgeError),
}

type Parse<T> = std::result::Result<T, Failure>;

impl Failure {
    fn into_error(self) -> ForgeError {
        match self {
            Self::Rom(error) | Self::Fatal(error) => error,
        }
    }
}

fn u32_at(data: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(data[offset..offset + 4].try_into().unwrap())
}

pub(crate) fn u64_at(data: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(data[offset..offset + 8].try_into().unwrap())
}

fn checked_region(offset: u64, size: u64, lower: u64, upper: u64, label: &str) -> Parse<u64> {
    if offset < lower || size == 0 || offset > upper || size > upper - offset {
        return Err(Failure::Rom(rom_error(format!("{label} lies outside the selected title partition"))));
    }
    Ok(offset + size)
}

fn read_at<R: Read + Seek>(
    stream: &mut R,
    offset: u64,
    size: u64,
    label: &str,
    cancel: &CancelToken,
) -> Parse<Vec<u8>> {
    let truncated = || Failure::Rom(rom_error(format!("the ROM is truncated while reading {label}")));
    let fatal = |error| Failure::Fatal(ForgeError::io(format!("Cannot read {label}"), error));
    let size = usize::try_from(size).map_err(|_| truncated())?;
    stream.seek(SeekFrom::Start(offset)).map_err(fatal)?;
    let mut data = Vec::new();
    data.try_reserve_exact(size).map_err(|_| {
        Failure::Fatal(ForgeError::new(ErrorKind::PreparationFailed, format!("Not enough memory to read {label}")))
    })?;
    data.resize(size, 0);
    let mut filled = 0;
    while filled < size {
        cancel.check().map_err(Failure::Fatal)?;
        let end = size.min(filled + CHUNK_BYTES);
        match stream.read(&mut data[filled..end]) {
            Ok(0) => return Err(truncated()),
            Ok(read) => filled += read,
            Err(error) if error.kind() == std::io::ErrorKind::Interrupted => {}
            Err(error) => return Err(fatal(error)),
        }
    }
    Ok(data)
}

/// `ctr_rom._read_exact` for callers outside the partition scan.
pub(crate) fn read_exact<R: Read + Seek>(
    stream: &mut R,
    offset: u64,
    size: u64,
    label: &str,
    cancel: &CancelToken,
) -> Result<Vec<u8>> {
    read_at(stream, offset, size, label, cancel).map_err(Failure::into_error)
}

struct Partition {
    base: u64,
    size: u64,
    index: u32,
    kind: ContainerKind,
}

fn parse_ncch<R: Read + Seek>(
    stream: &mut R,
    file_size: u64,
    partition: &Partition,
    cancel: &CancelToken,
) -> Parse<TitleLayout> {
    let index = partition.index;
    let base = partition.base;
    let end = base + partition.size;
    let header_label = format!("partition {index} NCCH header");
    checked_region(base, NCCH_HEADER_SIZE, 0, file_size, &header_label)?;
    if end > file_size {
        return Err(Failure::Rom(rom_error(format!("partition {index} exceeds the ROM size"))));
    }

    let header = read_at(stream, base, NCCH_HEADER_SIZE, &header_label, cancel)?;
    if &header[0x100..0x104] != b"NCCH" {
        return Err(Failure::Rom(rom_error(format!("partition {index} is not an NCCH title"))));
    }
    if header[0x18D] & 0x02 == 0 {
        return Err(Failure::Rom(rom_error(format!("partition {index} is not executable"))));
    }
    if u32_at(&header, 0x180) == 0 {
        return Err(Failure::Rom(rom_error(format!("partition {index} has no ExHeader"))));
    }

    let exheader_offset = base + NCCH_HEADER_SIZE;
    checked_region(exheader_offset, EXHEADER_SIZE, base, end, "ExHeader")?;
    let exheader = read_at(stream, exheader_offset, EXHEADER_SIZE, "ExHeader", cancel)?;

    let exefs_units = u64::from(u32_at(&header, 0x1A0));
    let exefs_size_units = u64::from(u32_at(&header, 0x1A4));
    if exefs_units == 0 || exefs_size_units == 0 {
        return Err(Failure::Rom(rom_error(format!("partition {index} has no ExeFS"))));
    }
    let exefs_offset = base + exefs_units * MEDIA_UNIT;
    let exefs_end = checked_region(exefs_offset, exefs_size_units * MEDIA_UNIT, base, end, "ExeFS")?;
    let exefs_header = read_at(stream, exefs_offset, EXEFS_HEADER_SIZE, "ExeFS", cancel)?;

    let mut code = None;
    for section in 0..EXEFS_SECTION_COUNT {
        let entry = section * 0x10;
        let raw_name = &exefs_header[entry..entry + 8];
        let name = raw_name.split(|byte| *byte == 0).next().unwrap_or(raw_name);
        if name != b".code" {
            continue;
        }
        let code_offset = exefs_offset + EXEFS_HEADER_SIZE + u64::from(u32_at(&exefs_header, entry + 8));
        let code_size = u64::from(u32_at(&exefs_header, entry + 12));
        checked_region(code_offset, code_size, exefs_offset + EXEFS_HEADER_SIZE, exefs_end, "ExeFS .code")?;
        code = Some((code_offset, code_size));
        break;
    }
    let Some((code_offset, code_size)) = code else {
        return Err(Failure::Rom(encrypted_error(format!(
            "partition {index} has no readable ExeFS .code section; the ROM may still be encrypted"
        ))));
    };

    let romfs_units = u64::from(u32_at(&header, 0x1B0));
    let romfs_size_units = u64::from(u32_at(&header, 0x1B4));
    if romfs_units == 0 || romfs_size_units == 0 {
        return Err(Failure::Rom(rom_error(format!("partition {index} has no RomFS"))));
    }
    let romfs_offset = base + romfs_units * MEDIA_UNIT;
    let romfs_size = romfs_size_units * MEDIA_UNIT;
    checked_region(romfs_offset, romfs_size, base, end, "RomFS")?;
    if read_at(stream, romfs_offset, 4, "RomFS header", cancel)? != b"IVFC" {
        return Err(Failure::Rom(encrypted_error(
            "the selected title RomFS is not decrypted or has an invalid IVFC header",
        )));
    }

    Ok(TitleLayout {
        container_kind: partition.kind,
        partition_index: index,
        partition_base: base,
        partition_end: end,
        program_id: u64_at(&header, 0x118),
        compressed_code: exheader[0x0D] & 1 != 0,
        code_offset,
        code_size,
        exheader_offset,
        romfs_offset,
        romfs_size,
    })
}

/// Finds the first usable application partition of an NCSD image, or the
/// single partition of a bare NCCH image. `file_size` is the source length.
///
/// Error families (Python raised one `CtrRomError` class): a missing `.code`
/// entry or a non-IVFC RomFS is `EncryptedInput`; a wrong magic is
/// `UnsupportedFormat`; the NCSD summary is `EncryptedInput` when any
/// partition failed that way, otherwise `InvalidInput`.
pub fn find_title_layout<R: Read + Seek>(stream: &mut R, file_size: u64, cancel: &CancelToken) -> Result<TitleLayout> {
    let header = read_exact(stream, 0, NCSD_HEADER_SIZE, "container header", cancel)?;
    let magic = &header[0x100..0x104];
    if magic == b"NCCH" {
        let partition = Partition { base: 0, size: file_size, index: 0, kind: ContainerKind::Ncch };
        return parse_ncch(stream, file_size, &partition, cancel).map_err(Failure::into_error);
    }
    if magic != b"NCSD" {
        return Err(ForgeError::new(
            ErrorKind::UnsupportedFormat,
            "the selected file is not a Nintendo 3DS NCSD/NCCH image",
        ));
    }

    let mut failures = Vec::new();
    let mut encrypted = false;
    for index in 0..NCSD_PARTITION_COUNT {
        let entry = 0x120 + index as usize * 8;
        let base = u64::from(u32_at(&header, entry)) * MEDIA_UNIT;
        let size = u64::from(u32_at(&header, entry + 4)) * MEDIA_UNIT;
        if base == 0 || size == 0 {
            continue;
        }
        let partition = Partition { base, size, index, kind: ContainerKind::Ncsd };
        match parse_ncch(stream, file_size, &partition, cancel) {
            Ok(layout) => return Ok(layout),
            Err(Failure::Rom(error)) => {
                encrypted |= error.kind == ErrorKind::EncryptedInput;
                failures.push(error.message);
            }
            Err(Failure::Fatal(error)) => return Err(error),
        }
    }

    let detail = if failures.is_empty() { "the partition table is empty".to_owned() } else { failures.join("; ") };
    let message = format!("no usable decrypted application partition was found: {detail}");
    Err(if encrypted { encrypted_error(message) } else { rom_error(message) })
}
