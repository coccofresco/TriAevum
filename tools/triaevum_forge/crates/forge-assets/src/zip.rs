// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! A stored (uncompressed) ZIP writer producing the bytes Python's
//! `zipfile.ZipFile(..., "w", ZIP_STORED).writestr(ZipInfo(name), data)`
//! writes to a seekable file: version 2.0 headers, no flags, DOS date
//! 1980-01-01 00:00:00, no extra fields or comments, external attributes
//! `0o600 << 16`. `ZipInfo` records the creating system as 0 (MS-DOS) on
//! Windows and 3 (Unix) elsewhere, so the archive bytes depend on the
//! platform exactly as Python's do.

const VERSION: u16 = 20;
const DOS_DATE_1980_01_01: u16 = (1 << 5) | 1;
const EXTERNAL_ATTRIBUTES: u32 = 0o600 << 16;
const CREATE_SYSTEM: u8 = if cfg!(windows) { 0 } else { 3 };

struct Entry {
    name: Vec<u8>,
    crc: u32,
    size: u32,
    offset: u32,
}

#[derive(Default)]
pub struct StoredZip {
    bytes: Vec<u8>,
    entries: Vec<Entry>,
}

impl StoredZip {
    pub fn new() -> Self {
        Self::default()
    }

    /// Appends one member. Names are ASCII (Python would set the UTF-8 flag
    /// otherwise) and members stay far below the ZIP64 limits.
    pub fn add(&mut self, name: &str, data: &[u8]) {
        assert!(name.is_ascii(), "stored ZIP member names are ASCII");
        let size = u32::try_from(data.len()).expect("member below 4 GiB");
        let offset = u32::try_from(self.bytes.len()).expect("archive below 4 GiB");
        let crc = crc32(data);
        let out = &mut self.bytes;
        out.extend(b"PK\x03\x04");
        push16(out, VERSION);
        push16(out, 0); // flags
        push16(out, 0); // stored
        push16(out, 0); // time
        push16(out, DOS_DATE_1980_01_01);
        push32(out, crc);
        push32(out, size);
        push32(out, size);
        push16(out, name.len() as u16);
        push16(out, 0); // extra
        out.extend(name.as_bytes());
        out.extend(data);
        self.entries.push(Entry { name: name.as_bytes().to_vec(), crc, size, offset });
    }

    pub fn finish(mut self) -> Vec<u8> {
        let directory_offset = self.bytes.len() as u32;
        let out = &mut self.bytes;
        for entry in &self.entries {
            out.extend(b"PK\x01\x02");
            out.push(VERSION as u8);
            out.push(CREATE_SYSTEM);
            push16(out, VERSION);
            push16(out, 0); // flags
            push16(out, 0); // stored
            push16(out, 0); // time
            push16(out, DOS_DATE_1980_01_01);
            push32(out, entry.crc);
            push32(out, entry.size);
            push32(out, entry.size);
            push16(out, entry.name.len() as u16);
            push16(out, 0); // extra
            push16(out, 0); // comment
            push16(out, 0); // disk
            push16(out, 0); // internal attributes
            push32(out, EXTERNAL_ATTRIBUTES);
            push32(out, entry.offset);
            out.extend(&entry.name);
        }
        let directory_size = self.bytes.len() as u32 - directory_offset;
        let count = self.entries.len() as u16;
        let out = &mut self.bytes;
        out.extend(b"PK\x05\x06");
        push16(out, 0);
        push16(out, 0);
        push16(out, count);
        push16(out, count);
        push32(out, directory_size);
        push32(out, directory_offset);
        push16(out, 0); // comment
        self.bytes
    }
}

fn push16(out: &mut Vec<u8>, value: u16) {
    out.extend(value.to_le_bytes());
}

fn push32(out: &mut Vec<u8>, value: u32) {
    out.extend(value.to_le_bytes());
}

/// CRC-32 (IEEE 802.3, reflected, as zlib computes it).
fn crc32(data: &[u8]) -> u32 {
    static TABLE: std::sync::OnceLock<[u32; 256]> = std::sync::OnceLock::new();
    let table = TABLE.get_or_init(|| {
        let mut table = [0u32; 256];
        for (index, slot) in table.iter_mut().enumerate() {
            let mut value = index as u32;
            for _ in 0..8 {
                value = if value & 1 != 0 { 0xEDB8_8320 ^ (value >> 1) } else { value >> 1 };
            }
            *slot = value;
        }
        table
    });
    !data.iter().fold(!0u32, |crc, byte| table[((crc ^ u32::from(*byte)) & 0xff) as usize] ^ (crc >> 8))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_python_zipfile_on_windows_layout() {
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
        let mut archive = StoredZip::new();
        archive.add("a/b.qbf", b"xyz");
        archive.add("manifest.json", b"{}");
        let hex: String = archive.finish().iter().map(|b| format!("{b:02x}")).collect();
        // Python 3.13.8 on Windows; the byte after the central version is the system.
        let windows = "504b03041400000000000000210067ba8eeb030000000300000007000000612f622e71626678797a\
504b03041400000000000000210043bfa6a302000000020000000d0000006d616e69666573742e6a736f6e7b7d\
504b010214001400000000000000210067ba8eeb0300000003000000070000000000000000000000800100000000612f622e716266\
504b010214001400000000000000210043bfa6a302000000020000000d00000000000000000000008001280000006d616e69666573742e6a736f6e\
504b0506000000000200020070000000550000000000";
        let platform =
            if cfg!(windows) { windows.to_string() } else { windows.replace("504b01021400", "504b01021403") };
        assert_eq!(hex, platform);
    }
}
