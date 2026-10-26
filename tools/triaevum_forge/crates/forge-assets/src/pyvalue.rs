// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Python spellings and comparisons over JSON values: the cache-folder keys
//! (`sha256(repr(sorted(identity.items())))`) and the `==` the Python Forge
//! applied to receipts (shared through [`forge_base::pyvalue`]).

use forge_base::hashing::sha256_hex;
use forge_base::pyvalue::repr_str;
use forge_base::{ForgeError, Result};
use serde_json::{Map, Value};

pub use forge_base::pyvalue::{py_eq, truthy};

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
    Ok(repr_str(text))
}

/// `recipe.get("inputs", {}).get("romfs", {}).get("sha256")` when truthy:
/// the RomFS identity the water and scene-light caches are keyed by.
pub fn recipe_romfs_source(recipe: &Value) -> Option<&Value> {
    let source = recipe.get("inputs")?.get("romfs")?.get("sha256")?;
    truthy(source).then_some(source)
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
        assert!(identity_key(json!({"k": "\u{e9}"}).as_object().unwrap()).is_err());
    }
}
