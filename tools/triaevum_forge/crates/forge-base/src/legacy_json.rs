// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Byte-exact equivalents of the Python `json.dumps` spellings Forge files use.
//!
//! Metadata files are referenced by SHA-256 elsewhere (process manifest from
//! content.tap, launch profile from the runtime receipt, TAM metadata), so
//! their bytes must match what the Python Forge wrote for the same value:
//!
//! * [`Style::Pretty`]: `json.dumps(v, indent=2, sort_keys=True)` plus LF,
//!   as `common.atomic_write_json` writes.
//! * [`Style::Compact`]: `json.dumps(v, sort_keys=True, separators=(",", ":"))`.
//! * [`Style::Spaced`]: `json.dumps(v, sort_keys=True)` (", " and ": ").
//!
//! Python escapes non-ASCII as lowercase `\uXXXX` (surrogate pairs above the
//! BMP) unless `ensure_ascii=False`, and spells floats with `float.__repr__`.

use serde_json::{Map, Number, Value};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Style {
    Pretty,
    Compact,
    Spaced,
}

#[derive(Clone, Copy, Debug)]
pub struct Options {
    pub style: Style,
    pub sort_keys: bool,
    pub ensure_ascii: bool,
}

impl Options {
    pub const PRETTY: Self = Self { style: Style::Pretty, sort_keys: true, ensure_ascii: true };
    pub const COMPACT: Self = Self { style: Style::Compact, sort_keys: true, ensure_ascii: true };
    pub const SPACED: Self = Self { style: Style::Spaced, sort_keys: true, ensure_ascii: true };
}

/// `json.dumps(value, ...)` with the given options, without a trailing LF.
pub fn dumps(value: &Value, options: Options) -> String {
    let mut out = String::new();
    write_value(&mut out, value, options, 0);
    out
}

/// The bytes `common.atomic_write_json` writes: pretty, sorted, ASCII, LF.
pub fn pretty_file_bytes(value: &Value) -> Vec<u8> {
    let mut text = dumps(value, Options::PRETTY);
    text.push('\n');
    text.into_bytes()
}

/// Compact sorted JSON plus LF, the TAM metadata section encoding.
pub fn compact_line_bytes(value: &Value) -> Vec<u8> {
    let mut text = dumps(value, Options::COMPACT);
    text.push('\n');
    text.into_bytes()
}

/// Parses a JSON object the way `common.load_json_object` does: an optional
/// UTF-8 BOM is accepted and the top level must be an object.
pub fn load_object(bytes: &[u8]) -> Result<Map<String, Value>, String> {
    let bytes = bytes.strip_prefix(b"\xef\xbb\xbf").unwrap_or(bytes);
    let text = std::str::from_utf8(bytes).map_err(|error| format!("not UTF-8: {error}"))?;
    match serde_json::from_str::<Value>(text).map_err(|error| error.to_string())? {
        Value::Object(map) => Ok(map),
        _ => Err("JSON document is not an object".to_string()),
    }
}

fn write_value(out: &mut String, value: &Value, options: Options, depth: usize) {
    match value {
        Value::Null => out.push_str("null"),
        Value::Bool(true) => out.push_str("true"),
        Value::Bool(false) => out.push_str("false"),
        Value::Number(number) => write_number(out, number),
        Value::String(text) => write_string(out, text, options.ensure_ascii),
        Value::Array(items) => {
            if items.is_empty() {
                out.push_str("[]");
                return;
            }
            out.push('[');
            for (index, item) in items.iter().enumerate() {
                if index > 0 {
                    out.push_str(item_separator(options.style));
                }
                newline_indent(out, options.style, depth + 1);
                write_value(out, item, options, depth + 1);
            }
            newline_indent(out, options.style, depth);
            out.push(']');
        }
        Value::Object(map) => {
            if map.is_empty() {
                out.push_str("{}");
                return;
            }
            let mut entries: Vec<(&String, &Value)> = map.iter().collect();
            if options.sort_keys {
                // Python sorts str keys by code point; UTF-8 byte order agrees.
                entries.sort_by(|a, b| a.0.as_bytes().cmp(b.0.as_bytes()));
            }
            out.push('{');
            for (index, (key, item)) in entries.into_iter().enumerate() {
                if index > 0 {
                    out.push_str(item_separator(options.style));
                }
                newline_indent(out, options.style, depth + 1);
                write_string(out, key, options.ensure_ascii);
                out.push_str(if options.style == Style::Compact { ":" } else { ": " });
                write_value(out, item, options, depth + 1);
            }
            newline_indent(out, options.style, depth);
            out.push('}');
        }
    }
}

fn item_separator(style: Style) -> &'static str {
    match style {
        Style::Pretty | Style::Compact => ",",
        Style::Spaced => ", ",
    }
}

fn newline_indent(out: &mut String, style: Style, depth: usize) {
    if style == Style::Pretty {
        out.push('\n');
        for _ in 0..depth {
            out.push_str("  ");
        }
    }
}

fn write_string(out: &mut String, text: &str, ensure_ascii: bool) {
    out.push('"');
    for character in text.chars() {
        match character {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '\u{08}' => out.push_str("\\b"),
            '\u{0c}' => out.push_str("\\f"),
            c if (c as u32) < 0x20 => push_unicode_escape(out, c as u32),
            c if ensure_ascii && !c.is_ascii() => {
                let mut units = [0u16; 2];
                for unit in c.encode_utf16(&mut units) {
                    push_unicode_escape(out, u32::from(*unit));
                }
            }
            c => out.push(c),
        }
    }
    out.push('"');
}

fn push_unicode_escape(out: &mut String, unit: u32) {
    out.push_str(&format!("\\u{unit:04x}"));
}

fn write_number(out: &mut String, number: &Number) {
    if let Some(value) = number.as_i64() {
        out.push_str(&value.to_string());
    } else if let Some(value) = number.as_u64() {
        out.push_str(&value.to_string());
    } else if let Some(value) = number.as_f64() {
        out.push_str(&python_float_repr(value));
    }
}

/// `float.__repr__` as `json.dumps` uses it (allow_nan spellings included).
pub fn python_float_repr(value: f64) -> String {
    if value.is_nan() {
        return "NaN".to_string();
    }
    if value.is_infinite() {
        return if value > 0.0 { "Infinity" } else { "-Infinity" }.to_string();
    }
    // Rust's `{:e}` is the shortest round-trip digit string, like Python's
    // repr; only the placement of the decimal point differs.
    let scientific = format!("{:e}", value.abs());
    let (mantissa, exponent) = scientific.split_once('e').expect("LowerExp has an exponent");
    let exponent: i32 = exponent.parse().expect("LowerExp exponent is an integer");
    let digits: String = mantissa.chars().filter(|c| *c != '.').collect();
    let sign = if value.is_sign_negative() { "-" } else { "" };
    if value == 0.0 {
        return format!("{sign}0.0");
    }
    if (-4..16).contains(&exponent) {
        let point = exponent + 1;
        let body = if point <= 0 {
            format!("0.{}{}", "0".repeat(usize::try_from(-point).unwrap_or(0)), digits)
        } else {
            let point = usize::try_from(point).unwrap_or(0);
            if digits.len() <= point {
                format!("{}{}.0", digits, "0".repeat(point - digits.len()))
            } else {
                format!("{}.{}", &digits[..point], &digits[point..])
            }
        };
        format!("{sign}{body}")
    } else {
        let mantissa = if digits.len() == 1 { digits } else { format!("{}.{}", &digits[..1], &digits[1..]) };
        let exponent_sign = if exponent < 0 { '-' } else { '+' };
        format!("{sign}{mantissa}e{exponent_sign}{:02}", exponent.abs())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn float_repr_matches_python() {
        let cases = [
            (0.0, "0.0"),
            (-0.0, "-0.0"),
            (1.0, "1.0"),
            (0.1, "0.1"),
            (1.5, "1.5"),
            (0.0001, "0.0001"),
            (0.00001, "1e-05"),
            (1.25e-7, "1.25e-07"),
            (1234567890123456.0, "1234567890123456.0"),
            (1e16, "1e+16"),
            (1.5e16, "1.5e+16"),
            (123.456, "123.456"),
            (-2.5e-5, "-2.5e-05"),
            (1e22, "1e+22"),
            (0.30000000000000004, "0.30000000000000004"),
            (f64::INFINITY, "Infinity"),
        ];
        for (value, expected) in cases {
            assert_eq!(python_float_repr(value), expected, "{value}");
        }
    }

    #[test]
    fn pretty_matches_atomic_write_json() {
        let value = json!({"b": [1, {"z": null, "a": true}], "a": {}, "c": [], "é": "ü\n\u{1F600}"});
        let expected = "{\n  \"a\": {},\n  \"b\": [\n    1,\n    {\n      \"a\": true,\n      \"z\": null\n    }\n  ],\n  \"c\": [],\n  \"\\u00e9\": \"\\u00fc\\n\\ud83d\\ude00\"\n}\n";
        assert_eq!(String::from_utf8(pretty_file_bytes(&value)).unwrap(), expected);
    }

    #[test]
    fn compact_and_spaced() {
        let value = json!({"b": 1, "a": [1.0, 2, "x"]});
        assert_eq!(dumps(&value, Options::COMPACT), "{\"a\":[1.0,2,\"x\"],\"b\":1}");
        assert_eq!(dumps(&value, Options::SPACED), "{\"a\": [1.0, 2, \"x\"], \"b\": 1}");
    }

    #[test]
    fn load_accepts_bom_and_rejects_non_objects() {
        assert!(load_object(b"\xef\xbb\xbf{\"a\": 1}").is_ok());
        assert!(load_object(b"[1]").is_err());
    }
}
