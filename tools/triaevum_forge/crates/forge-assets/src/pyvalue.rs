// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Python spellings and comparisons over JSON values: the cache-folder keys
//! (`sha256(repr(sorted(identity.items())))`) and the `==` the Python Forge
//! applied to receipts, where `1 == 1.0 == True`.

use forge_base::hashing::sha256_hex;
use forge_base::{ForgeError, Result};
use serde_json::{Map, Number, Value};

/// SHA-256 hex of the ASCII `repr(sorted(identity.items()))`, the folder key
/// of the TopScreen, water and scene-light caches.
pub fn identity_key(identity: &Map<String, Value>) -> Result<String> {
    let mut items: Vec<(&String, &Value)> = identity.iter().collect();
    items.sort_by(|a, b| a.0.as_bytes().cmp(b.0.as_bytes()));
    let mut text = String::from("[");
    for (index, (key, value)) in items.into_iter().enumerate() {
        if index > 0 {
            text.push_str(", ");
        }
        text.push('(');
        text.push_str(&str_repr(key)?);
        text.push_str(", ");
        text.push_str(&scalar_repr(value)?);
        text.push(')');
    }
    text.push(']');
    Ok(sha256_hex(text.as_bytes()))
}

fn scalar_repr(value: &Value) -> Result<String> {
    match value {
        Value::Null => Ok("None".into()),
        Value::Bool(true) => Ok("True".into()),
        Value::Bool(false) => Ok("False".into()),
        Value::Number(number) if !number.is_f64() => Ok(number.to_string()),
        Value::String(text) => str_repr(text),
        _ => Err(ForgeError::internal("Unsupported value in a cache identity")),
    }
}

/// `repr(str)` for ASCII text; non-ASCII made Python's `.encode("ascii")`
/// of the key fail, and fails here too.
fn str_repr(text: &str) -> Result<String> {
    if !text.is_ascii() {
        return Err(ForgeError::invalid(format!("Cache identity is not ASCII: {text:?}")));
    }
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
    Ok(out)
}

/// `recipe.get("inputs", {}).get("romfs", {}).get("sha256")` when truthy:
/// the RomFS identity the water and scene-light caches are keyed by.
pub fn recipe_romfs_source(recipe: &Value) -> Option<&Value> {
    let source = recipe.get("inputs")?.get("romfs")?.get("sha256")?;
    truthy(source).then_some(source)
}

/// `bool(value)`.
pub fn truthy(value: &Value) -> bool {
    match value {
        Value::Null => false,
        Value::Bool(flag) => *flag,
        Value::Number(number) => number.as_f64().is_some_and(|x| x != 0.0),
        Value::String(text) => !text.is_empty(),
        Value::Array(items) => !items.is_empty(),
        Value::Object(map) => !map.is_empty(),
    }
}

/// Python `==` between two `json.loads` results.
pub fn py_eq(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Null, Value::Null) => true,
        (Value::String(x), Value::String(y)) => x == y,
        (Value::Array(x), Value::Array(y)) => x.len() == y.len() && x.iter().zip(y).all(|(p, q)| py_eq(p, q)),
        (Value::Object(x), Value::Object(y)) => {
            x.len() == y.len() && x.iter().all(|(key, value)| y.get(key).is_some_and(|other| py_eq(value, other)))
        }
        _ => matches!((numeric(a), numeric(b)), (Some(x), Some(y)) if x == y),
    }
}

/// An exact numeric view: integers (bools included) or a float.
fn numeric(value: &Value) -> Option<NumericValue> {
    match value {
        Value::Bool(flag) => Some(NumericValue::Int(i128::from(*flag))),
        Value::Number(number) => Some(number_value(number)),
        _ => None,
    }
}

#[derive(Clone, Copy)]
enum NumericValue {
    Int(i128),
    Float(f64),
}

fn number_value(number: &Number) -> NumericValue {
    if let Some(value) = number.as_i64() {
        NumericValue::Int(i128::from(value))
    } else if let Some(value) = number.as_u64() {
        NumericValue::Int(i128::from(value))
    } else {
        NumericValue::Float(number.as_f64().unwrap_or(f64::NAN))
    }
}

impl PartialEq for NumericValue {
    fn eq(&self, other: &Self) -> bool {
        match (*self, *other) {
            (Self::Int(x), Self::Int(y)) => x == y,
            (Self::Float(x), Self::Float(y)) => x == y,
            (Self::Int(i), Self::Float(f)) | (Self::Float(f), Self::Int(i)) => {
                f.fract() == 0.0 && f.abs() < 1.7e38 && f as i128 == i
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn identity_key_matches_python_repr() {
        let identity = json!({"romfs_sha256": "ab", "catalog_version": 1, "hd": null, "q": "it's"});
        let expected = "[('catalog_version', 1), ('hd', None), ('q', \"it's\"), ('romfs_sha256', 'ab')]";
        assert_eq!(identity_key(identity.as_object().unwrap()).unwrap(), sha256_hex(expected.as_bytes()));
    }

    #[test]
    fn python_equality() {
        assert!(py_eq(&json!({"a": 1, "b": [true]}), &json!({"b": [1.0], "a": 1.0})));
        assert!(!py_eq(&json!(1), &json!("1")));
        assert!(!py_eq(&json!({"a": 1}), &json!({"a": 1, "b": 2})));
        assert!(!py_eq(&json!(0.5), &json!(0)));
    }
}
