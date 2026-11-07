// SPDX-License-Identifier: GPL-3.0-or-later
//! Native implementation of the fingerprint-guarded MM GAR/MFL preparation.
//! Algorithm attribution: tgacquin/mm3d-dekui; see titles/mm3d/dekui/setup.
use forge_base::{ForgeError, Result};
use std::collections::BTreeMap;

fn bad() -> ForgeError {
    ForgeError::invalid("Invalid MM GAR/MFL layout")
}
fn part(b: &[u8], at: usize, n: usize) -> Result<&[u8]> {
    b.get(at..at.checked_add(n).ok_or_else(bad)?).ok_or_else(bad)
}
fn u16at(b: &[u8], at: usize) -> Result<usize> {
    Ok(u16::from_le_bytes(part(b, at, 2)?.try_into().unwrap()) as usize)
}
fn u32at(b: &[u8], at: usize) -> Result<usize> {
    Ok(u32::from_le_bytes(part(b, at, 4)?.try_into().unwrap()) as usize)
}
fn put(b: &mut [u8], at: usize, bytes: &[u8]) -> Result<()> {
    b.get_mut(at..at.checked_add(bytes.len()).ok_or_else(bad)?).ok_or_else(bad)?.copy_from_slice(bytes);
    Ok(())
}
fn string(b: &[u8], at: usize) -> Result<String> {
    let rest = b.get(at..).ok_or_else(bad)?;
    let n = rest.iter().position(|x| *x == 0).ok_or_else(bad)?;
    if !rest[..n].is_ascii() {
        return Err(bad());
    }
    Ok(String::from_utf8(rest[..n].to_vec()).map_err(|_| bad())?)
}
struct Entry {
    record: usize,
    address: usize,
    start: usize,
    size: usize,
}
fn entries(b: &[u8]) -> Result<BTreeMap<String, Entry>> {
    if part(b, 0, 4)? != b"GAR\x02" || u32at(b, 4)? != b.len() {
        return Err(bad());
    }
    let (count, table, offsets) = (u16at(b, 10)?, u32at(b, 16)?, u32at(b, 20)?);
    let mut out = BTreeMap::new();
    for i in 0..count {
        let (record, address) = (table + i * 12, offsets + i * 4);
        let (start, size) = (u32at(b, address)?, u32at(b, record)?);
        part(b, start, size)?;
        let name = string(b, u32at(b, record + 8)?)?;
        if out.insert(name, Entry { record, address, start, size }).is_some() {
            return Err(bad());
        }
    }
    Ok(out)
}
struct Animation {
    name: String,
    start: usize,
    end: usize,
    targets: Vec<(usize, String)>,
}
struct Layout {
    widgets: Vec<String>,
    animations: Vec<Animation>,
    start: usize,
    strings: usize,
}
fn layout(b: &[u8]) -> Result<Layout> {
    if part(b, 0, 4)? != b"MFL " || u32at(b, 4)? != 4 {
        return Err(bad());
    }
    let (nodes, panes, players, count) = (u16at(b, 10)?, u16at(b, 14)?, u16at(b, 16)?, u16at(b, 18)?);
    let (start, strings) = (u32at(b, 24)?, u32at(b, 28)?);
    let mut names = Vec::new();
    let mut pos = strings;
    for _ in 0..1 + panes + nodes + players + count {
        let name = string(b, pos)?;
        pos += name.len() + 1;
        names.push(name);
    }
    let widgets = names[1 + panes..1 + panes + nodes].to_vec();
    let mut animations = Vec::new();
    pos = start;
    for name in &names[1 + panes + nodes + players..] {
        let begin = pos;
        let tracks = u16at(b, pos)?;
        if u16at(b, pos + 2)? != 30 || animations.iter().any(|a: &Animation| &a.name == name) {
            return Err(bad());
        }
        pos += 8;
        let mut targets = Vec::new();
        for _ in 0..tracks {
            let (target, keys) = (u16at(b, pos)?, u32at(b, pos + 4)?);
            if target >= nodes || keys > 1024 {
                return Err(bad());
            }
            targets.push((pos - begin, widgets[target].clone()));
            pos += 12 + keys * 12;
            if pos > strings {
                return Err(bad());
            }
        }
        animations.push(Animation { name: name.clone(), start: begin, end: pos, targets });
    }
    if pos != strings {
        return Err(bad());
    }
    Ok(Layout { widgets, animations, start, strings })
}
fn restore_layout(original: &[u8], modified: &[u8]) -> Result<Vec<u8>> {
    let native = layout(original)?;
    let current = layout(modified)?;
    let mut output = modified[..current.start].to_vec();
    for animation in &current.animations {
        if ["action_text_display", "action_text_vanish"].contains(&animation.name.as_str()) {
            let source = native.animations.iter().find(|a| a.name == animation.name).ok_or_else(bad)?;
            if source.targets.is_empty() {
                return Err(bad());
            }
            let mut mapped = original[source.start..source.end].to_vec();
            for (at, name) in &source.targets {
                let matches: Vec<_> = current.widgets.iter().enumerate().filter(|(_, n)| *n == name).collect();
                if matches.len() != 1 {
                    return Err(bad());
                }
                put(&mut mapped, *at, &(matches[0].0 as u16).to_le_bytes())?;
            }
            let old = &modified[animation.start..animation.end];
            if u16at(old, 0)? != 0 && old != mapped {
                return Err(ForgeError::invalid("Custom action animation would be overwritten"));
            }
            output.extend(mapped);
        } else {
            output.extend_from_slice(&modified[animation.start..animation.end]);
        }
    }
    let strings = output.len();
    output.extend_from_slice(&modified[current.strings..]);
    put(&mut output, 28, &(strings as u32).to_le_bytes())?;
    layout(&output)?;
    Ok(output)
}
fn restore_actions(original: &[u8], modified: &[u8]) -> Result<Vec<u8>> {
    let native = entries(original)?;
    let current = entries(modified)?;
    let mut out = modified.to_vec();
    for name in ["AActionIcon.mfl", "BActionIcon.mfl"] {
        let a = native.get(name).ok_or_else(bad)?;
        let b = current.get(name).ok_or_else(bad)?;
        let fixed = restore_layout(&original[a.start..a.start + a.size], &modified[b.start..b.start + b.size])?;
        if fixed == modified[b.start..b.start + b.size] {
            continue;
        }
        out.resize((out.len() + 7) & !7, 0);
        let start = out.len();
        put(&mut out, b.address, &(start as u32).to_le_bytes())?;
        put(&mut out, b.record, &(fixed.len() as u32).to_le_bytes())?;
        out.extend(fixed);
    }
    let size = out.len();
    put(&mut out, 4, &(size as u32).to_le_bytes())?;
    entries(&out)?;
    Ok(out)
}
fn stop_slides(data: &[u8]) -> Result<Vec<u8>> {
    let all = entries(data)?;
    let entry = all.get("MenuMapDungeon.mfl").ok_or_else(bad)?;
    let resource = &data[entry.start..entry.start + entry.size];
    let parsed = layout(resource)?;
    let mut out = data.to_vec();
    for animation in &parsed.animations {
        let raw = &resource[animation.start..animation.end];
        let mut p = 8;
        for _ in &animation.targets {
            let (obj, prop, count, kind, duration) =
                (u16at(raw, p)?, u16at(raw, p + 2)?, u16at(raw, p + 4)?, u16at(raw, p + 6)?, u32at(raw, p + 8)?);
            let widget = parsed.widgets.get(obj).ok_or_else(bad)?;
            p += 12;
            let mut value = None;
            if ["in_menu_map_anim", "out_menu_map_anim"].contains(&animation.name.as_str()) && prop <= 2 {
                value = Some(if widget == "btn_return_l" && prop == 1 { 202f32 } else { 0f32 });
            }
            if ["in_bb_map_area", "out_bb_map_area"].contains(&animation.name.as_str()) {
                if prop == 0 {
                    value = Some(if widget == "bb_map_area_right" { 256f32 } else { 0f32 });
                } else if prop == 18 {
                    value = Some(1f32);
                }
            }
            let bytes =
                if kind == 2 { (duration + 1).checked_mul(4) } else { count.checked_mul(12) }.ok_or_else(bad)?;
            part(raw, p, bytes)?;
            if let Some(value) = value {
                let (first, step) = if kind == 2 { (p, 4) } else { (p + 8, 12) };
                for at in (first..p + bytes).step_by(step) {
                    put(&mut out, entry.start + animation.start + at, &value.to_le_bytes())?;
                }
            }
            p += bytes;
        }
    }
    Ok(out)
}
fn floats(data: &mut [u8], values: &[(usize, f64)]) -> Result<()> {
    for (at, value) in values {
        put(data, *at, &(*value as f32).to_le_bytes())?;
    }
    Ok(())
}
pub fn transform(name: &str, source: &[u8], play: &[u8]) -> Result<Vec<u8>> {
    if name == "Joker.Main.Play.gar" {
        return restore_actions(play, source);
    }
    let mut out = if name == "Joker.Main.Play.SubMenu.gar" { stop_slides(source)? } else { source.to_vec() };
    match name {
        "Joker.Main.Play.SubMenu.gar" => {
            for at in [0x9eec, 0x9f74] {
                if u16at(&out, at)? != 14 {
                    return Err(bad());
                }
                put(&mut out, at, &15u16.to_le_bytes())?;
            }
        }
        "Joker.Main.New.gar" => {
            let margin = ((240f64 * 16. / 9.) as f32 as f64 - 400.) / 2.;
            let mut values =
                vec![(0x149c, 53.6), (0x2f70, -23.4), (0x2f7c, 2.), (0x6ae8, 15. - 40. / 3.), (0x6aec, 49.6)];
            for (at, x, shift) in [
                (0x1388, 0., margin),
                (0x13cc, 373., margin),
                (0x1498, 20., -margin),
                (0x1c4c, 8., -margin),
                (0x1e28, 8., -margin),
                (0x2994, 0., -margin),
                (0x6ee4, 0., margin),
            ] {
                values.push((at, x + shift));
            }
            floats(&mut out, &values)?;
        }
        "Joker.Main.gar" => {
            let mut values = vec![(0xc3e4, -40.), (0xc3f0, 480.), (0xc414, -50.), (0xc420, 500.)];
            for (base, x, y) in [
                (0xb368, -13.209999084472656, 0.5199999809265137),
                (0xba0c, -13.209999084472656, 0.5199999809265137),
                (0xba94, -12.949999809265137, 0.5199999809265137),
                (0xbb1c, 66.54999542236328, -0.570000171661377),
            ] {
                values.extend([(base, x), (base + 4, y), (base + 12, 0.87), (base + 16, 0.87)]);
            }
            floats(&mut out, &values)?;
        }
        "Joker.Main.gar.mm3d-header-source" => floats(
            &mut out,
            &[
                (0xd03c, 10.),
                (0xd054, 1.3),
                (0xd058, 1.3),
                (0xd094, -40. / 3.),
                (0xd0a0, 400. + 80. / 3.),
                (0xd0a4, 24. * (1.3f32 as f64)),
            ],
        )?,
        "Joker.gar" => floats(&mut out, &[(0x590, 400. + 80. / 3.)])?,
        _ => (),
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn malformed_is_rejected_without_panics() {
        for size in 0..128 {
            let bytes = vec![0u8; size];
            assert!(entries(&bytes).is_err());
            assert!(layout(&bytes).is_err());
        }
        assert!(transform("Joker.Main.gar", &[], &[]).is_err());
    }
}
