// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Test fixture, not a product binary: a scriptable stand-in for the runtime
//! and the renderer helpers. Tests copy it under any name and put a plan in
//! `<copy>.plan.json`; both the Rust tests and the Python oracle run the
//! same copy. Every run records its arguments, working directory and
//! `LD_LIBRARY_PATH` in `<copy>.args.json` (appending one line per run).
//!
//! Plan fields (all optional): `stdout`, `stderr` (text), `progress` (lines
//! printed and flushed first), `write` (`[{flag, join?, content}]`: writes
//! `content` to the path after `flag`, or to `join` inside it; `{flag:--x}`
//! in content is the JSON-escaped value after `--x`), `wait_for` (a flag
//! whose path must appear before continuing), `sleep_ms`, `exit`,
//! `worker_owner` (bind the worker lifetime, watch that PID, print
//! `watching`), `spawn_sleeper` (start a 60 s child that inherits stdout,
//! print `child-started`).

use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use serde_json::{json, Value};

fn after<'a>(arguments: &'a [String], flag: &str) -> Option<&'a str> {
    let index = arguments.iter().position(|argument| argument == flag)?;
    arguments.get(index + 1).map(String::as_str)
}

fn json_escaped(text: &str) -> String {
    let quoted = serde_json::to_string(text).expect("string");
    quoted[1..quoted.len() - 1].to_string()
}

fn substitute(content: &str, arguments: &[String]) -> String {
    let mut text = content.to_string();
    while let Some(start) = text.find("{flag:") {
        let end = start + text[start..].find('}').expect("closing brace");
        let flag = &text[start + 6..end];
        let value = json_escaped(after(arguments, flag).unwrap_or_default());
        text.replace_range(start..=end, &value);
    }
    text
}

fn record(executable: &Path, arguments: &[String]) {
    let entry = json!({
        "args": arguments,
        "cwd": std::env::current_dir().map(|path| path.to_string_lossy().into_owned()).unwrap_or_default(),
        "ld_library_path": std::env::var("LD_LIBRARY_PATH").ok(),
    });
    let mut path = executable.as_os_str().to_os_string();
    path.push(".args.json");
    let mut file = std::fs::OpenOptions::new().create(true).append(true).open(PathBuf::from(path)).expect("args log");
    writeln!(file, "{entry}").expect("args log");
}

fn main() {
    if std::env::var_os("FAKE_NATIVE_HELPER_SLEEPER").is_some() {
        std::thread::sleep(Duration::from_secs(60));
        return;
    }
    let executable = std::env::current_exe().expect("current exe");
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    record(&executable, &arguments);
    let mut plan_path = executable.as_os_str().to_os_string();
    plan_path.push(".plan.json");
    let plan: Value = std::fs::read(PathBuf::from(plan_path))
        .ok()
        .and_then(|bytes| serde_json::from_slice(&bytes).ok())
        .unwrap_or_else(|| json!({}));

    if let Some(owner) = plan.get("worker_owner").and_then(Value::as_i64) {
        forge_native::bind_worker_lifetime().expect("job");
        forge_native::watch_owner(owner).expect("owner");
        println!("watching");
        std::io::stdout().flush().ok();
        std::thread::sleep(Duration::from_secs(60));
        return;
    }
    if plan.get("spawn_sleeper").and_then(Value::as_bool) == Some(true) {
        forge_native::bind_worker_lifetime().expect("job");
        // Never reaped on purpose: the Job Object must kill it with this process.
        #[allow(clippy::zombie_processes)]
        std::process::Command::new(&executable).env("FAKE_NATIVE_HELPER_SLEEPER", "1").spawn().expect("sleeper");
        println!("child-started");
        std::io::stdout().flush().ok();
        std::thread::sleep(Duration::from_secs(60));
        return;
    }
    let mut stdout = std::io::stdout();
    for line in plan.get("progress").and_then(Value::as_array).into_iter().flatten() {
        writeln!(stdout, "{}", line.as_str().unwrap_or_default()).ok();
        stdout.flush().ok();
    }
    for item in plan.get("write").and_then(Value::as_array).into_iter().flatten() {
        let flag = item["flag"].as_str().expect("flag");
        let Some(target) = after(&arguments, flag) else { continue };
        let mut path = PathBuf::from(target);
        if let Some(join) = item.get("join").and_then(Value::as_str) {
            path.push(join);
        }
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).ok();
        }
        let content = substitute(item["content"].as_str().unwrap_or_default(), &arguments);
        std::fs::write(&path, content).expect("write");
    }
    if let Some(flag) = plan.get("wait_for").and_then(Value::as_str) {
        if let Some(target) = after(&arguments, flag) {
            let deadline = Instant::now() + Duration::from_secs(30);
            while !Path::new(target).exists() && Instant::now() < deadline {
                std::thread::sleep(Duration::from_millis(10));
            }
        }
    }
    if let Some(text) = plan.get("stdout").and_then(Value::as_str) {
        stdout.write_all(text.as_bytes()).ok();
    }
    stdout.flush().ok();
    if let Some(text) = plan.get("stderr").and_then(Value::as_str) {
        std::io::stderr().write_all(text.as_bytes()).ok();
    }
    if let Some(milliseconds) = plan.get("sleep_ms").and_then(Value::as_u64) {
        std::thread::sleep(Duration::from_millis(milliseconds));
    }
    let code = plan.get("exit").and_then(Value::as_i64).unwrap_or(0);
    std::process::exit(code as i32);
}
