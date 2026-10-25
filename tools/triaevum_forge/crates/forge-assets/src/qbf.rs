// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `qbf_font.py`: the bounded OOT3D QBF coverage reader. Native metrics stay
//! authoritative; only the verified format-2, 4-bpp Latin coverage encoding
//! is accepted.

use std::collections::HashMap;

use forge_base::hashing::{hex, sha256_hex};
use forge_base::{ForgeError, Result};
use serde_json::{json, Map, Value};

pub const MAX_FONT_BYTES: usize = 32 * 1024 * 1024;

#[derive(Clone, Debug)]
pub struct QbfCharacter {
    pub code: u16,
    pub glyph_index: u16,
    /// All four bytes, including the one native 002d2674 does not export.
    pub metrics: [u8; 4],
}

#[derive(Clone, Debug)]
pub struct QbfFont {
    pub data: Vec<u8>,
    pub characters: Vec<QbfCharacter>,
    pub glyph_count: u16,
    pub fallback_code: u16,
    pub width: u8,
    pub height: u8,
    pub glyph_offset: usize,
}

fn invalid(message: impl Into<String>) -> ForgeError {
    ForgeError::invalid(message)
}

impl QbfFont {
    pub fn glyph_bytes(&self) -> usize {
        usize::from(self.width) * usize::from(self.height) / 2
    }

    pub fn sha256(&self) -> String {
        sha256_hex(&self.data)
    }

    /// The glyph's stored A4 bytes. Equal stored bytes are equal linear
    /// coverage (each nibble maps to `n * 17`), which is how Python compares
    /// aliases.
    fn glyph(&self, index: u16) -> Result<&[u8]> {
        if index >= self.glyph_count {
            return Err(invalid("QBF glyph index outside bitmap table"));
        }
        let start = self.glyph_offset + usize::from(index) * self.glyph_bytes();
        Ok(&self.data[start..start + self.glyph_bytes()])
    }

    /// Linear alpha, top row first; native 002d2504 swaps nibbles when it
    /// writes PICA A4 tiles.
    pub fn glyph_coverage(&self, index: u16) -> Result<Vec<u8>> {
        Ok(self.glyph(index)?.iter().flat_map(|value| [(value >> 4) * 17, (value & 15) * 17]).collect())
    }
}

pub fn parse_qbf(data: &[u8]) -> Result<QbfFont> {
    if !(16..=MAX_FONT_BYTES).contains(&data.len()) || &data[..4] != b"QBF1" {
        return Err(invalid("invalid QBF header or size"));
    }
    let half = |offset: usize| u16::from_le_bytes([data[offset], data[offset + 1]]);
    let (count, glyphs, fallback) = (half(4), half(6), half(8));
    let (bpp, width, height, encoding) = (data[12], data[13], data[14], data[15]);
    if bpp != 4 || encoding != 2 {
        return Err(invalid("unsupported QBF coverage encoding (expected format 2/A4)"));
    }
    if width == 0 || height == 0 || width % 8 != 0 || height % 8 != 0 {
        return Err(invalid("QBF cell dimensions must be nonzero multiples of eight"));
    }
    let offset = 16 + usize::from(count) * 8;
    let expected = offset + usize::from(glyphs) * usize::from(width) * usize::from(height) / 2;
    if count == 0 || glyphs == 0 || expected != data.len() {
        return Err(invalid("QBF directory/bitmap size mismatch"));
    }
    let mut characters = Vec::with_capacity(usize::from(count));
    let mut previous: i32 = -1;
    for index in 0..usize::from(count) {
        let position = 16 + index * 8;
        let (code, glyph) = (half(position), half(position + 2));
        if i32::from(code) <= previous {
            return Err(invalid("QBF character directory is not strictly ordered"));
        }
        if glyph >= glyphs {
            return Err(invalid("QBF character references an absent glyph"));
        }
        let metrics = data[position + 4..position + 8].try_into().expect("four bytes");
        characters.push(QbfCharacter { code, glyph_index: glyph, metrics });
        previous = i32::from(code);
    }
    if !characters.iter().any(|record| record.code == fallback) {
        return Err(invalid("QBF fallback character is absent"));
    }
    Ok(QbfFont {
        data: data.to_vec(),
        characters,
        glyph_count: glyphs,
        fallback_code: fallback,
        width,
        height,
        glyph_offset: offset,
    })
}

/// Coverage mapped by character, never by presumed matching glyph indices.
/// The logical cell and every metric come from the original.
pub fn pair_font_coverage(native: &QbfFont, replacement: &QbfFont) -> Result<Map<String, Value>> {
    let (native_width, native_height) = (u32::from(native.width), u32::from(native.height));
    let (width, height) = (u32::from(replacement.width), u32::from(replacement.height));
    if width % native_width != 0 || height % native_height != 0 || width / native_width != height / native_height {
        return Err(invalid("QBF replacement needs a uniform integral coverage density"));
    }
    let density = width / native_width;
    if density < 1 {
        return Err(invalid("QBF replacement has lower coverage density"));
    }
    let by_code: HashMap<u16, &QbfCharacter> = replacement.characters.iter().map(|entry| (entry.code, entry)).collect();
    let mut mappings = Vec::with_capacity(native.characters.len());
    let mut pairs = Vec::with_capacity(native.characters.len());
    for entry in &native.characters {
        let source = by_code
            .get(&entry.code)
            .ok_or_else(|| invalid(format!("QBF replacement is missing native character U+{:04X}", entry.code)))?;
        mappings.push(json!({
            "code": entry.code,
            "native_glyph": entry.glyph_index,
            "coverage_glyph": source.glyph_index,
            "native_metrics_hex": hex(&entry.metrics),
        }));
        pairs.push((entry.glyph_index, source.glyph_index));
    }
    // A native glyph pointer can stand for several codes; the blit observer
    // must not guess between different HD bitmaps for that pointer.
    let mut pointer_map: HashMap<u16, &[u8]> = HashMap::new();
    for (native_glyph, coverage_glyph) in pairs {
        let coverage = replacement.glyph(coverage_glyph)?;
        let previous = *pointer_map.entry(native_glyph).or_insert(coverage);
        if previous != coverage {
            return Err(invalid("QBF native glyph alias maps to conflicting HD coverage"));
        }
    }
    let differences = native.characters.iter().filter(|entry| entry.metrics != by_code[&entry.code].metrics).count();
    let contract = json!({
        "native_sha256": native.sha256(),
        "coverage_sha256": replacement.sha256(),
        "logical_cell": [native.width, native.height],
        "coverage_cell": [replacement.width, replacement.height],
        "density": density,
        "fallback_code": native.fallback_code,
        "characters": mappings,
        "replacement_metric_differences": differences,
    });
    let Value::Object(contract) = contract else { unreachable!() };
    Ok(contract)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// `test_qbf_font.font_bytes`.
    fn font_bytes(size: u8, records: &[(u16, u16, [u8; 4])]) -> Vec<u8> {
        let glyphs = records.iter().map(|r| r.1).max().unwrap() + 1;
        let mut data = b"QBF1".to_vec();
        for value in [records.len() as u16, glyphs, 42, 0] {
            data.extend(value.to_le_bytes());
        }
        data.extend([4, size, size, 2]);
        for (code, glyph, metrics) in records {
            data.extend(code.to_le_bytes());
            data.extend(glyph.to_le_bytes());
            data.extend(metrics);
        }
        for index in 0..glyphs {
            data.extend(std::iter::repeat_n(0x1f + index as u8, usize::from(size) * usize::from(size) / 2));
        }
        data
    }

    fn default_records() -> Vec<(u16, u16, [u8; 4])> {
        vec![(32, 0, [6, 4, 0, 0]), (42, 1, [5, 6, 0, 0])]
    }

    #[test]
    fn native_dimensions_metrics_and_nibble_order() {
        let font = parse_qbf(&font_bytes(16, &default_records())).unwrap();
        assert_eq!((font.width, font.height, font.fallback_code), (16, 16, 42));
        assert_eq!(font.characters[0].metrics, [6, 4, 0, 0]);
        assert_eq!(font.glyph_coverage(0).unwrap(), [17u8, 255].repeat(128));
        assert!(font.glyph_coverage(2).is_err());
    }

    #[test]
    fn truncations_and_invalid_fields_rejected() {
        let data = font_bytes(16, &default_records());
        for size in [0, 4, 15, 16, 25, data.len() - 1] {
            assert!(parse_qbf(&data[..size]).is_err(), "{size}");
        }
        let mut longer = data.clone();
        longer.push(0);
        assert!(parse_qbf(&longer).is_err());
        for (offset, value) in
            [(0, 0), (4, 255), (6, 0), (8, 99), (12, 8), (13, 0), (14, 15), (15, 4), (18, 255), (24, 32)]
        {
            let mut broken = data.clone();
            broken[offset] = value;
            assert!(parse_qbf(&broken).is_err(), "{offset}");
        }
    }

    #[test]
    fn pairing_rules() {
        let native = parse_qbf(&font_bytes(16, &default_records())).unwrap();
        let hd = parse_qbf(&font_bytes(64, &[(32, 1, [0xff; 4]), (42, 0, [0xff; 4])])).unwrap();
        let result = pair_font_coverage(&native, &hd).unwrap();
        assert_eq!(result["density"], 4);
        assert_eq!(result["replacement_metric_differences"], 2);
        assert_eq!(
            result["characters"][0],
            json!({"code": 32, "native_glyph": 0, "coverage_glyph": 1, "native_metrics_hex": "06040000"})
        );
        let missing = parse_qbf(&font_bytes(64, &[(42, 0, [0; 4])])).unwrap();
        assert!(pair_font_coverage(&native, &missing).unwrap_err().message.contains("missing native character U+0020"));
        for size in [8, 24] {
            assert!(pair_font_coverage(&native, &parse_qbf(&font_bytes(size, &default_records())).unwrap()).is_err());
        }
        let aliased = parse_qbf(&font_bytes(16, &[(32, 0, [0; 4]), (42, 0, [0; 4])])).unwrap();
        let hd = parse_qbf(&font_bytes(64, &default_records())).unwrap();
        assert!(pair_font_coverage(&aliased, &hd).unwrap_err().message.contains("alias"));
    }
}
