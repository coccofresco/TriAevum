// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Python semantics over `json.loads` results for catalogs and recipes: the
//! shared helpers of [`forge_base::pyvalue`] plus the accessors that turn
//! Python's KeyError/AttributeError on malformed package data into a typed
//! catalog error.

use forge_base::{ForgeError, Result};
use serde_json::Value;

pub use forge_base::pyvalue::{
    eq_int, eq_str, get, has_keys, int, is_hex_digest, is_int, py_eq, py_str, py_strip, repr, repr_str, truthy,
    type_name, NULL,
};

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

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn malformed_data_is_a_typed_error() {
        assert!(attr_get(&json!([1]), "a").is_err());
        assert!(index(&json!({}), "a").unwrap_err().message.contains("missing \"a\""));
        assert!(attr_get(&json!({}), "a").unwrap().is_null());
        assert!(list(&json!({}), "titles").is_err());
    }
}
