// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Bounded catalog readers. Layouts and semantic rules belong to title recipes,
//! not the renderer. Reads encoded base mip bytes, never image statistics.
use std::collections::HashSet;
use std::path::Path;

use forge_base::{CancelToken, ForgeError, Result};
use serde::Deserialize;
use serde_json::Value;

use crate::romfs::{image_files, sorted_entries};
use crate::water::{base_level_bytes, bits_per_pixel, ModelTexture};

#[derive(Deserialize)]
pub struct Profile {
    pub title: String,
    pub revision: u32,
    directories: Vec<String>,
    suffixes: Vec<String>,
    zsi_magic: String,
    cmb: CmbLayout,
    water_rules: WaterRules,
}
#[derive(Deserialize)]
struct CmbLayout {
    version: u32,
    header_bytes: usize,
    texture_chunk_offset: usize,
    texture_data_offset: usize,
}
#[derive(Deserialize)]
struct WaterRules {
    exclude: Vec<String>,
    roles: Vec<Role>,
}
#[derive(Deserialize)]
struct Role {
    mask: u8,
    contains: Vec<String>,
}
fn invalid(message: &str) -> ForgeError {
    ForgeError::invalid(message)
}
fn bytes(data: &[u8], at: usize, count: usize) -> Result<&[u8]> {
    data.get(at..at.checked_add(count).ok_or_else(|| invalid("model extent overflow"))?)
        .ok_or_else(|| invalid("model read out of bounds"))
}
fn u32_at(data: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(bytes(data, at, 4)?.try_into().unwrap()))
}
fn u16_at(data: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(bytes(data, at, 2)?.try_into().unwrap()))
}
fn name(data: &[u8]) -> Result<String> {
    let end = data.iter().position(|b| *b == 0).unwrap_or(data.len());
    if !data[..end].is_ascii() {
        return Err(invalid("non-ASCII model name"));
    }
    Ok(String::from_utf8(data[..end].to_vec()).unwrap())
}
impl Profile {
    pub fn parse(value: &Value) -> Result<Self> {
        let p: Self = serde_json::from_value(value.clone()).map_err(|_| invalid("invalid water catalog profile"))?;
        if p.title.is_empty()
            || p.revision == 0
            || p.directories.is_empty()
            || p.directories.iter().any(|s| s.is_empty() || s == "." || s == ".." || s.contains(['/', '\\', ':']))
            || p.suffixes.is_empty()
            || p.suffixes.iter().any(|s| s.is_empty())
            || p.zsi_magic.len() != 8
            || !p.zsi_magic.bytes().all(|b| b.is_ascii_hexdigit())
            || !(12..=4096).contains(&p.cmb.header_bytes)
            || p.cmb.texture_chunk_offset > p.cmb.header_bytes - 4
            || p.cmb.texture_data_offset > p.cmb.header_bytes - 4
            || p.water_rules.roles.iter().any(|r| {
                r.mask == 0
                    || r.mask > 15
                    || r.contains.is_empty()
                    || r.contains.iter().any(|s| s.is_empty() || !s.is_ascii())
            })
            || p.water_rules.exclude.iter().any(|s| s.is_empty() || !s.is_ascii())
        {
            return Err(invalid("invalid water catalog profile bounds"));
        }
        Ok(p)
    }
    pub fn water_role(&self, name: &str) -> u8 {
        let name = name.to_ascii_lowercase();
        if self.water_rules.exclude.iter().any(|stem| name.contains(stem)) {
            return 0;
        }
        self.water_rules.roles.iter().find(|r| r.contains.iter().any(|stem| name.contains(stem))).map_or(0, |r| r.mask)
    }
    pub fn files(
        &self,
        romfs: &Path,
        cancel: &CancelToken,
        visit: &mut dyn FnMut(&str, &[u8]) -> Result<()>,
    ) -> Result<()> {
        let wanted = |path: &str| self.suffixes.iter().any(|suffix| path.ends_with(suffix));
        if romfs.is_dir() {
            for directory in &self.directories {
                for (name, path) in sorted_entries(&romfs.join(directory)) {
                    cancel.check()?;
                    if path.is_file() && wanted(&name) {
                        let data = std::fs::read(&path).map_err(|e| ForgeError::io("Cannot read model", e))?;
                        visit(&format!("{directory}/{name}"), &data)?;
                    }
                }
            }
            return Ok(());
        }
        let directories: Vec<&str> = self.directories.iter().map(String::as_str).collect();
        image_files(romfs, &directories, cancel, &mut |path, data| {
            if wanted(&path) {
                visit(&path, &data)?;
            }
            Ok(())
        })
    }
    pub fn textures<'a>(&self, path: &str, data: &'a [u8]) -> Result<Vec<ModelTexture<'a>>> {
        let mut out = Vec::new();
        if path.ends_with(".zsi") {
            let magic = bytes(data, 0, 4)?.iter().map(|b| format!("{b:02x}")).collect::<String>();
            if magic != self.zsi_magic {
                return Err(invalid("unsupported ZSI header"));
            }
            let mut cursor = 0;
            while let Some(relative) = data[cursor..].windows(4).position(|s| s == b"cmb ") {
                let at = cursor + relative;
                let size = u32_at(data, at + 4)? as usize;
                self.cmb_textures(&data[at..], &mut out)?;
                cursor = at + size.max(4);
            }
        } else if path.ends_with(".gar") || path.ends_with(".gar.lzs") {
            if bytes(data, 0, 4)? != b"GAR\x02" || u32_at(data, 4)? as usize != data.len() {
                return Err(invalid("unsupported GAR header"));
            }
            let count = u16_at(data, 10)? as usize;
            let table = u32_at(data, 16)? as usize;
            let offsets = u32_at(data, 20)? as usize;
            bytes(data, table, count * 12)?;
            bytes(data, offsets, count * 4)?;
            let mut names = HashSet::new();
            for i in 0..count {
                let size = u32_at(data, table + i * 12)? as usize;
                let start = u32_at(data, offsets + i * 4)? as usize;
                let name_at = u32_at(data, table + i * 12 + 8)? as usize;
                let tail = data.get(name_at..).ok_or_else(|| invalid("invalid GAR name offset"))?;
                let end = tail.iter().position(|b| *b == 0).ok_or_else(|| invalid("unterminated GAR name"))?;
                let name = name(&tail[..end])?;
                let model = bytes(data, start, size)?;
                if !names.insert(name.clone()) {
                    return Err(invalid("duplicate GAR member"));
                }
                if name.ends_with(".cmb") {
                    self.cmb_textures(model, &mut out)?;
                }
            }
        } else {
            return Err(invalid("unsupported model container"));
        }
        Ok(out)
    }
    fn cmb_textures<'a>(&self, data: &'a [u8], out: &mut Vec<ModelTexture<'a>>) -> Result<()> {
        bytes(data, 0, self.cmb.header_bytes)?;
        if bytes(data, 0, 4)? != b"cmb " || u32_at(data, 8)? != self.cmb.version {
            return Err(invalid("unsupported CMB header/version"));
        }
        let size = u32_at(data, 4)? as usize;
        if size < self.cmb.header_bytes {
            return Err(invalid("invalid CMB extent"));
        }
        let data = bytes(data, 0, size)?;
        let chunk = u32_at(data, self.cmb.texture_chunk_offset)? as usize;
        let texture_data = u32_at(data, self.cmb.texture_data_offset)? as usize;
        if bytes(data, chunk, 4)? != b"tex " {
            return Err(invalid("missing CMB texture chunk"));
        }
        let count = u32_at(data, chunk + 8)? as usize;
        bytes(data, chunk + 12, count.checked_mul(36).ok_or_else(|| invalid("CMB count overflow"))?)?;
        for i in 0..count {
            let e = chunk + 12 + i * 36;
            let extent = u32_at(data, e)? as usize;
            let width = u16_at(data, e + 8)?;
            let height = u16_at(data, e + 10)?;
            let format = u16_at(data, e + 12)?;
            let kind = u16_at(data, e + 14)?;
            let relative = u32_at(data, e + 16)? as usize;
            let name = name(bytes(data, e + 20, 16)?)?;
            let Some(bits) = bits_per_pixel(format, kind) else { continue };
            if texture_data == 0 {
                continue;
            }
            let base = base_level_bytes(width, height, bits);
            if width == 0 || height == 0 || base > extent {
                return Err(invalid("invalid CMB base mip extent"));
            }
            out.push((name, width, height, bytes(data, texture_data + relative, base)?));
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn lzs_bounds_and_overlapping_ring() {
        let mut packed = b"LzS\x01\0\0\0\0".to_vec();
        packed.extend_from_slice(&6u32.to_le_bytes());
        packed.extend_from_slice(&4u32.to_le_bytes());
        packed.extend_from_slice(&[1, b'a', 0xEE, 0xF2]);
        assert_eq!(unpack_lzs(&packed).unwrap(), b"aaaaaa");
        for end in 4..packed.len() {
            assert!(unpack_lzs(&packed[..end]).is_err(), "length {end}");
        }
        packed[8..12].copy_from_slice(&2u32.to_le_bytes());
        assert!(unpack_lzs(&packed).is_err());
        packed[8..12].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(unpack_lzs(&packed).is_err());
    }
    #[test]
    fn rejects_unbounded_profiles() {
        assert!(Profile::parse(&serde_json::json!({})).is_err());
        let mut p = serde_json::json!({"title":"fixture", "revision":1,
            "directories":["rooms"], "suffixes":[".zsi"], "zsi_magic":"5a534909",
            "cmb":{"version":123,"header_bytes":72,"texture_chunk_offset":48,"texture_data_offset":68},
            "water_rules":{"exclude":["wall"],"roles":[{"mask":1,"contains":["water"]}]}});
        assert_eq!(Profile::parse(&p).unwrap().water_role("WATER"), 1);
        assert_eq!(Profile::parse(&p).unwrap().water_role("water_wall"), 0);
        p["directories"] = serde_json::json!(["../escape"]);
        assert!(Profile::parse(&p).is_err());
        p["directories"] = serde_json::json!(["rooms"]);
        p["water_rules"]["roles"][0]["mask"] = serde_json::json!(16);
        assert!(Profile::parse(&p).is_err());
    }
}

pub fn unpack_lzs(data: &[u8]) -> Result<Vec<u8>> {
    if !data.starts_with(b"LzS\x01") {
        return Ok(data.to_vec());
    }
    let size = u32_at(data, 8)? as usize;
    let packed = u32_at(data, 12)? as usize;
    if packed != data.len() - 16 || size == 0 || size > 64 * 1024 * 1024 {
        return Err(invalid("invalid LzS extent"));
    }
    let mut out = Vec::with_capacity(size);
    let mut ring = [0u8; 4096];
    let (mut cursor, mut write) = (16, 0xFEE);
    while out.len() < size {
        let flags = bytes(data, cursor, 1)?[0];
        cursor += 1;
        for bit in 0..8 {
            if out.len() == size {
                break;
            }
            if flags & (1 << bit) != 0 {
                let value = bytes(data, cursor, 1)?[0];
                cursor += 1;
                out.push(value);
                ring[write] = value;
                write = (write + 1) & 4095;
            } else {
                let pair = bytes(data, cursor, 2)?;
                cursor += 2;
                let mut read = usize::from(pair[0]) | (usize::from(pair[1] & 0xF0) << 4);
                let count = usize::from(pair[1] & 15) + 3;
                if out.len() + count > size {
                    return Err(invalid("oversized LzS reference"));
                }
                for _ in 0..count {
                    let value = ring[read];
                    read = (read + 1) & 4095;
                    out.push(value);
                    ring[write] = value;
                    write = (write + 1) & 4095;
                }
            }
        }
    }
    Ok(out)
}
