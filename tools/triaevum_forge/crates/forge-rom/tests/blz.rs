// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Backwards-LZSS decoding: round trips and every edge rejection.

mod common;

use common::*;
use forge_base::{CancelToken, ErrorKind};
use forge_rom::{decompress_exefs_code, MAX_DECOMPRESSED_CODE_SIZE};

fn decode(data: &[u8]) -> Result<Vec<u8>, String> {
    decompress_exefs_code(data, &CancelToken::new()).map_err(|error| {
        assert_eq!(error.kind, ErrorKind::InvalidInput);
        error.message
    })
}

fn footer(body: &[u8], footer_size: u32, encoded_size: u32, additional: u32) -> Vec<u8> {
    let mut data = body.to_vec();
    data.extend_from_slice(&((footer_size << 24) | encoded_size).to_le_bytes());
    data.extend_from_slice(&additional.to_le_bytes());
    data
}

#[test]
fn greedy_encoder_round_trips() {
    let mut rng = Rng(7);
    for case in 0..24 {
        let length = 64 + rng.below(3000);
        let plain = compressible(&mut rng, length);
        let prefix = if case % 3 == 0 { rng.below(32) } else { 0 };
        let Some(packed) = blz_compress(&plain, prefix, case % 4, 0x1002) else { continue };
        assert!(packed.len() < plain.len());
        assert_eq!(decode(&packed).unwrap(), plain, "case {case}");
    }
}

#[test]
fn empty_encoded_range_keeps_input_and_zero_fills() {
    let data = footer(b"abcd", 8, 8, 4);
    let mut expected = data.clone();
    expected.extend_from_slice(&[0; 4]);
    assert_eq!(decode(&data).unwrap(), expected);
}

#[test]
fn rejections() {
    assert_eq!(decode(&[0; 7]).unwrap_err(), "compressed ExeFS .code is too small");
    let too_big = (MAX_DECOMPRESSED_CODE_SIZE - 11) as u32;
    assert_eq!(
        decode(&footer(b"abcd", 8, 8, too_big)).unwrap_err(),
        "compressed ExeFS .code declares an invalid output size"
    );
    let invalid_footer = "compressed ExeFS .code has an invalid footer";
    for (footer_size, encoded_size) in [(7, 8), (13, 13), (8, 7), (8, 13)] {
        assert_eq!(decode(&footer(b"abcd", footer_size, encoded_size, 0)).unwrap_err(), invalid_footer);
    }
    // Control 0x80 at index 1 leaves one byte for a two-byte reference.
    assert_eq!(
        decode(&footer(&[0x00, 0x80], 8, 10, 4)).unwrap_err(),
        "compressed ExeFS .code back-reference is truncated"
    );
    // A first reference copies from beyond the end of the output.
    assert_eq!(
        decode(&footer(&[0x00, 0x00, 0x00, 0x80], 8, 11, 4)).unwrap_err(),
        "compressed ExeFS .code back-reference is out of range"
    );
    // Eleven output bytes remain and a reference wants eighteen.
    assert_eq!(decode(&footer(&[0x00, 0xF0, 0x80], 8, 11, 0)).unwrap_err(), "compressed ExeFS .code output underflow");
}

#[test]
fn output_index_zero_stops_decoding() {
    // Three literals, then a 13-byte reference fills the output while two
    // encoded bytes remain; they are skipped.
    let body = [0xEE, 0xEE, 0x00, 0xA0, b'c', b'b', b'a', 0x10];
    assert_eq!(decode(&footer(&body, 8, 16, 0)).unwrap(), b"acbacbacbacbacba");
}

#[test]
fn cancellation_is_checked() {
    let mut rng = Rng(3);
    let plain = compressible(&mut rng, 2000);
    let packed = blz_compress(&plain, 0, 0, 0x1002).unwrap();
    let cancel = CancelToken::new();
    cancel.cancel();
    assert_eq!(decompress_exefs_code(&packed, &cancel).unwrap_err().kind, ErrorKind::Cancelled);
}
