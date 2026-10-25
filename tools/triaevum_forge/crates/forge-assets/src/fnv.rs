// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The two FNV-1a 64 variants the formats use.

const PRIME: u64 = 0x0000_0100_0000_01B3;

/// The renderer's texture content hash (`HashNativeBytes`): FNV-1a 64 with
/// its nonstandard offset basis 1469598103934665603. Water catalogs, room
/// fingerprints, scene-light surfaces and table checksums use it.
pub fn content_hash(data: &[u8]) -> u64 {
    fold(1_469_598_103_934_665_603, data)
}

/// Standard FNV-1a 64 (basis 0xCBF29CE484222325), the O3TU payload hash.
pub fn fnv1a64(data: &[u8]) -> u64 {
    fold(0xCBF2_9CE4_8422_2325, data)
}

pub(crate) fn fold(mut value: u64, data: &[u8]) -> u64 {
    for byte in data {
        value = (value ^ u64::from(*byte)).wrapping_mul(PRIME);
    }
    value
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bases() {
        assert_eq!(content_hash(b""), 1_469_598_103_934_665_603);
        assert_eq!(content_hash(b"a"), (1_469_598_103_934_665_603u64 ^ 0x61).wrapping_mul(1_099_511_628_211));
        assert_eq!(fnv1a64(b"a"), 0xaf63_dc4c_8601_ec8c);
    }
}
