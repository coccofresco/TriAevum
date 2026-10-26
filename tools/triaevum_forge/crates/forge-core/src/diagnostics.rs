// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Diagnostics for people and for the maintainer (UX plan WP-03): what the
//! last game run and the last Forge job did, a copyable report bounded to
//! [`TEXT_LIMIT`], and a support bundle (a zip) the user previews and saves.
//!
//! Privacy is the contract here. The bundle is an allowlist: Forge's log,
//! the game's log and its two previous files, the game's error output, the
//! runtime's state and display-recovery records, the runtime preflight
//! diagnostic, the runtime settings and launch profile, Android's startup
//! records and the *names* of crash dumps. Nothing else is read: never
//! saves, save states, game sources, prepared titles, mods or dump files.
//! Every text and every JSON string (keys included) passes through
//! [`Redactor`]: the installation's own folders become `<package>`,
//! `<activation>` and `<data>`, then user names in any remaining path
//! become `<user>` ([`crate::redact`]).
//!
//! Locations come from the resolved inventory ([`crate::inventory`]), so
//! diagnostics read the files where the runtime and Forge really keep them.
//! Files Forge writes for diagnostics live in Forge's log folder (`D/logs`):
//! `last-game-run.json` and the game's captured error output
//! `game-stderr.log`. The runtime writes `last-recovery.json` beside its
//! own log (its working directory's `logs/`), where Android also keeps
//! `native-stderr.log`.

use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use forge_base::fsutil::{atomic_write_bytes, atomic_write_json, load_json_object};
use forge_base::{ForgeError, Operation, Result};
use serde_json::{json, Map, Value};

use crate::explain::{explain, explain_outcome, Outcome};
use crate::inventory::{inventory_of, Inventory, InventoryRequest, Platform};
use crate::state::{inspect, InspectRequest, InstallationState};

pub const REQUEST_FORMAT: &str = "triaevum_forge_diagnostics_v1";
pub const FORMAT: &str = "triaevum_diagnostics_v1";
pub const GAME_RUN_FORMAT: &str = "triaevum_game_run_v1";
pub const RECOVERY_FORMAT: &str = "triaevum_display_recovery_v1";
pub const BUNDLE_MANIFEST_FORMAT: &str = "triaevum_support_bundle_v1";

/// The copyable report's size bound, in bytes.
pub const TEXT_LIMIT: usize = 16 * 1024;
/// Lines of a log the report and the game-exit card show.
pub const TAIL_LINES: usize = 40;
/// A bundled log keeps at most its last 2 MB; a larger JSON record is left out.
pub const MEMBER_LIMIT: u64 = 2 * 1024 * 1024;
/// The bundle's total before compression.
pub const BUNDLE_LIMIT: u64 = 16 * 1024 * 1024;

/// In Forge's log folder.
pub const GAME_RUN_FILE: &str = "last-game-run.json";
/// The game's error output, captured by Forge's Play (desktop).
pub const GAME_STDERR_FILE: &str = "game-stderr.log";
/// In the runtime's log folder.
pub const RECOVERY_FILE: &str = "last-recovery.json";
/// The game's error output on Android (`ports/android/runtime/android_host.cpp`).
pub const ANDROID_STDERR_FILE: &str = "native-stderr.log";
/// The runtime's log (`ship/Context.cpp`: `logs/<short name>.log`, earlier
/// launches rotated to `.1.log`, `.2.log`, ...).
pub const GAME_LOG_STEM: &str = "oot3d_native_game";
/// Errors from before the runtime log opened (`triaevum_early_log.h`), in
/// the runtime's log folder.
pub const EARLY_ERRORS_FILE: &str = "early-errors.log";
/// The line the runtime writes to its error output before it exits with an
/// error (`oot3d_native_game.cpp`).
pub const FATAL_PREFIX: &str = "TRIAEVUM_FATAL ";
/// The line the runtime logs for the graphics device it uses (`gfx_vulkan_device.cpp`).
const DEVICE_LINE: &str = "OOT3D Vulkan device [";

// ---------------------------------------------------------------------------
// Redaction

/// Removes the installation's folders and user names from text.
#[derive(Clone, Debug)]
pub struct Redactor {
    /// (folder spelling, placeholder), longest spelling first.
    roots: Vec<(String, &'static str)>,
    user: Option<String>,
}

impl Redactor {
    /// `roots` pairs a folder with its placeholder; a folder listed twice
    /// keeps its first placeholder.
    pub fn new(roots: &[(&Path, &'static str)], user: Option<String>) -> Self {
        let mut spellings: Vec<(String, &'static str)> = Vec::new();
        for (root, placeholder) in roots {
            let native = root.to_string_lossy();
            let native = native.strip_prefix(r"\\?\").unwrap_or(&native).trim_end_matches(['/', '\\']).to_string();
            if native.chars().count() < 3 {
                continue;
            }
            // As written, and JSON-escaped inside a log line.
            for spelling in [native.clone(), native.replace('\\', r"\\")] {
                if !spellings.iter().any(|(known, _)| same_text(known, &spelling)) {
                    spellings.push((spelling, placeholder));
                }
            }
        }
        spellings.sort_by_key(|(spelling, _)| std::cmp::Reverse(spelling.len()));
        Self { roots: spellings, user }
    }

    /// Placeholders for the roots of `sources` and the current user.
    pub fn for_sources(sources: &Sources) -> Self {
        Self::new(
            &[
                (&sources.data, "<data>"),
                // Portable installations activate in the package itself.
                (&sources.package, "<package>"),
                (&sources.activation, "<activation>"),
            ],
            crate::redact::current_user(),
        )
    }

    pub fn text(&self, text: &str) -> String {
        let mut output = text.to_string();
        for (spelling, placeholder) in &self.roots {
            output = replace_folder(&output, spelling, placeholder);
        }
        crate::redact::redact_with(&output, self.user.as_deref())
    }

    /// Every string of `value`, object keys included.
    pub fn json(&self, value: &Value) -> Value {
        match value {
            Value::String(text) => Value::String(self.text(text)),
            Value::Array(items) => Value::Array(items.iter().map(|item| self.json(item)).collect()),
            Value::Object(map) => {
                Value::Object(map.iter().map(|(key, item)| (self.text(key), self.json(item))).collect())
            }
            other => other.clone(),
        }
    }
}

fn same_text(left: &str, right: &str) -> bool {
    if cfg!(windows) {
        left.eq_ignore_ascii_case(right)
    } else {
        left == right
    }
}

/// Replaces `folder` where it appears as a whole path prefix: the next
/// character must end the folder name. Either separator matches either
/// (logs mix them); ASCII case is ignored on Windows.
fn replace_folder(text: &str, folder: &str, placeholder: &str) -> String {
    let fold = |value: &str| {
        let value = value.replace('\\', "/");
        if cfg!(windows) {
            value.to_ascii_lowercase()
        } else {
            value
        }
    };
    let (haystack, needle) = (fold(text), fold(folder));
    let mut output = String::with_capacity(text.len());
    let mut start = 0;
    while let Some(found) = haystack[start..].find(&needle) {
        let at = start + found;
        let end = at + needle.len();
        let next = text[end..].chars().next();
        let whole = next.is_none_or(|character| !(character.is_alphanumeric() || "_-.".contains(character)));
        output.push_str(&text[start..at]);
        output.push_str(if whole { placeholder } else { &text[at..end] });
        start = end;
    }
    output.push_str(&text[start..]);
    output
}

// ---------------------------------------------------------------------------
// Sources

/// Where diagnostics read from, resolved like the runtime and Forge resolve it.
#[derive(Clone, Debug)]
pub struct Sources {
    pub platform: Platform,
    pub package: PathBuf,
    pub activation: PathBuf,
    pub data: PathBuf,
    /// The runtime's `logs/` (its working directory's).
    pub game_logs: PathBuf,
    /// Forge's `D/logs`.
    pub forge_logs: PathBuf,
    pub crash_dumps: Option<PathBuf>,
    /// The earlier `<save data>/crashes` once dumps go to `D/logs/crashes`.
    pub crash_dumps_legacy: Option<PathBuf>,
    pub runtime_state: Option<PathBuf>,
    pub runtime_config: Option<PathBuf>,
    pub launch_profile: Option<PathBuf>,
    pub android_diagnostics: Option<PathBuf>,
}

impl Sources {
    pub fn from_inventory(inventory: &Inventory) -> Self {
        let path = |name: &str| inventory.entry(name).map(|entry| entry.resolved_path.clone());
        let roots = &inventory.roots;
        Self {
            platform: roots.platform,
            package: roots.package.clone(),
            activation: roots.activation.clone(),
            data: roots.data.clone(),
            game_logs: path("game_logs").unwrap_or_else(|| roots.activation.join("logs")),
            forge_logs: path("forge_logs").unwrap_or_else(|| crate::log::logs_directory(&roots.data)),
            crash_dumps: path("crash_dumps"),
            crash_dumps_legacy: path("crash_dumps_legacy"),
            runtime_state: path("runtime_state"),
            runtime_config: path("runtime_config"),
            launch_profile: path("launch_profile"),
            android_diagnostics: path("android_diagnostics"),
        }
    }

    pub fn game_log(&self) -> PathBuf {
        self.game_logs.join(format!("{GAME_LOG_STEM}.log"))
    }

    /// Where Forge's Play writes the game's error output.
    pub fn game_stderr(&self) -> PathBuf {
        self.forge_logs.join(GAME_STDERR_FILE)
    }

    /// The game's error output files that exist (Forge's capture, Android's).
    pub fn error_outputs(&self) -> Vec<PathBuf> {
        [self.game_stderr(), self.game_logs.join(ANDROID_STDERR_FILE)]
            .into_iter()
            .filter(|path| path.is_file())
            .collect()
    }

    pub fn game_run_path(&self) -> PathBuf {
        self.forge_logs.join(GAME_RUN_FILE)
    }

    pub fn recovery_path(&self) -> PathBuf {
        self.game_logs.join(RECOVERY_FILE)
    }
}

// ---------------------------------------------------------------------------
// Reading logs

/// The last `count` lines of a text file (at most its last 128 KB), or None
/// when it cannot be read.
pub fn tail_lines(path: &Path, count: usize) -> Option<Vec<String>> {
    const WINDOW: u64 = 128 * 1024;
    let mut file = std::fs::File::open(path).ok()?;
    let length = file.metadata().ok()?.len();
    let start = length.saturating_sub(WINDOW);
    file.seek(SeekFrom::Start(start)).ok()?;
    let mut bytes = Vec::new();
    file.take(WINDOW).read_to_end(&mut bytes).ok()?;
    let text = String::from_utf8_lossy(&bytes);
    let mut lines: Vec<&str> = text.lines().collect();
    if start > 0 && !lines.is_empty() {
        lines.remove(0);
    }
    let first = lines.len().saturating_sub(count);
    Some(lines[first..].iter().map(|line| line.trim_end_matches('\r').to_string()).collect())
}

/// The first line containing `needle` in the first 512 KB of a text file.
fn find_line(path: &Path, needle: &str) -> Option<String> {
    let mut bytes = Vec::new();
    std::fs::File::open(path).ok()?.take(512 * 1024).read_to_end(&mut bytes).ok()?;
    String::from_utf8_lossy(&bytes).lines().find(|line| line.contains(needle)).map(str::to_string)
}

fn modified(path: &Path) -> Option<SystemTime> {
    std::fs::metadata(path).and_then(|metadata| metadata.modified()).ok()
}

/// Written at or after `since` (with two seconds of clock slack).
fn written_since(path: &Path, since: Option<SystemTime>) -> bool {
    match (since, modified(path)) {
        (None, _) => true,
        (Some(since), Some(time)) => time + Duration::from_secs(2) >= since,
        (Some(_), None) => false,
    }
}

/// The game's own reason for its last error exit, from its error output.
pub fn fatal_message(sources: &Sources, since: Option<SystemTime>) -> Option<String> {
    sources.error_outputs().iter().filter(|path| written_since(path, since)).find_map(|path| {
        let lines = tail_lines(path, 400)?;
        lines.iter().rev().find_map(|line| line.strip_prefix(FATAL_PREFIX).map(str::to_string))
    })
}

// ---------------------------------------------------------------------------
// Game runs

/// One game session started by Forge.
#[derive(Clone, Debug, PartialEq)]
pub struct GameRun {
    pub started: SystemTime,
    pub duration: Duration,
    /// [`forge_native::process::return_code`]: Windows exit codes as
    /// unsigned values, Unix signals as negative numbers.
    pub exit_code: i64,
}

impl GameRun {
    pub fn failed(&self) -> bool {
        self.exit_code != 0
    }

    pub fn to_value(&self) -> Value {
        json!({
            "format": GAME_RUN_FORMAT,
            "started": crate::log::timestamp(self.started),
            "started_unix": self.started.duration_since(UNIX_EPOCH).map(|elapsed| elapsed.as_secs()).unwrap_or(0),
            "duration_seconds": self.duration.as_secs(),
            "exit_code": self.exit_code,
            "meaning": exit_meaning(self.exit_code),
        })
    }

    pub fn from_value(value: &Map<String, Value>) -> Option<Self> {
        if value.get("format")?.as_str()? != GAME_RUN_FORMAT {
            return None;
        }
        Some(Self {
            started: UNIX_EPOCH + Duration::from_secs(value.get("started_unix")?.as_u64()?),
            duration: Duration::from_secs(value.get("duration_seconds")?.as_u64()?),
            exit_code: value.get("exit_code")?.as_i64()?,
        })
    }
}

/// What an exit code says, in words.
pub fn exit_meaning(code: i64) -> String {
    let crash = |name: &str| format!("the game crashed ({name})");
    match code {
        0 => "normal exit".to_string(),
        1 => "the game stopped with an error".to_string(),
        2 => "the game was started with invalid arguments".to_string(),
        0xC000_0005 => crash("access violation"),
        0xC000_001D => crash("illegal instruction"),
        0xC000_00FD => crash("stack overflow"),
        0xC000_0374 => crash("heap corruption"),
        0xC000_0409 => crash("fail-fast exit, often an uncaught error"),
        0xC000_0135 | 0xC000_007B => "Windows could not load a library the game needs".to_string(),
        0x4000_0015 | 3 => "the game aborted".to_string(),
        -6 => crash("SIGABRT"),
        -7 => crash("SIGBUS"),
        -9 => "the game was killed (SIGKILL)".to_string(),
        -11 => crash("SIGSEGV"),
        -4 => crash("SIGILL"),
        -8 => crash("SIGFPE"),
        -15 => "the game was asked to stop (SIGTERM)".to_string(),
        code if code > 0xFFFF => format!("the game ended with status 0x{code:08X}"),
        code => format!("the game ended with code {code}"),
    }
}

fn code_text(code: i64) -> String {
    if code > 0xFFFF {
        format!("0x{code:08X}")
    } else {
        code.to_string()
    }
}

/// Records the last game run in Forge's log folder; failures are ignored
/// (diagnostics never block playing).
pub fn record_game_run(sources: &Sources, run: &GameRun) {
    let _ = atomic_write_json(&sources.game_run_path(), &run.to_value());
}

pub fn last_game_run(sources: &Sources) -> Option<GameRun> {
    GameRun::from_value(&load_json_object(&sources.game_run_path()).ok()?)
}

/// The lines a log wrote during `run` (its last [`TAIL_LINES`]), redacted.
fn run_tail(path: &Path, since: Option<SystemTime>, redactor: &Redactor) -> Option<Vec<String>> {
    if !written_since(path, since) {
        return None;
    }
    let lines = tail_lines(path, TAIL_LINES)?;
    (!lines.is_empty()).then(|| lines.iter().map(|line| redactor.text(line)).collect())
}

/// The details of a game run: exit code, start, duration, the game's own
/// error and the end of its log and error output written during the run.
pub fn game_run_details(sources: &Sources, run: &GameRun, redactor: &Redactor) -> String {
    let mut lines = vec![
        format!("Exit code: {} ({})", code_text(run.exit_code), exit_meaning(run.exit_code)),
        format!("Started: {}, ran for {} s", crate::log::timestamp(run.started), run.duration.as_secs()),
    ];
    let since = Some(run.started);
    if let Some(message) = fatal_message(sources, since) {
        lines.push(format!("Error reported by the game: {}", redactor.text(&message)));
    }
    if let Some(tail) = run_tail(&sources.game_log(), since, redactor) {
        lines.push(String::new());
        lines.push(format!("Last lines of {GAME_LOG_STEM}.log:"));
        lines.extend(tail);
    }
    for path in sources.error_outputs() {
        if let Some(tail) = run_tail(&path, since, redactor) {
            let name = path.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
            lines.push(String::new());
            lines.push(format!("Last lines of {name}:"));
            lines.extend(tail);
        }
    }
    lines.join("\n")
}

/// The "The game closed with an error" notice for a failed run, None for a
/// clean exit: `{"outcome": "game_exited_with_error", "details"}`.
pub fn game_exit_notice(sources: &Sources, run: &GameRun) -> Option<Value> {
    if !run.failed() {
        return None;
    }
    let details = game_run_details(sources, run, &Redactor::for_sources(sources));
    Some(json!({"outcome": Outcome::GameExitedWithError.code(), "details": details}))
}

// ---------------------------------------------------------------------------
// Display recovery

/// The runtime's record of a display reset after an early exit
/// (`graphics_settings_runtime.cpp`, `TRIAEVUM_SAFE_PRESENTATION`).
#[derive(Clone, Debug, PartialEq)]
pub struct Recovery {
    pub time: String,
    pub reason: String,
    pub fields: Vec<String>,
}

pub fn last_recovery(sources: &Sources) -> Option<Recovery> {
    let record = load_json_object(&sources.recovery_path()).ok()?;
    if record.get("format")?.as_str()? != RECOVERY_FORMAT {
        return None;
    }
    let text = |key: &str| record.get(key).and_then(Value::as_str).unwrap_or_default().to_string();
    let fields = record
        .get("fields")
        .and_then(Value::as_array)
        .map(|fields| fields.iter().filter_map(Value::as_str).map(str::to_string).collect())
        .unwrap_or_default();
    Some(Recovery { time: text("time"), reason: text("reason"), fields })
}

/// The "Display settings were reset after an early exit" notice.
pub fn recovery_notice(recovery: &Recovery) -> Value {
    let fields = if recovery.fields.is_empty() { "none".to_string() } else { recovery.fields.join(", ") };
    let details = format!("When: {}\nWhy: {}\nReset: {fields}", recovery.time, recovery.reason);
    json!({"outcome": Outcome::DisplaySettingsReset.code(), "details": details})
}

// ---------------------------------------------------------------------------
// The report

/// What the report states besides the files it reads.
pub struct ReportFacts<'a> {
    pub state: Option<&'a InstallationState>,
    pub game_running: bool,
    /// When the report was made.
    pub now: SystemTime,
}

/// The dumps of both crash folders (the earlier one keeps older dumps).
fn crash_dumps(sources: &Sources) -> Vec<(String, u64, Option<SystemTime>)> {
    let mut dumps: Vec<(String, u64, Option<SystemTime>)> = [&sources.crash_dumps, &sources.crash_dumps_legacy]
        .into_iter()
        .flatten()
        .filter_map(|directory| std::fs::read_dir(directory).ok())
        .flat_map(|entries| entries.flatten())
        .filter_map(|entry| {
            let name = entry.file_name().to_string_lossy().into_owned();
            let metadata = entry.metadata().ok()?;
            (metadata.is_file() && name.to_ascii_lowercase().ends_with(".dmp"))
                .then(|| (name, metadata.len(), metadata.modified().ok()))
        })
        .collect();
    dumps.sort_by(|left, right| right.2.cmp(&left.2).then_with(|| left.0.cmp(&right.0)));
    dumps
}

/// The lines of the last job in Forge's log (its trailing lines with the
/// same `[job]` tag), at most 15.
fn last_job_lines(sources: &Sources) -> Vec<String> {
    let Some(lines) = tail_lines(&sources.forge_logs.join(crate::log::LOG_FILE), 200) else { return Vec::new() };
    let tag = |line: &str| line.split_whitespace().nth(1).map(str::to_string);
    let Some(last) = lines.last().and_then(|line| tag(line)) else { return Vec::new() };
    let mut job: Vec<String> =
        lines.iter().rev().take_while(|line| tag(line).as_deref() == Some(last.as_str())).cloned().collect();
    job.truncate(15);
    job.reverse();
    job
}

fn preflight_summary(sources: &Sources) -> Option<String> {
    let record = load_json_object(&crate::log::preflight_error_path(&sources.data)).ok()?;
    let mut parts = Vec::new();
    for key in ["generated_at", "error", "message", "reason", "exit_code", "returncode"] {
        match record.get(key) {
            Some(Value::String(text)) => parts.push(format!("{key}: {text}")),
            Some(Value::Number(number)) => parts.push(format!("{key}: {number}")),
            _ => {}
        }
    }
    Some(if parts.is_empty() { "present (see the support bundle)".to_string() } else { parts.join("; ") })
}

/// Appends `lines` under `title` while the report stays below the limit,
/// dropping the oldest lines first.
fn push_tail(report: &mut String, title: &str, lines: &[String], reserve: usize) {
    if lines.is_empty() {
        return;
    }
    let header = format!("\n{title}\n");
    let budget = TEXT_LIMIT.saturating_sub(reserve);
    let mut first = 0;
    let size = |from: usize| header.len() + lines[from..].iter().map(|line| line.len() + 3).sum::<usize>();
    while first < lines.len() && report.len() + size(first) > budget {
        first += 1;
    }
    if first == lines.len() {
        return;
    }
    report.push_str(&header);
    for line in &lines[first..] {
        report.push_str("  ");
        report.push_str(line);
        report.push('\n');
    }
}

/// The copyable diagnostics text, at most [`TEXT_LIMIT`] bytes, redacted.
pub fn report_text(sources: &Sources, facts: &ReportFacts) -> String {
    let redactor = Redactor::for_sources(sources);
    let mut report = String::new();
    let mut line = |text: String| {
        report.push_str(&text);
        report.push('\n');
    };
    line(format!("TriAevum diagnostics, {}", crate::log::timestamp(facts.now)));
    line(format!(
        "Forge {} | package {} | {}",
        env!("CARGO_PKG_VERSION"),
        crate::state::package_version(&sources.package).unwrap_or_else(|| "unknown".to_string()),
        sources.platform.code()
    ));
    line(format!("System: {} ({})", forge_native::system::os_identity(), std::env::consts::ARCH));
    match facts.state {
        Some(state) => {
            let value = state.to_value();
            let mut text = format!("Installation: {}", state.code());
            for key in ["recipe", "package_version", "language"] {
                if let Some(field) = value.get(key).and_then(Value::as_str) {
                    text.push_str(&format!(", {key} {field}"));
                }
            }
            if let Some(title) = value.pointer("/explanation/title").and_then(Value::as_str) {
                text.push_str(&format!(" ({title})"));
            }
            line(text);
        }
        None => line("Installation: not inspected".to_string()),
    }
    line(format!("Game running now: {}", if facts.game_running { "yes" } else { "no" }));
    line(format!(
        "Folders: package {}, data {}, game logs {}",
        redactor.text(&sources.package.to_string_lossy()),
        redactor.text(&sources.data.to_string_lossy()),
        redactor.text(&sources.game_logs.to_string_lossy())
    ));
    let device = find_line(&sources.game_log(), DEVICE_LINE)
        .map(|found| redactor.text(found.split(DEVICE_LINE).nth(1).map_or(&found, |rest| rest)));
    line(format!(
        "Graphics device (game log): {}",
        device.map_or("not recorded".to_string(), |rest| format!("[{rest}"))
    ));
    match last_game_run(sources) {
        Some(run) => line(format!(
            "Last game run: {}, {} s, exit code {} ({})",
            crate::log::timestamp(run.started),
            run.duration.as_secs(),
            code_text(run.exit_code),
            exit_meaning(run.exit_code)
        )),
        None => line("Last game run: none recorded by this Forge".to_string()),
    }
    if let Some(message) = fatal_message(sources, None) {
        line(format!("Last error reported by the game: {}", redactor.text(&message)));
    }
    if let Some(recovery) = last_recovery(sources) {
        line(format!(
            "Display recovery: {} reset {} ({})",
            recovery.time,
            recovery.fields.join(", "),
            redactor.text(&recovery.reason)
        ));
    }
    if let Some(summary) = preflight_summary(sources) {
        line(format!("Runtime preflight error: {}", redactor.text(&summary)));
    }
    let dumps = crash_dumps(sources);
    if !dumps.is_empty() {
        let newest: Vec<String> = dumps.iter().take(3).map(|(name, _, _)| name.clone()).collect();
        line(format!("Crash dumps: {} (newest: {})", dumps.len(), newest.join(", ")));
    }
    let job: Vec<String> = last_job_lines(sources).iter().map(|text| redactor.text(text)).collect();
    // Room is kept for the game's tails after the job lines.
    push_tail(&mut report, "Last Forge job (forge.log):", &job, TEXT_LIMIT / 2);
    let tail = |path: &Path| -> Vec<String> {
        tail_lines(path, TAIL_LINES).unwrap_or_default().iter().map(|text| redactor.text(text)).collect()
    };
    let outputs = sources.error_outputs();
    let reserve = if outputs.is_empty() { 0 } else { TEXT_LIMIT / 4 };
    push_tail(
        &mut report,
        &format!("Game log ({GAME_LOG_STEM}.log, last lines):"),
        &tail(&sources.game_log()),
        reserve,
    );
    for path in outputs {
        let name = path.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
        push_tail(&mut report, &format!("Game error output ({name}, last lines):"), &tail(&path), 0);
    }
    truncate_text(report, TEXT_LIMIT)
}

/// Cuts `text` to at most `limit` bytes on a character boundary.
pub fn truncate_text(mut text: String, limit: usize) -> String {
    if text.len() <= limit {
        return text;
    }
    const MARK: &str = "\n[truncated]\n";
    let mut end = limit - MARK.len();
    while !text.is_char_boundary(end) {
        end -= 1;
    }
    text.truncate(end);
    text.push_str(MARK);
    text
}

// ---------------------------------------------------------------------------
// The support bundle

#[derive(Clone, Debug, PartialEq)]
pub enum MemberSource {
    /// A log: redacted line by line, its last [`MEMBER_LIMIT`] bytes kept.
    Text(PathBuf),
    /// A JSON record: every string redacted; left out above [`MEMBER_LIMIT`].
    Json(PathBuf),
    /// Text Forge makes (the report, the crash-dump names, the manifest).
    Generated(String),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Member {
    /// The name inside the zip.
    pub name: String,
    pub source: MemberSource,
    /// The bytes it holds before redaction (bounded).
    pub size: u64,
}

/// What a bundle will hold, for the preview before saving.
#[derive(Clone, Debug)]
pub struct BundlePlan {
    pub members: Vec<Member>,
    /// (name, why) of allowed files that were left out.
    pub omitted: Vec<(String, String)>,
    redactor: Redactor,
}

impl BundlePlan {
    /// `[{"name", "size"}]` plus `omitted`, for frontends.
    pub fn to_value(&self) -> Value {
        json!({
            "members": self.members.iter().map(|member| json!({"name": member.name, "size": member.size})).collect::<Vec<_>>(),
            "omitted": self.omitted.iter().map(|(name, why)| json!({"name": name, "reason": why})).collect::<Vec<_>>(),
        })
    }

    pub fn total_size(&self) -> u64 {
        self.members.iter().map(|member| member.size).sum()
    }
}

/// The files a bundle may read, in priority order: (zip name, path, JSON?).
fn allowlist(sources: &Sources) -> Vec<(String, PathBuf, bool)> {
    let mut files = Vec::new();
    let forge = |name: &str| (format!("forge-logs/{name}"), sources.forge_logs.join(name));
    let game = |name: &str| (format!("game-logs/{name}"), sources.game_logs.join(name));
    let mut push = |(name, path): (String, PathBuf), json: bool| files.push((name, path, json));
    push(forge(GAME_RUN_FILE), true);
    push(forge(crate::log::PREFLIGHT_ERROR_FILE), true);
    push(game(RECOVERY_FILE), true);
    push(forge(GAME_STDERR_FILE), false);
    push(game(ANDROID_STDERR_FILE), false);
    push(game(&format!("{GAME_LOG_STEM}.log")), false);
    push(game(EARLY_ERRORS_FILE), false);
    push(forge(crate::log::LOG_FILE), false);
    if let Some(path) = &sources.runtime_config {
        let name = path.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
        push((format!("settings/{name}"), path.clone()), true);
    }
    if let Some(path) = &sources.launch_profile {
        let name = path.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
        push((format!("state/{name}"), path.clone()), true);
    }
    if let Some(path) = &sources.runtime_state {
        push(("state/runtime-state.json".to_string(), path.clone()), true);
    }
    if let Some(directory) = &sources.android_diagnostics {
        for name in ["game-stages.txt", "java-failure-game.txt", "java-failure-forge.txt"] {
            push((format!("android/{name}"), directory.join(name)), false);
        }
    }
    for index in 1..=2 {
        push(game(&format!("{GAME_LOG_STEM}.{index}.log")), false);
    }
    for index in 1..crate::log::KEPT_FILES {
        push(forge(&format!("{}.{index}", crate::log::LOG_FILE)), false);
    }
    files
}

/// Plans a bundle of the allowed files that exist, the crash-dump names and
/// `report` (normally [`report_text`]).
pub fn plan_bundle(sources: &Sources, report: &str) -> BundlePlan {
    let redactor = Redactor::for_sources(sources);
    let mut members = vec![Member {
        name: "diagnostics.txt".to_string(),
        size: report.len() as u64,
        source: MemberSource::Generated(report.to_string()),
    }];
    let dumps = crash_dumps(sources);
    if !dumps.is_empty() {
        let mut text = String::from("Crash dumps (names only; the dump files are not included):\n");
        for (name, size, time) in &dumps {
            let when = time.map(crate::log::timestamp).unwrap_or_default();
            text.push_str(&format!("{name}\t{size} bytes\t{when}\n"));
        }
        members.push(Member {
            name: "crash-dumps.txt".to_string(),
            size: text.len() as u64,
            source: MemberSource::Generated(text),
        });
    }
    let mut omitted = Vec::new();
    let mut total: u64 = members.iter().map(|member| member.size).sum();
    for (name, path, is_json) in allowlist(sources) {
        let Ok(metadata) = std::fs::metadata(&path) else { continue };
        if !metadata.is_file() {
            continue;
        }
        let size = metadata.len();
        if is_json && size > MEMBER_LIMIT {
            omitted.push((name, format!("larger than {} MB", MEMBER_LIMIT >> 20)));
            continue;
        }
        let size = size.min(MEMBER_LIMIT);
        if total + size > BUNDLE_LIMIT {
            omitted.push((name, format!("the bundle reached {} MB", BUNDLE_LIMIT >> 20)));
            continue;
        }
        total += size;
        let source = if is_json { MemberSource::Json(path) } else { MemberSource::Text(path) };
        members.push(Member { name, source, size });
    }
    BundlePlan { members, omitted, redactor }
}

/// The last `limit` bytes of a file from a line start, with a note when
/// earlier bytes were left out.
fn read_tail(path: &Path, limit: u64) -> std::io::Result<String> {
    let mut file = std::fs::File::open(path)?;
    let length = file.metadata()?.len();
    let start = length.saturating_sub(limit);
    file.seek(SeekFrom::Start(start))?;
    let mut bytes = Vec::new();
    file.take(limit).read_to_end(&mut bytes)?;
    let text = String::from_utf8_lossy(&bytes).into_owned();
    if start == 0 {
        return Ok(text);
    }
    let body = text.split_once('\n').map_or("", |(_, rest)| rest);
    Ok(format!("[{start} earlier bytes left out]\n{body}"))
}

fn member_bytes(member: &Member, redactor: &Redactor) -> std::result::Result<Vec<u8>, String> {
    match &member.source {
        MemberSource::Generated(text) => Ok(text.as_bytes().to_vec()),
        MemberSource::Text(path) => {
            let text = read_tail(path, MEMBER_LIMIT).map_err(|error| error.to_string())?;
            let mut redacted: String = text.lines().map(|line| redactor.text(line) + "\n").collect();
            if redacted.is_empty() {
                redacted.push('\n');
            }
            Ok(redacted.into_bytes())
        }
        MemberSource::Json(path) => {
            let bytes = std::fs::read(path).map_err(|error| error.to_string())?;
            match serde_json::from_slice::<Value>(&bytes) {
                Ok(value) => Ok(crate::legacy_json::pretty_file_bytes(&redactor.json(&value))),
                // Not JSON (truncated by a crash): redacted as text.
                Err(_) => Ok(redactor.text(&String::from_utf8_lossy(&bytes)).into_bytes()),
            }
        }
    }
}

/// What a saved bundle holds.
#[derive(Clone, Debug, PartialEq)]
pub struct BundleSummary {
    pub path: PathBuf,
    pub bytes: u64,
    pub members: Vec<String>,
}

/// Writes the planned bundle to `destination` atomically (the zip is built
/// in memory, written to a temporary file beside it and renamed over it).
/// A member that cannot be read is listed in `manifest.json` instead.
pub fn write_bundle(plan: &BundlePlan, destination: &Path) -> Result<BundleSummary> {
    let options = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
    let mut writer = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let zip_error =
        |error: zip::result::ZipError| ForgeError::internal(format!("Cannot build the support bundle: {error}"));
    let io_error = |error: std::io::Error| ForgeError::internal(format!("Cannot build the support bundle: {error}"));
    let mut written = Vec::new();
    let mut unreadable = Vec::new();
    for member in &plan.members {
        match member_bytes(member, &plan.redactor) {
            Ok(bytes) => {
                writer.start_file(member.name.as_str(), options).map_err(zip_error)?;
                writer.write_all(&bytes).map_err(io_error)?;
                written.push(member.name.clone());
            }
            Err(why) => unreadable.push(json!({"name": member.name, "reason": plan.redactor.text(&why)})),
        }
    }
    let manifest = json!({
        "format": BUNDLE_MANIFEST_FORMAT,
        "members": written,
        "omitted": plan.omitted.iter().map(|(name, why)| json!({"name": name, "reason": why})).collect::<Vec<_>>(),
        "unreadable": unreadable,
        "redaction": "Installation folders are <package>, <activation> and <data>; user names in paths are <user>.",
    });
    writer.start_file("manifest.json", options).map_err(zip_error)?;
    writer.write_all(&crate::legacy_json::pretty_file_bytes(&manifest)).map_err(io_error)?;
    written.push("manifest.json".to_string());
    let bytes = writer.finish().map_err(zip_error)?.into_inner();
    atomic_write_bytes(destination, &bytes).map_err(|error| error.during(Operation::Backup))?;
    Ok(BundleSummary { path: destination.to_path_buf(), bytes: bytes.len() as u64, members: written })
}

/// A file name for a bundle made now: `TriAevum-diagnostics-YYYYMMDD-HHMMSS.zip`.
pub fn bundle_file_name(now: SystemTime) -> String {
    let stamp = crate::log::timestamp(now).replace(['-', ':'], "").replace('T', "-").replace('Z', "");
    format!("TriAevum-diagnostics-{stamp}.zip")
}

// ---------------------------------------------------------------------------
// The query (ABI, JNI, CLI)

/// Everything a frontend shows on its diagnostics page.
pub struct Diagnostics {
    pub sources: Sources,
    pub text: String,
    pub plan: BundlePlan,
    pub last_run: Option<GameRun>,
    pub recovery: Option<Recovery>,
}

/// Diagnostics of the installation `request` names (an inventory request).
pub fn collect(request: &InventoryRequest) -> Result<Diagnostics> {
    let inventory = inventory_of(&InventoryRequest {
        package_root: request.package_root.clone(),
        data_root: request.data_root.clone(),
        android: request.android.as_ref().map(|android| crate::install::AndroidTarget {
            native_library_dir: android.native_library_dir.clone(),
            profile_root: android.profile_root.clone(),
        }),
        android_app: request.android_app.clone(),
        sizes: false,
    })?;
    let sources = Sources::from_inventory(&inventory);
    let state = inspect(&InspectRequest {
        package_root: request.package_root.clone(),
        data_root: request.data_root.clone(),
        target: match &request.android {
            None => crate::install::InstallTarget::Package,
            Some(android) => crate::install::InstallTarget::Android(crate::install::AndroidTarget {
                native_library_dir: android.native_library_dir.clone(),
                profile_root: android.profile_root.clone(),
            }),
        },
        verify_sources: false,
    });
    let facts =
        ReportFacts { state: Some(&state), game_running: inventory.activity.game_running, now: SystemTime::now() };
    let text = report_text(&sources, &facts);
    let plan = plan_bundle(&sources, &text);
    Ok(Diagnostics { last_run: last_game_run(&sources), recovery: last_recovery(&sources), sources, text, plan })
}

impl Diagnostics {
    pub fn to_value(&self) -> Value {
        let redactor = Redactor::for_sources(&self.sources);
        let notice = |value: Option<Value>| value.and_then(|value| crate::explain::explain_value(&value));
        json!({
            "format": FORMAT,
            "status": "ok",
            "text": self.text,
            "bundle_plan": self.plan.to_value(),
            "last_game_run": self.last_run.as_ref().map(GameRun::to_value),
            "game_exit": notice(self.last_run.as_ref().and_then(|run| game_exit_notice(&self.sources, run)))
                .map(|explanation| explanation.to_value()),
            "recovery": notice(self.recovery.as_ref().map(recovery_notice)).map(|explanation| explanation.to_value()),
            "logs": {
                "game": redactor.text(&self.sources.game_logs.to_string_lossy()),
                "forge": redactor.text(&self.sources.forge_logs.to_string_lossy()),
            },
        })
    }
}

/// [`collect`] for a JSON request (ABI and JNI): `{"format":
/// "triaevum_forge_diagnostics_v1", "package_root"?, "data_root"?,
/// "android"?, "bundle"?: path}`. With `bundle`, the support bundle is
/// written there and `bundle` in the result names it. The result is
/// `{"format", "status": "ok", "text", "bundle_plan", "last_game_run",
/// "game_exit", "recovery", "logs", "bundle"?}` or `status: "failed"` with
/// its `explanation`.
pub fn diagnostics_bytes(request: &[u8]) -> Vec<u8> {
    let parsed = crate::legacy_json::load_object(request)
        .map_err(|error| ForgeError::invalid(format!("Invalid diagnostics request: {error}")))
        .and_then(|request| {
            if request.get("format").and_then(Value::as_str) != Some(REQUEST_FORMAT) {
                return Err(ForgeError::invalid(format!("Diagnostics request format must be {REQUEST_FORMAT}")));
            }
            let mut inventory_request = request.clone();
            inventory_request.insert("format".into(), json!(crate::inventory::REQUEST_FORMAT));
            inventory_request.remove("bundle");
            let bundle = match request.get("bundle") {
                None | Some(Value::Null) => None,
                Some(Value::String(path)) if !path.is_empty() => Some(PathBuf::from(path)),
                Some(_) => return Err(ForgeError::invalid("bundle must be a path")),
            };
            Ok((InventoryRequest::parse(&inventory_request)?, bundle))
        })
        .map_err(|error| error.during(Operation::RequestValidation));
    let outcome = parsed.and_then(|(request, bundle)| {
        let diagnostics = collect(&request).map_err(|error| error.during(Operation::Inspect))?;
        let mut value = diagnostics.to_value();
        if let Some(path) = bundle {
            let summary = write_bundle(&diagnostics.plan, &path)?;
            value["bundle"] = json!({
                "path": path.to_string_lossy(),
                "bytes": summary.bytes,
                "members": summary.members,
            });
        }
        Ok(value)
    });
    let value = outcome.unwrap_or_else(
        |error| json!({"format": FORMAT, "status": "failed", "explanation": explain(&error).to_value()}),
    );
    crate::legacy_json::compact_line_bytes(&value)
}

/// The explanation of a game run, for frontends that hold the run itself.
pub fn explain_game_run(sources: &Sources, run: &GameRun) -> Option<crate::explain::Explanation> {
    let notice = game_exit_notice(sources, run)?;
    Some(explain_outcome(Outcome::GameExitedWithError, notice["details"].as_str().unwrap_or_default()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn folders_are_replaced_only_as_whole_names() {
        let redactor = Redactor::new(
            &[(Path::new("/opt/Tri Aevum/data"), "<data>"), (Path::new("/opt/Tri Aevum"), "<package>")],
            Some("jdoe".to_string()),
        );
        assert_eq!(redactor.text("open /opt/Tri Aevum/data/logs/x.log"), "open <data>/logs/x.log");
        assert_eq!(redactor.text("run /opt/Tri Aevum/TriAevum"), "run <package>/TriAevum");
        assert_eq!(redactor.text("/opt/Tri Aevum2/x"), "/opt/Tri Aevum2/x");
        assert_eq!(redactor.text("rom /home/jdoe/Downloads/z.3ds"), "rom /home/<user>/Downloads/z.3ds");
    }

    #[cfg(windows)]
    #[test]
    fn windows_spellings_and_case_are_redacted() {
        let redactor =
            Redactor::new(&[(Path::new(r"\\?\D:\Games\Jane Smith Stuff\TriAevum"), "<package>")], Some("jane".into()));
        assert_eq!(redactor.text(r"d:\games\jane smith stuff\triaevum\x"), r"<package>\x");
        assert_eq!(redactor.text(r#"{"p":"D:\\Games\\Jane Smith Stuff\\TriAevum\\a"}"#), r#"{"p":"<package>\\a"}"#);
        assert_eq!(redactor.text("D:/Games/Jane Smith Stuff/TriAevum/a"), "<package>/a");
        assert_eq!(redactor.text(r"C:\Users\Jane\x"), r"C:\Users\<user>\x");
    }

    #[test]
    fn json_keys_and_values_are_redacted() {
        let redactor = Redactor::new(&[(Path::new("/srv/tri"), "<package>")], None);
        let value = json!({"/srv/tri/a": ["/srv/tri/b", 3, {"k": "/home/someone/c"}]});
        assert_eq!(redactor.json(&value), json!({"<package>/a": ["<package>/b", 3, {"k": "/home/<user>/c"}]}));
    }

    #[test]
    fn exit_codes_are_described() {
        assert_eq!(exit_meaning(0), "normal exit");
        assert!(exit_meaning(0xC000_0005).contains("access violation"));
        assert!(exit_meaning(-11).contains("SIGSEGV"));
        assert_eq!(code_text(0xC000_0409), "0xC0000409");
        assert_eq!(
            bundle_file_name(UNIX_EPOCH + Duration::from_secs(1_793_016_245)),
            "TriAevum-diagnostics-20261026-120405.zip"
        );
    }

    #[test]
    fn report_text_never_exceeds_its_limit() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path();
        let sources = Sources {
            platform: Platform::LinuxPortable,
            package: root.to_path_buf(),
            activation: root.to_path_buf(),
            data: root.join("data"),
            game_logs: root.join("logs"),
            forge_logs: root.join("data/logs"),
            crash_dumps: None,
            crash_dumps_legacy: None,
            runtime_state: None,
            runtime_config: None,
            launch_profile: None,
            android_diagnostics: None,
        };
        std::fs::create_dir_all(&sources.game_logs).unwrap();
        std::fs::create_dir_all(&sources.forge_logs).unwrap();
        let long = "é".repeat(700);
        let lines: String = (0..200).map(|index| format!("line {index} {long}\n")).collect();
        std::fs::write(sources.game_log(), &lines).unwrap();
        std::fs::write(sources.game_stderr(), format!("{lines}{FATAL_PREFIX}could not create the device\n")).unwrap();
        let text = report_text(&sources, &ReportFacts { state: None, game_running: false, now: SystemTime::now() });
        assert!(text.len() <= TEXT_LIMIT, "{}", text.len());
        assert!(text.contains("Last error reported by the game: could not create the device"));
        assert!(text.contains("Game log (oot3d_native_game.log, last lines):"));
    }
}
