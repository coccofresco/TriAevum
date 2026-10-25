// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Texture colour: texel decoding (PICA formats and ETC1), the untiled grid,
//! halo detection and the emitted colour.

use crate::pymath;

type Rgba = [i64; 4];

const ETC1_MODIFIERS: [[i64; 2]; 8] = [[2, 8], [5, 17], [9, 29], [13, 42], [18, 60], [24, 80], [33, 106], [47, 183]];

/// The 16 texels of an ETC1 block, in `4 * x + y` order.
fn etc1_texels(block: u64) -> [[i64; 3]; 16] {
    let bits = |shift: u32, mask: u64| ((block >> shift) & mask) as i64;
    let flip = bits(32, 1) != 0;
    let differential = bits(33, 1) != 0;
    let tables = [bits(37, 7) as usize, bits(34, 7) as usize];
    let halves: [[i64; 3]; 2] = if differential {
        let base = [bits(59, 31), bits(51, 31), bits(43, 31)];
        let delta = [56, 48, 40].map(|shift| bits(shift, 7) - if bits(shift, 4) != 0 { 8 } else { 0 });
        [
            base.map(|c| (c << 3) | (c >> 2)),
            [0, 1, 2].map(|k| {
                let c = base[k] + delta[k];
                (c << 3) | (c >> 2)
            }),
        ]
    } else {
        [[60, 52, 44].map(|shift| bits(shift, 15) * 17), [56, 48, 40].map(|shift| bits(shift, 15) * 17)]
    };
    let mut texels = [[0i64; 3]; 16];
    for x in 0..4u32 {
        for y in 0..4u32 {
            let texel = 4 * x + y;
            let half = usize::from((if flip { y } else { x }) >= 2);
            let mut modifier = ETC1_MODIFIERS[tables[half]][bits(texel, 1) as usize];
            if bits(16 + texel, 1) != 0 {
                modifier = -modifier;
            }
            texels[texel as usize] = halves[half].map(|c| (c + modifier).clamp(0, 255));
        }
    }
    texels
}

fn le_u64(bytes: &[u8]) -> u64 {
    let mut value = 0u64;
    for (index, byte) in bytes.iter().enumerate().take(8) {
        value |= u64::from(*byte) << (8 * index);
    }
    value
}

/// `texels`: (r, g, b, a) of every stored texel, in storage order.
pub fn texels(format: u16, kind: u16, data: &[u8]) -> Vec<Rgba> {
    let mut out = Vec::new();
    match (format, kind) {
        (0x6752, 0x1401) => {
            for t in data.chunks_exact(4) {
                out.push([t[3], t[2], t[1], t[0]].map(i64::from));
            }
        }
        (0x6754, 0x1401) => {
            for t in data.chunks_exact(3) {
                out.push([i64::from(t[2]), i64::from(t[1]), i64::from(t[0]), 255]);
            }
        }
        (0x6752, 0x8034) | (0x6754, 0x8363) | (0x6752, 0x8033) => {
            for pair in data.chunks_exact(2) {
                let v = i64::from(u16::from_le_bytes([pair[0], pair[1]]));
                out.push(match kind {
                    0x8034 => {
                        [(v >> 11 & 31) * 255 / 31, (v >> 6 & 31) * 255 / 31, (v >> 1 & 31) * 255 / 31, (v & 1) * 255]
                    }
                    0x8363 => [(v >> 11 & 31) * 255 / 31, (v >> 5 & 63) * 255 / 63, (v & 31) * 255 / 31, 255],
                    _ => [(v >> 12 & 15) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17],
                });
            }
        }
        (0x6758, 0x1401) => {
            for t in data.chunks_exact(2) {
                let (a, l) = (i64::from(t[0]), i64::from(t[1]));
                out.push([l, l, l, a]);
            }
        }
        (0x6758, 0x6760) => {
            for &v in data {
                let (l, a) = (i64::from(v >> 4) * 17, i64::from(v & 15) * 17);
                out.push([l, l, l, a]);
            }
        }
        (0x6757, 0x1401) => out.extend(data.iter().map(|&v| [i64::from(v), i64::from(v), i64::from(v), 255])),
        (0x6757, 0x6761) => {
            for &v in data {
                for n in [v & 15, v >> 4] {
                    let l = i64::from(n) * 17;
                    out.push([l, l, l, 255]);
                }
            }
        }
        (0x6756, 0x1401) => out.extend(data.iter().map(|&v| [255, 255, 255, i64::from(v)])),
        (0x6756, 0x6761) => {
            for &v in data {
                for n in [v & 15, v >> 4] {
                    out.push([255, 255, 255, i64::from(n) * 17]);
                }
            }
        }
        (0x675A | 0x675B, _) => {
            let alpha = format == 0x675B;
            let stride = if alpha { 16 } else { 8 };
            for block_bytes in data.chunks_exact(stride) {
                let alphas = if alpha { le_u64(&block_bytes[..8]) } else { 0 };
                let block = le_u64(&block_bytes[stride - 8..]);
                for (n, [r, g, b]) in etc1_texels(block).into_iter().enumerate() {
                    let a = if alpha { ((alphas >> (4 * n)) & 15) as i64 * 17 } else { 255 };
                    out.push([r, g, b, a]);
                }
            }
        }
        _ => {}
    }
    out
}

/// `texel_grid`: the base level by position (row 0 as stored), undoing the
/// PICA 8x8 tiling (Morton order inside a tile; ETC1 tiles hold four 4x4
/// blocks). `None` beyond 1024 texels a side.
fn texel_grid(format: u16, kind: u16, data: &[u8], width: usize, height: usize) -> Option<Vec<Vec<Rgba>>> {
    if width == 0 || height == 0 || width > 1024 || height > 1024 {
        return None;
    }
    let mut grid = vec![vec![[0i64; 4]; width]; height];
    let tiles_x = width.max(8).div_ceil(8);
    if format == 0x675A || format == 0x675B {
        let alpha = format == 0x675B;
        let stride = if alpha { 16 } else { 8 };
        for (index, block_bytes) in data.chunks_exact(stride).enumerate() {
            let (tile, sub) = (index / 4, index % 4);
            let (tile_y, tile_x) = (tile / tiles_x, tile % tiles_x);
            let alphas = if alpha { le_u64(&block_bytes[..8]) } else { 0 };
            let block = le_u64(&block_bytes[stride - 8..]);
            for (n, [r, g, b]) in etc1_texels(block).into_iter().enumerate() {
                let x = tile_x * 8 + (sub % 2) * 4 + n / 4;
                let y = tile_y * 8 + (sub / 2) * 4 + n % 4;
                if x < width && y < height {
                    let a = if alpha { ((alphas >> (4 * n)) & 15) as i64 * 17 } else { 255 };
                    grid[y][x] = [r, g, b, a];
                }
            }
        }
        return Some(grid);
    }
    for (index, texel) in texels(format, kind, data).into_iter().enumerate() {
        let (tile, m) = (index / 64, index % 64);
        let (tile_y, tile_x) = (tile / tiles_x, tile % tiles_x);
        let x = tile_x * 8 + (0..3).map(|i| ((m >> (2 * i)) & 1) << i).sum::<usize>();
        let y = tile_y * 8 + (0..3).map(|i| ((m >> (2 * i + 1)) & 1) << i).sum::<usize>();
        if x < width && y < height {
            grid[y][x] = texel;
        }
    }
    Some(grid)
}

fn mean(values: &[f64]) -> f64 {
    if values.is_empty() {
        0.0
    } else {
        pymath::sum(values.iter().copied()) / values.len() as f64
    }
}

/// `is_halo`: a glow card's texture, bright in the middle, dark at the rim,
/// round and smooth around its centre.
pub fn is_halo(format: u16, kind: u16, data: &[u8], width: u16, height: u16) -> bool {
    if width != height || width < 16 {
        return false;
    }
    let (width, height) = (usize::from(width), usize::from(height));
    let Some(grid) = texel_grid(format, kind, data, width, height) else { return false };
    let weighted = matches!(format, 0x6752 | 0x675B | 0x6758 | 0x6756);
    let mut rings: [Vec<f64>; 5] = Default::default();
    let mut sectors: [Vec<f64>; 8] = Default::default();
    let centre = (width as f64 - 1.0) / 2.0;
    let (half_width, half_height) = (width as f64 / 2.0, height as f64 / 2.0);
    let tau = 2.0 * std::f64::consts::PI;
    for (y, row) in grid.iter().enumerate() {
        for (x, &[r, g, b, a]) in row.iter().enumerate() {
            let value = r.max(g).max(b) as f64 / 255.0 * if weighted { a as f64 / 255.0 } else { 1.0 };
            let (dx, dy) = ((x as f64 - centre) / half_width, (y as f64 - centre) / half_height);
            let d = pymath::hypot(&[dx, dy]);
            rings[((d * 4.0) as usize).min(4)].push(value);
            if (0.2..=0.6).contains(&d) {
                let sector = ((dy.atan2(dx) + std::f64::consts::PI) / tau * 8.0) as usize % 8;
                sectors[sector].push(value);
            }
        }
    }
    let middle = mean(&rings[0]);
    let rim = (mean(&rings[3]) + mean(&rings[4])) / 2.0;
    let band: Vec<f64> = sectors.iter().filter(|s| !s.is_empty()).map(|s| mean(s)).collect();
    let level = mean(&band);
    let spread = if level > 0.0 {
        let squares: Vec<f64> = band.iter().map(|v| pymath::square_pow(v - level)).collect();
        mean(&squares).sqrt() / level
    } else {
        1.0
    };
    middle >= 0.45 && rim <= 0.12 && middle - rim >= 0.4 && level >= 0.4 && spread <= 0.35
}

/// `texture_colour`: the mean of the opaque texels, the bright ones
/// weighted more; `None` without any.
pub fn texture_colour(format: u16, kind: u16, data: &[u8]) -> Option<[f64; 3]> {
    let mut total = [0.0f64; 3];
    let mut weight = 0.0f64;
    for [r, g, b, a] in texels(format, kind, data) {
        if a < 128 {
            continue;
        }
        let w = r.max(g).max(b) as f64 / 255.0;
        total[0] += r as f64 / 255.0 * w;
        total[1] += g as f64 / 255.0 * w;
        total[2] += b as f64 / 255.0 * w;
        weight += w;
    }
    if weight <= 0.0 {
        return None;
    }
    Some(total.map(|c| c / weight))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rgba8_reads_as_abgr_and_etc1_decodes() {
        assert_eq!(texels(0x6752, 0x1401, &[1, 2, 3, 4, 9]), vec![[4, 3, 2, 1]]);
        let colour = texture_colour(0x6752, 0x1401, &[255, 0x20, 0x80, 0xFF].repeat(64)).unwrap();
        assert_eq!(colour[0], 1.0);
        // An all-zero individual-mode block: base 0, table 0, modifiers +2.
        assert_eq!(etc1_texels(0)[0], [2, 2, 2]);
        assert_eq!(texels(0x675B, 0, &[0xff; 16]).len(), 16);
    }
}
