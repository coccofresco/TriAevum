// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Port of `forge.package_private_module` (the content-addressed private TAM
//! and the forge-state.json `module` record) and `activate_prepared_title`
//! (active-title.json). Existing state keeps every field it already had.

use std::fs::File;
use std::path::{Path, PathBuf};

use serde_json::{json, Value};

use forge_base::fsutil::atomic_write_json;
use forge_base::hashing::{hex, sha256_file, sha256_hex};
use forge_base::{CancelToken, ErrorKind, ForgeError, Result};

use crate::content::{load_prepared_content, manifest_integer, MODULE_NAME_KEY_DIGITS};
use crate::paths::{expanduser, path_string, resolve, InstallationContext};
use crate::tam::{Tam, TamRequest, AUDIO_SERVICE_V1, FILESYSTEM_SERVICE_V1, INPUT_SERVICE_V1, PICA_SERVICE_V1};

pub const ACTIVE_TITLE_FORMAT: &str = "triaevum_active_title_v1";
/// The services every Forge-packaged module requires, in the order Python
/// passed them to `build_tam`.
pub const REQUIRED_SERVICES: [u32; 4] = [PICA_SERVICE_V1, AUDIO_SERVICE_V1, FILESYSTEM_SERVICE_V1, INPUT_SERVICE_V1];

/// `package_private_module`'s result dictionary.
#[derive(Clone, Debug)]
pub struct PackageResult {
    /// "packaged" or "reused".
    pub status: &'static str,
    pub directory: PathBuf,
    pub module: PathBuf,
    pub module_sha256: String,
    pub native_image_sha256: String,
    pub title_aot_image_sha256: Option<String>,
    pub active_title: Option<PathBuf>,
}

impl PackageResult {
    pub fn as_json(&self) -> Result<Value> {
        let mut result = json!({
            "status": self.status,
            "directory": path_string(&self.directory)?,
            "module": path_string(&self.module)?,
            "module_sha256": self.module_sha256,
            "native_image_sha256": self.native_image_sha256,
            "title_aot_image_sha256": self.title_aot_image_sha256,
        });
        if let Some(active) = &self.active_title {
            result["active_title"] = json!(path_string(active)?);
        }
        Ok(result)
    }
}

/// SHA-256(`triaevum-private-module-v1\0` target `\0` translator `\0`
/// native `\0` title-AOT-or-empty); the file name is its first 32 digits.
pub fn module_cache_key(target_triple: &str, translator: &str, native: &str, title_aot: &str) -> String {
    sha256_hex(format!("triaevum-private-module-v1\0{target_triple}\0{translator}\0{native}\0{title_aot}").as_bytes())
}

/// `package_private_module`: builds the TAM for a prepared title, stores it
/// at `modules/<32-hex>.tam` (reusing an identical existing file, refusing a
/// corrupted one), records the module in forge-state.json and optionally
/// activates the title.
pub fn package_private_module(
    prepared_directory: &Path,
    native_image: &Path,
    title_aot_image: Option<&Path>,
    target_triple: &str,
    translator_identity: &str,
    active_title_state: Option<&Path>,
    cancel: &CancelToken,
) -> Result<PackageResult> {
    let native_image = resolve(&expanduser(native_image))?;
    if !native_image.is_file() || native_image.is_symlink() {
        return Err(ForgeError::invalid(format!("native module image is invalid: {}", path_string(&native_image)?)));
    }
    let prepared = load_prepared_content(prepared_directory, &["code"], cancel)?;
    let title_root = prepared.directory.clone();
    let mut state = prepared.state.clone();
    let Some(Value::Array(regions)) = prepared.index.get("physical_memory_regions") else {
        return Err(ForgeError::invalid("content.tap is missing module identity metadata"));
    };
    let source_identity = prepared.input("code").expect("code input was required").sha256.clone();
    let recipe = match prepared.index.get("recipe") {
        Some(Value::String(recipe)) if !recipe.is_empty() => recipe.clone(),
        _ => return Err(ForgeError::invalid("content.tap recipe is invalid")),
    };
    let mut memory_regions = Vec::with_capacity(regions.len());
    for region in regions {
        let Value::Object(region) = region else { continue };
        let field = |key: &str, label: &str| match region.get(key) {
            None => Err(ForgeError::invalid("content.tap physical-memory region is incomplete")),
            value => manifest_integer(value, label),
        };
        memory_regions.push((
            field("physical_base", "physical base")?,
            field("guest_base", "guest base")?,
            field("bytes", "physical region size")?,
        ));
    }
    if memory_regions.len() != regions.len() {
        return Err(ForgeError::invalid("content.tap physical-memory region is malformed"));
    }

    let services: Vec<i128> = REQUIRED_SERVICES.iter().map(|&id| i128::from(id)).collect();
    let request = TamRequest {
        native_image: &native_image,
        title_aot_image,
        recipe: &recipe,
        target_triple,
        source_identity: &source_identity,
        translator_identity,
        required_services: &services,
        physical_memory_regions: &memory_regions,
        runtime_abi: crate::tam::RUNTIME_ABI_V1,
    };
    let tam = Tam::build(&request, cancel).map_err(|error| match error.kind {
        ErrorKind::InvalidInput => {
            ForgeError::invalid(format!("cannot package private game module: {}", error.message))
        }
        _ => error,
    })?;
    let metadata = tam.metadata();
    let native_hash = metadata["native_image"]["sha256"].as_str().expect("native hash").to_string();
    let title_aot_hash =
        metadata.get("title_aot_image").map(|image| image["sha256"].as_str().expect("hash").to_string());
    let translator = metadata["translator_identity_sha256"].as_str().expect("translator").to_string();
    let cache_key = module_cache_key(target_triple, &translator, &native_hash, title_aot_hash.as_deref().unwrap_or(""));
    let relative_module = format!("modules/{}.tam", &cache_key[..MODULE_NAME_KEY_DIGITS]);
    let module_path = title_root.join("modules").join(format!("{}.tam", &cache_key[..MODULE_NAME_KEY_DIGITS]));

    let (status, module_hash) = if module_path.exists() {
        let digest = hex(&tam.write_to(&mut std::io::sink(), cancel)?);
        let existing_bytes = std::fs::metadata(&module_path)
            .map_err(|error| ForgeError::io(format!("Cannot read {}", module_path.display()), error))?
            .len();
        if existing_bytes != tam.len() || hex(&sha256_file(&module_path, cancel)?.1) != digest {
            return Err(ForgeError::integrity("existing private module cache entry is corrupted"));
        }
        ("reused", digest)
    } else {
        let digest = write_module(&module_path, &tam, cancel).map_err(|error| {
            ForgeError::new(error.kind, format!("cannot write private game module: {}", error.message))
        })?;
        ("packaged", digest)
    };

    let mut sorted_services = REQUIRED_SERVICES.to_vec();
    sorted_services.sort_unstable();
    state.insert(
        "module".to_string(),
        json!({
            "status": "ready",
            "container": relative_module,
            "bytes": tam.len(),
            "sha256": module_hash,
            "source_identity_sha256": source_identity,
            "translator_identity_sha256": translator,
            "native_image_sha256": native_hash,
            "title_aot_image_sha256": title_aot_hash,
            "cache_key": cache_key,
            "target_triple": target_triple,
            "required_services": sorted_services,
        }),
    );
    atomic_write_json(&title_root.join("forge-state.json"), &Value::Object(state))?;
    let active_title = match active_title_state {
        Some(path) => Some(activate_prepared_title(&title_root, path, cancel)?.active_title),
        None => None,
    };
    Ok(PackageResult {
        status,
        directory: title_root,
        module: module_path,
        module_sha256: module_hash,
        native_image_sha256: native_hash,
        title_aot_image_sha256: title_aot_hash,
        active_title,
    })
}

/// Streams the container into a short hidden temporary file beside
/// `path`, syncs it and renames it into place (`common.atomic_write_bytes`).
fn write_module(path: &Path, tam: &Tam, cancel: &CancelToken) -> Result<String> {
    let parent = path.parent().expect("module path has a parent");
    std::fs::create_dir_all(parent)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", parent.display()), error))?;
    let mut temporary = tempfile::Builder::new()
        .prefix(".")
        .suffix(".tmp")
        .tempfile_in(parent)
        .map_err(|error| ForgeError::io(format!("Cannot write in {}", parent.display()), error))?;
    let digest = tam.write_to(temporary.as_file_mut(), cancel)?;
    File::sync_all(temporary.as_file())
        .map_err(|error| ForgeError::io(format!("Cannot write {}", path.display()), error))?;
    temporary
        .persist(path)
        .map_err(|error| ForgeError::io(format!("Cannot replace {}", path.display()), error.error))?;
    Ok(hex(&digest))
}

/// `activate_prepared_title`'s result: `{"status": "active", "active_title",
/// **active}`.
#[derive(Clone, Debug)]
pub struct ActivationResult {
    pub active_title: PathBuf,
    pub active: Value,
}

impl ActivationResult {
    pub fn as_json(&self) -> Result<Value> {
        let mut result = json!({"status": "active", "active_title": path_string(&self.active_title)?});
        for (key, value) in self.active.as_object().expect("object") {
            result[key] = value.clone();
        }
        Ok(result)
    }
}

/// `activate_prepared_title`: publishes active-title.json for a prepared
/// title whose module is ready. Like Python it does not rehash the TAM.
pub fn activate_prepared_title(
    prepared_directory: &Path,
    active_title_state: &Path,
    cancel: &CancelToken,
) -> Result<ActivationResult> {
    let prepared = load_prepared_content(prepared_directory, &[], cancel)?;
    let module = match prepared.state.get("module") {
        Some(Value::Object(module)) if module.get("status").and_then(Value::as_str) == Some("ready") => module,
        _ => return Err(ForgeError::invalid("only a title with a verified private module can be active")),
    };
    let active_path = resolve(&expanduser(active_title_state))?;
    let parent = active_path.parent().map(Path::to_path_buf).unwrap_or_default();
    let context = InstallationContext::new(&parent);
    let active = json!({
        "format": ACTIVE_TITLE_FORMAT,
        "directory": context.reference(&prepared.directory, &parent)?,
        "directory_scope": context.scope(&prepared.directory, None)?.as_str(),
        "recipe": prepared.index.get("recipe").cloned().unwrap_or(Value::Null),
        "content_key": prepared.index.get("content_key").cloned().unwrap_or(Value::Null),
        "module_sha256": module.get("sha256").cloned().unwrap_or(Value::Null),
    });
    atomic_write_json(&active_path, &active)?;
    Ok(ActivationResult { active_title: active_path, active })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::content::tests::{fixture, synthetic_manifest};
    use crate::tam_reader::parse_tam;
    use forge_base::fsutil::load_json_object;

    const TARGET: &str = "x86_64-pc-windows-msvc";

    #[test]
    fn packages_private_module_in_content_addressed_cache() {
        let fixture = fixture();
        let prepared = fixture.prepare(&synthetic_manifest).unwrap();
        let native = fixture.root.join("private").join("game-module.dll");
        std::fs::write(&native, b"MZ private translated module").unwrap();
        let translator = "7".repeat(64);
        let cancel = CancelToken::new();
        let state_path = prepared.directory.join("forge-state.json");
        let mut state = load_json_object(&state_path).unwrap();
        state.insert("translation".to_string(), json!({"status": "kept"}));
        atomic_write_json(&state_path, &Value::Object(state)).unwrap();

        let packaged =
            package_private_module(&prepared.directory, &native, None, TARGET, &translator, None, &cancel).unwrap();
        assert_eq!(packaged.status, "packaged");
        assert_eq!(packaged.module.file_stem().unwrap().len(), 32);
        let bytes = std::fs::read(&packaged.module).unwrap();
        parse_tam(&bytes, 1).unwrap();
        let state = load_json_object(&state_path).unwrap();
        let module = &state["module"];
        assert_eq!(module["status"], "ready");
        assert_eq!(module["translator_identity_sha256"], translator);
        assert_eq!(module["cache_key"].as_str().unwrap().len(), 64);
        assert_eq!(module["sha256"], sha256_hex(&bytes));
        assert_eq!(module["title_aot_image_sha256"], Value::Null);
        assert_eq!(module["required_services"], json!([0x3344_4948, 0x4143_4950, 0x4944_5541, 0x5359_5346]));
        assert_eq!(state["translation"]["status"], "kept", "unknown state fields survive");
        let reused =
            package_private_module(&prepared.directory, &native, None, TARGET, &translator, None, &cancel).unwrap();
        assert_eq!(reused.status, "reused");

        std::fs::write(&packaged.module, b"corrupted").unwrap();
        let error = package_private_module(&prepared.directory, &native, None, TARGET, &translator, None, &cancel);
        assert_eq!(error.unwrap_err().message, "existing private module cache entry is corrupted");
    }

    #[test]
    fn activation_publishes_a_verified_ready_module() {
        let fixture = fixture();
        let prepared = fixture.prepare(&synthetic_manifest).unwrap();
        let cancel = CancelToken::new();
        let active_path = fixture.root.join("user-data").join("active-title.json");
        let refused = activate_prepared_title(&prepared.directory, &active_path, &cancel).unwrap_err();
        assert_eq!(refused.message, "only a title with a verified private module can be active");
        let native = fixture.root.join("private").join("game-module.dll");
        std::fs::write(&native, b"MZ private translated module").unwrap();
        package_private_module(&prepared.directory, &native, None, TARGET, &"7".repeat(64), None, &cancel).unwrap();
        let result = activate_prepared_title(&prepared.directory, &active_path, &cancel).unwrap();
        let active = load_json_object(&active_path).unwrap();
        let state = load_json_object(&prepared.directory.join("forge-state.json")).unwrap();
        assert_eq!(result.as_json().unwrap()["status"], "active");
        assert_eq!(active["format"], ACTIVE_TITLE_FORMAT);
        assert_eq!(active["directory"], path_string(&prepared.directory).unwrap());
        assert_eq!(active["directory_scope"], "external");
        assert_eq!(active["content_key"], state["content_key"]);
        assert_eq!(active["module_sha256"], state["module"]["sha256"]);

        let inside = fixture.output.join("active-title.json");
        activate_prepared_title(&prepared.directory, &inside, &cancel).unwrap();
        let active = load_json_object(&inside).unwrap();
        assert_eq!(active["directory_scope"], "relative");
        assert!(active["directory"].as_str().unwrap().starts_with("fixture/"));
    }

    #[test]
    fn rejects_packaging_after_tampering() {
        let fixture = fixture();
        let prepared = fixture.prepare(&synthetic_manifest).unwrap();
        let native = fixture.root.join("private").join("game-module.dll");
        std::fs::write(&native, b"MZ private translated module").unwrap();
        let cancel = CancelToken::new();
        let translator = "7".repeat(64);
        std::fs::write(&fixture.code, b"changed-code").unwrap();
        let error = package_private_module(&prepared.directory, &native, None, TARGET, &translator, None, &cancel);
        assert_eq!(error.unwrap_err().message, "code input changed after content preparation");
        std::fs::write(prepared.directory.join("process-manifest.json"), b"{}").unwrap();
        let error = package_private_module(&prepared.directory, &native, None, TARGET, &translator, None, &cancel);
        assert_eq!(error.unwrap_err().message, "process manifest changed after content preparation");
        let error =
            package_private_module(&prepared.directory, &fixture.root, None, TARGET, &translator, None, &cancel);
        assert!(error.unwrap_err().message.starts_with("native module image is invalid: "));
    }

    #[test]
    fn rejects_malformed_memory_regions_and_bad_identities() {
        let fixture = fixture();
        let prepared = fixture.prepare(&synthetic_manifest).unwrap();
        let native = fixture.root.join("private").join("game-module.dll");
        std::fs::write(&native, b"MZ").unwrap();
        let cancel = CancelToken::new();
        let index_path = prepared.directory.join("content.tap");
        let original = load_json_object(&index_path).unwrap();
        let translator = "7".repeat(64);
        let cases = [
            (json!([{"physical_base": 1, "guest_base": 2}]), "content.tap physical-memory region is incomplete"),
            (json!([{"physical_base": 1.0, "guest_base": 2, "bytes": 3}]), "physical base is not an integer"),
            (json!([1]), "content.tap physical-memory region is malformed"),
            (json!({}), "content.tap is missing module identity metadata"),
            (
                json!([]),
                "cannot package private game module: PICA service requires at least one physical memory region",
            ),
        ];
        for (regions, expected) in cases {
            let mut index = original.clone();
            index["physical_memory_regions"] = regions;
            atomic_write_json(&index_path, &Value::Object(index)).unwrap();
            let error = package_private_module(&prepared.directory, &native, None, TARGET, &translator, None, &cancel);
            assert_eq!(error.unwrap_err().message, expected);
        }
        atomic_write_json(&index_path, &Value::Object(original)).unwrap();
        let error =
            package_private_module(&prepared.directory, &native, None, "host-default", &translator, None, &cancel);
        assert_eq!(
            error.unwrap_err().message,
            "cannot package private game module: unsupported target triple: host-default"
        );
    }
}
