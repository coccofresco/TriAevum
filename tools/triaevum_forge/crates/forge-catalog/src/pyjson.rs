// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Python semantics over `json.loads` results: the equality, type tests,
//! truthiness and `str()` spellings the Python Forge applied to catalogs and
//! recipes. `1 == 1.0 == True` and a missing key reads as `None`, so a plain
//! `serde_json::Value` comparison would accept or reject different records.

use forge_base::{ForgeError, Result};
use serde_json::{Map, Number, Value};

pub static NULL: Value = Value::Null;

/// `mapping.get(key)`: a missing key reads as `None`.
pub fn get<'a>(map: &'a Map<String, Value>, key: &str) -> &'a Value {
    map.get(key).unwrap_or(&NULL)
}

/// `value.get(key)` on a value that Python assumed was a dict. Python raised
/// AttributeError (a traceback) for other types; both versions reject.
pub fn attr_get<'a>(value: &'a Value, key: &str) -> Result<&'a Value> {
    match value {
        Value::Object(map) => Ok(get(map, key)),
        other => Err(malformed(format!("expected an object with {key:?}, found {}", type_name(other)))),
    }
}

/// `value[key]`: Python raised KeyError/TypeError for a missing key or a
/// non-dict; both versions reject.
pub fn index<'a>(value: &'a Value, key: &str) -> Result<&'a Value> {
    match value {
        Value::Object(map) => map.get(key).ok_or_else(|| malformed(format!("missing {key:?}"))),
        other => Err(malformed(format!("expected an object with {key:?}, found {}", type_name(other)))),
    }
}

/// `value` iterated as a list (Python iterated whatever it found).
pub fn list<'a>(value: &'a Value, what: &str) -> Result<&'a Vec<Value>> {
    value.as_array().ok_or_else(|| malformed(format!("{what} is not a list")))
}

/// Where Python crashed with KeyError/AttributeError/TypeError on malformed
/// package data. Deliberate difference: a typed error instead of a traceback.
pub fn malformed(detail: String) -> ForgeError {
    ForgeError::integrity(format!("Malformed catalog data: {detail}"))
}

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

/// Python `==` between JSON-loaded values.
pub fn py_eq(left: &Value, right: &Value) -> bool {
    match (left, right) {
        (Value::Null, Value::Null) => true,
        (Value::String(a), Value::String(b)) => a == b,
        (Value::Array(a), Value::Array(b)) => a.len() == b.len() && a.iter().zip(b).all(|(x, y)| py_eq(x, y)),
        (Value::Object(a), Value::Object(b)) => {
            a.len() == b.len() && a.iter().all(|(key, x)| b.get(key).is_some_and(|y| py_eq(x, y)))
        }
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
    // Python compares int and float exactly; an integral f64 below 1e38
    // converts to i128 without loss.
    float.is_finite() && float.fract() == 0.0 && float.abs() < 1.0e38 && float as i128 == integer
}

/// `set(mapping) == {...keys}`.
pub fn has_keys(map: &Map<String, Value>, keys: &[&str]) -> bool {
    map.len() == keys.len() && keys.iter().all(|key| map.contains_key(*key))
}

/// `isinstance(v, str) and len(v) == 64 and all(c in "0123456789abcdef" ...)`.
pub fn is_hex_digest(value: &Value) -> bool {
    value.as_str().is_some_and(forge_base::hashing::is_sha256_hex)
}

/// `str(value)`, where Python formatted a JSON value into a message.
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
            None => forge_base::legacy_json::python_float_repr(number.as_f64().unwrap_or(f64::NAN)),
        },
        Value::String(text) => repr_str(text),
        Value::Array(items) => format!("[{}]", items.iter().map(repr).collect::<Vec<_>>().join(", ")),
        Value::Object(map) => format!(
            "{{{}}}",
            map.iter().map(|(key, item)| format!("{}: {}", repr_str(key), repr(item))).collect::<Vec<_>>().join(", ")
        ),
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
    fn python_equality() {
        assert!(py_eq(&json!(1), &json!(1.0)));
        assert!(py_eq(&json!(true), &json!(1)));
        assert!(!py_eq(&json!(true), &json!(2)));
        assert!(py_eq(&json!({"a": [1, null], "b": "x"}), &json!({"b": "x", "a": [1.0, null]})));
        assert!(!py_eq(&json!({"a": 1}), &json!({"a": 1, "b": null})));
        assert!(!py_eq(&json!("1"), &json!(1)));
        assert!(py_eq(&json!(18446744073709551615u64), &json!(18446744073709551615u64)));
    }

    #[test]
    fn spellings() {
        assert_eq!(repr(&json!({"a": [true, null, 1.5, "it's"]})), "{'a': [True, None, 1.5, \"it's\"]}");
        assert_eq!(py_str(&json!("plain")), "plain");
        assert!(!is_int(&json!(true)) && !is_int(&json!(1.0)) && is_int(&json!(-3)));
        assert!(!truthy(&json!({})) && truthy(&json!([0])));
        assert_eq!(py_strip("\u{1f} EUR \n"), "EUR");
    }
}
