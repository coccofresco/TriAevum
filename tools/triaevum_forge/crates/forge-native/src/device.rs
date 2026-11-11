// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `device_pipeline_preparation.py`: Forge's host for the renderer-owned,
//! headless NRI pipeline preparation job. GPU identity, pipeline layouts and
//! cache validation stay in the renderer; Forge verifies the helper, runs it
//! on every installation (yesterday's success says nothing about today's
//! GPU/driver) and records an honest receipt. No title boot, window, game
//! compiler, SDK download or cross-device cache reuse.

use std::collections::HashSet;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use forge_base::fsutil::atomic_write_json;
use forge_base::hashing::{hex, sha256_file};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use forge_catalog::checked_file;
use forge_catalog::pyjson::{eq_str, get, index, int, is_int, list, py_eq};
use forge_prepare::paths::resolve;
use serde_json::{json, Map, Value};

use crate::pyerr::{io_failure, load_json_object, open_error, os_error};
use crate::services::{DeviceJob, NativeServices, Report};

pub const FORMAT: &str = "triaevum_device_pipeline_preparation_v1";
/// The game executable's device pipeline tool mode (oot3d_native_game.cpp).
pub const RUNTIME_TOOL_MODE: &str = "--pipeline-prepare";
/// The renderer's NRI pipeline cache file of a title without a namespace
/// (`Renderer3ds::kNriPipelineCacheFilename`).
pub const CACHE_FILENAME: &str = "nri_pipeline_cache.bin";
/// The renderer's plain Vulkan pipeline cache file, likewise.
const PLAIN_CACHE_FILENAME: &str = "pipeline_cache.bin";
/// Time budgets: startup programs take about a minute on six cores cold.
pub const TIMEOUT_SECONDS: f64 = 600.0;
pub const STARTUP_PROGRAMS_TIMEOUT_SECONDS: f64 = 1800.0;
const DIAGNOSTIC_TAIL_BYTES: u64 = 4000;

fn contract(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::ArtifactIntegrity, message)
}

fn settings(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::InvalidSettings, message)
}

/// `installation_cache_directory(data_root)`: the writable cache Forge and
/// the launch profile both name (never the package directory: Flatpak
/// packages are read-only, Windows packages are portable).
pub fn installation_cache_directory(data_root: &Path) -> Result<PathBuf> {
    Ok(resolve(data_root)?.join("cache").join("renderer"))
}

/// `renderer_cache_directory()`: `SDL_GetPrefPath(nullptr,
/// "oot3d_native_vulkan")/shader_cache`, or `TRIAEVUM_RENDERER_CACHE_DIR`.
/// Android supplies its app-private directory instead.
pub fn renderer_cache_directory() -> Result<PathBuf> {
    if let Some(value) = std::env::var_os("TRIAEVUM_RENDERER_CACHE_DIR").filter(|value| !value.is_empty()) {
        let directory = PathBuf::from(value);
        if !directory.is_absolute() {
            return Err(settings("TRIAEVUM_RENDERER_CACHE_DIR must be absolute"));
        }
        return Ok(directory);
    }
    let root = if cfg!(windows) {
        match std::env::var_os("APPDATA").filter(|value| !value.is_empty()) {
            Some(base) => PathBuf::from(base),
            None => return Err(settings("Windows roaming application-data directory is unavailable")),
        }
    } else if cfg!(target_os = "linux") {
        match std::env::var_os("XDG_DATA_HOME").filter(|value| !value.is_empty()) {
            Some(base) => PathBuf::from(base),
            None => forge_prepare::paths::expanduser(Path::new("~")).join(".local").join("share"),
        }
    } else {
        return Err(settings("This host must supply the renderer cache directory"));
    };
    if !root.is_absolute() {
        return Err(settings("Renderer cache directory must be absolute"));
    }
    Ok(root.join("oot3d_native_vulkan").join("shader_cache"))
}

/// `Renderer3ds::TitlePipelineCacheFilename(filename, namespace)`: the
/// title's own file for a pipeline cache file name. `nri_pipeline_cache.bin`
/// is `nri_pipeline_cache.mm3d.bin` in namespace `mm3d`; the empty namespace
/// keeps the name. `None` for a namespace the renderer refuses (more than 32
/// characters, or anything but `a-z`, `0-9`, `_` and `-`).
pub fn title_cache_filename(filename: &str, namespace: &str) -> Option<String> {
    let valid = namespace.len() <= 32
        && namespace.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'_' || b == b'-');
    if !valid {
        return None;
    }
    if namespace.is_empty() {
        return Some(filename.to_owned());
    }
    Some(match filename.rfind('.') {
        Some(dot) => format!("{}.{namespace}{}", &filename[..dot], &filename[dot..]),
        None => format!("{filename}.{namespace}"),
    })
}

/// The NRI pipeline cache file the active title's runtime reads, and the
/// device pipeline preparation therefore writes.
pub fn active_cache_filename() -> String {
    title_cache_filename(CACHE_FILENAME, forge_catalog::title::ACTIVE.pipeline_cache_namespace)
        .expect("the title table holds a valid pipeline cache namespace")
}

fn is_symlink(path: &Path) -> bool {
    std::fs::symlink_metadata(path).is_ok_and(|metadata| metadata.file_type().is_symlink())
}

/// `[0-9a-f]+_[0-9a-f]+\.spvc`.
fn spirv_cache_name(name: &str) -> bool {
    let Some(stem) = name.strip_suffix(".spvc") else { return false };
    let hex_run =
        |text: &str| !text.is_empty() && text.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b));
    matches!(stem.split_once('_'), Some((left, right)) if hex_run(left) && hex_run(right))
}

fn copy_entries(destination: &Path, source: &Path, namespace: &str) -> Result<usize> {
    // The title's own two driver files (the original names without a namespace).
    let mut files: Vec<PathBuf> = [CACHE_FILENAME, PLAIN_CACHE_FILENAME]
        .iter()
        .filter_map(|name| title_cache_filename(name, namespace))
        .map(|name| source.join(name))
        .collect();
    let modules = source.join("spirv-v2");
    if modules.is_dir() && !is_symlink(&modules) {
        let entries = std::fs::read_dir(&modules).map_err(|error| {
            let message = os_error(&error, Some(&modules));
            io_failure(error, message)
        })?;
        for entry in entries.take(10000) {
            let entry = entry.map_err(|error| {
                let message = os_error(&error, Some(&modules));
                io_failure(error, message)
            })?;
            if entry.file_name().to_str().is_some_and(spirv_cache_name) {
                files.push(entry.path());
            }
        }
    }
    let mut remaining: u64 = 128 * 1024 * 1024;
    let mut copied = 0;
    for path in files {
        if is_symlink(&path) || !path.is_file() {
            continue;
        }
        let size = std::fs::metadata(&path).map(|metadata| metadata.len()).unwrap_or(0);
        let spirv = path.extension().is_some_and(|extension| extension == "spvc");
        let limit = if spirv { 16 * 1024 * 1024 } else { 64 * 1024 * 1024 + 56 };
        if !(0 < size && size <= remaining.min(limit)) {
            continue;
        }
        let mut payload = Vec::new();
        std::fs::File::open(&path).and_then(|file| file.take(size + 1).read_to_end(&mut payload)).map_err(|error| {
            let message = open_error(&error, &path);
            io_failure(error, message)
        })?;
        if payload.len() as u64 != size {
            continue;
        }
        let relative = path.strip_prefix(source).unwrap_or(&path);
        forge_base::fsutil::atomic_write_bytes(&destination.join(relative), &payload)?;
        remaining -= size;
        copied += 1;
    }
    Ok(copied)
}

/// `adopt_existing_cache(destination, source=None)`: best-effort, one-time
/// copy of an old SDL-location renderer cache into a new installation cache.
/// The old cache stays intact, learned installation entries are never
/// overwritten, and only the two driver files and bounded v2 SPIR-V entries
/// are eligible; the renderer still validates every entry. Errors are
/// reported, not fatal.
pub fn adopt_existing_cache(destination: &Path, source: Option<&Path>, report: Report<'_>) -> usize {
    let mut copied = 0;
    let attempt = (|| -> Result<()> {
        if destination.exists() {
            return Ok(());
        }
        let source = match source {
            Some(source) => source.to_path_buf(),
            None => renderer_cache_directory()?,
        };
        if is_symlink(&source) || !source.is_dir() || path_key(&resolve(&source)?) == path_key(&resolve(destination)?) {
            return Ok(());
        }
        copied = copy_entries(destination, &source, forge_catalog::title::ACTIVE.pipeline_cache_namespace)?;
        Ok(())
    })();
    if let Err(error) = attempt {
        report("shaders", &format!("Existing renderer cache could not be fully copied: {}", error.message));
    }
    if copied > 0 {
        report(
            "shaders",
            &format!("Preserved {copied} existing renderer cache entries (compatibility checked by renderer)."),
        );
    }
    copied
}

fn file_sha256(path: &Path) -> Result<String> {
    if let Err(error) = std::fs::File::open(path) {
        let message = open_error(&error, path);
        return Err(io_failure(error, message));
    }
    Ok(hex(&sha256_file(path, &CancelToken::new())?.1))
}

/// pathlib equality of resolved paths: case-insensitive on Windows.
fn path_key(path: &Path) -> String {
    let text = path.to_string_lossy();
    if cfg!(windows) {
        text.to_lowercase()
    } else {
        text.into_owned()
    }
}

fn diagnostics(stage: &Path) -> Option<String> {
    let path = stage.join("stderr.log");
    let mut file = std::fs::File::open(&path).ok().filter(|_| path.is_file())?;
    let size = file.metadata().ok()?.len();
    file.seek(SeekFrom::Start(size.saturating_sub(DIAGNOSTIC_TAIL_BYTES))).ok()?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes).ok()?;
    Some(String::from_utf8_lossy(&bytes).into_owned())
}

fn evaluate(status: i64, job: &DeviceJob) -> Result<Map<String, Value>> {
    let mut result = load_json_object(&job.report_path())?;
    if !eq_str(get(&result, "format"), FORMAT) {
        return Err(ForgeError::new(ErrorKind::PreparationFailed, "Invalid device preparation receipt"));
    }
    let total = get(&result, "total");
    let complete = status == 0
        && eq_str(get(&result, "device_pipeline_prewarm"), "complete")
        && is_int(total)
        && int(total).is_some_and(|total| total > 0)
        && py_eq(get(&result, "prepared"), total)
        && py_eq(get(&result, "failed"), &json!(0))
        && result.get("game_booted") == Some(&Value::Bool(false));
    if complete {
        let filename = title_cache_filename(CACHE_FILENAME, &job.pipeline_cache_namespace)
            .ok_or_else(|| contract("Invalid pipeline cache namespace"))?;
        let actual_cache = job.cache_directory.join(filename);
        let reported = get(&result, "cache_path").as_str().unwrap_or_default();
        let reported = resolve(Path::new(reported))?;
        if path_key(&reported) != path_key(&resolve(&actual_cache)?) || !actual_cache.is_file() {
            return Err(ForgeError::new(
                ErrorKind::PreparationFailed,
                "Preparation did not produce the requested device cache",
            ));
        }
        result.insert("cache_sha256".into(), json!(file_sha256(&actual_cache)?));
    } else if !eq_str(get(&result, "device_pipeline_prewarm"), "cancelled") {
        return Err(ForgeError::new(ErrorKind::PreparationFailed, "Device pipeline preparation was incomplete"));
    }
    Ok(result)
}

/// `prepare_device_pipelines(root, data_root, title, pack, cache_directory,
/// cancel)`: the catalog's device job against the prepared portable pack.
/// The receipt `shader-seeds/device-preparation/latest.json` says complete
/// only for a full, verified result; a cancelled job is cancelled, never
/// success; any other outcome is a failed receipt and installation goes on.
/// Invalid catalog artifacts fail before the helper runs.
#[allow(clippy::too_many_arguments)] // Python's keyword arguments, one for one.
pub fn prepare_device_pipelines(
    services: &dyn NativeServices,
    root: &Path,
    data_root: &Path,
    title: &Map<String, Value>,
    pack: Option<&Path>,
    cache_directory: Option<&Path>,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Option<Map<String, Value>>> {
    prepare_title_device_pipelines(
        forge_catalog::title::ACTIVE.pipeline_cache_namespace,
        services,
        root,
        data_root,
        title,
        pack,
        cache_directory,
        report,
        cancel,
    )
}

/// [`prepare_device_pipelines`] for a title's pipeline cache namespace
/// (`ForgeTitle::pipeline_cache_namespace`): the helper prepares the cache
/// file that title's runtime reads, with that title's startup programs, and
/// the receipt is complete only when exactly that file was produced. The
/// empty namespace is the original job (OoT3D), option for option.
#[allow(clippy::too_many_arguments)]
pub fn prepare_title_device_pipelines(
    pipeline_cache_namespace: &str,
    services: &dyn NativeServices,
    root: &Path,
    data_root: &Path,
    title: &Map<String, Value>,
    pack: Option<&Path>,
    cache_directory: Option<&Path>,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Option<Map<String, Value>>> {
    let contract_value = get(title, "device_pipeline_preparation");
    if contract_value.is_null() {
        return Ok(None);
    }
    if title_cache_filename(CACHE_FILENAME, pipeline_cache_namespace).is_none() {
        return Err(contract("Invalid pipeline cache namespace"));
    }
    let Value::Object(fields) = contract_value else {
        return Err(contract("Unsupported device pipeline preparation contract"));
    };
    if !eq_str(get(fields, "format"), FORMAT) {
        return Err(contract("Unsupported device pipeline preparation contract"));
    }
    let Some(pack) = pack.filter(|pack| pack.is_file()) else {
        return Err(contract("Device pipeline preparation requires prepared portable shaders"));
    };
    let verify = CancelToken::new();
    let helper = checked_file(root, index(contract_value, "helper")?, &verify)?;
    let title_program_pack = title.get("title_program_pack")
        .map(|record| checked_file(root, record, &verify)).transpose()?;
    if fields.contains_key("manifest") && fields.contains_key("manifests") {
        return Err(contract("Specify manifest or manifests, not both"));
    }
    let records = match (fields.get("manifests"), fields.get("manifest")) {
        (Some(records), _) => records.clone(),
        (None, Some(record)) => json!([record]),
        (None, None) => json!([]),
    };
    let records = match &records {
        Value::Array(records) if (1..=64).contains(&records.len()) => records,
        _ => return Err(contract("Device preparation requires 1 to 64 pipeline manifests")),
    };
    let mut manifests = Vec::new();
    let mut seen = HashSet::new();
    for record in records {
        let manifest = checked_file(root, record, &verify)?;
        if seen.insert(path_key(&manifest)) {
            manifests.push(manifest);
        }
    }
    let dependencies = match fields.get("dependencies") {
        None => Vec::new(),
        Some(records) => list(records, "dependencies")?
            .iter()
            .map(|record| checked_file(root, record, &verify))
            .collect::<Result<Vec<_>>>()?,
    };
    let adapter = match fields.get("adapter") {
        None => 0,
        Some(value) => match int(value) {
            Some(adapter) if is_int(value) && (0..64).contains(&adapter) => adapter as u32,
            _ => return Err(contract("Invalid device preparation adapter")),
        },
    };
    // The renderer's startup program parts (built into the helper, as into the
    // runtime): the driver's longest work, otherwise paid on the first launch.
    let startup_programs = match fields.get("startup_programs") {
        None => false,
        Some(Value::Bool(flag)) => *flag,
        Some(_) => return Err(contract("Invalid device preparation startup programs")),
    };
    // The game executable as helper: only its known tool mode.
    let tool_mode = match fields.get("helper_tool_mode") {
        None => None,
        Some(Value::String(mode)) if mode == RUNTIME_TOOL_MODE => Some(mode.clone()),
        Some(_) => return Err(contract("Unsupported device preparation helper tool mode")),
    };
    let cache = match cache_directory {
        Some(directory) => resolve(directory)?,
        None => resolve(&renderer_cache_directory()?)?,
    };
    let receipts = resolve(data_root)?.join("shader-seeds").join("device-preparation");
    std::fs::create_dir_all(&receipts).map_err(|error| {
        let message = os_error(&error, Some(&receipts));
        io_failure(error, message)
    })?;
    // Never trust yesterday's success for today's GPU/driver. The helper loads
    // only a matching cache and checks every recipe again (fast on a warm cache).
    report("pipelines", "Preparing Vulkan pipelines on this GPU (no game boot)...");
    let stage = tempfile::Builder::new().prefix(".preparing-").tempdir_in(&receipts).map_err(|error| {
        let message = os_error(&error, Some(&receipts));
        io_failure(error, message)
    })?;
    let job = DeviceJob {
        helper: helper.clone(),
        root: root.to_path_buf(),
        stage: stage.path().to_path_buf(),
        pack: pack.to_path_buf(),
        title_program_pack: title_program_pack.clone(),
        pipeline_cache_namespace: pipeline_cache_namespace.to_owned(),
        cache_directory: cache,
        adapter,
        manifests: manifests.clone(),
        startup_programs,
        timeout_seconds: if startup_programs { STARTUP_PROGRAMS_TIMEOUT_SECONDS } else { TIMEOUT_SECONDS },
        tool_mode,
    };
    let outcome =
        services.prepare_device_pipelines(&job, cancel, &mut *report).and_then(|status| evaluate(status, &job));
    // An optional acceleration step must not make a playable install
    // unusable. Corrupt/missing catalog artifacts fail before launching.
    let mut result = outcome.unwrap_or_else(|error| {
        let mut failed = Map::new();
        failed.insert("format".into(), json!(FORMAT));
        failed.insert("device_pipeline_prewarm".into(), json!("failed"));
        failed.insert("error".into(), json!(error.message));
        if let Some(text) = diagnostics(stage.path()) {
            failed.insert("diagnostics".into(), json!(text));
        }
        failed
    });
    result.insert("game_coverage_proven".into(), json!(false));
    result.insert("pack_sha256".into(), json!(file_sha256(pack)?));
    if let Some(programs) = &title_program_pack {
        result.insert("title_program_pack_sha256".into(), json!(file_sha256(programs)?));
    }
    let single = if manifests.len() == 1 { json!(file_sha256(&manifests[0])?) } else { Value::Null };
    result.insert("manifest_sha256".into(), single);
    let manifest_sha256 = manifests.iter().map(|path| file_sha256(path)).collect::<Result<Vec<_>>>()?;
    result.insert("manifest_sha256s".into(), json!(manifest_sha256));
    result.insert("helper_sha256".into(), json!(file_sha256(&helper)?));
    let dependency_sha256 = dependencies.iter().map(|path| file_sha256(path)).collect::<Result<Vec<_>>>()?;
    result.insert("dependency_sha256".into(), json!(dependency_sha256));
    atomic_write_json(&receipts.join("latest.json"), &Value::Object(result.clone()))?;
    let status = result["device_pipeline_prewarm"].as_str().map(str::to_owned).unwrap_or_default();
    report("pipelines", &format!("GPU pipeline preparation: {status}"));
    drop(stage);
    Ok(Some(result))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn title_cache_filenames_follow_the_renderer_rule() {
        // title_shader_preparation.h TitlePipelineCacheFilename, case for case.
        assert_eq!(title_cache_filename(CACHE_FILENAME, "").as_deref(), Some("nri_pipeline_cache.bin"));
        assert_eq!(title_cache_filename(CACHE_FILENAME, "mm3d").as_deref(), Some("nri_pipeline_cache.mm3d.bin"));
        assert_eq!(title_cache_filename("pipeline_cache.bin", "mm3d").as_deref(), Some("pipeline_cache.mm3d.bin"));
        assert_eq!(title_cache_filename("cache", "a-b_9").as_deref(), Some("cache.a-b_9"));
        for bad in ["MM3D", "mm 3d", "../mm3d", "mm3d.bin", &"a".repeat(33)] {
            assert_eq!(title_cache_filename(CACHE_FILENAME, bad), None, "{bad}");
        }
        assert!(title_cache_filename(CACHE_FILENAME, &"a".repeat(32)).is_some());
        // The active title's file: the original one for the OoT3D Forge, the
        // file the Moonfall Curse runtime reads for the MM3D Forge.
        let expected = if forge_catalog::title::ACTIVE.id == "mm3d" { "nri_pipeline_cache.mm3d.bin" } else { CACHE_FILENAME };
        assert_eq!(active_cache_filename(), expected);
    }

    #[test]
    fn spirv_names() {
        assert!(spirv_cache_name("ab_cd.spvc"));
        for bad in ["invalid-name.spvc", "AB_cd.spvc", "_cd.spvc", "ab_.spvc", "ab_cd.bin", "ab_c_d.spvc"] {
            assert!(!spirv_cache_name(bad), "{bad}");
        }
    }
}
