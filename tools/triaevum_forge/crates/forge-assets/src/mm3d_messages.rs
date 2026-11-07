// SPDX-License-Identifier: GPL-3.0-or-later
//! Native GMSG/control compiler. Specifications contain reviewed replacement
//! wording; original text and all byte guards are read from the local RomFS.
use crate::fnv::fnv1a64;
use forge_base::hashing::{hex, sha256_hex};
use forge_base::{ForgeError, Result};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};
fn bad(message: &str) -> ForgeError {
    ForgeError::invalid(format!("MM messages: {message}"))
}
fn word(b: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(b.get(at..at + 2).ok_or_else(|| bad("truncated u16"))?.try_into().unwrap()))
}
fn dword(b: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(b.get(at..at + 4).ok_or_else(|| bad("truncated u32"))?.try_into().unwrap()))
}
fn number(v: &Value, max: u64) -> Result<u64> {
    v.as_u64().filter(|n| *n <= max).ok_or_else(|| bad("integer outside range"))
}
fn string(v: &Value) -> Result<&str> {
    v.as_str().ok_or_else(|| bad("expected string"))
}
#[derive(Clone, PartialEq, Debug)]
enum Token {
    Text(String),
    Control(u16, Vec<u8>),
}
fn decode(raw: &[u8]) -> Result<Vec<Token>> {
    let mut out = Vec::new();
    let mut at = 0;
    while at < raw.len() {
        if raw[at] != 127 {
            let end =
                raw[at..].iter().position(|b| *b == 127).map(|n| at + n).ok_or_else(|| bad("unterminated message"))?;
            out.push(Token::Text(std::str::from_utf8(&raw[at..end]).map_err(|_| bad("invalid UTF8"))?.into()));
            at = end;
            continue;
        }
        at += 1;
        if at % 2 != 0 {
            if raw.get(at) != Some(&0) {
                return Err(bad("control alignment"));
            }
            at += 1;
        }
        let cmd = word(raw, at)?;
        at += 2;
        let count = match cmd {
            0 | 1 | 2 | 0x1e | 0x27 | 0x28 | 0x2c | 0x34 | 0x38 => 0,
            0x25 | 0x26 | 0x29 | 0x2b | 0x2f | 0x31 | 0x3a => 2,
            0x32 => {
                if at != 4 {
                    if raw.get(at..at + 2) != Some(&[0, 0]) {
                        return Err(bad("extended alignment"));
                    }
                    at += 2;
                }
                4
            }
            0x20 => {
                let mut end = at + 2;
                word(raw, at)?;
                for _ in 0..2 {
                    end += raw
                        .get(end..)
                        .ok_or_else(|| bad("plural"))?
                        .iter()
                        .position(|b| *b == 0)
                        .ok_or_else(|| bad("plural terminator"))?
                        + 1;
                }
                end - at
            }
            _ => return Err(bad(&format!("unsupported control {cmd:02x}"))),
        };
        let args = raw.get(at..at + count).ok_or_else(|| bad("truncated control"))?.to_vec();
        at += count;
        if cmd == 0x20 && at % 2 != 0 {
            if raw.get(at) != Some(&0) {
                return Err(bad("plural alignment"));
            }
            at += 1;
        }
        out.push(Token::Control(cmd, args));
        if cmd == 0 {
            return Ok(out);
        }
    }
    Err(bad("missing terminator"))
}
fn encode(tokens: &[Token]) -> Vec<u8> {
    let mut out = Vec::new();
    for token in tokens {
        match token {
            Token::Text(text) => out.extend(text.as_bytes()),
            Token::Control(cmd, args) => {
                out.push(127);
                if out.len() % 2 != 0 {
                    out.push(0);
                }
                out.extend(cmd.to_le_bytes());
                if *cmd == 0x32 && out.len() != 4 {
                    out.extend([0, 0]);
                }
                out.extend(args);
                if *cmd == 0x20 && out.len() % 2 != 0 {
                    out.push(0);
                }
            }
        }
    }
    out
}
fn markup(text: &str) -> Result<Vec<Token>> {
    if text.contains(['\0', '\x7f']) {
        return Err(bad("replacement introduces control bytes"));
    }
    let mut out = Vec::new();
    let mut rest = text;
    while !rest.is_empty() {
        let at = rest.find(['{', '\n']).unwrap_or(rest.len());
        if at > 0 {
            out.push(Token::Text(rest[..at].into()));
            rest = &rest[at..];
            continue;
        }
        if rest.starts_with('\n') {
            out.push(Token::Control(1, vec![]));
            rest = &rest[1..];
            continue;
        }
        let end = rest.find('}').ok_or_else(|| bad("unterminated markup"))?;
        let name = &rest[1..end];
        let (cmd, value) = if let Some(color) = name.strip_prefix("Color:") {
            (0x3a, color.parse::<u16>().map_err(|_| bad("color"))?)
        } else {
            (
                0x25,
                match name {
                    "A" => 1,
                    "B" => 2,
                    "X" => 3,
                    "Y" => 4,
                    "L" => 5,
                    "R" => 6,
                    "Dpad" => 10,
                    "Stick" => 17,
                    _ => return Err(bad("unknown icon")),
                },
            )
        };
        out.push(Token::Control(cmd, value.to_le_bytes().to_vec()));
        rest = &rest[end + 1..];
    }
    Ok(out)
}
fn flow(tokens: &[Token]) -> Vec<Token> {
    tokens.iter().filter(|t| matches!(t,Token::Control(c,_) if ![1,0x25,0x38,0x3a].contains(c))).cloned().collect()
}
fn replace(tokens: &mut Vec<Token>, rule: &Value) -> Result<()> {
    let selected = if let Some(section) = rule.get("section") {
        let wanted = number(section, 20000)? as usize;
        let mut starts = Vec::new();
        let mut open = true;
        for (i, token) in tokens.iter().enumerate() {
            match token {
                Token::Text(_) if open => {
                    starts.push(i);
                    open = false;
                }
                Token::Control(c, _) if [0x29, 0x31, 2].contains(c) => {
                    open = true;
                }
                _ => (),
            }
        }
        *starts.get(wanted).ok_or_else(|| bad("missing reviewed section"))?
    } else if let Some(segment) = rule.get("segment") {
        let n = number(segment, 20000)? as usize;
        tokens
            .iter()
            .enumerate()
            .filter(|(_, t)| matches!(t, Token::Text(_)))
            .nth(n)
            .map(|(i, _)| i)
            .ok_or_else(|| bad("missing reviewed segment"))?
    } else {
        let needle = string(&rule["start"])?;
        let occurrence = rule.get("occurrence").map(|v| number(v, 20000)).transpose()?.unwrap_or(0) as usize;
        tokens
            .iter()
            .enumerate()
            .filter(|(_, t)| matches!(t,Token::Text(s) if s.contains(needle)))
            .nth(occurrence)
            .map(|(i, _)| i)
            .ok_or_else(|| bad("reviewed text does not match"))?
    };
    let prefix = match &tokens[selected] {
        Token::Text(s) => {
            if rule.get("segment").is_some() || rule.get("section").is_some() {
                String::new()
            } else {
                s.split_once(string(&rule["start"])?).unwrap().0.into()
            }
        }
        _ => unreachable!(),
    };
    let mut end = selected + 1;
    while end < tokens.len() && !matches!(&tokens[end],Token::Control(c,_) if [0,2,0x2b,0x31].contains(c)) {
        end += 1;
    }
    let mut replacement = markup(string(&rule["text"])?)?;
    if !prefix.is_empty() {
        if let Some(Token::Text(first)) = replacement.first_mut() {
            first.insert_str(0, &prefix);
        } else {
            replacement.insert(0, Token::Text(prefix));
        }
    }
    tokens.splice(selected..end, replacement);
    Ok(())
}
pub struct Message<'a> {
    pub bytes: &'a [u8],
    pub metadata: u32,
}
pub fn archive(data: &[u8], mask: u32) -> Result<BTreeMap<u16, Message<'_>>> {
    if data.len() > 16 * 1024 * 1024
        || data.get(..8) != Some(b"GMSG\x01\0\0\0")
        || dword(data, 12)? != mask
        || mask.count_ones() != 1
        || mask >= 1 << 14
    {
        return Err(bad("invalid GMSG language/header"));
    }
    let count = dword(data, 8)? as usize;
    if count == 0 || count > 20000 || 16 + 20 * count > data.len() {
        return Err(bad("invalid table"));
    }
    let mut out = BTreeMap::new();
    let mut extents = Vec::new();
    let mut previous = None;
    for i in 0..count {
        let at = 16 + i * 20;
        let id = word(data, at)?;
        let offset = dword(data, at + 12)? as usize;
        let size = dword(data, at + 16)? as usize;
        if previous.is_some_and(|p| id <= p) || offset < 16 + 20 * count || offset % 2 != 0 || size == 0 {
            return Err(bad("invalid record"));
        }
        let bytes = data
            .get(offset..offset.checked_add(size).ok_or_else(|| bad("overflow"))?)
            .ok_or_else(|| bad("allocation outside archive"))?;
        previous = Some(id);
        extents.push((offset, offset + size));
        out.insert(id, Message { bytes, metadata: dword(data, at + 4)? });
    }
    extents.sort();
    if extents.windows(2).any(|w| w[0].1 > w[1].0) {
        return Err(bad("overlapping messages"));
    }
    Ok(out)
}
fn message_id(rule: &Value) -> Result<u16> {
    u16::from_str_radix(string(&rule["id"])?, 16).map_err(|_| bad("message id"))
}
/// Exact native allocation and control flow are preserved; any unmatched rule
/// aborts the entire publication, never silently leaves one locale unfinished.
#[cfg(test)]
fn compile(data: &[u8], mask: u32, spec: &Value) -> Result<Vec<Value>> {
    compile_inner(data, mask, spec, false)
}
fn compile_inner(data: &[u8], mask: u32, spec: &Value, grow: bool) -> Result<Vec<Value>> {
    let native = archive(data, mask)?;
    let mut grouped: BTreeMap<u16, Vec<&Value>> = BTreeMap::new();
    if let Some(edits) = spec.get("edits") {
        if !spec["language_masks"].as_array().is_some_and(|a| a.contains(&json!(mask))) {
            return Err(bad("dialogue spec language mismatch"));
        }
        for rule in edits.as_array().ok_or_else(|| bad("edits must be an array"))? {
            grouped.entry(message_id(rule)?).or_default().push(rule);
        }
    }
    let mut out = Vec::new();
    let mut seen = BTreeSet::new();
    let row = |id: u16, raw: &[u8], edited: Vec<u8>, price: bool| {
        json!({"id":id,"language":mask.trailing_zeros(),"price":price,
        "original":hex(raw),"edited":hex(&edited),"original_hash":format!("{:016x}",fnv1a64(raw)),"edited_hash":format!("{:016x}",fnv1a64(&edited))})
    };
    for (id, rules) in grouped {
        let source = native.get(&id).ok_or_else(|| bad("missing dialogue message"))?;
        let original = decode(source.bytes)?;
        let mut tokens = original.clone();
        for rule in rules {
            replace(&mut tokens, rule)?;
        }
        if flow(&tokens) != flow(&original) {
            return Err(bad("dialogue flow changed"));
        }
        let mut edited = encode(&tokens);
        if !grow && edited.len() > source.bytes.len() {
            return Err(bad(&format!("{id:04x}: dialogue allocation {}/{}", edited.len(), source.bytes.len())));
        }
        if decode(&edited)? != tokens {
            return Err(bad(&format!("{id:04x}: dialogue control round trip")));
        }
        edited.resize(edited.len().max(source.bytes.len()), 0);
        out.push(row(id, source.bytes, edited, false));
        seen.insert(id);
    }
    if let Some(prices) = spec.get("prices") {
        if spec["language_mask"] != mask {
            return Err(bad("price spec language mismatch"));
        }
        for rule in prices.as_array().ok_or_else(|| bad("prices must be an array"))? {
            let id = message_id(rule)?;
            if !seen.insert(id) {
                return Err(bad("duplicate/overlapping message owner"));
            }
            let source = native.get(&id).ok_or_else(|| bad("missing price message"))?;
            if string(&rule["original_sha256"])? != sha256_hex(source.bytes)
                || number(&rule["original_metadata"], u32::MAX as u64)? != source.metadata as u64
            {
                return Err(bad("unreviewed price source"));
            }
            let mut tokens = decode(source.bytes)?;
            let mut metadata = source.metadata;
            if let Some(replacements) = rule.get("replacements") {
                for replacement in replacements.as_array().ok_or_else(|| bad("price replacements"))? {
                    let old = string(&replacement["old"])?;
                    let new = string(&replacement["new"])?;
                    if old.is_empty() || new.contains(['\0', '\x7f']) {
                        return Err(bad("invalid price replacement"));
                    }
                    let matches: usize =
                        tokens.iter().map(|t| if let Token::Text(t) = t { t.matches(old).count() } else { 0 }).sum();
                    if matches != 1 {
                        return Err(bad("price replacement must match exactly once"));
                    }
                    for t in &mut tokens {
                        if let Token::Text(t) = t {
                            *t = t.replace(old, new);
                        }
                    }
                }
            }
            if let Some(fields) = rule.get("double_fields") {
                for shift in fields.as_array().ok_or_else(|| bad("price fields"))? {
                    let shift = number(shift, 20)? as u32;
                    if ![0, 10, 20].contains(&shift) {
                        return Err(bad("price field offset"));
                    }
                    let value = (source.metadata >> shift) & 1023;
                    if value == 1023 {
                        continue;
                    }
                    if value * 2 >= 1023 {
                        return Err(bad("price overflow"));
                    }
                    metadata = (metadata & !(1023 << shift)) | ((value * 2) << shift);
                }
            }
            let mut edited = encode(&tokens);
            if (!grow && edited.len() > source.bytes.len()) || decode(&edited)? != tokens {
                return Err(bad("price allocation/control mismatch"));
            }
            edited.resize(edited.len().max(source.bytes.len()), 0);
            let mut item = row(id, source.bytes, edited, true);
            item["original_metadata"] = json!(source.metadata);
            item["edited_metadata"] = json!(metadata);
            out.push(item);
        }
    }
    Ok(out)
}
fn unhex(value: &Value) -> Result<Vec<u8>> {
    let s = string(value)?;
    if s.len() % 2 != 0 {
        return Err(bad("odd hex extent"));
    }
    s.as_bytes()
        .chunks_exact(2)
        .map(|p| {
            let a = (p[0] as char).to_digit(16).ok_or_else(|| bad("hex digit"))?;
            let b = (p[1] as char).to_digit(16).ok_or_else(|| bad("hex digit"))?;
            Ok((a * 16 + b) as u8)
        })
        .collect()
}
fn ids(plan: &Value, field: &str) -> Result<BTreeSet<u16>> {
    plan[field]
        .as_array()
        .ok_or_else(|| bad("price plan list"))?
        .iter()
        .map(|v| u16::from_str_radix(string(v)?, 16).map_err(|_| bad("price plan id")))
        .collect()
}
fn currency(s: &str) -> bool {
    let word = s.trim_start().to_lowercase();
    ["rupee", "rubin", "rubis", "rupia", "rupie", "rrrupia"].iter().any(|c| word.starts_with(c))
}
fn suffix(tokens: &[Token], index: usize, rest: &str) -> String {
    let mut out = rest.to_owned();
    for token in &tokens[index + 1..] {
        match token {
            Token::Text(text) => {
                out.push_str(text);
                if out.len() > 80 {
                    break;
                }
            }
            Token::Control(0x3a, _) | Token::Control(0x38, _) => (),
            Token::Control(0x20, args) => {
                if let Some(bytes) = args.get(2..) {
                    if let Some(end) = bytes.iter().position(|b| *b == 0) {
                        out.push_str(&String::from_utf8_lossy(&bytes[..end]));
                    }
                }
                break;
            }
            _ => break,
        }
    }
    out
}
fn price_tokens(tokens: &[Token], special: Option<&Value>) -> Result<(Vec<Token>, usize)> {
    let mut out = tokens.to_vec();
    let mut changed = 0;
    for (index, token) in tokens.iter().enumerate() {
        let Token::Text(text) = token else { continue };
        let mut result = String::new();
        let bytes = text.as_bytes();
        let mut at = 0;
        let mut copied = 0;
        while at < bytes.len() {
            if !bytes[at].is_ascii_digit() {
                at += 1;
                continue;
            }
            let start = at;
            while at < bytes.len() && (bytes[at].is_ascii_digit() || [b',', b'.'].contains(&bytes[at])) {
                at += 1;
            }
            let mut end = at;
            while end > start && !bytes[end - 1].is_ascii_digit() {
                end -= 1;
            }
            if start > 0 && text[..start].chars().next_back().is_some_and(|c| c.is_alphanumeric()) {
                continue;
            }
            if !currency(&suffix(tokens, index, &text[end..])) {
                continue;
            }
            let old = &text[start..end];
            let digits: String = old.chars().filter(char::is_ascii_digit).collect();
            let n: u32 = digits.parse().map_err(|_| bad("price numeral overflow"))?;
            let new = if let Some(special) = special {
                match special.get(&digits) {
                    Some(v) => string(v)?.to_owned(),
                    None => continue,
                }
            } else {
                n.checked_mul(2).ok_or_else(|| bad("price numeral overflow"))?.to_string()
            };
            let new = if let Some(separator) = old.chars().find(|c| *c == ',' || *c == '.') {
                let mut grouped = String::new();
                for (i, c) in new.chars().enumerate() {
                    if i > 0 && (new.len() - i) % 3 == 0 {
                        grouped.push(separator);
                    }
                    grouped.push(c);
                }
                grouped
            } else {
                new
            };
            result.push_str(&text[copied..start]);
            result.push_str(&new);
            copied = end;
            changed += 1;
        }
        result.push_str(&text[copied..]);
        out[index] = Token::Text(result);
    }
    Ok((out, changed))
}
fn planned_prices(data: &[u8], mask: u32) -> Result<Vec<Value>> {
    let plan: Value = serde_json::from_str(include_str!("../../../../../titles/mm3d/dekui/setup/price-plan.json"))
        .map_err(|_| bad("embedded price plan"))?;
    let metadata = ids(&plan, "metadata_ids")?;
    let literal = ids(&plan, "literal_ids")?;
    let exempt = ids(&plan, "exempt_ids")?;
    let mut all: BTreeSet<_> = metadata.union(&literal).copied().collect();
    for id in plan["special_literals"].as_object().ok_or_else(|| bad("special price plan"))?.keys() {
        all.insert(u16::from_str_radix(id, 16).map_err(|_| bad("special id"))?);
    }
    if !all.is_disjoint(&exempt) {
        return Err(bad("price plan touches exempt message"));
    }
    let native = archive(data, mask)?;
    let mut out = Vec::new();
    for id in all {
        let source = native.get(&id).ok_or_else(|| bad("missing planned price message"))?;
        let tokens = decode(source.bytes).map_err(|e| bad(&format!("price {id:04x}: {}", e.message)))?;
        let special = plan["special_literals"].get(format!("{id:04x}"));
        let (edited_tokens, changed) = if mask == 32 && id == 0x0a2a {
            // This native German line spells twenty as well as showing (20).
            // Use our own numeric wording; never leave the spoken amount stale.
            let mut edited = tokens.clone();
            let mut count = 0;
            for token in &mut edited {
                if let Token::Text(text) = token {
                    if text.contains("(20)") && text.contains("Rubine") {
                        *text = "40 Rubine".into();
                        count += 1;
                    }
                }
            }
            if count != 1 {
                return Err(bad("German 0a2a price wording changed"));
            }
            (edited, count)
        } else if literal.contains(&id) || special.is_some() {
            price_tokens(&tokens, special)?
        } else {
            (tokens.clone(), 0)
        };
        let dynamic_only = [0x0670, 0x109b, 0x1d03, 0x1d04, 0x278e, 0x279a, 0x2b5d, 0x3471, 0x3478, 0x3528];
        if (literal.contains(&id) || special.is_some()) && changed == 0 && !dynamic_only.contains(&id) {
            return Err(bad(&format!("price {id:04x}: no reviewed currency literal matched")));
        }
        let mut after = source.metadata;
        if metadata.contains(&id) {
            for shift in [0, 10, 20] {
                let n = (source.metadata >> shift) & 1023;
                if n == 1023 {
                    continue;
                }
                if n * 2 >= 1023 {
                    return Err(bad("planned price metadata overflow"));
                }
                after = (after & !(1023 << shift)) | ((n * 2) << shift);
            }
        }
        let edited = encode(&edited_tokens);
        if decode(&edited)? != edited_tokens || flow(&tokens) != flow(&edited_tokens) {
            return Err(bad("price changed native flow"));
        }
        out.push(json!({"id":id,"language":mask.trailing_zeros(),"price":true,"literal_changes":changed,"original":hex(source.bytes),"edited":hex(&edited),"original_metadata":source.metadata,"edited_metadata":after}));
    }
    Ok(out)
}
/// Rebuild native allocation records before runtime installation. This avoids
/// shortening/translating native price prose merely because a doubled numeral
/// gains a digit. Both original and edited variants fit the same padded slot;
/// the guest starts with original wording/metadata and the existing toggles
/// remain the only owners of whether an edit is applied.
pub fn prepare(data: &[u8], mask: u32, spec: &Value) -> Result<(Vec<u8>, Vec<Value>)> {
    let mut rows = compile_inner(data, mask, spec, true)?;
    if spec.get("prices").is_none() {
        rows.extend(planned_prices(data, mask)?);
    }
    let mut by_id = BTreeMap::new();
    for (i, row) in rows.iter().enumerate() {
        let id = number(&row["id"], 65535)? as u16;
        if by_id.insert(id, i).is_some() {
            return Err(bad("overlapping dialogue/price owner"));
        }
    }
    let native = archive(data, mask)?;
    let table_end = 16 + native.len() * 20;
    let mut packed = data[..table_end].to_vec();
    for (i, (id, source)) in native.iter().enumerate() {
        if packed.len() % 2 != 0 {
            packed.push(0);
        }
        let offset = packed.len();
        let mut raw = source.bytes.to_vec();
        if let Some(index) = by_id.get(id) {
            let row = &mut rows[*index];
            let mut edited = unhex(&row["edited"])?;
            let size = raw.len().max(edited.len());
            raw.resize(size, 0);
            edited.resize(size, 0);
            row["original"] = json!(hex(&raw));
            row["edited"] = json!(hex(&edited));
            row["original_hash"] = json!(format!("{:016x}", fnv1a64(&raw)));
            row["edited_hash"] = json!(format!("{:016x}", fnv1a64(&edited)));
        }
        packed[16 + i * 20 + 12..16 + i * 20 + 16].copy_from_slice(&(offset as u32).to_le_bytes());
        packed[16 + i * 20 + 16..16 + i * 20 + 20].copy_from_slice(&(raw.len() as u32).to_le_bytes());
        packed.extend(raw);
    }
    archive(&packed, mask)?;
    Ok((packed, rows))
}
#[cfg(test)]
mod tests {
    use super::*;
    fn fixture() -> Vec<u8> {
        let raw = encode(&[Token::Text("Price 10 coins".into()), Token::Control(0, vec![])]);
        let mut b = b"GMSG\x01\0\0\0".to_vec();
        for v in [1u32, 16] {
            b.extend(v.to_le_bytes());
        }
        b.extend(62u16.to_le_bytes());
        b.extend([0, 0]);
        for v in [0xfffffc0au32, 0, 36, (raw.len() + 32) as u32] {
            b.extend(v.to_le_bytes());
        }
        b.extend(raw);
        b.extend([0; 32]);
        b
    }
    #[test]
    fn dialogue_and_price_contracts() {
        let data = fixture();
        let spec = json!({"language_masks":[16],"edits":[{"id":"003e","segment":0,"text":"Choose {A}"}]});
        assert_eq!(compile(&data, 16, &spec).unwrap().len(), 1);
        assert!(compile(&data, 32, &spec).is_err());
        let raw = archive(&data, 16).unwrap()[&62].bytes;
        let spec = json!({"language_mask":16,"prices":[{"id":"003e","original_sha256":sha256_hex(raw),"original_metadata":0xfffffc0au32,"replacements":[{"old":"10","new":"20"}],"double_fields":[0,10,20]}]});
        let out = compile(&data, 16, &spec).unwrap();
        assert_eq!(out[0]["edited_metadata"], 0xfffffc14u32);
    }
    #[test]
    fn codec_and_truncation() {
        let t = vec![
            Token::Text("hello".into()),
            Token::Control(0x20, vec![1, 0, b'a', 0, b'b', 0]),
            Token::Control(0x32, 42u32.to_le_bytes().to_vec()),
            Token::Control(0, vec![]),
        ];
        assert_eq!(decode(&encode(&t)).unwrap(), t);
        let data = fixture();
        for n in 0..36 {
            assert!(archive(&data[..n], 16).is_err());
        }
    }
}
