// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Python semantics over `json.loads` results, shared by every crate that
//! compares or spells legacy metadata: `==` (where `1 == 1.0 == True`), a
//! missing key reading as `None`, truthiness, type tests and the `str()` /
//! `repr()` spellings that end up in messages and cache keys. A plain
//! `serde_json::Value` comparison would accept or reject different records.

use serde_json::{Map, Number, Value};

use crate::hashing::is_sha256_hex;
use crate::legacy_json::python_float_repr;

pub static NULL: Value = Value::Null;

/// `mapping.get(key)`: a missing key reads as `None`.
pub fn get<'a>(map: &'a Map<String, Value>, key: &str) -> &'a Value {
    map.get(key).unwrap_or(&NULL)
}

/// `type(value).__name__`.
pub fn type_name(value: &Value) -> &'static str {
    match value {
        Value::Null => "NoneType",
        Value::Bool(_) => "bool",
        Value::Number(number) if number.is_f64() => "float",
        Value::Number(_) => "int",
        Value::String(_) => "str",
        Value::Array(_) => "list",
        Value::Object(_) => "dict",
    }
}

/// `type(value) is int` (bool and float excluded).
pub fn is_int(value: &Value) -> bool {
    matches!(value, Value::Number(number) if !number.is_f64())
}

/// The integer of a `type(value) is int` value.
pub fn int(value: &Value) -> Option<i128> {
    match value {
        Value::Number(number) => integer(number),
        _ => None,
    }
}

/// `isinstance(value, int) and not isinstance(value, bool)`: the integer.
pub fn as_python_int(value: &Value) -> Option<i128> {
    int(value)
}

fn integer(number: &Number) -> Option<i128> {
    number.as_i64().map(i128::from).or_else(|| number.as_u64().map(i128::from))
}

/// `bool(value)`.
pub fn truthy(value: &Value) -> bool {
    match value {
        Value::Null => false,
        Value::Bool(flag) => *flag,
        Value::Number(number) => integer(number).map_or_else(|| number.as_f64() != Some(0.0), |n| n != 0),
        Value::String(text) => !text.is_empty(),
        Value::Array(items) => !items.is_empty(),
        Value::Object(map) => !map.is_empty(),
    }
}

/// Python `left == right` between JSON-loaded values.
pub fn py_eq(left: &Value, right: &Value) -> bool {
    match (left, right) {
        (Value::Null, Value::Null) => true,
        (Value::String(a), Value::String(b)) => a == b,
        (Value::Array(a), Value::Array(b)) => a.len() == b.len() && a.iter().zip(b).all(|(x, y)| py_eq(x, y)),
        (Value::Object(a), Value::Object(b)) => objects_eq(a, b),
        (Value::Bool(_) | Value::Number(_), Value::Bool(_) | Value::Number(_)) => {
            match (numeric(left), numeric(right)) {
                (Numeric::Int(a), Numeric::Int(b)) => a == b,
                (Numeric::Float(a), Numeric::Float(b)) => a == b,
                (Numeric::Int(a), Numeric::Float(b)) | (Numeric::Float(b), Numeric::Int(a)) => int_float_eq(a, b),
            }
        }
        _ => false,
    }
}

/// `left == right` for two dicts (same keys, Python-equal values).
pub fn objects_eq(left: &Map<String, Value>, right: &Map<String, Value>) -> bool {
    left.len() == right.len() && left.iter().all(|(key, value)| right.get(key).is_some_and(|other| py_eq(value, other)))
}

/// `dict.get(key) == expected`, where a missing key is `None`.
pub fn get_eq(map: &Map<String, Value>, key: &str, expected: &Value) -> bool {
    py_eq(get(map, key), expected)
}

/// `left == number` for an integer Forge computed (a size).
pub fn eq_int(left: &Value, number: u64) -> bool {
    py_eq(left, &Value::from(number))
}

/// `left == text`.
pub fn eq_str(left: &Value, text: &str) -> bool {
    left.as_str() == Some(text)
}

enum Numeric {
    Int(i128),
    Float(f64),
}

fn numeric(value: &Value) -> Numeric {
    match value {
        Value::Bool(flag) => Numeric::Int(i128::from(*flag)),
        Value::Number(number) => match integer(number) {
            Some(n) => Numeric::Int(n),
            None => Numeric::Float(number.as_f64().unwrap_or(f64::NAN)),
        },
        _ => Numeric::Float(f64::NAN),
    }
}

fn int_float_eq(integer: i128, float: f64) -> bool {
    // Python compares int and float exactly. JSON integers fit in u64/i64, so
    // an integral f64 below 1e38 (lossless as i128) covers every equal pair.
    float.is_finite() && float.fract() == 0.0 && float.abs() < 1.0e38 && float as i128 == integer
}

/// `set(mapping) == {...keys}`.
pub fn has_keys(map: &Map<String, Value>, keys: &[&str]) -> bool {
    map.len() == keys.len() && keys.iter().all(|key| map.contains_key(*key))
}

/// `isinstance(v, str) and len(v) == 64 and all(c in "0123456789abcdef" ...)`.
pub fn is_hex_digest(value: &Value) -> bool {
    value.as_str().is_some_and(is_sha256_hex)
}

/// `str(value)`: the text itself for a string, `repr(value)` otherwise.
pub fn py_str(value: &Value) -> String {
    match value {
        Value::String(text) => text.clone(),
        other => repr(other),
    }
}

/// `repr(value)` for JSON-loaded values.
pub fn repr(value: &Value) -> String {
    match value {
        Value::Null => "None".to_string(),
        Value::Bool(true) => "True".to_string(),
        Value::Bool(false) => "False".to_string(),
        Value::Number(number) => match integer(number) {
            Some(n) => n.to_string(),
            None => float_repr(number.as_f64().unwrap_or(f64::NAN)),
        },
        Value::String(text) => repr_str(text),
        Value::Array(items) => format!("[{}]", items.iter().map(repr).collect::<Vec<_>>().join(", ")),
        Value::Object(map) => format!(
            "{{{}}}",
            map.iter().map(|(key, item)| format!("{}: {}", repr_str(key), repr(item))).collect::<Vec<_>>().join(", ")
        ),
    }
}

/// `repr(float)`: `json.dumps` spells the non-finite values differently.
fn float_repr(value: f64) -> String {
    match python_float_repr(value).as_str() {
        "NaN" => "nan".to_string(),
        "Infinity" => "inf".to_string(),
        "-Infinity" => "-inf".to_string(),
        finite => finite.to_string(),
    }
}

/// `repr(str)`: single quotes unless the text has one and no double quote.
pub fn repr_str(text: &str) -> String {
    let quote = if text.contains('\'') && !text.contains('"') { '"' } else { '\'' };
    let mut out = String::with_capacity(text.len() + 2);
    out.push(quote);
    for character in text.chars() {
        match character {
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c == quote => {
                out.push('\\');
                out.push(c);
            }
            c if (c as u32) < 0x20 || c as u32 == 0x7f => out.push_str(&format!("\\x{:02x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push(quote);
    out
}

/// `str.strip()` with Python's whitespace set.
pub fn py_strip(text: &str) -> &str {
    text.trim_matches(|c: char| c.is_whitespace() || ('\u{1c}'..='\u{1f}').contains(&c))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn equality_follows_python() {
        assert!(py_eq(&json!(1), &json!(1.0)));
        assert!(py_eq(&json!(true), &json!(1)));
        assert!(!py_eq(&json!(true), &json!(2)));
        assert!(!py_eq(&json!(0.5), &json!(0)));
        assert!(!py_eq(&json!(2), &json!("2")));
        assert!(py_eq(&json!({"a": [1, null, false], "b": "x"}), &json!({"b": "x", "a": [1.0, null, 0]})));
        assert!(!py_eq(&json!({"a": 1}), &json!({"a": 1, "b": null})));
        assert!(py_eq(&json!(u64::MAX), &json!(u64::MAX)));
        assert!(get_eq(json!({}).as_object().unwrap(), "missing", &Value::Null));
    }

    #[test]
    fn spellings_follow_python() {
        assert_eq!(repr(&json!({"a": [true, null, 1.5, "it's"]})), "{'a': [True, None, 1.5, \"it's\"]}");
        assert_eq!(py_str(&json!("plain")), "plain");
        assert_eq!(py_str(&json!(null)), "None");
        assert_eq!(py_str(&json!(12)), "12");
        assert_eq!(py_str(&json!(1.5)), "1.5");
        assert_eq!(repr_str("a\\b\n\u{7f}"), "'a\\\\b\\n\\x7f'");
        assert_eq!(py_strip("\u{1f} EUR \n"), "EUR");
    }

    #[test]
    fn types_and_truthiness() {
        assert!(!is_int(&json!(true)) && !is_int(&json!(1.0)) && is_int(&json!(-3)));
        assert_eq!(as_python_int(&json!(7)), Some(7));
        assert_eq!(as_python_int(&json!(7.0)), None);
        assert!(!truthy(&json!({})) && truthy(&json!([0])) && !truthy(&json!(0.0)) && truthy(&json!(-1)));
        assert_eq!(type_name(&json!(1.0)), "float");
    }
}
