// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `qualified_input_coverage.py`: reviewed ROM import contracts (from
//! `input_coverage/coverage.json`) preserved across platform/runtime
//! rebuilds of the catalog and recipes.

use std::path::Path;

use forge_base::{CancelToken, ForgeError, Result};
use serde_json::{json, Map, Value};

use crate::adapters::validate_adapter;
use crate::family::expected;
use crate::paths::join_posix;
use crate::precompiled::checked_file;
use crate::pyjson::{attr_get, index, list, malformed, py_eq, py_str, truthy};

/// The distribution's coverage definitions directory.
pub const COVERAGE_DIRECTORY: &str = "tools/triaevum_release/input_coverage";
pub const DEFINITIONS: &str = "coverage.json";

fn rejected(message: &str) -> ForgeError {
    ForgeError::integrity(message)
}

fn load_definitions(root: &Path) -> Result<Value> {
    Ok(Value::Object(forge_base::fsutil::load_json_object(&join_posix(root, DEFINITIONS))?))
}

fn matching<'a>(items: &'a [Value], key: &str, wanted: &Value) -> Result<Vec<&'a Value>> {
    let mut found = Vec::new();
    for item in items {
        if py_eq(index(item, key)?, wanted) {
            found.push(item);
        }
    }
    Ok(found)
}

fn list_field<'a>(value: &'a Value, key: &str) -> Result<&'a Vec<Value>> {
    list(index(value, key)?, key)
}

fn object(value: Value, what: &str) -> Result<Map<String, Value>> {
    match value {
        Value::Object(map) => Ok(map),
        _ => Err(malformed(format!("{what} is not an object"))),
    }
}

/// `bind(catalog, recipes, root=...)`: catalog and recipes with every
/// qualified input bound to its canonical title, plus the COPY artifacts the
/// package must carry (`{"source", "path", "role"}`).
pub fn bind(catalog: &Value, recipes: &Value, root: &Path, cancel: &CancelToken) -> Result<(Value, Value, Vec<Value>)> {
    let definitions = load_definitions(root)?;
    let (mut catalog, mut recipes) = (catalog.clone(), recipes.clone());
    let mut artifacts: Vec<(Value, Value)> = Vec::new();
    for binding in list_field(&definitions, "bindings")? {
        let base_id = index(binding, "base_recipe")?;
        let bases = matching(list_field(&recipes, "recipes")?, "id", base_id)?;
        let titles = matching(list_field(&catalog, "titles")?, "recipe", base_id)?;
        if bases.len() != 1 || titles.len() != 1 {
            return Err(rejected("Qualified inputs require exactly one canonical recipe/title"));
        }
        let (base, title) = (bases[0].clone(), titles[0].clone());
        let base_inputs = index(&base, "inputs")?;
        let canonical_changed = !py_eq(base_inputs, index(binding, "base_inputs")?)
            || !py_eq(index(&title, "inputs")?, base_inputs)
            || !py_eq(attr_get(&base, "process")?, index(binding, "process")?)
            || !attr_get(&base, "input_adapter")?.is_null();
        if canonical_changed {
            return Err(rejected("Qualified inputs do not match the current canonical execution contract"));
        }
        let identity = index(binding, "input")?;
        let identity_id = index(identity, "id")?;
        if !py_eq(index(index(identity, "inputs")?, "code")?, index(base_inputs, "code")?) {
            return Err(rejected("Qualified input must preserve canonical code"));
        }
        let existing = matching(list_field(&recipes, "recipes")?, "id", identity_id)?;
        if existing.len() > 1 {
            return Err(rejected("Duplicate qualified recipe"));
        }
        let mut recipe = object(existing.first().map_or_else(|| base.clone(), |r| (*r).clone()), "recipe")?;
        if !existing.is_empty() {
            let conflicts = !py_eq(index(&Value::Object(recipe.clone()), "inputs")?, index(identity, "inputs")?)
                || !py_eq(crate::pyjson::get(&recipe, "process"), attr_get(&base, "process")?);
            if conflicts {
                return Err(rejected("Existing input contract conflicts with qualification"));
            }
        }
        for field in ["input_adapter", "data_compatibility"] {
            let current = crate::pyjson::get(&recipe, field);
            if !current.is_null() && !py_eq(current, attr_get(identity, field)?) {
                return Err(rejected("Existing qualified contract conflicts"));
            }
        }
        recipe.shift_remove("precompiled_base_recipe");
        for (key, value) in object(identity.clone(), "input")? {
            recipe.insert(key, value);
        }
        for phase in ["source", "execution"] {
            if expected(&recipe, phase)?.is_none() {
                return Err(rejected("Qualified input requires both content families"));
            }
        }
        validate_adapter(root, &recipe, cancel)?;
        let adapter = crate::pyjson::get(&recipe, "input_adapter").clone();
        if truthy(&adapter) {
            let record = index(&adapter, "code_copies")?;
            let source = checked_file(root, record, cancel)?;
            let path = index(record, "path")?.clone();
            let artifact = json!({"source": source.to_string_lossy(), "path": path, "role": "input_copy_adapter"});
            match artifacts.iter_mut().find(|(key, _)| py_eq(key, &path)) {
                Some(slot) => slot.1 = artifact,
                None => artifacts.push((path, artifact)),
            }
        }
        let mut new_title = object(title, "title")?;
        let recipe_value = Value::Object(recipe.clone());
        new_title.insert("recipe".into(), index(&recipe_value, "id")?.clone());
        new_title.insert("inputs".into(), index(&recipe_value, "inputs")?.clone());
        new_title.insert("data_compatibility".into(), index(&recipe_value, "data_compatibility")?.clone());
        new_title.shift_remove("input_adapter");
        if truthy(&adapter) {
            new_title.insert("input_adapter".into(), adapter);
        }
        let recipe_id = recipe_value["id"].clone();
        replace_entry(&mut recipes, "recipes", "id", &recipe_id, recipe_value)?;
        replace_entry(&mut catalog, "titles", "recipe", &recipe_id, Value::Object(new_title))?;
    }
    Ok((catalog, recipes, artifacts.into_iter().map(|(_, artifact)| artifact).collect()))
}

/// `items = [r for r in items if r[key] != id] + [value]`.
fn replace_entry(container: &mut Value, field: &str, key: &str, id: &Value, value: Value) -> Result<()> {
    let mut kept = Vec::new();
    for item in list_field(container, field)? {
        if !py_eq(index(item, key)?, id) {
            kept.push(item.clone());
        }
    }
    kept.push(value);
    container[field] = Value::Array(kept);
    Ok(())
}

/// `require_coverage(catalog, recipes, root=...)`: every qualified input
/// whose base recipe ships must ship unchanged.
pub fn require_coverage(catalog: &Value, recipes: &Value, root: &Path) -> Result<()> {
    let definitions = load_definitions(root)?;
    let recipe_list = list_field(recipes, "recipes")?;
    let mut present = Vec::new();
    for recipe in recipe_list {
        present.push(index(recipe, "id")?);
    }
    for binding in list_field(&definitions, "bindings")? {
        let base_id = index(binding, "base_recipe")?;
        if !present.iter().any(|id| py_eq(id, base_id)) {
            continue;
        }
        let identity = index(binding, "input")?;
        let identity_id = index(identity, "id")?;
        let matches = matching(recipe_list, "id", identity_id)?;
        let titles = matching(list_field(catalog, "titles")?, "recipe", identity_id)?;
        if matches.len() != 1 || titles.len() != 1 {
            return Err(rejected(&format!("Release lost qualified input coverage: {}", py_str(identity_id))));
        }
        for field in ["inputs", "input_adapter", "data_compatibility"] {
            let wanted = attr_get(identity, field)?;
            if !py_eq(attr_get(matches[0], field)?, wanted) || !py_eq(attr_get(titles[0], field)?, wanted) {
                return Err(rejected(&format!("Release changed qualified input coverage: {}", py_str(identity_id))));
            }
        }
    }
    Ok(())
}
