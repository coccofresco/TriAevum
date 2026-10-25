// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Synthetic fixtures shared by the integration tests.

use std::collections::BTreeMap;

const NONE: u32 = 0xFFFF_FFFF;

#[derive(Default)]
struct Node {
    directories: BTreeMap<String, Node>,
    files: Vec<(String, Vec<u8>)>,
}

fn padded_name(text: &str) -> Vec<u8> {
    let mut encoded: Vec<u8> = text.encode_utf16().flat_map(u16::to_le_bytes).collect();
    encoded.resize(encoded.len().div_ceil(4) * 4, 0);
    encoded
}

fn push32(out: &mut Vec<u8>, values: &[u32]) {
    for value in values {
        out.extend(value.to_le_bytes());
    }
}

/// A Level-3 RomFS image of `(path, data)` files, nested directories
/// included (directories in name order, files in the given order).
pub fn romfs_image(files: &[(&str, &[u8])]) -> Vec<u8> {
    let mut root = Node::default();
    for (path, data) in files {
        let mut parts: Vec<&str> = path.split('/').collect();
        let name = parts.pop().unwrap();
        let mut node = &mut root;
        for part in parts {
            node = node.directories.entry(part.to_string()).or_default();
        }
        node.files.push((name.to_string(), data.to_vec()));
    }
    // Pre-order: (node, name, parent index, sibling list position).
    let mut order: Vec<(&Node, String, Option<usize>)> = Vec::new();
    fn walk<'a>(
        node: &'a Node,
        name: String,
        parent: Option<usize>,
        order: &mut Vec<(&'a Node, String, Option<usize>)>,
    ) {
        let index = order.len();
        order.push((node, name, parent));
        for (child_name, child) in &node.directories {
            walk(child, child_name.clone(), Some(index), order);
        }
    }
    walk(&root, String::new(), None, &mut order);
    let mut directory_offsets = Vec::new();
    let mut offset = 0u32;
    for (_, name, _) in &order {
        directory_offsets.push(offset);
        offset += 24 + padded_name(name).len() as u32;
    }
    let mut file_offsets = Vec::new();
    let mut offset = 0u32;
    for (node, _, _) in &order {
        let mut offsets = Vec::new();
        for (name, _) in &node.files {
            offsets.push(offset);
            offset += 32 + padded_name(name).len() as u32;
        }
        file_offsets.push(offsets);
    }
    let (mut directory_table, mut file_table, mut file_data) = (Vec::new(), Vec::new(), Vec::new());
    for (index, (node, name, parent)) in order.iter().enumerate() {
        let siblings: Vec<usize> = match parent {
            Some(parent) => (0..order.len()).filter(|i| order[*i].2 == Some(*parent)).collect(),
            None => vec![index],
        };
        let position = siblings.iter().position(|i| *i == index).unwrap();
        let sibling = siblings.get(position + 1).map_or(NONE, |i| directory_offsets[*i]);
        let child = (0..order.len()).find(|i| order[*i].2 == Some(index)).map_or(NONE, |i| directory_offsets[i]);
        let first = file_offsets[index].first().copied().unwrap_or(NONE);
        let parent_offset = parent.map_or(0, |parent| directory_offsets[parent]);
        push32(
            &mut directory_table,
            &[parent_offset, sibling, child, first, NONE, name.encode_utf16().count() as u32 * 2],
        );
        directory_table.extend(padded_name(name));
        for (position, (file_name, data)) in node.files.iter().enumerate() {
            let following = file_offsets[index].get(position + 1).copied().unwrap_or(NONE);
            push32(&mut file_table, &[directory_offsets[index], following]);
            file_table.extend((file_data.len() as u64).to_le_bytes());
            file_table.extend((data.len() as u64).to_le_bytes());
            push32(&mut file_table, &[NONE, file_name.encode_utf16().count() as u32 * 2]);
            file_table.extend(padded_name(file_name));
            file_data.extend(data);
            file_data.resize(file_data.len().div_ceil(16) * 16, 0);
        }
    }
    let directory_meta = 0x28u32;
    let file_meta = directory_meta + directory_table.len() as u32;
    let data_start = file_meta + file_table.len() as u32;
    let mut image = Vec::new();
    push32(&mut image, &[0x28, 0, 0, directory_meta, directory_table.len() as u32, 0, 0, file_meta]);
    push32(&mut image, &[file_table.len() as u32, data_start]);
    image.extend(directory_table);
    image.extend(file_table);
    image.extend(file_data);
    image
}

/// `test_qbf_font.font_bytes`: a QBF of `size`-pixel cells.
pub fn font_bytes(size: u8, records: &[(u16, u16, [u8; 4])]) -> Vec<u8> {
    let glyphs = records.iter().map(|record| record.1).max().unwrap() + 1;
    let mut data = b"QBF1".to_vec();
    for value in [records.len() as u16, glyphs, 42, 0] {
        data.extend(value.to_le_bytes());
    }
    data.extend([4, size, size, 2]);
    for (code, glyph, metrics) in records {
        data.extend(code.to_le_bytes());
        data.extend(glyph.to_le_bytes());
        data.extend(metrics);
    }
    for index in 0..glyphs {
        data.extend(std::iter::repeat_n(0x1f + index as u8, usize::from(size) * usize::from(size) / 2));
    }
    data
}
