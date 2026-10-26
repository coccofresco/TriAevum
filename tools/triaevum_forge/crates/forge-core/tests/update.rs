// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Windows "Update from ZIP" (UX plan WP-04B) on a synthetic installation:
//! the plan's refusals (nothing written), staging, and full updates through
//! child processes (Forge, the helper, the new engine), including the kill
//! matrix: the parent or the helper dies after every boundary and the
//! recovery must leave a validated old or new tuple, user files intact.
//!
//! The child roles re-execute this test binary (`*_entry` tests, inert
//! unless their environment selects them), like the restore abort test.

#![cfg(windows)]

mod common;
#[path = "common/update.rs"]
mod update_fixture;

use std::collections::BTreeMap;
use std::ffi::OsString;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use common::{host, write, FakeRuntime, Fixture, RECIPE};
use forge_base::fsutil::load_json_object;
use forge_base::hashing::{hex, sha256_hex};
use forge_base::{CancelToken, ErrorKind, Reason};
use forge_core::explain::explain;
use forge_core::layout::HostOs;
use forge_core::refresh::{refresh_installation, RefreshRequest};
use forge_core::release_signature::{generate_test_key, sign, Authenticity, Trust, TrustedKey};
use forge_core::state::{inspect, InspectRequest, InstallationState, RepairReason};
use forge_core::update::{
    cleanup_stale, hand_off, plan, stage, Commands, PlanOptions, Planned, Program, UpdateRequest, UpdateSource,
};
use forge_core::update_gate::{self, DELEGATION_VARIABLE};
use forge_core::update_helper::{self, HelperArgs, PROPOSAL_VARIABLE};
use forge_core::update_journal::{self, Faults};
use serde_json::{json, Value};
use update_fixture::{assert_kept, config_files, customize_settings, receipt, sentinels};

const OLD_RUNTIME: &[u8] = b"fake runtime";
const NEW_RUNTIME: &[u8] = b"fake runtime 2";
const OLD_PLUGIN: &[u8] = b"MZ fake title plugin";
const NEW_PLUGIN: &[u8] = b"MZ fake title plugin 2";
const PREFIX: &str = "TriAevum-0.8.1-windows";

/// The package files beyond the fixture's catalogue artifacts.
fn extra_files(new: bool) -> Vec<(&'static str, &'static str, Vec<u8>)> {
    let tag = if new { "new" } else { "old" };
    let mut files = vec![
        ("TriAevumForge.exe", "forge_executable", format!("{tag} forge window").into_bytes()),
        ("forge/triaevum-forge.exe", "forge_command_line", format!("{tag} command line").into_bytes()),
        ("forge/triaevum-update-helper.exe", "forge_update_helper", format!("{tag} helper").into_bytes()),
        ("docs/shared.md", "documentation", b"unchanged documentation".to_vec()),
        ("README.md", "documentation", b"read me".to_vec()),
    ];
    files.push(if new {
        ("docs/new.md", "documentation", b"only in the new package".to_vec())
    } else {
        ("docs/old-only.md", "documentation", b"only in the old package".to_vec())
    });
    files
}

fn roles() -> Vec<(String, &'static str)> {
    vec![
        ("recipes/oot3d.json".into(), "revision_recipe"),
        ("recipes/precompiled-titles.json".into(), "precompiled_catalog"),
        (host().runtime.into(), "runtime_executable"),
        (host().native_module.into(), "forge_runtime_module"),
        (format!("titles/{RECIPE}/{}", host().title_module), "precompiled_title"),
    ]
}

/// Writes the release manifest of the package at `root` over `files` and
/// signs it; returns every package file with its bytes.
fn publish(root: &Path, version: &str, files: &[(String, &str)], key: &[u8]) -> BTreeMap<String, Vec<u8>> {
    let mut inventory = Vec::new();
    let mut contents = BTreeMap::new();
    for (path, role) in files {
        let bytes = std::fs::read(root.join(path)).unwrap();
        inventory.push(json!({"path": path, "role": role, "bytes": bytes.len(), "sha256": sha256_hex(&bytes)}));
        contents.insert(path.clone(), bytes);
    }
    inventory.sort_by(|a, b| a["path"].as_str().cmp(&b["path"].as_str()));
    let manifest = json!({
        "format": "triaevum_public_release_manifest_v1",
        "release": {"name": "TriAevum", "version": version, "target": host().target,
                    "distribution_model": "precompiled_title_rom_import_v1", "contains_title_code": true},
        "files": inventory,
    });
    let bytes = forge_base::legacy_json::pretty_file_bytes(&manifest);
    write(&root.join("docs/release-manifest.json"), &bytes);
    let signature = serde_json::to_vec(&sign(&bytes, key).unwrap()).unwrap();
    write(&root.join("docs/release-signature.json"), &signature);
    contents.insert("docs/release-manifest.json".into(), bytes);
    contents.insert("docs/release-signature.json".into(), signature);
    contents
}

/// An installed 0.8.0 package with user files, and a downloaded 0.8.1 ZIP.
struct Site {
    fixture: Fixture,
    downloads: tempfile::TempDir,
    private_key: Vec<u8>,
    key: TrustedKey,
    old_files: BTreeMap<String, Vec<u8>>,
    new_files: BTreeMap<String, Vec<u8>>,
    new_folder: PathBuf,
    zip: PathBuf,
    kept: Vec<(PathBuf, Vec<u8>)>,
    settings: Vec<(String, Vec<u8>)>,
}

impl Site {
    fn new() -> Self {
        Self::with_version("0.8.1")
    }

    fn with_version(version: &str) -> Self {
        let fixture = Fixture::new();
        let (private_key, key) = generate_test_key();
        let mut files = roles();
        for (path, role, bytes) in extra_files(false) {
            write(&fixture.package.join(path), &bytes);
            files.push((path.into(), role));
        }
        let old_files = publish(&fixture.package, "0.8.0", &files, &private_key);
        fixture.install(&FakeRuntime::default(), &mut Vec::new()).unwrap();
        customize_settings(&fixture.data());
        let kept = sentinels(&fixture);
        let settings = config_files(&fixture.data());

        let downloads = tempfile::tempdir().unwrap();
        let new_folder = downloads.path().join(PREFIX);
        for (path, _) in roles() {
            write(&new_folder.join(&path), &old_files[&path]);
        }
        let mut files = roles();
        for (path, role, bytes) in extra_files(true) {
            write(&new_folder.join(path), &bytes);
            files.push((path.into(), role));
        }
        // The new runtime and title module, bound by the new catalogue.
        let record =
            |relative: &str, bytes: &[u8]| json!({"path": relative, "bytes": bytes.len(), "sha256": sha256_hex(bytes)});
        let catalog_path = new_folder.join("recipes/precompiled-titles.json");
        let mut catalog = Value::Object(load_json_object(&catalog_path).unwrap());
        write(&new_folder.join(host().runtime), NEW_RUNTIME);
        catalog["runtime"] = record(host().runtime, NEW_RUNTIME);
        let plugin = format!("titles/{RECIPE}/{}", host().title_module);
        write(&new_folder.join(&plugin), NEW_PLUGIN);
        catalog["titles"][0]["plugin"] = record(&plugin, NEW_PLUGIN);
        write(&catalog_path, catalog.to_string().as_bytes());
        let new_files = publish(&new_folder, version, &files, &private_key);
        let zip = downloads.path().join(format!("{PREFIX}.zip"));
        zip_folder(&new_folder, PREFIX, &zip);
        Self { fixture, downloads, private_key, key, old_files, new_files, new_folder, zip, kept, settings }
    }

    fn activation(&self) -> &Path {
        &self.fixture.package
    }

    fn options(&self) -> PlanOptions {
        PlanOptions { trust: Trust { keys: vec![self.key.clone()], allow_unsigned: false }, available_space: None }
    }

    fn request(&self, source: UpdateSource) -> UpdateRequest {
        UpdateRequest { relaunch: false, ..UpdateRequest::new(source, self.activation()) }
    }

    fn archive_request(&self) -> UpdateRequest {
        self.request(UpdateSource::Archive(self.zip.clone()))
    }

    /// A ZIP of `entries` (name -> bytes) in the downloads folder.
    fn zip_of(&self, name: &str, entries: &[(String, Vec<u8>)]) -> PathBuf {
        let path = self.downloads.path().join(name);
        let mut writer = zip::ZipWriter::new(std::fs::File::create(&path).unwrap());
        let options = zip::write::SimpleFileOptions::default();
        for (member, bytes) in entries {
            writer.start_file(member.as_str(), options).unwrap();
            writer.write_all(bytes).unwrap();
        }
        writer.finish().unwrap();
        path
    }

    /// The new package's members as ZIP entries, with `edit` applied to
    /// the package-relative map first.
    fn entries(&self, edit: impl FnOnce(&mut BTreeMap<String, Vec<u8>>)) -> Vec<(String, Vec<u8>)> {
        let mut files = self.new_files.clone();
        edit(&mut files);
        files.into_iter().map(|(path, bytes)| (format!("{PREFIX}/{path}"), bytes)).collect()
    }

    /// A copy of the new package folder changed by `edit`, republished
    /// (consistent, signed manifest).
    fn variant(&self, name: &str, edit: impl FnOnce(&Path)) -> PathBuf {
        let folder = self.downloads.path().join(name);
        update_fixture::copy_tree(&self.new_folder, &folder);
        edit(&folder);
        let manifest: Value = serde_json::from_slice(&self.new_files["docs/release-manifest.json"]).unwrap();
        let roles: Vec<(String, String)> = manifest["files"]
            .as_array()
            .unwrap()
            .iter()
            .map(|item| (item["path"].as_str().unwrap().to_string(), item["role"].as_str().unwrap().to_string()))
            .collect();
        let list: Vec<(String, &str)> = roles.iter().map(|(path, role)| (path.clone(), role.as_str())).collect();
        publish(&folder, "0.8.1", &list, &self.private_key);
        folder
    }

    /// Re-signs a manifest edited by `edit` and returns the ZIP entries.
    fn resigned(&self, edit: impl FnOnce(&mut Value)) -> Vec<(String, Vec<u8>)> {
        let mut manifest: Value = serde_json::from_slice(&self.new_files["docs/release-manifest.json"]).unwrap();
        edit(&mut manifest);
        let bytes = forge_base::legacy_json::pretty_file_bytes(&manifest);
        let signature = serde_json::to_vec(&sign(&bytes, &self.private_key).unwrap()).unwrap();
        self.entries(|files| {
            files.insert("docs/release-manifest.json".into(), bytes);
            files.insert("docs/release-signature.json".into(), signature);
        })
    }
}

fn zip_folder(folder: &Path, prefix: &str, output: &Path) {
    let mut writer = zip::ZipWriter::new(std::fs::File::create(output).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    writer.add_directory(format!("{prefix}/"), options).unwrap();
    let mut stack = vec![folder.to_path_buf()];
    while let Some(directory) = stack.pop() {
        for entry in std::fs::read_dir(&directory).unwrap() {
            let entry = entry.unwrap();
            if entry.file_type().unwrap().is_dir() {
                stack.push(entry.path());
                continue;
            }
            let relative = entry.path().strip_prefix(folder).unwrap().to_string_lossy().replace('\\', "/");
            writer.start_file(format!("{prefix}/{relative}"), options).unwrap();
            writer.write_all(&std::fs::read(entry.path()).unwrap()).unwrap();
        }
    }
    writer.finish().unwrap();
}

/// Every file under `root` with its bytes (lock files excluded).
fn tree(root: &Path) -> BTreeMap<String, Vec<u8>> {
    let mut files = BTreeMap::new();
    let mut stack = vec![root.to_path_buf()];
    while let Some(directory) = stack.pop() {
        for entry in std::fs::read_dir(&directory).unwrap() {
            let entry = entry.unwrap();
            let path = entry.path();
            if entry.file_type().unwrap().is_dir() {
                stack.push(path);
            } else if !path.to_string_lossy().ends_with(".lock") {
                let relative = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
                // A file another program holds is recorded as such.
                files.insert(relative, std::fs::read(&path).unwrap_or_else(|_| b"<held>".to_vec()));
            }
        }
    }
    files
}

fn refused(site: &Site, request: &UpdateRequest, options: &PlanOptions) -> forge_base::ForgeError {
    let before = tree(site.activation());
    let error = plan(request, options).unwrap_err();
    assert_eq!(tree(site.activation()), before, "a refused plan wrote something: {}", error.message);
    error
}

#[test]
fn the_plan_previews_a_signed_update_and_writes_nothing() {
    let site = Site::new();
    let before = tree(site.activation());
    let Planned::Ready(planned) = plan(&site.archive_request(), &site.options()).unwrap() else { panic!("not ready") };
    assert_eq!(tree(site.activation()), before);
    let summary = planned.summary();
    assert_eq!(summary["previous_version"], "0.8.0");
    assert_eq!(summary["version"], "0.8.1");
    assert_eq!(summary["authenticity"], json!({"status": "signed", "key_id": site.key.key_id}));
    assert_eq!(summary["installed_state"], "ready");
    assert_eq!(summary["recipe"], RECIPE);
    assert_eq!(summary["remove"], 1, "docs/old-only.md");
    assert!(summary["replace"].as_u64().unwrap() >= 6, "{summary}");
    assert_eq!(summary["locally_modified"], json!([]));
    assert_eq!(planned.authenticity, Authenticity::Signed { key_id: site.key.key_id.clone() });

    // A package file the user changed is reported before it is replaced.
    std::fs::write(site.activation().join("docs/shared.md"), b"edited by the user").unwrap();
    let Planned::Ready(planned) = plan(&site.archive_request(), &site.options()).unwrap() else { panic!() };
    assert_eq!(planned.summary()["locally_modified"], json!(["docs/shared.md"]));
}

#[test]
fn authenticity_is_required_and_the_developer_override_is_reported() {
    let site = Site::new();
    let unsigned = site.zip_of(
        "unsigned.zip",
        &site.entries(|files| {
            files.remove("docs/release-signature.json");
        }),
    );
    let error = refused(&site, &site.request(UpdateSource::Archive(unsigned.clone())), &site.options());
    assert_eq!((error.kind, error.reason), (ErrorKind::ArtifactIntegrity, Some(Reason::UnsignedPackage)));
    assert_eq!(explain(&error).id, "update.unsigned");

    let stranger =
        PlanOptions { trust: Trust { keys: vec![generate_test_key().1], allow_unsigned: false }, ..site.options() };
    let error = refused(&site, &site.archive_request(), &stranger);
    assert_eq!(error.reason, Some(Reason::InvalidSignature));
    assert_eq!(explain(&error).id, "update.bad_signature");

    // A manifest changed after signing.
    let tampered = site.zip_of(
        "tampered.zip",
        &site.entries(|files| {
            let manifest = files.get_mut("docs/release-manifest.json").unwrap();
            let text = String::from_utf8(manifest.clone()).unwrap().replace("0.8.1", "0.9.9");
            *manifest = text.into_bytes();
        }),
    );
    let error = refused(&site, &site.request(UpdateSource::Archive(tampered)), &site.options());
    assert_eq!(error.reason, Some(Reason::InvalidSignature));

    let developer = PlanOptions { trust: Trust { keys: Vec::new(), allow_unsigned: true }, ..site.options() };
    let Planned::Ready(planned) = plan(&site.request(UpdateSource::Archive(unsigned)), &developer).unwrap() else {
        panic!()
    };
    assert_eq!(
        planned.summary()["authenticity"],
        json!({"status": "unsigned_developer_override", "reason": "unsigned"})
    );
}

#[test]
fn unsafe_or_inconsistent_archives_are_refused() {
    let site = Site::new();
    let options = site.options();
    let cases: Vec<(&str, Vec<(String, Vec<u8>)>)> = vec![
        ("traversal", { let mut e = site.entries(|_| {}); e.push((format!("{PREFIX}/../evil.txt"), b"x".to_vec())); e }),
        ("absolute", { let mut e = site.entries(|_| {}); e.push(("/evil.txt".into(), b"x".to_vec())); e }),
        ("drive", { let mut e = site.entries(|_| {}); e.push((format!("C:/{PREFIX}/evil.txt"), b"x".to_vec())); e }),
        ("stream", { let mut e = site.entries(|_| {}); e.push((format!("{PREFIX}/docs/a.md:evil"), b"x".to_vec())); e }),
        ("two packages", { let mut e = site.entries(|_| {}); e.push(("Other/README.md".into(), b"x".to_vec())); e }),
        ("file at the top", { let mut e = site.entries(|_| {}); e.push(("README.md".into(), b"x".to_vec())); e }),
        ("undeclared member", { let mut e = site.entries(|_| {}); e.push((format!("{PREFIX}/docs/extra.md"), b"x".to_vec())); e }),
        ("case collision", { let mut e = site.entries(|_| {}); e.push((format!("{PREFIX}/readme.MD"), b"x".to_vec())); e }),
        ("missing member", site.entries(|files| { files.remove("docs/new.md"); })),
        ("size differs", site.entries(|files| { files.insert("docs/new.md".into(), b"longer than declared".to_vec()); })),
        ("no manifest", site.entries(|files| { files.remove("docs/release-manifest.json"); })),
        ("corrupt manifest", site.resigned(|manifest| *manifest = json!("not an object"))),
        ("wrong architecture", site.resigned(|manifest| manifest["release"]["target"] = json!("x86_64-unknown-linux-gnu"))),
        ("not precompiled", site.resigned(|manifest| manifest["release"]["distribution_model"] = json!("local_compile_v1"))),
        ("missing titles", site.resigned(|manifest| {
            let files = manifest["files"].as_array_mut().unwrap();
            files.retain(|item| !item["path"].as_str().unwrap().starts_with("titles/"));
        })),
        ("outside the package folders", site.resigned(|manifest| {
            manifest["files"].as_array_mut().unwrap().push(json!({"path": "data/config/TriAevum.json", "role": "configuration", "bytes": 1, "sha256": "a".repeat(64)}));
        })),
        ("private payload", site.resigned(|manifest| {
            manifest["files"].as_array_mut().unwrap().push(json!({"path": "resources/game.tam", "role": "title_neutral_resource", "bytes": 1, "sha256": "a".repeat(64)}));
        })),
        ("no helper", site.resigned(|manifest| {
            manifest["files"].as_array_mut().unwrap().retain(|item| item["path"] != "forge/triaevum-update-helper.exe");
        })),
    ];
    for (name, entries) in cases {
        let zip = site.zip_of(&format!("{}.zip", name.replace(' ', "-")), &entries);
        let error = refused(&site, &site.request(UpdateSource::Archive(zip)), &options);
        let expected =
            if name == "wrong architecture" { ErrorKind::IncompatibleTarget } else { ErrorKind::ArtifactIntegrity };
        assert_eq!(error.kind, expected, "{name}: {}", error.message);
        eprintln!("{name}: {}", error.message);
    }
    let link = site.downloads.path().join("link.zip");
    let mut writer = zip::ZipWriter::new(std::fs::File::create(&link).unwrap());
    let options_zip = zip::write::SimpleFileOptions::default();
    for (member, bytes) in site.entries(|_| {}) {
        writer.start_file(member, options_zip).unwrap();
        writer.write_all(&bytes).unwrap();
    }
    writer.add_symlink(format!("{PREFIX}/docs/link.md"), "../../outside", options_zip).unwrap();
    writer.finish().unwrap();
    let error = refused(&site, &site.request(UpdateSource::Archive(link)), &options);
    assert!(error.message.contains("is a link"), "{}", error.message);
    let garbage = site.downloads.path().join("garbage.zip");
    std::fs::write(&garbage, b"not a zip").unwrap();
    assert_eq!(
        refused(&site, &site.request(UpdateSource::Archive(garbage)), &options).kind,
        ErrorKind::ArtifactIntegrity
    );
}

#[test]
fn versions_space_and_installation_preconditions() {
    let site = Site::new();
    // The same package: nothing to do.
    let same = site.zip_of(
        "same.zip",
        &site.old_files.iter().map(|(path, bytes)| (format!("{PREFIX}/{path}"), bytes.clone())).collect::<Vec<_>>(),
    );
    assert_eq!(
        plan(&site.request(UpdateSource::Archive(same)), &site.options()).unwrap_display(),
        json!({"status": "unchanged", "reason": "current", "version": "0.8.0"})
    );
    // Low space.
    let low = PlanOptions { available_space: Some(4096), ..site.options() };
    let error = refused(&site, &site.archive_request(), &low);
    assert_eq!((error.kind, error.reason), (ErrorKind::StorageFull, Some(Reason::InsufficientSpace)));
    // A running game.
    let lock = forge_core::session::session_lock_path(site.activation(), HostOs::Windows);
    std::fs::create_dir_all(lock.parent().unwrap()).unwrap();
    let game = std::fs::File::create(&lock).unwrap();
    game.lock().unwrap();
    let error = refused(&site, &site.archive_request(), &site.options());
    assert_eq!((error.kind, error.reason), (ErrorKind::Busy, Some(Reason::GameRunning)));
    drop(game);
    // No release inventory in the installation.
    let manifest = site.activation().join("docs/release-manifest.json");
    let saved = std::fs::read(&manifest).unwrap();
    std::fs::remove_file(&manifest).unwrap();
    let error = refused(&site, &site.archive_request(), &site.options());
    assert_eq!(error.kind, ErrorKind::IncompatibleTarget);
    std::fs::write(&manifest, saved).unwrap();
    // A package without the installed revision.
    let folder = site.variant("other-revision", |folder| {
        let path = folder.join("recipes/precompiled-titles.json");
        let mut catalog = Value::Object(load_json_object(&path).unwrap());
        catalog["titles"][0]["recipe"] = json!("another-revision");
        write(&path, catalog.to_string().as_bytes());
    });
    let error = refused(&site, &site.request(UpdateSource::Folder(folder)), &site.options());
    assert_eq!(error.reason, Some(Reason::MissingModule));
    assert_eq!(explain(&error).id, "package.component_missing");
}

#[test]
fn a_downgrade_needs_confirmation() {
    let site = Site::with_version("0.7.9");
    let answer = plan(&site.archive_request(), &site.options()).unwrap_display();
    assert_eq!(
        answer,
        json!({"status": "confirmation_required", "reason": "downgrade", "installed_version": "0.8.0", "present_version": "0.7.9"})
    );
    let request = UpdateRequest { allow_downgrade: true, ..site.archive_request() };
    assert!(matches!(plan(&request, &site.options()).unwrap(), Planned::Ready(_)));
}

#[test]
fn staging_verifies_every_file_and_the_catalogue_bindings() {
    let site = Site::new();
    let Planned::Ready(planned) = plan(&site.archive_request(), &site.options()).unwrap() else { panic!() };
    let commands = Commands::for_plan(&planned);
    let staged = stage(&planned, &commands, &CancelToken::new()).unwrap();
    assert!(staged.staging.starts_with(site.activation()));
    assert_eq!(std::fs::read(staged.staging.join("package").join(host().runtime)).unwrap(), NEW_RUNTIME);
    assert_eq!(std::fs::read(staged.helper()).unwrap(), b"new helper");
    assert_eq!(staged.proposal["previous_version"], "0.8.0");
    assert_eq!(commands.rebind.program, site.activation().join("forge/triaevum-forge.exe"));
    assert_eq!(commands.rebind.arguments[0], "refresh");
    std::fs::remove_dir_all(&staged.staging).unwrap();

    // A catalogue whose title module record does not match the module: the
    // manifest is consistent, the catalogue binding is not.
    let folder = site.variant("stale-pair", |folder| {
        write(&folder.join(format!("titles/{RECIPE}/{}", host().title_module)), b"MZ another title module");
    });
    let before = tree(site.activation());
    let Planned::Ready(planned) = plan(&site.request(UpdateSource::Folder(folder)), &site.options()).unwrap() else {
        panic!()
    };
    let error = stage(&planned, &Commands::for_plan(&planned), &CancelToken::new()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::ArtifactIntegrity, "{}", error.message);
    assert_eq!(tree(site.activation()), before, "the failed staging was removed");
}

trait Display {
    fn unwrap_display(self) -> Value;
}

impl Display for forge_base::Result<Planned> {
    fn unwrap_display(self) -> Value {
        match self.unwrap() {
            Planned::Done(value) => value,
            Planned::Ready(plan) => plan.summary(),
        }
    }
}

// ---------------------------------------------------------------------------
// Child roles.

const ROLE_PARENT: &str = "UPDATE_TEST_PARENT";
const ROLE_HELPER: &str = "UPDATE_TEST_HELPER";
const ACTIVATION: &str = "UPDATE_TEST_ACTIVATION";

fn env(name: &str) -> Option<String> {
    std::env::var(name).ok().filter(|value| !value.is_empty())
}

fn entry(name: &str) -> Program {
    let arguments = [name, "--exact", "--nocapture", "--test-threads=1"].map(OsString::from).to_vec();
    Program { program: std::env::current_exe().unwrap(), arguments }
}

fn faults(variable: &str) -> Faults {
    env(variable).and_then(|text| text.parse().ok()).map(Faults::at).unwrap_or_default()
}

/// Forge: plan, stage and hand off, then exit.
#[test]
fn update_parent_entry() {
    if env(ROLE_PARENT).is_none() {
        return;
    }
    let activation = PathBuf::from(env(ACTIVATION).unwrap());
    let source = PathBuf::from(env("UPDATE_TEST_SOURCE").unwrap());
    let source = if source.is_dir() { UpdateSource::Folder(source) } else { UpdateSource::Archive(source) };
    let request = UpdateRequest {
        relaunch: false,
        allow_downgrade: env("UPDATE_TEST_ALLOW_DOWNGRADE").is_some(),
        ..UpdateRequest::new(source, &activation)
    };
    let public = env("UPDATE_TEST_PUBLIC_KEY").unwrap();
    let mut key = [0u8; 32];
    for (index, byte) in key.iter_mut().enumerate() {
        *byte = u8::from_str_radix(&public[index * 2..index * 2 + 2], 16).unwrap();
    }
    let options =
        PlanOptions { trust: Trust { keys: vec![TrustedKey::new(key)], allow_unsigned: false }, available_space: None };
    let Planned::Ready(planned) = plan(&request, &options).unwrap() else { panic!("nothing to update") };
    let commands = Commands {
        rebind: entry("update_engine_entry"),
        validate: entry("update_validate_entry"),
        relaunch: env("UPDATE_TEST_RELAUNCH").map(|program| Program { program: program.into(), arguments: Vec::new() }),
    };
    let staged = stage(&planned, &commands, &CancelToken::new()).unwrap();
    let result = hand_off(&staged, &entry("update_helper_entry"), &faults("UPDATE_TEST_PARENT_ABORT_AT")).unwrap();
    println!("{result}");
    std::process::exit(0);
}

/// The helper, with fault injection; records its process id and how many
/// boundaries it passed.
#[test]
fn update_helper_entry() {
    if env(ROLE_HELPER).is_none() || env(PROPOSAL_VARIABLE).is_none() {
        return;
    }
    let activation = PathBuf::from(env(ACTIVATION).unwrap());
    let marker = activation.parent().unwrap().join("helper.pid");
    std::fs::write(&marker, std::process::id().to_string()).unwrap();
    let faults = faults("UPDATE_TEST_HELPER_ABORT_AT");
    let status = update_helper::run(HelperArgs::from_environment(), &faults);
    std::fs::write(activation.parent().unwrap().join("helper.boundaries"), faults.passed().to_string()).unwrap();
    std::process::exit(status);
}

/// The new engine's `refresh` (the real one, with the fake runtime).
#[test]
fn update_engine_entry() {
    if env(DELEGATION_VARIABLE).is_none() || env(ACTIVATION).is_none() {
        return;
    }
    let activation = PathBuf::from(env(ACTIVATION).unwrap());
    if env("UPDATE_TEST_ENGINE").as_deref() == Some("fail") {
        println!("{}", json!({"status": "error", "error": "simulated new engine startup failure"}));
        std::process::exit(1);
    }
    let request = RefreshRequest {
        allow_downgrade: env("UPDATE_TEST_ALLOW_DOWNGRADE").is_some(),
        ..RefreshRequest::package(&activation)
    };
    match refresh_installation(&request, &FakeRuntime::default(), &mut |_, _| {}, &CancelToken::new()) {
        Ok(result) => {
            println!("{result}");
            std::process::exit(0);
        }
        Err(error) => {
            println!("{}", json!({"status": "error", "error": error.message}));
            std::process::exit(1);
        }
    }
}

/// The new engine's `status`.
#[test]
fn update_validate_entry() {
    if env(DELEGATION_VARIABLE).is_none() || env(ACTIVATION).is_none() {
        return;
    }
    let activation = PathBuf::from(env(ACTIVATION).unwrap());
    if env("UPDATE_TEST_VALIDATE").as_deref() == Some("wrong") {
        println!("{}", json!({"format": "triaevum_installation_state_v1", "state": "repair_needed"}));
        std::process::exit(0);
    }
    println!("{}", inspect(&InspectRequest::package(&activation)).to_value());
    std::process::exit(0);
}

#[derive(Default)]
struct Run {
    parent_abort: Option<usize>,
    helper_abort: Option<usize>,
    engine_fails: bool,
    validation_fails: bool,
    relaunch: Option<PathBuf>,
    source: Option<PathBuf>,
}

/// What a run left.
struct Outcome {
    helper_started: bool,
    helper_boundaries: Option<usize>,
}

fn wait_for_process(pid: u32) {
    use windows_sys::Win32::Foundation::CloseHandle;
    use windows_sys::Win32::System::Threading::{OpenProcess, WaitForSingleObject, PROCESS_SYNCHRONIZE};
    // SAFETY: plain Win32 calls; a null handle means the process is gone.
    unsafe {
        let handle = OpenProcess(PROCESS_SYNCHRONIZE, 0, pid);
        if !handle.is_null() {
            WaitForSingleObject(handle, 120_000);
            CloseHandle(handle);
        }
    }
}

/// Runs Forge (a child) that hands off to the helper (a grandchild), and
/// waits until the helper has ended.
fn run_update(site: &Site, run: &Run) -> Outcome {
    let work = site.activation().parent().unwrap().to_path_buf();
    for marker in ["helper.pid", "helper.boundaries"] {
        let _ = std::fs::remove_file(work.join(marker));
    }
    let source = run.source.clone().unwrap_or_else(|| site.zip.clone());
    let mut command = Command::new(std::env::current_exe().unwrap());
    command
        .args(["update_parent_entry", "--exact", "--nocapture", "--test-threads=1"])
        .env(ROLE_PARENT, "1")
        .env(ROLE_HELPER, "1")
        .env(ACTIVATION, site.activation())
        .env("UPDATE_TEST_SOURCE", &source)
        .env("UPDATE_TEST_PUBLIC_KEY", hex(&site.key.public_key))
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    let optional = [
        ("UPDATE_TEST_PARENT_ABORT_AT", run.parent_abort.map(|n| n.to_string())),
        ("UPDATE_TEST_HELPER_ABORT_AT", run.helper_abort.map(|n| n.to_string())),
        ("UPDATE_TEST_ENGINE", run.engine_fails.then(|| "fail".to_string())),
        ("UPDATE_TEST_VALIDATE", run.validation_fails.then(|| "wrong".to_string())),
        ("UPDATE_TEST_RELAUNCH", run.relaunch.as_ref().map(|path| path.to_string_lossy().into_owned())),
    ];
    for (name, value) in optional {
        match value {
            Some(value) => command.env(name, value),
            None => command.env_remove(name),
        };
    }
    command.env_remove(DELEGATION_VARIABLE).env_remove(PROPOSAL_VARIABLE);
    let status = command.status().unwrap();
    if run.parent_abort.is_none() {
        assert!(status.success(), "Forge child failed: {status:?}");
    }
    // The helper writes its marker as soon as it starts.
    let started = Instant::now();
    let mut pid = None;
    while started.elapsed() < Duration::from_secs(5) {
        if let Ok(text) = std::fs::read_to_string(work.join("helper.pid")) {
            pid = text.trim().parse().ok();
            if pid.is_some() {
                break;
            }
        }
        std::thread::sleep(Duration::from_millis(50));
    }
    if let Some(pid) = pid {
        wait_for_process(pid);
    }
    let boundaries =
        std::fs::read_to_string(work.join("helper.boundaries")).ok().and_then(|text| text.trim().parse().ok());
    Outcome { helper_started: pid.is_some(), helper_boundaries: boundaries }
}

#[derive(Debug, PartialEq, Eq)]
enum Tuple {
    Old,
    New,
}

/// Recovers as the next Forge would, then checks that the installation is
/// exactly the old or the new package, Ready, with every user file kept.
fn recover_and_validate(site: &Site) -> Tuple {
    let activation = site.activation();
    if update_gate::pending(activation) {
        assert!(matches!(
            inspect(&InspectRequest::package(activation)),
            InstallationState::RepairNeeded { reason: RepairReason::InterruptedUpdate, .. }
        ));
        update_journal::recover(activation).unwrap().expect("a pending journal is settled");
    }
    cleanup_stale(activation);
    assert!(!update_gate::pending(activation));
    assert!(!update_gate::held(activation, HostOs::Windows));
    let leftovers: Vec<String> = std::fs::read_dir(activation)
        .unwrap()
        .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
        .filter(|name| name.starts_with(".update-"))
        .collect();
    assert!(leftovers.is_empty(), "{leftovers:?}");
    let update_directory = update_gate::update_directory(activation);
    assert!(!update_directory.join("old").exists() && !update_directory.join("activation").exists());

    let runtime = std::fs::read(activation.join(host().installed_runtime)).unwrap();
    let (tuple, files, other, root_runtime, plugin) = if runtime == NEW_RUNTIME {
        (Tuple::New, &site.new_files, &site.old_files, NEW_RUNTIME, NEW_PLUGIN)
    } else {
        (Tuple::Old, &site.old_files, &site.new_files, OLD_RUNTIME, OLD_PLUGIN)
    };
    for (path, bytes) in files {
        // Activation moved the packaged runtime to the root (checked below).
        if path == host().runtime && !activation.join(path).exists() {
            continue;
        }
        assert_eq!(&std::fs::read(activation.join(path)).unwrap(), bytes, "{tuple:?}: {path}");
    }
    for path in other.keys().filter(|path| !files.contains_key(*path)) {
        assert!(!activation.join(path).exists(), "{tuple:?}: {path} belongs to the other version");
    }
    assert_eq!(
        std::fs::read(activation.join(host().installed_runtime)).unwrap(),
        root_runtime,
        "{tuple:?}: root runtime"
    );
    let state = inspect(&InspectRequest::package(activation));
    assert!(matches!(state, InstallationState::Ready { .. }), "{tuple:?}: {state:?}");
    let receipt = receipt(&site.fixture);
    assert_eq!(receipt["runtime_sha256"], sha256_hex(root_runtime), "{tuple:?}");
    assert_eq!(receipt["plugin_sha256"], sha256_hex(plugin), "{tuple:?}");
    assert_kept(&site.kept);
    assert_eq!(config_files(&site.fixture.data()), site.settings, "{tuple:?}: settings keep their bytes");
    tuple
}

fn result(site: &Site) -> serde_json::Map<String, Value> {
    load_json_object(&update_journal::result_path(site.activation())).unwrap()
}

#[test]
fn a_full_update_from_the_zip_keeps_everything() {
    let site = Site::new();
    let outcome = run_update(&site, &Run::default());
    assert!(outcome.helper_started);
    let result = result(&site);
    assert_eq!(result["status"], "updated", "{result:?}");
    assert_eq!(result["version"], "0.8.1");
    assert_eq!(result["previous_version"], "0.8.0");
    assert_eq!(result["config_changes"], json!([]));
    assert_eq!(recover_and_validate(&site), Tuple::New);
    let record =
        load_json_object(&update_fixture::title_directory(&site.fixture).join("installed-package.json")).unwrap();
    assert_eq!(record["version"], "0.8.1");
    // The update was the only one: a second plan finds nothing to do.
    assert_eq!(
        plan(&site.archive_request(), &site.options()).unwrap_display(),
        json!({"status": "unchanged", "reason": "current", "version": "0.8.1"})
    );
}

#[test]
fn updating_from_an_extracted_folder_and_a_legacy_root_profile() {
    // beta.1.2: the new Forge runs from the extracted folder with
    // --update-target; an old launch profile is left in the root.
    let site = Site::new();
    let legacy = site.activation().join("TriAevum.launch.json");
    std::fs::write(&legacy, b"{\"legacy\": true}").unwrap();
    run_update(&site, &Run { source: Some(site.new_folder.clone()), ..Run::default() });
    assert_eq!(result(&site)["status"], "updated");
    assert_eq!(std::fs::read(&legacy).unwrap(), b"{\"legacy\": true}", "the legacy root profile is kept");
    std::fs::remove_file(&legacy).unwrap();
    assert_eq!(recover_and_validate(&site), Tuple::New);
}

#[test]
fn engine_failure_and_failed_final_validation_roll_back() {
    for (name, run) in [
        ("engine startup failure", Run { engine_fails: true, ..Run::default() }),
        ("failed final validation after a committed activation", Run { validation_fails: true, ..Run::default() }),
    ] {
        let site = Site::new();
        run_update(&site, &run);
        let result = result(&site);
        assert_eq!(result["status"], "rolled_back", "{name}: {result:?}");
        assert!(result["error"].as_str().is_some(), "{name}");
        assert_eq!(recover_and_validate(&site), Tuple::Old, "{name}");
    }
}

#[test]
fn a_relaunch_failure_is_reported_separately() {
    let site = Site::new();
    let missing = site.downloads.path().join("no-such-forge.exe");
    run_update(&site, &Run { relaunch: Some(missing), ..Run::default() });
    let result = result(&site);
    assert_eq!(result["status"], "updated");
    assert!(result["relaunch_error"].as_str().unwrap().contains("no-such-forge.exe"), "{result:?}");
    assert_eq!(recover_and_validate(&site), Tuple::New);
}

/// A program holding a package file open without sharing (a permanent
/// third-party lock); dropped to release it.
struct Holder(std::process::Child);

impl Holder {
    fn hold(path: &Path) -> Self {
        let script = format!(
            "$f=[IO.File]::Open('{}','Open','ReadWrite','None'); [Console]::Out.WriteLine('held'); [Console]::Out.Flush(); Start-Sleep 120",
            path.display()
        );
        let mut child = Command::new("powershell")
            .args(["-NoProfile", "-Command", &script])
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let mut line = String::new();
        std::io::BufRead::read_line(&mut std::io::BufReader::new(child.stdout.take().unwrap()), &mut line).unwrap();
        assert_eq!(line.trim(), "held");
        Self(child)
    }
}

impl Drop for Holder {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

#[test]
fn a_permanently_held_package_file_refuses_without_changes() {
    let site = Site::new();
    let held = site.activation().join("forge/triaevum-forge.exe");
    // Already held when the update is planned.
    {
        let _holder = Holder::hold(&held);
        let error = refused(&site, &site.archive_request(), &site.options());
        assert_eq!(error.kind, ErrorKind::Busy, "{}", error.message);
        assert_eq!(explain(&error).id, "package_replace.file_holder");
        assert!(error.message.starts_with("Close TriAevum before updating"), "{}", error.message);
    }
    // Held after staging: the helper names it and changes nothing.
    let Planned::Ready(planned) = plan(&site.archive_request(), &site.options()).unwrap() else { panic!() };
    let commands =
        Commands { rebind: entry("update_engine_entry"), validate: entry("update_validate_entry"), relaunch: None };
    let staged = stage(&planned, &commands, &CancelToken::new()).unwrap();
    let proposal = staged.staging.join(forge_core::update::PROPOSAL);
    std::fs::write(&proposal, forge_base::legacy_json::pretty_file_bytes(&staged.proposal)).unwrap();
    let before = tree(site.activation());
    let holder = Holder::hold(&held);
    let status = update_helper::run(HelperArgs { proposal: Some(proposal), parent: None }, &Faults::none());
    drop(holder);
    assert_eq!(status, update_helper::EXIT_REFUSED);
    let result = result(&site);
    assert_eq!(result["status"], "refused", "{result:?}");
    assert_eq!(result["explanation"]["id"], "package_replace.file_holder");
    assert!(result["error"].as_str().unwrap().starts_with("Close TriAevum before updating"), "{result:?}");
    let unchanged = |files: BTreeMap<String, Vec<u8>>| -> BTreeMap<String, Vec<u8>> {
        files
            .into_iter()
            .filter(|(path, _)| !path.starts_with(".update-") && !path.starts_with(".forge-update/"))
            .collect()
    };
    assert_eq!(unchanged(tree(site.activation())), unchanged(before), "nothing changed");
    assert_eq!(recover_and_validate(&site), Tuple::Old);
}

#[test]
fn a_running_game_refuses_the_helper_without_changes() {
    let site = Site::new();
    let Planned::Ready(planned) = plan(&site.archive_request(), &site.options()).unwrap() else { panic!() };
    let commands =
        Commands { rebind: entry("update_engine_entry"), validate: entry("update_validate_entry"), relaunch: None };
    let staged = stage(&planned, &commands, &CancelToken::new()).unwrap();
    let proposal = staged.staging.join(forge_core::update::PROPOSAL);
    std::fs::write(&proposal, forge_base::legacy_json::pretty_file_bytes(&staged.proposal)).unwrap();
    // The game started after the plan: it holds its session lock.
    let lock = forge_core::session::session_lock_path(site.activation(), HostOs::Windows);
    let game = std::fs::File::options().read(true).write(true).create(true).truncate(false).open(&lock).unwrap();
    game.lock().unwrap();
    let status = update_helper::run(HelperArgs { proposal: Some(proposal), parent: None }, &Faults::none());
    drop(game);
    assert_eq!(status, update_helper::EXIT_REFUSED);
    let result = result(&site);
    assert_eq!(result["explanation"]["id"], "session.game_running", "{result:?}");
    assert_eq!(recover_and_validate(&site), Tuple::Old);
}

/// The kill matrix (acceptance list of WP-04B): Forge dies after each of
/// its handoff boundaries, the helper after each journal write, rename,
/// publication and cleanup step; recovery leaves a validated tuple.
#[test]
fn the_kill_matrix_always_recovers_a_validated_tuple() {
    let site = Site::new();
    let outcome = run_update(&site, &Run::default());
    let boundaries = outcome.helper_boundaries.expect("the helper reports its boundaries");
    assert_eq!(recover_and_validate(&site), Tuple::New);
    eprintln!("helper boundaries: {boundaries}");
    assert!(boundaries >= 20, "{boundaries}");
    let mut seen = Vec::new();
    for parent_abort in 1..=3 {
        let site = Site::new();
        run_update(&site, &Run { parent_abort: Some(parent_abort), ..Run::default() });
        let tuple = recover_and_validate(&site);
        // Before the helper started nothing changes; once it acknowledged,
        // it completes the update on its own.
        let expected = if parent_abort == 1 { Tuple::Old } else { Tuple::New };
        assert_eq!(tuple, expected, "parent killed at boundary {parent_abort}");
        seen.push(format!("parent@{parent_abort}={tuple:?}"));
    }
    let mut committed = false;
    for helper_abort in 1..=boundaries {
        let site = Site::new();
        let outcome = run_update(&site, &Run { helper_abort: Some(helper_abort), ..Run::default() });
        assert!(outcome.helper_boundaries.is_none(), "the helper died at boundary {helper_abort}");
        let tuple = recover_and_validate(&site);
        // Rolled back until the commit, finished forward from it.
        committed |= tuple == Tuple::New;
        assert_eq!(tuple, if committed { Tuple::New } else { Tuple::Old }, "helper killed at boundary {helper_abort}");
        seen.push(format!("helper@{helper_abort}={tuple:?}"));
    }
    assert!(committed, "the last boundaries follow the commit");
    eprintln!("kill matrix: {}", seen.join(" "));
}
