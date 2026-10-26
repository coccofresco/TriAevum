// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The O3RO RomFS overlay (`romfs_mods.romfs_file_records`,
//! `build_romfs_overlay`, `prepare_romfs_mods`, `romfs_service_view`).
//!
//! An overlay patches the file records of the user's RomFS service view so
//! the replaced files point at payloads appended after the view. No base
//! bytes are copied or moved. Layout (little endian): 64-byte header
//! `<4sIQQIIQ24x>` = "O3RO", version 1, base service size, extended view size,
//! patch count, flags 0, tail file offset; then 32-byte entries `<QIIQQ>`
//! = service-relative patch position, byte count (16), flags 0, expected-bytes
//! file offset, replacement-bytes file offset; then each patch's expected 16
//! bytes and new `<QQ>` payload-relative offset and size; then the 16-byte
//! aligned payloads with zero gaps.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::archive::{archive_contract, stored_archive, ArchiveContract};
use crate::catalog::{Catalog, RomFsMod};
use crate::extract::ArchiveExtractor;
use crate::romfs_mods::{enabled_mods, extract_archive, stored_members, StoredMembers};
use crate::util::{identity_key, io_error};

pub const OVERLAY_NAME: &str = "romfs_overlay.o3ro";
/// 1: O3RO v1 overlay, file-record payload patches and an appended tail.
pub const IMPORT_VERSION: u64 = 1;
const INVALID: u32 = 0xFFFF_FFFF;
const PAYLOAD_ALIGNMENT: u64 = 16;
pub const MAXIMUM_MEMBER_BYTES: u64 = 64 * 1024 * 1024;
const MAXIMUM_TABLE_BYTES: u32 = 16 * 1024 * 1024;

/// Python distinguishes `ValueError` (the data is wrong: callers keep the
/// native files) from `OSError` (propagates); the overlay builder keeps that
/// split.
#[derive(Debug)]
enum Failure {
    Value(String),
    Other(ForgeError),
}

impl From<ForgeError> for Failure {
    fn from(error: ForgeError) -> Self {
        Self::Other(error)
    }
}

impl From<Failure> for ForgeError {
    fn from(failure: Failure) -> Self {
        match failure {
            Failure::Value(message) => ForgeError::invalid(message),
            Failure::Other(error) => error,
        }
    }
}

fn value<T>(message: impl Into<String>) -> std::result::Result<T, Failure> {
    Err(Failure::Value(message.into()))
}

/// A file entry of the RomFS: its record offset in the file table, payload
/// offset in the data section and payload size.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FileRecord {
    pub record: u32,
    pub payload: u64,
    pub length: u64,
}

#[derive(Debug)]
pub struct RomfsRecords {
    /// The ten u32 fields of the Level-3 header.
    pub header: [u32; 10],
    pub files: HashMap<String, FileRecord>,
}

fn u32_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().expect("four bytes"))
}

fn u64_at(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().expect("eight bytes"))
}

fn name_of(table: &[u8], offset: u64, fixed: u64) -> std::result::Result<String, Failure> {
    if !offset.is_multiple_of(4) || offset + fixed > table.len() as u64 {
        return value("Invalid RomFS record");
    }
    let length = u64::from(u32_at(table, (offset + fixed - 4) as usize));
    if !length.is_multiple_of(2) || offset + fixed + length > table.len() as u64 {
        return value("Invalid RomFS name");
    }
    let start = (offset + fixed) as usize;
    let units: Vec<u16> = table[start..start + length as usize]
        .chunks_exact(2)
        .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
        .collect();
    let Ok(name) = String::from_utf16(&units) else {
        return value("Invalid RomFS name");
    };
    if name.contains(['/', '\\', '\0']) || name == "." || name == ".." {
        return value("Invalid RomFS name");
    }
    Ok(name)
}

fn read_records<S: Read + Seek>(stream: &mut S, base: u64, size: u64) -> std::result::Result<RomfsRecords, Failure> {
    let mut read = |offset: u64, length: u64| -> std::result::Result<Vec<u8>, Failure> {
        if offset.checked_add(length).is_none_or(|end| end > size) {
            return value("RomFS extent outside the service view");
        }
        stream
            .seek(SeekFrom::Start(base + offset))
            .map_err(|error| Failure::Other(io_error("Cannot read the RomFS", error)))?;
        let mut data = vec![0u8; length as usize];
        let mut filled = 0;
        while filled < data.len() {
            match stream.read(&mut data[filled..]) {
                Ok(0) => return value("Truncated RomFS"),
                Ok(count) => filled += count,
                Err(error) if error.kind() == std::io::ErrorKind::Interrupted => {}
                Err(error) => return Err(Failure::Other(io_error("Cannot read the RomFS", error))),
            }
        }
        Ok(data)
    };

    let raw = read(0, 40)?;
    let mut header = [0u32; 10];
    for (index, field) in header.iter_mut().enumerate() {
        *field = u32_at(&raw, index * 4);
    }
    if header[0] != 40 {
        return value("Invalid RomFS Level-3 header");
    }
    for (offset, length) in
        [(header[1], header[2]), (header[3], header[4]), (header[5], header[6]), (header[7], header[8])]
    {
        if offset % 4 != 0
            || length == 0
            || length > MAXIMUM_TABLE_BYTES
            || u64::from(offset) + u64::from(length) > size
        {
            return value("Invalid RomFS metadata bounds");
        }
    }
    if u64::from(header[9]) > size {
        return value("Invalid RomFS data offset");
    }
    let directories = read(u64::from(header[3]), u64::from(header[4]))?;
    let files = read(u64::from(header[7]), u64::from(header[8]))?;

    let mut records = HashMap::new();
    let mut visited_directories = HashSet::new();
    let mut visited_files = HashSet::new();
    let mut pending: Vec<(u64, String)> = vec![(0, String::new())];
    while let Some((offset, prefix)) = pending.pop() {
        if !visited_directories.insert(offset) {
            return value("Cyclic RomFS directory");
        }
        let name = name_of(&directories, offset, 24)?;
        let at = offset as usize;
        let (sibling, child, mut file) =
            (u32_at(&directories, at + 4), u32_at(&directories, at + 8), u32_at(&directories, at + 12));
        let path = if !prefix.is_empty() && !name.is_empty() {
            format!("{prefix}/{name}")
        } else if !name.is_empty() {
            name
        } else {
            prefix.clone()
        };
        if sibling != INVALID {
            pending.push((u64::from(sibling), prefix));
        }
        if child != INVALID {
            pending.push((u64::from(child), path.clone()));
        }
        while file != INVALID {
            if !visited_files.insert(file) {
                return value("Cyclic RomFS file");
            }
            let text = name_of(&files, u64::from(file), 32)?;
            let at = file as usize;
            let (owner, following) = (u32_at(&files, at), u32_at(&files, at + 4));
            let (payload, length) = (u64_at(&files, at + 8), u64_at(&files, at + 16));
            let end = u128::from(header[9]) + u128::from(payload) + u128::from(length);
            if u64::from(owner) != offset || end > u128::from(size) {
                return value("Invalid RomFS file record");
            }
            let full = if path.is_empty() { text } else { format!("{path}/{text}") };
            if records.contains_key(&full) {
                return value("Duplicate RomFS path");
            }
            records.insert(full, FileRecord { record: file, payload, length });
            file = following;
        }
    }
    Ok(RomfsRecords { header, files: records })
}

/// The Level-3 header and every file's record of a RomFS service view
/// (`size` bytes at `base` of `stream`), by directory traversal.
pub fn romfs_file_records<S: Read + Seek>(stream: &mut S, base: u64, size: u64) -> Result<RomfsRecords> {
    read_records(stream, base, size).map_err(ForgeError::from)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OverlaySummary {
    pub files: u64,
    pub base_view_bytes: u64,
    pub appended_bytes: u64,
}

fn align(value: u64) -> u64 {
    value.div_ceil(PAYLOAD_ALIGNMENT) * PAYLOAD_ALIGNMENT
}

struct Patch {
    field: u64,
    expected: [u8; 16],
    new: [u8; 16],
}

fn build_overlay(
    romfs: &Path,
    service_offset: u64,
    service_size: u64,
    replacements: &BTreeMap<String, PathBuf>,
    output: &Path,
    cancel: &CancelToken,
) -> std::result::Result<OverlaySummary, Failure> {
    if replacements.is_empty() {
        return value("A RomFS overlay needs at least one replaced file");
    }
    let mut stream = File::open(romfs).map_err(|error| io_error(format!("Cannot read {}", romfs.display()), error))?;
    let image_size = stream.metadata().map_err(|error| io_error("Cannot stat the RomFS", error))?.len();
    if service_size == 0 || service_offset.checked_add(service_size).is_none_or(|end| end > image_size) {
        return value("The RomFS service view is outside its image");
    }
    let RomfsRecords { header, files } = read_records(&mut stream, service_offset, service_size)?;
    let mut patches = Vec::new();
    let mut tail_layout = Vec::new();
    let mut cursor = service_size;
    // BTreeMap iterates in the Python `sorted(replacements)` order.
    for (path, source) in replacements {
        let Some(record) = files.get(path) else {
            return value(format!("The RomFS has no {path} to replace"));
        };
        let length =
            fs::metadata(source).map_err(|error| io_error(format!("Cannot read {}", source.display()), error))?.len();
        if length == 0 || length > MAXIMUM_MEMBER_BYTES {
            return value(format!("Invalid replacement size for {path}"));
        }
        cursor = align(cursor);
        let field = u64::from(header[7]) + u64::from(record.record) + 8;
        stream
            .seek(SeekFrom::Start(service_offset + field))
            .map_err(|error| io_error("Cannot read the RomFS", error))?;
        let mut expected = [0u8; 16];
        stream.read_exact(&mut expected).map_err(|error| io_error("Cannot read the RomFS", error))?;
        let mut new = [0u8; 16];
        new[..8].copy_from_slice(&(cursor - u64::from(header[9])).to_le_bytes());
        new[8..].copy_from_slice(&length.to_le_bytes());
        patches.push(Patch { field, expected, new });
        tail_layout.push((cursor, source, length));
        cursor += length;
    }
    patches.sort_by_key(|patch| patch.field);
    let view_size = cursor;
    let table_bytes = 64 + 32 * patches.len() as u64;
    let tail_offset = table_bytes + 32 * patches.len() as u64;

    let parent = output.parent().filter(|path| !path.as_os_str().is_empty()).unwrap_or(Path::new("."));
    fs::create_dir_all(parent).map_err(|error| io_error(format!("Cannot create {}", parent.display()), error))?;
    let mut temporary = tempfile::Builder::new()
        .prefix(".")
        .suffix(".tmp")
        .tempfile_in(parent)
        .map_err(|error| io_error(format!("Cannot write in {}", parent.display()), error))?;
    write_overlay(&mut temporary, &patches, &tail_layout, service_size, view_size, tail_offset, cancel)?;
    let size = temporary.as_file().metadata().map_err(|error| io_error("Cannot stat the overlay", error))?.len();
    if size != tail_offset + (view_size - service_size) {
        return value("The RomFS overlay was not written completely");
    }
    temporary.persist(output).map_err(|error| io_error(format!("Cannot replace {}", output.display()), error.error))?;
    Ok(OverlaySummary {
        files: patches.len() as u64,
        base_view_bytes: service_size,
        appended_bytes: view_size - service_size,
    })
}

fn write_overlay(
    temporary: &mut tempfile::NamedTempFile,
    patches: &[Patch],
    tail_layout: &[(u64, &PathBuf, u64)],
    service_size: u64,
    view_size: u64,
    tail_offset: u64,
    cancel: &CancelToken,
) -> std::result::Result<(), Failure> {
    let write = |error: std::io::Error| Failure::Other(io_error("Cannot write the RomFS overlay", error));
    let mut out = std::io::BufWriter::new(temporary.as_file_mut());
    let mut header = Vec::with_capacity(64);
    header.extend_from_slice(b"O3RO");
    header.extend_from_slice(&1u32.to_le_bytes());
    header.extend_from_slice(&service_size.to_le_bytes());
    header.extend_from_slice(&view_size.to_le_bytes());
    header.extend_from_slice(&(patches.len() as u32).to_le_bytes());
    header.extend_from_slice(&0u32.to_le_bytes());
    header.extend_from_slice(&tail_offset.to_le_bytes());
    header.extend_from_slice(&[0u8; 24]);
    out.write_all(&header).map_err(write)?;
    let mut data_offset = 64 + 32 * patches.len() as u64;
    for patch in patches {
        let mut entry = Vec::with_capacity(32);
        entry.extend_from_slice(&patch.field.to_le_bytes());
        entry.extend_from_slice(&(patch.expected.len() as u32).to_le_bytes());
        entry.extend_from_slice(&0u32.to_le_bytes());
        entry.extend_from_slice(&data_offset.to_le_bytes());
        entry.extend_from_slice(&(data_offset + patch.expected.len() as u64).to_le_bytes());
        out.write_all(&entry).map_err(write)?;
        data_offset += (patch.expected.len() + patch.new.len()) as u64;
    }
    for patch in patches {
        out.write_all(&patch.expected).map_err(write)?;
        out.write_all(&patch.new).map_err(write)?;
    }
    let mut position = service_size;
    for &(view_offset, source, length) in tail_layout {
        cancel.check()?;
        out.write_all(&vec![0u8; (view_offset - position) as usize]).map_err(write)?;
        let mut payload =
            File::open(source).map_err(|error| io_error(format!("Cannot read {}", source.display()), error))?;
        std::io::copy(&mut payload, &mut out).map_err(write)?;
        position = view_offset + length;
    }
    out.flush().map_err(write)?;
    drop(out);
    temporary.as_file().sync_all().map_err(write)?;
    Ok(())
}

/// Writes an O3RO v1 overlay: each replaced file's record points at its new
/// payload, appended (16-byte aligned) after the service view. Replacement
/// paths are RomFS paths; payloads are 1 byte to 64 MiB each.
pub fn build_romfs_overlay(
    romfs: &Path,
    service_offset: u64,
    service_size: u64,
    replacements: &BTreeMap<String, PathBuf>,
    output: &Path,
    cancel: &CancelToken,
) -> Result<OverlaySummary> {
    build_overlay(romfs, service_offset, service_size, replacements, output, cancel).map_err(ForgeError::from)
}

/// `(offset, size)` of the RomFS service view the runtime exposes, from the
/// process manifest.
pub fn romfs_service_view(process_manifest: &Path) -> Result<(u64, u64)> {
    let manifest = load_json_object(process_manifest)?;
    let source = manifest.get("source");
    let field = |key: &str| source.and_then(|source| source.get(key)).and_then(Value::as_u64);
    match (field("romfs_service_offset"), field("romfs_service_size")) {
        (Some(offset), Some(size)) if size > 0 => Ok((offset, size)),
        _ => Err(ForgeError::invalid("The process manifest has no RomFS service view")),
    }
}

/// An enabled mod that applies: its recipe contract and verified members.
struct Applied<'a> {
    mod_: &'a RomFsMod,
    contract: ArchiveContract,
    members: StoredMembers,
}

fn reusable_overlay(
    overlay: &Path,
    receipt_path: &Path,
    identity: &Map<String, Value>,
    cancel: &CancelToken,
) -> Result<bool> {
    if !(overlay.is_file() && receipt_path.is_file()) {
        return Ok(false);
    }
    let check = || -> Result<bool> {
        let receipt = load_json_object(receipt_path)?;
        let digest = hex(&sha256_file(overlay, cancel)?.1);
        Ok(receipt.get("source") == Some(&Value::Object(identity.clone()))
            && receipt.get("overlay_sha256").and_then(Value::as_str) == Some(digest.as_str()))
    };
    match check() {
        Err(error) if error.kind == ErrorKind::Cancelled => Err(error),
        // Python: OSError and ValueError mean "rebuild".
        Err(_) => Ok(false),
        Ok(reuse) => Ok(reuse),
    }
}

/// Everything `prepare_romfs_mods` needs besides the mutable callbacks.
pub struct PrepareContext<'a> {
    pub catalog: &'a Catalog,
    /// The package root (stored archives are looked up beside it).
    pub root: &'a Path,
    pub data_root: &'a Path,
    /// The recipe of the user's game revision.
    pub recipe: &'a Value,
    pub romfs: &'a Path,
    pub romfs_sha256: &'a str,
    pub extractor: &'a dyn ArchiveExtractor,
    pub cancel: &'a CancelToken,
}

/// The overlay of the user's enabled RomFS mods, or `None` when none apply.
///
/// `service_view` gives the runtime's (offset, size) RomFS view; it is read
/// only when a mod applies. A mod that is enabled but unavailable, or that
/// cannot be extracted or applied to this RomFS, is reported through `report`
/// (stage, message) and left out: the title is still prepared with the native
/// files. Malformed settings and operating-system failures are errors.
pub fn prepare_romfs_mods(
    context: &PrepareContext<'_>,
    service_view: &mut dyn FnMut() -> Result<(u64, u64)>,
    report: &mut dyn FnMut(&str, &str),
) -> Result<Option<PathBuf>> {
    let PrepareContext { catalog, root, data_root, recipe, romfs, romfs_sha256, extractor, cancel } = *context;
    let mut applied: Vec<Applied<'_>> = Vec::new();
    for mod_id in enabled_mods(data_root, catalog)? {
        let Some(mod_) = catalog.get(&mod_id) else { continue };
        let Some(contract) = archive_contract(recipe, mod_)? else {
            report("mods", &format!("{} does not support this game revision; it is not applied.", mod_.title));
            continue;
        };
        let members = match stored_members(data_root, mod_, cancel)? {
            Some(members) => members,
            None => {
                let Some(archive) = stored_archive(root, data_root, &contract) else {
                    report(
                        "mods",
                        &format!("{} is enabled but not downloaded; the native files are used.", mod_.title),
                    );
                    continue;
                };
                report("mods", &format!("Extracting {} {} from the original archive...", mod_.title, mod_.version));
                match extract_archive(&archive, data_root, mod_, &contract, extractor, cancel) {
                    Ok(members) => members,
                    Err(error) if error.kind == ErrorKind::Cancelled => return Err(error),
                    Err(error) => {
                        report("mods", &format!("{} The native files are used.", error.message));
                        continue;
                    }
                }
            }
        };
        applied.push(Applied { mod_, contract, members });
    }
    // The unsupported custom RomFS folder (owner decision 6), off unless the
    // user turned it on; a folder that fails validation is left out whole.
    let mut custom = None;
    if crate::custom_romfs::enabled(data_root)? {
        let scan = crate::custom_romfs::scan(data_root, cancel)?;
        if let Some(issue) = scan.rejected.first() {
            report("mods", &format!("The custom RomFS folder is not applied: {}.", issue.message()));
        } else if scan.files.is_empty() {
            report("mods", "The custom RomFS folder is on but holds no files.");
        } else {
            custom = Some(scan);
        }
    }
    if applied.is_empty() && custom.is_none() {
        return Ok(None);
    }
    let (service_offset, service_size) = service_view()?;
    let mut replacements = BTreeMap::new();
    for item in &applied {
        // Later catalogue entries win.
        replacements.extend(item.members.iter().map(|(name, path)| (name.clone(), path.clone())));
    }
    let custom = match custom {
        Some(scan) => apply_custom(scan, &applied, romfs, service_offset, service_size, &mut replacements, report)?,
        None => None,
    };
    if replacements.is_empty() {
        return Ok(None);
    }
    let mut identity = Map::new();
    identity.insert("import_version".into(), json!(IMPORT_VERSION));
    identity.insert("romfs_sha256".into(), json!(romfs_sha256));
    identity.insert("service_offset".into(), json!(service_offset));
    identity.insert("service_size".into(), json!(service_size));
    identity.insert(
        "mods".into(),
        Value::Array(
            applied.iter().map(|item| json!([item.mod_.mod_id, item.mod_.version, item.contract.sha256])).collect(),
        ),
    );
    // Only an applied custom folder adds its member: the catalogue-only
    // identity (and its folder) stays the Python Forge's.
    if let Some(custom) = &custom {
        identity.insert("custom_romfs".into(), custom.identity(cancel)?);
    }
    let directory = data_root.join("mods").join("romfs").join(identity_key(&identity)?);
    let overlay = directory.join(OVERLAY_NAME);
    let receipt_path = directory.join("import.json");
    if reusable_overlay(&overlay, &receipt_path, &identity, cancel)? {
        return Ok(Some(overlay));
    }
    let mut names: Vec<String> =
        applied.iter().map(|item| format!("{} {}", item.mod_.title, item.mod_.version)).collect();
    if custom.is_some() {
        names.push("the custom RomFS folder (unsupported)".to_string());
    }
    report("mods", &format!("Applying {} to the local game data...", names.join(", ")));
    let summary = match build_overlay(romfs, service_offset, service_size, &replacements, &overlay, cancel) {
        Ok(summary) => summary,
        // E.g. a revision without one of the replaced files: keep the native files.
        Err(Failure::Value(message)) => {
            report(
                "mods",
                &format!(
                    "The enabled mods cannot be applied to this game data ({message}); the native files are used."
                ),
            );
            return Ok(None);
        }
        Err(Failure::Other(error)) => return Err(error),
    };
    let overlay_sha256 = hex(&sha256_file(&overlay, cancel)?.1);
    let credits: Vec<Value> = applied
        .iter()
        .map(|item| {
            json!({"id": item.mod_.mod_id, "title": item.mod_.title, "version": item.mod_.version,
                   "attribution": item.mod_.attribution(), "license": item.mod_.license_name,
                   "license_url": item.mod_.license_url, "source_url": item.mod_.page})
        })
        .collect();
    let mut receipt = json!({
        "source": identity, "overlay_sha256": overlay_sha256,
        "summary": {"files": summary.files, "base_view_bytes": summary.base_view_bytes,
                    "appended_bytes": summary.appended_bytes},
        "private_local_artifact": true, "redistributable": false, "mods": credits});
    if let Some(custom) = &custom {
        receipt["custom_romfs"] = custom.receipt();
    }
    atomic_write_json(&receipt_path, &receipt)?;
    Ok(Some(overlay))
}

/// The custom folder's files that apply: in the base RomFS (the rest is
/// reported and left out), winning over a catalogued mod's (reported).
struct AppliedCustom {
    name: Option<String>,
    files: Vec<crate::custom_romfs::CustomFile>,
}

impl AppliedCustom {
    /// `[[path, bytes, sha256], ...]`: a changed file makes a new overlay.
    fn identity(&self, cancel: &CancelToken) -> Result<Value> {
        let mut entries = Vec::new();
        for file in &self.files {
            let digest = hex(&sha256_file(&file.source, cancel)?.1);
            entries.push(json!([file.path, file.bytes, digest]));
        }
        Ok(Value::Array(entries))
    }

    /// What the receipt and the status pages say: names and counts only.
    fn receipt(&self) -> Value {
        json!({
            "unsupported": true,
            "name": self.name,
            "files": self.files.len(),
            "paths": self.files.iter().map(|file| file.path.as_str()).collect::<Vec<_>>(),
        })
    }
}

fn apply_custom(
    mut scan: crate::custom_romfs::Scan,
    applied: &[Applied<'_>],
    romfs: &Path,
    service_offset: u64,
    service_size: u64,
    replacements: &mut BTreeMap<String, PathBuf>,
    report: &mut dyn FnMut(&str, &str),
) -> Result<Option<AppliedCustom>> {
    let mut stream = File::open(romfs).map_err(|error| io_error(format!("Cannot read {}", romfs.display()), error))?;
    let files = match read_records(&mut stream, service_offset, service_size) {
        Ok(records) => records.files,
        Err(Failure::Value(message)) => {
            report(
                "mods",
                &format!("The custom RomFS folder is not applied: the game data cannot be read ({message})."),
            );
            return Ok(None);
        }
        Err(Failure::Other(error)) => return Err(error),
    };
    scan.check_romfs(&|path| files.contains_key(path));
    let members: BTreeMap<String, String> = applied
        .iter()
        .flat_map(|item| item.members.keys().map(|path| (path.clone(), item.mod_.title.clone())))
        .collect();
    scan.check_mods(&members);
    for issue in &scan.flagged {
        report("mods", &format!("Custom RomFS folder: {}.", issue.message()));
    }
    if scan.files.is_empty() {
        return Ok(None);
    }
    replacements.extend(scan.files.iter().map(|file| (file.path.clone(), file.source.clone())));
    Ok(Some(AppliedCustom { name: scan.name, files: scan.files }))
}
