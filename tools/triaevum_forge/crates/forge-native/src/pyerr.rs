// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Python exception texts the Python Forge put into messages and failure
//! receipts (`str(exc)` of OSError, TimeoutExpired and JSONDecodeError), and
//! text decoding as `subprocess` text mode did it. Receipts store these
//! strings, so the common cases are spelled exactly; OS-provided wording is
//! the system's own (FormatMessage/strerror), as it was for Python.

use std::ffi::OsString;
use std::io;
use std::path::Path;

use forge_base::legacy_json::python_float_repr;
use forge_base::{ForgeError, Result};
use forge_catalog::pyjson::repr_str;
use serde_json::{Map, Value};

/// The OS description without Rust's ` (os error N)` suffix, trimmed the way
/// CPython trims FormatMessage output (trailing whitespace and dots).
fn os_text(error: &io::Error) -> String {
    let text = error.to_string();
    let text = match text.rfind(" (os error ") {
        Some(index) => text[..index].to_string(),
        None => text,
    };
    if cfg!(windows) {
        text.trim_end_matches(|c: char| c.is_whitespace() || c == '.').to_string()
    } else {
        text
    }
}

/// `str(OSError)` raised by an `os`-level call (stat, realpath, CreateProcess).
/// Windows reports `[WinError N]`, POSIX `[Errno N]`.
pub fn os_error(error: &io::Error, filename: Option<&Path>) -> String {
    let Some(code) = error.raw_os_error() else { return error.to_string() };
    let label = if cfg!(windows) { "WinError" } else { "Errno" };
    let mut text = format!("[{label} {code}] {}", os_text(error));
    if let Some(path) = filename {
        text.push_str(": ");
        text.push_str(&repr_str(&path.to_string_lossy()));
    }
    text
}

/// `str(OSError)` raised by Python's `open()`: the C runtime's errno, also
/// on Windows.
pub fn open_error(error: &io::Error, filename: &Path) -> String {
    if cfg!(windows) {
        let errno = match error.raw_os_error() {
            Some(2 | 3) => Some((2, "No such file or directory")),
            Some(5) => Some((13, "Permission denied")),
            _ => None,
        };
        if let Some((code, text)) = errno {
            return format!("[Errno {code}] {text}: {}", repr_str(&filename.to_string_lossy()));
        }
    }
    os_error(error, Some(filename))
}

/// A ForgeError carrying the Python OSError text, in the I/O error family.
pub fn io_failure(error: io::Error, message: String) -> ForgeError {
    let mut failure = ForgeError::io("", error);
    failure.message = message;
    failure
}

/// `str(list)` of a command line: Python reprs of each argument.
pub fn command_repr(command: &[OsString]) -> String {
    let items: Vec<String> = command.iter().map(|item| repr_str(&item.to_string_lossy())).collect();
    format!("[{}]", items.join(", "))
}

/// A number as Python printed the timeout it was given (all Forge budgets
/// are integers; tests use fractions).
pub fn py_seconds(seconds: f64) -> String {
    if seconds.fract() == 0.0 && seconds.abs() < 1e15 {
        format!("{}", seconds as i64)
    } else {
        python_float_repr(seconds)
    }
}

/// `str(subprocess.TimeoutExpired)`.
pub fn timeout_expired(command: &[OsString], seconds: f64) -> String {
    format!("Command '{}' timed out after {} seconds", command_repr(command), py_seconds(seconds))
}

/// `bytes.decode("utf-8", errors="replace")` followed by text mode's
/// universal newlines (`\r\n` and `\r` read as `\n`).
///
/// Deliberate difference: Python decoded product and language output with
/// the locale encoding; the runtime prints UTF-8/ASCII, so UTF-8 with
/// replacement is used for every helper.
pub fn decode_text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).replace("\r\n", "\n").replace('\r', "\n")
}

/// `text[-count:]`, counted in code points as Python strings are.
pub fn tail(text: &str, count: usize) -> &str {
    match text.char_indices().rev().nth(count.saturating_sub(1)) {
        Some((index, _)) if count > 0 => &text[index..],
        _ if count == 0 => "",
        _ => text,
    }
}

/// `json.loads(text)` with the JSONDecodeError message Python 3.13 gives
/// for the same document ([`crate::pyjson_error`]).
pub fn json_loads(text: &str) -> std::result::Result<Value, String> {
    serde_json::from_str::<Value>(text)
        .map_err(|error| crate::pyjson_error::locate(text).unwrap_or_else(|| error.to_string()))
}

/// `common.load_json_object(path)` with Python's exception texts: the
/// `open()` OSError, the UTF-8 or JSON ValueError, or "does not contain a
/// JSON object".
pub fn load_json_object(path: &Path) -> Result<Map<String, Value>> {
    let bytes = std::fs::read(path).map_err(|error| {
        let message = open_error(&error, path);
        io_failure(error, message)
    })?;
    let bytes = bytes.strip_prefix(b"\xef\xbb\xbf").unwrap_or(&bytes);
    let text = std::str::from_utf8(bytes).map_err(|error| ForgeError::invalid(utf8_error(bytes, &error)))?;
    match json_loads(text).map_err(ForgeError::invalid)? {
        Value::Object(map) => Ok(map),
        _ => Err(ForgeError::invalid(format!("{} does not contain a JSON object", path.display()))),
    }
}

fn utf8_error(bytes: &[u8], error: &std::str::Utf8Error) -> String {
    let position = error.valid_up_to();
    let byte = bytes[position];
    let reason = match error.error_len() {
        None => "unexpected end of data",
        Some(_) if (0x80..0xc2).contains(&byte) || byte >= 0xf5 => "invalid start byte",
        Some(_) => "invalid continuation byte",
    };
    format!("'utf-8' codec can't decode byte 0x{byte:02x} in position {position}: {reason}")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn json_messages_match_python() {
        assert_eq!(json_loads("").unwrap_err(), "Expecting value: line 1 column 1 (char 0)");
        assert_eq!(json_loads("abc").unwrap_err(), "Expecting value: line 1 column 1 (char 0)");
        assert_eq!(json_loads("{\"a\":1} x").unwrap_err(), "Extra data: line 1 column 9 (char 8)");
        assert_eq!(json_loads("\n {}\n\n]").unwrap_err(), "Extra data: line 4 column 1 (char 6)");
    }

    #[test]
    fn tails_count_code_points() {
        assert_eq!(tail("abcé", 2), "cé");
        assert_eq!(tail("ab", 5), "ab");
        assert_eq!(tail("ab", 0), "");
    }

    #[test]
    fn timeout_text() {
        let command: Vec<OsString> = vec!["C:\\x\\a.exe".into(), "--y".into()];
        assert_eq!(timeout_expired(&command, 0.2), r"Command '['C:\\x\\a.exe', '--y']' timed out after 0.2 seconds");
        assert_eq!(py_seconds(600.0), "600");
    }

    #[test]
    fn universal_newlines() {
        assert_eq!(decode_text(b"a\r\nb\rc\n\xff"), "a\nb\nc\n\u{fffd}");
    }
}
