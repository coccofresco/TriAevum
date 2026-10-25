// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `build_topscreen_texture_override_pack.build_texture_pack`: the O3TU v2
//! pack of the SingleScreen 3.0 UI atlas TriAevum ships.
//!
//! Layout: `O3TU`, `<III>` version 2, native overrides 0, profile texture
//! count; per texture `<IHHB3xIQ>` semantic length, width, height, PICA
//! format, payload length, FNV-1a 64 of the payload; then the UTF-8 semantic
//! and the raw CTXB payload. The developer-only `--singlescreen-archive`
//! source (a local 3.0 ZIP) is not part of the Forge and is not ported.

use forge_base::{ForgeError, Result};

use crate::fnv::fnv1a64;

/// The single profile texture: the shipped atlas and its semantic.
pub const UI_ATLAS_SEMANTIC: &str = "oot3d/topscreen/3.0/ui_atlas";
const CTXB_HEADER_SIZE: usize = 0x48;

/// PICA format of a CTXB (format, data type) pair.
fn pica_format(raw_format: u16, raw_type: u16) -> Option<u8> {
    Some(match (raw_format, raw_type) {
        (0x6752, 0x1401) => 0,
        (0x6754, 0x1401) => 1,
        (0x6752, 0x8034) => 2,
        (0x6754, 0x8363) => 3,
        (0x6752, 0x8033) => 4,
        (0x6758, 0x1401) => 5,
        (0x6757, 0x1401) => 7,
        (0x6756, 0x1401) => 8,
        (0x6758, 0x6760) => 9,
        (0x6757, 0x6761) => 10,
        (0x6756, 0x6761) => 11,
        (0x675A, 0x0000) => 12,
        (0x675B, 0x0000) => 13,
        _ => return None,
    })
}

/// The O3TU pack of the shipped `custom_ui01.ctxb`.
pub fn build_texture_pack(ui_atlas: &[u8]) -> Result<Vec<u8>> {
    let semantic = UI_ATLAS_SEMANTIC;
    if ui_atlas.len() < CTXB_HEADER_SIZE || &ui_atlas[..4] != b"ctxb" {
        return Err(ForgeError::invalid(format!("invalid profile CTXB: {semantic}")));
    }
    let half = |offset: usize| u16::from_le_bytes([ui_atlas[offset], ui_atlas[offset + 1]]);
    let (width, height, raw_format, raw_type) = (half(0x2C), half(0x2E), half(0x30), half(0x32));
    let format = pica_format(raw_format, raw_type).ok_or_else(|| {
        ForgeError::invalid(format!("unsupported profile CTXB format: {semantic}: 0x{raw_format:04x}/0x{raw_type:04x}"))
    })?;
    let payload = &ui_atlas[CTXB_HEADER_SIZE..];
    if width == 0 || height == 0 || payload.is_empty() {
        return Err(ForgeError::invalid(format!("empty profile CTXB: {semantic}")));
    }
    let payload_length = u32::try_from(payload.len()).map_err(|_| ForgeError::invalid("oversized profile CTXB"))?;
    let mut output = b"O3TU".to_vec();
    for value in [2u32, 0, 1] {
        output.extend(value.to_le_bytes());
    }
    output.extend((semantic.len() as u32).to_le_bytes());
    output.extend(width.to_le_bytes());
    output.extend(height.to_le_bytes());
    output.extend([format, 0, 0, 0]);
    output.extend(payload_length.to_le_bytes());
    output.extend(fnv1a64(payload).to_le_bytes());
    output.extend(semantic.as_bytes());
    output.extend(payload);
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ctxb(format: (u16, u16), payload: &[u8]) -> Vec<u8> {
        let mut data = vec![0u8; CTXB_HEADER_SIZE];
        data[..4].copy_from_slice(b"ctxb");
        for (index, value) in [8u16, 8, format.0, format.1].into_iter().enumerate() {
            data[0x2C + index * 2..0x2E + index * 2].copy_from_slice(&value.to_le_bytes());
        }
        data.extend(payload);
        data
    }

    #[test]
    fn layout_and_rejections() {
        let pack = build_texture_pack(&ctxb((0x6752, 0x1401), &[3; 256])).unwrap();
        assert_eq!(&pack[..16], b"O3TU\x02\0\0\0\0\0\0\0\x01\0\0\0");
        assert_eq!(pack.len(), 16 + 24 + UI_ATLAS_SEMANTIC.len() + 256);
        assert_eq!(&pack[40..40 + UI_ATLAS_SEMANTIC.len()], UI_ATLAS_SEMANTIC.as_bytes());
        assert_eq!(u64::from_le_bytes(pack[32..40].try_into().unwrap()), fnv1a64(&[3; 256]));
        assert!(build_texture_pack(b"ctxb").is_err());
        let error = build_texture_pack(&ctxb((0x1234, 0x1401), &[1])).unwrap_err();
        assert_eq!(error.message, "unsupported profile CTXB format: oot3d/topscreen/3.0/ui_atlas: 0x1234/0x1401");
        assert!(build_texture_pack(&ctxb((0x6752, 0x1401), &[])).is_err());
    }
}
