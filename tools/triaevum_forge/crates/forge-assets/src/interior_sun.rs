// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `interior_sun.py`, Forge side: the ROM-derived interior sun, prepared by
//! the shipped runtime without booting a title. The native work is behind
//! [`InteriorSunService`]: on desktop the core runs the verified runtime as a
//! helper process with [`PrebuildCommand::arguments`]; on Android it calls
//! the same C++ job in process. Either way the job writes its
//! `triaevum_interior_sun_v1` result to the requested manifest path, and
//! this module validates it, hashes the shadow-shell caches and keeps the
//! receipt `<data_root>/interior-sun/<key>.json`.

use std::ffi::OsString;
use std::io::Read;
use std::path::{Component, Path, PathBuf};
use std::time::Duration;

use forge_base::fsutil::{atomic_write_json, load_json_object};
use forge_base::hashing::{hex, sha256_file, sha256_hex};
use forge_base::legacy_json::{dumps, python_float_repr, Options};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::pyvalue::py_eq;
use crate::Report;

pub const FORMAT: &str = "triaevum_interior_sun_v1";
pub const CRITERION_VERSION: u32 = 3;
/// The helper's time budget (`run_native(..., timeout=3600)`).
pub const TIMEOUT: Duration = Duration::from_secs(3600);

/// One native preparation request. Paths are resolved (absolute, links
/// followed) as Python passed them.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PrebuildCommand {
    pub romfs: PathBuf,
    pub cache_directory: PathBuf,
    pub manifest: PathBuf,
    pub mask_table: Option<PathBuf>,
}

impl PrebuildCommand {
    /// The runtime's arguments after its own path: `--prebuild-interior-sun
    /// --romfs R --renderer-cache-directory C --manifest M [--mask-table T]`.
    pub fn arguments(&self) -> Vec<OsString> {
        let mut arguments: Vec<OsString> = vec![
            "--prebuild-interior-sun".into(),
            "--romfs".into(),
            self.romfs.clone().into(),
            "--renderer-cache-directory".into(),
            self.cache_directory.clone().into(),
            "--manifest".into(),
            self.manifest.clone().into(),
        ];
        if let Some(table) = &self.mask_table {
            arguments.extend(["--mask-table".into(), table.clone().into()]);
        }
        arguments
    }
}

/// What the native job reported besides its manifest: the exit status and
/// its diagnostic text (decoded with replacement, as Python read it).
#[derive(Clone, Debug, Default)]
pub struct NativeRun {
    pub status: i32,
    pub stdout: String,
    pub stderr: String,
}

/// The native interior-sun job. Desktop: the verified runtime as a helper
/// process (cwd = package root, helper environment, hidden window, killed
/// after [`TIMEOUT`]). Android: the in-process C++ job.
pub trait InteriorSunService {
    /// SHA-256 of the verified runtime that performs the job (part of the
    /// receipt identity). The caller verifies the runtime record before.
    fn runtime_sha256(&self) -> Result<String>;
    /// Runs the job; a timeout or cancellation is an `Err`.
    fn prebuild(&self, command: &PrebuildCommand, cancel: &CancelToken) -> Result<NativeRun>;
}

fn invalid(message: &str) -> ForgeError {
    ForgeError::new(ErrorKind::PreparationFailed, message)
}

/// Python's non-strict `Path.resolve()`: absolute, links resolved as far as
/// the path exists, the rest normalized lexically, no `\\?\` prefix.
fn resolve(path: &Path) -> PathBuf {
    let absolute = std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf());
    let mut existing = absolute.as_path();
    let mut tail = Vec::new();
    loop {
        if let Ok(canonical) = std::fs::canonicalize(existing) {
            let mut resolved = strip_verbatim(canonical);
            for component in tail.iter().rev() {
                match component {
                    Component::ParentDir => {
                        resolved.pop();
                    }
                    Component::CurDir => {}
                    other => resolved.push(other.as_os_str()),
                }
            }
            return resolved;
        }
        match (existing.parent(), existing.components().next_back()) {
            (Some(parent), Some(last)) => {
                tail.push(last);
                existing = parent;
            }
            _ => return absolute,
        }
    }
}

fn strip_verbatim(path: PathBuf) -> PathBuf {
    if !cfg!(windows) {
        return path;
    }
    let text = path.to_string_lossy();
    if let Some(rest) = text.strip_prefix(r"\\?\UNC\") {
        return PathBuf::from(format!(r"\\{rest}"));
    }
    match text.strip_prefix(r"\\?\") {
        Some(rest) if rest.as_bytes().get(1) == Some(&b':') => PathBuf::from(rest),
        _ => path,
    }
}

/// Path comparison as pathlib does it: case-insensitive on Windows.
fn path_key(path: &Path) -> PathBuf {
    if cfg!(windows) {
        PathBuf::from(path.to_string_lossy().to_lowercase())
    } else {
        path.to_path_buf()
    }
}

fn is_hex16(text: &str) -> bool {
    text.len() == 16 && text.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}

/// `_outputs`: validates a native result against the cache directory and
/// returns `{relative cache path: sha256}` of its shadow-shell files.
fn outputs(result: &Map<String, Value>, cache: &Path, cancel: &CancelToken) -> Result<Map<String, Value>> {
    let rooms = result.get("rooms").filter(|rooms| rooms.is_i64() || rooms.is_u64()).and_then(Value::as_i64);
    let entries = result.get("entries").and_then(Value::as_array);
    let valid = result.get("format").and_then(Value::as_str) == Some(FORMAT)
        && result.get("criterion_version").is_some_and(|v| py_eq(v, &json!(CRITERION_VERSION)))
        && result.get("game_booted") == Some(&Value::Bool(false))
        && result
            .get("cache_directory")
            .and_then(Value::as_str)
            .is_some_and(|directory| path_key(&resolve(Path::new(directory))) == path_key(cache))
        && rooms.is_some_and(|rooms| (1..=4096).contains(&rooms))
        && entries.is_some_and(|entries| !entries.is_empty() && entries.len() <= 4096 * 17);
    if !valid {
        return Err(invalid("Invalid interior-sun prebuild manifest"));
    }
    let mut hashes = Map::new();
    for entry in entries.expect("checked above") {
        let status = entry.get("status").and_then(Value::as_str);
        let Some(status) = status.filter(|s| ["built", "hit", "precomputed", "fallback"].contains(s)) else {
            return Err(invalid("Invalid interior-sun room result"));
        };
        if status == "fallback" {
            if entry.get("reason").and_then(Value::as_str).is_none_or(str::is_empty) {
                return Err(invalid("Unexplained interior-sun fallback"));
            }
            continue;
        }
        let fingerprint = entry.get("fingerprint").and_then(Value::as_str).filter(|fp| is_hex16(fp));
        let interior = entry.get("interior").and_then(Value::as_bool);
        let (Some(fingerprint), Some(interior)) = (fingerprint, interior) else {
            return Err(invalid("Invalid interior-sun cache identity"));
        };
        let expected = format!("shadow-shells/{fingerprint}{}.bin", if interior { "" } else { "-exterior" });
        if entry.get("path").and_then(Value::as_str) != Some(expected.as_str()) {
            return Err(invalid("Invalid interior-sun cache path"));
        }
        let path = cache.join(&expected);
        if !path_key(&resolve(&path)).starts_with(path_key(cache)) || !path.is_file() {
            return Err(invalid("Interior-sun cache is missing"));
        }
        if hashes.contains_key(&expected) {
            continue;
        }
        let io = |error| ForgeError::io(format!("Cannot read {}", path.display()), error);
        let size = std::fs::metadata(&path).map_err(io)?.len();
        if !(48..=28_000_048).contains(&size) {
            return Err(invalid("Invalid interior-sun cache size"));
        }
        let mut header = [0u8; 40];
        std::fs::File::open(&path).and_then(|mut file| file.read_exact(&mut header)).map_err(io)?;
        let word = |offset: usize| u32::from_le_bytes(header[offset..offset + 4].try_into().expect("four bytes"));
        let actual_fingerprint = u64::from_le_bytes(header[8..16].try_into().expect("eight bytes"));
        let (vertices, triangles) = (u64::from(word(24)), u64::from(word(28)));
        let header_valid = &header[..4] == b"RSSH"
            && word(4) == CRITERION_VERSION
            && Some(actual_fingerprint) == u64::from_str_radix(fingerprint, 16).ok()
            && word(16) == 64
            && word(20) == 512
            && vertices <= 1_500_000
            && triangles <= 500_000
            && word(32) == u32::from(interior)
            && word(36) == 0
            && size == 48 + vertices * 12 + triangles * 20;
        if !header_valid {
            return Err(invalid("Invalid interior-sun cache header"));
        }
        hashes.insert(expected, json!(hex(&sha256_file(&path, cancel)?.1)));
    }
    Ok(hashes)
}

/// `str(value)` for the counters in the ready message.
fn py_str(value: Option<&Value>) -> String {
    match value {
        None => "0".into(),
        Some(Value::Null) => "None".into(),
        Some(Value::Bool(flag)) => if *flag { "True" } else { "False" }.into(),
        Some(Value::Number(number)) if number.is_f64() => python_float_repr(number.as_f64().unwrap_or(f64::NAN)),
        Some(Value::Number(number)) => number.to_string(),
        Some(Value::String(text)) => text.clone(),
        Some(other) => other.to_string(),
    }
}

/// The receipt's result when the receipt still matches; any unreadable or
/// stale receipt means preparing again. (Python crashed with
/// AttributeError on a receipt whose `result` was not an object; that case
/// prepares again too.)
fn reusable(receipt_path: &Path, identity: &Value, cache: &Path, cancel: &CancelToken) -> Option<Map<String, Value>> {
    let receipt = load_json_object(receipt_path).ok()?;
    if !receipt.get("identity").is_some_and(|stored| py_eq(stored, identity)) {
        return None;
    }
    let result = receipt.get("result")?.as_object()?;
    let current = outputs(result, cache, cancel).ok()?;
    receipt.get("outputs").is_some_and(|stored| py_eq(stored, &Value::Object(current))).then(|| result.clone())
}

/// A receipt written before the installation moved (issue #124). Its
/// identity names the earlier absolute cache folder, so its key no longer
/// matches. It is adopted when everything else in its identity matches, its
/// cache folder sits at the same place below its data folder as the current
/// one (the cache moved with the installation), and every shadow-shell file
/// it lists is in the current cache with the recorded hash. Returns the
/// receipt's path and its result, rebased on the current cache folder.
fn relocated(
    receipts: &Path,
    identity: &Value,
    data_root: &Path,
    cache: &Path,
    cancel: &CancelToken,
) -> Option<(PathBuf, Map<String, Value>)> {
    let below = cache.strip_prefix(data_root).ok().filter(|below| below.components().next().is_some())?;
    let mut wanted = identity.as_object()?.clone();
    wanted.remove("cache_directory");
    let mut candidates: Vec<PathBuf> = std::fs::read_dir(receipts)
        .ok()?
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| path.extension().is_some_and(|extension| extension == "json"))
        .collect();
    candidates.sort();
    for path in candidates {
        let Ok(receipt) = load_json_object(&path) else { continue };
        let Some(mut stored) = receipt.get("identity").and_then(Value::as_object).cloned() else { continue };
        let Some(Value::String(earlier)) = stored.remove("cache_directory") else { continue };
        let earlier = PathBuf::from(earlier);
        if !py_eq(&Value::Object(stored), &Value::Object(wanted.clone()))
            || path_key(&earlier) == path_key(cache)
            || !path_key(&earlier).ends_with(path_key(below))
        {
            continue;
        }
        let Some(mut result) = receipt.get("result").and_then(Value::as_object).cloned() else { continue };
        result.insert("cache_directory".into(), json!(cache.to_string_lossy()));
        let Ok(current) = outputs(&result, cache, cancel) else { continue };
        if receipt.get("outputs").is_some_and(|stored| py_eq(stored, &Value::Object(current))) {
            return Some((path, result));
        }
    }
    None
}

/// `prepare_interior_sun`: a release mask table (integrity-checked by the
/// caller) supplies the masks of the rooms it covers; the runtime builds the
/// others. Returns the native result plus `receipt_reused`.
#[allow(clippy::too_many_arguments)]
pub fn prepare_interior_sun(
    service: &dyn InteriorSunService,
    data_root: &Path,
    romfs: &Path,
    romfs_sha256: &str,
    cache_directory: &Path,
    mask_table: Option<&Path>,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Map<String, Value>> {
    if !(romfs_sha256.len() == 64 && romfs_sha256.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))) {
        return Err(ForgeError::invalid("Invalid RomFS content identity"));
    }
    let cache = resolve(cache_directory);
    let mut identity = Map::new();
    identity.insert("romfs_sha256".into(), json!(romfs_sha256));
    identity.insert("criterion_version".into(), json!(CRITERION_VERSION));
    identity.insert("runtime_sha256".into(), json!(service.runtime_sha256()?));
    identity.insert("cache_directory".into(), json!(cache.to_string_lossy()));
    if let Some(table) = mask_table {
        identity.insert("mask_table_sha256".into(), json!(hex(&sha256_file(table, cancel)?.1)));
    }
    let identity = Value::Object(identity);
    let key = sha256_hex(dumps(&identity, Options::SPACED).as_bytes());
    let receipts = resolve(data_root).join("interior-sun");
    let receipt_path = receipts.join(format!("{key}.json"));
    if let Some(mut result) = reusable(&receipt_path, &identity, &cache, cancel) {
        report("interior-sun", "Reusing offline interior sun...");
        result.insert("receipt_reused".into(), json!(true));
        return Ok(result);
    }
    // A moved installation: the earlier receipt takes this identity instead
    // of the rooms being prepared again and the old receipt left behind.
    if let Some((earlier, mut result)) = relocated(&receipts, &identity, &resolve(data_root), &cache, cancel) {
        report("interior-sun", "Reusing offline interior sun...");
        let hashes = outputs(&result, &cache, cancel)?;
        atomic_write_json(&receipt_path, &json!({"identity": identity, "outputs": hashes, "result": result}))?;
        if earlier != receipt_path {
            let _ = std::fs::remove_file(&earlier);
        }
        result.insert("receipt_reused".into(), json!(true));
        return Ok(result);
    }
    std::fs::create_dir_all(&receipts)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", receipts.display()), error))?;
    report("interior-sun", "Preparing interior sun from your ROM (offline)...");
    let temporary = tempfile::Builder::new()
        .prefix(".preparing-")
        .tempdir_in(&receipts)
        .map_err(|error| ForgeError::io(format!("Cannot write in {}", receipts.display()), error))?;
    let command = PrebuildCommand {
        romfs: resolve(romfs),
        cache_directory: cache.clone(),
        manifest: temporary.path().join("result.json"),
        mask_table: mask_table.map(resolve),
    };
    let run = service.prebuild(&command, cancel)?;
    if run.status != 0 {
        let diagnostic = if run.stderr.is_empty() { &run.stdout } else { &run.stderr };
        let skip = diagnostic.chars().count().saturating_sub(4000);
        let tail: String = diagnostic.chars().skip(skip).collect();
        return Err(invalid(&format!("Interior-sun preparation failed: {tail}")));
    }
    let mut result = load_json_object(&command.manifest)?;
    let hashes = outputs(&result, &cache, cancel)?;
    let count = hashes.len();
    atomic_write_json(&receipt_path, &json!({"identity": identity, "outputs": hashes, "result": result}))?;
    drop(temporary);
    report(
        "interior-sun",
        &format!(
            "Interior sun ready: {count} caches ({} from the release table), {} native fallbacks",
            py_str(result.get("precomputed")),
            py_str(result.get("fallback"))
        ),
    );
    result.insert("receipt_reused".into(), json!(false));
    Ok(result)
}
