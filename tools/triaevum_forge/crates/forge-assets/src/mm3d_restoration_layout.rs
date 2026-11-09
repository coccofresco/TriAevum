// SPDX-License-Identifier: GPL-3.0-or-later
//! Project Restoration layout files rebuilt at install time from the user's
//! dump (recipe `triaevum_restoration_layout_v1`, coordinator item
//! C-DISTRO-1). The Moonfall Curse package ships no game bytes: dump members
//! are read from the user's `layout/Joker.Main.Play.gar`, modified members
//! and `Joker.mfpj` are BPS patches of their dump base, and only the
//! Restoration-original members ship (GPL-2.0-or-later, see
//! LICENSES/Project-Restoration.md). The offline generator is
//! tools/mm3d/restoration_layout.py; GAR v2 facts come from the tree's
//! titles/mm3d/dekui/setup/dekui_gar.py, BPS from the public format
//! description. Title-mm3d Forge only.
use crate::romfs;
use forge_base::hashing::sha256_hex;
use forge_base::{ForgeError, Result};
use serde_json::Value;
use std::collections::BTreeMap;
use std::path::Path;

pub const FORMAT: &str = "triaevum_restoration_layout_v1";
/// Package-relative recipe (beside the patches and members it names).
pub const RECIPE: &str = "layout.recipe.json";
const LIMIT: u64 = 16 * 1024 * 1024;

fn invalid(message: impl Into<String>) -> ForgeError {
    ForgeError::invalid(message)
}
fn bad_gar() -> ForgeError {
    invalid("Invalid GAR archive")
}

// ---------------------------------------------------------------- GAR v2 --
/// A GAR v2 in the canonical layout [`emit_gar`] writes.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Gar {
    pub hash: String,
    pub types: Vec<String>,
    pub align: usize,
    pub members: Vec<(String, Vec<u8>)>,
}

fn u16at(b: &[u8], at: usize) -> Result<usize> {
    Ok(u16::from_le_bytes(b.get(at..at + 2).ok_or_else(bad_gar)?.try_into().unwrap()) as usize)
}
fn u32at(b: &[u8], at: usize) -> Result<usize> {
    Ok(u32::from_le_bytes(b.get(at..at.checked_add(4).ok_or_else(bad_gar)?).ok_or_else(bad_gar)?.try_into().unwrap())
        as usize)
}
fn cstr(b: &[u8], at: usize) -> Result<String> {
    let rest = b.get(at..).ok_or_else(bad_gar)?;
    let n = rest.iter().position(|x| *x == 0).ok_or_else(bad_gar)?;
    if !rest[..n].is_ascii() {
        return Err(bad_gar());
    }
    Ok(String::from_utf8(rest[..n].to_vec()).map_err(|_| bad_gar())?)
}

/// Parse a GAR v2; refused unless re-emitting it gives the same bytes.
pub fn parse_gar(b: &[u8]) -> Result<Gar> {
    if b.len() < 0x20 || &b[..4] != b"GAR\x02" || u32at(b, 4)? != b.len() {
        return Err(invalid("Unsupported GAR header"));
    }
    let (ntypes, count) = (u16at(b, 8)?, u16at(b, 10)?);
    let (types_at, records, offsets) = (u32at(b, 12)?, u32at(b, 16)?, u32at(b, 20)?);
    let hash = String::from_utf8(b[24..32].iter().copied().take_while(|c| *c != 0).collect())
        .map_err(|_| bad_gar())?;
    let mut types = Vec::with_capacity(ntypes);
    for i in 0..ntypes {
        types.push(cstr(b, u32at(b, types_at + i * 16 + 8)?)?);
    }
    let mut members: Vec<(String, Vec<u8>)> = Vec::with_capacity(count);
    for i in 0..count {
        let record = records + i * 12;
        let (size, start) = (u32at(b, record)?, u32at(b, offsets + i * 4)?);
        let payload = b.get(start..start.checked_add(size).ok_or_else(bad_gar)?).ok_or_else(bad_gar)?;
        let name = cstr(b, u32at(b, record + 8)?)?;
        if members.iter().any(|(n, _)| *n == name) {
            return Err(invalid("Duplicate GAR entry"));
        }
        members.push((name, payload.to_vec()));
    }
    let gar = Gar { hash, types, align: 4, members };
    if emit_gar(&gar)? != b {
        return Err(invalid("GAR layout is not the canonical v2 layout"));
    }
    Ok(gar)
}

fn pad(out: &mut Vec<u8>, align: usize) {
    while out.len() % align != 0 {
        out.push(0);
    }
}
fn put32(out: &mut [u8], at: usize, value: usize) -> Result<()> {
    let value = u32::try_from(value).map_err(|_| bad_gar())?;
    out[at..at + 4].copy_from_slice(&value.to_le_bytes());
    Ok(())
}

/// Canonical GAR v2: header; type table (count, index list, name, -1); per
/// type its index list then its 4-aligned name; file records (size, stem,
/// full name); 4-aligned name pairs; offsets; payloads.
pub fn emit_gar(gar: &Gar) -> Result<Vec<u8>> {
    if gar.align == 0 || gar.hash.len() >= 8 || gar.types.len() > 0xFFFF || gar.members.len() > 0xFFFF {
        return Err(bad_gar());
    }
    let mut by_type: Vec<Vec<usize>> = vec![Vec::new(); gar.types.len()];
    for (index, (name, _)) in gar.members.iter().enumerate() {
        let kind = name.rsplit_once('.').map_or("unknown", |(_, ext)| ext);
        let slot = gar.types.iter().position(|t| t == kind).ok_or_else(|| invalid("GAR member type not declared"))?;
        by_type[slot].push(index);
    }
    let mut out = vec![0u8; 0x20 + 16 * gar.types.len()];
    for (t, name) in gar.types.iter().enumerate() {
        let indices = &by_type[t];
        let list_at = if indices.is_empty() { 0xFFFF_FFFF } else { out.len() };
        for index in indices {
            out.extend((*index as u32).to_le_bytes());
        }
        let name_at = out.len();
        out.extend(name.as_bytes());
        out.push(0);
        pad(&mut out, 4);
        let at = 0x20 + t * 16;
        put32(&mut out, at, indices.len())?;
        put32(&mut out, at + 4, list_at)?;
        put32(&mut out, at + 8, name_at)?;
        put32(&mut out, at + 12, 0xFFFF_FFFF)?;
    }
    let records = out.len();
    out.resize(records + 12 * gar.members.len(), 0);
    for (i, (name, payload)) in gar.members.iter().enumerate() {
        if !name.is_ascii() || name.contains('\0') {
            return Err(bad_gar());
        }
        let full_at = out.len();
        out.extend(name.as_bytes());
        out.push(0);
        let stem_at = out.len();
        out.extend(name.rsplit_once('.').map_or(name.as_str(), |(stem, _)| stem).as_bytes());
        out.push(0);
        pad(&mut out, 4);
        put32(&mut out, records + i * 12, payload.len())?;
        put32(&mut out, records + i * 12 + 4, stem_at)?;
        put32(&mut out, records + i * 12 + 8, full_at)?;
    }
    let offsets = out.len();
    out.resize(offsets + 4 * gar.members.len(), 0);
    for (i, (_, payload)) in gar.members.iter().enumerate() {
        if i > 0 {
            pad(&mut out, gar.align);
        }
        let at = out.len();
        put32(&mut out, offsets + i * 4, at)?;
        out.extend(payload);
    }
    out[..4].copy_from_slice(b"GAR\x02");
    let size = out.len();
    put32(&mut out, 4, size)?;
    out[8..10].copy_from_slice(&(gar.types.len() as u16).to_le_bytes());
    out[10..12].copy_from_slice(&(gar.members.len() as u16).to_le_bytes());
    put32(&mut out, 12, 0x20)?;
    put32(&mut out, 16, records)?;
    put32(&mut out, 20, offsets)?;
    out[24..24 + gar.hash.len()].copy_from_slice(gar.hash.as_bytes());
    Ok(out)
}

// ------------------------------------------------------------------- BPS --
fn bad_bps(what: &str) -> ForgeError {
    invalid(format!("Invalid Restoration patch: {what}"))
}
fn varint(b: &[u8], at: &mut usize) -> Result<u64> {
    let (mut data, mut shift) = (0u64, 1u64);
    loop {
        let x = *b.get(*at).ok_or_else(|| bad_bps("truncated number"))?;
        *at += 1;
        data = data.checked_add(u64::from(x & 0x7F).checked_mul(shift).ok_or_else(|| bad_bps("number"))?)
            .ok_or_else(|| bad_bps("number"))?;
        if x & 0x80 != 0 {
            return Ok(data);
        }
        shift = shift.checked_shl(7).filter(|s| *s < 1 << 56).ok_or_else(|| bad_bps("number"))?;
        data = data.checked_add(shift).ok_or_else(|| bad_bps("number"))?;
    }
}
fn trailer(patch: &[u8], from_end: usize) -> u32 {
    let at = patch.len() - from_end;
    u32::from_le_bytes(patch[at..at + 4].try_into().unwrap())
}
fn size(value: u64) -> Result<usize> {
    usize::try_from(value).ok().filter(|v| (*v as u64) <= LIMIT).ok_or_else(|| bad_bps("size"))
}

/// Apply a BPS patch: the patch, source and target CRC32 are all checked.
pub fn bps_apply(patch: &[u8], source: &[u8]) -> Result<Vec<u8>> {
    if patch.len() < 16 || &patch[..4] != b"BPS1" {
        return Err(bad_bps("not a BPS patch"));
    }
    if crate::zip::crc32(&patch[..patch.len() - 4]) != trailer(patch, 4) {
        return Err(bad_bps("patch checksum mismatch"));
    }
    let mut at = 4;
    let source_size = size(varint(patch, &mut at)?)?;
    let target_size = size(varint(patch, &mut at)?)?;
    let metadata = size(varint(patch, &mut at)?)?;
    at = at.checked_add(metadata).ok_or_else(|| bad_bps("metadata"))?;
    if source_size != source.len() || crate::zip::crc32(source) != trailer(patch, 12) {
        return Err(bad_bps("source mismatch"));
    }
    let end = patch.len() - 12;
    let mut target = Vec::with_capacity(target_size);
    let (mut source_rel, mut target_rel) = (0i64, 0i64);
    while at < end {
        let data = varint(patch, &mut at)?;
        let (command, length) = (data & 3, size((data >> 2) + 1)?);
        if target.len() + length > target_size {
            return Err(bad_bps("action exceeds target"));
        }
        match command {
            0 => {
                let range = target.len()..target.len() + length;
                target.extend_from_slice(source.get(range).ok_or_else(|| bad_bps("SourceRead"))?);
            }
            1 => {
                let bytes = patch.get(at..at + length).filter(|_| at + length <= end).ok_or_else(|| bad_bps("TargetRead"))?;
                target.extend_from_slice(bytes);
                at += length;
            }
            _ => {
                let raw = varint(patch, &mut at)?;
                let offset = if raw & 1 != 0 { -((raw >> 1) as i64) } else { (raw >> 1) as i64 };
                if command == 2 {
                    source_rel += offset;
                    let start = usize::try_from(source_rel).map_err(|_| bad_bps("SourceCopy"))?;
                    target.extend_from_slice(source.get(start..start + length).ok_or_else(|| bad_bps("SourceCopy"))?);
                    source_rel += length as i64;
                } else {
                    target_rel += offset;
                    let mut from = usize::try_from(target_rel).map_err(|_| bad_bps("TargetCopy"))?;
                    if from >= target.len() {
                        return Err(bad_bps("TargetCopy"));
                    }
                    for _ in 0..length {
                        target.push(target[from]);
                        from += 1;
                    }
                    target_rel += length as i64;
                }
            }
        }
    }
    if at != end || target.len() != target_size || crate::zip::crc32(&target) != trailer(patch, 8) {
        return Err(bad_bps("target mismatch"));
    }
    Ok(target)
}

// ----------------------------------------------------------------- recipe --
fn text<'a>(value: &'a Value, key: &str) -> Result<&'a str> {
    value[key].as_str().ok_or_else(|| invalid(format!("Invalid Restoration layout recipe: missing {key}")))
}
/// A package-relative resource path the recipe may name: under `layout/`,
/// no empty, `.` or `..` component, no drive or backslash.
fn resource_path(relative: &str) -> Result<&str> {
    let ok = relative.starts_with("layout/")
        && !relative.contains(['\\', ':'])
        && relative.split('/').all(|c| !c.is_empty() && c != "." && c != "..");
    if ok { Ok(relative) } else { Err(invalid(format!("Invalid Restoration layout resource {relative}"))) }
}

/// Rebuild the recipe's outputs (`layout/...` -> bytes). `dump` reads one
/// file of the user's RomFS (None when absent); `resource` one package file
/// of the recipe's folder.
pub fn reconstruct(
    recipe: &Value,
    dump: &mut dyn FnMut(&str) -> Result<Option<Vec<u8>>>,
    resource: &mut dyn FnMut(&str) -> Result<Vec<u8>>,
) -> Result<BTreeMap<String, Vec<u8>>> {
    if recipe["format"] != FORMAT {
        return Err(invalid("Unknown Project Restoration layout recipe; reinstall the package"));
    }
    let mut files: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    let mut archives: BTreeMap<String, Vec<(String, Vec<u8>)>> = BTreeMap::new();
    let mut dump_member = |reference: &str| -> Result<Vec<u8>> {
        let (archive, member) = reference.split_once('#').unwrap_or((reference, ""));
        if !files.contains_key(archive) {
            let bytes = dump(archive)?.ok_or_else(|| {
                invalid(format!(
                    "The game dump has no {archive}. Moonfall Curse needs the EUR CTR-P-AJRP dump; dump the game again."
                ))
            })?;
            files.insert(archive.to_owned(), bytes);
        }
        let bytes = &files[archive];
        if member.is_empty() {
            return Ok(bytes.clone());
        }
        if !archives.contains_key(archive) {
            let gar = parse_gar(bytes)
                .map_err(|_| invalid(format!("The game dump's {archive} is not the EUR release archive; dump the game again.")))?;
            archives.insert(archive.to_owned(), gar.members);
        }
        archives[archive].iter().find(|(n, _)| n == member).map(|(_, b)| b.clone()).ok_or_else(|| {
            invalid(format!("The game dump's {archive} has no member {member}; dump the EUR game again."))
        })
    };
    let mut produce = |source: &Value| -> Result<Vec<u8>> {
        let (data, what) = if let Some(reference) = source["dump"].as_str() {
            (dump_member(reference)?, reference.to_owned())
        } else if let Some(patch) = source["patch"].as_str() {
            let base_name = text(source, "base")?;
            let base = dump_member(base_name)?;
            if sha256_hex(&base) != text(source, "base_sha256")? {
                return Err(invalid(format!(
                    "The game dump's {base_name} differs from the EUR release; Moonfall Curse needs an unmodified EUR CTR-P-AJRP dump."
                )));
            }
            (bps_apply(&resource(resource_path(patch)?)?, &base)?, base_name.to_owned())
        } else if let Some(ship) = source["ship"].as_str() {
            (resource(resource_path(ship)?)?, ship.to_owned())
        } else {
            return Err(invalid("Unknown Restoration layout member source"));
        };
        if sha256_hex(&data) != text(source, "sha256")? {
            return Err(invalid(format!(
                "Restoration layout member {what} does not match; dump the EUR game again or reinstall the package."
            )));
        }
        Ok(data)
    };
    let mut result = BTreeMap::new();
    for output in recipe["outputs"].as_array().ok_or_else(|| invalid("Invalid Restoration layout recipe"))? {
        let path = resource_path(text(output, "path")?)?;
        let data = if output["gar"].is_object() {
            let gar = &output["gar"];
            if gar["version"] != 2 {
                return Err(invalid("Unsupported GAR version in the Restoration layout recipe"));
            }
            let mut members = Vec::new();
            for member in output["members"].as_array().ok_or_else(|| invalid("Invalid Restoration layout recipe"))? {
                members.push((text(member, "name")?.to_owned(), produce(&member["source"])?));
            }
            emit_gar(&Gar {
                hash: text(gar, "hash")?.to_owned(),
                types: gar["types"]
                    .as_array()
                    .ok_or_else(|| invalid("Invalid Restoration layout recipe"))?
                    .iter()
                    .map(|t| t.as_str().map(str::to_owned).ok_or_else(|| invalid("Invalid GAR type")))
                    .collect::<Result<_>>()?,
                align: gar["align"].as_u64().filter(|a| (1..=4096).contains(a)).ok_or_else(|| invalid("Invalid GAR align"))?
                    as usize,
                members,
            })?
        } else {
            produce(&output["source"])?
        };
        if sha256_hex(&data) != text(output, "sha256")? {
            return Err(invalid(format!("Reconstructed {path} does not match the Restoration layout recipe")));
        }
        result.insert(path.to_owned(), data);
    }
    Ok(result)
}

fn read_limited(path: &Path) -> Result<Vec<u8>> {
    let size = std::fs::metadata(path).map_err(|e| ForgeError::io(format!("Cannot read {}", path.display()), e))?.len();
    if size > LIMIT {
        return Err(invalid("Restoration layout resource exceeds the preparation limit"));
    }
    std::fs::read(path).map_err(|e| ForgeError::io(format!("Cannot read {}", path.display()), e))
}

/// The recipe in `resources` (the package's Restoration folder) followed
/// against the user's RomFS image.
pub fn reconstruct_package(resources: &Path, romfs_image: &Path) -> Result<BTreeMap<String, Vec<u8>>> {
    let missing = |_| {
        invalid(format!(
            "The Moonfall Curse package is missing its Project Restoration layout recipe at {}; reinstall the package.",
            resources.display()
        ))
    };
    let recipe: Value = serde_json::from_slice(&read_limited(&resources.join(RECIPE)).map_err(missing)?)
        .map_err(|_| invalid("Invalid Project Restoration layout recipe; reinstall the package."))?;
    reconstruct(&recipe, &mut |name| romfs::read_level3_file(romfs_image, name), &mut |relative| {
        read_limited(&resources.join(relative)).map_err(|_| {
            invalid(format!(
                "The Moonfall Curse package is missing its Project Restoration resource {relative}; reinstall the package."
            ))
        })
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn crc(data: &[u8]) -> [u8; 4] {
        crate::zip::crc32(data).to_le_bytes()
    }
    fn gar(members: &[(&str, &[u8])]) -> Vec<u8> {
        emit_gar(&Gar {
            hash: "jenkins".into(),
            types: ["unknown", "mfpk", "mfl", "ctxb"].map(String::from).to_vec(),
            align: 4,
            members: members.iter().map(|(n, b)| (n.to_string(), b.to_vec())).collect(),
        })
        .unwrap()
    }
    /// Python-side vector (tools/mm3d/test_restoration_layout.py): TargetRead
    /// "ab", SourceRead 2.
    fn hand_patch(source: &[u8], target: &[u8]) -> Vec<u8> {
        let mut body = b"BPS1".to_vec();
        body.extend([0x84, 0x84, 0x80, 0x85, b'a', b'b', 0x84]);
        body.extend(crc(source));
        body.extend(crc(target));
        let c = crc(&body);
        body.extend(c);
        body
    }

    #[test]
    fn gar_round_trip_and_negatives() {
        let members: &[(&str, &[u8])] =
            &[("A.mfpk", &[1; 64]), ("B.mfl", &[2; 16]), ("C.mfl", &[3; 32]), ("t.ctxb", &[4; 32])];
        let bytes = gar(members);
        let parsed = parse_gar(&bytes).unwrap();
        assert_eq!(parsed.members.len(), 4);
        assert_eq!(emit_gar(&parsed).unwrap(), bytes);
        let mut padded = bytes.clone();
        padded.extend([0; 4]);
        let n = padded.len() as u32;
        padded[4..8].copy_from_slice(&n.to_le_bytes());
        assert!(parse_gar(&padded).unwrap_err().to_string().contains("canonical"));
        assert!(parse_gar(b"GAR\x01").is_err());
    }

    #[test]
    fn bps_vectors_and_crc_negative() {
        assert_eq!(bps_apply(&hand_patch(b"xxyy", b"abyy"), b"xxyy").unwrap(), b"abyy");
        // Source mismatch, corrupted patch, wrong magic.
        assert!(bps_apply(&hand_patch(b"xxyy", b"abyy"), b"xxyz").unwrap_err().to_string().contains("source mismatch"));
        let mut corrupt = hand_patch(b"xxyy", b"abyy");
        corrupt[9] ^= 1;
        assert!(bps_apply(&corrupt, b"xxyy").unwrap_err().to_string().contains("checksum"));
        assert!(bps_apply(b"UPS1\0\0\0\0\0\0\0\0\0\0\0\0", b"").is_err());
        // A wrong target CRC with a consistent patch checksum.
        let mut body = hand_patch(b"xxyy", b"abyz");
        body.truncate(body.len() - 4);
        let c = crc(&body);
        body.extend(c);
        assert!(bps_apply(&body, b"xxyy").unwrap_err().to_string().contains("target mismatch"));
    }

    #[test]
    fn bps_copies() {
        // SourceCopy 4 from offset 4 ((4<<1)=8 -> 0x88), TargetCopy 4 from 0.
        let source = b"0123abcd";
        let target = b"abcdabcd";
        let mut body = b"BPS1".to_vec();
        body.extend([0x88, 0x88, 0x80, ((3 << 2) | 2) | 0x80, 0x88, ((3 << 2) | 3) | 0x80, 0x80]);
        body.extend(crc(source));
        body.extend(crc(target));
        let c = crc(&body);
        body.extend(c);
        assert_eq!(bps_apply(&body, source).unwrap(), target);
    }

    fn recipe(play_sha: &str, mod_base: &[u8], modified: &[u8], own: &[u8], new_sha: &str) -> Value {
        json!({"format": FORMAT, "outputs": [{
            "path": "layout/Joker.Main.New.gar", "sha256": new_sha,
            "gar": {"version": 2, "hash": "jenkins", "types": ["unknown", "mfpk", "mfl", "ctxb"], "align": 4},
            "members": [
                {"name": "Play.mfpk", "source": {"dump": "layout/Joker.Main.Play.gar#Play.mfpk", "sha256": play_sha}},
                {"name": "Mod.mfl", "source": {"patch": "layout/patches/Mod.bps", "base": "layout/Joker.Main.Play.gar#Mod.mfl",
                    "base_sha256": sha256_hex(mod_base), "sha256": sha256_hex(modified)}},
                {"name": "Own.mfl", "source": {"ship": "layout/members/Own.mfl", "sha256": sha256_hex(own)}}]}]})
    }

    #[test]
    fn reconstruction_and_user_facing_errors() {
        let (play, base) = (vec![7u8; 16], b"xxyy".to_vec());
        let (modified, own) = (b"abyy".to_vec(), vec![9u8; 8]);
        let dump_gar = gar(&[("Play.mfpk", &play), ("Mod.mfl", &base)]);
        let expected = gar(&[("Play.mfpk", &play), ("Mod.mfl", &modified), ("Own.mfl", &own)]);
        let r = recipe(&sha256_hex(&play), &base, &modified, &own, &sha256_hex(&expected));
        let patch = hand_patch(&base, &modified);
        let mut resource = |p: &str| -> Result<Vec<u8>> {
            Ok(match p {
                "layout/patches/Mod.bps" => patch.clone(),
                "layout/members/Own.mfl" => own.clone(),
                _ => return Err(invalid("missing")),
            })
        };
        let out = reconstruct(&r, &mut |_| Ok(Some(dump_gar.clone())), &mut resource).unwrap();
        assert_eq!(out["layout/Joker.Main.New.gar"], expected);
        // Missing dump file, missing member, changed member, changed base.
        let error = reconstruct(&r, &mut |_| Ok(None), &mut resource).unwrap_err().to_string();
        assert!(error.contains("has no layout/Joker.Main.Play.gar") && error.contains("EUR"), "{error}");
        let partial = gar(&[("Play.mfpk", &play)]);
        let error = reconstruct(&r, &mut |_| Ok(Some(partial.clone())), &mut resource).unwrap_err().to_string();
        assert!(error.contains("has no member Mod.mfl"), "{error}");
        let changed = gar(&[("Play.mfpk", &[8u8; 16]), ("Mod.mfl", &base)]);
        let error = reconstruct(&r, &mut |_| Ok(Some(changed.clone())), &mut resource).unwrap_err().to_string();
        assert!(error.contains("Play.mfpk does not match"), "{error}");
        let other_base = gar(&[("Play.mfpk", &play), ("Mod.mfl", b"xxyz")]);
        let error = reconstruct(&r, &mut |_| Ok(Some(other_base.clone())), &mut resource).unwrap_err().to_string();
        assert!(error.contains("differs from the EUR release"), "{error}");
        // A recipe may not name a path outside its folder.
        let mut escape = r.clone();
        escape["outputs"][0]["members"][2]["source"]["ship"] = json!("layout/../../x");
        assert!(reconstruct(&escape, &mut |_| Ok(Some(dump_gar.clone())), &mut resource).is_err());
        assert!(reconstruct(&json!({"format": "other"}), &mut |_| Ok(None), &mut resource).is_err());
    }

    /// The committed recipe names the three fingerprinted Restoration inputs.
    #[test]
    fn committed_recipe_matches_the_fingerprints() {
        let recipe: Value =
            serde_json::from_str(include_str!("../../../../../titles/mm3d/restoration/package/layout.recipe.json")).unwrap();
        let fingerprints: Value =
            serde_json::from_str(include_str!("../../../../../titles/mm3d/dekui/setup/setup-fingerprints.json")).unwrap();
        let mut expected: Vec<(String, String)> = fingerprints["files"]
            .as_array()
            .unwrap()
            .iter()
            .filter(|e| e["input_root"] == "restoration" && e["input"].as_str().unwrap().starts_with("romfs/"))
            .map(|e| (e["input"].as_str().unwrap()[6..].to_owned(), e["input_sha256"].as_str().unwrap().to_owned()))
            .collect();
        expected.sort();
        expected.dedup();
        let mut actual: Vec<(String, String)> = recipe["outputs"]
            .as_array()
            .unwrap()
            .iter()
            .map(|o| (o["path"].as_str().unwrap().to_owned(), o["sha256"].as_str().unwrap().to_owned()))
            .collect();
        actual.sort();
        assert_eq!(actual, expected);
    }
}
