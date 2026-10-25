// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `romfs_identity.py`: bounded logical RomFS identity, independent of the
//! physical placement of files and of an IVFC wrapper. Directory traversal
//! and hash lookup must describe the same records (Azahar layered_fs.cpp).
//!
//! Identity: SHA-256 of `b"triaevum-romfs-tree-v1\0"` + compact ASCII JSON of
//! the sorted entries `["directory", path]` / `["file", path, length, sha256]`.

use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

use forge_base::hashing::{copy_hashing, hex, sha256_bytes};
use forge_base::legacy_json::{dumps, Options};
use forge_base::{CancelToken, ForgeError, Result};
use serde_json::{json, Value};

use crate::region_assets::{name_hash, INVALID};

const TREE_DOMAIN: &[u8] = b"triaevum-romfs-tree-v1\0";
const METADATA_LIMIT: u32 = 16 * 1024 * 1024;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RomfsFingerprint {
    pub sha256: String,
    pub files: u64,
    pub directories: u64,
    pub payload_bytes: u64,
}

impl RomfsFingerprint {
    /// The dict `fingerprint()` returned, in its key order.
    pub fn to_value(&self) -> Value {
        json!({"sha256": self.sha256, "files": self.files, "directories": self.directories,
               "payload_bytes": self.payload_bytes})
    }
}

fn invalid(message: &str) -> ForgeError {
    ForgeError::invalid(message)
}

fn u32_at(data: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(data[offset..offset + 4].try_into().expect("four bytes"))
}

fn u64_at(data: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(data[offset..offset + 8].try_into().expect("eight bytes"))
}

struct Image {
    stream: File,
    base: u64,
    size: i128,
}

impl Image {
    fn read(&mut self, offset: u64, length: u64) -> Result<Vec<u8>> {
        if i128::from(offset) + i128::from(length) > self.size {
            return Err(invalid("RomFS extent outside image"));
        }
        let mut data = vec![0u8; usize::try_from(length).map_err(|_| invalid("Truncated RomFS"))?];
        self.stream
            .seek(SeekFrom::Start(self.base + offset))
            .and_then(|_| self.stream.read_exact(&mut data))
            .map_err(|_| invalid("Truncated RomFS"))?;
        Ok(data)
    }
}

struct Record {
    parent: u32,
    name: Vec<u8>,
    chain: u32,
    path: String,
    payload: u64,
    length: u64,
}

fn record(table: &[u8], offset: u32, fixed: usize, extents: &mut Vec<(usize, usize)>) -> Result<(Vec<u8>, String)> {
    let offset = offset as usize;
    if !offset.is_multiple_of(4) || offset + fixed > table.len() {
        return Err(invalid("Invalid RomFS record"));
    }
    let length = u32_at(table, offset + fixed - 4) as usize;
    if !length.is_multiple_of(2) || offset + fixed + length > table.len() {
        return Err(invalid("Invalid RomFS name bounds"));
    }
    let encoded = table[offset + fixed..offset + fixed + length].to_vec();
    let units: Vec<u16> = encoded.chunks_exact(2).map(|unit| u16::from_le_bytes([unit[0], unit[1]])).collect();
    // Python's strict UTF-16LE decode rejects the same unpaired surrogates
    // (with a codec message instead of this one).
    let name = String::from_utf16(&units).map_err(|_| invalid("Invalid RomFS name"))?;
    if name.contains('/') || name.contains('\\') || name.contains('\0') || name == "." || name == ".." {
        return Err(invalid("Invalid RomFS name"));
    }
    extents.push((offset, (offset + fixed + length + 3) & !3));
    Ok((encoded, name))
}

/// Ordered records: insertion order plus lookup by record offset.
#[derive(Default)]
struct Records {
    rows: Vec<Record>,
    by_offset: HashMap<u32, usize>,
}

impl Records {
    fn contains(&self, offset: u32) -> bool {
        self.by_offset.contains_key(&offset)
    }

    fn insert(&mut self, offset: u32, row: Record) {
        self.by_offset.insert(offset, self.rows.len());
        self.rows.push(row);
    }

    fn get(&self, offset: u32) -> Option<&Record> {
        self.by_offset.get(&offset).map(|&index| &self.rows[index])
    }
}

/// Verifies the lookup table of one record kind against the traversal.
fn verify_lookup(image: &mut Image, records: &Records, offset: u32, length: u32, cancel: &CancelToken) -> Result<()> {
    if records.rows.iter().map(|row| row.path.as_str()).collect::<HashSet<_>>().len() != records.rows.len() {
        return Err(invalid("Duplicate RomFS path"));
    }
    let buckets = image.read(u64::from(offset), u64::from(length))?;
    let count = length / 4;
    let mut visited = HashSet::new();
    for bucket in 0..count {
        cancel.check()?;
        let mut link = u32_at(&buckets, bucket as usize * 4);
        while link != INVALID {
            let Some(row) = records.get(link).filter(|_| !visited.contains(&link)) else {
                return Err(invalid("Invalid RomFS hash chain"));
            };
            visited.insert(link);
            if name_hash(row.parent, &row.name) % count != bucket {
                return Err(invalid("Incorrect RomFS hash bucket"));
            }
            link = row.chain;
        }
    }
    if visited.len() != records.rows.len() {
        return Err(invalid("RomFS traversal and lookup differ"));
    }
    Ok(())
}

enum Entry {
    Directory(String),
    File(String, u64, String),
}

impl Entry {
    fn key(&self) -> (u8, &str) {
        match self {
            Self::Directory(path) => (0, path),
            Self::File(path, _, _) => (1, path),
        }
    }

    fn to_value(&self) -> Value {
        match self {
            Self::Directory(path) => json!(["directory", path]),
            Self::File(path, length, digest) => json!(["file", path, length, digest]),
        }
    }
}

/// Checks every structural rule and returns the logical identity of a
/// Level-3 RomFS (optionally IVFC wrapped).
pub fn fingerprint(path: &Path, cancel: &CancelToken) -> Result<RomfsFingerprint> {
    let open_error = |error| ForgeError::io(format!("Cannot read {}", path.display()), error);
    let mut stream = File::open(path).map_err(open_error)?;
    let file_size = stream.metadata().map_err(open_error)?.len();
    let mut magic = Vec::with_capacity(4);
    (&mut stream).take(4).read_to_end(&mut magic).map_err(open_error)?;
    let base = if magic == b"IVFC" { 0x1000 } else { 0 };
    let mut image = Image { stream, base, size: i128::from(file_size) - i128::from(base) };

    let header = image.read(0, 40)?;
    let h: Vec<u32> = (0..10).map(|i| u32_at(&header, i * 4)).collect();
    if h[0] != 40 {
        return Err(invalid("Invalid RomFS Level-3 header"));
    }
    let mut end = 40u64;
    for (offset, length) in [(h[1], h[2]), (h[3], h[4]), (h[5], h[6]), (h[7], h[8])] {
        if u64::from(offset) < end || offset % 4 != 0 || !(1..=METADATA_LIMIT).contains(&length) {
            return Err(invalid("Invalid RomFS metadata bounds"));
        }
        image.read(u64::from(offset), u64::from(length))?;
        end = u64::from(offset) + u64::from(length);
    }
    let data_offset = i128::from(h[9]);
    if !h[2].is_multiple_of(4)
        || !h[6].is_multiple_of(4)
        || u64::from(h[9]) < end
        || data_offset > image.size
        || !h[9].is_multiple_of(16)
    {
        return Err(invalid("Invalid RomFS data bounds"));
    }
    let dirs = image.read(u64::from(h[3]), u64::from(h[4]))?;
    let files = image.read(u64::from(h[7]), u64::from(h[8]))?;

    let (mut directory_records, mut file_records) = (Records::default(), Records::default());
    let (mut dir_extents, mut file_extents) = (Vec::new(), Vec::new());
    let mut pending: Vec<(u32, String, u32)> = vec![(0, String::new(), 0)];
    while let Some((offset, prefix, owner)) = pending.pop() {
        cancel.check()?;
        if directory_records.contains(offset) {
            return Err(invalid("Cyclic RomFS directory"));
        }
        let (encoded, name) = record(&dirs, offset, 24, &mut dir_extents)?;
        let at = offset as usize;
        let (parent, sibling, child) = (u32_at(&dirs, at), u32_at(&dirs, at + 4), u32_at(&dirs, at + 8));
        let (mut file, chain) = (u32_at(&dirs, at + 12), u32_at(&dirs, at + 16));
        if parent != owner
            || (offset == 0 && (!name.is_empty() || sibling != INVALID))
            || (offset != 0 && name.is_empty())
        {
            return Err(invalid("Invalid RomFS ancestry"));
        }
        let full = if prefix.is_empty() { name } else { format!("{prefix}/{name}") };
        directory_records
            .insert(offset, Record { parent, name: encoded, chain, path: full.clone(), payload: 0, length: 0 });
        if sibling != INVALID {
            pending.push((sibling, prefix, owner));
        }
        if child != INVALID {
            pending.push((child, full.clone(), offset));
        }
        while file != INVALID {
            if file_records.contains(file) {
                return Err(invalid("Cyclic RomFS file"));
            }
            let (encoded, text) = record(&files, file, 32, &mut file_extents)?;
            let at = file as usize;
            let (parent, following) = (u32_at(&files, at), u32_at(&files, at + 4));
            let (payload, length, chain) = (u64_at(&files, at + 8), u64_at(&files, at + 16), u32_at(&files, at + 24));
            if parent != offset
                || text.is_empty()
                || data_offset + i128::from(payload) + i128::from(length) > image.size
            {
                return Err(invalid("Invalid RomFS file bounds or ancestry"));
            }
            let path = if full.is_empty() { text } else { format!("{full}/{text}") };
            file_records.insert(file, Record { parent, name: encoded, chain, path, payload, length });
            file = following;
        }
    }
    for mut ranges in [dir_extents, file_extents] {
        ranges.sort_unstable();
        let mut end = 0;
        for (start, limit) in ranges {
            if start < end {
                return Err(invalid("Overlapping RomFS records"));
            }
            end = limit;
        }
    }
    verify_lookup(&mut image, &directory_records, h[1], h[2], cancel)?;
    verify_lookup(&mut image, &file_records, h[5], h[6], cancel)?;

    let mut payloads: Vec<(u64, u64)> =
        file_records.rows.iter().filter(|row| row.length != 0).map(|row| (row.payload, row.length)).collect();
    payloads.sort_unstable();
    let mut end = 0u128;
    for (start, length) in payloads {
        if u128::from(start) < end {
            return Err(invalid("Overlapping RomFS payloads"));
        }
        end = u128::from(start) + u128::from(length);
    }

    let mut entries: Vec<Entry> = directory_records.rows.iter().map(|row| Entry::Directory(row.path.clone())).collect();
    let mut total = 0u64;
    for row in &file_records.rows {
        image
            .stream
            .seek(SeekFrom::Start(base + u64::from(h[9]) + row.payload))
            .map_err(|_| invalid("Truncated RomFS file"))?;
        let (read, digest) = copy_hashing(&mut image.stream, &mut std::io::sink(), row.length, cancel)?;
        if read != row.length {
            return Err(invalid("Truncated RomFS file"));
        }
        entries.push(Entry::File(row.path.clone(), row.length, hex(&digest)));
        total += row.length;
    }
    entries.sort_by(|a, b| a.key().cmp(&b.key()));
    let canonical = dumps(&Value::Array(entries.iter().map(Entry::to_value).collect()), Options::COMPACT);
    let mut preimage = TREE_DOMAIN.to_vec();
    preimage.extend_from_slice(canonical.as_bytes());
    Ok(RomfsFingerprint {
        sha256: hex(&sha256_bytes(&preimage)),
        files: file_records.rows.len() as u64,
        directories: directory_records.rows.len() as u64,
        payload_bytes: total,
    })
}
