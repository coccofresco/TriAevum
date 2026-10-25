// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `shader_corpus_layout.py`: publisher binding and release-audit checks of
//! the bundled portable shader corpus, never a driver cache. A corpus is
//! bound only with its qualification record (issue #76), checked against
//! the publisher's checkout ([`bind_shader_corpus`]) or against the
//! corresponding source archive the package ships
//! ([`validate_bundled_corpus`]). Includes the qualification checks of
//! `shader_corpus_qualification.py` those two need.

use std::collections::HashSet;
use std::io::Read;
use std::path::{Path, PathBuf};

use forge_base::hashing::{hex, sha256_bytes, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_catalog::paths::join_posix;
use forge_catalog::pyjson::{eq_str, get, index, py_eq, py_str, truthy};
use forge_catalog::{checked_file, for_target, WINDOWS};
use serde_json::{json, Map, Value};

use crate::o3ps::{decode_file, Module, MAX_BYTES};
use crate::pyerr::{io_failure, json_loads, load_json_object, os_error};

pub const QUALIFICATION_PATH: &str = "forge/shader-corpus/qualification.json";
pub const QUALIFICATION_FORMAT: &str = "triaevum_shader_corpus_qualification_v1";
pub const INPUTS_FORMAT: &str = "triaevum_build_inputs_v1";
const PIPELINE_MANIFEST_BYTES: u64 = 16 * 1024 * 1024;

fn invalid(message: impl Into<String>) -> ForgeError {
    ForgeError::new(ErrorKind::ArtifactIntegrity, message)
}

fn is_symlink(path: &Path) -> bool {
    std::fs::symlink_metadata(path).is_ok_and(|metadata| metadata.file_type().is_symlink())
}

fn file_size(path: &Path) -> Result<u64> {
    std::fs::metadata(path).map(|metadata| metadata.len()).map_err(|error| {
        let message = os_error(&error, Some(path));
        io_failure(error, message)
    })
}

fn file_sha256(path: &Path) -> Result<String> {
    Ok(hex(&sha256_file(path, &CancelToken::new())?.1))
}

/// `read_portable_corpus(pack)`: the canonical layout is checked
/// structurally; returns the descriptor schema and the table.
pub fn read_portable_corpus(pack: &Path) -> Result<(u32, Vec<Module>)> {
    if is_symlink(pack) || !pack.is_file() || file_size(pack)? > MAX_BYTES {
        return Err(invalid("Invalid portable corpus file"));
    }
    decode_file(pack, true).map_err(|error| {
        if error.message.contains("canonical") {
            invalid("Public corpus must be canonical, without unclaimed data")
        } else {
            error
        }
    })
}

/// `read_pipeline_manifest(source, schema)`: an observed
/// `oot3d_pica_pipeline_manifest_v2` with this descriptor schema.
pub fn read_pipeline_manifest(source: &Path, schema: u32) -> Result<Map<String, Value>> {
    if is_symlink(source) || file_size(source)? > PIPELINE_MANIFEST_BYTES {
        return Err(invalid("Invalid pipeline manifest file"));
    }
    let data = load_json_object(source)?;
    let keys = ["format", "schema_version", "descriptor_schema_version", "pipeline_count", "pipelines"];
    let pipelines = match data.get("pipelines") {
        Some(Value::Array(pipelines)) if !pipelines.is_empty() => Some(pipelines.len()),
        _ => None,
    };
    let compatible = data.len() == keys.len()
        && keys.iter().all(|key| data.contains_key(*key))
        && eq_str(get(&data, "format"), "oot3d_pica_pipeline_manifest_v2")
        && py_eq(get(&data, "schema_version"), &json!(2))
        && py_eq(get(&data, "descriptor_schema_version"), &json!(schema))
        && pipelines.is_some_and(|count| py_eq(get(&data, "pipeline_count"), &json!(count)));
    if !compatible {
        return Err(invalid("Incompatible observed pipeline manifest"));
    }
    Ok(data)
}

/// `inputs_digest(manifest)`: SHA-256 over `path\0sha\n` in path order.
pub fn inputs_digest(files: &Map<String, Value>) -> String {
    let mut entries: Vec<(&String, &Value)> = files.iter().collect();
    entries.sort_by(|left, right| left.0.as_bytes().cmp(right.0.as_bytes()));
    let mut preimage = Vec::new();
    for (path, digest) in entries {
        preimage.extend_from_slice(format!("{path}\0{}\n", py_str(digest)).as_bytes());
    }
    hex(&sha256_bytes(&preimage))
}

fn checked_inputs<'a>(record: Option<&'a Value>, what: &str) -> Result<&'a Map<String, Value>> {
    let files = match record {
        Some(Value::Object(record)) if eq_str(get(record, "format"), INPUTS_FORMAT) => match record.get("files") {
            Some(Value::Object(files)) if !files.is_empty() && eq_str(get(record, "sha256"), &inputs_digest(files)) => {
                Some(files)
            }
            _ => None,
        },
        _ => None,
    };
    files.ok_or_else(|| invalid(format!("Invalid {what} input record")))
}

/// Reads a repository-relative file, `None` when it does not exist.
pub type Reader<'a> = &'a dyn Fn(&str) -> Option<Vec<u8>>;

/// `repository_reader(root)`.
pub fn repository_reader(root: &Path) -> impl Fn(&str) -> Option<Vec<u8>> + '_ {
    move |path| {
        let candidate = root.join(path);
        if candidate.is_file() {
            std::fs::read(candidate).ok()
        } else {
            None
        }
    }
}

/// `changed_inputs(manifest, read)`: listed files missing or changed, in
/// record order.
fn changed_inputs(files: &Map<String, Value>, read: Reader<'_>) -> Vec<String> {
    files
        .iter()
        .filter(|(path, digest)| match read(path) {
            Some(data) => !eq_str(digest, &hex(&sha256_bytes(&data))),
            None => true,
        })
        .map(|(path, _)| path.clone())
        .collect()
}

/// `validate_qualification(record, pack_sha256=..., manifest_sha256=...,
/// read=...)`: refuses a corpus that is not the one qualified, or not for
/// these sources.
pub fn validate_qualification(
    record: &Value,
    pack_sha256: &str,
    manifest_sha256: &[String],
    read: Reader<'_>,
) -> Result<()> {
    let record = match record {
        Value::Object(record) if eq_str(get(record, "format"), QUALIFICATION_FORMAT) => record,
        _ => return Err(invalid("Shader corpus has no qualification record")),
    };
    if !eq_str(get(record, "pack_sha256"), pack_sha256) {
        return Err(invalid("Shader corpus differs from its qualification record"));
    }
    let mut sorted: Vec<String> = manifest_sha256.to_vec();
    sorted.sort();
    if !py_eq(get(record, "pipeline_manifest_sha256"), &json!(sorted)) {
        return Err(invalid("Pipeline recipes differ from the shader corpus qualification"));
    }
    if !truthy(get(record, "variant_requests")) || !truthy(get(record, "policy_inputs")) {
        return Err(invalid("Shader corpus was not regenerated from recorded variant sessions"));
    }
    let stale = changed_inputs(checked_inputs(record.get("generator_inputs"), "generator")?, read);
    if let Some(first) = stale.first() {
        return Err(invalid(format!(
            "Stale shader corpus: {} generator inputs changed since regeneration (first: {first}); regenerate it",
            stale.len()
        )));
    }
    // Python iterated whatever it found; anything but a list of records fails.
    let policies = match &record["policy_inputs"] {
        Value::Array(policies) => policies.clone(),
        _ => vec![Value::Null],
    };
    for policy in &policies {
        let stale = changed_inputs(checked_inputs(Some(policy), "decision-policy")?, read);
        if let Some(first) = stale.first() {
            return Err(invalid(format!(
                "Stale variant recordings: {} runtime inputs changed since recording (first: {first}); \
                 record the sessions again",
                stale.len()
            )));
        }
    }
    Ok(())
}

fn role<'a>(declared: &'a Map<String, Value>, path: &Value) -> &'a Value {
    match path.as_str().and_then(|path| declared.get(path)) {
        Some(Value::Object(record)) => get(record, "role"),
        _ => &Value::Null,
    }
}

fn read_zip_member(archive: &mut zip::ZipArchive<std::fs::File>, name: &str) -> Option<Vec<u8>> {
    let mut member = archive.by_name(name).ok()?;
    let mut data = Vec::new();
    member.read_to_end(&mut data).ok()?;
    Some(data)
}

/// `_validate_against_source_archive`: the qualification checked against
/// the one corresponding-source archive the package declares.
fn validate_against_source_archive(
    root: &Path,
    declared: &Map<String, Value>,
    record: &Value,
    pack_sha256: &str,
    manifest_sha256: &[String],
) -> Result<()> {
    let mut sources = Vec::new();
    for (path, item) in declared {
        if eq_str(index(item, "role")?, "corresponding_source") {
            sources.push(path);
        }
    }
    if sources.len() != 1 {
        return Err(invalid("Shader corpus qualification requires the corresponding source archive"));
    }
    let path = join_posix(root, sources[0]);
    let file = std::fs::File::open(&path).map_err(|error| {
        let message = crate::pyerr::open_error(&error, &path);
        io_failure(error, message)
    })?;
    let archive = zip::ZipArchive::new(file)
        .map_err(|error| invalid(format!("Invalid corresponding source archive {}: {error}", path.display())))?;
    let names: HashSet<String> =
        archive.file_names().filter_map(|name| name.ok()).map(|name| name.into_owned()).collect();
    let archive = std::cell::RefCell::new(archive);
    let read = |name: &str| {
        if names.contains(name) {
            read_zip_member(&mut archive.borrow_mut(), name)
        } else {
            None
        }
    };
    validate_qualification(record, pack_sha256, manifest_sha256, &read)
}

fn record_str<'a>(record: &'a Value, key: &str) -> Result<&'a Value> {
    index(record, key)
}

/// `validate_bundled_corpus(root, catalog, declared)`: every title binds the
/// one declared, canonical, qualified corpus with its helper and recipes,
/// and no corpus artifact is left uncatalogued.
pub fn validate_bundled_corpus(root: &Path, catalog: &Map<String, Value>, declared: &Map<String, Value>) -> Result<()> {
    const ROLES: [&str; 3] = ["portable_shader_corpus", "portable_pipeline_recipes", "portable_shader_qualification"];
    let mut actual = HashSet::new();
    for (path, record) in declared {
        if index(record, "role")?.as_str().is_some_and(|role| ROLES.contains(&role)) {
            actual.insert(path.clone());
        }
    }
    if actual.is_empty() {
        return Ok(());
    }
    let cancel = CancelToken::new();
    let empty = Value::Object(Map::new());
    let mut expected = HashSet::new();
    let mut qualified = HashSet::new();
    for title in forge_catalog::pyjson::list(index(&Value::Object(catalog.clone()), "titles")?, "titles")? {
        let seed = title.get("shader_preparation").unwrap_or(&empty);
        let device = title.get("device_pipeline_preparation").unwrap_or(&empty);
        if !eq_str(seed.get("mode").unwrap_or(&Value::Null), "portable_pack")
            || !eq_str(device.get("format").unwrap_or(&Value::Null), crate::device::FORMAT)
        {
            return Err(invalid("Every title requires the bundled corpus and GPU preparation"));
        }
        let pack = record_str(seed, "pack")?;
        let pack_path = record_str(pack, "path")?;
        if !eq_str(role(declared, pack_path), "portable_shader_corpus") {
            return Err(invalid("Portable corpus lacks its explicit release role"));
        }
        let (schema, _) = read_portable_corpus(&checked_file(root, pack, &cancel)?)?;
        if !py_eq(&json!(schema), record_str(seed, "descriptor_schema_version")?) {
            return Err(invalid("Portable corpus descriptor schema mismatch"));
        }
        expected.insert(py_str(pack_path));
        let helper = record_str(device, "helper")?;
        checked_file(root, helper, &cancel)?;
        if !eq_str(role(declared, record_str(helper, "path")?), "shader_preparation_tool") {
            return Err(invalid("Pipeline helper lacks its explicit release role"));
        }
        let manifests = device.get("manifests").unwrap_or(&Value::Null);
        if !truthy(manifests) {
            return Err(invalid("Bundled corpus requires observed pipeline recipes"));
        }
        let manifests = forge_catalog::pyjson::list(manifests, "manifests")?;
        let mut manifest_sha256 = Vec::new();
        for record in manifests {
            let path = record_str(record, "path")?;
            if !eq_str(role(declared, path), "portable_pipeline_recipes") {
                return Err(invalid("Pipeline recipes lack their explicit release role"));
            }
            read_pipeline_manifest(&checked_file(root, record, &cancel)?, schema)?;
            expected.insert(py_str(path));
            manifest_sha256.push(py_str(record_str(record, "sha256")?));
        }
        let qualification = match seed.get("qualification") {
            Some(qualification @ Value::Object(_)) => qualification,
            _ => return Err(invalid("Bundled corpus lacks its qualification record")),
        };
        let qualification_path = record_str(qualification, "path")?;
        if !eq_str(role(declared, qualification_path), "portable_shader_qualification") {
            return Err(invalid("Shader corpus qualification lacks its explicit release role"));
        }
        let record_path = checked_file(root, qualification, &cancel)?;
        let text = std::fs::read(&record_path).map_err(|error| {
            let message = crate::pyerr::open_error(&error, &record_path);
            io_failure(error, message)
        })?;
        let text = String::from_utf8(text).map_err(|_| invalid("Shader corpus qualification is not UTF-8"))?;
        let record = json_loads(&text).map_err(invalid)?;
        let pack_sha256 = py_str(record_str(pack, "sha256")?);
        let key = (pack_sha256.clone(), py_str(record_str(qualification, "sha256")?), manifest_sha256.clone());
        if !qualified.contains(&key) {
            validate_against_source_archive(root, declared, &record, &pack_sha256, &manifest_sha256)?;
            qualified.insert(key);
        }
        expected.insert(py_str(qualification_path));
    }
    if actual != expected {
        return Err(invalid("Uncatalogued portable shader artifact"));
    }
    Ok(())
}

/// `precompiled_title_layout.artifact(path, destination)`.
fn artifact(path: &Path, destination: &str) -> Result<Value> {
    if is_symlink(path) || !path.is_file() {
        return Err(invalid(format!("Invalid release artifact: {}", path.display())));
    }
    Ok(json!({"path": destination, "sha256": file_sha256(path)?, "bytes": file_size(path)?}))
}

/// One package file the publisher copies: `{source, path, role}`.
struct Items {
    items: Vec<Value>,
    destinations: HashSet<String>,
}

impl Items {
    fn add(&mut self, source: &Path, path: &str, role: &str) -> Result<Value> {
        let record = artifact(source, path)?;
        // str.casefold() for the ASCII package paths used here.
        if !self.destinations.insert(path.to_lowercase()) {
            return Err(invalid(format!("Duplicate corpus artifact: {path}")));
        }
        let source = forge_prepare::paths::resolve(source)?;
        self.items.push(json!({"source": source.to_string_lossy(), "path": path, "role": role}));
        Ok(record)
    }
}

/// `bind_shader_corpus(catalog, pack, manifests, helper, dependencies,
/// qualification=..., source_root=...)`: binds one qualified corpus to every
/// supported recipe without modifying the inputs. Returns the new catalog
/// and the package items to copy.
pub fn bind_shader_corpus(
    catalog: &Map<String, Value>,
    pack: &Path,
    manifests: &[PathBuf],
    helper: &Path,
    dependencies: &[PathBuf],
    qualification: &Path,
    source_root: &Path,
) -> Result<(Map<String, Value>, Vec<Value>)> {
    let target = catalog.get("target").ok_or_else(|| invalid("Shader corpus catalog has no target"))?;
    let platform = for_target(&py_str(target))?;
    if !truthy(get(catalog, "titles")) {
        return Err(invalid("Shader corpus requires title recipes"));
    }
    // Never guess a corpus from a directory or import a GPU-cache binary.
    let (schema, _) = read_portable_corpus(pack)?;
    if !(1..=64).contains(&manifests.len()) {
        return Err(invalid("Shader corpus requires observed pipeline manifests"));
    }
    let mut items = Items { items: Vec::new(), destinations: HashSet::new() };
    let packed = items.add(pack, "forge/shader-corpus/portable.o3ps", "portable_shader_corpus")?;
    let mut pipeline_records = Vec::new();
    for (position, source) in manifests.iter().enumerate() {
        read_pipeline_manifest(source, schema)?;
        let path = format!("forge/shader-corpus/pipelines-{position}.json");
        pipeline_records.push(items.add(source, &path, "portable_pipeline_recipes")?);
    }
    // Refuse a corpus not generated from, and recorded with, these sources.
    let manifest_sha256: Vec<String> = pipeline_records.iter().map(|record| py_str(&record["sha256"])).collect();
    let reader = repository_reader(source_root);
    validate_qualification(
        &Value::Object(load_json_object(qualification)?),
        &py_str(&packed["sha256"]),
        &manifest_sha256,
        &reader,
    )?;
    let qualified = items.add(qualification, QUALIFICATION_PATH, "portable_shader_qualification")?;
    let suffix = if platform == WINDOWS { ".exe" } else { "" };
    let tool =
        items.add(helper, &format!("forge/oot3d_native_nri_pipeline_prepare{suffix}"), "shader_preparation_tool")?;
    let mut libraries = Vec::new();
    for path in dependencies {
        let name = path.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
        libraries.push(items.add(path, &format!("forge/{name}"), "forge_runtime_module")?);
    }
    let mut result = catalog.clone();
    let Some(Value::Array(titles)) = result.get_mut("titles") else {
        return Err(invalid("Shader corpus requires title recipes"));
    };
    for title in titles {
        let Value::Object(title) = title else { return Err(invalid("Malformed catalog title")) };
        let Some(renderer) = title.get("renderer_shader_preparation").cloned() else {
            return Err(invalid("Bind the platform renderer compiler before the corpus"));
        };
        // Older distributed Forge versions obtain the pass compiler from the
        // combined seed contract. Keep the same compiler, not an evidence copy.
        let mut seed = json!({
            "format": crate::shader::FORMAT,
            "mode": "portable_pack",
            "descriptor_schema_version": schema,
            "pack": packed,
            "qualification": qualified,
        });
        for key in ["compiler", "dependencies"] {
            if let Some(value) = renderer.get(key) {
                seed[key] = value.clone();
            }
        }
        title.insert("shader_preparation".to_string(), seed);
        // The helper built with this runtime also prepares the renderer's
        // startup program parts, so the first launch does not wait for them.
        title.insert(
            "device_pipeline_preparation".to_string(),
            json!({
                "format": crate::device::FORMAT,
                "helper": tool,
                "manifests": pipeline_records,
                "dependencies": libraries,
                "startup_programs": true,
            }),
        );
    }
    Ok((result, items.items))
}
