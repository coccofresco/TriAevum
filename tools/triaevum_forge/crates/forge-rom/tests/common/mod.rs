// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Synthetic NCSD/NCCH images and BLZ streams. `build_rom` reproduces
//! `test_ctr_rom._build_rom` byte for byte (checked by the Python oracle).

#![allow(dead_code)]

pub const PARTITION_BASE: usize = 0x4000;
pub const CODE: &[u8] = b"synthetic-uncompressed-exefs-code";
pub const PROGRAM_ID: u64 = 0x0004_0000_0003_3600;
pub const EXEFS: usize = 0xC00;
pub const ROMFS_SIZE: usize = 0x2000;

pub fn put_u32(data: &mut [u8], offset: usize, value: u32) {
    data[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

pub fn get_u32(data: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(data[offset..offset + 4].try_into().unwrap())
}

fn align(value: usize) -> usize {
    value.div_ceil(0x200) * 0x200
}

/// Offset of the RomFS inside a partition built by [`ncch`].
pub fn romfs_offset(code_len: usize) -> usize {
    (EXEFS + align(0x200 + code_len)).max(0x2000)
}

/// One executable NCCH partition: ExHeader at 0x200, ExeFS at 0xC00 holding
/// only `.code`, then an `IVFC` RomFS of 0x2000 bytes filled with `R`.
pub fn ncch(code: &[u8], compressed: bool, declares_decrypted: bool) -> Vec<u8> {
    let exefs_size = align(0x200 + code.len());
    let romfs = romfs_offset(code.len());
    let size = romfs + ROMFS_SIZE;
    let mut image = vec![0u8; size];
    image[0x100..0x104].copy_from_slice(b"NCCH");
    put_u32(&mut image, 0x108, (size / 0x200) as u32);
    image[0x118..0x120].copy_from_slice(&PROGRAM_ID.to_le_bytes());
    put_u32(&mut image, 0x180, 0x400);
    image[0x18D] = 0x02;
    image[0x18F] = if declares_decrypted { 0x04 } else { 0 };

    for index in 0..0x800 {
        image[0x200 + index] = (index & 0xFF) as u8;
    }
    image[0x200 + 0x0D] = u8::from(compressed);

    put_u32(&mut image, 0x1A0, (EXEFS / 0x200) as u32);
    put_u32(&mut image, 0x1A4, (exefs_size / 0x200) as u32);
    image[EXEFS..EXEFS + 8].copy_from_slice(b".code\0\0\0");
    put_u32(&mut image, EXEFS + 8, 0);
    put_u32(&mut image, EXEFS + 12, code.len() as u32);
    image[EXEFS + 0x200..EXEFS + 0x200 + code.len()].copy_from_slice(code);

    put_u32(&mut image, 0x1B0, (romfs / 0x200) as u32);
    put_u32(&mut image, 0x1B4, (ROMFS_SIZE / 0x200) as u32);
    image[romfs..romfs + 4].copy_from_slice(b"IVFC");
    image[romfs + 4..romfs + ROMFS_SIZE].fill(b'R');
    image
}

/// An NCSD image with the partitions laid out back to back from 0x4000.
pub fn ncsd(partitions: &[Vec<u8>]) -> Vec<u8> {
    let mut image = vec![0u8; PARTITION_BASE];
    image[0x100..0x104].copy_from_slice(b"NCSD");
    for (index, partition) in partitions.iter().enumerate() {
        let base = image.len();
        put_u32(&mut image, 0x120 + index * 8, (base / 0x200) as u32);
        put_u32(&mut image, 0x124 + index * 8, (align(partition.len()) / 0x200) as u32);
        image.extend_from_slice(partition);
        image.resize(align(image.len()), 0);
    }
    image
}

pub fn build_rom(declares_decrypted: bool) -> Vec<u8> {
    ncsd(&[ncch(CODE, false, declares_decrypted)])
}

pub fn expected_exheader() -> Vec<u8> {
    let mut exheader: Vec<u8> = (0..0x800).map(|index| (index & 0xFF) as u8).collect();
    exheader[0x0D] = 0;
    exheader
}

/// A [`build_rom`] image broken in one field, with the partition failure
/// message Python reports for it inside the NCSD summary.
pub struct Mutation {
    pub name: &'static str,
    pub image: Vec<u8>,
    pub detail: &'static str,
    pub encrypted: bool,
}

pub fn partition_mutations() -> Vec<Mutation> {
    const P: usize = PARTITION_BASE;
    type Change = fn(&mut Vec<u8>);
    let outside = |label| match label {
        "ExHeader" => "ExHeader lies outside the selected title partition",
        "ExeFS" => "ExeFS lies outside the selected title partition",
        ".code" => "ExeFS .code lies outside the selected title partition",
        _ => "RomFS lies outside the selected title partition",
    };
    let cases: [(&'static str, Change, &'static str, bool); 17] = [
        (
            "header_outside",
            |i| put_u32(i, 0x120, 0x40),
            "partition 0 NCCH header lies outside the selected title partition",
            false,
        ),
        ("exceeds_rom", |i| put_u32(i, 0x124, 0x21), "partition 0 exceeds the ROM size", false),
        ("not_ncch", |i| i[P + 0x100] = b'X', "partition 0 is not an NCCH title", false),
        ("not_executable", |i| i[P + 0x18D] = 0x01, "partition 0 is not executable", false),
        ("no_exheader", |i| put_u32(i, P + 0x180, 0), "partition 0 has no ExHeader", false),
        ("exheader_outside", |i| put_u32(i, 0x124, 4), outside("ExHeader"), false),
        ("no_exefs_offset", |i| put_u32(i, P + 0x1A0, 0), "partition 0 has no ExeFS", false),
        ("no_exefs_size", |i| put_u32(i, P + 0x1A4, 0), "partition 0 has no ExeFS", false),
        ("exefs_outside", |i| put_u32(i, P + 0x1A4, 0x20), outside("ExeFS"), false),
        ("code_offset_outside", |i| put_u32(i, P + EXEFS + 8, 0x200), outside(".code"), false),
        ("code_empty", |i| put_u32(i, P + EXEFS + 12, 0), outside(".code"), false),
        ("code_size_outside", |i| put_u32(i, P + EXEFS + 12, 0x201), outside(".code"), false),
        ("no_romfs_offset", |i| put_u32(i, P + 0x1B0, 0), "partition 0 has no RomFS", false),
        ("no_romfs_size", |i| put_u32(i, P + 0x1B4, 0), "partition 0 has no RomFS", false),
        ("romfs_outside", |i| put_u32(i, P + 0x1B4, 0x11), outside("RomFS"), false),
        (
            "encrypted_exefs",
            |i| i[P + EXEFS..P + EXEFS + 8].copy_from_slice(&[0x8a, 1, 2, 3, 4, 5, 6, 7]),
            "partition 0 has no readable ExeFS .code section; the ROM may still be encrypted",
            true,
        ),
        (
            "encrypted_romfs",
            |i| i[P + 0x2000] = b'X',
            "the selected title RomFS is not decrypted or has an invalid IVFC header",
            true,
        ),
    ];
    cases
        .into_iter()
        .map(|(name, change, detail, encrypted)| {
            let mut image = build_rom(true);
            change(&mut image);
            Mutation { name, image, detail, encrypted }
        })
        .collect()
}

/// splitmix64, so fixtures are reproducible without a dependency.
pub struct Rng(pub u64);

impl Rng {
    pub fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    pub fn below(&mut self, bound: usize) -> usize {
        (self.next() % bound as u64) as usize
    }

    pub fn bytes(&mut self, count: usize) -> Vec<u8> {
        (0..count).map(|_| self.next() as u8).collect()
    }
}

/// Compressible text: words from a small alphabet with random repeats.
pub fn compressible(rng: &mut Rng, length: usize) -> Vec<u8> {
    let mut data = Vec::with_capacity(length);
    while data.len() < length {
        if data.len() > 8 && rng.below(3) == 0 {
            let start = rng.below(data.len() - 4);
            let count = (4 + rng.below(30)).min(data.len() - start);
            let copied = data[start..start + count].to_vec();
            data.extend_from_slice(&copied);
        } else {
            data.push(b"abcdeXYZ\0\x01\xFF"[rng.below(11)]);
        }
    }
    data.truncate(length);
    data
}

/// Packs a decoder-order token stream into a BLZ buffer: the verbatim
/// prefix, the reversed stream, `padding` 0xFF bytes and the footer.
pub fn blz_pack(prefix: &[u8], stream: &[u8], padding: usize, decompressed_size: usize) -> Option<Vec<u8>> {
    let footer_size = 8 + padding;
    let encoded_size = stream.len() + footer_size;
    let mut packed = prefix.to_vec();
    packed.extend(stream.iter().rev());
    packed.extend(std::iter::repeat_n(0xFF, padding));
    let additional = decompressed_size.checked_sub(packed.len() + 8)?;
    if footer_size > 0xFF || encoded_size > 0xFF_FFFF {
        return None;
    }
    packed.extend_from_slice(&(((footer_size as u32) << 24) | encoded_size as u32).to_le_bytes());
    packed.extend_from_slice(&(additional as u32).to_le_bytes());
    Some(packed)
}

/// A greedy backwards-LZSS encoder; `None` when the result would not be
/// smaller than `plain` (the format cannot express a negative growth).
pub fn blz_compress(plain: &[u8], raw_prefix: usize, padding: usize, max_distance: usize) -> Option<Vec<u8>> {
    let mut stream = Vec::new();
    let mut position = plain.len();
    while position > raw_prefix {
        let control = stream.len();
        stream.push(0u8);
        for bit in 0..8 {
            if position <= raw_prefix {
                break;
            }
            let longest = 18.min(position - raw_prefix);
            let (mut best_length, mut best_distance) = (0, 0);
            if longest >= 3 {
                for distance in 3..=max_distance.min(0x1002) {
                    if position - 1 + distance >= plain.len() {
                        break;
                    }
                    let mut length = 0;
                    while length < longest && plain[position - 1 - length] == plain[position - 1 - length + distance] {
                        length += 1;
                    }
                    if length > best_length {
                        (best_length, best_distance) = (length, distance);
                    }
                }
            }
            if best_length >= 3 {
                stream[control] |= 0x80 >> bit;
                let segment = ((best_length - 3) << 12) | (best_distance - 3);
                stream.push((segment >> 8) as u8);
                stream.push(segment as u8);
                position -= best_length;
            } else {
                stream.push(plain[position - 1]);
                position -= 1;
            }
        }
    }
    blz_pack(&plain[..raw_prefix], &stream, padding, plain.len())
}

/// A decoder-order stream of random literal and back-reference tokens; the
/// result may decode or fail on any edge check.
pub fn blz_random(rng: &mut Rng) -> Vec<u8> {
    let prefix_length = rng.below(16);
    let prefix = rng.bytes(prefix_length);
    let mut stream = Vec::new();
    for _ in 0..1 + rng.below(40) {
        let mut control = 0u8;
        let mut tokens = Vec::new();
        for bit in 0..8 {
            if rng.below(4) == 0 {
                control |= 0x80 >> bit;
                let reach = if rng.below(8) == 0 { 0x1000 } else { 24 };
                let segment = (rng.below(16) << 12) | rng.below(reach);
                tokens.push((segment >> 8) as u8);
                tokens.push(segment as u8);
            } else {
                tokens.push(rng.next() as u8);
            }
        }
        stream.push(control);
        stream.extend(tokens);
    }
    let kept = 1 + rng.below(stream.len());
    stream.truncate(kept);
    let padding = rng.below(4);
    let natural = prefix.len() + stream.len() + 8 + padding;
    let size = natural + rng.below(3 * kept + 1);
    let mut packed = blz_pack(&prefix, &stream, padding, size).unwrap();
    let length = packed.len();
    match rng.below(10) {
        0 => {
            let word = get_u32(&packed, length - 8);
            let encoded = (word & 0xFF_FFFF) as usize;
            let grown = (encoded + rng.below(length)) as u32 & 0xFF_FFFF;
            put_u32(&mut packed, length - 8, (word & 0xFF00_0000) | grown);
        }
        1 => packed[length - 5] = rng.next() as u8,
        2 => put_u32(&mut packed, length - 4, rng.next() as u32),
        _ => {}
    }
    packed
}
