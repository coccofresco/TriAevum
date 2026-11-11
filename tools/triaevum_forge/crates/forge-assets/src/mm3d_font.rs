// SPDX-License-Identifier: GPL-3.0-or-later
//! MM's native GZFX sheets are already Morton-tiled A4, not OoT QBF blits.
//! Keep every native advance/bearing/cell/UV/line metric. Only coverage of
//! verified codepoint cells is replaced; unsupported/aliased cells stay native.
use crate::{qbf::parse_qbf, zip::StoredZip};
use forge_base::hashing::sha256_hex;
use forge_base::{ForgeError, Result};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};
fn invalid() -> ForgeError {
    ForgeError::invalid("Invalid MM GZFX font contract")
}
fn u16at(b: &[u8], at: usize) -> Result<usize> {
    Ok(u16::from_le_bytes(b.get(at..at + 2).ok_or_else(invalid)?.try_into().unwrap()) as usize)
}
fn u32at(b: &[u8], at: usize) -> Result<usize> {
    Ok(u32::from_le_bytes(b.get(at..at + 4).ok_or_else(invalid)?.try_into().unwrap()) as usize)
}
fn pixel(x: usize, y: usize, w: usize) -> usize {
    let m = |v: usize| (v & 1) | ((v & 2) << 1) | ((v & 4) << 2);
    ((y / 8) * (w / 8) + x / 8) * 64 + m(x & 7) + 2 * m(y & 7)
}
fn read(b: &[u8], x: usize, y: usize, w: usize) -> u8 {
    let p = pixel(x, y, w);
    (b[p / 2] >> ((p % 2) * 4)) & 15
}
fn write(b: &mut [u8], x: usize, y: usize, w: usize, v: u8) {
    let p = pixel(x, y, w);
    let shift = (p % 2) * 4;
    b[p / 2] = (b[p / 2] & !(15 << shift)) | ((v & 15) << shift);
}
fn bounds(data: &[u8], w: usize, h: usize) -> Option<(usize, usize, usize, usize)> {
    let (mut x0, mut y0, mut x1, mut y1) = (w, h, 0, 0);
    for y in 0..h {
        for x in 0..w {
            if data[y * w + x] > 0 {
                x0 = x0.min(x);
                y0 = y0.min(y);
                x1 = x1.max(x + 1);
                y1 = y1.max(y + 1);
            }
        }
    }
    if x1 > x0 && y1 > y0 { Some((x0, y0, x1, y1)) } else { None }
}
/// Letters without descenders: their ink ends on the baseline.
const BASELINE_LETTERS: &str = "acemnorsuvwxz";
/// Flat-topped capitals: their ink spans the cap height.
const CAP_LETTERS: &str = "EFHILT";

/// The font-wide mapping from HD (QBF) pixels to coverage pixels of a native cell.
struct FontMetrics {
    /// Coverage pixels per HD pixel (cap height ratio).
    scale: f64,
    /// Baseline rows: in a native cell, in coverage pixels; in an HD cell, in HD pixels.
    native_baseline: f64,
    hd_baseline: f64,
}

fn median(mut values: Vec<usize>) -> Option<usize> {
    values.sort_unstable();
    values.get(values.len() / 2).copied()
}

fn native_cell(
    native: &[u8],
    header: usize,
    ps: usize,
    (page, cell): (usize, usize),
    (cw, ch): (usize, usize),
) -> Result<Vec<u8>> {
    let at = header + page * ps;
    let (offset, w, h) = (u32at(native, at)?, u16at(native, at + 4)?, u16at(native, at + 6)?);
    let source = native.get(offset..offset + w * h / 2).ok_or_else(invalid)?;
    let (left, top) = ((cell & 255) * cw, (cell >> 8) * ch);
    if w % 8 != 0 || left + cw > w || top + ch > h {
        return Err(invalid());
    }
    Ok((0..ch).flat_map(|y| (0..cw).map(move |x| (x, y))).map(|(x, y)| read(source, left + x, top + y, w)).collect())
}

/// Baseline and cap height of both fonts, measured from their own letters; None
/// when either font lacks the reference letters (then each glyph is fitted alone).
fn font_metrics(
    native: &[u8],
    header: usize,
    ps: usize,
    cell_size: (usize, usize),
    records: &BTreeMap<usize, (usize, usize)>,
    hd: &crate::qbf::QbfFont,
    hd_codes: &BTreeMap<usize, u16>,
    density: usize,
) -> Result<Option<FontMetrics>> {
    let (hw, hh) = (hd.width as usize, hd.height as usize);
    let ink = |letters: &str| -> Result<(Vec<(usize, usize)>, Vec<(usize, usize)>)> {
        let (mut n, mut h) = (Vec::new(), Vec::new());
        for code in letters.chars().map(|c| c as usize) {
            let (Some(record), Some(glyph)) = (records.get(&code), hd_codes.get(&code)) else { continue };
            let cell = native_cell(native, header, ps, *record, cell_size)?;
            let hd_glyph = hd.glyph_coverage(*glyph)?;
            if let (Some((_, y0, _, y1)), Some((_, hy0, _, hy1))) =
                (bounds(&cell, cell_size.0, cell_size.1), bounds(&hd_glyph, hw, hh))
            {
                n.push((y0, y1));
                h.push((hy0, hy1));
            }
        }
        Ok((n, h))
    };
    let (base_n, base_h) = ink(BASELINE_LETTERS)?;
    let (cap_n, cap_h) = ink(CAP_LETTERS)?;
    let (Some(native_baseline), Some(hd_baseline), Some(native_cap), Some(hd_cap)) = (
        median(base_n.iter().map(|v| v.1).collect()),
        median(base_h.iter().map(|v| v.1).collect()),
        median(cap_n.iter().map(|v| v.1 - v.0).collect()),
        median(cap_h.iter().map(|v| v.1 - v.0).collect()),
    ) else {
        return Ok(None);
    };
    if native_cap == 0 || hd_cap == 0 {
        return Ok(None);
    }
    let to_f = |v: usize| -> Result<f64> { Ok(f64::from(u32::try_from(v).map_err(|_| invalid())?)) };
    Ok(Some(FontMetrics {
        scale: to_f(native_cap * density)? / to_f(hd_cap)?,
        native_baseline: to_f(native_baseline * density)?,
        hd_baseline: to_f(hd_baseline)?,
    }))
}

pub fn prepare(native: &[u8], hd: &[u8], dialogue: &Value) -> Result<(Vec<u8>, Value)> {
    if native.len() > 32 * 1024 * 1024 || native.get(..4) != Some(b"GZFX") {
        return Err(invalid());
    }
    let (header, ps, cs, pages, count) =
        (u16at(native, 8)?, u16at(native, 10)?, u16at(native, 12)?, u32at(native, 16)?, u32at(native, 20)?);
    let (cw, ch) = (u16at(native, 38)?, u16at(native, 40)?);
    if header < 48
        || ps < 8
        || cs < 12
        || pages == 0
        || pages > 16
        || count == 0
        || count > 65536
        || cw == 0
        || cw > 128
        || ch == 0
        || ch > 128
        || u16at(native, 28)? & 0x1ff != 2
    {
        return Err(invalid());
    }
    let chars = header + pages * ps;
    if chars + count * cs > native.len() {
        return Err(invalid());
    }
    let mut records = BTreeMap::new();
    let mut previous = None;
    for i in 0..count {
        let at = chars + i * cs;
        let code = u32at(native, at)?;
        let page = u16at(native, at + 6)?;
        let cell = u16at(native, at + 10)?;
        if previous.is_some_and(|p| code <= p)
            || page >= pages
            || u16at(native, at + 4)? > 128
            || u16at(native, at + 8)? > 128
        {
            return Err(invalid());
        }
        previous = Some(code);
        records.insert(code, (page, cell));
    }
    for language in dialogue.as_object().ok_or_else(invalid)?.values() {
        for edit in language["edits"].as_array().ok_or_else(invalid)? {
            for code in edit["text"].as_str().ok_or_else(invalid)?.chars().filter(|c| !c.is_control()) {
                if !records.contains_key(&(code as usize)) {
                    return Err(ForgeError::invalid(format!(
                        "Native MM font lacks reviewed character U+{:04X}",
                        code as u32
                    )));
                }
            }
        }
    }
    let replacement = parse_qbf(hd)?;
    let density = 4;
    let mut zip = StoredZip::new();
    let mut sheets = Vec::new();
    let mut replaced = 0;
    let hd_codes: BTreeMap<_, _> = replacement.characters.iter().map(|r| (r.code as usize, r.glyph_index)).collect();
    let mut cell_owners: BTreeMap<(usize, usize), BTreeSet<u16>> = BTreeMap::new();
    for (code, cell) in &records {
        cell_owners.entry(*cell).or_default().insert(hd_codes.get(code).copied().unwrap_or(u16::MAX));
    }
    let metrics = font_metrics(native, header, ps, (cw, ch), &records, &replacement, &hd_codes, density)?;
    for page in 0..pages {
        let at = header + page * ps;
        let (offset, w, h) = (u32at(native, at)?, u16at(native, at + 4)?, u16at(native, at + 6)?);
        if w == 0 || h == 0 || w > 1024 || h > 1024 || w % 8 != 0 || h % 8 != 0 || offset < chars + count * cs {
            return Err(invalid());
        }
        let source = native.get(offset..offset + w * h / 2).ok_or_else(invalid)?;
        let mut coverage = vec![0u8; w * h * density * density / 2];
        for y in 0..h * density {
            for x in 0..w * density {
                write(&mut coverage, x, y, w * density, read(source, x / density, y / density, w));
            }
        }
        for ((owner, cell), glyphs) in &cell_owners {
            if *owner != page || glyphs.len() != 1 || glyphs.contains(&u16::MAX) {
                continue;
            }
            let (left, top) = ((cell & 255) * cw, (cell >> 8) * ch);
            if left + cw > w || top + ch > h {
                return Err(invalid());
            }
            let mut original = Vec::new();
            for y in 0..ch {
                for x in 0..cw {
                    original.push(read(source, left + x, top + y, w));
                }
            }
            let Some((x0, y0, x1, y1)) = bounds(&original, cw, ch) else { continue };
            let glyph = replacement.glyph_coverage(*glyphs.iter().next().unwrap())?;
            let (hw, hh) = (replacement.width as usize, replacement.height as usize);
            let Some((hx0, hy0, hx1, hy1)) = bounds(&glyph, hw, hh) else { continue };
            // Native ink bounds anchor the HD design, preserving baseline and
            // extents instead of inheriting OoT's line height or bearings.
            for y in 0..ch * density {
                for x in 0..cw * density {
                    write(&mut coverage, left * density + x, top * density + y, w * density, 0);
                }
            }
            if let Some(m) = &metrics {
                // One scale and one baseline for the whole font: each HD glyph keeps
                // its own proportions and its position relative to the baseline;
                // horizontally it is centred on the native ink, so the native
                // advances still place it. Stretching every glyph's ink box onto
                // the native ink box (the OoT3D QBF approach) gave each letter its
                // own size and vertical offset.
                let (cx, cy) =
                    (f64::from(u32::try_from((x0 + x1) * density).map_err(|_| invalid())?) / 2.0, m.native_baseline);
                let hxc = f64::from(u32::try_from(hx0 + hx1).map_err(|_| invalid())?) / 2.0;
                for y in 0..ch * density {
                    for x in 0..cw * density {
                        let hx = hxc + (x as f64 + 0.5 - cx) / m.scale;
                        let hy = m.hd_baseline + (y as f64 + 0.5 - cy) / m.scale;
                        if hx < 0.0 || hy < 0.0 || hx >= hw as f64 || hy >= hh as f64 {
                            continue;
                        }
                        let alpha = glyph[hy as usize * hw + hx as usize] / 17;
                        write(&mut coverage, left * density + x, top * density + y, w * density, alpha);
                    }
                }
            } else {
                let (tw, th) = ((x1 - x0) * density, (y1 - y0) * density);
                for y in 0..th {
                    for x in 0..tw {
                        let alpha = glyph[(hy0 + y * (hy1 - hy0) / th) * hw + hx0 + x * (hx1 - hx0) / tw] / 17;
                        write(&mut coverage, (left + x0) * density + x, (top + y0) * density + y, w * density, alpha);
                    }
                }
            }
            replaced += 1;
        }
        let original = format!("native/{page}.a4");
        let output = format!("coverage/{page}.a4");
        zip.add(&original, source);
        zip.add(&output, &coverage);
        sheets.push(json!({"width":w,"height":h,"density":density,"native_file":original,"coverage_file":output,"native_sha256":sha256_hex(source),"coverage_sha256":sha256_hex(&coverage)}));
    }
    if replaced == 0 {
        return Err(ForgeError::invalid("HD font has no compatible MM glyphs"));
    }
    let manifest = json!({"format":"triaevum_static_font_coverage_v1","source_format":"MM3D GZFX","source_sha256":sha256_hex(native),"coverage_source_sha256":sha256_hex(hd),"minimum_output_height":480,"native_characters":count,"replaced_cells":replaced,"sheets":sheets});
    zip.add("manifest.json", &serde_json::to_vec(&manifest).map_err(|_| invalid())?);
    Ok((zip.finish(), manifest))
}
#[cfg(test)]
mod tests {
    use super::*;
    /// Writes the coverage pack of a real font pair (private inputs, never committed):
    /// MM3D_ROMFS=<installed sources/*/romfs.bin> MM3D_HD_QBF=<ltn16.qbf> MM3D_FONT_OUT=<zip> cargo test -- --ignored.
    #[test]
    #[ignore = "needs the RomFS imported from a user's ROM"]
    fn real_font_pack() {
        let var = |name| std::env::var(name).expect(name);
        let native = crate::romfs::read_level3_file(std::path::Path::new(&var("MM3D_ROMFS")), "message/ltn16.gzf")
            .unwrap()
            .unwrap();
        let hd = std::fs::read(var("MM3D_HD_QBF")).unwrap();
        let (pack, manifest) = prepare(&native, &hd, &json!({})).unwrap();
        std::fs::write(var("MM3D_FONT_OUT"), pack).unwrap();
        // The native font beside the pack, for metric comparisons (private, like the pack).
        std::fs::write(format!("{}.gzf", var("MM3D_FONT_OUT")), &native).unwrap();
        println!("{manifest}");
    }
    #[test]
    fn bad_font() {
        assert!(prepare(&[], &[], &json!({})).is_err());
    }
    #[test]
    fn morton_roundtrip() {
        let mut a = vec![0u8; 128];
        for y in 0..16 {
            for x in 0..16 {
                write(&mut a, x, y, 16, ((x + y) % 16) as u8);
            }
        }
        for y in 0..16 {
            for x in 0..16 {
                assert_eq!(read(&a, x, y, 16), ((x + y) % 16) as u8);
            }
        }
    }
}
