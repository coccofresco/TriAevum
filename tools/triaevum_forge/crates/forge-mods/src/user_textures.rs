// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The user's own texture pack (UX plan WP-07): a folder or a zip copied into
//! the managed load folder `D/textures/load/<title id>`, which the runtime
//! loads above the HD interface pack and the game's textures. Nothing here is
//! catalogued or redistributed: the pack is the user's.
//!
//! The rules (`docs/TRIAEVUM_ENHANCEMENTS.md`, shared with F1's Import...):
//!
//! - A path component that is a 3DS title id (16 hex digits starting with
//!   `00040000`) marks a title folder, whose contents are the pack
//!   (`textures/<id>/`, `load/textures/<id>/`, `<id>/`, or the chosen folder
//!   itself). With several, the installed title's is used, else the import
//!   refuses; one of another title is used (textures match by content).
//! - Without one the pack is flat: its common top folder is stripped and it
//!   goes under the installed title only after the user confirmed.
//! - `.png` files need the PNG signature and an `IHDR` of 1..=16384 pixels;
//!   `.dds`/`.ktx` are copied (the runtime does not load them yet); `.json`
//!   files must be JSON objects; other files are skipped. Links, special
//!   files and archive entries outside the pack refuse the import.
//! - At most 200,000 files, 256 MiB each, 8 GiB in total.
//! - The pack needs a texture the runtime loads: a `tex1_<W>x<H>_<hash>...`
//!   `.png` or one `pack.json` maps.
//! - It is copied beside the destination, verified, and only then swapped
//!   with the current pack, which stays untouched if anything fails.

use std::collections::{BTreeSet, HashSet};
use std::fs::{self, File};
use std::io::{BufReader, Read, Write};
use std::path::{Path, PathBuf};

use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use serde_json::{json, Value};

use crate::util::io_error;

pub const MAXIMUM_FILES: u64 = 200_000;
pub const MAXIMUM_FILE_BYTES: u64 = 256 * 1024 * 1024;
pub const MAXIMUM_TOTAL_BYTES: u64 = 8 * 1024 * 1024 * 1024;
const MAXIMUM_PNG_SIDE: u32 = 16_384;
const MAXIMUM_JSON_BYTES: u64 = 16 * 1024 * 1024;
const MAXIMUM_DEPTH: usize = 32;
const PNG_SIGNATURE: [u8; 8] = [0x89, b'P', b'N', b'G', 0x0D, 0x0A, 0x1A, 0x0A];

/// `0004000000033600` and the like: a 3DS application's title id.
pub fn is_title_id(name: &str) -> bool {
    name.len() == 16 && name.bytes().all(|byte| byte.is_ascii_hexdigit()) && name[..8] == *"00040000"
}

/// Azahar's (and Citra's) texture name: `tex1_<W>x<H>_<hash>` before
/// anything else, in any letter case (`azahar_pbr_materials.cpp`).
pub fn is_texture_name(file_name: &str) -> bool {
    let lower = file_name.to_ascii_lowercase();
    let Some(stem) = lower.strip_suffix(".png") else { return false };
    let Some(rest) = stem.strip_prefix("tex1_") else { return false };
    let digits = |text: &str| -> usize { text.bytes().take_while(u8::is_ascii_digit).count() };
    let width = digits(rest);
    let Some(rest) = rest[width..].strip_prefix('x').filter(|_| width > 0) else { return false };
    let height = digits(rest);
    let Some(rest) = rest[height..].strip_prefix('_').filter(|_| height > 0) else { return false };
    rest.bytes().take_while(u8::is_ascii_hexdigit).count() > 0
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    Png,
    Compressed,
    Json,
    Other,
}

fn kind_of(name: &str) -> Kind {
    let lower = name.to_ascii_lowercase();
    if lower.ends_with(".png") {
        Kind::Png
    } else if lower.ends_with(".dds") || lower.ends_with(".ktx") {
        Kind::Compressed
    } else if lower.ends_with(".json") {
        Kind::Json
    } else {
        Kind::Other
    }
}

/// Characters no file name may carry on any of the platforms.
fn valid_component(name: &str) -> bool {
    !name.is_empty()
        && name != "."
        && name != ".."
        && !name.ends_with(['.', ' '])
        && !name.chars().any(|c| c.is_control() || matches!(c, '<' | '>' | ':' | '"' | '|' | '?' | '*' | '\\' | '/'))
}

/// Where a file of the source is.
#[derive(Clone, Debug)]
enum Origin {
    File(PathBuf),
    Entry(usize),
}

#[derive(Clone, Debug)]
struct Entry {
    components: Vec<String>,
    bytes: u64,
    origin: Origin,
}

fn refuse(message: impl Into<String>) -> ForgeError {
    ForgeError::invalid(message)
}

fn zip_error(error: zip::result::ZipError) -> ForgeError {
    match error {
        zip::result::ZipError::Io(error) => io_error("Cannot read the texture pack archive", error),
        unsupported @ (zip::result::ZipError::UnsupportedArchive(_)
        | zip::result::ZipError::CompressionMethodNotSupported(_)
        | zip::result::ZipError::InvalidPassword) => ForgeError::new(
            ErrorKind::UnsupportedFormat,
            format!("The archive uses a ZIP feature this Forge cannot read ({unsupported}). Extract it with any tool and import the folder."),
        ),
        other => refuse(format!("The texture pack is not a valid ZIP file: {other}")),
    }
}

/// The files of a folder, without following links.
fn folder_entries(folder: &Path, cancel: &CancelToken) -> Result<Vec<Entry>> {
    let mut entries = Vec::new();
    let mut pending = vec![(folder.to_path_buf(), Vec::<String>::new())];
    while let Some((directory, prefix)) = pending.pop() {
        cancel.check()?;
        let listing = fs::read_dir(&directory)
            .map_err(|error| io_error(format!("Cannot read {}", directory.display()), error))?;
        for child in listing {
            let child = child.map_err(|error| io_error(format!("Cannot read {}", directory.display()), error))?;
            let name = child.file_name().to_string_lossy().into_owned();
            let mut components = prefix.clone();
            components.push(name);
            let shown = components.join("/");
            let metadata = fs::symlink_metadata(child.path())
                .map_err(|error| io_error(format!("Cannot read {}", child.path().display()), error))?;
            let kind = metadata.file_type();
            if kind.is_dir() {
                if components.len() > MAXIMUM_DEPTH {
                    return Err(refuse(format!("{shown} is more than {MAXIMUM_DEPTH} folders deep")));
                }
                pending.push((child.path(), components));
            } else if kind.is_file() {
                entries.push(Entry { components, bytes: metadata.len(), origin: Origin::File(child.path()) });
            } else {
                return Err(refuse(format!("{shown} is a link or a special file; texture packs cannot contain links")));
            }
        }
    }
    Ok(entries)
}

fn zip_entries(archive: &mut zip::ZipArchive<BufReader<File>>) -> Result<Vec<Entry>> {
    let mut entries = Vec::new();
    for index in 0..archive.len() {
        let file = archive.by_index(index).map_err(zip_error)?;
        let name = file.name().map(|name| name.into_owned()).unwrap_or_else(|_| format!("entry {index}"));
        let Some(enclosed) = file.enclosed_name() else {
            return Err(refuse(format!("The archive entry {name} points outside the pack")));
        };
        if file.is_symlink() {
            return Err(refuse(format!("{name} is a link; texture packs cannot contain links")));
        }
        if file.is_dir() {
            continue;
        }
        let components: Vec<String> =
            enclosed.components().map(|part| part.as_os_str().to_string_lossy().into_owned()).collect();
        entries.push(Entry { components, bytes: file.size(), origin: Origin::Entry(index) });
    }
    Ok(entries)
}

/// How the source is laid out.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Layout {
    /// The installed title's folder.
    Title,
    /// One folder of another title id, used as it is.
    OtherTitle(String),
    /// No title folder: placed under the installed title once confirmed.
    Flat,
}

impl Layout {
    pub fn code(&self) -> &'static str {
        match self {
            Self::Title => "title",
            Self::OtherTitle(_) => "other_title",
            Self::Flat => "flat",
        }
    }
}

/// The entries of the pack itself, relative to its root.
fn select(entries: Vec<Entry>, source_name: Option<&str>, title_id: &str) -> Result<(Layout, Vec<Entry>)> {
    // Title folders: the first title-id component of each file's path.
    let mut roots: BTreeSet<(Vec<String>, String)> = BTreeSet::new();
    for entry in &entries {
        let parents = &entry.components[..entry.components.len() - 1];
        if let Some(index) = parents.iter().position(|part| is_title_id(part)) {
            roots.insert((parents[..=index].to_vec(), parents[index].to_ascii_uppercase()));
        }
    }
    let strip = |entries: Vec<Entry>, root: &[String]| -> Vec<Entry> {
        entries
            .into_iter()
            .filter(|entry| entry.components.len() > root.len() && entry.components.starts_with(root))
            .map(|entry| Entry { components: entry.components[root.len()..].to_vec(), ..entry })
            .collect()
    };
    if roots.is_empty() {
        // The chosen folder may itself be a title folder (Azahar's own).
        if let Some(name) = source_name.filter(|name| is_title_id(name)) {
            let name = name.to_ascii_uppercase();
            let layout = if name == title_id { Layout::Title } else { Layout::OtherTitle(name) };
            return Ok((layout, entries));
        }
        // Flat: drop the folders every file shares (a wrapper folder).
        let mut common: Vec<String> = match entries.first() {
            Some(first) => first.components[..first.components.len() - 1].to_vec(),
            None => Vec::new(),
        };
        for entry in &entries {
            let parents = &entry.components[..entry.components.len() - 1];
            let shared = common.iter().zip(parents).take_while(|(left, right)| left == right).count();
            common.truncate(shared);
        }
        return Ok((Layout::Flat, strip(entries, &common)));
    }
    let titles: BTreeSet<&String> = roots.iter().map(|(_, title)| title).collect();
    let chosen: Vec<&(Vec<String>, String)> = if titles.len() == 1 {
        roots.iter().collect()
    } else {
        roots.iter().filter(|(_, title)| title == title_id).collect()
    };
    match chosen[..] {
        [(root, title)] => {
            let layout = if title == title_id { Layout::Title } else { Layout::OtherTitle(title.clone()) };
            Ok((layout, strip(entries, root)))
        }
        [] => Err(refuse(format!(
            "The pack has folders for several games ({}) but none for the installed game {title_id}",
            titles.iter().map(|title| title.as_str()).collect::<Vec<_>>().join(", ")
        ))),
        _ => Err(refuse(format!("The pack has more than one folder for {title_id}; import one of them"))),
    }
}

/// What an import found and copied.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ImportSummary {
    pub layout: Layout,
    /// `.png` textures the runtime loads by name or through `pack.json`.
    pub textures: u64,
    /// Files copied (textures, sidecars, `.json`, `.dds`/`.ktx`).
    pub files: u64,
    pub bytes: u64,
    /// `.dds`/`.ktx` files: kept, not loaded yet.
    pub not_loaded: u64,
    /// Other files left out.
    pub skipped: u64,
    pub destination: PathBuf,
}

impl ImportSummary {
    pub fn to_value(&self) -> Value {
        json!({
            "layout": self.layout.code(),
            "source_title": match &self.layout { Layout::OtherTitle(title) => Some(title.as_str()), _ => None },
            "textures": self.textures,
            "files": self.files,
            "bytes": self.bytes,
            "not_loaded": self.not_loaded,
            "skipped": self.skipped,
            "destination": self.destination.to_string_lossy(),
        })
    }
}

/// The source to read: a folder or a zip file.
pub struct Source {
    path: PathBuf,
    archive: Option<zip::ZipArchive<BufReader<File>>>,
    entries: Vec<Entry>,
}

impl Source {
    pub fn open(path: &Path, cancel: &CancelToken) -> Result<Self> {
        let metadata =
            fs::metadata(path).map_err(|error| io_error(format!("Cannot read {}", path.display()), error))?;
        if metadata.is_dir() {
            let entries = folder_entries(path, cancel)?;
            return Ok(Self { path: path.to_path_buf(), archive: None, entries });
        }
        let file = File::open(path).map_err(|error| io_error(format!("Cannot read {}", path.display()), error))?;
        let mut archive = zip::ZipArchive::new(BufReader::new(file)).map_err(zip_error)?;
        let entries = zip_entries(&mut archive)?;
        Ok(Self { path: path.to_path_buf(), archive: Some(archive), entries })
    }

    fn folder_name(&self) -> Option<String> {
        if self.archive.is_some() {
            return None;
        }
        self.path.file_name().map(|name| name.to_string_lossy().into_owned())
    }

    /// The layout an import of this source would use for `title_id`.
    pub fn layout(&self, title_id: &str) -> Result<Layout> {
        Ok(select(self.entries.clone(), self.folder_name().as_deref(), title_id)?.0)
    }

    fn reader(&mut self, origin: &Origin) -> Result<Box<dyn Read + '_>> {
        match (origin, self.archive.as_mut()) {
            (Origin::File(path), _) => Ok(Box::new(
                File::open(path).map_err(|error| io_error(format!("Cannot read {}", path.display()), error))?,
            )),
            (Origin::Entry(index), Some(archive)) => Ok(Box::new(archive.by_index(*index).map_err(zip_error)?)),
            (Origin::Entry(_), None) => Err(ForgeError::internal("archive entry without an archive")),
        }
    }
}

fn png_header(header: &[u8], shown: &str) -> Result<()> {
    if header.len() < 24 || header[..8] != PNG_SIGNATURE || &header[12..16] != b"IHDR" {
        return Err(refuse(format!("{shown} is not a PNG image")));
    }
    let side = |offset: usize| u32::from_be_bytes(header[offset..offset + 4].try_into().expect("four bytes"));
    let (width, height) = (side(16), side(20));
    if !(1..=MAXIMUM_PNG_SIDE).contains(&width) || !(1..=MAXIMUM_PNG_SIDE).contains(&height) {
        return Err(refuse(format!("{shown} is {width}x{height} pixels (at most {MAXIMUM_PNG_SIDE} per side)")));
    }
    Ok(())
}

/// The file names `pack.json`'s `textures` object maps to hashes.
fn mapped_names(document: &serde_json::Map<String, Value>) -> HashSet<String> {
    let mut names = HashSet::new();
    let mut add = |path: &str| {
        if let Some(name) = path.rsplit(['/', '\\']).next().filter(|name| !name.is_empty()) {
            names.insert(name.to_ascii_lowercase());
        }
    };
    if let Some(Value::Object(textures)) = document.get("textures") {
        for value in textures.values() {
            match value {
                Value::String(path) => add(path),
                Value::Array(paths) => paths.iter().filter_map(Value::as_str).for_each(&mut add),
                _ => {}
            }
        }
    }
    names
}

/// Copies one file into the staging folder, checking it on the way.
fn copy_entry(
    reader: &mut dyn Read,
    entry: &Entry,
    kind: Kind,
    target: &Path,
    cancel: &CancelToken,
) -> Result<(u64, Option<serde_json::Map<String, Value>>)> {
    let shown = entry.components.join("/");
    if let Some(parent) = target.parent() {
        fs::create_dir_all(parent).map_err(|error| io_error(format!("Cannot create {}", parent.display()), error))?;
    }
    let mut limited = reader.take(MAXIMUM_FILE_BYTES + 1);
    if kind == Kind::Json {
        let mut bytes = Vec::new();
        (&mut limited)
            .take(MAXIMUM_JSON_BYTES + 1)
            .read_to_end(&mut bytes)
            .map_err(|error| io_error(format!("Cannot read {shown}"), error))?;
        if bytes.len() as u64 > MAXIMUM_JSON_BYTES {
            return Err(refuse(format!("{shown} is larger than {MAXIMUM_JSON_BYTES} bytes")));
        }
        let document = forge_base::legacy_json::load_object(&bytes)
            .map_err(|error| refuse(format!("{shown} is not a JSON object: {error}")))?;
        fs::write(target, &bytes).map_err(|error| io_error(format!("Cannot write {}", target.display()), error))?;
        return Ok((bytes.len() as u64, Some(document)));
    }
    let mut output =
        File::create(target).map_err(|error| io_error(format!("Cannot write {}", target.display()), error))?;
    let mut buffer = vec![0u8; 1 << 16];
    let mut written = 0u64;
    let mut header = Vec::with_capacity(24);
    loop {
        cancel.check()?;
        let read = limited.read(&mut buffer).map_err(|error| io_error(format!("Cannot read {shown}"), error))?;
        if read == 0 {
            break;
        }
        if kind == Kind::Png && header.len() < 24 {
            let take = (24 - header.len()).min(read);
            header.extend_from_slice(&buffer[..take]);
        }
        written += read as u64;
        if written > MAXIMUM_FILE_BYTES {
            return Err(refuse(format!("{shown} is larger than {MAXIMUM_FILE_BYTES} bytes")));
        }
        output
            .write_all(&buffer[..read])
            .map_err(|error| io_error(format!("Cannot write {}", target.display()), error))?;
    }
    if kind == Kind::Png {
        png_header(&header, &shown)?;
    }
    Ok((written, None))
}

/// What [`import`] needs.
pub struct ImportRequest<'a> {
    /// The managed load folder, `D/textures/load/<title id>`.
    pub destination: &'a Path,
    /// The installed title (16 uppercase hex digits).
    pub title_id: &'a str,
    /// The user confirmed placing a flat pack under the installed title.
    pub flat_confirmed: bool,
}

/// Copies the pack into a staging folder beside the destination, verifies
/// it, then swaps it with the current pack. On any failure the current pack
/// is untouched.
pub fn import(source: &mut Source, request: &ImportRequest<'_>, cancel: &CancelToken) -> Result<ImportSummary> {
    let (layout, entries) = select(source.entries.clone(), source.folder_name().as_deref(), request.title_id)?;
    if layout == Layout::Flat && !request.flat_confirmed {
        return Err(refuse("The pack has no folder named after a game; confirm placing it under the installed game"));
    }
    let parent =
        request.destination.parent().ok_or_else(|| ForgeError::internal("The texture folder has no parent folder"))?;
    fs::create_dir_all(parent).map_err(|error| io_error(format!("Cannot create {}", parent.display()), error))?;
    let staging = tempfile::Builder::new()
        .prefix(".import-")
        .tempdir_in(parent)
        .map_err(|error| io_error(format!("Cannot create a staging folder in {}", parent.display()), error))?;
    let mut summary = ImportSummary {
        layout,
        textures: 0,
        files: 0,
        bytes: 0,
        not_loaded: 0,
        skipped: 0,
        destination: request.destination.to_path_buf(),
    };
    let mut pngs: Vec<String> = Vec::new();
    let mut mapped = HashSet::new();
    for entry in &entries {
        cancel.check()?;
        let shown = entry.components.join("/");
        let name = entry.components.last().cloned().unwrap_or_default();
        let kind = kind_of(&name);
        if kind == Kind::Other {
            summary.skipped += 1;
            continue;
        }
        if !entry.components.iter().all(|part| valid_component(part)) {
            return Err(refuse(format!("{shown} has a name that cannot be stored on this system")));
        }
        if entry.bytes > MAXIMUM_FILE_BYTES {
            return Err(refuse(format!("{shown} is larger than {MAXIMUM_FILE_BYTES} bytes")));
        }
        summary.files += 1;
        if summary.files > MAXIMUM_FILES {
            return Err(refuse(format!("The pack has more than {MAXIMUM_FILES} files")));
        }
        let target = entry.components.iter().fold(staging.path().to_path_buf(), |path, part| path.join(part));
        let mut reader = source.reader(&entry.origin)?;
        let (bytes, document) = copy_entry(&mut reader, entry, kind, &target, cancel)?;
        summary.bytes += bytes;
        if summary.bytes > MAXIMUM_TOTAL_BYTES {
            return Err(refuse(format!("The pack is larger than {MAXIMUM_TOTAL_BYTES} bytes")));
        }
        match kind {
            Kind::Png => pngs.push(name.to_ascii_lowercase()),
            Kind::Compressed => summary.not_loaded += 1,
            Kind::Json if entry.components.len() == 1 && name.eq_ignore_ascii_case("pack.json") => {
                mapped = mapped_names(&document.unwrap_or_default());
            }
            _ => {}
        }
    }
    summary.textures = pngs.iter().filter(|name| is_texture_name(name) || mapped.contains(*name)).count() as u64;
    if summary.textures == 0 {
        return Err(refuse("This does not look like a texture pack: it has no tex1_..._....png textures"));
    }
    swap(staging, request.destination)?;
    Ok(summary)
}

/// Puts the verified staging folder in place of `destination`.
fn swap(staging: tempfile::TempDir, destination: &Path) -> Result<()> {
    let previous = if destination.exists() {
        let parent = destination.parent().unwrap_or(Path::new("."));
        let stamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos();
        let previous = parent.join(format!(".previous-{}-{stamp}", std::process::id()));
        fs::rename(destination, &previous)
            .map_err(|error| io_error(format!("Cannot replace {}", destination.display()), error))?;
        Some(previous)
    } else {
        None
    };
    if let Err(error) = fs::rename(staging.path(), destination) {
        if let Some(previous) = &previous {
            let _ = fs::rename(previous, destination);
        }
        return Err(io_error(format!("Cannot store {}", destination.display()), error));
    }
    let _ = staging.keep();
    if let Some(previous) = previous {
        let _ = fs::remove_dir_all(previous);
    }
    Ok(())
}

/// Files below a folder (recursively), at most `limit` counted.
pub fn count_files(folder: &Path, limit: u64) -> u64 {
    let mut count = 0;
    let mut pending = vec![folder.to_path_buf()];
    while let Some(directory) = pending.pop() {
        let Ok(entries) = fs::read_dir(&directory) else { continue };
        for entry in entries.flatten() {
            match entry.file_type() {
                Ok(kind) if kind.is_dir() => pending.push(entry.path()),
                Ok(kind) if kind.is_file() => {
                    count += 1;
                    if count >= limit {
                        return count;
                    }
                }
                _ => {}
            }
        }
    }
    count
}

#[cfg(test)]
mod tests {
    use super::*;

    const TITLE: &str = "0004000000033600";

    fn png(width: u32, height: u32) -> Vec<u8> {
        let mut bytes = PNG_SIGNATURE.to_vec();
        bytes.extend(13u32.to_be_bytes());
        bytes.extend(b"IHDR");
        bytes.extend(width.to_be_bytes());
        bytes.extend(height.to_be_bytes());
        bytes.extend([8, 6, 0, 0, 0, 0, 0, 0, 0]);
        bytes
    }

    fn write(path: &Path, bytes: &[u8]) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, bytes).unwrap();
    }

    fn zip(path: &Path, files: &[(&str, &[u8])]) {
        let mut writer = zip::ZipWriter::new(File::create(path).unwrap());
        for (name, bytes) in files {
            writer.start_file(*name, zip::write::SimpleFileOptions::default()).unwrap();
            writer.write_all(bytes).unwrap();
        }
        writer.finish().unwrap();
    }

    fn import_path(source: &Path, destination: &Path, flat: bool) -> Result<ImportSummary> {
        let cancel = CancelToken::new();
        let mut source = Source::open(source, &cancel)?;
        import(&mut source, &ImportRequest { destination, title_id: TITLE, flat_confirmed: flat }, &cancel)
    }

    const TEXTURE: &str = "tex1_32x32_0123456789ABCDEF_13_mip0.png";

    #[test]
    fn names_and_title_ids() {
        assert!(is_title_id("0004000000033500") && is_title_id("00040000000335aB"));
        assert!(!is_title_id("0004000100033500") && !is_title_id("textures"));
        assert!(is_texture_name(TEXTURE) && is_texture_name("TEX1_8x8_ab.PNG"));
        assert!(!is_texture_name("tex1_x8_ab.png") && !is_texture_name("tex1_8x8_.png") && !is_texture_name("a.png"));
    }

    #[test]
    fn a_zip_with_title_folders_uses_the_installed_title() {
        let root = tempfile::tempdir().unwrap();
        let archive = root.path().join("pack.zip");
        let texture = png(32, 32);
        zip(
            &archive,
            &[
                ("textures/0004000000033600/pack.json", br#"{"options": {}}"#),
                (&format!("textures/0004000000033600/UI/{TEXTURE}"), &texture),
                ("textures/0004000000033500/other.png", &texture),
                ("readme.txt", b"hello"),
            ],
        );
        let destination = root.path().join("data/textures/load").join(TITLE);
        let summary = import_path(&archive, &destination, false).unwrap();
        assert_eq!((summary.layout.clone(), summary.textures, summary.files), (Layout::Title, 1, 2));
        assert_eq!(fs::read(destination.join("UI").join(TEXTURE)).unwrap(), texture);
        assert!(destination.join("pack.json").is_file());
        assert!(!destination.join("other.png").exists());
    }

    #[test]
    fn a_flat_zip_needs_confirmation_and_loses_its_wrapper_folder() {
        let root = tempfile::tempdir().unwrap();
        let archive = root.path().join("flat.zip");
        zip(&archive, &[(&format!("My pack/{TEXTURE}"), &png(32, 32)), ("My pack/notes.txt", b"x")]);
        let destination = root.path().join("load").join(TITLE);
        let error = import_path(&archive, &destination, false).unwrap_err();
        assert!(error.message.contains("confirm"), "{}", error.message);
        assert!(!destination.exists());
        let summary = import_path(&archive, &destination, true).unwrap();
        assert_eq!((summary.layout.clone(), summary.skipped), (Layout::Flat, 1));
        assert!(destination.join(TEXTURE).is_file());
    }

    #[test]
    fn an_invalid_pack_leaves_the_current_one_in_place() {
        let root = tempfile::tempdir().unwrap();
        let destination = root.path().join("load").join(TITLE);
        let good = root.path().join("good").join(TITLE);
        write(&good.join(TEXTURE), &png(32, 32));
        import_path(&good, &destination, false).unwrap();
        // A .png that is not a PNG.
        let bad = root.path().join("bad").join(TITLE);
        write(&bad.join("tex1_8x8_00000000000000AA_13_mip0.png"), b"not a png");
        let error = import_path(&bad, &destination, false).unwrap_err();
        assert!(error.message.contains("is not a PNG image"), "{}", error.message);
        assert!(destination.join(TEXTURE).is_file(), "the previous pack stays");
        // No texture the runtime would load.
        let empty = root.path().join("empty").join(TITLE);
        write(&empty.join("picture.png"), &png(4, 4));
        let error = import_path(&empty, &destination, false).unwrap_err();
        assert!(error.message.starts_with("This does not look like a texture pack"), "{}", error.message);
        // Too large a side.
        let huge = root.path().join("huge").join(TITLE);
        write(&huge.join(TEXTURE), &png(32, 70_000));
        assert!(import_path(&huge, &destination, false).unwrap_err().message.contains("pixels"));
        // Only the previous pack and no leftovers beside it.
        let names: Vec<String> = fs::read_dir(destination.parent().unwrap())
            .unwrap()
            .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
            .collect();
        assert_eq!(names, [TITLE]);
        assert_eq!(count_files(&destination, 10), 1);
    }

    #[test]
    fn a_new_pack_replaces_the_previous_one_whole() {
        let root = tempfile::tempdir().unwrap();
        let destination = root.path().join("load").join(TITLE);
        let first = root.path().join("first");
        write(&first.join(TITLE).join("old").join(TEXTURE), &png(32, 32));
        import_path(&first, &destination, false).unwrap();
        let second = root.path().join("second.zip");
        let mapped = br#"{"textures": {"0123456789abcdef": "UI/menu.png"}}"#;
        zip(
            &second,
            &[
                ("load/textures/0004000000033600/pack.json", mapped),
                ("load/textures/0004000000033600/UI/menu.png", &png(8, 8)),
            ],
        );
        let summary = import_path(&second, &destination, false).unwrap();
        assert_eq!(summary.textures, 1, "mapped by pack.json");
        assert!(!destination.join("old").exists() && destination.join("UI/menu.png").is_file());
    }

    #[test]
    fn another_title_and_ambiguous_packs() {
        let root = tempfile::tempdir().unwrap();
        let usa = root.path().join("usa.zip");
        zip(&usa, &[(&format!("0004000000033500/{TEXTURE}"), &png(32, 32))]);
        let destination = root.path().join("load").join(TITLE);
        let summary = import_path(&usa, &destination, false).unwrap();
        assert_eq!(summary.layout, Layout::OtherTitle("0004000000033500".into()));
        assert_eq!(summary.to_value()["source_title"], "0004000000033500");
        let both = root.path().join("both.zip");
        zip(
            &both,
            &[
                (&format!("0004000000033500/{TEXTURE}"), &png(32, 32)),
                (&format!("00040000000AAAAA/{TEXTURE}"), &png(32, 32)),
            ],
        );
        let error = import_path(&both, &destination, false).unwrap_err();
        assert!(error.message.contains("none for the installed game"), "{}", error.message);
        // The chosen folder itself is a title folder (Azahar's own layout).
        let azahar = root.path().join("Azahar/load/textures/0004000000033600");
        write(&azahar.join(TEXTURE), &png(32, 32));
        assert_eq!(import_path(&azahar, &destination, false).unwrap().layout, Layout::Title);
    }

    #[test]
    fn archive_entries_outside_the_pack_are_refused() {
        let root = tempfile::tempdir().unwrap();
        let archive = root.path().join("evil.zip");
        zip(&archive, &[("../escape.png", &png(32, 32)), (TEXTURE, &png(32, 32))]);
        let error = import_path(&archive, &root.path().join("load").join(TITLE), true).unwrap_err();
        assert!(error.message.contains("points outside the pack"), "{}", error.message);
    }

    #[cfg(unix)]
    #[test]
    fn links_are_refused() {
        let root = tempfile::tempdir().unwrap();
        let source = root.path().join("src").join(TITLE);
        write(&source.join(TEXTURE), &png(32, 32));
        std::os::unix::fs::symlink(root.path(), source.join("loop")).unwrap();
        let error = import_path(&source, &root.path().join("load").join(TITLE), false).unwrap_err();
        assert!(error.message.contains("link"), "{}", error.message);
    }
}
