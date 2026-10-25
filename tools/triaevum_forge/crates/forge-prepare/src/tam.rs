// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Port of `tools/triaevum_release/tam_builder.py`: the private TAM module
//! container (`modules/<32-hex>.tam`). Header, section table, 16-byte
//! alignment, compact sorted metadata and section order are byte-identical
//! to `build_tam`.
//!
//! Deliberate difference: Python read both images into memory. Here the
//! images are hashed in one pass and streamed into the output in a second;
//! a file that changes between the passes is refused instead of producing a
//! container whose descriptors do not match its payload.

use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};

use serde_json::{json, Value};
use sha2::{Digest, Sha256};

use forge_base::hashing::{copy_hashing, hex, sha256_bytes, sha256_file};
use forge_base::{legacy_json, CancelToken, ForgeError, Result, LEGACY_TOOL_VERSION};

use crate::paths::{expanduser, path_string, resolve};

pub const MAGIC: &[u8; 8] = b"TRIAEVM1";
pub const FORMAT_MAJOR: u16 = 1;
pub const FORMAT_MINOR: u16 = 1;
pub const HEADER_SIZE: u64 = 64;
pub const SECTION_ENTRY_SIZE: u64 = 64;
pub const SECTION_ALIGNMENT: u64 = 16;
pub const RUNTIME_ABI_V1: i128 = 1;
pub const PICA_SERVICE_V1: u32 = 0x4143_4950;
pub const AUDIO_SERVICE_V1: u32 = 0x4944_5541;
pub const INPUT_SERVICE_V1: u32 = 0x3344_4948;
pub const FILESYSTEM_SERVICE_V1: u32 = 0x5359_5346;
pub const MAXIMUM_PHYSICAL_MEMORY_REGIONS: usize = 64;
pub const SECTION_METADATA_JSON: u32 = 1;
pub const SECTION_NATIVE_IMAGE: u32 = 2;
pub const SECTION_TITLE_AOT_IMAGE: u32 = 3;
pub const SECTION_REQUIRED: u32 = 1 << 0;
pub const SECTION_EXECUTABLE: u32 = 1 << 1;
pub const SUPPORTED_TARGET_TRIPLES: [&str; 6] = [
    "aarch64-apple-darwin",
    "aarch64-pc-windows-msvc",
    "aarch64-unknown-linux-gnu",
    "x86_64-apple-darwin",
    "x86_64-pc-windows-msvc",
    "x86_64-unknown-linux-gnu",
];

/// `build_tam` arguments. Integers are wide so out-of-range metadata values
/// reach the same refusals as in Python instead of failing to convert.
#[derive(Clone, Debug)]
pub struct TamRequest<'a> {
    pub native_image: &'a Path,
    pub title_aot_image: Option<&'a Path>,
    pub recipe: &'a str,
    pub target_triple: &'a str,
    pub source_identity: &'a str,
    pub translator_identity: &'a str,
    pub required_services: &'a [i128],
    pub physical_memory_regions: &'a [(i128, i128, i128)],
    pub runtime_abi: i128,
}

enum Payload {
    Inline(Vec<u8>),
    File { path: PathBuf, label: &'static str },
}

struct Section {
    kind: u32,
    flags: u32,
    offset: u64,
    bytes: u64,
    digest: [u8; 32],
    payload: Payload,
}

/// A planned container: identities are known, payload bytes are streamed by
/// [`Tam::write_to`].
pub struct Tam {
    metadata: Value,
    minor: u16,
    runtime_abi: u32,
    sections: Vec<Section>,
    total: u64,
}

fn invalid(message: impl Into<String>) -> ForgeError {
    ForgeError::invalid(message)
}

fn align(value: u64) -> u64 {
    (value + SECTION_ALIGNMENT - 1) & !(SECTION_ALIGNMENT - 1)
}

fn identity(value: &str, label: &str) -> Result<String> {
    let lowered = value.to_lowercase();
    if lowered.len() != 64 || !lowered.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b)) {
        return Err(invalid(format!("{label} must be a lowercase or uppercase SHA-256")));
    }
    Ok(lowered)
}

/// `_normalize_physical_memory_regions`: bounded count, 32-bit ranges, no
/// physical or guest overlap; returned in tuple order.
pub fn normalize_physical_memory_regions(regions: &[(i128, i128, i128)]) -> Result<Vec<(i128, i128, i128)>> {
    if regions.len() > MAXIMUM_PHYSICAL_MEMORY_REGIONS {
        return Err(invalid("too many physical memory regions"));
    }
    const SPACE: i128 = 1 << 32;
    const MAX: i128 = 0xFFFF_FFFF;
    for &(physical, guest, bytes) in regions {
        if physical < 0
            || guest < 0
            || bytes <= 0
            || physical > MAX
            || guest > MAX
            || bytes > MAX
            || physical + bytes > SPACE
            || guest + bytes > SPACE
        {
            return Err(invalid("physical memory region is outside the 32-bit address space"));
        }
    }
    let overlaps = |left: &(i128, i128, i128), right: &(i128, i128, i128), physical: bool| {
        let (left_base, right_base) = if physical { (left.0, right.0) } else { (left.1, right.1) };
        left_base < right_base + right.2 && right_base < left_base + left.2
    };
    for (index, region) in regions.iter().enumerate() {
        for previous in &regions[..index] {
            if overlaps(region, previous, true) || overlaps(region, previous, false) {
                return Err(invalid("physical memory regions overlap"));
            }
        }
    }
    let mut sorted = regions.to_vec();
    sorted.sort_unstable();
    Ok(sorted)
}

/// Resolves an image and hashes it: (resolved path, bytes, digest).
fn image(path: &Path, missing: &str, empty: &str, cancel: &CancelToken) -> Result<(PathBuf, u64, [u8; 32])> {
    let resolved = resolve(&expanduser(path))?;
    if !resolved.is_file() {
        return Err(invalid(format!("{missing}: {}", path_string(&resolved)?)));
    }
    let (bytes, digest) = sha256_file(&resolved, cancel)?;
    if bytes == 0 {
        return Err(invalid(empty));
    }
    Ok((resolved, bytes, digest))
}

impl Tam {
    /// `build_tam`: validates the request in Python's order, hashes the
    /// images and lays out the container.
    pub fn build(request: &TamRequest<'_>, cancel: &CancelToken) -> Result<Self> {
        let native_path = resolve(&expanduser(request.native_image))?;
        if !native_path.is_file() {
            return Err(invalid(format!("native module image does not exist: {}", path_string(&native_path)?)));
        }
        if request.recipe.trim().is_empty() || request.target_triple.trim().is_empty() {
            return Err(invalid("recipe and target triple must not be empty"));
        }
        if !SUPPORTED_TARGET_TRIPLES.contains(&request.target_triple) {
            return Err(invalid(format!("unsupported target triple: {}", request.target_triple)));
        }
        let source_identity = identity(request.source_identity, "source identity")?;
        let translator_identity = identity(request.translator_identity, "translator identity")?;
        if request.runtime_abi != RUNTIME_ABI_V1 {
            return Err(invalid(format!("unsupported runtime ABI: {}", request.runtime_abi)));
        }
        let mut services = request.required_services.to_vec();
        services.sort_unstable();
        services.dedup();
        if services.len() != request.required_services.len()
            || services.iter().any(|&service| service <= 0 || service > 0xFFFF_FFFF)
        {
            return Err(invalid("required service IDs must be unique nonzero uint32 values"));
        }
        let regions = normalize_physical_memory_regions(request.physical_memory_regions)?;
        if services.contains(&i128::from(PICA_SERVICE_V1)) && regions.is_empty() {
            return Err(invalid("PICA service requires at least one physical memory region"));
        }

        let (native_path, native_bytes, native_digest) =
            image(request.native_image, "native module image does not exist", "native module image is empty", cancel)?;
        let title_aot = match request.title_aot_image {
            Some(path) => Some(image(path, "title AOT image does not exist", "title AOT image is empty", cancel)?),
            None => None,
        };

        let mut metadata = json!({
            "format": "triaevum_module_metadata_v1",
            "private_local_artifact": true,
            "redistributable": false,
            "runtime_abi": 1,
            "query_symbol": "TriAevumQueryModuleV1",
            "recipe": request.recipe,
            "target_triple": request.target_triple,
            "source_identity_sha256": source_identity,
            "translator_identity_sha256": translator_identity,
            "native_image": {"bytes": native_bytes, "sha256": hex(&native_digest)},
            "forge_tool_version": LEGACY_TOOL_VERSION,
            "required_services": services.iter().map(|&id| json!({"id": id as u64, "schema_version": 1})).collect::<Vec<_>>(),
            "physical_memory_regions": regions.iter().map(|&(physical, guest, bytes)| json!({
                "physical_base": physical as u64, "guest_base": guest as u64, "bytes": bytes as u64,
            })).collect::<Vec<_>>(),
        });
        if let Some((_, bytes, digest)) = &title_aot {
            metadata["title_aot_image"] = json!({"abi": 1, "bytes": bytes, "sha256": hex(digest)});
        }
        let metadata_bytes = legacy_json::compact_line_bytes(&metadata);

        let executable = SECTION_REQUIRED | SECTION_EXECUTABLE;
        let mut planned = vec![
            (SECTION_METADATA_JSON, SECTION_REQUIRED, metadata_bytes.len() as u64, sha256_bytes(&metadata_bytes), Payload::Inline(metadata_bytes)),
            (SECTION_NATIVE_IMAGE, executable, native_bytes, native_digest, Payload::File { path: native_path, label: "native module image" }),
        ];
        let minor = if let Some((path, bytes, digest)) = title_aot {
            planned.push((SECTION_TITLE_AOT_IMAGE, executable, bytes, digest, Payload::File { path, label: "title AOT image" }));
            FORMAT_MINOR
        } else {
            0
        };
        let mut cursor = align(HEADER_SIZE + planned.len() as u64 * SECTION_ENTRY_SIZE);
        let mut sections = Vec::with_capacity(planned.len());
        for (kind, flags, bytes, digest, payload) in planned {
            cursor = align(cursor);
            sections.push(Section { kind, flags, offset: cursor, bytes, digest, payload });
            cursor += bytes;
        }
        Ok(Self { metadata, minor, runtime_abi: 1, sections, total: cursor })
    }

    pub fn metadata(&self) -> &Value {
        &self.metadata
    }

    /// Exact container length in bytes.
    pub fn len(&self) -> u64 {
        self.total
    }

    pub fn is_empty(&self) -> bool {
        self.total == 0
    }

    fn header_and_table(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity((HEADER_SIZE + self.sections.len() as u64 * SECTION_ENTRY_SIZE) as usize);
        out.extend_from_slice(MAGIC);
        out.extend_from_slice(&FORMAT_MAJOR.to_le_bytes());
        out.extend_from_slice(&self.minor.to_le_bytes());
        out.extend_from_slice(&(HEADER_SIZE as u16).to_le_bytes());
        out.extend_from_slice(&(SECTION_ENTRY_SIZE as u16).to_le_bytes());
        out.extend_from_slice(&self.runtime_abi.to_le_bytes());
        out.extend_from_slice(&(self.sections.len() as u32).to_le_bytes());
        out.extend_from_slice(&self.total.to_le_bytes());
        out.extend_from_slice(&HEADER_SIZE.to_le_bytes());
        out.extend_from_slice(&[0u8; 24]);
        for section in &self.sections {
            out.extend_from_slice(&section.kind.to_le_bytes());
            out.extend_from_slice(&section.flags.to_le_bytes());
            out.extend_from_slice(&section.offset.to_le_bytes());
            out.extend_from_slice(&section.bytes.to_le_bytes());
            out.extend_from_slice(&section.digest);
            out.extend_from_slice(&0u64.to_le_bytes());
        }
        out
    }

    /// Streams the container into `writer` and returns its SHA-256.
    pub fn write_to(&self, writer: &mut impl Write, cancel: &CancelToken) -> Result<[u8; 32]> {
        let mut output = HashingWriter { inner: writer, hasher: Sha256::new(), written: 0 };
        let write_error = |error| ForgeError::io("Cannot write the TAM module", error);
        output.write_all(&self.header_and_table()).map_err(write_error)?;
        for section in &self.sections {
            let padding = section.offset - output.written;
            output.write_all(&vec![0u8; padding as usize]).map_err(write_error)?;
            match &section.payload {
                Payload::Inline(bytes) => output.write_all(bytes).map_err(write_error)?,
                Payload::File { path, label } => {
                    let mut source =
                        File::open(path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
                    let (copied, digest) = copy_hashing(&mut source, &mut output, section.bytes, cancel)?;
                    if copied != section.bytes || digest != section.digest {
                        return Err(ForgeError::integrity(format!("{label} changed while the TAM module was written")));
                    }
                }
            }
        }
        output.flush().map_err(write_error)?;
        Ok(output.hasher.finalize().into())
    }

    /// The whole container in memory (tests and small images).
    pub fn to_bytes(&self, cancel: &CancelToken) -> Result<Vec<u8>> {
        let mut bytes = Vec::with_capacity(usize::try_from(self.total).unwrap_or(0));
        self.write_to(&mut bytes, cancel)?;
        Ok(bytes)
    }
}

struct HashingWriter<'a, W: Write> {
    inner: &'a mut W,
    hasher: Sha256,
    written: u64,
}

impl<W: Write> Write for HashingWriter<'_, W> {
    fn write(&mut self, buffer: &[u8]) -> std::io::Result<usize> {
        let count = self.inner.write(buffer)?;
        self.hasher.update(&buffer[..count]);
        self.written += count as u64;
        Ok(count)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.inner.flush()
    }
}

/// Result of [`write_tam_file`], the fields `tam_builder.main` printed.
#[derive(Debug)]
pub struct TamFile {
    pub output: PathBuf,
    pub bytes: u64,
    pub sha256: String,
    pub metadata: Value,
}

/// `tam_builder.main`: refuses an existing output or a stale
/// `.<name>.tmp`, writes the temporary file and renames it into place.
pub fn write_tam_file(output: &Path, request: &TamRequest<'_>, cancel: &CancelToken) -> Result<TamFile> {
    let output = resolve(&expanduser(output))?;
    if output.exists() {
        return Err(invalid(format!("TAM output already exists: {}", path_string(&output)?)));
    }
    let tam = Tam::build(request, cancel)?;
    let parent = output.parent().ok_or_else(|| invalid("TAM output has no parent directory"))?;
    std::fs::create_dir_all(parent).map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    let name = output.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
    let temporary = output.with_file_name(format!(".{name}.tmp"));
    if temporary.exists() {
        return Err(invalid(format!("stale TAM temporary output exists: {}", path_string(&temporary)?)));
    }
    let written = (|| {
        let mut file = File::create(&temporary)
            .map_err(|error| ForgeError::io(format!("Cannot write {}", temporary.display()), error))?;
        let digest = tam.write_to(&mut file, cancel)?;
        file.sync_all().map_err(|error| ForgeError::io(format!("Cannot write {}", temporary.display()), error))?;
        drop(file);
        std::fs::rename(&temporary, &output)
            .map_err(|error| ForgeError::io(format!("Cannot replace {}", output.display()), error))?;
        Ok(digest)
    })();
    let digest = match written {
        Ok(digest) => digest,
        Err(error) => {
            // Python left the temporary behind on failure; a later run then
            // refused it as stale. Removing our own partial file is safer.
            let _ = std::fs::remove_file(&temporary);
            return Err(error);
        }
    };
    Ok(TamFile { output, bytes: tam.len(), sha256: hex(&digest), metadata: tam.metadata })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tam_reader::{parse_tam, TamErrorCode};

    fn request<'a>(native: &'a Path, title_aot: Option<&'a Path>) -> TamRequest<'a> {
        TamRequest {
            native_image: native,
            title_aot_image: title_aot,
            recipe: "fixture",
            target_triple: "x86_64-pc-windows-msvc",
            source_identity: "a",
            translator_identity: "b",
            required_services: &[],
            physical_memory_regions: &[],
            runtime_abi: RUNTIME_ABI_V1,
        }
    }

    fn hashes() -> (String, String) {
        ("a".repeat(64), "B".repeat(64))
    }

    #[test]
    fn builds_canonical_hashed_sections() {
        let directory = tempfile::tempdir().unwrap();
        let native = directory.path().join("module.dll");
        std::fs::write(&native, b"MZ native test module").unwrap();
        let (source, translator) = hashes();
        let regions = [(0x2000_0000, 0x1400_0000, 0x0800_0000), (0x1800_0000, 0x1F00_0000, 0x0060_0000)];
        let tam = Tam::build(
            &TamRequest {
                source_identity: &source,
                translator_identity: &translator,
                required_services: &[0x4944_5541, 0x4143_4950],
                physical_memory_regions: &regions,
                ..request(&native, None)
            },
            &CancelToken::new(),
        )
        .unwrap();
        let bytes = tam.to_bytes(&CancelToken::new()).unwrap();
        assert_eq!(bytes.len() as u64, tam.len());
        let view = parse_tam(&bytes, 1).unwrap();
        assert_eq!(view.format_minor, 0);
        assert_eq!(view.sections.len(), 2);
        let metadata: Value = serde_json::from_slice(view.sections[0].bytes).unwrap();
        assert_eq!(&metadata, tam.metadata());
        assert_eq!(metadata["translator_identity_sha256"], "b".repeat(64));
        assert_eq!(metadata["required_services"][0]["id"], 0x4143_4950);
        assert_eq!(metadata["physical_memory_regions"][0]["physical_base"], 0x1800_0000);
        assert_eq!(view.sections[1].bytes, b"MZ native test module");
    }

    #[test]
    fn bundles_hashed_title_aot_companion() {
        let directory = tempfile::tempdir().unwrap();
        let native = directory.path().join("native.dll");
        let title = directory.path().join("title.dll");
        std::fs::write(&native, b"MZ generic runtime module").unwrap();
        std::fs::write(&title, b"MZ private title AOT").unwrap();
        let (source, translator) = hashes();
        let tam = Tam::build(
            &TamRequest { source_identity: &source, translator_identity: &translator, ..request(&native, Some(&title)) },
            &CancelToken::new(),
        )
        .unwrap();
        let bytes = tam.to_bytes(&CancelToken::new()).unwrap();
        let view = parse_tam(&bytes, 1).unwrap();
        assert_eq!((view.format_minor, view.sections.len()), (1, 3));
        assert_eq!(view.sections[2].bytes, b"MZ private title AOT");
        assert_eq!(tam.metadata()["title_aot_image"]["abi"], 1);
        assert_eq!(parse_tam(&bytes, 2).unwrap_err().code, TamErrorCode::IncompatibleRuntimeAbi);
    }

    #[test]
    fn rejects_invalid_requests_with_python_messages() {
        let directory = tempfile::tempdir().unwrap();
        let native = directory.path().join("module.dll");
        std::fs::write(&native, b"MZ").unwrap();
        let empty = directory.path().join("empty.dll");
        std::fs::write(&empty, b"").unwrap();
        let (source, translator) = hashes();
        let base = TamRequest { source_identity: &source, translator_identity: &translator, ..request(&native, None) };
        let cancel = CancelToken::new();
        let message = |request: TamRequest<'_>| Tam::build(&request, &cancel).err().unwrap().message;
        assert_eq!(
            message(TamRequest { source_identity: "not-a-hash", ..base.clone() }),
            "source identity must be a lowercase or uppercase SHA-256"
        );
        assert_eq!(message(TamRequest { target_triple: "host-default", ..base.clone() }), "unsupported target triple: host-default");
        assert_eq!(message(TamRequest { target_triple: " ", ..base.clone() }), "recipe and target triple must not be empty");
        assert_eq!(message(TamRequest { runtime_abi: 2, ..base.clone() }), "unsupported runtime ABI: 2");
        assert_eq!(
            message(TamRequest { required_services: &[0x4143_4950, 0x4143_4950], ..base.clone() }),
            "required service IDs must be unique nonzero uint32 values"
        );
        assert_eq!(
            message(TamRequest { required_services: &[0x1_0000_0000], ..base.clone() }),
            "required service IDs must be unique nonzero uint32 values"
        );
        assert_eq!(
            message(TamRequest { required_services: &[0x4143_4950], ..base.clone() }),
            "PICA service requires at least one physical memory region"
        );
        assert_eq!(
            message(TamRequest {
                physical_memory_regions: &[(0x1800_0000, 0x1F00_0000, 0x1000), (0x1800_0800, 0x1400_0000, 0x1000)],
                ..base.clone()
            }),
            "physical memory regions overlap"
        );
        assert_eq!(
            message(TamRequest { physical_memory_regions: &[(0xFFFF_F000, 0, 0x2000)], ..base.clone() }),
            "physical memory region is outside the 32-bit address space"
        );
        assert_eq!(
            message(TamRequest { physical_memory_regions: &[(0, 0, 1); 65], ..base.clone() }),
            "too many physical memory regions"
        );
        assert_eq!(message(TamRequest { native_image: &empty, ..base.clone() }), "native module image is empty");
        assert_eq!(message(TamRequest { title_aot_image: Some(&empty), ..base.clone() }), "title AOT image is empty");
        assert!(message(TamRequest { native_image: &directory.path().join("missing"), ..base.clone() })
            .starts_with("native module image does not exist: "));
    }

    #[test]
    fn file_writer_refuses_existing_output_and_stale_temporary() {
        let directory = tempfile::tempdir().unwrap();
        let native = directory.path().join("module.dll");
        std::fs::write(&native, b"MZ").unwrap();
        let (source, translator) = hashes();
        let request = TamRequest { source_identity: &source, translator_identity: &translator, ..request(&native, None) };
        let cancel = CancelToken::new();
        let output = directory.path().join("out").join("game.tam");
        let written = write_tam_file(&output, &request, &cancel).unwrap();
        let bytes = std::fs::read(&output).unwrap();
        assert_eq!((written.bytes, written.sha256), (bytes.len() as u64, forge_base::hashing::sha256_hex(&bytes)));
        let again = write_tam_file(&output, &request, &cancel).unwrap_err().message;
        assert!(again.starts_with("TAM output already exists: "), "{again}");
        let second = directory.path().join("out").join("second.tam");
        std::fs::write(directory.path().join("out").join(".second.tam.tmp"), b"").unwrap();
        let stale = write_tam_file(&second, &request, &cancel).unwrap_err().message;
        assert!(stale.starts_with("stale TAM temporary output exists: "), "{stale}");
    }
}
