// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `scene_lights.Model`: the parts of a version-6 CMB the catalog needs.
//! Textures (name, format, base level), materials (texture indices,
//! blending, first texture coordinate transform) and per mesh its material
//! and rigid triangles in model space (world space for rooms); skinned
//! primitives are left out.

use std::collections::HashMap;
use std::rc::Rc;

use crate::pymath::{self, NotFinite};
use crate::water::{base_level_bytes, bits_per_pixel, texture_name};

/// How analysing a model failed. `Skip` is what Python caught (ValueError,
/// struct.error, ArithmeticError): the model adds no lights. `Fatal` is an
/// exception Python did not catch, which ended the whole catalog build.
#[derive(Debug)]
pub enum Fault {
    Skip,
    Fatal(&'static str),
}

impl From<NotFinite> for Fault {
    fn from(_: NotFinite) -> Self {
        Fault::Skip
    }
}

pub type ModelResult<T> = Result<T, Fault>;
pub type Point = [f64; 3];
/// Three corners: (vertex index, model-space position).
pub type Triangle = [(u32, Point); 3];
type Matrix = [[f64; 4]; 3];

const GL_ONE: u16 = 0x0001;
const MATERIAL_SIZE: u64 = 0x15C;
const PICA_U8: u16 = 0x1401;
const PICA_U16: u16 = 0x1403;
const PICA_U32: u16 = 0x1405;

#[derive(Clone, Copy)]
enum Scalar {
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    F32,
}

impl Scalar {
    fn of(kind: u16) -> Option<Self> {
        Some(match kind {
            0x1400 => Self::I8,
            0x1401 => Self::U8,
            0x1402 => Self::I16,
            0x1403 => Self::U16,
            0x1404 => Self::I32,
            0x1405 => Self::U32,
            0x1406 => Self::F32,
            _ => return None,
        })
    }

    fn size(self) -> u64 {
        match self {
            Self::I8 | Self::U8 => 1,
            Self::I16 | Self::U16 => 2,
            Self::I32 | Self::U32 | Self::F32 => 4,
        }
    }

    fn read(self, bytes: &[u8]) -> f64 {
        match self {
            Self::I8 => f64::from(bytes[0] as i8),
            Self::U8 => f64::from(bytes[0]),
            Self::I16 => f64::from(i16::from_le_bytes([bytes[0], bytes[1]])),
            Self::U16 => f64::from(u16::from_le_bytes([bytes[0], bytes[1]])),
            Self::I32 => f64::from(i32::from_le_bytes(bytes[..4].try_into().expect("four bytes"))),
            Self::U32 => f64::from(u32::from_le_bytes(bytes[..4].try_into().expect("four bytes"))),
            Self::F32 => f64::from(f32::from_le_bytes(bytes[..4].try_into().expect("four bytes"))),
        }
    }
}

pub struct Texture<'a> {
    pub name: String,
    pub width: u16,
    pub height: u16,
    pub format: u16,
    pub kind: u16,
    pub pixels: &'a [u8],
}

pub struct Material {
    pub texture_indices: [i16; 3],
    pub additive: bool,
    /// The first texture coordinate's scale and translation (su, sv, tu, tv).
    pub uv_transform: [f64; 4],
}

/// A shape's visible rigid triangles and the texture coordinates at each
/// triangle's centre.
pub struct Shape {
    pub triangles: Vec<Triangle>,
    pub uvs: Vec<(f64, f64)>,
}

pub struct Model<'a> {
    data: &'a [u8],
    pub textures: Vec<Texture<'a>>,
    pub materials: Vec<Material>,
    /// Per mesh: (shape, material).
    pub meshes: Vec<(u16, u8)>,
    shape_count: u32,
    bones: Vec<Matrix>,
    shp: u64,
    vatr: u64,
    indices: u64,
    index_count: u32,
    shapes: HashMap<u16, Rc<Shape>>,
    centre: Option<Point>,
}

/// Bounds-checked little-endian reads (`Model._unpack`).
struct Reader<'a>(&'a [u8]);

impl Reader<'_> {
    fn bytes(&self, offset: u64, size: u64) -> ModelResult<&[u8]> {
        let end = offset.checked_add(size).ok_or(Fault::Skip)?;
        if end > self.0.len() as u64 {
            return Err(Fault::Skip);
        }
        Ok(&self.0[offset as usize..end as usize])
    }

    fn u8(&self, offset: u64) -> ModelResult<u8> {
        Ok(self.bytes(offset, 1)?[0])
    }

    fn u16(&self, offset: u64) -> ModelResult<u16> {
        let b = self.bytes(offset, 2)?;
        Ok(u16::from_le_bytes([b[0], b[1]]))
    }

    fn i16(&self, offset: u64) -> ModelResult<i16> {
        Ok(self.u16(offset)? as i16)
    }

    fn u32(&self, offset: u64) -> ModelResult<u32> {
        Ok(u32::from_le_bytes(self.bytes(offset, 4)?.try_into().expect("four bytes")))
    }

    fn f32(&self, offset: u64) -> ModelResult<f64> {
        Ok(f64::from(f32::from_le_bytes(self.bytes(offset, 4)?.try_into().expect("four bytes"))))
    }

    fn magic(&self, offset: u64, magic: &[u8; 4]) -> bool {
        self.bytes(offset, 4).is_ok_and(|bytes| bytes == magic)
    }
}

/// Python's `d[start:start + size] if start + size <= len(d) else b""`.
fn stored_slice(data: &[u8], start: u64, size: u64) -> &[u8] {
    match start.checked_add(size) {
        Some(end) if end <= data.len() as u64 => &data[start as usize..end as usize],
        _ => &[],
    }
}

impl<'a> Model<'a> {
    pub fn new(data: &'a [u8], offset: usize) -> ModelResult<Self> {
        let whole = Reader(data);
        if !whole.magic(offset as u64, b"cmb ") || offset + 0x44 > data.len() {
            return Err(Fault::Skip);
        }
        let size = whole.u32(offset as u64 + 4)? as usize;
        let version = whole.u32(offset as u64 + 8)?;
        if version != 6 || size < 0x44 || offset + size > data.len() {
            return Err(Fault::Skip);
        }
        let d = &data[offset..offset + size];
        let r = Reader(d);
        let header: Vec<u64> = (0..9).map(|i| r.u32(0x20 + i * 4).map(u64::from)).collect::<ModelResult<_>>()?;
        let (index_count, skl, mats, tex, sklm, vatr, indices, texture_data) =
            (header[0] as u32, header[1], header[2], header[3], header[4], header[6], header[7], header[8]);
        for (chunk, magic) in [(skl, b"skl "), (mats, b"mats"), (tex, b"tex "), (sklm, b"sklm"), (vatr, b"vatr")] {
            if !r.magic(chunk, magic) {
                return Err(Fault::Skip);
            }
        }
        let mut textures = Vec::new();
        for i in 0..u64::from(r.u32(tex + 8)?) {
            let entry = tex + 0x0C + i * 0x24;
            r.bytes(entry, 20)?;
            let data_size = u64::from(r.u32(entry)?);
            let (width, height) = (r.u16(entry + 8)?, r.u16(entry + 10)?);
            let (format, kind) = (r.u16(entry + 12)?, r.u16(entry + 14)?);
            let relative = u64::from(r.u32(entry + 16)?);
            let name_start = (entry + 0x14).min(d.len() as u64) as usize;
            let name_end = (entry + 0x24).min(d.len() as u64) as usize;
            let name = texture_name(&d[name_start..name_end]);
            let base = bits_per_pixel(format, kind).map_or(0, |bits| base_level_bytes(width, height, bits) as u64);
            let pixels = stored_slice(d, texture_data + relative, base.min(data_size));
            textures.push(Texture { name, width, height, format, kind, pixels });
        }
        let mut materials = Vec::new();
        for i in 0..u64::from(r.u32(mats + 8)?) {
            let base = mats + 0x0C + i * MATERIAL_SIZE;
            let mut texture_indices = [0i16; 3];
            for (k, slot) in texture_indices.iter_mut().enumerate() {
                *slot = r.i16(base + 0x10 + k as u64 * 0x18)?;
            }
            let blend_mode = r.u32(base + 0x138)?;
            let blend_dst = r.u16(base + 0x13E)?;
            r.bytes(base + 0x5C, 20)?;
            let (su, sv) = (r.f32(base + 0x5C)?, r.f32(base + 0x60)?);
            let (tu, tv) = (r.f32(base + 0x68)?, r.f32(base + 0x6C)?);
            // `su or 1.0`: zero (either sign) means unscaled; NaN stays.
            let or_one = |value: f64| if value == 0.0 { 1.0 } else { value };
            materials.push(Material {
                texture_indices,
                additive: blend_mode != 0 && blend_dst == GL_ONE,
                uv_transform: [or_one(su), or_one(sv), tu, tv],
            });
        }
        let mshs = sklm + u64::from(r.u32(sklm + 8)?);
        let shp = sklm + u64::from(r.u32(sklm + 0x0C)?);
        if !r.magic(mshs, b"mshs") || !r.magic(shp, b"shp ") {
            return Err(Fault::Skip);
        }
        let mut meshes = Vec::new();
        for i in 0..u64::from(r.u32(mshs + 8)?) {
            let entry = mshs + 0x10 + i * 4;
            r.bytes(entry, 3)?;
            meshes.push((r.u16(entry)?, r.u8(entry + 2)?));
        }
        let shape_count = r.u32(shp + 8)?;
        let bones = bone_transforms(&r, skl)?;
        Ok(Self {
            data: d,
            textures,
            materials,
            meshes,
            shape_count,
            bones,
            shp,
            vatr,
            indices,
            index_count,
            shapes: HashMap::new(),
            centre: None,
        })
    }

    /// `triangles(shape)` and `triangle_uvs(shape)`, cached once read.
    pub fn shape(&mut self, shape: u16) -> ModelResult<Rc<Shape>> {
        if let Some(cached) = self.shapes.get(&shape) {
            return Ok(Rc::clone(cached));
        }
        if u32::from(shape) >= self.shape_count {
            return Err(Fault::Skip);
        }
        let read = Rc::new(self.read_shape(shape)?);
        self.shapes.insert(shape, Rc::clone(&read));
        Ok(read)
    }

    fn read_shape(&self, shape: u16) -> ModelResult<Shape> {
        let r = Reader(self.data);
        let sepd = self.shp + u64::from(r.u16(self.shp + 0x10 + u64::from(shape) * 2)?);
        if !r.magic(sepd, b"sepd") {
            return Err(Fault::Skip);
        }
        let primitive_count = r.u16(sepd + 8)?;
        let flags = r.u16(sepd + 10)?;
        let auto_flags = r.u16(sepd + 0x106)?;
        r.bytes(sepd + 0x24, 12)?;
        let (offset, scale) = (u64::from(r.u32(sepd + 0x24)?), r.f32(sepd + 0x28)?);
        let (kind, mode) = (r.u16(sepd + 0x2C)?, r.u16(sepd + 0x2E)?);
        r.bytes(sepd + 0x30, 12)?;
        let constant = [r.f32(sepd + 0x30)?, r.f32(sepd + 0x34)?, r.f32(sepd + 0x38)?];
        r.bytes(self.vatr + 0x0C, 8)?;
        let (length, start) = (u64::from(r.u32(self.vatr + 0x0C)?), u64::from(r.u32(self.vatr + 0x10)?));
        let stream = self.vatr + start + offset;
        let fixed = flags & 1 == 0 || auto_flags & 1 != 0 || mode != 0;
        let code = Scalar::of(kind);
        let Some(code) = code.filter(|_| scale.is_finite() && constant.iter().all(|c| c.is_finite())) else {
            return Err(Fault::Skip);
        };
        let width = code.size() * 3;
        // The first texture coordinates (flag 8), for lamps inside atlases.
        r.bytes(sepd + 0x78, 12)?;
        let (uv_offset, uv_scale) = (u64::from(r.u32(sepd + 0x78)?), r.f32(sepd + 0x7C)?);
        let (uv_kind, uv_mode) = (r.u16(sepd + 0x80)?, r.u16(sepd + 0x82)?);
        r.bytes(sepd + 0x84, 8)?;
        let uv_constant = (r.f32(sepd + 0x84)?, r.f32(sepd + 0x88)?);
        r.bytes(self.vatr + 0x24, 8)?;
        let (uv_length, uv_start) = (u64::from(r.u32(self.vatr + 0x24)?), u64::from(r.u32(self.vatr + 0x28)?));
        let uv_code = Scalar::of(uv_kind);
        let uv_fixed = flags & 8 == 0 || auto_flags & 8 != 0 || uv_mode != 0 || uv_code.is_none();
        let uv_width = uv_code.map_or(0, |code| code.size() * 2);

        let uv = |index: u64| -> ModelResult<(f64, f64)> {
            let code = match uv_code {
                Some(code) if !uv_fixed && uv_scale.is_finite() => code,
                _ => return Ok(uv_constant),
            };
            if uv_offset + (index + 1) * uv_width > uv_length {
                return Err(Fault::Skip);
            }
            let bytes = r.bytes(self.vatr + uv_start + uv_offset + index * uv_width, uv_width)?;
            let size = code.size() as usize;
            Ok((code.read(&bytes[..size]) * uv_scale, code.read(&bytes[size..]) * uv_scale))
        };
        let position = |index: u64| -> ModelResult<Point> {
            if fixed {
                return Ok(constant);
            }
            if offset + (index + 1) * width > length {
                return Err(Fault::Skip);
            }
            let bytes = r.bytes(stream + index * width, width)?;
            let size = code.size() as usize;
            Ok([0, 1, 2].map(|axis| code.read(&bytes[axis * size..]) * scale))
        };

        let mut triangles = Vec::new();
        let mut uvs = Vec::new();
        for p in 0..u64::from(primitive_count) {
            let prms = sepd + u64::from(r.u16(sepd + 0x108 + p * 2)?);
            if !r.magic(prms, b"prms") {
                return Err(Fault::Skip);
            }
            r.bytes(prms + 0x0C, 12)?;
            let (skinning, bone_count) = (r.u16(prms + 0x0C)?, r.u16(prms + 0x0E)?);
            let (bone_table, relative) = (u64::from(r.u32(prms + 0x10)?), u64::from(r.u32(prms + 0x14)?));
            let prm = prms + relative;
            if !r.magic(prm, b"prm ") {
                return Err(Fault::Skip);
            }
            r.bytes(prm + 8, 16)?;
            let (visible, primitive_mode) = (r.u32(prm + 8)?, r.u32(prm + 12)?);
            let index_type = r.u16(prm + 16)?;
            let (count, first) = (u64::from(r.u16(prm + 20)?), u64::from(r.u16(prm + 22)?));
            let index_code = match index_type {
                PICA_U8 => Some(Scalar::U8),
                PICA_U16 => Some(Scalar::U16),
                PICA_U32 => Some(Scalar::U32),
                _ => None,
            };
            let Some(index_code) = index_code.filter(|_| visible != 0 && primitive_mode == 0) else { continue };
            // Skinned primitives move with their bones at run time: left out.
            if skinning != 0 || bone_count != 1 {
                continue;
            }
            let bone = usize::from(r.u16(prms + bone_table)?);
            let m = *self.bones.get(bone).ok_or(Fault::Skip)?;
            // The first index is counted in 16-bit units whatever the index
            // type, and the indices stay within the model's index buffer.
            if first * 2 + count * index_code.size() > u64::from(self.index_count) * 2 {
                return Err(Fault::Skip);
            }
            let index_bytes = r.bytes(self.indices + first * 2, count * index_code.size())?;
            let step = index_code.size() as usize;
            let values: Vec<u32> = index_bytes.chunks_exact(step).map(|bytes| index_code.read(bytes) as u32).collect();
            let mut i = 0;
            while i + 2 < values.len() {
                let mut corners = [(0u32, [0.0; 3]); 3];
                for (k, corner) in corners.iter_mut().enumerate() {
                    let [x, y, z] = position(u64::from(values[i + k]))?;
                    let placed = [0, 1, 2].map(|row| m[row][0] * x + m[row][1] * y + m[row][2] * z + m[row][3]);
                    if !placed.iter().all(|v| v.is_finite()) {
                        return Err(Fault::Skip);
                    }
                    *corner = (values[i + k], placed);
                }
                triangles.push(corners);
                let coordinates = [uv(u64::from(values[i]))?, uv(u64::from(values[i + 1]))?, uv(u64::from(values[i + 2]))?];
                uvs.push((
                    pymath::sum(coordinates.iter().map(|c| c.0)) / 3.0,
                    pymath::sum(coordinates.iter().map(|c| c.1)) / 3.0,
                ));
                i += 3;
            }
        }
        Ok(Shape { triangles, uvs })
    }

    /// `model_centre`: the centre of the bounding box of every readable
    /// mesh shape (the room, for a room model), cached.
    pub fn centre(&mut self) -> Point {
        if let Some(centre) = self.centre {
            return centre;
        }
        let mut points = Vec::new();
        for index in 0..self.meshes.len() {
            let shape = self.meshes[index].0;
            if let Ok(read) = self.shape(shape) {
                points.extend(read.triangles.iter().flat_map(|tri| tri.iter().map(|corner| corner.1)));
            }
        }
        let centre = if points.is_empty() {
            [0.0; 3]
        } else {
            [0, 1, 2].map(|k| {
                (pymath::min(points.iter().map(|p| p[k])) + pymath::max(points.iter().map(|p| p[k]))) / 2.0
            })
        };
        self.centre = Some(centre);
        centre
    }
}

/// Each bone's bind transform to model space (3x4 rows): scale, then
/// rotations about X, Y and Z, then translation, under its parent's.
fn bone_transforms(r: &Reader<'_>, skl: u64) -> ModelResult<Vec<Matrix>> {
    let count = r.u32(skl + 8)? as usize;
    let mut bones = Vec::new();
    for i in 0..count as u64 {
        let entry = skl + 0x10 + i * 0x28;
        r.bytes(entry, 0x28)?;
        let parent = r.i16(entry + 2)?;
        let mut values = [0.0; 9];
        for (k, value) in values.iter_mut().enumerate() {
            *value = r.f32(entry + 4 + k as u64 * 4)?;
        }
        bones.push((parent, values));
    }
    let mut local = Vec::with_capacity(count);
    for (_, [sx, sy, sz, rx, ry, rz, tx, ty, tz]) in &bones {
        let (cx, snx) = (pymath::cos(*rx)?, pymath::sin(*rx)?);
        let (cy, sny) = (pymath::cos(*ry)?, pymath::sin(*ry)?);
        let (cz, snz) = (pymath::cos(*rz)?, pymath::sin(*rz)?);
        // Rz * Ry * Rx, scaled per column.
        let rotation = [
            [cz * cy, cz * sny * snx - snz * cx, cz * sny * cx + snz * snx],
            [snz * cy, snz * sny * snx + cz * cx, snz * sny * cx - cz * snx],
            [-sny, cy * snx, cy * cx],
        ];
        let translation = [*tx, *ty, *tz];
        local.push([0, 1, 2].map(|i| [rotation[i][0] * sx, rotation[i][1] * sy, rotation[i][2] * sz, translation[i]]));
    }
    let mut world: Vec<Option<Matrix>> = vec![None; count];
    for index in 0..count {
        // The chain up to a resolved bone or the root, then down it.
        let mut chain = Vec::new();
        let mut current = index as i64;
        while current >= 0 {
            // Python indexed `world[current]` before its own range check: a
            // parent past the end raised IndexError, which nothing caught.
            let slot = world.get(current as usize).ok_or(Fault::Fatal("CMB bone parent index out of range"))?;
            if slot.is_some() {
                break;
            }
            if chain.len() > count {
                return Err(Fault::Skip);
            }
            chain.push(current as usize);
            current = i64::from(bones[current as usize].0);
        }
        for &bone in chain.iter().rev() {
            let parent = bones[bone].0;
            if parent < 0 {
                world[bone] = Some(local[bone]);
                continue;
            }
            let a = world[parent as usize].expect("parents resolve first");
            let b = local[bone];
            world[bone] = Some([0, 1, 2].map(|i| {
                [0, 1, 2, 3].map(|j| {
                    pymath::sum((0..3).map(|k| a[i][k] * b[k][j])) + if j == 3 { a[i][3] } else { 0.0 }
                })
            }));
        }
    }
    Ok(world.into_iter().map(|bone| bone.expect("every bone resolved")).collect())
}
