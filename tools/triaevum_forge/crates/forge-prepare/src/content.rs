// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Port of `forge.py`: input verification, the content key, the title
//! folder, `prepare_content` (process-manifest.json, content.tap and the
//! initial forge-state.json) and `load_prepared_content`.

use std::path::{Path, PathBuf};

use serde_json::{json, Map, Value};

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::hashing::{hex, sha256_file, sha256_hex};
use forge_base::{legacy_json, CancelToken, ErrorKind, ForgeError, Result, LEGACY_TOOL_VERSION};

use crate::hash_cache::HashCache;
use crate::paths::{expanduser, path_string, resolve, resolve_reference, InstallationContext};
use crate::process_manifest::{self, build_manifest};
use crate::pyvalue::{as_python_int, get_eq, get_str, objects_eq, py_eq};

pub const CONTENT_INDEX_FORMAT: &str = "triaevum_content_index_v1";
pub const FORGE_STATE_FORMAT: &str = "triaevum_forge_state_v1";
pub const PATH_MODE: &str = "manifest_relative_v1";
pub const INPUT_KINDS: [&str; 3] = ["code", "exheader", "romfs"];
/// Windows refuses longer paths unless long paths are enabled system-wide,
/// which TriAevum does not require (#103).
pub const WINDOWS_MAX_PATH: usize = 259;
pub const TITLE_DIRECTORY_KEY_DIGITS: usize = 16;
pub const MODULE_NAME_KEY_DIGITS: usize = 32;
const NOT_BUILT_REASON: &str = "direct game.tam code-generation backend is not implemented";

/// `forge.VerifiedInput`: a resolved input with its size and digest.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VerifiedInput {
    pub kind: String,
    pub path: PathBuf,
    pub bytes: u64,
    pub sha256: String,
}

impl VerifiedInput {
    pub fn as_json(&self) -> Result<Value> {
        Ok(json!({"path": path_string(&self.path)?, "bytes": self.bytes, "sha256": self.sha256}))
    }
}

/// The three retained inputs, in the order Python iterated them.
#[derive(Clone, Debug)]
pub struct VerifiedInputs {
    pub code: VerifiedInput,
    pub exheader: VerifiedInput,
    pub romfs: VerifiedInput,
}

impl VerifiedInputs {
    pub fn iter(&self) -> impl Iterator<Item = &VerifiedInput> {
        [&self.code, &self.exheader, &self.romfs].into_iter()
    }
}

fn forge_error(message: impl Into<String>) -> ForgeError {
    ForgeError::invalid(message)
}

fn is_regular_file(path: &Path) -> bool {
    path.is_file() && !path.is_symlink()
}

/// `verify_input`: a regular file whose size and SHA-256 match the contract.
pub fn verify_input(
    kind: &str,
    path: &Path,
    contract: &Map<String, Value>,
    cache: &mut HashCache,
    cancel: &CancelToken,
) -> Result<VerifiedInput> {
    let resolved = resolve(&expanduser(path))?;
    if !is_regular_file(&resolved) {
        return Err(forge_error(format!("{kind} input is not a regular file: {}", path_string(&resolved)?)));
    }
    let size = std::fs::metadata(&resolved)
        .map_err(|error| ForgeError::io(format!("Cannot read {}", resolved.display()), error))?
        .len();
    let expected_bytes = contract.get("bytes");
    // int(contract.get("bytes", -1)); a non-integer contract never matches.
    if expected_bytes.and_then(as_python_int).unwrap_or(-1) != i128::from(size) {
        let shown = expected_bytes.map(crate::pyvalue::py_str).unwrap_or_else(|| "None".to_string());
        return Err(ForgeError::new(
            ErrorKind::UnsupportedRevision,
            format!("{kind} size mismatch: expected {shown}, got {size}"),
        ));
    }
    let digest = cache.digest(&resolved, cancel)?;
    let expected = get_str(contract, "sha256").to_lowercase();
    if digest != expected {
        return Err(ForgeError::new(
            ErrorKind::UnsupportedRevision,
            format!("{kind} SHA-256 mismatch: expected {expected}, got {digest}"),
        ));
    }
    Ok(VerifiedInput { kind: kind.to_string(), path: resolved, bytes: size, sha256: digest })
}

/// Raw input locations handed to `verify_sources`.
#[derive(Clone, Copy, Debug)]
pub struct SourcePaths<'a> {
    pub code: &'a Path,
    pub exheader: &'a Path,
    pub romfs: &'a Path,
}

impl<'a> SourcePaths<'a> {
    fn by_kind(&self) -> [(&'static str, &'a Path); 3] {
        [("code", self.code), ("exheader", self.exheader), ("romfs", self.romfs)]
    }
}

/// The accepted HD UI texture archive (`ui_texture_pack.pack_contract`),
/// resolved by the caller from its catalog; `None` when the recipe does not
/// support the pack.
#[derive(Clone, Debug)]
pub struct ArchiveContract {
    pub kind: String,
    pub sha256: String,
}

/// Verifies an execution-family substitute (`data_compatibility.verify(...,
/// phase="execution")`); supplied by the catalog crate.
pub type FamilyVerifier<'a> = &'a dyn Fn(&Map<String, Value>, &SourcePaths<'_>) -> Result<()>;

/// `verify_sources`. A recipe with `data_compatibility` accepts inputs of
/// its declared execution family through `family`; the UI archive contract
/// comes from the mods catalog. Saves the hash cache on success.
pub fn verify_sources(
    recipe: &Map<String, Value>,
    paths: &SourcePaths<'_>,
    cache: &mut HashCache,
    family: Option<FamilyVerifier<'_>>,
    ui_texture_archive: Option<(&Path, Option<&ArchiveContract>)>,
    cancel: &CancelToken,
) -> Result<(VerifiedInputs, Option<VerifiedInput>)> {
    let Some(Value::Object(declared)) = recipe.get("inputs") else {
        return Err(forge_error("revision recipe input contracts are malformed"));
    };
    let mut contracts = declared.clone();
    if !recipe.get("data_compatibility").unwrap_or(&Value::Null).is_null() {
        let mut actual = Map::new();
        for (kind, path) in paths.by_kind() {
            let size = std::fs::metadata(path)
                .map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?
                .len();
            actual.insert(kind.to_string(), json!({"bytes": size, "sha256": cache.digest(path, cancel)?}));
        }
        if !objects_eq(&actual, &contracts) {
            let verify = family.ok_or_else(|| ForgeError::internal("data-compatibility verifier is not available"))?;
            verify(recipe, paths)?;
            contracts = actual;
        }
    }
    let mut verified = Vec::with_capacity(3);
    for (kind, path) in paths.by_kind() {
        let Some(Value::Object(contract)) = contracts.get(kind) else {
            return Err(forge_error(format!("revision recipe has no {kind} contract")));
        };
        verified.push(verify_input(kind, path, contract, cache, cancel)?);
    }
    let [code, exheader, romfs]: [VerifiedInput; 3] = verified.try_into().expect("three inputs");

    let mut verified_mod = None;
    if let Some((archive, contract)) = ui_texture_archive {
        let contract =
            contract.ok_or_else(|| forge_error("revision recipe does not support the HD UI texture pack"))?;
        let resolved = resolve(&expanduser(archive))?;
        if !is_regular_file(&resolved) {
            return Err(forge_error(format!(
                "HD UI texture pack archive is not a regular file: {}",
                path_string(&resolved)?
            )));
        }
        let digest = cache.digest(&resolved, cancel)?;
        if digest != contract.sha256 {
            return Err(ForgeError::integrity(format!(
                "HD UI texture pack archive SHA-256 mismatch: expected {}, got {digest}",
                contract.sha256
            )));
        }
        let bytes = std::fs::metadata(&resolved)
            .map_err(|error| ForgeError::io(format!("Cannot read {}", resolved.display()), error))?
            .len();
        verified_mod = Some(VerifiedInput { kind: contract.kind.clone(), path: resolved, bytes, sha256: digest });
    }
    cache.save()?;
    Ok((VerifiedInputs { code, exheader, romfs }, verified_mod))
}

/// SHA-256(`triaevum-private-content-v1\0` recipe `\0` code `\0` exheader
/// `\0` romfs), hex digests as ASCII.
pub fn content_key(recipe_id: &str, verified: &VerifiedInputs) -> String {
    let mut preimage = b"triaevum-private-content-v1\0".to_vec();
    preimage.extend_from_slice(recipe_id.as_bytes());
    for input in verified.iter() {
        preimage.push(0);
        preimage.extend_from_slice(input.sha256.as_bytes());
    }
    sha256_hex(&preimage)
}

/// The title's folder: the content key's first 16 digits, or the full key
/// that installations made before 0.8.0-beta.1.2 already use.
pub fn title_directory(output_root: &Path, recipe_id: &str, key: &str) -> Result<PathBuf> {
    let base = resolve(&expanduser(output_root))?.join(recipe_id);
    let legacy = base.join(key);
    if legacy.is_dir() {
        return Ok(legacy);
    }
    Ok(base.join(key.get(..TITLE_DIRECTORY_KEY_DIGITS).unwrap_or(key)))
}

/// Stops before any work when the deepest title file
/// (`titles/<id>/<key>/modules/<key>.tam`) would not fit a Windows path.
pub fn check_title_path_length(output_root: &Path, recipe_ids: &[&str], windows: bool) -> Result<()> {
    if !windows {
        return Ok(());
    }
    let base = resolve(&expanduser(output_root))?;
    let module = format!("{}.tam", "0".repeat(MODULE_NAME_KEY_DIGITS));
    let mut longest = 0usize;
    for recipe_id in recipe_ids {
        let path = base.join(recipe_id).join("0".repeat(TITLE_DIRECTORY_KEY_DIGITS)).join("modules").join(&module);
        // len(str(path)) counts code points.
        longest = longest.max(path_string(&path)?.chars().count());
    }
    if longest > WINDOWS_MAX_PATH {
        return Err(ForgeError::new(
            ErrorKind::InvalidSettings,
            format!(
                "the TriAevum folder's path is too long for Windows: its files would need {longest} characters and \
                 Windows allows {WINDOWS_MAX_PATH}. Move the TriAevum folder to a path at least {} characters \
                 shorter, such as D:\\Games\\TriAevum, and run Forge again.",
                longest - WINDOWS_MAX_PATH
            ),
        ));
    }
    Ok(())
}

/// `_manifest_integer`: a JSON integer (not a bool or float).
pub(crate) fn manifest_integer(value: Option<&Value>, field: &str) -> Result<i128> {
    value.and_then(as_python_int).ok_or_else(|| forge_error(format!("{field} is not an integer")))
}

/// `_validate_process_manifest`: source hashes match the verified inputs,
/// the entrypoint matches the recipe, and the Forge physical mappings
/// (0x20000000 -> linear heap, 0x18000000 -> VRAM) are derivable.
pub fn validate_process_manifest(
    manifest: &Value,
    recipe: &Map<String, Value>,
    verified: &VerifiedInputs,
) -> Result<Vec<Value>> {
    let Value::Object(manifest) = manifest else {
        return Err(forge_error("process manifest builder did not return an object"));
    };
    if manifest.get("format").and_then(Value::as_str) != Some(process_manifest::FORMAT) {
        return Err(forge_error("process manifest builder returned an unsupported format"));
    }
    let (Some(Value::Object(source)), Some(Value::Object(process))) = (manifest.get("source"), manifest.get("process"))
    else {
        return Err(forge_error("process manifest is missing source or process metadata"));
    };
    for (field, expected) in
        [("exheader_sha256", &verified.exheader.sha256), ("code_bin_sha256", &verified.code.sha256)]
    {
        if get_str(source, field).to_lowercase() != *expected {
            return Err(forge_error(format!("process manifest {field} does not match verified input")));
        }
    }
    if let Some(Value::Object(contract)) = recipe.get("process") {
        let expected = manifest_integer(contract.get("entrypoint"), "recipe process entrypoint")?;
        let actual = manifest_integer(process.get("entrypoint"), "process manifest entrypoint")?;
        if actual != expected {
            return Err(forge_error("process manifest entrypoint does not match recipe"));
        }
    }
    let (Some(Value::Object(linear_heap)), Some(Value::Array(system_regions))) =
        (process.get("linear_heap"), process.get("system_regions"))
    else {
        return Err(forge_error("process manifest has no physical-memory layout"));
    };
    let mut regions = vec![(
        0x2000_0000i128,
        manifest_integer(linear_heap.get("base_address"), "linear heap base address")?,
        manifest_integer(linear_heap.get("size"), "linear heap size")?,
    )];
    let vram = system_regions.iter().find_map(|item| match item {
        Value::Object(region) if region.get("name").and_then(Value::as_str) == Some("ctr_vram") => Some(region),
        _ => None,
    });
    let Some(vram) = vram else {
        return Err(forge_error("process manifest has no CTR VRAM region"));
    };
    regions.push((
        0x1800_0000,
        manifest_integer(vram.get("address"), "CTR VRAM address")?,
        manifest_integer(vram.get("mapped_size"), "CTR VRAM mapped size")?,
    ));
    let mut descriptors = Vec::with_capacity(2);
    for (physical, guest, bytes) in regions {
        if !(0..=0xFFFF_FFFF).contains(&guest) || bytes <= 0 || bytes > 0xFFFF_FFFF {
            return Err(forge_error("process manifest physical-memory region is invalid"));
        }
        descriptors.push(json!({"physical_base": physical as u64, "guest_base": guest as u64, "bytes": bytes as u64}));
    }
    Ok(descriptors)
}

/// Builds the process manifest for (exheader, code, romfs). The default is
/// [`build_manifest`]; tests inject synthetic manifests as Python did.
pub type ProcessManifestBuilder<'a> = &'a dyn Fn(&Path, &Path, &Path, &CancelToken) -> Result<Value>;

/// `prepare_content`'s result dictionary.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PrepareResult {
    /// "prepared" or "reused".
    pub status: &'static str,
    pub directory: PathBuf,
    pub content_index: PathBuf,
    pub process_manifest: PathBuf,
    /// The existing module status ("not_built" for a new folder; "unknown"
    /// when a reused state has no module object).
    pub module_status: Value,
}

impl PrepareResult {
    pub fn as_json(&self) -> Result<Value> {
        Ok(json!({
            "status": self.status,
            "directory": path_string(&self.directory)?,
            "content_index": path_string(&self.content_index)?,
            "process_manifest": path_string(&self.process_manifest)?,
            "module_status": self.module_status,
        }))
    }
}

fn initial_state(recipe_id: &str, key: &str) -> Map<String, Value> {
    let state = json!({
        "format": FORGE_STATE_FORMAT,
        "tool_version": LEGACY_TOOL_VERSION,
        "recipe": recipe_id,
        "content_key": key,
        "content": {"status": "ready", "index": "content.tap"},
        "process": {"status": "ready", "manifest": "process-manifest.json"},
        "module": {"status": "not_built", "reason": NOT_BUILT_REASON},
        "pipelines": {"status": "not_built"},
    });
    match state {
        Value::Object(map) => map,
        _ => unreachable!("literal object"),
    }
}

/// `prepare_content`: derives and validates the process manifest, then
/// writes process-manifest.json, content.tap and forge-state.json into a new
/// title folder, or verifies that an existing folder holds exactly the same
/// index, state identity and manifest ("reused"). New folders use
/// `manifest_relative_v1`; an existing legacy absolute-path folder is
/// compared in its own spelling and never rewritten.
pub fn prepare_content(
    recipe: &Map<String, Value>,
    verified: &VerifiedInputs,
    output_root: &Path,
    verified_mod: Option<&VerifiedInput>,
    process_manifest_builder: Option<ProcessManifestBuilder<'_>>,
    cancel: &CancelToken,
) -> Result<PrepareResult> {
    let recipe_id = recipe_id(recipe)?;
    let key = content_key(&recipe_id, verified);
    let title_root = title_directory(output_root, &recipe_id, &key)?;
    let index_path = title_root.join("content.tap");
    let state_path = title_root.join("forge-state.json");
    let manifest_path = title_root.join("process-manifest.json");
    let built = match process_manifest_builder {
        Some(builder) => builder(&verified.exheader.path, &verified.code.path, &verified.romfs.path, cancel),
        None => build_manifest(&verified.exheader.path, &verified.code.path, &verified.romfs.path, cancel),
    };
    let mut manifest = built.map_err(|error| match error.kind {
        ErrorKind::Cancelled => error,
        kind => ForgeError::new(kind, format!("cannot derive CTR process manifest: {}", error.message)),
    })?;
    let physical_memory_regions = validate_process_manifest(&manifest, recipe, verified)?;

    // Preserve legacy manifests exactly; new installations use file-relative paths.
    let portable = !title_root.exists() || {
        let existing = load_json_object(&index_path).map_err(incomplete)?;
        existing.get("path_mode").and_then(Value::as_str) == Some(PATH_MODE)
    };
    let context = InstallationContext::new(
        resolve(&expanduser(output_root))?.parent().map(Path::to_path_buf).unwrap_or_default(),
    );
    if portable {
        let mut scopes = Map::new();
        let source = manifest["source"].as_object_mut().expect("validated source object");
        for field in ["code_bin_path", "exheader_path", "romfs_image_path"] {
            if let Some(Value::String(value)) = source.get(field) {
                let source_path = PathBuf::from(value);
                let reference = context.reference(&source_path, &title_root)?;
                scopes.insert(field.to_string(), json!(context.scope(&source_path, Some(&title_root))?.as_str()));
                source.insert(field.to_string(), json!(reference));
            }
        }
        manifest["source_path_scopes"] = Value::Object(scopes);
    }
    let manifest_bytes = legacy_json::pretty_file_bytes(&manifest);

    let mut inputs = Map::new();
    for input in verified.iter() {
        inputs.insert(input.kind.clone(), input.as_json()?);
    }
    let mut optional_inputs = Map::new();
    if let Some(input) = verified_mod {
        optional_inputs.insert(input.kind.clone(), input.as_json()?);
    }
    let mut index = json!({
        "format": CONTENT_INDEX_FORMAT,
        "private_local_artifact": true,
        "redistributable": false,
        "recipe": recipe_id,
        "content_key": key,
        "inputs": inputs,
        "optional_inputs": optional_inputs,
        "process_manifest": {
            "path": path_string(&manifest_path)?,
            "bytes": manifest_bytes.len(),
            "sha256": sha256_hex(&manifest_bytes),
        },
        "physical_memory_regions": physical_memory_regions,
    });
    if portable {
        index["path_mode"] = json!(PATH_MODE);
        index["process_manifest"]["path"] = json!("process-manifest.json");
        for group in ["inputs", "optional_inputs"] {
            for descriptor in index[group].as_object_mut().expect("object").values_mut() {
                let source_path = PathBuf::from(descriptor["path"].as_str().expect("string path"));
                descriptor["path"] = json!(context.reference(&source_path, &title_root)?);
                descriptor["path_scope"] = json!(context.scope(&source_path, Some(&title_root))?.as_str());
            }
        }
    }
    let state = initial_state(&recipe_id, &key);

    if title_root.exists() {
        let existing_index = load_json_object(&index_path).map_err(incomplete)?;
        let existing_state = load_json_object(&state_path).map_err(incomplete)?;
        let existing_manifest = load_json_object(&manifest_path).map_err(incomplete)?;
        let state_matches = ["format", "recipe", "content_key", "content", "process"]
            .iter()
            .all(|field| get_eq(&existing_state, field, &state[*field]));
        if !py_eq(&Value::Object(existing_index), &index)
            || !state_matches
            || !py_eq(&Value::Object(existing_manifest), &manifest)
        {
            return Err(ForgeError::integrity(format!(
                "existing Forge output does not match inputs: {}",
                path_string(&title_root)?
            )));
        }
        let module_status = match existing_state.get("module") {
            Some(Value::Object(module)) => module.get("status").cloned().unwrap_or(Value::Null),
            _ => json!("unknown"),
        };
        return Ok(PrepareResult {
            status: "reused",
            directory: title_root,
            content_index: index_path,
            process_manifest: manifest_path,
            module_status,
        });
    }

    let parent = title_root.parent().expect("title folder has a parent");
    std::fs::create_dir_all(parent)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    std::fs::create_dir(&title_root)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", title_root.display()), error))?;
    let written = forge_base::fsutil::atomic_write_bytes(&manifest_path, &manifest_bytes)
        .and_then(|()| atomic_write_json(&index_path, &index))
        .and_then(|()| atomic_write_json(&state_path, &Value::Object(state)));
    if let Err(error) = written {
        for path in [&manifest_path, &index_path, &state_path] {
            let _ = std::fs::remove_file(path);
        }
        let _ = std::fs::remove_dir(&title_root);
        return Err(error);
    }
    Ok(PrepareResult {
        status: "prepared",
        directory: title_root,
        content_index: index_path,
        process_manifest: manifest_path,
        module_status: json!("not_built"),
    })
}

fn recipe_id(recipe: &Map<String, Value>) -> Result<String> {
    // str(recipe["id"])
    recipe.get("id").map(crate::pyvalue::py_str).ok_or_else(|| forge_error("revision recipe has no id"))
}

fn incomplete(error: ForgeError) -> ForgeError {
    ForgeError::new(error.kind, format!("existing Forge output is incomplete: {}", error.message))
}

/// `forge.PreparedForgeContent`.
#[derive(Clone, Debug)]
pub struct PreparedContent {
    pub directory: PathBuf,
    pub index: Map<String, Value>,
    pub state: Map<String, Value>,
    pub inputs: Vec<VerifiedInput>,
}

impl PreparedContent {
    pub fn input(&self, kind: &str) -> Option<&VerifiedInput> {
        self.inputs.iter().find(|input| input.kind == kind)
    }
}

/// `load_prepared_content`: checks formats and state/index agreement, that
/// the indexed process manifest is the folder's own and unchanged, and
/// rehashes each required input.
pub fn load_prepared_content(
    prepared_directory: &Path,
    required_inputs: &[&str],
    cancel: &CancelToken,
) -> Result<PreparedContent> {
    let title_root = resolve(&expanduser(prepared_directory))?;
    if !title_root.is_dir() || title_root.is_symlink() {
        return Err(forge_error(format!("prepared directory is invalid: {}", path_string(&title_root)?)));
    }
    let index_path = title_root.join("content.tap");
    let state_path = title_root.join("forge-state.json");
    let manifest_path = title_root.join("process-manifest.json");
    let loaded = load_json_object(&index_path).and_then(|index| Ok((index, load_json_object(&state_path)?)));
    let (index, state) = loaded.map_err(|error| {
        ForgeError::new(error.kind, format!("prepared Forge output is incomplete: {}", error.message))
    })?;
    if index.get("format").and_then(Value::as_str) != Some(CONTENT_INDEX_FORMAT)
        || index.get("private_local_artifact") != Some(&Value::Bool(true))
        || index.get("redistributable") != Some(&Value::Bool(false))
        || state.get("format").and_then(Value::as_str) != Some(FORGE_STATE_FORMAT)
    {
        return Err(ForgeError::new(ErrorKind::UnsupportedFormat, "prepared Forge output has an unsupported format"));
    }
    let ready = |section: &str| match state.get(section) {
        Some(Value::Object(map)) => get_eq(map, "status", &json!("ready")),
        _ => false,
    };
    if !get_eq(&state, "recipe", index.get("recipe").unwrap_or(&Value::Null))
        || !get_eq(&state, "content_key", index.get("content_key").unwrap_or(&Value::Null))
        || !ready("content")
        || !ready("process")
    {
        return Err(ForgeError::integrity("Forge state does not match its prepared content"));
    }
    let (Some(Value::Object(process_descriptor)), Some(Value::Object(input_descriptors))) =
        (index.get("process_manifest"), index.get("inputs"))
    else {
        return Err(forge_error("content.tap is missing process or input metadata"));
    };
    let indexed_process = resolve_reference(&get_str(process_descriptor, "path"), &title_root)?;
    if indexed_process != resolve(&manifest_path)? {
        return Err(ForgeError::integrity("content.tap process manifest escapes its prepared directory"));
    }
    let (process_bytes, process_digest) = std::fs::metadata(&manifest_path)
        .map_err(|error| ForgeError::io("cannot verify process manifest", error))
        .and_then(|metadata| Ok((metadata.len(), sha256_file(&manifest_path, cancel)?.1)))
        .map_err(|error| match error.kind {
            ErrorKind::Cancelled => error,
            _ => ForgeError::new(error.kind, prefixed("cannot verify process manifest", &error.message)),
        })?;
    if !get_eq(process_descriptor, "bytes", &json!(process_bytes))
        || hex(&process_digest) != get_str(process_descriptor, "sha256").to_lowercase()
    {
        return Err(ForgeError::integrity("process manifest changed after content preparation"));
    }

    let mut inputs = Vec::with_capacity(required_inputs.len());
    for kind in required_inputs {
        let Some(Value::Object(descriptor)) = input_descriptors.get(*kind) else {
            return Err(forge_error(format!("content.tap is missing {kind} input metadata")));
        };
        let path = resolve_reference(&get_str(descriptor, "path"), &title_root)?;
        let expected_bytes = descriptor.get("bytes").unwrap_or(&Value::Null);
        let expected_hash = get_str(descriptor, "sha256").to_lowercase();
        let valid = (|| -> Result<Option<u64>> {
            if !is_regular_file(&path) {
                return Ok(None);
            }
            let size = std::fs::metadata(&path)
                .map_err(|error| ForgeError::io(format!("cannot verify prepared {kind} input"), error))?
                .len();
            if !py_eq(&json!(size), expected_bytes) {
                return Ok(None);
            }
            let digest = sha256_file(&path, cancel).map_err(|error| match error.kind {
                ErrorKind::Cancelled => error,
                _ => ForgeError::new(
                    error.kind,
                    prefixed(&format!("cannot verify prepared {kind} input"), &error.message),
                ),
            })?;
            Ok((hex(&digest.1) == expected_hash).then_some(size))
        })()?;
        let Some(size) = valid else {
            return Err(ForgeError::integrity(format!("{kind} input changed after content preparation")));
        };
        inputs.push(VerifiedInput { kind: kind.to_string(), path, bytes: size, sha256: expected_hash });
    }
    Ok(PreparedContent { directory: title_root, index, state, inputs })
}

fn prefixed(prefix: &str, message: &str) -> String {
    if message.starts_with(prefix) {
        message.to_string()
    } else {
        format!("{prefix}: {message}")
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    fn digest(data: &[u8]) -> String {
        sha256_hex(data)
    }

    /// test_forge.ForgeTests fixture: three small private inputs and a recipe.
    pub(crate) struct Fixture {
        pub _directory: tempfile::TempDir,
        pub root: PathBuf,
        pub code: PathBuf,
        pub exheader: PathBuf,
        pub romfs: PathBuf,
        pub output: PathBuf,
        pub recipe: Map<String, Value>,
    }

    pub(crate) fn fixture() -> Fixture {
        let directory = tempfile::tempdir().unwrap();
        let root = resolve(directory.path()).unwrap();
        let private = root.join("private");
        std::fs::create_dir(&private).unwrap();
        let mut recipe_inputs = Map::new();
        let mut paths = Vec::new();
        for (kind, name, bytes) in [
            ("code", "code.fixture", &b"code-fixture"[..]),
            ("exheader", "exheader.fixture", b"exheader-fixture"),
            ("romfs", "romfs.fixture", b"romfs-fixture"),
        ] {
            let path = private.join(name);
            std::fs::write(&path, bytes).unwrap();
            recipe_inputs.insert(kind.to_string(), json!({"bytes": bytes.len(), "sha256": digest(bytes)}));
            paths.push(path);
        }
        let recipe = json!({"id": "fixture", "inputs": recipe_inputs}).as_object().unwrap().clone();
        let [code, exheader, romfs]: [PathBuf; 3] = paths.try_into().unwrap();
        let output = root.join("forge-output");
        Fixture { _directory: directory, root, code, exheader, romfs, output, recipe }
    }

    impl Fixture {
        pub fn verify(&self) -> VerifiedInputs {
            let mut cache = HashCache::new(self.output.join(".hash-cache.json"), true);
            let paths = SourcePaths { code: &self.code, exheader: &self.exheader, romfs: &self.romfs };
            verify_sources(&self.recipe, &paths, &mut cache, None, None, &CancelToken::new()).unwrap().0
        }

        pub fn prepare(&self, builder: ProcessManifestBuilder<'_>) -> Result<PrepareResult> {
            prepare_content(&self.recipe, &self.verify(), &self.output, None, Some(builder), &CancelToken::new())
        }
    }

    /// test_forge.build_process_manifest.
    pub(crate) fn synthetic_manifest(exheader: &Path, code: &Path, romfs: &Path, _: &CancelToken) -> Result<Value> {
        let read = |path: &Path| std::fs::read(path).unwrap();
        Ok(json!({
            "format": "oot3d_native_process_manifest_v1",
            "source": {
                "exheader_path": path_string(&resolve(exheader)?)?,
                "exheader_sha256": digest(&read(exheader)),
                "code_bin_path": path_string(&resolve(code)?)?,
                "code_bin_sha256": digest(&read(code)),
                "romfs_image_path": path_string(&resolve(romfs)?)?,
            },
            "process": {
                "entrypoint": 0x0010_0000,
                "linear_heap": {"base_address": 0x1400_0000, "size": 0x0400_0000},
                "system_regions": [{"name": "ctr_vram", "address": 0x1F00_0000, "mapped_size": 0x0060_0000}],
            },
        }))
    }

    #[test]
    fn prepares_index_without_copying_private_inputs() {
        let fixture = fixture();
        let result = fixture.prepare(&synthetic_manifest).unwrap();
        assert_eq!(result.status, "prepared");
        let mut names: Vec<String> = std::fs::read_dir(&result.directory)
            .unwrap()
            .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
            .collect();
        names.sort();
        assert_eq!(names, ["content.tap", "forge-state.json", "process-manifest.json"]);
        let index = load_json_object(&result.directory.join("content.tap")).unwrap();
        assert_eq!(index["redistributable"], false);
        let code = index["inputs"]["code"].as_object().unwrap();
        assert_eq!(resolve_reference(code["path"].as_str().unwrap(), &result.directory).unwrap(), fixture.code);
        assert_eq!(code["path_scope"], "relative");
        assert_eq!(
            index["physical_memory_regions"],
            json!([
                {"physical_base": 0x2000_0000, "guest_base": 0x1400_0000, "bytes": 0x0400_0000},
                {"physical_base": 0x1800_0000, "guest_base": 0x1F00_0000, "bytes": 0x0060_0000},
            ])
        );
        let manifest = std::fs::read(result.directory.join("process-manifest.json")).unwrap();
        assert_eq!(index["process_manifest"]["sha256"], digest(&manifest));
    }

    #[test]
    fn title_folder_is_short_and_keeps_an_existing_full_key_folder() {
        let fixture = fixture();
        let key = content_key("fixture", &fixture.verify());
        let result = fixture.prepare(&synthetic_manifest).unwrap();
        assert_eq!(result.directory, fixture.output.join("fixture").join(&key[..16]));
        let legacy = fixture.output.join("fixture").join(&key);
        std::fs::rename(&result.directory, &legacy).unwrap();
        let reused = fixture.prepare(&synthetic_manifest).unwrap();
        assert_eq!((reused.status, reused.directory), ("reused", legacy));
    }

    #[test]
    fn path_length_check_names_the_characters_to_remove() {
        let recipes = ["oot3d-eur-project-baseline-16a6b0aa"];
        check_title_path_length(Path::new("/x/TriAevum/data/titles"), &recipes, true).unwrap();
        let deep = PathBuf::from(format!("/x/{}/data/titles", "d".repeat(150)));
        let message = check_title_path_length(&deep, &recipes, true).unwrap_err().message;
        assert!(message.contains("too long for Windows") && message.contains("characters shorter"), "{message}");
        check_title_path_length(&deep, &recipes, false).unwrap();
    }

    #[test]
    fn prepared_content_survives_installation_move() {
        let fixture = fixture();
        let result = fixture.prepare(&synthetic_manifest).unwrap();
        let moved_parent = tempfile::tempdir().unwrap();
        let moved = resolve(moved_parent.path()).unwrap().join("installation with spaces \u{e8}");
        copy_tree(&fixture.root, &moved);
        for path in [&fixture.code, &fixture.exheader, &fixture.romfs] {
            std::fs::remove_file(path).unwrap();
        }
        let directory = moved.join(result.directory.strip_prefix(&fixture.root).unwrap());
        let prepared = load_prepared_content(&directory, &INPUT_KINDS, &CancelToken::new()).unwrap();
        assert_eq!(prepared.input("code").unwrap().path, moved.join("private").join("code.fixture"));
        let manifest = load_json_object(&directory.join("process-manifest.json")).unwrap();
        for key in ["code_bin_path", "exheader_path", "romfs_image_path"] {
            assert!(resolve_reference(manifest["source"][key].as_str().unwrap(), &directory).unwrap().is_file());
        }
    }

    fn copy_tree(from: &Path, to: &Path) {
        std::fs::create_dir_all(to).unwrap();
        for entry in std::fs::read_dir(from).unwrap() {
            let entry = entry.unwrap();
            let target = to.join(entry.file_name());
            if entry.file_type().unwrap().is_dir() {
                copy_tree(&entry.path(), &target);
            } else {
                std::fs::copy(entry.path(), target).unwrap();
            }
        }
    }

    #[test]
    fn reuses_identical_and_rejects_different_output() {
        let fixture = fixture();
        assert_eq!(fixture.prepare(&synthetic_manifest).unwrap().status, "prepared");
        let reused = fixture.prepare(&synthetic_manifest).unwrap();
        assert_eq!((reused.status, reused.module_status), ("reused", json!("not_built")));
        let different = |e: &Path, c: &Path, r: &Path, t: &CancelToken| {
            let mut manifest = synthetic_manifest(e, c, r, t)?;
            manifest["process"]["entrypoint"] = json!(0x0010_1000);
            Ok(manifest)
        };
        let message = fixture.prepare(&different).unwrap_err().message;
        assert!(message.starts_with("existing Forge output does not match inputs: "), "{message}");
        std::fs::remove_file(reused.directory.join("forge-state.json")).unwrap();
        let message = fixture.prepare(&synthetic_manifest).unwrap_err().message;
        assert!(message.starts_with("existing Forge output is incomplete: "), "{message}");
    }

    #[test]
    fn rejects_wrong_input_identity() {
        let fixture = fixture();
        std::fs::write(&fixture.code, b"wrong").unwrap();
        let mut cache = HashCache::new(fixture.output.join(".hash-cache.json"), false);
        let paths = SourcePaths { code: &fixture.code, exheader: &fixture.exheader, romfs: &fixture.romfs };
        let error = verify_sources(&fixture.recipe, &paths, &mut cache, None, None, &CancelToken::new()).unwrap_err();
        assert_eq!(error.message, "code size mismatch: expected 12, got 5");
        std::fs::write(&fixture.code, b"wrong-fixtur").unwrap();
        let error = verify_sources(&fixture.recipe, &paths, &mut cache, None, None, &CancelToken::new()).unwrap_err();
        assert!(error.message.starts_with("code SHA-256 mismatch: expected "), "{}", error.message);
    }

    #[test]
    fn rejects_unsupported_archives_and_family_inputs() {
        let fixture = fixture();
        let paths = SourcePaths { code: &fixture.code, exheader: &fixture.exheader, romfs: &fixture.romfs };
        let mut cache = HashCache::new(fixture.output.join(".hash-cache.json"), false);
        let cancel = CancelToken::new();
        let archive = fixture.root.join("pack.zip");
        std::fs::write(&archive, b"zip").unwrap();
        let error =
            verify_sources(&fixture.recipe, &paths, &mut cache, None, Some((&archive, None)), &cancel).unwrap_err();
        assert_eq!(error.message, "revision recipe does not support the HD UI texture pack");
        let contract = ArchiveContract { kind: "ui_texture_archive".to_string(), sha256: digest(b"zip") };
        let (_, verified_mod) =
            verify_sources(&fixture.recipe, &paths, &mut cache, None, Some((&archive, Some(&contract))), &cancel)
                .unwrap();
        assert_eq!(verified_mod.unwrap().bytes, 3);

        let mut family_recipe = fixture.recipe.clone();
        family_recipe.insert("data_compatibility".to_string(), json!({"family": "x"}));
        family_recipe["inputs"]["code"]["sha256"] = json!("0".repeat(64));
        let error = verify_sources(&family_recipe, &paths, &mut cache, None, None, &cancel).unwrap_err();
        assert_eq!(error.kind, ErrorKind::Internal);
        let accept = |_: &Map<String, Value>, _: &SourcePaths<'_>| Ok(());
        verify_sources(&family_recipe, &paths, &mut cache, Some(&accept), None, &cancel).unwrap();
        let refuse =
            |_: &Map<String, Value>, _: &SourcePaths<'_>| Err(ForgeError::invalid("Incompatible execution data: code"));
        let error = verify_sources(&family_recipe, &paths, &mut cache, Some(&refuse), None, &cancel).unwrap_err();
        assert_eq!(error.message, "Incompatible execution data: code");
    }

    #[test]
    fn rejects_process_manifest_for_different_code() {
        let fixture = fixture();
        let mismatched = |e: &Path, c: &Path, r: &Path, t: &CancelToken| {
            let mut manifest = synthetic_manifest(e, c, r, t)?;
            manifest["source"]["code_bin_sha256"] = json!("0".repeat(64));
            Ok(manifest)
        };
        assert_eq!(
            fixture.prepare(&mismatched).unwrap_err().message,
            "process manifest code_bin_sha256 does not match verified input"
        );
    }

    #[test]
    fn rejects_malformed_process_memory_layout() {
        let fixture = fixture();
        type Mutation = dyn Fn(&mut Value);
        let cases: [(&Mutation, &str); 6] = [
            (&|m| m["process"]["linear_heap"]["size"] = Value::Null, "linear heap size is not an integer"),
            (&|m| m["process"]["linear_heap"]["size"] = json!(true), "linear heap size is not an integer"),
            (&|m| m["process"]["linear_heap"]["size"] = json!(0), "process manifest physical-memory region is invalid"),
            (&|m| m["process"]["system_regions"] = json!([]), "process manifest has no CTR VRAM region"),
            (&|m| m["process"]["system_regions"] = json!({}), "process manifest has no physical-memory layout"),
            (&|m| m["format"] = json!("other"), "process manifest builder returned an unsupported format"),
        ];
        for (mutate, expected) in cases {
            let builder = |e: &Path, c: &Path, r: &Path, t: &CancelToken| {
                let mut manifest = synthetic_manifest(e, c, r, t)?;
                mutate(&mut manifest);
                Ok(manifest)
            };
            assert_eq!(fixture.prepare(&builder).unwrap_err().message, expected);
        }
        let mut recipe = fixture.recipe.clone();
        recipe.insert("process".to_string(), json!({"entrypoint": 0x0020_0000}));
        let error = prepare_content(
            &recipe,
            &fixture.verify(),
            &fixture.output,
            None,
            Some(&synthetic_manifest),
            &CancelToken::new(),
        )
        .unwrap_err();
        assert_eq!(error.message, "process manifest entrypoint does not match recipe");
        let failing = |_: &Path, _: &Path, _: &Path, _: &CancelToken| Err(ForgeError::invalid("ExHeader is truncated"));
        assert_eq!(
            fixture.prepare(&failing).unwrap_err().message,
            "cannot derive CTR process manifest: ExHeader is truncated"
        );
        assert!(!fixture.output.join("fixture").exists());
    }

    #[test]
    fn loading_rejects_tampered_output() {
        let fixture = fixture();
        let result = fixture.prepare(&synthetic_manifest).unwrap();
        let cancel = CancelToken::new();
        load_prepared_content(&result.directory, &INPUT_KINDS, &cancel).unwrap();
        let state_path = result.directory.join("forge-state.json");
        let state = std::fs::read(&state_path).unwrap();
        let mut changed = load_json_object(&state_path).unwrap();
        changed["content_key"] = json!("other");
        atomic_write_json(&state_path, &Value::Object(changed)).unwrap();
        assert_eq!(
            load_prepared_content(&result.directory, &[], &cancel).unwrap_err().message,
            "Forge state does not match its prepared content"
        );
        std::fs::write(&state_path, state).unwrap();
        std::fs::write(&fixture.code, b"changed-code").unwrap();
        assert_eq!(
            load_prepared_content(&result.directory, &["code"], &cancel).unwrap_err().message,
            "code input changed after content preparation"
        );
        std::fs::write(result.directory.join("process-manifest.json"), b"{}").unwrap();
        assert_eq!(
            load_prepared_content(&result.directory, &[], &cancel).unwrap_err().message,
            "process manifest changed after content preparation"
        );
        let index_path = result.directory.join("content.tap");
        let mut index = load_json_object(&index_path).unwrap();
        index["process_manifest"]["path"] = json!("../process-manifest.json");
        atomic_write_json(&index_path, &Value::Object(index.clone())).unwrap();
        assert_eq!(
            load_prepared_content(&result.directory, &[], &cancel).unwrap_err().message,
            "content.tap process manifest escapes its prepared directory"
        );
        index["redistributable"] = json!(0);
        atomic_write_json(&index_path, &Value::Object(index)).unwrap();
        assert_eq!(
            load_prepared_content(&result.directory, &[], &cancel).unwrap_err().message,
            "prepared Forge output has an unsupported format"
        );
        assert!(load_prepared_content(&fixture.root.join("missing"), &[], &cancel)
            .unwrap_err()
            .message
            .starts_with("prepared directory is invalid: "));
    }
}
