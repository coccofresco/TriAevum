// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Derived local assets: TopScreen, fonts, water textures, scene lights,
//! precomputed tables and the Forge side of the interior sun.
//!
//! Port of the Python Forge's `topscreen_assets.py`,
//! `build_topscreen_texture_override_pack.py`, `prepare_qbf_font_pack.py`,
//! `qbf_font.py`, `water_textures.py`, `scene_lights.py`,
//! `precomputed_tables.py` and `interior_sun.py`, byte-compatible per
//! FORGE_SPEC.md section 3.1: the same inputs give the same O3TU, font ZIP,
//! water catalog and scene-light JSON bytes.

mod catalog_cache;
mod fnv;
mod pymath;
mod pyvalue;
mod romfs;
mod zip;

pub mod font_pack;
pub mod interior_sun;
pub mod precomputed_tables;
pub mod qbf;
pub mod scene_lights;
pub mod texture_pack;
pub mod topscreen;
pub mod water;

pub use fnv::{content_hash, fnv1a64};

/// Progress callback: `(stage, message)`, as the Python `report` argument.
pub type Report<'a> = &'a mut dyn FnMut(&str, &str);
