// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `shader_preparation.py`: the recipe-bound portable shader seed and the
//! renderer pass shader cache. This compiles GPU shader modules, never title
//! C++ or an SDK; no seed is discovered by filename or downloaded. Device
//! pipeline prewarm is [`crate::device`].

use std::fs::File;
use std::path::{Path, PathBuf};

use forge_base::fsutil::atomic_write_json;
use forge_base::hashing::{copy_hashing, hex, sha256_file};
use forge_base::legacy_json::{dumps, Options};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_catalog::checked_file;
use forge_catalog::pyjson::{eq_str, get, index, int, is_int, list, py_eq};
use forge_prepare::paths::resolve;
use serde_json::{json, Map, Value};

use crate::o3ps::pack_header;
use crate::pyerr::{io_failure, load_json_object, open_error, os_error, tail};
use crate::services::{NativeOutput, NativeServices, PortableCompile, RendererPassRequest, Report, TransferableImport};

pub const FORMAT: &str = "triaevum_shader_preparation_v1";
pub const RENDERER_FORMAT: &str = "triaevum_renderer_shader_preparation_v1";
pub const RENDERER_CONTRACT: &str = "triaevum_renderer_shader_compiler_v1";

fn contract(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::ArtifactIntegrity, message)
}

fn file_sha256(path: &Path) -> Result<String> {
    if let Err(error) = File::open(path) {
        let message = open_error(&error, path);
        return Err(io_failure(error, message));
    }
    Ok(hex(&sha256_file(path, &CancelToken::new())?.1))
}

/// `_run(command, root)`: a nonzero exit is "Shader preparation failed"
/// with the last 4000 characters of its diagnostics.
fn checked_run(output: Result<NativeOutput>) -> Result<()> {
    let output = output?;
    if output.status != 0 {
        let streams = if output.stderr.is_empty() { &output.stdout } else { &output.stderr };
        return Err(ForgeError::new(
            ErrorKind::PreparationFailed,
            format!("Shader preparation failed: {}", tail(streams, 4000)),
        ));
    }
    Ok(())
}

fn checked_files(root: &Path, records: Option<&Value>, what: &str, cancel: &CancelToken) -> Result<Vec<PathBuf>> {
    match records {
        None => Ok(Vec::new()),
        Some(records) => list(records, what)?.iter().map(|record| checked_file(root, record, cancel)).collect(),
    }
}

fn temporary_stage(parent: &Path) -> Result<tempfile::TempDir> {
    tempfile::Builder::new().prefix(".preparing-").tempdir_in(parent).map_err(|error| {
        let message = os_error(&error, Some(parent));
        io_failure(error, message)
    })
}

fn create_directory(path: &Path) -> Result<()> {
    std::fs::create_dir_all(path).map_err(|error| {
        let message = os_error(&error, Some(path));
        io_failure(error, message)
    })
}

/// The renderer result check of `prepare_renderer_shader_cache`.
fn renderer_result_complete(result: &Map<String, Value>, cache: &Path) -> Result<bool> {
    let counters = ["modules", "hits", "compiled"].map(|key| get(result, key));
    let numbers: Option<Vec<i128>> =
        counters.iter().map(|value| if is_int(value) { int(value).filter(|n| *n >= 0) } else { None }).collect();
    let Some(numbers) = numbers else { return Ok(false) };
    let (modules, hits, compiled) = (numbers[0], numbers[1], numbers[2]);
    let directory = match result.get("cache_directory") {
        Some(Value::String(directory)) => directory,
        _ => return Ok(false),
    };
    Ok(eq_str(get(result, "format"), RENDERER_FORMAT)
        && result.get("game_booted") == Some(&Value::Bool(false))
        && 0 < modules
        && modules <= 256
        && hits + compiled == modules
        && py_eq(get(result, "compile_failed"), &json!(0))
        && py_eq(get(result, "write_failed"), &json!(0))
        && py_eq(get(result, "writes"), &json!(compiled))
        && same_path(&resolve(Path::new(directory))?, cache))
}

/// pathlib equality: case-insensitive on Windows.
fn same_path(left: &Path, right: &Path) -> bool {
    if cfg!(windows) {
        left.to_string_lossy().to_lowercase() == right.to_string_lossy().to_lowercase()
    } else {
        left == right
    }
}

/// `prepare_renderer_shader_cache(root, data_root, title, cache_directory)`:
/// asks the verified pass compiler to check and fill the renderer's SPIR-V
/// cache, without booting the game. Optional acceleration: a native failure
/// produces a failed receipt and preparation continues, while invalid
/// catalog artifacts fail before anything runs. Writes
/// `shader-seeds/renderer-preparation/latest.json`.
pub fn prepare_renderer_shader_cache(
    services: &dyn NativeServices,
    root: &Path,
    data_root: &Path,
    title: &Map<String, Value>,
    cache_directory: &Path,
    report: Report<'_>,
) -> Result<Option<Map<String, Value>>> {
    // Renderer-owned sources are distributable independently of a private game
    // inventory. Retain compatibility with the original combined catalog.
    let explicit = title.contains_key("renderer_shader_preparation");
    let seed = if explicit { get(title, "renderer_shader_preparation") } else { get(title, "shader_preparation") };
    if seed.is_null() {
        return Ok(None);
    }
    let expected = if explicit { RENDERER_CONTRACT } else { FORMAT };
    let Value::Object(seed) = seed else { return Err(contract("Unsupported shader preparation contract")) };
    if !eq_str(get(seed, "format"), expected) {
        return Err(contract("Unsupported shader preparation contract"));
    }
    let Some(compiler) = seed.get("compiler") else { return Ok(None) };
    let cancel = CancelToken::new();
    let compiler = checked_file(root, compiler, &cancel)?;
    let dependencies = checked_files(root, seed.get("dependencies"), "dependencies", &cancel)?;
    let cache = resolve(cache_directory)?;
    let receipts = resolve(data_root)?.join("shader-seeds").join("renderer-preparation");
    create_directory(&receipts)?;
    report("shaders", "Preparing renderer pass shaders (no game boot)...");
    let stage = temporary_stage(&receipts)?;
    let manifest = stage.path().join("result.json");
    let request = RendererPassRequest {
        compiler: compiler.clone(),
        root: root.to_path_buf(),
        cache_directory: cache.clone(),
        manifest: manifest.clone(),
    };
    let attempt = (|| -> Result<Map<String, Value>> {
        checked_run(services.prepare_renderer_passes(&request))?;
        let mut result = load_json_object(&manifest)?;
        if !renderer_result_complete(&result, &cache)? {
            return Err(ForgeError::new(
                ErrorKind::PreparationFailed,
                "Renderer shader preparation did not persist the requested cache",
            ));
        }
        result.insert("renderer_shader_preparation".into(), json!("complete"));
        Ok(result)
    })();
    // Optional acceleration, like device pipeline preparation. Invalid
    // catalog artifacts fail above; unsupported old tools remain playable.
    let mut result = attempt.unwrap_or_else(|error| {
        let mut failed = Map::new();
        failed.insert("format".into(), json!(RENDERER_FORMAT));
        failed.insert("renderer_shader_preparation".into(), json!("failed"));
        failed.insert("error".into(), json!(error.message));
        failed
    });
    result.insert("compiler_sha256".into(), json!(file_sha256(&compiler)?));
    let dependency_sha256 = dependencies.iter().map(|path| file_sha256(path)).collect::<Result<Vec<_>>>()?;
    result.insert("dependency_sha256".into(), json!(dependency_sha256));
    atomic_write_json(&receipts.join("latest.json"), &Value::Object(result.clone()))?;
    let status = result["renderer_shader_preparation"].as_str().unwrap_or_default().to_string();
    report("shaders", &format!("Renderer shader preparation: {status}"));
    drop(stage);
    Ok(Some(result))
}

/// Copies `source` to `destination` through a temporary sibling (as
/// `common.atomic_write_bytes`, streamed); returns the SHA-256.
fn atomic_copy(source: &Path, destination: &Path) -> Result<String> {
    let parent = destination.parent().unwrap_or(Path::new("."));
    let mut input = File::open(source).map_err(|error| {
        let message = open_error(&error, source);
        io_failure(error, message)
    })?;
    let mut temporary = tempfile::Builder::new().prefix(".").suffix(".tmp").tempfile_in(parent).map_err(|error| {
        let message = os_error(&error, Some(parent));
        io_failure(error, message)
    })?;
    let (_, digest) = copy_hashing(&mut input, temporary.as_file_mut(), u64::MAX, &CancelToken::new())?;
    temporary
        .as_file()
        .sync_all()
        .map_err(|error| ForgeError::io(format!("Cannot write {}", destination.display()), error))?;
    temporary
        .persist(destination)
        .map_err(|error| ForgeError::io(format!("Cannot replace {}", destination.display()), error.error))?;
    Ok(hex(&digest))
}

enum SeedMode {
    Portable { pack: PathBuf },
    Compiled { transferable: Option<(String, Vec<PathBuf>, PathBuf)>, compiler: PathBuf, inventories: Vec<PathBuf> },
}

/// `prepare_shader_seed(root, data_root, title)`: the verified portable
/// shader pack for this recipe at `shader-seeds/<key>/portable.o3ps`, copied
/// from the catalog or compiled once from verified inputs. The key binds the
/// contract, the recipe and every artifact digest; a matching receipt and
/// pack are reused, anything else is rebuilt.
pub fn prepare_shader_seed(
    services: &dyn NativeServices,
    root: &Path,
    data_root: &Path,
    title: &Map<String, Value>,
    report: Report<'_>,
) -> Result<Option<PathBuf>> {
    let seed = get(title, "shader_preparation");
    if seed.is_null() {
        return Ok(None);
    }
    let Value::Object(seed_map) = seed else { return Err(contract("Unsupported shader preparation contract")) };
    if !eq_str(get(seed_map, "format"), FORMAT) {
        return Err(contract("Unsupported shader preparation contract"));
    }
    let schema = get(seed_map, "descriptor_schema_version");
    let schema = match int(schema) {
        Some(schema) if is_int(get(seed_map, "descriptor_schema_version")) && schema > 0 => schema,
        _ => return Err(contract("Shader preparation requires a descriptor schema")),
    };
    let cancel = CancelToken::new();
    let mode = get(seed_map, "mode").as_str().unwrap_or_default();
    let (mode_data, artifacts) = match mode {
        "portable_pack" => {
            let pack = checked_file(root, index(seed, "pack")?, &cancel)?;
            pack_header(&pack, schema)?;
            (SeedMode::Portable { pack: pack.clone() }, vec![pack])
        }
        "citra_transferable" | "source_inventories" => {
            let mut transferable = None;
            let mut artifacts = Vec::new();
            if mode == "citra_transferable" {
                let dialect = match get(seed_map, "dialect").as_str() {
                    Some(dialect @ ("citra-legacy-v1" | "azahar-v1")) => dialect.to_string(),
                    _ => return Err(contract("Shader cache requires an explicit Citra/Azahar dialect")),
                };
                let sources = checked_files(root, seed_map.get("caches"), "caches", &cancel)?;
                if sources.is_empty() {
                    return Err(contract("Shader preparation has no transferable inputs"));
                }
                let importer = checked_file(root, index(seed, "importer")?, &cancel)?;
                artifacts.extend(sources.iter().cloned());
                artifacts.push(importer.clone());
                transferable = Some((dialect, sources, importer));
            }
            let compiler = checked_file(root, index(seed, "compiler")?, &cancel)?;
            let inventories = checked_files(root, seed_map.get("inventories"), "inventories", &cancel)?;
            if mode == "source_inventories" && inventories.is_empty() {
                return Err(contract("Shader preparation has no source inventories"));
            }
            // Include dynamically linked compiler dependencies in the cache identity.
            let dependencies = checked_files(root, seed_map.get("dependencies"), "dependencies", &cancel)?;
            artifacts.push(compiler.clone());
            artifacts.extend(inventories.iter().cloned());
            artifacts.extend(dependencies);
            (SeedMode::Compiled { transferable, compiler, inventories }, artifacts)
        }
        _ => return Err(contract("Unsupported shader preparation mode")),
    };
    let artifact_hashes = artifacts.iter().map(|path| file_sha256(path)).collect::<Result<Vec<_>>>()?;
    let identity = json!({"contract": seed, "recipe": index(&Value::Object(title.clone()), "recipe")?, "artifact_hashes": artifact_hashes});
    let key = forge_base::hashing::sha256_hex(dumps(&identity, Options::SPACED).as_bytes());
    let directory = resolve(data_root)?.join("shader-seeds").join(&key);
    let pack_path = directory.join("portable.o3ps");
    let receipt_path = directory.join("preparation.json");
    if receipt_path.is_file() && pack_path.is_file() {
        let reusable = (|| -> Result<bool> {
            let receipt = load_json_object(&receipt_path)?;
            if py_eq(get(&receipt, "identity"), &identity)
                && eq_str(get(&receipt, "pack_sha256"), &file_sha256(&pack_path)?)
            {
                pack_header(&pack_path, schema)?;
                return Ok(true);
            }
            Ok(false)
        })();
        if matches!(reusable, Ok(true)) {
            report("shaders", "Reusing prepared portable shaders...");
            return Ok(Some(pack_path));
        }
    }
    create_directory(&directory)?;
    report("shaders", "Preparing portable shaders (no game recompilation)...");
    let stage = temporary_stage(&directory)?;
    let pack = match mode_data {
        SeedMode::Portable { pack } => pack,
        SeedMode::Compiled { transferable, compiler, mut inventories } => {
            if let Some((dialect, inputs, importer)) = transferable {
                let inventory = stage.path().join("transferable.json");
                let request = TransferableImport {
                    importer,
                    root: root.to_path_buf(),
                    dialect,
                    output: inventory.clone(),
                    inputs,
                };
                checked_run(services.import_transferable_shaders(&request))?;
                let imported = load_json_object(&inventory)?;
                let complete = match imported.get("transferable_import") {
                    Some(Value::Object(import)) => import.get("complete_import") == Some(&Value::Bool(true)),
                    _ => false,
                };
                if !complete {
                    return Err(ForgeError::new(
                        ErrorKind::PreparationFailed,
                        "Transferable shader import was incomplete",
                    ));
                }
                inventories.insert(0, inventory);
            }
            let pack = stage.path().join("compiled.o3ps");
            let request = PortableCompile {
                compiler,
                root: root.to_path_buf(),
                pack: pack.clone(),
                manifest: stage.path().join("compile.json"),
                inventories,
            };
            checked_run(services.compile_portable_shaders(&request))?;
            pack
        }
    };
    let count = pack_header(&pack, schema)?;
    let pack_sha256 = atomic_copy(&pack, &pack_path)?;
    atomic_write_json(
        &receipt_path,
        &json!({
            "format": FORMAT,
            "identity": identity,
            "pack_sha256": pack_sha256,
            "modules": count,
            "device_pipeline_prewarm": "not_performed",
            "game_coverage_proven": false,
        }),
    )?;
    drop(stage);
    report("shaders", &format!("Prepared {count} portable shader modules."));
    Ok(Some(pack_path))
}
