// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! `oot3d_region_assets.py`: offline USA resource service-view adaptation for
//! the canonical EUR module.
//!
//! File payloads remain those of the input ROM. Only same-size region and
//! language names, RomFS hash chains and QM language slot references change.
//! The output is a Level-3 service view, not an IVFC image with invalid
//! integrity hashes. RomFS hash semantics follow Azahar's layered_fs.cpp
//! (GPL-2.0-or-later).

use std::collections::{BTreeMap, HashSet};
use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;

use forge_base::{CancelToken, ForgeError, Result};
use serde_json::{json, Value};

use crate::paths::resolve;

pub const INVALID: u32 = 0xFFFF_FFFF;
pub const ALGORITHM: &str = "oot3d_usa_resources_to_eur_v2";
/// The EUR executable opens hint movie 181 + its language index (English 2,
/// French 4, Spanish 6); the USA ROM stores the same movies under 1, 5 and 7.
pub const HINT_MOVIES: [(&str, &str); 3] = [
    ("misc/hint/movie/hint182.moflex", "hint183.moflex"),
    ("misc/hint/movie/hint186.moflex", "hint185.moflex"),
    ("misc/hint/movie/hint188.moflex", "hint187.moflex"),
];
pub const FILE_RENAMES: [(&str, &str); 4] =
    [("message/us/us.qm", "eu.qm"), HINT_MOVIES[0], HINT_MOVIES[1], HINT_MOVIES[2]];
const RESOURCE_DIRECTORIES: [&str; 2] = ["message/us", "misc/us"];
const METADATA_LIMIT: u32 = 16 * 1024 * 1024;
const COPY_CHUNK: usize = 4 * 1024 * 1024;

/// The CTR RomFS lookup hash of an encoded (UTF-16LE) name under `parent`.
pub fn name_hash(parent: u32, encoded: &[u8]) -> u32 {
    encoded
        .chunks_exact(2)
        .fold(parent ^ 123_456_789, |value, unit| value.rotate_right(5) ^ u32::from(u16::from_le_bytes([unit[0], unit[1]])))
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

/// Copies the US English/French/Spanish message references of a QM v4 table
/// into the EUR slots. Payload bytes are not touched.
pub fn adapt_qm_table(data: &[u8], file_size: u64) -> Result<Vec<u8>> {
    if data.len() < 16 {
        return Err(invalid("truncated QM header"));
    }
    let (version, count, reserved) = (u32_at(data, 4), u32_at(data, 8), u32_at(data, 12));
    if &data[..4] != b"QM\0\0" || version != 4 || reserved != 0 || !(1..=100_000).contains(&count) {
        return Err(invalid("unsupported QM container"));
    }
    let table_end = 16 + u64::from(count) * 96;
    if data.len() as u64 != table_end || table_end > file_size {
        return Err(invalid("invalid QM record table"));
    }
    let mut output = data.to_vec();
    for index in 0..count as usize {
        let entry = 16 + index * 96;
        for slot in 0..9 {
            let offset = u64::from(u32_at(data, entry + 16 + slot * 8));
            let size = u64::from(u32_at(data, entry + 20 + slot * 8));
            if (offset != 0) != (size != 0) || (offset != 0 && (offset < table_end || offset + size > file_size)) {
                return Err(invalid("QM payload is outside file"));
            }
        }
        // Native language slots: US English 1 -> EU English 2,
        // US French 5 -> EU French 4, US Spanish 7 -> EU Spanish 6.
        for (destination, source) in [(2usize, 1usize), (4, 5), (6, 7)] {
            let start = entry + 16 + destination * 8;
            let original = &data[start..start + 8];
            let replacement = &data[entry + 16 + source * 8..entry + 24 + source * 8];
            if original != [0u8; 8] && original != replacement {
                return Err(invalid("QM destination language is already populated"));
            }
            output[start..start + 8].copy_from_slice(replacement);
        }
    }
    Ok(output)
}

struct Image {
    stream: File,
    base: u64,
    size: i128,
}

impl Image {
    fn read(&mut self, offset: u64, count: u64) -> Result<Vec<u8>> {
        if i128::from(offset) + i128::from(count) > self.size {
            return Err(invalid("RomFS extent outside image"));
        }
        let mut data = vec![0u8; usize::try_from(count).map_err(|_| invalid("truncated RomFS extent"))?];
        self.stream
            .seek(SeekFrom::Start(self.base + offset))
            .and_then(|_| self.stream.read_exact(&mut data))
            .map_err(|_| invalid("truncated RomFS extent"))?;
        Ok(data)
    }
}

struct DirectoryRecord {
    parent: u32,
    name: Vec<u8>,
    path: String,
}

struct FileRecord {
    parent: u32,
    name: Vec<u8>,
    path: String,
    payload: u64,
    length: u64,
}

/// Reads one Level-3 record: (encoded name, decoded name).
fn record(table: &[u8], offset: u32, fixed: usize, extents: &mut Vec<(usize, usize)>) -> Result<(Vec<u8>, String)> {
    let offset = offset as usize;
    if offset + fixed > table.len() || !offset.is_multiple_of(4) {
        return Err(invalid("invalid RomFS record offset"));
    }
    let name_size = u32_at(table, offset + fixed - 4) as usize;
    if !name_size.is_multiple_of(2) || offset + fixed + name_size > table.len() {
        return Err(invalid("invalid RomFS record name"));
    }
    let name = table[offset + fixed..offset + fixed + name_size].to_vec();
    extents.push((offset, (offset + fixed + name_size + 3) & !3));
    // Python's strict UTF-16LE decode rejects the same unpaired surrogates
    // with a codec message; this keeps the record message instead.
    let units: Vec<u16> = name.chunks_exact(2).map(|unit| u16::from_le_bytes([unit[0], unit[1]])).collect();
    let text = String::from_utf16(&units).map_err(|_| invalid("invalid RomFS record name"))?;
    Ok((name, text))
}

fn bad_name(name: &str) -> bool {
    name.contains('/') || name.contains('\\') || name.contains('\0') || name == "." || name == ".."
}

fn join(prefix: &str, name: &str) -> String {
    if prefix.is_empty() {
        name.to_string()
    } else {
        format!("{prefix}/{name}")
    }
}

fn check_extents(mut ranges: Vec<(usize, usize)>) -> Result<()> {
    ranges.sort_unstable();
    let mut end = 0;
    for (start, limit) in ranges {
        if start < end {
            return Err(invalid("overlapping RomFS records"));
        }
        end = limit;
    }
    Ok(())
}

/// Writes the EUR service view of a USA Level-3 RomFS (optionally IVFC
/// wrapped) to a new `destination`, returning the resource summary.
pub fn normalize_romfs(source: &Path, destination: &Path, cancel: &CancelToken) -> Result<Value> {
    let regular = std::fs::symlink_metadata(source).is_ok_and(|m| m.file_type().is_file());
    if !regular || destination.exists() || resolve(source) == resolve(destination) {
        return Err(invalid("normalization requires a regular input and a new output"));
    }
    let open_error = |error| ForgeError::io(format!("Cannot read {}", source.display()), error);
    let mut stream = File::open(source).map_err(open_error)?;
    let image_size = stream.metadata().map_err(open_error)?.len();
    let mut magic = Vec::with_capacity(4);
    (&mut stream).take(4).read_to_end(&mut magic).map_err(open_error)?;
    let base = if magic == b"IVFC" { 0x1000 } else { 0 };
    let mut image = Image { stream, base, size: i128::from(image_size) - i128::from(base) };

    let header = image.read(0, 40)?;
    let h: Vec<u32> = (0..10).map(|i| u32_at(&header, i * 4)).collect();
    if h[0] != 40 || !h[2].is_multiple_of(4) || !h[6].is_multiple_of(4) || h[2] == 0 || h[6] == 0 {
        return Err(invalid("invalid Level-3 header"));
    }
    if [h[2], h[4], h[6], h[8]].into_iter().max().unwrap_or(0) > METADATA_LIMIT {
        return Err(invalid("RomFS metadata exceeds limits"));
    }
    let mut end = 40i128;
    for (start, length) in [(h[1], h[2]), (h[3], h[4]), (h[5], h[6]), (h[7], h[8])] {
        let (start, length) = (i128::from(start), i128::from(length));
        if start < end || start % 4 != 0 || length == 0 || start + length > image.size {
            return Err(invalid("invalid or overlapping RomFS metadata extents"));
        }
        end = start + length;
    }
    let data_offset = i128::from(h[9]);
    if data_offset < end || data_offset > image.size || data_offset % 16 != 0 {
        return Err(invalid("invalid RomFS data extent"));
    }
    let mut dirs = image.read(u64::from(h[3]), u64::from(h[4]))?;
    let mut files = image.read(u64::from(h[7]), u64::from(h[8]))?;

    let mut dir_records: BTreeMap<u32, DirectoryRecord> = BTreeMap::new();
    let mut file_records: BTreeMap<u32, FileRecord> = BTreeMap::new();
    let (mut dir_extents, mut file_extents) = (Vec::new(), Vec::new());
    let mut pending: Vec<(u32, String, u32)> = vec![(0, String::new(), 0)];
    while let Some((offset, prefix, expected_parent)) = pending.pop() {
        cancel.check()?;
        if dir_records.contains_key(&offset) {
            return Err(invalid("cyclic or duplicate RomFS directory"));
        }
        let (encoded, name) = record(&dirs, offset, 24, &mut dir_extents)?;
        let at = offset as usize;
        let (parent, sibling, child_dir) = (u32_at(&dirs, at), u32_at(&dirs, at + 4), u32_at(&dirs, at + 8));
        let mut child_file = u32_at(&dirs, at + 12);
        if parent != expected_parent
            || bad_name(&name)
            || (offset == 0 && (!name.is_empty() || sibling != INVALID))
            || (offset != 0 && name.is_empty())
        {
            return Err(invalid("invalid RomFS directory ancestry"));
        }
        let path = join(&prefix, &name);
        dir_records.insert(offset, DirectoryRecord { parent, name: encoded, path: path.clone() });
        if sibling != INVALID {
            pending.push((sibling, prefix, expected_parent));
        }
        if child_dir != INVALID {
            pending.push((child_dir, path.clone(), offset));
        }
        while child_file != INVALID {
            if file_records.contains_key(&child_file) {
                return Err(invalid("cyclic or duplicate RomFS file"));
            }
            let (encoded, text) = record(&files, child_file, 32, &mut file_extents)?;
            let at = child_file as usize;
            let (owner, following) = (u32_at(&files, at), u32_at(&files, at + 4));
            let (payload, length) = (u64_at(&files, at + 8), u64_at(&files, at + 16));
            if owner != offset || text.is_empty() || bad_name(&text) {
                return Err(invalid("invalid RomFS file ancestry"));
            }
            if data_offset + i128::from(payload) + i128::from(length) > image.size {
                return Err(invalid("RomFS file outside image"));
            }
            file_records.insert(child_file, FileRecord { parent: owner, name: encoded, path: join(&path, &text), payload, length });
            child_file = following;
        }
    }
    check_extents(dir_extents)?;
    check_extents(file_extents)?;
    let dir_paths: HashSet<&str> = dir_records.values().map(|r| r.path.as_str()).collect();
    let file_paths: HashSet<&str> = file_records.values().map(|r| r.path.as_str()).collect();
    if dir_paths.len() != dir_records.len() || file_paths.len() != file_records.len() {
        return Err(invalid("duplicate RomFS path"));
    }
    if !RESOURCE_DIRECTORIES.iter().all(|path| dir_paths.contains(path)) {
        return Err(invalid("missing USA resource directories"));
    }
    if ["message/eu", "misc/eu"].iter().any(|path| dir_paths.contains(path)) {
        return Err(invalid("EU resource directories already exist"));
    }
    if !HINT_MOVIES.iter().all(|(path, _)| file_paths.contains(path)) {
        return Err(invalid("missing USA hint movies"));
    }
    let renamed_movie =
        |(path, name): &(&str, &str)| format!("{}/{}", path.rsplit_once('/').map_or(*path, |(directory, _)| directory), name);
    if HINT_MOVIES.iter().any(|movie| file_paths.contains(renamed_movie(movie).as_str())) {
        return Err(invalid("EU hint movies already exist"));
    }
    let qm: Vec<&FileRecord> = file_records.values().filter(|r| r.path == "message/us/us.qm").collect();
    if qm.len() != 1 {
        return Err(invalid("missing USA message container"));
    }
    let (qm_offset, qm_size) = (u64::from(h[9]) + qm[0].payload, qm[0].length);
    let head = image.read(qm_offset, 16)?;
    let count = u32_at(&head, 8);
    if !(1..=100_000).contains(&count) || 16 + u64::from(count) * 96 > qm_size {
        return Err(invalid("invalid QM count"));
    }
    let qm_table = adapt_qm_table(&image.read(qm_offset, 16 + u64::from(count) * 96)?, qm_size)?;

    let encode = |name: &str| name.encode_utf16().flat_map(u16::to_le_bytes).collect::<Vec<u8>>();
    let eu = encode("eu");
    let mut directory_hashes = vec![INVALID; (h[2] / 4) as usize];
    for (&offset, record) in &dir_records {
        let at = offset as usize;
        let mut name = record.name.as_slice();
        if RESOURCE_DIRECTORIES.contains(&record.path.as_str()) {
            dirs[at + 24..at + 28].copy_from_slice(&eu);
            name = &eu;
        }
        let bucket = (name_hash(record.parent, name) % directory_hashes.len() as u32) as usize;
        dirs[at + 16..at + 20].copy_from_slice(&directory_hashes[bucket].to_le_bytes());
        directory_hashes[bucket] = offset;
    }
    let mut file_hashes = vec![INVALID; (h[6] / 4) as usize];
    for (&offset, record) in &file_records {
        let at = offset as usize;
        let renamed = FILE_RENAMES.iter().find(|(path, _)| *path == record.path).map(|(_, name)| encode(name));
        let name = match &renamed {
            Some(name) => {
                files[at + 32..at + 32 + name.len()].copy_from_slice(name);
                name.as_slice()
            }
            None => record.name.as_slice(),
        };
        let bucket = (name_hash(record.parent, name) % file_hashes.len() as u32) as usize;
        files[at + 24..at + 28].copy_from_slice(&file_hashes[bucket].to_le_bytes());
        file_hashes[bucket] = offset;
    }
    let table_bytes = |table: &[u32]| table.iter().flat_map(|value| value.to_le_bytes()).collect::<Vec<u8>>();
    let patches = [
        (u64::from(h[1]), table_bytes(&directory_hashes)),
        (u64::from(h[3]), dirs),
        (u64::from(h[5]), table_bytes(&file_hashes)),
        (u64::from(h[7]), files),
        (qm_offset, qm_table),
    ];
    write_service_view(&mut image, destination, &patches, cancel)?;
    Ok(json!({
        "files": file_records.len(), "renamed_directories": 2, "renamed_files": FILE_RENAMES.len(),
        "qm_records": count, "language_slots": {"2": 1, "4": 5, "6": 7},
        "hint_movies": {"183": 182, "185": 186, "187": 188},
        "output_format": "romfs_level3_service_view", "payloads_replaced": 0,
    }))
}

/// Copies the Level-3 bytes (no IVFC wrapper) to a temporary file beside
/// `destination`, applies the patches, syncs and renames it into place.
fn write_service_view(image: &mut Image, destination: &Path, patches: &[(u64, Vec<u8>)], cancel: &CancelToken) -> Result<()> {
    let parent = destination.parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or(Path::new("."));
    std::fs::create_dir_all(parent).map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    let write_error = |error| ForgeError::io(format!("Cannot write {}", destination.display()), error);
    let mut output = tempfile::Builder::new().suffix(".partial").tempfile_in(parent).map_err(write_error)?;
    image.stream.seek(SeekFrom::Start(image.base)).map_err(|error| ForgeError::io("Read failed", error))?;
    let mut buffer = vec![0u8; COPY_CHUNK];
    loop {
        cancel.check()?;
        let read = match image.stream.read(&mut buffer) {
            Ok(0) => break,
            Ok(read) => read,
            Err(error) if error.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(error) => return Err(ForgeError::io("Read failed", error)),
        };
        output.write_all(&buffer[..read]).map_err(write_error)?;
    }
    for (offset, payload) in patches {
        output.seek(SeekFrom::Start(*offset)).and_then(|_| output.write_all(payload)).map_err(write_error)?;
    }
    output.flush().and_then(|()| output.as_file().sync_all()).map_err(write_error)?;
    output.persist(destination).map_err(|error| write_error(error.error))?;
    Ok(())
}
