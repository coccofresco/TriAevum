// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `precompiled_variants.py`: publisher binding of explicitly reviewed
//! ExHeader variants to an existing title. Not runtime compatibility
//! inference; no input bytes, module, adapter or archive are transformed.

use forge_base::{ForgeError, Result};
use serde_json::{Map, Value};

use crate::pyjson::{eq_int, index, is_hex_digest, list, malformed, py_eq, type_name};

fn rejected(message: &str) -> ForgeError {
    ForgeError::integrity(message)
}

fn list_mut<'a>(value: &'a mut Value, key: &str) -> Result<&'a mut Vec<Value>> {
    match value.get_mut(key) {
        Some(Value::Array(items)) => Ok(items),
        _ => Err(malformed(format!("{key} is not a list"))),
    }
}

/// `'input_adapter' in record`.
fn contains_key(record: &Value, key: &str) -> Result<bool> {
    match record {
        Value::Object(map) => Ok(map.contains_key(key)),
        Value::Array(items) => Ok(items.iter().any(|item| item.as_str() == Some(key))),
        Value::String(text) => Ok(text.contains(key)),
        other => Err(malformed(format!("cannot search {}", type_name(other)))),
    }
}

/// `next((r for r in items if r[key] == wanted), None)`.
fn first_match<'a>(items: &'a [Value], key: &str, wanted: &Value) -> Result<Option<&'a Value>> {
    for item in items {
        if py_eq(index(item, key)?, wanted) {
            return Ok(Some(item));
        }
    }
    Ok(None)
}

fn attr<'a>(value: &'a Value, key: &str) -> Result<&'a Value> {
    crate::pyjson::attr_get(value, key)
}

/// `add_verified_variants(catalog, recipes, definitions)`: copies of the
/// catalog and recipes with every reviewed variant of `definitions` bound to
/// its base title. Rerunning on the result is a no-op.
pub fn add_verified_variants(catalog: &Value, recipes: &Value, definitions: &Value) -> Result<(Value, Value)> {
    let (mut catalog, mut recipes) = (catalog.clone(), recipes.clone());
    for variant in list(index(definitions, "recipes")?, "recipes")? {
        let base_id = attr(variant, "precompiled_base_recipe")?;
        if base_id.is_null() {
            continue;
        }
        let base = first_match(list(index(&recipes, "recipes")?, "recipes")?, "id", base_id)?.cloned();
        let title = first_match(list(index(&catalog, "titles")?, "titles")?, "recipe", base_id)?.cloned();
        let (Some(base), Some(title)) = (base, title) else {
            return Err(rejected("Verified variant requires its packaged base recipe and title"));
        };
        for record in [&base, &title, variant] {
            if contains_key(record, "input_adapter")? {
                return Err(rejected("ExHeader variants cannot introduce or inherit input adapters"));
            }
        }
        let variant_inputs = index(variant, "inputs")?;
        let base_inputs = index(&base, "inputs")?;
        let mut changed = !py_eq(index(&title, "inputs")?, base_inputs);
        for kind in ["code", "romfs"] {
            changed = changed || !py_eq(index(variant_inputs, kind)?, index(base_inputs, kind)?);
        }
        changed = changed
            || !py_eq(attr(variant, "process")?, attr(&base, "process")?)
            || !py_eq(attr(variant, "optional_inputs")?, attr(&base, "optional_inputs")?);
        if changed {
            return Err(rejected("Variant changes more than the reviewed ExHeader identity"));
        }
        let empty = Value::Object(Map::new());
        let header = match attr(variant_inputs, "exheader")? {
            Value::Null if !variant_inputs.as_object().is_some_and(|m| m.contains_key("exheader")) => &empty,
            found => found,
        };
        let digest = attr(header, "sha256")?;
        let exact = header.as_object().is_some_and(|h| crate::pyjson::has_keys(h, &["bytes", "sha256"]))
            && eq_int(&header["bytes"], 2048)
            && is_hex_digest(digest);
        if !exact {
            return Err(rejected("Variant requires an exact ExHeader identity"));
        }
        let variant_id = index(variant, "id")?;
        for existing in list(index(&recipes, "recipes")?, "recipes")? {
            if !py_eq(index(existing, "id")?, variant_id) && py_eq(index(existing, "inputs")?, variant_inputs) {
                return Err(rejected("Duplicate input triplet: reuse the existing recipe"));
            }
        }
        let mut expected = title.as_object().cloned().unwrap_or_default();
        expected.insert("recipe".into(), variant_id.clone());
        expected.insert("inputs".into(), variant_inputs.clone());
        let expected = Value::Object(expected);
        for (items, key, value) in [
            (list_mut(&mut recipes, "recipes")?, "id", variant),
            (list_mut(&mut catalog, "titles")?, "recipe", &expected),
        ] {
            let mut matches = Vec::new();
            for item in items.iter() {
                if py_eq(index(item, key)?, variant_id) {
                    matches.push(item);
                }
            }
            if !matches.is_empty() && (matches.len() != 1 || !py_eq(matches[0], value)) {
                return Err(rejected("Existing variant binding conflicts with its definition"));
            }
            if matches.is_empty() {
                items.push(value.clone());
            }
        }
    }
    Ok((catalog, recipes))
}
