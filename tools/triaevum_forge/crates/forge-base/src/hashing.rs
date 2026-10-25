// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Streaming SHA-256 with bounded memory, and the lowercase hex spelling every
//! Forge format uses.

use std::fs::File;
use std::io::{Read, Write};
use std::path::Path;

use sha2::{Digest, Sha256};

use crate::cancel::CancelToken;
use crate::error::{ForgeError, Result};

pub const CHUNK_BYTES: usize = 4 * 1024 * 1024;

pub fn hex(bytes: &[u8]) -> String {
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    let mut text = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        text.push(DIGITS[usize::from(byte >> 4)] as char);
        text.push(DIGITS[usize::from(byte & 0x0f)] as char);
    }
    text
}

pub fn sha256_bytes(bytes: &[u8]) -> [u8; 32] {
    Sha256::digest(bytes).into()
}

pub fn sha256_hex(bytes: &[u8]) -> String {
    hex(&sha256_bytes(bytes))
}

/// Whether `text` is a lowercase 64-digit hex SHA-256, as catalogs require.
pub fn is_sha256_hex(text: &str) -> bool {
    text.len() == 64 && text.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}

/// Hashes a reader to its end, returning (bytes read, digest).
pub fn sha256_reader(reader: &mut impl Read, cancel: &CancelToken) -> Result<(u64, [u8; 32])> {
    copy_hashing(reader, &mut std::io::sink(), u64::MAX, cancel)
}

pub fn sha256_file(path: &Path, cancel: &CancelToken) -> Result<(u64, [u8; 32])> {
    let mut file =
        File::open(path).map_err(|error| ForgeError::io(format!("Cannot read {}", path.display()), error))?;
    sha256_reader(&mut file, cancel)
}

/// Copies at most `limit` bytes from `reader` to `writer`, hashing them.
pub fn copy_hashing(
    reader: &mut impl Read,
    writer: &mut impl Write,
    limit: u64,
    cancel: &CancelToken,
) -> Result<(u64, [u8; 32])> {
    let mut hasher = Sha256::new();
    let mut buffer = vec![0u8; CHUNK_BYTES];
    let mut total = 0u64;
    while total < limit {
        cancel.check()?;
        let want = usize::try_from((limit - total).min(CHUNK_BYTES as u64)).unwrap_or(CHUNK_BYTES);
        let read = match reader.read(&mut buffer[..want]) {
            Ok(0) => break,
            Ok(read) => read,
            Err(error) if error.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(error) => return Err(ForgeError::io("Read failed", error)),
        };
        hasher.update(&buffer[..read]);
        writer.write_all(&buffer[..read]).map_err(|error| ForgeError::io("Write failed", error))?;
        total += read as u64;
    }
    Ok((total, hasher.finalize().into()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_digest() {
        assert_eq!(sha256_hex(b"abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        assert!(is_sha256_hex(&sha256_hex(b"")));
        assert!(!is_sha256_hex(&sha256_hex(b"").to_uppercase()));
    }

    #[test]
    fn bounded_copy_stops_at_limit() {
        let data = vec![7u8; 10_000];
        let mut out = Vec::new();
        let (count, digest) = copy_hashing(&mut &data[..], &mut out, 1234, &CancelToken::new()).unwrap();
        assert_eq!(count, 1234);
        assert_eq!(out.len(), 1234);
        assert_eq!(digest, sha256_bytes(&data[..1234]));
    }
}
