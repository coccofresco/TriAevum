// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! O3PS portable shader packs (`merge_shader_packs.py`; native owner
//! `fast/extension/pica_aot_shader_pack.cpp`): a `<8s4I>` header (magic
//! `O3PSAOT\0`, version 1, descriptor schema, entry count, reserved 0) and
//! 56-byte `<IIQQQIIQQ>` entries pointing at SPIR-V payloads. Packs are read
//! from disk in bounded chunks; a corpus can exceed 2 GiB.

use std::collections::HashSet;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

use forge_base::{ErrorKind, ForgeError, Result};

use crate::pyerr::{io_failure, open_error, os_error};

pub const MAGIC: &[u8; 8] = b"O3PSAOT\0";
pub const HEADER_BYTES: u64 = 24;
pub const ENTRY_BYTES: u64 = 56;
/// The runtime's kMaximumPackBytes.
pub const MAX_BYTES: u64 = 4 * 1024 * 1024 * 1024;
const SPIRV_MAGIC: [u8; 4] = [0x03, 0x02, 0x23, 0x07];

fn invalid(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::ArtifactIntegrity, message)
}

fn open(path: &Path) -> Result<File> {
    File::open(path).map_err(|error| {
        let message = open_error(&error, path);
        io_failure(error, message)
    })
}

fn read_error(error: std::io::Error) -> ForgeError {
    let message = os_error(&error, None);
    io_failure(error, message)
}

fn u32_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().expect("4 bytes"))
}

fn u64_at(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().expect("8 bytes"))
}

struct Header {
    magic: [u8; 8],
    version: u32,
    schema: u32,
    count: u32,
    reserved: u32,
}

fn read_header(file: &mut File) -> Result<Header> {
    let mut bytes = [0u8; HEADER_BYTES as usize];
    file.read_exact(&mut bytes).map_err(read_error)?;
    Ok(Header {
        magic: bytes[..8].try_into().expect("8 bytes"),
        version: u32_at(&bytes, 8),
        schema: u32_at(&bytes, 12),
        count: u32_at(&bytes, 16),
        reserved: u32_at(&bytes, 20),
    })
}

/// `shader_preparation._pack_header(path, schema)`: the runtime preparer's
/// header/schema/table-fit check; returns the module count.
pub fn pack_header(path: &Path, schema: i128) -> Result<u32> {
    let size = std::fs::metadata(path)
        .map_err(|error| {
            let message = os_error(&error, Some(path));
            io_failure(error, message)
        })?
        .len();
    if !(HEADER_BYTES..=MAX_BYTES).contains(&size) {
        return Err(invalid("Invalid portable shader pack size"));
    }
    let header = read_header(&mut open(path)?)?;
    if &header.magic != MAGIC
        || header.version != 1
        || i128::from(header.schema) != schema
        || header.count == 0
        || header.reserved != 0
        || HEADER_BYTES + u64::from(header.count) * ENTRY_BYTES > size
    {
        return Err(invalid("Portable shader pack has incompatible header/schema"));
    }
    Ok(header.count)
}

/// One table entry: `(stage, primary, secondary, size)` and its payload.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Module {
    pub key: (u32, u64, u64, u64),
    pub offset: u64,
    pub end: u64,
    pub digest: u64,
}

/// The renderer's FNV-1a over a payload (`binary_hash`): a zero digest
/// is spelled 1.
pub struct BinaryHash(u64);

impl BinaryHash {
    pub fn new() -> Self {
        Self(1_469_598_103_934_665_603)
    }

    pub fn update(&mut self, bytes: &[u8]) {
        for byte in bytes {
            self.0 = (self.0 ^ u64::from(*byte)).wrapping_mul(1_099_511_628_211);
        }
    }

    pub fn finish(&self) -> u64 {
        if self.0 == 0 {
            1
        } else {
            self.0
        }
    }
}

impl Default for BinaryHash {
    fn default() -> Self {
        Self::new()
    }
}

/// `decode(data, canonical=...)` without holding the pack in memory: the
/// descriptor schema and the table, after every structural and payload
/// check Python made, in Python's order (entries, payload hashes, overlap,
/// canonical layout).
pub fn decode_file(path: &Path, canonical: bool) -> Result<(u32, Vec<Module>)> {
    let mut file = open(path)?;
    let size = file.metadata().map_err(read_error)?.len();
    if !(HEADER_BYTES..=MAX_BYTES).contains(&size) {
        return Err(invalid("Invalid portable pack size"));
    }
    let header = read_header(&mut file)?;
    let table_end = HEADER_BYTES + u64::from(header.count) * ENTRY_BYTES;
    if &header.magic != MAGIC
        || header.version != 1
        || header.schema == 0
        || header.count == 0
        || header.reserved != 0
        || table_end > size
    {
        return Err(invalid("Invalid portable pack header"));
    }
    let mut table = vec![0u8; (table_end - HEADER_BYTES) as usize];
    file.read_exact(&mut table).map_err(read_error)?;
    let mut modules = Vec::with_capacity(header.count as usize);
    let mut keys = HashSet::new();
    for index in 0..header.count as usize {
        let entry = &table[index * ENTRY_BYTES as usize..(index + 1) * ENTRY_BYTES as usize];
        let (stage, pad, primary, secondary, source_size) =
            (u32_at(entry, 0), u32_at(entry, 4), u64_at(entry, 8), u64_at(entry, 16), u64_at(entry, 24));
        let (words, pad2, offset, digest) =
            (u32_at(entry, 32), u32_at(entry, 36), u64_at(entry, 40), u64_at(entry, 48));
        let end = u128::from(offset) + u128::from(words) * 4;
        let key = (stage, primary, secondary, source_size);
        if !(1..=3).contains(&stage)
            || pad != 0
            || pad2 != 0
            || primary == 0
            || secondary == 0
            || source_size == 0
            || words == 0
            || offset < table_end
            || offset % 4 != 0
            || end > u128::from(size)
            || keys.contains(&key)
        {
            return Err(invalid("Invalid portable pack entry"));
        }
        let mut magic = [0u8; 4];
        file.seek(SeekFrom::Start(offset)).map_err(read_error)?;
        file.read_exact(&mut magic).map_err(read_error)?;
        if magic != SPIRV_MAGIC {
            return Err(invalid("Corrupt SPIR-V payload"));
        }
        keys.insert(key);
        modules.push(Module { key, offset, end: end as u64, digest });
    }
    let mut buffer = vec![0u8; 4 * 1024 * 1024];
    for module in &modules {
        file.seek(SeekFrom::Start(module.offset)).map_err(read_error)?;
        let mut hash = BinaryHash::new();
        let mut remaining = module.end - module.offset;
        while remaining > 0 {
            let want = remaining.min(buffer.len() as u64) as usize;
            file.read_exact(&mut buffer[..want]).map_err(read_error)?;
            hash.update(&buffer[..want]);
            remaining -= want as u64;
        }
        if hash.finish() != module.digest {
            return Err(invalid("Corrupt SPIR-V payload"));
        }
    }
    let mut ranges: Vec<(u64, u64)> = modules.iter().map(|module| (module.offset, module.end)).collect();
    ranges.sort_unstable();
    let mut previous = table_end;
    for (start, end) in ranges {
        if start < previous {
            return Err(invalid("Overlapping SPIR-V payloads"));
        }
        previous = end;
    }
    if canonical {
        let mut expected = table_end;
        for module in &modules {
            if module.offset != expected {
                return Err(invalid("Portable pack is not canonical"));
            }
            expected = module.end;
        }
        if expected != size || modules.windows(2).any(|pair| pair[0].key >= pair[1].key) {
            return Err(invalid("Portable pack is not canonical"));
        }
    }
    Ok((header.schema, modules))
}
