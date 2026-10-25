// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! NCSD/NCCH discovery: accepted layouts and every rejection message.

mod common;

use std::io::Cursor;

use common::*;
use forge_base::{CancelToken, ErrorKind, ForgeError};
use forge_rom::{find_title_layout, ContainerKind, TitleLayout};

const P: usize = PARTITION_BASE;

fn layout(image: &[u8]) -> Result<TitleLayout, ForgeError> {
    find_title_layout(&mut Cursor::new(image), image.len() as u64, &CancelToken::new())
}

fn rejection(image: &[u8]) -> (ErrorKind, String) {
    let error = layout(image).expect_err("image must be rejected");
    (error.kind, error.message)
}

fn summary(detail: &str) -> String {
    format!("no usable decrypted application partition was found: {detail}")
}

fn mutated(change: impl FnOnce(&mut Vec<u8>)) -> Vec<u8> {
    let mut image = build_rom(true);
    change(&mut image);
    image
}

#[test]
fn ncsd_partition_zero_layout() {
    let found = layout(&build_rom(true)).unwrap();
    assert_eq!(
        found,
        TitleLayout {
            container_kind: ContainerKind::Ncsd,
            partition_index: 0,
            partition_base: P as u64,
            partition_end: 2 * P as u64,
            program_id: PROGRAM_ID,
            compressed_code: false,
            code_offset: (P + EXEFS + 0x200) as u64,
            code_size: CODE.len() as u64,
            exheader_offset: (P + 0x200) as u64,
            romfs_offset: (P + 0x2000) as u64,
            romfs_size: ROMFS_SIZE as u64,
        }
    );
}

#[test]
fn bare_ncch_is_a_single_partition() {
    let image = ncch(CODE, true, true);
    let found = layout(&image).unwrap();
    assert_eq!(found.container_kind, ContainerKind::Ncch);
    assert_eq!(found.partition_base, 0);
    assert_eq!(found.partition_end, image.len() as u64);
    assert!(found.compressed_code);
}

#[test]
fn legacy_crypto_flag_does_not_reject_plaintext() {
    assert!(layout(&build_rom(false)).is_ok());
}

#[test]
fn first_usable_partition_wins_and_failures_are_skipped() {
    let mut broken = ncch(CODE, false, true);
    broken[0x100] = b'X';
    let image = ncsd(&[broken, ncch(CODE, false, true)]);
    let found = layout(&image).unwrap();
    assert_eq!(found.partition_index, 1);
    assert_eq!(found.partition_base, 0x8000);
}

#[test]
fn code_name_stops_at_nul() {
    let image = mutated(|image| image[P + EXEFS + 5..P + EXEFS + 8].copy_from_slice(b"\0zz"));
    assert!(layout(&image).is_ok());
    let image = mutated(|image| image[P + EXEFS + 5] = b'x');
    assert_eq!(rejection(&image).0, ErrorKind::EncryptedInput);
}

#[test]
fn container_rejections() {
    assert_eq!(
        rejection(&build_rom(true)[..0x1FF]),
        (ErrorKind::InvalidInput, "the ROM is truncated while reading container header".into())
    );
    assert_eq!(
        rejection(&mutated(|image| image[0x100..0x104].copy_from_slice(b"NCSE"))),
        (ErrorKind::UnsupportedFormat, "the selected file is not a Nintendo 3DS NCSD/NCCH image".into())
    );
    assert_eq!(
        rejection(&mutated(|image| image[0x120..0x128].fill(0))),
        (ErrorKind::InvalidInput, summary("the partition table is empty"))
    );
    // A record with a base but no size is skipped like an empty one.
    assert_eq!(rejection(&mutated(|image| put_u32(image, 0x124, 0))).1, summary("the partition table is empty"));
}

#[test]
fn partition_rejections() {
    for case in partition_mutations() {
        let kind = if case.encrypted { ErrorKind::EncryptedInput } else { ErrorKind::InvalidInput };
        assert_eq!(rejection(&case.image), (kind, summary(case.detail)), "{}", case.name);
    }
}

#[test]
fn bare_ncch_errors_are_not_summarized() {
    let mut image = ncch(CODE, false, true);
    image[0x18D] = 0;
    assert_eq!(rejection(&image), (ErrorKind::InvalidInput, "partition 0 is not executable".into()));
}

#[test]
fn all_failures_are_listed_in_partition_order() {
    let mut first = ncch(CODE, false, true);
    first[0x18D] = 0;
    let mut second = ncch(CODE, false, true);
    put_u32(&mut second, 0x1B0, 0);
    let image = ncsd(&[first, second]);
    assert_eq!(
        rejection(&image),
        (ErrorKind::InvalidInput, summary("partition 0 is not executable; partition 1 has no RomFS"))
    );
}

#[test]
fn cancellation_propagates() {
    let cancel = CancelToken::new();
    cancel.cancel();
    let image = build_rom(true);
    let error = find_title_layout(&mut Cursor::new(&image), image.len() as u64, &cancel).unwrap_err();
    assert_eq!(error.kind, ErrorKind::Cancelled);
}
