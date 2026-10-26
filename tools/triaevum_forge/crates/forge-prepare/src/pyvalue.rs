// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Python semantics for JSON values the legacy Forge compared or stringified
//! (shared with the other crates through [`forge_base::pyvalue`]).

use serde_json::{Map, Value};

pub use forge_base::pyvalue::{as_python_int, get_eq, objects_eq, py_eq, py_str};

/// `str(d.get(key, ""))`.
pub fn get_str(map: &Map<String, Value>, key: &str) -> String {
    map.get(key).map(py_str).unwrap_or_default()
}
