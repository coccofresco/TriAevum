// SPDX-License-Identifier: GPL-3.0-or-later
//! Launcher-owned MM EUR UI assets. No Python, compiler or ROM payload is shipped.
use crate::{Report, mm3d_font, mm3d_layout, mm3d_messages, romfs};
use forge_base::fsutil::{atomic_write_bytes, atomic_write_json, load_json_object};
use forge_base::hashing::{hex, sha256_file, sha256_hex};
use forge_base::{CancelToken, ForgeError, Result};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};

pub const CODE: &str = "6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d";
const FINGERPRINTS: &str = include_str!("../../../../../titles/mm3d/dekui/setup/setup-fingerprints.json");
const FORMAT: &str = "triaevum_mm3d_native_ui_v1";
#[derive(Debug)]
pub struct PreparedUi {
    pub layer: PathBuf,
    pub messages: PathBuf,
}
fn invalid(message: impl Into<String>) -> ForgeError {
    ForgeError::invalid(message)
}
fn text<'a>(value: &'a Value, key: &str) -> Result<&'a str> {
    value[key].as_str().ok_or_else(|| invalid(format!("Missing MM preparation field {key}")))
}
fn read(path: &Path) -> Result<Vec<u8>> {
    let size = std::fs::metadata(path).map_err(|e| ForgeError::io(format!("Cannot read {}", path.display()), e))?.len();
    if size > 64 * 1024 * 1024 {
        return Err(invalid("MM UI input exceeds preparation limit"));
    }
    std::fs::read(path).map_err(|e| ForgeError::io(format!("Cannot read {}", path.display()), e))
}
fn native(image: &Path, name: &str) -> Result<Vec<u8>> {
    romfs::read_level3_file(image, name)?.ok_or_else(|| invalid(format!("Missing MM UI resource: {name}")))
}
fn digest(path: &Path, cancel: &CancelToken) -> Result<String> {
    Ok(hex(&sha256_file(path, cancel)?.1))
}

/// Validate a published layer before activation/launch. The six exact names
/// come from the source contract, not a user-controlled manifest path.
pub fn verify_layer(layer: &Path, cancel: &CancelToken) -> Result<()> {
    let receipt = load_json_object(&layer.with_extension("receipt.json"))?;
    if receipt.get("format") != Some(&json!(FORMAT)) {
        return Err(invalid("Unknown MM UI receipt"));
    }
    let manifest: Value = serde_json::from_str(FINGERPRINTS).map_err(|e| invalid(e.to_string()))?;
    let mut expected_files = BTreeSet::new();
    for entry in manifest["files"].as_array().ok_or_else(|| invalid("Invalid embedded MM fingerprints"))? {
        let output = text(entry, "output")?;
        if !output.starts_with("romfs/") || output.contains(".mm3d-") {
            continue;
        }
        let name = &output[6..];
        expected_files.insert(name.to_owned());
        let expected = text(entry, "output_sha256")?;
        if digest(&layer.join(name), cancel)? != expected
            || receipt.get("files").and_then(|v| v.get(name)).and_then(Value::as_str) != Some(expected)
        {
            return Err(invalid(format!("MM UI asset changed: {name}; prepare again")));
        }
    }
    for name in ["eue", "eug", "euf", "eus", "eui"] {
        let path = format!("message/eu/{name}.gmsg");
        expected_files.insert(path.clone());
        let expected = receipt
            .get("files")
            .and_then(|v| v.get(&path))
            .and_then(Value::as_str)
            .ok_or_else(|| invalid("MM UI receipt lacks a prepared language archive"))?;
        if digest(&layer.join(&path), cancel)? != expected {
            return Err(invalid(format!("Prepared MM archive changed: {path}")));
        }
    }
    // Hashing only the listed members would allow an unreceipted extra file
    // to be mounted by the runtime's recursive layer reader.
    let mut pending = vec![layer.to_owned()];
    let mut actual_files = BTreeSet::new();
    while let Some(directory) = pending.pop() {
        let metadata =
            std::fs::symlink_metadata(&directory).map_err(|e| ForgeError::io("Cannot inspect MM layer", e))?;
        if !metadata.is_dir() || metadata.file_type().is_symlink() {
            return Err(invalid("Linked MM preparation directory"));
        }
        for entry in std::fs::read_dir(&directory).map_err(|e| ForgeError::io("Cannot enumerate MM layer", e))? {
            let entry = entry.map_err(|e| ForgeError::io("Cannot inspect MM member", e))?;
            let kind = entry.file_type().map_err(|e| ForgeError::io("Cannot inspect MM member type", e))?;
            if kind.is_symlink() {
                return Err(invalid("Linked MM preparation member"));
            }
            let path = entry.path();
            let name = path
                .strip_prefix(layer)
                .map_err(|_| invalid("MM layer path escaped"))?
                .to_str()
                .ok_or_else(|| invalid("Non-UTF8 MM member"))?
                .replace('\\', "/");
            if kind.is_dir() {
                if !expected_files.iter().any(|f| f.starts_with(&(name.clone() + "/"))) {
                    return Err(invalid("Unexpected MM preparation directory"));
                }
                pending.push(path);
            } else if kind.is_file() && expected_files.contains(&name) {
                actual_files.insert(name);
            } else {
                return Err(invalid("Unexpected MM preparation member"));
            }
        }
    }
    if actual_files != expected_files {
        return Err(invalid("Incomplete MM preparation inventory"));
    }
    if receipt.get("font_sha256").and_then(Value::as_str)
        != Some(digest(&layer.with_file_name("font_coverage.zip"), cancel)?.as_str())
    {
        return Err(invalid("Prepared MM HD font changed; prepare again"));
    }
    Ok(())
}

/// Called by the normal install worker after input verification. OoT is inert.
/// Project Restoration is part of the MM3D product (owner decision
/// 2026-11-08: dekui and Restoration are integrated product code, NOT Forge
/// inputs): its romfs members are read from the MM3D package's own
/// resources (RESTORATION_RESOURCES) and fingerprint-verified; the user's
/// only input is the game dump. Never fetched or inferred from another title.
pub const RESTORATION_RESOURCES: &str = "resources/mm3d/project-restoration";

/// Where a Restoration member named by the fingerprint manifest (its own
/// `romfs/...` layout) lives inside the package: under
/// RESTORATION_RESOURCES without the `romfs` component, which the release
/// policy forbids in a public package (forbidden_path_components).
pub fn restoration_package_member(input: &str) -> Option<String> {
    let relative = input.strip_prefix("romfs/")?;
    if relative.is_empty() || relative.split('/').any(|c| c.is_empty() || c == "." || c == "..") {
        return None;
    }
    Some(format!("{RESTORATION_RESOURCES}/{relative}"))
}

pub fn prepare(
    package: &Path,
    data_root: &Path,
    romfs_path: &Path,
    code_sha256: &str,
    romfs_sha256: &str,
    report: Report<'_>,
    cancel: &CancelToken,
) -> Result<Option<PreparedUi>> {
    if code_sha256 != CODE {
        return Ok(None);
    }
    cancel.check()?;
    let manifest: Value = serde_json::from_str(FINGERPRINTS).map_err(|e| invalid(e.to_string()))?;
    let source = package.join(RESTORATION_RESOURCES);
    let original_play = native(romfs_path, "layout/Joker.Main.Play.gar")?;
    if sha256_hex(&original_play) != text(&manifest, "original_action_sha256")? {
        return Err(invalid("Unsupported MM original action archive"));
    }
    let mut inputs = BTreeMap::new();
    let mut files = BTreeMap::new();
    for entry in manifest["files"].as_array().ok_or_else(|| invalid("Invalid embedded MM fingerprints"))? {
        cancel.check()?;
        let output = text(entry, "output")?;
        if !output.starts_with("romfs/") || output.contains(".mm3d-") {
            continue;
        }
        let input = text(entry, "input")?;
        let bytes = if text(entry, "input_root")? == "original" {
            native(romfs_path, &input[6..])?
        } else {
            let member = restoration_package_member(input)
                .ok_or_else(|| invalid(format!("Invalid MM preparation input {input}")))?;
            read(&package.join(member)).map_err(|_|invalid(format!("The MM3D package is missing its bundled Project Restoration resources at {}; reinstall the package.",source.display())))?
        };
        if sha256_hex(&bytes) != text(entry, "input_sha256")? {
            return Err(invalid(format!("Unsupported MM preparation input {input}")));
        }
        inputs.insert(input.to_owned(), sha256_hex(&bytes));
        let name =
            Path::new(output).file_name().and_then(|n| n.to_str()).ok_or_else(|| invalid("Invalid MM member"))?;
        let transformed = mm3d_layout::transform(name, &bytes, &original_play)?;
        if sha256_hex(&transformed) != text(entry, "output_sha256")?
            || Some(transformed.len() as u64) != entry["size"].as_u64()
        {
            return Err(invalid(format!("MM reconstruction mismatch: {output}")));
        }
        files.insert(output[6..].to_owned(), transformed);
    }
    let specs_path = data_root.join("imports/mm3d-message-specs.json");
    let spec_bytes = if specs_path.is_file() {
        read(&specs_path)?
    } else {
        include_bytes!("../../../../../titles/mm3d/dekui/setup/reviewed-dialogue.json").to_vec()
    };
    let specs: Value = serde_json::from_slice(&spec_bytes).map_err(|e| invalid(e.to_string()))?;
    if !specs
        .as_object()
        .is_some_and(|s| s.len() == 5 && s.keys().all(|k| ["en", "de", "fr", "es", "it"].contains(&k.as_str())))
    {
        return Err(invalid("MM preparation requires reviewed specs for all five EUR languages"));
    }
    let font_path = package.join("resources/topscreen/singlescreen-3.1/ltn16.qbf");
    let hd_font = read(&font_path)?;
    if sha256_hex(&hd_font) != crate::topscreen::HD_FONT_IDENTITY.1 {
        return Err(invalid("Bundled HD font identity mismatch"));
    }
    let (font_pack, font_manifest) = mm3d_font::prepare(&native(romfs_path, "message/ltn16.gzf")?, &hd_font, &specs)?;
    let mut edits = Vec::new();
    let mut languages = BTreeMap::new();
    for (code, name, mask) in
        [("en", "eue", 16u32), ("de", "eug", 32), ("fr", "euf", 64), ("es", "eus", 128), ("it", "eui", 256)]
    {
        let bytes = native(romfs_path, &format!("message/eu/{name}.gmsg"))?;
        if bytes.len() < 16
            || &bytes[..8] != b"GMSG\x01\0\0\0"
            || u32::from_le_bytes(bytes[12..16].try_into().unwrap()) != mask
        {
            return Err(invalid(format!("Wrong MM {code} message archive")));
        }
        let (expanded, compiled) = mm3d_messages::prepare(&bytes, mask, &specs[code])
            .map_err(|error| invalid(format!("{code}: {}", error.message)))?;
        languages.insert(
            code,
            json!({"source_sha256":sha256_hex(&bytes),"edits":compiled.len(),"expanded_sha256":sha256_hex(&expanded)}),
        );
        files.insert(format!("message/eu/{name}.gmsg"), expanded);
        edits.extend(compiled);
    }
    let messages = serde_json::to_vec(&json!({"format":"triaevum_mm3d_message_edits_v1","edits":edits}))
        .map_err(|e| invalid(e.to_string()))?;
    let identity = json!({"version":1,"code":code_sha256,"romfs":romfs_sha256,"inputs":inputs,"languages":languages,"messages":sha256_hex(&messages),"font":sha256_hex(&font_pack),"generator":sha256_hex(concat!(include_str!("mm3d_ui.rs"),include_str!("mm3d_layout.rs"),include_str!("mm3d_messages.rs"),include_str!("mm3d_font.rs"),include_str!("../../../../../titles/mm3d/dekui/setup/setup-fingerprints.json")).as_bytes())});
    let root = data_root.join("prepared/mm3d-ui");
    let pointer = root.join("current.json");
    if let Ok(old) = load_json_object(&pointer) {
        if old.get("identity") == Some(&identity) {
            if let Some(name) = old
                .get("generation")
                .and_then(Value::as_str)
                .filter(|n| n.starts_with("generation-") && !n.contains(['/', '\\', ':']) && *n != "..")
            {
                let directory = root.join(name);
                let layer = directory.join("romfs");
                let edits = directory.join("message_edits.json");
                if verify_layer(&layer, cancel).is_ok()
                    && digest(&edits, cancel).is_ok_and(|v| v == sha256_hex(&messages))
                {
                    report("assets", "MM3D UI preparation cache hit");
                    return Ok(Some(PreparedUi { layer, messages: edits }));
                }
            }
        }
    }
    report("assets", "Preparing MM3D native language and interface assets");
    std::fs::create_dir_all(&root).map_err(|e| ForgeError::io("Cannot create MM preparation folder", e))?;
    let stage = tempfile::Builder::new()
        .prefix("generation-")
        .tempdir_in(&root)
        .map_err(|e| ForgeError::io("Cannot stage MM UI", e))?;
    let layer = stage.path().join("romfs");
    let edits = stage.path().join("message_edits.json");
    let mut hashes = BTreeMap::new();
    for (name, bytes) in files {
        cancel.check()?;
        atomic_write_bytes(&layer.join(&name), &bytes)?;
        hashes.insert(name, sha256_hex(&bytes));
    }
    atomic_write_bytes(&edits, &messages)?;
    atomic_write_bytes(&layer.with_file_name("font_coverage.zip"), &font_pack)?;
    atomic_write_json(
        &layer.with_extension("receipt.json"),
        &json!({"format":FORMAT,"identity":identity,"files":hashes,"font_sha256":sha256_hex(&font_pack),"font":font_manifest}),
    )?;
    verify_layer(&layer, cancel)?;
    cancel.check()?;
    let directory = stage.keep();
    if let Err(error) = atomic_write_json(
        &pointer,
        &json!({"identity":identity,"generation":directory.file_name().unwrap().to_string_lossy()}),
    ) {
        let _ = std::fs::remove_dir_all(&directory);
        return Err(error);
    }
    Ok(Some(PreparedUi { layer, messages: edits }))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn restoration_is_a_package_resource_not_a_user_input() {
        // The Restoration members come from the package, never from the
        // user's data root (the dump is the only user input).
        let package = Path::new("P");
        assert_eq!(package.join(RESTORATION_RESOURCES), Path::new("P/resources/mm3d/project-restoration"));
        assert!(!RESTORATION_RESOURCES.starts_with("imports"));
    }
    /// Every Restoration member the preparation reads maps to a package path
    /// the release policy admits: no forbidden component, no forbidden
    /// extension (lists checked against release_policy.json below).
    #[test]
    fn restoration_members_are_policy_clean_package_paths() {
        let manifest: Value = serde_json::from_str(FINGERPRINTS).unwrap();
        let policy_path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../triaevum_release/release_policy.json");
        let policy: Value = serde_json::from_slice(&std::fs::read(policy_path).unwrap()).unwrap();
        let list = |key: &str| -> Vec<String> {
            policy[key].as_array().unwrap().iter().map(|v| v.as_str().unwrap().to_owned()).collect()
        };
        let (components, extensions) = (list("forbidden_path_components"), list("forbidden_extensions"));
        let (mut used, mut gar) = (0, 0);
        for entry in manifest["files"].as_array().unwrap() {
            let output = entry["output"].as_str().unwrap();
            if entry["input_root"] != "restoration" || !output.starts_with("romfs/") || output.contains(".mm3d-") {
                continue;
            }
            let member = restoration_package_member(entry["input"].as_str().unwrap()).unwrap();
            assert!(member.starts_with("resources/mm3d/project-restoration/layout/"), "{member}");
            assert!(!member.split('/').any(|c| components.iter().any(|f| f == c)), "{member}");
            // A forbidden extension (.gar) is admitted only through its role
            // exception, for exactly the paths that role lists (Q-INTEG-1:
            // the local MM3D package bundles the modified layout archives
            // as-is, pinned, like TopScreen's ctxb/zar).
            if let Some(extension) = extensions.iter().find(|e| member.ends_with(e.as_str())) {
                let roles = policy["forbidden_extension_role_exceptions"][extension.as_str()].as_array().unwrap();
                assert!(
                    roles.iter().any(|role| policy["allowed_roles"][role.as_str().unwrap()]
                        .as_array()
                        .unwrap()
                        .iter()
                        .any(|path| path.as_str() == Some(member.as_str()))),
                    "{member}"
                );
                gar += 1;
            }
            used += 1;
        }
        assert_eq!(used, 3, "Joker.Main.New.gar, Joker.Main.Play.gar, Joker.mfpj");
        assert_eq!(gar, 2, "both layout archives go through the restoration_mm3d_asset role");
        // Negative: the raw manifest path would violate the policy, and
        // non-romfs or escaping inputs are refused.
        assert!(components.iter().any(|c| c == "romfs"));
        assert_eq!(restoration_package_member("code.bps"), None);
        assert_eq!(restoration_package_member("romfs/../code.bps"), None);
        assert_eq!(restoration_package_member("romfs/"), None);
    }
    #[test]
    fn other_titles_are_inert() {
        let p = Path::new("must-not-be-read-or-created");
        assert!(prepare(p, p, p, "oot3d", "", &mut |_, _| {}, &CancelToken::new()).unwrap().is_none());
    }
    #[test]
    #[ignore = "requires an MM3D package (bundled Restoration resources) and the user MM RomFS"]
    fn local_atomic_preparation_contract() {
        let package = PathBuf::from(std::env::var_os("TRIAEVUM_MM_TEST_PACKAGE").expect("package"));
        let romfs = PathBuf::from(std::env::var_os("TRIAEVUM_MM_TEST_ROMFS").expect("RomFS"));
        let temp = tempfile::tempdir().unwrap();
        let data = temp.path();
        let cancel = CancelToken::new();
        // No user Restoration import exists: the package resources are used.
        assert!(!data.join("imports/mm3d-restoration").exists());
        let hash = digest(&romfs, &cancel).unwrap();
        let build = || prepare(&package, data, &romfs, CODE, &hash, &mut |_, _| {}, &cancel).unwrap().unwrap();
        let first = build();
        let cached = build();
        assert_eq!(first.layer, cached.layer);
        let rows: Value = serde_json::from_slice(&std::fs::read(&first.messages).unwrap()).unwrap();
        assert_eq!(rows["edits"].as_array().unwrap().len(), 795);
        let font = first.layer.with_file_name("font_coverage.zip");
        std::fs::write(&font, b"broken").unwrap();
        assert!(verify_layer(&first.layer, &cancel).is_err());
        let repaired = build();
        assert_ne!(first.layer, repaired.layer);
        assert!(first.layer.is_dir());
        verify_layer(&repaired.layer, &cancel).unwrap();
        let extra = repaired.layer.join("unreceipted.bin");
        std::fs::write(&extra, b"extra").unwrap();
        assert!(verify_layer(&repaired.layer, &cancel).is_err());
        std::fs::remove_file(extra).unwrap();
        let pointer = data.join("prepared/mm3d-ui/current.json");
        let before = std::fs::read(&pointer).unwrap();
        let custom = data.join("imports/mm3d-message-specs.json");
        std::fs::write(&custom, b"{}").unwrap();
        assert!(prepare(&package, data, &romfs, CODE, &hash, &mut |_, _| {}, &cancel).is_err());
        assert_eq!(std::fs::read(&pointer).unwrap(), before);
        verify_layer(&repaired.layer, &cancel).unwrap();
        let mut specs: Value =
            serde_json::from_str(include_str!("../../../../../titles/mm3d/dekui/setup/reviewed-dialogue.json"))
                .unwrap();
        specs["en"]["edits"].as_array_mut().unwrap().last_mut().unwrap()["text"] = json!("Choose a name.");
        std::fs::write(&custom, serde_json::to_vec(&specs).unwrap()).unwrap();
        let interrupted = CancelToken::new();
        assert!(prepare(&package, data, &romfs, CODE, &hash, &mut |_, _| interrupted.cancel(), &interrupted).is_err());
        assert_eq!(std::fs::read(&pointer).unwrap(), before);
        verify_layer(&repaired.layer, &cancel).unwrap();
        std::fs::remove_file(custom).unwrap();
        assert_eq!(build().layer, repaired.layer);
        println!(
            "local MM acceptance: 795 rows, cache reuse, corrupt-font repair, retained generation, extra-file rejection, invalid/cancelled preparation rollback"
        );
    }
}
