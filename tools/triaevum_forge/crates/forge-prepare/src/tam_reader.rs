// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The container checks of `runtime/triaevum_module/src/tam_reader.cpp`
//! (`ParseTam`), with the same error codes and messages, so Forge can verify
//! a TAM the way the runtime will before publishing it. Metadata semantics
//! (`tam_metadata.cpp`) are not repeated here.

use sha2::{Digest, Sha256};

use crate::tam::{HEADER_SIZE, MAGIC, SECTION_ALIGNMENT, SECTION_ENTRY_SIZE};

pub const FORMAT_MAJOR: u16 = 1;
pub const FORMAT_MINOR: u16 = 1;
pub const MAXIMUM_SECTIONS: u32 = 32;
pub const MAXIMUM_METADATA_BYTES: u64 = 1024 * 1024;
const REQUIRED: u32 = 1 << 0;
const EXECUTABLE: u32 = 1 << 1;
const COMPRESSED: u32 = 1 << 2;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TamErrorCode {
    Truncated,
    InvalidMagic,
    UnsupportedFormat,
    IncompatibleRuntimeAbi,
    InvalidHeader,
    InvalidSectionTable,
    InvalidSection,
    UnsupportedSection,
    MissingRequiredSection,
    DuplicateSection,
    HashMismatch,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TamError {
    pub code: TamErrorCode,
    pub message: &'static str,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TamSectionType {
    MetadataJson = 1,
    NativeImage = 2,
    TitleAotImage = 3,
}

#[derive(Clone, Debug)]
pub struct TamSectionView<'a> {
    pub kind: TamSectionType,
    pub flags: u32,
    pub bytes: &'a [u8],
    pub sha256: [u8; 32],
}

#[derive(Clone, Debug)]
pub struct TamView<'a> {
    pub format_minor: u16,
    pub runtime_abi: u32,
    pub sections: Vec<TamSectionView<'a>>,
}

impl<'a> TamView<'a> {
    pub fn find(&self, kind: TamSectionType) -> Option<&TamSectionView<'a>> {
        self.sections.iter().find(|section| section.kind == kind)
    }
}

fn read16(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes([bytes[offset], bytes[offset + 1]])
}

fn read32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().expect("four bytes"))
}

fn read64(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().expect("eight bytes"))
}

fn is_zero(bytes: &[u8]) -> bool {
    bytes.iter().all(|&byte| byte == 0)
}

fn fail<T>(code: TamErrorCode, message: &'static str) -> Result<T, TamError> {
    Err(TamError { code, message })
}

/// `ParseTam(file, expectedRuntimeAbi)`.
pub fn parse_tam(file: &[u8], expected_runtime_abi: u32) -> Result<TamView<'_>, TamError> {
    use TamErrorCode::*;
    let size = file.len() as u64;
    if size < HEADER_SIZE {
        return fail(Truncated, "TAM header is truncated");
    }
    if &file[..8] != MAGIC {
        return fail(InvalidMagic, "TAM magic is invalid");
    }
    let format_major = read16(file, 8);
    let format_minor = read16(file, 10);
    let header_size = read16(file, 12);
    let entry_size = read16(file, 14);
    let runtime_abi = read32(file, 16);
    let section_count = read32(file, 20);
    let declared_size = read64(file, 24);
    let table_offset = read64(file, 32);

    if format_major != FORMAT_MAJOR || format_minor > FORMAT_MINOR {
        return fail(UnsupportedFormat, "TAM format version is not supported");
    }
    if u64::from(header_size) != HEADER_SIZE
        || u64::from(entry_size) != SECTION_ENTRY_SIZE
        || table_offset != HEADER_SIZE
        || !is_zero(&file[40..64])
    {
        return fail(InvalidHeader, "TAM canonical header fields are invalid");
    }
    if runtime_abi != expected_runtime_abi {
        return fail(IncompatibleRuntimeAbi, "TAM runtime ABI does not match the host");
    }
    if declared_size != size {
        return fail(InvalidHeader, "TAM declared file size does not match the input");
    }
    if section_count == 0 || section_count > MAXIMUM_SECTIONS {
        return fail(InvalidSectionTable, "TAM section count is outside the supported range");
    }
    let table_bytes = u64::from(section_count) * SECTION_ENTRY_SIZE;
    if table_offset > size || table_bytes > size - table_offset {
        return fail(Truncated, "TAM section table is truncated");
    }
    let data_begin = (table_offset + table_bytes + SECTION_ALIGNMENT - 1) & !(SECTION_ALIGNMENT - 1);
    if data_begin > size {
        return fail(Truncated, "TAM aligned section data is truncated");
    }
    let table_end = (table_offset + table_bytes) as usize;
    if !is_zero(&file[table_end..data_begin as usize]) {
        return fail(InvalidSectionTable, "TAM section-table padding is not zero");
    }

    let mut view = TamView { format_minor, runtime_abi, sections: Vec::with_capacity(section_count as usize) };
    let (mut has_metadata, mut has_native, mut has_title_aot) = (false, false, false);
    let mut previous_end = data_begin;
    for index in 0..section_count as usize {
        let descriptor = table_offset as usize + index * SECTION_ENTRY_SIZE as usize;
        let raw_type = read32(file, descriptor);
        let flags = read32(file, descriptor + 4);
        let offset = read64(file, descriptor + 8);
        let length = read64(file, descriptor + 16);
        let expected: [u8; 32] = file[descriptor + 24..descriptor + 56].try_into().expect("32 bytes");
        if !is_zero(&file[descriptor + 56..descriptor + 64])
            || flags & !(REQUIRED | EXECUTABLE | COMPRESSED) != 0
            || flags & COMPRESSED != 0
        {
            return fail(InvalidSection, "TAM section uses unsupported fields or flags");
        }
        if length == 0
            || offset < data_begin
            || !offset.is_multiple_of(SECTION_ALIGNMENT)
            || offset > size
            || length > size - offset
            || offset < previous_end
        {
            return fail(InvalidSection, "TAM section range or ordering is invalid");
        }
        if !is_zero(&file[previous_end as usize..offset as usize]) {
            return fail(InvalidSection, "TAM inter-section padding is not zero");
        }
        let bytes = &file[offset as usize..(offset + length) as usize];
        if <[u8; 32]>::from(Sha256::digest(bytes)) != expected {
            return fail(HashMismatch, "TAM section SHA-256 does not match");
        }
        let kind = match raw_type {
            1 => {
                if has_metadata {
                    return fail(DuplicateSection, "TAM has duplicate metadata sections");
                }
                if length > MAXIMUM_METADATA_BYTES || flags != REQUIRED {
                    return fail(InvalidSection, "TAM metadata section contract is invalid");
                }
                has_metadata = true;
                TamSectionType::MetadataJson
            }
            2 => {
                if has_native {
                    return fail(DuplicateSection, "TAM has duplicate native-image sections");
                }
                if flags != REQUIRED | EXECUTABLE {
                    return fail(InvalidSection, "TAM native-image section contract is invalid");
                }
                has_native = true;
                TamSectionType::NativeImage
            }
            3 => {
                if has_title_aot {
                    return fail(DuplicateSection, "TAM has duplicate title-AOT sections");
                }
                if format_minor < 1 || flags != REQUIRED | EXECUTABLE {
                    return fail(InvalidSection, "TAM title-AOT section contract is invalid");
                }
                has_title_aot = true;
                TamSectionType::TitleAotImage
            }
            _ if flags & REQUIRED != 0 => {
                return fail(UnsupportedSection, "TAM contains an unknown required section");
            }
            _ => {
                previous_end = offset + length;
                continue;
            }
        };
        view.sections.push(TamSectionView { kind, flags, bytes, sha256: expected });
        previous_end = offset + length;
    }
    if previous_end != size {
        return fail(InvalidSection, "TAM has unclaimed trailing bytes");
    }
    if !has_metadata || !has_native {
        return fail(MissingRequiredSection, "TAM is missing metadata or native image");
    }
    Ok(view)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// tam_reader_tests.cpp's BuildModule.
    fn build_module(with_title_aot: bool) -> Vec<u8> {
        let metadata = br#"{"format":"test"}"#.to_vec();
        let native = vec![b'M', b'Z', 1, 2, 3];
        let title = vec![b'M', b'Z', 4, 5, 6];
        let count = if with_title_aot { 3 } else { 2 };
        let align = |value: usize| (value + 15) & !15;
        let metadata_offset = align(64 + count * 64);
        let native_offset = align(metadata_offset + metadata.len());
        let title_offset = align(native_offset + native.len());
        let total = if with_title_aot { title_offset + title.len() } else { native_offset + native.len() };
        let mut bytes = vec![0u8; total];
        bytes[..8].copy_from_slice(MAGIC);
        bytes[8..10].copy_from_slice(&1u16.to_le_bytes());
        bytes[10..12].copy_from_slice(&u16::from(with_title_aot).to_le_bytes());
        bytes[12..14].copy_from_slice(&64u16.to_le_bytes());
        bytes[14..16].copy_from_slice(&64u16.to_le_bytes());
        bytes[16..20].copy_from_slice(&1u32.to_le_bytes());
        bytes[20..24].copy_from_slice(&(count as u32).to_le_bytes());
        bytes[24..32].copy_from_slice(&(total as u64).to_le_bytes());
        bytes[32..40].copy_from_slice(&64u64.to_le_bytes());
        let mut sections =
            vec![(1u32, REQUIRED, metadata_offset, metadata), (2, REQUIRED | EXECUTABLE, native_offset, native)];
        if with_title_aot {
            sections.push((3, REQUIRED | EXECUTABLE, title_offset, title));
        }
        for (index, (kind, flags, offset, payload)) in sections.into_iter().enumerate() {
            let descriptor = 64 + index * 64;
            bytes[descriptor..descriptor + 4].copy_from_slice(&kind.to_le_bytes());
            bytes[descriptor + 4..descriptor + 8].copy_from_slice(&flags.to_le_bytes());
            bytes[descriptor + 8..descriptor + 16].copy_from_slice(&(offset as u64).to_le_bytes());
            bytes[descriptor + 16..descriptor + 24].copy_from_slice(&(payload.len() as u64).to_le_bytes());
            bytes[descriptor + 24..descriptor + 56].copy_from_slice(&Sha256::digest(&payload));
            bytes[offset..offset + payload.len()].copy_from_slice(&payload);
        }
        bytes
    }

    #[test]
    fn mirrors_the_cpp_reader_cases() {
        let mut bytes = build_module(false);
        let view = parse_tam(&bytes, 1).unwrap();
        assert_eq!(view.sections.len(), 2);
        assert!(view.find(TamSectionType::MetadataJson).is_some() && view.find(TamSectionType::NativeImage).is_some());
        let title = build_module(true);
        let view = parse_tam(&title, 1).unwrap();
        assert!(view.sections.len() == 3 && view.find(TamSectionType::TitleAotImage).is_some());
        assert_eq!(parse_tam(&bytes, 2).unwrap_err().code, TamErrorCode::IncompatibleRuntimeAbi);
        *bytes.last_mut().unwrap() ^= 0x80;
        assert_eq!(parse_tam(&bytes, 1).unwrap_err().code, TamErrorCode::HashMismatch);
        let mut trailing = build_module(false);
        trailing.push(0);
        let length = trailing.len() as u64;
        trailing[24..32].copy_from_slice(&length.to_le_bytes());
        assert_eq!(parse_tam(&trailing, 1).unwrap_err().message, "TAM has unclaimed trailing bytes");
    }

    #[test]
    fn rejects_noncanonical_headers_and_sections() {
        let base = build_module(true);
        let mutate = |offset: usize, value: u8| {
            let mut bytes = base.clone();
            bytes[offset] = value;
            parse_tam(&bytes, 1).unwrap_err()
        };
        assert_eq!(parse_tam(&base[..63], 1).unwrap_err().code, TamErrorCode::Truncated);
        assert_eq!(mutate(0, b'X').code, TamErrorCode::InvalidMagic);
        assert_eq!(mutate(10, 2).code, TamErrorCode::UnsupportedFormat);
        assert_eq!(mutate(40, 1).code, TamErrorCode::InvalidHeader);
        assert_eq!(mutate(20, 0).code, TamErrorCode::InvalidSectionTable);
        assert_eq!(mutate(64 + 4, 5).message, "TAM section uses unsupported fields or flags");
        assert_eq!(mutate(64 + 64, 1).code, TamErrorCode::DuplicateSection);
        assert_eq!(mutate(64 + 128, 9).code, TamErrorCode::UnsupportedSection);
        let mut minor_zero = base.clone();
        minor_zero[10] = 0;
        assert_eq!(parse_tam(&minor_zero, 1).unwrap_err().message, "TAM title-AOT section contract is invalid");
    }
}
