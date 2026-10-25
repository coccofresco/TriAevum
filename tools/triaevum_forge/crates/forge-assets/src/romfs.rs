// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The two private RomFS readers of the Python asset tools. Both accept a
//! raw-IVFC image (Level 3 at 0x1000) or a Level-3 service view.
//!
//! * [`read_level3_file`]: `read_level3_romfs_file` (font pack), one file by
//!   path with bounded, checked metadata reads and its error messages.
//! * [`image_files`]: `water_textures._romfs_image_files` (water, scene
//!   lights), every file directly under named top-level directories. It is
//!   intentionally looser: short reads give short files, cycles end lists.
//!
//! Neither is the identity checker; that lives in forge-catalog.

use std::collections::HashSet;
use std::fs::File;
use std::io::{BufReader, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use forge_base::{CancelToken, ForgeError, Result};

const SUPERBLOCK_SIZE: u64 = 0x1000;
const INVALID_OFFSET: u32 = 0xFFFF_FFFF;

fn invalid(message: &str) -> ForgeError {
    ForgeError::invalid(message)
}

fn io_error(path: &Path, error: std::io::Error) -> ForgeError {
    ForgeError::io(format!("Cannot read {}", path.display()), error)
}

fn u32_at(data: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(data[offset..offset + 4].try_into().expect("four bytes"))
}

fn u64_at(data: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(data[offset..offset + 8].try_into().expect("eight bytes"))
}

/// Python's strict `bytes.decode("utf-16-le")`.
fn utf16_name(bytes: &[u8]) -> Result<String> {
    if !bytes.len().is_multiple_of(2) {
        return Err(invalid("RomFS name is not valid UTF-16"));
    }
    let units: Vec<u16> = bytes.chunks_exact(2).map(|pair| u16::from_le_bytes([pair[0], pair[1]])).collect();
    String::from_utf16(&units).map_err(|_| invalid("RomFS name is not valid UTF-16"))
}

/// Reads up to `limit` bytes, fewer at the end of the file.
fn read_up_to(stream: &mut impl Read, limit: u64) -> std::io::Result<Vec<u8>> {
    let mut data = Vec::new();
    stream.take(limit).read_to_end(&mut data)?;
    Ok(data)
}

fn level3_base(stream: &mut (impl Read + Seek)) -> std::io::Result<u64> {
    stream.seek(SeekFrom::Start(0))?;
    let prefix = read_up_to(stream, 4)?;
    Ok(if prefix == b"IVFC" { SUPERBLOCK_SIZE } else { 0 })
}

/// One file of a RomFS image, or `None` where Python raised
/// FileNotFoundError (a missing image, directory or file).
pub fn read_level3_file(image: &Path, relative_path: &str) -> Result<Option<Vec<u8>>> {
    let file = match File::open(image) {
        Ok(file) => file,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(io_error(image, error)),
    };
    let mut reader = Level3Reader::open(BufReader::new(file)).map_err(|fault| fault.into_error(image))?;
    reader.find(relative_path).map_err(|fault| fault.into_error(image))
}

enum Fault {
    Format(&'static str),
    Io(std::io::Error),
}

impl From<std::io::Error> for Fault {
    fn from(error: std::io::Error) -> Self {
        Self::Io(error)
    }
}

impl Fault {
    fn into_error(self, image: &Path) -> ForgeError {
        match self {
            Self::Format(message) => invalid(message),
            Self::Io(error) => io_error(image, error),
        }
    }
}

struct Level3Reader<R> {
    stream: R,
    size: u64,
    base: u64,
    directories: (u64, u64),
    files: (u64, u64),
    data: u64,
}

struct Directory {
    sibling: u32,
    child_directory: u32,
    child_file: u32,
    name: String,
}

struct FileEntry {
    sibling: u32,
    offset: u64,
    size: u64,
    name: String,
}

impl<R: Read + Seek> Level3Reader<R> {
    fn open(mut stream: R) -> std::result::Result<Self, Fault> {
        let size = stream.seek(SeekFrom::End(0))?;
        let base = level3_base(&mut stream)?;
        if base + 0x28 > size {
            return Err(Fault::Format("truncated original RomFS image"));
        }
        stream.seek(SeekFrom::Start(base))?;
        let mut header = [0u8; 0x28];
        stream.read_exact(&mut header)?;
        let field = |index: usize| u64::from(u32_at(&header, index * 4));
        if field(0) != 0x28 {
            return Err(Fault::Format("original RomFS has no Level-3 header"));
        }
        Ok(Self { stream, size, base, directories: (field(3), field(4)), files: (field(7), field(8)), data: field(9) })
    }

    fn metadata(&mut self, offset: u64, table: (u64, u64), fixed: u64) -> std::result::Result<Vec<u8>, Fault> {
        let (table_offset, table_size) = table;
        if offset >= table_size || fixed > table_size - offset {
            return Err(Fault::Format("RomFS metadata offset is outside its table"));
        }
        self.stream.seek(SeekFrom::Start(self.base + table_offset + offset))?;
        let data = read_up_to(&mut self.stream, fixed)?;
        if data.len() as u64 != fixed {
            return Err(Fault::Format("truncated RomFS metadata"));
        }
        Ok(data)
    }

    fn name(&mut self, size: u32, truncated: &'static str) -> std::result::Result<String, Fault> {
        let data = read_up_to(&mut self.stream, u64::from(size))?;
        if data.len() != size as usize {
            return Err(Fault::Format(truncated));
        }
        utf16_name(&data).map_err(|_| Fault::Format("invalid RomFS UTF-16 name"))
    }

    fn directory(&mut self, offset: u32) -> std::result::Result<Directory, Fault> {
        let offset = u64::from(offset);
        let fixed = self.metadata(offset, self.directories, 24)?;
        let name_size = u32_at(&fixed, 20);
        if !name_size.is_multiple_of(2) || 24 + u64::from(name_size) > self.directories.1 - offset {
            return Err(Fault::Format("invalid RomFS directory name"));
        }
        let name = self.name(name_size, "truncated RomFS directory name")?;
        Ok(Directory {
            sibling: u32_at(&fixed, 4),
            child_directory: u32_at(&fixed, 8),
            child_file: u32_at(&fixed, 12),
            name,
        })
    }

    fn file(&mut self, offset: u32) -> std::result::Result<FileEntry, Fault> {
        let offset = u64::from(offset);
        let fixed = self.metadata(offset, self.files, 32)?;
        let name_size = u32_at(&fixed, 28);
        if !name_size.is_multiple_of(2) || 32 + u64::from(name_size) > self.files.1 - offset {
            return Err(Fault::Format("invalid RomFS file name"));
        }
        let name = self.name(name_size, "truncated RomFS file name")?;
        Ok(FileEntry { sibling: u32_at(&fixed, 4), offset: u64_at(&fixed, 8), size: u64_at(&fixed, 16), name })
    }

    fn find(&mut self, relative_path: &str) -> std::result::Result<Option<Vec<u8>>, Fault> {
        let parts: Vec<&str> = relative_path.split('/').filter(|part| !part.is_empty()).collect();
        let Some((file_name, directories)) = parts.split_last() else {
            return Err(Fault::Format("empty RomFS path"));
        };
        let mut directory_offset = 0u32;
        for component in directories {
            let mut child = self.directory(directory_offset)?.child_directory;
            let mut visited = HashSet::new();
            loop {
                if child == INVALID_OFFSET {
                    return Ok(None);
                }
                if !visited.insert(child) {
                    return Err(Fault::Format("cyclic RomFS directory list"));
                }
                let entry = self.directory(child)?;
                if entry.name == *component {
                    directory_offset = child;
                    break;
                }
                child = entry.sibling;
            }
        }
        let mut child = self.directory(directory_offset)?.child_file;
        let mut visited = HashSet::new();
        while child != INVALID_OFFSET {
            if !visited.insert(child) {
                return Err(Fault::Format("cyclic RomFS file list"));
            }
            let entry = self.file(child)?;
            if entry.name == *file_name {
                let start = self.base.checked_add(self.data).and_then(|base| base.checked_add(entry.offset));
                let Some(start) = start.filter(|start| *start <= self.size && entry.size <= self.size - start) else {
                    return Err(Fault::Format("RomFS file payload is outside the image"));
                };
                self.stream.seek(SeekFrom::Start(start))?;
                let data = read_up_to(&mut self.stream, entry.size)?;
                if data.len() as u64 != entry.size {
                    return Err(Fault::Format("truncated RomFS file payload"));
                }
                return Ok(Some(data));
            }
            child = entry.sibling;
        }
        Ok(None)
    }
}

/// Visits `("<directory>/<file>", bytes)` for every file directly under the
/// named top-level directories, in the image's sibling-list order.
pub fn image_files(
    image: &Path,
    directories: &[&str],
    cancel: &CancelToken,
    visit: &mut dyn FnMut(String, Vec<u8>) -> Result<()>,
) -> Result<()> {
    let file = File::open(image).map_err(|error| io_error(image, error))?;
    let mut stream = BufReader::new(file);
    let truncated = || invalid("RomFS metadata is truncated");
    let base = level3_base(&mut stream).map_err(|error| io_error(image, error))?;
    let read_at = |stream: &mut BufReader<File>, offset: u64, limit: u64| -> Result<Vec<u8>> {
        stream.seek(SeekFrom::Start(offset)).and_then(|_| read_up_to(stream, limit)).map_err(|error| io_error(image, error))
    };
    let header = read_at(&mut stream, base, 0x28)?;
    if header.len() < 0x28 {
        return Err(truncated());
    }
    let field = |index: usize| u64::from(u32_at(&header, index * 4));
    if field(0) != 0x28 {
        return Err(invalid("RomFS has no Level-3 header"));
    }
    let directory_table = read_at(&mut stream, base + field(3), field(4))?;
    let file_table = read_at(&mut stream, base + field(7), field(8))?;
    let data_base = base + field(9);

    let directory = |offset: u32| -> Result<(u32, u32, u32, String)> {
        let start = offset as usize;
        if start.checked_add(24).is_none_or(|end| end > directory_table.len()) {
            return Err(truncated());
        }
        let name_size = u32_at(&directory_table, start + 20) as usize;
        let name = slice(&directory_table, start + 24, name_size);
        Ok((u32_at(&directory_table, start + 4), u32_at(&directory_table, start + 8), u32_at(&directory_table, start + 12), utf16_name(name)?))
    };
    let (_, mut child, _, _) = directory(0)?;
    let mut seen = HashSet::new();
    while child != INVALID_OFFSET && seen.insert(child) {
        let (sibling, _, first_file, name) = directory(child)?;
        if directories.contains(&name.as_str()) {
            let mut entry = first_file;
            let mut files_seen = HashSet::new();
            while entry != INVALID_OFFSET && files_seen.insert(entry) {
                cancel.check()?;
                let start = entry as usize;
                if start.checked_add(32).is_none_or(|end| end > file_table.len()) {
                    return Err(truncated());
                }
                let next = u32_at(&file_table, start + 4);
                let offset = u64_at(&file_table, start + 8);
                let size = u64_at(&file_table, start + 16);
                let name_size = u32_at(&file_table, start + 28) as usize;
                let file_name = utf16_name(slice(&file_table, start + 32, name_size))?;
                let position = data_base.checked_add(offset).ok_or_else(truncated)?;
                let data = read_at(&mut stream, position, size)?;
                visit(format!("{name}/{file_name}"), data)?;
                entry = next;
            }
        }
        child = sibling;
    }
    Ok(())
}

/// Python's `data[start:start + length]`.
fn slice(data: &[u8], start: usize, length: usize) -> &[u8] {
    let start = start.min(data.len());
    let end = start.saturating_add(length).min(data.len());
    &data[start..end]
}

/// The entries of an extracted RomFS directory in `sorted(Path.glob(...))`
/// order: by case-folded name on Windows, by code point elsewhere. A missing
/// directory has none, as pathlib's glob yields nothing.
pub fn sorted_entries(directory: &Path) -> Vec<(String, PathBuf)> {
    let Ok(entries) = std::fs::read_dir(directory) else { return Vec::new() };
    let mut listing: Vec<(String, PathBuf)> = entries
        .filter_map(|entry| entry.ok())
        .map(|entry| (entry.file_name().to_string_lossy().into_owned(), entry.path()))
        .collect();
    if cfg!(windows) {
        listing.sort_by_cached_key(|(name, _)| name.to_lowercase());
    } else {
        listing.sort_by(|a, b| a.0.cmp(&b.0));
    }
    listing
}

#[cfg(test)]
mod tests {
    use super::*;

    fn name(text: &str) -> Vec<u8> {
        let mut encoded: Vec<u8> = text.encode_utf16().flat_map(u16::to_le_bytes).collect();
        while !encoded.len().is_multiple_of(4) {
            encoded.push(0);
        }
        encoded
    }

    /// `test_water_textures.level3_romfs`: `{directory: {file: data}}` under
    /// the root, in the given order.
    type Tree<'a> = [(&'a str, Vec<(&'a str, Vec<u8>)>)];

    fn level3_romfs(directories: &Tree<'_>) -> Vec<u8> {
        let mut directory_offset = 24 + 4;
        let mut layout = Vec::new();
        for (directory, files) in directories {
            layout.push((directory_offset as u32, *directory, files));
            directory_offset += 24 + name(directory).len();
        }
        let le = |values: &[u32]| values.iter().flat_map(|v| v.to_le_bytes()).collect::<Vec<u8>>();
        let first = layout.first().map_or(INVALID_OFFSET, |entry| entry.0);
        let mut directory_table = le(&[0, INVALID_OFFSET, first, INVALID_OFFSET, INVALID_OFFSET, 0]);
        directory_table.extend([0u8; 4]);
        let (mut file_table, mut file_data): (Vec<u8>, Vec<u8>) = (Vec::new(), Vec::new());
        for (index, (offset, directory, files)) in layout.iter().enumerate() {
            let sibling = layout.get(index + 1).map_or(INVALID_OFFSET, |entry| entry.0);
            let first_file = if files.is_empty() { INVALID_OFFSET } else { file_table.len() as u32 };
            for (position, (file_name, data)) in files.iter().enumerate() {
                let entry_size = 32 + name(file_name).len();
                let next = if position + 1 < files.len() { (file_table.len() + entry_size) as u32 } else { INVALID_OFFSET };
                file_table.extend(le(&[*offset, next]));
                file_table.extend((file_data.len() as u64).to_le_bytes());
                file_table.extend((data.len() as u64).to_le_bytes());
                file_table.extend(le(&[INVALID_OFFSET, (file_name.encode_utf16().count() * 2) as u32]));
                file_table.extend(name(file_name));
                file_data.extend(data);
            }
            directory_table.extend(le(&[0, sibling, INVALID_OFFSET, first_file, INVALID_OFFSET]));
            directory_table.extend(le(&[(directory.encode_utf16().count() * 2) as u32]));
            directory_table.extend(name(directory));
        }
        let directory_meta = 0x28u32;
        let file_meta = directory_meta + directory_table.len() as u32;
        let data_start = file_meta + file_table.len() as u32;
        let mut image = le(&[0x28, 0, 0, directory_meta, directory_table.len() as u32, 0, 0, file_meta]);
        image.extend(le(&[file_table.len() as u32, data_start]));
        image.extend(directory_table);
        image.extend(file_table);
        image.extend(file_data);
        image
    }

    #[test]
    fn both_readers_find_files_in_level3_and_ivfc_images() {
        let image = level3_romfs(&[
            ("scene", vec![("a.zsi", b"one".to_vec()), ("b.zsi", b"two".to_vec())]),
            ("message", vec![("ltn16.qbf", b"font".to_vec())]),
        ]);
        let directory = tempfile::tempdir().unwrap();
        let mut ivfc = b"IVFC".to_vec();
        ivfc.resize(0x1000, 0);
        ivfc.extend(&image);
        for (file, bytes) in [("l3.bin", image.clone()), ("ivfc.bin", ivfc)] {
            let path = directory.path().join(file);
            std::fs::write(&path, bytes).unwrap();
            assert_eq!(read_level3_file(&path, "message/ltn16.qbf").unwrap().unwrap(), b"font");
            assert!(read_level3_file(&path, "message/other.qbf").unwrap().is_none());
            assert!(read_level3_file(&path, "missing/ltn16.qbf").unwrap().is_none());
            let mut seen = Vec::new();
            image_files(&path, &["scene"], &CancelToken::new(), &mut |name, data| {
                seen.push((name, data));
                Ok(())
            })
            .unwrap();
            assert_eq!(seen, vec![("scene/a.zsi".to_string(), b"one".to_vec()), ("scene/b.zsi".to_string(), b"two".to_vec())]);
        }
        assert!(read_level3_file(&directory.path().join("absent.bin"), "a/b").unwrap().is_none());
        let short = directory.path().join("short.bin");
        std::fs::write(&short, b"tiny").unwrap();
        assert_eq!(read_level3_file(&short, "a/b").unwrap_err().message, "truncated original RomFS image");
    }
}
