// Copyright Citra Emulator Project / Azahar Emulator Project
// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Backwards-LZSS (BLZ) decoding of compressed ExeFS `.code`
//! (`ctr_rom.decompress_exefs_code`).

use forge_base::{CancelToken, Result};

use crate::{rom_error, MAX_DECOMPRESSED_CODE_SIZE};

const CANCEL_INTERVAL: usize = 4096;

/// Decodes the backwards-LZSS representation used by compressed ExeFS code.
/// The encoded stream is read from the end towards the front; bytes before
/// the encoded range are kept verbatim at the start of the output.
///
/// Python's separate "literal is truncated" check is not reproduced: it
/// repeats the loop guard immediately above it and can never fire.
pub fn decompress_exefs_code(compressed: &[u8], cancel: &CancelToken) -> Result<Vec<u8>> {
    let length = compressed.len();
    if length < 8 {
        return Err(rom_error("compressed ExeFS .code is too small"));
    }
    let word = |offset: usize| u32::from_le_bytes(compressed[offset..offset + 4].try_into().unwrap());
    let buffer_top_and_bottom = word(length - 8);
    let additional_size = word(length - 4);
    let decompressed_size = length as u64 + u64::from(additional_size);
    if decompressed_size > MAX_DECOMPRESSED_CODE_SIZE {
        return Err(rom_error("compressed ExeFS .code declares an invalid output size"));
    }

    let footer_size = (buffer_top_and_bottom >> 24) as usize;
    let encoded_size = (buffer_top_and_bottom & 0x00FF_FFFF) as usize;
    if footer_size < 8 || footer_size > length || encoded_size < footer_size || encoded_size > length {
        return Err(rom_error("compressed ExeFS .code has an invalid footer"));
    }

    let mut index = length - footer_size;
    let stop_index = length - encoded_size;
    let mut output_index = decompressed_size as usize;
    let mut output = vec![0u8; output_index];
    output[..length].copy_from_slice(compressed);

    let mut iterations = 0usize;
    while index > stop_index {
        if iterations.is_multiple_of(CANCEL_INTERVAL) {
            cancel.check()?;
        }
        iterations += 1;
        index -= 1;
        let mut control = compressed[index];
        for _ in 0..8 {
            if index <= stop_index || output_index == 0 {
                break;
            }
            if control & 0x80 != 0 {
                if index < 2 {
                    return Err(rom_error("compressed ExeFS .code back-reference is truncated"));
                }
                index -= 2;
                let segment = usize::from(compressed[index]) | (usize::from(compressed[index + 1]) << 8);
                let segment_size = ((segment >> 12) & 0xF) + 3;
                let segment_offset = (segment & 0xFFF) + 2;
                if output_index < segment_size {
                    return Err(rom_error("compressed ExeFS .code output underflow"));
                }
                for _ in 0..segment_size {
                    let source = output_index + segment_offset;
                    if source >= output.len() {
                        return Err(rom_error("compressed ExeFS .code back-reference is out of range"));
                    }
                    output_index -= 1;
                    output[output_index] = output[source];
                }
            } else {
                index -= 1;
                output_index -= 1;
                output[output_index] = compressed[index];
            }
            control <<= 1;
        }
    }
    Ok(output)
}
