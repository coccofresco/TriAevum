// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The update helper (UX plan WP-04B steps 3 to 6), the body of
//! `triaevum-update-helper.exe` (crate `forge-update-helper`, statically
//! linked, run from the staging folder so it never depends on a file it
//! replaces).
//!
//! Started by Forge's [`crate::update::hand_off`] with the proposal's path
//! in [`PROPOSAL_VARIABLE`] and an inherited handle of the Forge process in
//! [`PARENT_VARIABLE`], it:
//!
//! 1. reads the proposal and acknowledges (`ready <id>` on its output), then
//!    waits for the Forge process to end;
//! 2. takes the update gate exclusively ([`crate::update_gate`]) and, while
//!    it changes files, the installation and game session locks; nothing is
//!    killed: a running game or a file another program holds is `busy`
//!    ("Close TriAevum before updating") and nothing changes;
//! 3. revalidates the staged files and records what is on disk now, then
//!    creates the journal and replaces the files ([`crate::update_journal`]);
//! 4. backs up the activation, releases the two locks and runs the new
//!    engine's `refresh` (04A) as its delegate, then the new engine's
//!    `status` as the final validation;
//! 5. commits, removes the old generation and relaunches Forge with the
//!    result file; any failure before the commit rolls back to the old
//!    validated tuple, and damaged rollback material keeps the journal for
//!    `recovery_required` (this helper stays in the staging folder and,
//!    started without a proposal, recovers).

use std::ffi::OsString;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant};

use forge_base::fsutil::load_json_object;
use forge_base::{ErrorKind, ForgeError, Operation, Reason, Result};
use serde_json::{json, Map, Value};

use crate::explain::explain;
use crate::layout::HostOs;
use crate::package_inventory::live_path;
use crate::update::{Program, PROPOSAL_FORMAT};
use crate::update_gate::{update_directory, DELEGATION_VARIABLE};
use crate::update_journal::{
    activation_targets, back_up_activation, digest, finish, move_root_runtime_aside, replace_files, result_path,
    rollback, validate_tuple, Entry, Faults, Generation, Journal, JOURNAL_FORMAT, RESULT_FORMAT, STAGED_PACKAGE,
};

pub const PROPOSAL_VARIABLE: &str = "TRIAEVUM_UPDATE_PROPOSAL";
pub const PARENT_VARIABLE: &str = "TRIAEVUM_UPDATE_PARENT";
/// How long the helper waits for Forge to close, and for the gate.
pub const PARENT_TIMEOUT: Duration = Duration::from_secs(120);
pub const GATE_TIMEOUT: Duration = Duration::from_secs(30);
pub const LOCK_TIMEOUT: Duration = Duration::from_secs(10);

/// Exit statuses.
pub const EXIT_UPDATED: i32 = 0;
pub const EXIT_NOTHING_PENDING: i32 = 0;
pub const EXIT_INVALID: i32 = 2;
pub const EXIT_ROLLED_BACK: i32 = 3;
pub const EXIT_REFUSED: i32 = 4;
pub const EXIT_RECOVERY_REQUIRED: i32 = 5;

fn update_error(kind: ErrorKind, message: impl Into<String>) -> ForgeError {
    ForgeError::new(kind, message).during(Operation::Update)
}

/// The helper's inputs, from its environment.
#[derive(Clone, Debug, Default)]
pub struct HelperArgs {
    pub proposal: Option<PathBuf>,
    /// The Forge process: an inherited handle value and its process id.
    pub parent: Option<(usize, u32)>,
}

impl HelperArgs {
    pub fn from_environment() -> Self {
        let proposal = std::env::var_os(PROPOSAL_VARIABLE).filter(|value| !value.is_empty()).map(PathBuf::from);
        let parent = std::env::var(PARENT_VARIABLE).ok().and_then(|value| {
            let (handle, pid) = value.split_once(':')?;
            Some((handle.parse().ok()?, pid.parse().ok()?))
        });
        Self { proposal, parent }
    }
}

/// Starts the helper for `proposal` with an inherited handle of this
/// process (never a bare process id alone: the handle pins this process
/// instance), standard output piped for the acknowledgement.
pub fn spawn_helper(helper: &Program, proposal: &Path) -> Result<Child> {
    let parent = platform::inheritable_self()?;
    let mut command = Command::new(&helper.program);
    command
        .args(&helper.arguments)
        .env(PROPOSAL_VARIABLE, proposal)
        .env(PARENT_VARIABLE, format!("{}:{}", parent.value(), std::process::id()))
        .env_remove(DELEGATION_VARIABLE)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::null());
    let spawned = platform::spawn_detached(&mut command);
    drop(parent);
    spawned.map_err(|error| {
        ForgeError::io(format!("Cannot start {}", helper.program.display()), error).during(Operation::Update)
    })
}

/// The proposal Forge wrote, checked.
fn load_proposal(path: &Path) -> Result<Map<String, Value>> {
    let proposal = load_json_object(path)?;
    let text = |key: &str| proposal.get(key).and_then(Value::as_str).unwrap_or_default();
    if text("format") != PROPOSAL_FORMAT || text("id").is_empty() || text("activation").is_empty() {
        return Err(update_error(ErrorKind::InvalidInput, "The update proposal is not valid"));
    }
    if proposal.get("changes").and_then(Value::as_array).is_none() {
        return Err(update_error(ErrorKind::InvalidInput, "The update proposal lists no changes"));
    }
    Ok(proposal)
}

fn retry<T>(timeout: Duration, mut attempt: impl FnMut() -> Result<T>) -> Result<T> {
    let started = Instant::now();
    loop {
        match attempt() {
            Err(error) if error.kind == ErrorKind::Busy && started.elapsed() < timeout => {
                std::thread::sleep(Duration::from_millis(250));
            }
            outcome => return outcome,
        }
    }
}

/// A refusal before the journal exists: nothing changed.
fn refused_result(proposal: &Map<String, Value>, error: &ForgeError) -> Value {
    let field = |key: &str| proposal.get(key).cloned().unwrap_or(Value::Null);
    json!({
        "format": RESULT_FORMAT,
        "id": field("id"),
        "status": "refused",
        "previous_version": field("previous_version"),
        "version": field("version"),
        "hud_layout": field("hud_layout"),
        "authenticity": field("authenticity"),
        "error": error.message,
        "explanation": explain(error).to_value(),
    })
}

/// The helper's whole run. Returns the process exit status.
pub fn run(args: HelperArgs, faults: &Faults) -> i32 {
    let Some(proposal_path) = args.proposal else { return recover_beside() };
    let proposal = match load_proposal(&proposal_path) {
        Ok(proposal) => proposal,
        Err(error) => {
            eprintln!("triaevum-update-helper: {}", error.message);
            return EXIT_INVALID;
        }
    };
    let id = proposal["id"].as_str().unwrap_or_default().to_string();
    {
        let mut stdout = std::io::stdout().lock();
        let _ = writeln!(stdout, "ready {id}");
        let _ = stdout.flush();
    }
    let activation = PathBuf::from(proposal["activation"].as_str().unwrap_or_default());
    let relaunch = proposal.get("relaunch").and_then(Program::parse);
    let (result, status) = match platform::wait_for_parent(args.parent, PARENT_TIMEOUT) {
        Err(error) => (refused_result(&proposal, &error), EXIT_REFUSED),
        Ok(()) => match run_update(&proposal, faults) {
            Ok(result) => {
                let status = match result["status"].as_str() {
                    Some("updated") => EXIT_UPDATED,
                    Some("rolled_back") => EXIT_ROLLED_BACK,
                    _ => EXIT_REFUSED,
                };
                (result, status)
            }
            Err(Failure::Refused(error)) => {
                let result = refused_result(&proposal, &error);
                let _ = crate::update::write_result(&activation, &result);
                // The staged files are not needed any more; this copy of the
                // helper stays until the next Forge removes the folder.
                if let Some(staging) = proposal.get("staging").and_then(Value::as_str) {
                    let _ = std::fs::remove_dir_all(Path::new(staging).join(STAGED_PACKAGE));
                }
                (result, EXIT_REFUSED)
            }
            Err(Failure::Recovery(error)) => {
                let result = load_json_object(&result_path(&activation)).map(Value::Object).unwrap_or_else(|_| {
                    json!({"format": RESULT_FORMAT, "id": id, "status": "recovery_required", "error": error.message})
                });
                (result, EXIT_RECOVERY_REQUIRED)
            }
        },
    };
    eprintln!("triaevum-update-helper: {}", result["status"].as_str().unwrap_or("unknown"));
    if let Some(program) = relaunch {
        relaunch_forge(&activation, &program);
    }
    status
}

/// Why the update did not complete.
enum Failure {
    /// Before the journal: nothing changed.
    Refused(ForgeError),
    /// The rollback itself failed; the journal stays.
    Recovery(ForgeError),
}

fn run_update(proposal: &Map<String, Value>, faults: &Faults) -> std::result::Result<Value, Failure> {
    let activation = PathBuf::from(proposal["activation"].as_str().unwrap_or_default());
    let os = HostOs::current();
    let _gate =
        retry(GATE_TIMEOUT, || crate::update_gate::take_for_update(&activation, os)).map_err(Failure::Refused)?;
    if crate::update_gate::pending(&activation) {
        let journal = crate::update_gate::journal_path(&activation);
        return Err(Failure::Refused(crate::update_gate::interrupted(&journal, Operation::Update)));
    }
    let quiet =
        retry(LOCK_TIMEOUT, || crate::session::quiesce_for_update(&activation, os)).map_err(Failure::Refused)?;
    let journal_value = preflight(proposal, &activation).map_err(Failure::Refused)?;
    let mut journal = Journal::create(&activation, journal_value).map_err(Failure::Refused)?;
    faults.boundary("journal created");
    let mut quiet = Some(quiet);
    let outcome = apply(&mut journal, &mut quiet, faults);
    match outcome {
        Ok(()) => Ok(finish(&journal, faults).unwrap_or_else(|error| {
            // Committed: the next Forge finishes the cleanup.
            let mut result = crate::update_journal::result_value(&journal, "updated", None);
            result["cleanup_pending"] = json!(error.message);
            let _ = crate::update::write_result(&activation, &result);
            result
        })),
        Err(error) => {
            // Before the rebind the two locks are still held; after it the
            // journal (runtime guard) and the gate keep everyone out, and an
            // inner activation the engine left is settled with its own lock.
            let cause = error.message.clone();
            let mut result = rollback(&mut journal, &cause).map_err(Failure::Recovery)?;
            result["explanation"] = explain(&error).to_value();
            let _ = crate::update::write_result(&activation, &result);
            drop(quiet);
            Ok(result)
        }
    }
}

/// Steps 3 to 6 up to the commit.
fn apply(journal: &mut Journal, quiet: &mut Option<crate::session::Quiescence>, faults: &Faults) -> Result<()> {
    let activation = journal.activation();
    move_root_runtime_aside(journal, faults)?;
    replace_files(journal, faults)?;
    journal.advance("replaced")?;
    faults.boundary("replaced");
    let data_root = PathBuf::from(journal.text("data_root"));
    let targets = activation_targets(&activation, &data_root)?;
    back_up_activation(journal, &targets, faults)?;
    // The new engine takes these locks itself.
    *quiet = None;
    let rebind = Program::parse(journal.value.get("rebind").unwrap_or(&Value::Null))
        .ok_or_else(|| update_error(ErrorKind::InvalidInput, "The update proposal has no rebind command"))?;
    let refreshed = run_engine(&rebind, journal.id(), "The new version could not bind your prepared game")?;
    match refreshed["status"].as_str() {
        Some("updated") | Some("unchanged") => {}
        Some("confirmation_required") => {
            return Err(update_error(
                ErrorKind::PreparationFailed,
                "The prepared game was made with a newer version; confirm the downgrade",
            ));
        }
        _ => return Err(engine_failure("The new version could not bind your prepared game", &refreshed)),
    }
    journal.set("config_changes", refreshed.get("config_changes").cloned().unwrap_or(json!([])));
    faults.boundary("published");
    journal.advance("validating")?;
    faults.boundary("validating");
    validate_tuple(journal, Generation::New)?;
    let validate = Program::parse(journal.value.get("validate").unwrap_or(&Value::Null))
        .ok_or_else(|| update_error(ErrorKind::InvalidInput, "The update proposal has no validation command"))?;
    let state = run_engine(&validate, journal.id(), "The updated installation could not be checked")?;
    let expected = match journal.text("installed_state") {
        "ready" | "update_available" => "ready",
        other => other,
    };
    let actual = state["state"].as_str().unwrap_or_default();
    if actual != expected {
        return Err(update_error(
            ErrorKind::PreparationFailed,
            format!("The updated installation is {actual}, not {expected}; the previous version was restored"),
        ));
    }
    journal.advance("committed")?;
    faults.boundary("committed");
    Ok(())
}

fn engine_failure(context: &str, output: &Value) -> ForgeError {
    let detail = output.get("error").and_then(Value::as_str).unwrap_or("no result");
    update_error(ErrorKind::PreparationFailed, format!("{context}: {detail}"))
}

/// Revalidates the staged files and the live ones, checks for holders and
/// space, and builds the journal (state `replacing`) with what is on disk.
fn preflight(proposal: &Map<String, Value>, activation: &Path) -> Result<Map<String, Value>> {
    let staging = PathBuf::from(proposal.get("staging").and_then(Value::as_str).unwrap_or_default());
    let staged_root = staging.join(STAGED_PACKAGE);
    let platform = crate::layout::host_platform()?;
    let items = proposal["changes"].as_array().cloned().unwrap_or_default();
    let mut entries = Vec::with_capacity(items.len());
    let mut old_bytes: u64 = 0;
    for item in &items {
        let path = item.get("path").and_then(Value::as_str).unwrap_or_default().to_string();
        crate::package_inventory::check_member_path(&path)?;
        if crate::package_inventory::preserved(&path) {
            return Err(update_error(ErrorKind::InvalidInput, format!("The update may not change {path}")));
        }
        let new_sha256 = item.get("new_sha256").and_then(Value::as_str).map(str::to_string);
        let new_bytes = item.get("new_bytes").and_then(Value::as_u64);
        if let Some(sha256) = &new_sha256 {
            let staged = live_path(&staged_root, &path);
            let size = std::fs::metadata(&staged).map(|metadata| metadata.len()).ok();
            if size != new_bytes || digest(&staged).ok().as_deref() != Some(sha256.as_str()) {
                return Err(crate::package_inventory::package_error(format!(
                    "The staged {path} changed after it was verified"
                )));
            }
        }
        let live = live_path(activation, &path);
        let (old_sha256, old_size) = match std::fs::symlink_metadata(&live) {
            Ok(metadata) if metadata.is_file() => {
                ensure_free(&live)?;
                (Some(crate::package_inventory::live_file_digest(&live)?), Some(metadata.len()))
            }
            Ok(_) => {
                return Err(update_error(ErrorKind::InvalidInput, format!("{} is not a regular file", live.display())))
            }
            Err(_) => (None, None),
        };
        old_bytes += old_size.unwrap_or(0);
        entries.push(Entry { path, old_sha256, old_bytes: old_size, new_sha256, new_bytes });
    }
    let root_runtime = activation.join(platform.installed_runtime);
    for loaded in [root_runtime.clone(), activation.join(platform.title_module)] {
        if loaded.is_file() {
            ensure_free(&loaded)?;
        }
    }
    let directory = update_directory(activation);
    std::fs::create_dir_all(&directory)
        .map_err(|error| ForgeError::io(format!("Cannot create {}", directory.display()), error))?;
    let required = crate::update::SPACE_MARGIN + if hard_links_work(&directory) { 0 } else { old_bytes };
    if let Some(available) = forge_native::system::available_space(activation) {
        if available < required {
            return Err(ForgeError::new(
                ErrorKind::StorageFull,
                "Not enough free space to keep the old files until the update finishes",
            )
            .during(Operation::Update)
            .because(Reason::InsufficientSpace));
        }
    }
    let root_sha256 = if root_runtime.is_file() { Some(digest(&root_runtime)?) } else { None };
    let mut journal = Map::new();
    journal.insert("format".into(), json!(JOURNAL_FORMAT));
    for key in [
        "id",
        "activation",
        "data_root",
        "staging",
        "previous_version",
        "version",
        "authenticity",
        "installed_state",
        "recipe",
        "hud_layout",
        "allow_downgrade",
        "locally_modified",
        "rebind",
        "validate",
        "relaunch",
    ] {
        journal.insert(key.into(), proposal.get(key).cloned().unwrap_or(Value::Null));
    }
    journal.insert("state".into(), json!("replacing"));
    journal.insert(
        "helper".into(),
        json!(std::env::current_exe().map(|path| path.to_string_lossy().into_owned()).unwrap_or_default()),
    );
    journal.insert("root_runtime".into(), json!({"path": platform.installed_runtime, "sha256": root_sha256}));
    journal.insert("changes".into(), Value::Array(entries.iter().map(Entry::to_value).collect()));
    Ok(journal)
}

/// No process holds `path`: a held file names its holders ("Close TriAevum
/// before updating"); nothing is closed for the user.
fn ensure_free(path: &Path) -> Result<()> {
    if platform::in_use(path) {
        return Err(crate::package_inventory::held_file(path));
    }
    Ok(())
}

fn hard_links_work(directory: &Path) -> bool {
    let probe = directory.join(format!(".link-probe-{}", std::process::id()));
    let linked = probe.with_extension("link");
    let works = std::fs::write(&probe, b"probe").is_ok() && std::fs::hard_link(&probe, &linked).is_ok();
    let _ = std::fs::remove_file(&linked);
    let _ = std::fs::remove_file(&probe);
    works
}

/// Runs a new-engine command as the journal's delegate and reads its JSON
/// result.
fn run_engine(program: &Program, journal_id: &str, context: &str) -> Result<Value> {
    let mut command = Command::new(&program.program);
    command
        .args(&program.arguments)
        .env(DELEGATION_VARIABLE, journal_id)
        .env_remove(PROPOSAL_VARIABLE)
        .env_remove(PARENT_VARIABLE)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    let mut child = platform::spawn_bound(&mut command).map_err(|error| {
        update_error(
            ErrorKind::PreparationFailed,
            format!("{context}: cannot start {}: {error}", program.program.display()),
        )
    })?;
    let mut stderr = child.stderr.take();
    let errors = std::thread::spawn(move || {
        let mut text = String::new();
        if let Some(stderr) = stderr.as_mut() {
            let _ = stderr.read_to_string(&mut text);
        }
        text
    });
    let mut output = String::new();
    if let Some(mut stdout) = child.stdout.take() {
        let _ = stdout.read_to_string(&mut output);
    }
    let status =
        child.wait().map_err(|error| ForgeError::io(format!("{context}: the engine did not finish"), error))?;
    let errors = errors.join().unwrap_or_default();
    // The whole output (the command line prints one JSON object), else the
    // last line that ends in one.
    let parsed = serde_json::from_str::<Value>(output.trim()).ok().filter(Value::is_object).or_else(|| {
        output.lines().rev().find_map(|line| {
            let start = line.find('{')?;
            serde_json::from_str::<Value>(line[start..].trim()).ok().filter(Value::is_object)
        })
    });
    match parsed {
        Some(value) if status.success() => Ok(value),
        Some(value) => Err(engine_failure(context, &value)),
        None => {
            let tail: String = errors.lines().rev().take(3).collect::<Vec<_>>().join(" | ");
            Err(update_error(ErrorKind::PreparationFailed, format!("{context} (exit {status}): {tail}")))
        }
    }
}

fn relaunch_forge(activation: &Path, program: &Program) {
    let mut command = Command::new(&program.program);
    command
        .args(&program.arguments)
        .current_dir(activation)
        .env_remove(PROPOSAL_VARIABLE)
        .env_remove(PARENT_VARIABLE)
        .env_remove(DELEGATION_VARIABLE)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    if let Err(error) = platform::spawn_detached(&mut command) {
        // Reported separately from the update's own outcome.
        let path = result_path(activation);
        if let Ok(mut result) = load_json_object(&path) {
            result.insert("relaunch_error".into(), json!(format!("{}: {error}", program.program.display())));
            let _ = crate::update::write_result(activation, &Value::Object(result));
        }
    }
}

/// Without a proposal: recovers the journal of the installation this copy
/// sits in (`A/.update-*/triaevum-update-helper.exe`), then opens Forge.
fn recover_beside() -> i32 {
    let Some(activation) = std::env::current_exe().ok().and_then(|path| Some(path.parent()?.parent()?.to_path_buf()))
    else {
        return EXIT_INVALID;
    };
    let status = match crate::update_journal::recover(&activation) {
        Ok(None) => EXIT_NOTHING_PENDING,
        Ok(Some(result)) if result["status"] == "updated" => EXIT_UPDATED,
        Ok(Some(_)) => EXIT_ROLLED_BACK,
        Err(error) => {
            eprintln!("triaevum-update-helper: {}", error.message);
            EXIT_RECOVERY_REQUIRED
        }
    };
    let forge =
        activation.join(crate::layout::host_platform().map(|platform| platform.forge).unwrap_or("TriAevumForge.exe"));
    if forge.is_file() {
        let arguments: Vec<OsString> = vec!["--update-result".into(), result_path(&activation).into()];
        relaunch_forge(&activation, &Program { program: forge, arguments });
    }
    status
}

#[cfg(windows)]
mod platform {
    use std::os::windows::io::AsRawHandle;
    use std::os::windows::process::CommandExt;
    use std::path::Path;
    use std::process::{Child, Command};
    use std::sync::Mutex;
    use std::time::Duration;

    use forge_base::{ErrorKind, ForgeError, Operation, Result};
    use windows_sys::Win32::Foundation::{CloseHandle, DuplicateHandle, HANDLE, WAIT_OBJECT_0, WAIT_TIMEOUT};
    use windows_sys::Win32::System::JobObjects::{
        AssignProcessToJobObject, CreateJobObjectW, JobObjectExtendedLimitInformation, SetInformationJobObject,
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
    };
    use windows_sys::Win32::System::Threading::{
        GetCurrentProcess, GetProcessId, WaitForSingleObject, CREATE_BREAKAWAY_FROM_JOB, CREATE_NO_WINDOW,
        PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_SYNCHRONIZE,
    };

    /// An inheritable handle of this process, closed when dropped (the
    /// child keeps its own inherited copy).
    pub struct SelfHandle(HANDLE);

    impl SelfHandle {
        pub fn value(&self) -> usize {
            self.0 as usize
        }
    }

    impl Drop for SelfHandle {
        fn drop(&mut self) {
            // SAFETY: the handle was duplicated for this object alone.
            unsafe { CloseHandle(self.0) };
        }
    }

    pub fn inheritable_self() -> Result<SelfHandle> {
        let mut handle: HANDLE = std::ptr::null_mut();
        // SAFETY: duplicates the current-process pseudo handle into this
        // process with synchronize/query rights, inheritable.
        let ok = unsafe {
            DuplicateHandle(
                GetCurrentProcess(),
                GetCurrentProcess(),
                GetCurrentProcess(),
                &mut handle,
                PROCESS_SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                1,
                0,
            )
        };
        if ok == 0 {
            return Err(ForgeError::io("Cannot prepare the update helper", std::io::Error::last_os_error()));
        }
        Ok(SelfHandle(handle))
    }

    /// Outside Forge's job (when it allows that), without a console window.
    pub fn spawn_detached(command: &mut Command) -> std::io::Result<Child> {
        command.creation_flags(CREATE_BREAKAWAY_FROM_JOB | CREATE_NO_WINDOW);
        match command.spawn() {
            Ok(child) => Ok(child),
            Err(_) => command.creation_flags(CREATE_NO_WINDOW).spawn(),
        }
    }

    /// The kill-on-close job of the helper's children: if the helper dies,
    /// the new engine it started dies with it (its activation journal then
    /// remains for the recovery).
    static JOB: Mutex<usize> = Mutex::new(0);

    pub fn spawn_bound(command: &mut Command) -> std::io::Result<Child> {
        command.creation_flags(CREATE_NO_WINDOW);
        let child = command.spawn()?;
        let mut job = JOB.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        // SAFETY: Win32 calls with checked results; the limit structure is a
        // zeroed plain-data struct whose size is passed alongside it.
        unsafe {
            if *job == 0 {
                let handle = CreateJobObjectW(std::ptr::null(), std::ptr::null());
                if !handle.is_null() {
                    let mut limits: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
                    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                    let size = std::mem::size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32;
                    let configured = SetInformationJobObject(
                        handle,
                        JobObjectExtendedLimitInformation,
                        (&limits as *const JOBOBJECT_EXTENDED_LIMIT_INFORMATION).cast(),
                        size,
                    );
                    if configured != 0 {
                        *job = handle as usize;
                    } else {
                        CloseHandle(handle);
                    }
                }
            }
            if *job != 0 {
                AssignProcessToJobObject(*job as HANDLE, child.as_raw_handle() as HANDLE);
            }
        }
        Ok(child)
    }

    /// Waits for the Forge process the inherited handle names.
    pub fn wait_for_parent(parent: Option<(usize, u32)>, timeout: Duration) -> Result<()> {
        let Some((handle, pid)) = parent else { return Ok(()) };
        let handle = handle as HANDLE;
        // SAFETY: the value came from our parent as an inherited handle; a
        // handle that is not that process fails the identity check.
        let identity = unsafe { GetProcessId(handle) };
        if identity != pid {
            return Err(ForgeError::new(ErrorKind::InvalidInput, "The update helper was not started by Forge"));
        }
        // SAFETY: a valid process handle checked above.
        let waited = unsafe { WaitForSingleObject(handle, timeout.as_millis().min(u32::MAX as u128) as u32) };
        // SAFETY: closes our inherited copy.
        unsafe { CloseHandle(handle) };
        match waited {
            WAIT_OBJECT_0 => Ok(()),
            WAIT_TIMEOUT => Err(ForgeError::new(ErrorKind::Busy, "Forge did not close, so the update did not start")
                .during(Operation::Update)),
            _ => Err(ForgeError::io("Cannot wait for Forge to close", std::io::Error::last_os_error())),
        }
    }

    /// Whether another process has `path` open without sharing it for
    /// writing (a running executable or library, an editor, a scanner).
    pub fn in_use(path: &Path) -> bool {
        use std::os::windows::fs::OpenOptionsExt;
        const ERROR_SHARING_VIOLATION: i32 = 32;
        const ERROR_LOCK_VIOLATION: i32 = 33;
        let opened = std::fs::OpenOptions::new().read(true).write(true).share_mode(0).open(path);
        match opened {
            Ok(_) => false,
            Err(error) => matches!(error.raw_os_error(), Some(ERROR_SHARING_VIOLATION | ERROR_LOCK_VIOLATION)),
        }
    }
}

#[cfg(not(windows))]
mod platform {
    use std::path::Path;
    use std::process::{Child, Command};
    use std::time::{Duration, Instant};

    use forge_base::{ErrorKind, ForgeError, Operation, Result};

    pub struct SelfHandle;

    impl SelfHandle {
        pub fn value(&self) -> usize {
            0
        }
    }

    pub fn inheritable_self() -> Result<SelfHandle> {
        Ok(SelfHandle)
    }

    pub fn spawn_detached(command: &mut Command) -> std::io::Result<Child> {
        command.spawn()
    }

    pub fn spawn_bound(command: &mut Command) -> std::io::Result<Child> {
        command.spawn()
    }

    pub fn wait_for_parent(parent: Option<(usize, u32)>, timeout: Duration) -> Result<()> {
        let Some((_, pid)) = parent else { return Ok(()) };
        let started = Instant::now();
        while Path::new(&format!("/proc/{pid}")).exists() {
            if started.elapsed() > timeout {
                return Err(ForgeError::new(ErrorKind::Busy, "Forge did not close, so the update did not start")
                    .during(Operation::Update));
            }
            std::thread::sleep(Duration::from_millis(100));
        }
        Ok(())
    }

    pub fn in_use(_path: &Path) -> bool {
        false
    }
}
