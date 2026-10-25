// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Python semantics for JSON values the legacy Forge compared or stringified:
//! `==` between a loaded document and a freshly built one (where `True == 1`
//! and `2 == 2.0`), and `str(value)` used to turn a descriptor field into a
//! path or digest before comparing it.

use serde_json::{Map, Number, Value};

use forge_base::legacy_json::python_float_repr;

/// Python `a == b` for JSON-loaded values.
pub fn py_eq(left: &Value, right: &Value) -> bool {
    match (left, right) {
        (Value::Null, Value::Null) => true,
        (Value::String(a), Value::String(b)) => a == b,
        (Value::Array(a), Value::Array(b)) => a.len() == b.len() && a.iter().zip(b).all(|(x, y)| py_eq(x, y)),
        (Value::Object(a), Value::Object(b)) => objects_eq(a, b),
        (Value::Bool(_) | Value::Number(_), Value::Bool(_) | Value::Number(_)) => {
            match (numeric(left), numeric(right)) {
                (Some(a), Some(b)) => a.equals(&b),
                _ => false,
            }
        }
        _ => false,
    }
}

pub fn objects_eq(left: &Map<String, Value>, right: &Map<String, Value>) -> bool {
    left.len() == right.len() && left.iter().all(|(key, value)| right.get(key).is_some_and(|other| py_eq(value, other)))
}

/// `dict.get(key)` compared with `==`, where a missing key is `None`.
pub fn get_eq(map: &Map<String, Value>, key: &str, expected: &Value) -> bool {
    py_eq(map.get(key).unwrap_or(&Value::Null), expected)
}

enum Numeric {
    Integer(i128),
    Float(f64),
}

fn numeric(value: &Value) -> Option<Numeric> {
    match value {
        Value::Bool(flag) => Some(Numeric::Integer(i128::from(*flag))),
        Value::Number(number) => Some(number_value(number)),
        _ => None,
    }
}

fn number_value(number: &Number) -> Numeric {
    if let Some(value) = number.as_i64() {
        Numeric::Integer(i128::from(value))
    } else if let Some(value) = number.as_u64() {
        Numeric::Integer(i128::from(value))
    } else {
        Numeric::Float(number.as_f64().unwrap_or(f64::NAN))
    }
}

impl Numeric {
    fn equals(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::Integer(a), Self::Integer(b)) => a == b,
            (Self::Float(a), Self::Float(b)) => a == b,
            // Python compares int and float exactly.
            (Self::Integer(i), Self::Float(f)) | (Self::Float(f), Self::Integer(i)) => {
                f.is_finite() && f.fract() == 0.0 && f.abs() < 2f64.powi(127) && (*f as i128) == *i
            }
        }
    }
}

/// Python `str(value)` for a JSON-loaded value (scalars exactly; containers
/// approximately, which only matters for nonsense metadata).
pub fn py_str(value: &Value) -> String {
    match value {
        Value::Null => "None".to_string(),
        Value::Bool(true) => "True".to_string(),
        Value::Bool(false) => "False".to_string(),
        Value::String(text) => text.clone(),
        Value::Number(number) => match number_value(number) {
            Numeric::Integer(value) => value.to_string(),
            Numeric::Float(value) => python_float_repr(value).replace("Infinity", "inf").replace("NaN", "nan"),
        },
        other => other.to_string(),
    }
}

/// `str(d.get(key, ""))`.
pub fn get_str(map: &Map<String, Value>, key: &str) -> String {
    map.get(key).map(py_str).unwrap_or_default()
}

/// Python truthiness of `isinstance(value, int) and not isinstance(value, bool)`.
pub fn as_python_int(value: &Value) -> Option<i128> {
    match value {
        Value::Number(number) => match number_value(number) {
            Numeric::Integer(value) => Some(value),
            Numeric::Float(_) => None,
        },
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn equality_follows_python() {
        assert!(py_eq(&json!(true), &json!(1)));
        assert!(py_eq(&json!(2), &json!(2.0)));
        assert!(!py_eq(&json!(2), &json!("2")));
        assert!(py_eq(&json!({"a": [1, false]}), &json!({"a": [1.0, 0]})));
        assert!(!py_eq(&json!({"a": 1}), &json!({"a": 1, "b": null})));
        assert!(py_eq(&json!(u64::MAX), &json!(u64::MAX)));
    }

    #[test]
    fn str_follows_python() {
        assert_eq!(py_str(&json!(null)), "None");
        assert_eq!(py_str(&json!(true)), "True");
        assert_eq!(py_str(&json!(12)), "12");
        assert_eq!(py_str(&json!(1.5)), "1.5");
        assert_eq!(py_str(&json!("x")), "x");
    }
}
