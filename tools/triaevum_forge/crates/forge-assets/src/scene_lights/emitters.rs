// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Classification and emitters: a room model's emitting materials become
//! objects (connected triangles, merged when close), each object a point
//! light (lava: area clusters), merged and capped per category per room.

use std::collections::HashMap;

use forge_base::{ForgeError, Result};
use serde_json::{json, Number, Value};

use super::model::{Fault, Model, ModelResult, Point};
use super::texels::{is_halo, texture_colour};
use crate::fnv::content_hash;
use crate::pymath::{self, max2, min2, round_digits, round_int};
use crate::water::cmb_offsets;

/// Categories in `MERGE` order with their merge distance, per-room cap,
/// gain (calibrated against the title's own lighting) and largest reach.
const CATEGORIES: [(&str, f64, usize, f64, f64); 6] = [
    ("window-lamp", 300.0, 12, 0.45, 700.0),
    ("magic-glow", 300.0, 8, 0.6, 500.0),
    ("fire-lava", 500.0, 12, 0.45, 1000.0),
    ("light-shaft", 200.0, 8, 0.55, 600.0),
    ("lamp", 60.0, 16, 0.5, 500.0),
    ("doorway", 150.0, 8, 1.6, 1800.0),
];
/// A merged light names at most this many surfaces.
pub const MAX_SURFACES: usize = 2;
const DAYLIGHT: [f64; 3] = [1.0, 0.95, 0.85];
const DOORWAY_HEIGHT: (f64, f64) = (90.0, 400.0);
const DOORWAY_WIDTH: f64 = 500.0;
const FIRE_COLOUR: [f64; 3] = [1.0, 0.55, 0.22];
const LAMP_TINT: [f64; 3] = [1.0, 0.78, 0.5];

pub fn is_category(name: &str) -> bool {
    CATEGORIES.iter().any(|entry| entry.0 == name)
}

fn category_entry(name: &str) -> (&'static str, f64, usize, f64, f64) {
    *CATEGORIES.iter().find(|entry| entry.0 == name).expect("a known category")
}

fn gain(name: &str) -> f64 {
    category_entry(name).3
}

fn reach(name: &str) -> f64 {
    category_entry(name).4
}

/// Lamps drawn inside a shared atlas: boxes (u0, v0, u1, v1, colour) in the
/// material's transformed first texture coordinates (wrapped into 0..1).
fn lamp_regions(name: &str) -> Option<&'static [([f64; 4], [f64; 3])]> {
    Some(match name {
        "rm_ten_obj_02" => &[([0.750, 0.550, 0.875, 0.815], [1.0, 0.84, 0.52])],
        "tar_rm_kana_01" => &[([0.500, 0.000, 0.750, 0.285], [1.0, 0.80, 0.45])],
        "kok_rm_obj_04" => &[([0.0, 0.508, 1.0, 1.0], [1.0, 0.55, 0.22])],
        _ => return None,
    })
}

/// Lamp bodies drawn without a glow card, with their light's colour.
fn lamp_body(name: &str) -> Option<[f64; 3]> {
    Some(match name {
        "shop_any_lant_0" => [1.0, 0.8, 0.5],
        "rm_shat_torc_01" => [1.0, 0.8, 0.45],
        "shop_bomchu_bin" => [1.0, 0.75, 0.55],
        "turi_rantan_lig" => [1.0, 0.82, 0.5],
        _ => return None,
    })
}

/// `re.search` for `fir\d`.
fn has_fir_digit(text: &str) -> bool {
    text.match_indices("fir").any(|(at, _)| text[at + 3..].starts_with(|c: char| c.is_ascii_digit()))
}

/// The fire scenes' additive glow overlays, `re.search` of
/// `^(dg05_.*_al|dg_dod_.*t|dg_g_kabe_.*_t|gc_kabe_\d+t|gcp_kabe_\d+_t)$`:
/// a whole match, where `$` may also stand before a final newline.
fn is_fire_overlay(name: &str) -> bool {
    fn whole(t: &str) -> bool {
        if t.contains('\n') {
            return false;
        }
        let wild = |prefix: &str, suffix: &str| {
            t.len() >= prefix.len() + suffix.len() && t.starts_with(prefix) && t.ends_with(suffix)
        };
        let digits = |prefix: &str, suffix: &str| {
            t.strip_prefix(prefix)
                .and_then(|rest| rest.strip_suffix(suffix))
                .is_some_and(|middle| !middle.is_empty() && middle.bytes().all(|b| b.is_ascii_digit()))
        };
        wild("dg05_", "_al")
            || wild("dg_dod_", "t")
            || wild("dg_g_kabe_", "_t")
            || digits("gc_kabe_", "t")
            || digits("gcp_kabe_", "_t")
    }
    whole(name) || name.strip_suffix('\n').is_some_and(whole)
}

/// `category`: the emitting category of a material, from its first texture.
pub fn category(first_texture: &str, additive: bool, halo: bool) -> Option<&'static str> {
    if (additive && halo) || lamp_body(first_texture).is_some() || lamp_regions(first_texture).is_some() {
        return Some("lamp");
    }
    let first = first_texture.to_lowercase();
    let any = |stems: &[&str]| stems.iter().any(|stem| first.contains(stem));
    if any(&["doughnut", "kja_", "mir_decal"]) || first.starts_with("ido_") {
        return None;
    }
    // A torch stand is not a light: the flame on it is (an actor's).
    if any(&["shokudai", "shoku_", "syoku", "torch", "taimatsu", "rousoku", "candle"]) {
        return None;
    }
    if any(&["mado", "window", "lamp", "neon"]) {
        return Some("window-lamp");
    }
    if any(&["mark_lt", "mark_tam", "maho_star"]) {
        return Some("magic-glow");
    }
    if any(&["hikari", "light"]) {
        return Some("light-shaft");
    }
    if any(&["fire", "mgm", "yougan", "magma", "lava", "hinoko", "hono"]) || has_fir_digit(&first) {
        return Some("fire-lava");
    }
    if additive && is_fire_overlay(&first) {
        return Some("fire-lava");
    }
    None
}

/// A plain white texture: the glow the rooms fill their openings with.
fn is_daylight(colour: Option<[f64; 3]>) -> bool {
    colour.is_some_and(|c| pymath::sum(c) / 3.0 >= 0.95 && pymath::max(c) - pymath::min(c) <= 0.02)
}

#[derive(Clone, Debug)]
pub struct Emitter {
    pub category: &'static str,
    pub position: Point,
    pub radius: i64,
    pub gain: f64,
    pub colour: [f64; 3],
    pub weight: f64,
    pub texture: String,
    pub surfaces: Vec<String>,
}

impl Emitter {
    fn new(position: Point, radius: i64, gain: f64, colour: [f64; 3], weight: f64) -> Self {
        Self { category: "", position, radius, gain, colour, weight, texture: String::new(), surfaces: Vec::new() }
    }

    fn named(mut self, category: &'static str, texture: &str, surface: &str) -> Self {
        self.category = category;
        self.texture = texture.to_string();
        self.surfaces = vec![surface.to_string()];
        self
    }

    fn finite(&self) -> bool {
        self.position.iter().chain(&self.colour).chain([&self.gain, &self.weight]).all(|v| v.is_finite())
    }
}

/// `doorway_emitter`: a white additive volume shaped like an opening lets
/// daylight in near its outer end, the end away from the room's centre.
fn doorway_emitter(low: Point, high: Point, centre: Point) -> ModelResult<Option<Emitter>> {
    let size = [0, 1, 2].map(|k| high[k] - low[k]);
    if !(DOORWAY_HEIGHT.0 <= size[1] && size[1] <= DOORWAY_HEIGHT.1)
        || max2(size[0], size[2]) > DOORWAY_WIDTH
        || pymath::min(size) < 25.0
    {
        return Ok(None);
    }
    let middle = [0, 1, 2].map(|k| (low[k] + high[k]) / 2.0);
    let key = |k: usize| (middle[k] - centre[k]).abs() / max2(size[k], 1.0);
    let axis = if key(2) > key(0) { 2 } else { 0 };
    let mut position = middle;
    position[axis] += (size[axis] * 0.35).copysign(middle[axis] - centre[axis]);
    position[1] = low[1] + 0.6 * size[1];
    let reach_size = pymath::max([size[1], size[0], size[2]]) * 7.0;
    let radius = round_int(min2(reach("doorway"), max2(900.0, reach_size)))?;
    Ok(Some(Emitter::new(position, radius, gain("doorway"), DAYLIGHT, size[0] * size[1] * size[2])))
}

/// A triangle tagged with its (shape, vertex) corner keys.
type Tagged = [((u16, u32), Point); 3];
type Corners = [Point; 3];
/// `[low corner, high corner, triangles]`.
type ObjectBox = (Point, Point, Vec<Corners>);

/// `objects`: connected triangle sets (shared vertices), merged when their
/// bounding boxes come within `merge` units.
fn objects(triangles: &[Tagged], merge: f64) -> Vec<ObjectBox> {
    if triangles.is_empty() {
        return Vec::new();
    }
    let mut parent: Vec<usize> = (0..triangles.len()).collect();
    fn find(parent: &mut [usize], mut a: usize) -> usize {
        while parent[a] != a {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        a
    }
    let mut owner: HashMap<(u16, u32), usize> = HashMap::new();
    for (t, triangle) in triangles.iter().enumerate() {
        for (key, _) in triangle {
            match owner.get(key) {
                Some(&first) => {
                    let root = find(&mut parent, first);
                    let own = find(&mut parent, t);
                    parent[own] = root;
                }
                None => {
                    owner.insert(*key, t);
                }
            }
        }
    }
    let mut group_of: HashMap<usize, usize> = HashMap::new();
    let mut groups: Vec<Vec<Corners>> = Vec::new();
    for (t, triangle) in triangles.iter().enumerate() {
        let root = find(&mut parent, t);
        let index = *group_of.entry(root).or_insert_with(|| {
            groups.push(Vec::new());
            groups.len() - 1
        });
        groups[index].push(triangle.map(|corner| corner.1));
    }
    let mut boxes: Vec<ObjectBox> = groups
        .into_iter()
        .map(|tris| {
            let low = [0, 1, 2].map(|k| pymath::min(tris.iter().flat_map(|tri| tri.iter().map(move |p| p[k]))));
            let high = [0, 1, 2].map(|k| pymath::max(tris.iter().flat_map(|tri| tri.iter().map(move |p| p[k]))));
            (low, high, tris)
        })
        .collect();
    'merging: loop {
        for i in 0..boxes.len() {
            for j in i + 1..boxes.len() {
                let (a, b) = (&boxes[i], &boxes[j]);
                if (0..3).all(|k| a.0[k] - merge <= b.1[k] && b.0[k] - merge <= a.1[k]) {
                    let removed = boxes.remove(j);
                    let a = &mut boxes[i];
                    a.0 = [0, 1, 2].map(|k| min2(a.0[k], removed.0[k]));
                    a.1 = [0, 1, 2].map(|k| max2(a.1[k], removed.1[k]));
                    a.2.extend(removed.2);
                    continue 'merging;
                }
            }
        }
        break;
    }
    boxes
}

fn area_normal(tris: &[Corners]) -> (Point, f64) {
    let mut n = [0.0f64; 3];
    let mut area = 0.0f64;
    for [a, b, c] in tris {
        let u = [0, 1, 2].map(|k| b[k] - a[k]);
        let v = [0, 1, 2].map(|k| c[k] - a[k]);
        let cross = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
        n = [0, 1, 2].map(|k| n[k] + cross[k]);
        area += 0.5 * pymath::sum(cross.iter().map(|c| c * c)).sqrt();
    }
    let length = or_one(pymath::sum(n.iter().map(|c| c * c)).sqrt());
    (n.map(|c| c / length), area)
}

/// `value or 1.0` for a float.
fn or_one(value: f64) -> f64 {
    if value == 0.0 {
        1.0
    } else {
        value
    }
}

/// A stable descending sort by a float key, as `sort(key=lambda e: -k(e))`.
/// Keys are compared after normalizing -0.0, so equal keys keep their order.
fn sort_descending<T>(items: &mut [T], key: impl Fn(&T) -> f64) {
    items.sort_by(|a, b| (-key(a) + 0.0).total_cmp(&(-key(b) + 0.0)));
}

fn emitters_for(category: &'static str, colour: [f64; 3], boxes: &[ObjectBox]) -> ModelResult<Vec<Emitter>> {
    let mut out = Vec::new();
    let gain = gain(category);
    if category == "fire-lava" {
        // 600-unit cells over the triangles' centres weighted by area, the
        // largest first; raised off the surface along its facing.
        let mut index: HashMap<[u64; 3], usize> = HashMap::new();
        let mut cells: Vec<(f64, Point, Point)> = Vec::new();
        for (_, _, tris) in boxes {
            for tri in tris {
                let (normal, area) = area_normal(std::slice::from_ref(tri));
                let middle = [0, 1, 2].map(|k| pymath::sum(tri.iter().map(|p| p[k])) / 3.0);
                let mut key = [0u64; 3];
                for k in 0..3 {
                    let cell = pymath::floor_div(middle[k], 600.0);
                    if !cell.is_finite() {
                        return Err(Fault::Skip);
                    }
                    key[k] = (cell + 0.0).to_bits();
                }
                let slot = *index.entry(key).or_insert_with(|| {
                    cells.push((0.0, [0.0; 3], [0.0; 3]));
                    cells.len() - 1
                });
                let cell = &mut cells[slot];
                cell.0 += area;
                cell.1 = [0, 1, 2].map(|k| cell.1[k] + middle[k] * area);
                cell.2 = [0, 1, 2].map(|k| cell.2[k] + normal[k] * area);
            }
        }
        sort_descending(&mut cells, |cell| cell.0);
        for (area, sum_position, sum_normal) in cells.into_iter().take(8) {
            if area < 2500.0 {
                continue;
            }
            let p = sum_position.map(|v| v / area);
            let length = or_one(pymath::sum(sum_normal.iter().map(|v| v * v)).sqrt());
            let normal = sum_normal.map(|v| v / length);
            let p = if normal[1].abs() < 0.5 {
                [0, 1, 2].map(|k| p[k] + normal[k] * 60.0)
            } else {
                [p[0], p[1] + 60.0, p[2]]
            };
            let radius = round_int(min2(900.0, 300.0 + area.sqrt() * 0.8))?;
            out.push(Emitter::new(p, radius, gain, colour, area));
        }
        return Ok(out);
    }
    for (low, high, tris) in boxes {
        let size = pymath::max((0..3).map(|k| high[k] - low[k]));
        let middle = [0, 1, 2].map(|k| (low[k] + high[k]) / 2.0);
        let (normal, area) = area_normal(tris);
        match category {
            // A halo is centred on its lamp; a lamp body is the lamp.
            "lamp" => {
                let radius = round_int(min2(reach(category), max2(250.0, size * 2.5)))?;
                out.push(Emitter::new(middle, radius, gain, colour, 1.0));
            }
            "window-lamp" => {
                let p = if area > 0.0 { [0, 1, 2].map(|k| middle[k] + normal[k] * 40.0) } else { middle };
                out.push(Emitter::new(p, round_int(min2(600.0, 250.0 + size * 0.6))?, gain, colour, 1.0));
            }
            "magic-glow" => {
                out.push(Emitter::new(middle, round_int(min2(500.0, 250.0 + size))?, gain, colour, 1.0));
            }
            "light-shaft" => {
                // Where the shaft lands, lighting the ground around it.
                if high[1] - low[1] < 60.0 {
                    continue;
                }
                let width = max2(high[0] - low[0], high[2] - low[2]);
                let radius = round_int(min2(600.0, max2(400.0, width * 1.5)))?;
                out.push(Emitter::new([middle[0], low[1] + 40.0, middle[2]], radius, gain, colour, 1.0));
            }
            _ => {}
        }
    }
    Ok(out)
}

/// `consolidate`: per category, emitters closer than its merge distance
/// become one (weighted position, reach grown to cover both); the
/// strongest are kept up to the category's cap.
fn consolidate(emitters: &[Emitter]) -> Result<Vec<Emitter>> {
    let mut kept = Vec::new();
    for (category, merge, cap, _, reach) in CATEGORIES {
        let mut group: Vec<Emitter> = emitters.iter().filter(|e| e.category == category).cloned().collect();
        'merging: loop {
            for i in 0..group.len() {
                for j in i + 1..group.len() {
                    let distance = pymath::dist(&group[i].position, &group[j].position);
                    if distance < merge {
                        let b = group.remove(j);
                        let a = &mut group[i];
                        let w = a.weight + b.weight;
                        a.position = [0, 1, 2].map(|k| (a.position[k] * a.weight + b.position[k] * b.weight) / w);
                        let grown = max2(a.radius as f64, b.radius as f64) + 0.5 * distance;
                        a.radius = round_int(min2(reach, grown))
                            .map_err(|_| ForgeError::internal("A merged scene light has no finite reach"))?;
                        a.weight = w;
                        // The merged light is the light of both emitters' surfaces.
                        for surface in b.surfaces {
                            if !a.surfaces.contains(&surface) && a.surfaces.len() < MAX_SURFACES {
                                a.surfaces.push(surface);
                            }
                        }
                        continue 'merging;
                    }
                }
            }
            break;
        }
        sort_descending(&mut group, |e| e.weight);
        group.truncate(cap);
        kept.extend(group);
    }
    Ok(kept)
}

/// `model_emitters`: the emitters of one model, before consolidation.
fn model_emitters(model: &mut Model<'_>) -> ModelResult<Vec<Emitter>> {
    let mut emitters = Vec::new();
    let mut shapes_by_material: Vec<(usize, Vec<u16>)> = Vec::new();
    for &(shape, material) in &model.meshes {
        let material = usize::from(material);
        if material < model.materials.len() {
            match shapes_by_material.iter_mut().find(|entry| entry.0 == material) {
                Some(entry) => entry.1.push(shape),
                None => shapes_by_material.push((material, vec![shape])),
            }
        }
    }
    // Per material: (first texture index, additive, category).
    let mut materials: HashMap<usize, (usize, bool, Option<&'static str>)> = HashMap::new();
    for (material, _) in &shapes_by_material {
        let entry = &model.materials[*material];
        let first =
            entry.texture_indices.iter().filter_map(|&i| usize::try_from(i).ok()).find(|&i| i < model.textures.len());
        if let Some(texture_index) = first {
            let t = &model.textures[texture_index];
            let halo = entry.additive && is_halo(t.format, t.kind, t.pixels, t.width, t.height);
            materials.insert(*material, (texture_index, entry.additive, category(&t.name, entry.additive, halo)));
        }
    }
    // Lit windows drawn as additive glow: the glow is the light, the panes glass.
    let window_glow = materials.values().any(|(_, additive, cat)| *cat == Some("window-lamp") && *additive);
    for (material, shapes) in &shapes_by_material {
        let Some(&(texture_index, additive, mut cat)) = materials.get(material) else { continue };
        if cat == Some("window-lamp") && !additive && window_glow {
            cat = None;
        }
        let (name, format, kind, pixels) = {
            let t = &model.textures[texture_index];
            (t.name.clone(), t.format, t.kind, t.pixels)
        };
        let surface = format!("{:016x}", content_hash(pixels));
        let raw = texture_colour(format, kind, pixels);
        let mut boxes = None;
        if additive && is_daylight(raw) {
            // Doorway-shaped white volumes let daylight in; the material's
            // other objects keep its named category.
            let mut triangles = Vec::new();
            for &shape in shapes {
                let read = model.shape(shape)?;
                triangles.extend(read.triangles.iter().map(|tri| tri.map(|(index, p)| ((shape, index), p))));
            }
            let mut rest = Vec::new();
            for (low, high, tris) in objects(&triangles, 10.0) {
                match doorway_emitter(low, high, model.centre())? {
                    None => rest.push((low, high, tris)),
                    Some(emitter) => emitters.push(emitter.named("doorway", &name, &surface)),
                }
            }
            boxes = Some(rest);
        }
        let Some(cat) = cat else { continue };
        // Fire found by loose name stems must look like fire.
        if cat == "fire-lava" && raw.is_some_and(|c| !(c[0] >= c[2] && pymath::max(c) > 0.25)) {
            continue;
        }
        let peak = raw.map_or(0.0, pymath::max);
        let mut colour = match raw {
            Some(raw) if peak > 0.0 => raw.map(|c| c / peak),
            _ => [1.0, 0.7, 0.4],
        };
        // Greyscale fire textures take their colour from the combiner.
        if cat == "fire-lava" && pymath::min(colour) > 0.85 {
            colour = FIRE_COLOUR;
        } else if cat == "window-lamp" {
            colour = [0, 1, 2].map(|k| colour[k] * LAMP_TINT[k]);
        } else if let Some(body) = lamp_body(&name).filter(|_| cat == "lamp") {
            colour = body;
        }
        // An atlas lamp is only its regions' triangles, each region its colour.
        let regions = if cat == "lamp" { lamp_regions(&name) } else { None };
        if let (Some(boxes), None) = (&boxes, regions) {
            for emitter in emitters_for(cat, colour, boxes)? {
                emitters.push(emitter.named(cat, &name, &surface));
            }
            continue;
        }
        let batches: Vec<([f64; 3], [f64; 4])> = match regions {
            None => vec![(colour, [0.0, 0.0, 1.0, 1.0])],
            Some(regions) => regions.iter().map(|(bounds, colour)| (*colour, *bounds)).collect(),
        };
        let [su, sv, tu, tv] = model.materials[*material].uv_transform;
        for (batch_colour, [u0, v0, u1, v1]) in batches {
            let mut triangles = Vec::new();
            for &shape in shapes {
                let read = model.shape(shape)?;
                for (tri, &(u, v)) in read.triangles.iter().zip(&read.uvs) {
                    if regions.is_some() {
                        let (u, v) = (pymath::modulo(u * su + tu, 1.0), pymath::modulo(v * sv + tv, 1.0));
                        if !(u0 <= u && u <= u1 && v0 <= v && v <= v1) {
                            continue;
                        }
                    }
                    triangles.push(tri.map(|(index, p)| ((shape, index), p)));
                }
            }
            // A lamp's crossed glow cards coincide and separate lamps may
            // hang close: halos group tightly, bodies loosely.
            let merge = if cat == "lamp" && additive { 10.0 } else { 40.0 };
            for emitter in emitters_for(cat, batch_colour, &objects(&triangles, merge))? {
                emitters.push(emitter.named(cat, &name, &surface));
            }
        }
    }
    Ok(emitters)
}

fn float(value: f64) -> Result<Value> {
    Number::from_f64(value)
        .map(Value::Number)
        .ok_or_else(|| ForgeError::invalid("Out of range float values are not JSON compliant"))
}

fn to_value(emitter: &Emitter) -> Result<Value> {
    let position = emitter.position.iter().map(|v| float(round_digits(*v, 1))).collect::<Result<Vec<_>>>()?;
    let colour = emitter.colour.iter().map(|v| float(round_digits(*v, 3))).collect::<Result<Vec<_>>>()?;
    Ok(json!({
        "position": position,
        "radius": emitter.radius,
        "gain": float(emitter.gain)?,
        "colour": colour,
        "category": emitter.category,
        "texture": emitter.texture,
        "surfaces": emitter.surfaces,
    }))
}

/// `room_emitters`: a room file's emitters in catalog form (rounded, no
/// weight). A model that does not read cleanly adds none.
pub fn room_emitters(data: &[u8]) -> Result<Vec<Value>> {
    let fatal = |message: &str| ForgeError::invalid(format!("Cannot analyse a room model: {message}"));
    let mut emitters = Vec::new();
    for offset in cmb_offsets(data) {
        let mut model = match Model::new(data, offset) {
            Ok(model) => model,
            Err(Fault::Skip) => continue,
            Err(Fault::Fatal(message)) => return Err(fatal(message)),
        };
        match model_emitters(&mut model) {
            Ok(found) if found.iter().all(Emitter::finite) => emitters.extend(found),
            Ok(_) | Err(Fault::Skip) => {}
            Err(Fault::Fatal(message)) => return Err(fatal(message)),
        }
    }
    consolidate(&emitters)?.iter().map(to_value).collect()
}

/// `room_fingerprint`: the room file's command list start (after the
/// 16-byte header), hashed with the renderer's content hash.
pub fn room_fingerprint(data: &[u8]) -> String {
    let start = 0x10.min(data.len());
    let end = (0x10 + 128).min(data.len());
    format!("{:016x}", content_hash(&data[start..end]))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn categories_follow_the_first_texture() {
        assert_eq!(category("gnn_torch_03", false, false), None);
        assert_eq!(category("s00_syoku_01", false, false), None);
        assert_eq!(category("spot01_mado", false, false), Some("window-lamp"));
        assert_eq!(category("dg05_kabe_al", true, false), Some("fire-lava"));
        assert_eq!(category("dg05_kabe_al", false, false), None);
        assert_eq!(category("ganon_kabe", false, false), None);
        assert_eq!(category("anything", true, true), Some("lamp"));
        assert_eq!(category("anything", false, true), None);
        assert_eq!(category("shop_any_lant_0", false, false), Some("lamp"));
        assert_eq!(category("xfir9", false, false), Some("fire-lava"));
        assert_eq!(category("gc_kabe_12t", true, false), Some("fire-lava"));
        assert_eq!(category("gc_kabe_t", true, false), None);
        assert_eq!(category("dg05__al\n", true, false), Some("fire-lava"));
        assert_eq!(category("ido_mark_lt", false, false), None);
    }

    #[test]
    fn objects_merge_close_boxes() {
        let tri = |x: f64, key: u32| -> Tagged {
            [((0, key), [x, 0.0, 0.0]), ((0, key + 1), [x + 1.0, 0.0, 0.0]), ((0, key + 2), [x, 1.0, 0.0])]
        };
        assert_eq!(objects(&[tri(0.0, 0), tri(5.0, 10)], 10.0).len(), 1);
        assert_eq!(objects(&[tri(0.0, 0), tri(500.0, 10)], 10.0).len(), 2);
    }
}
