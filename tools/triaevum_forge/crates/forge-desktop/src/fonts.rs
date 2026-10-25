// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The window's font comes from the operating system, so Forge ships no font
//! files or font licences (egui's bundled fonts are disabled).

use std::path::{Path, PathBuf};
use std::sync::Arc;

use eframe::egui::{FontData, FontDefinitions, FontFamily};

/// The first readable primary font, then every readable fallback (symbols,
/// CJK file names).
fn candidates() -> (Vec<PathBuf>, Vec<PathBuf>) {
    if cfg!(windows) {
        let fonts = Path::new(&std::env::var_os("WINDIR").unwrap_or_else(|| "C:\\Windows".into())).join("Fonts");
        let all = |names: &[&str]| names.iter().map(|name| fonts.join(name)).collect();
        (all(&["segoeui.ttf", "arial.ttf", "tahoma.ttf"]), all(&["seguisym.ttf", "msyh.ttc"]))
    } else if cfg!(target_os = "macos") {
        let primary = ["/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Supplemental/Arial.ttf"];
        (primary.iter().map(PathBuf::from).collect(), vec![PathBuf::from("/System/Library/Fonts/Hiragino Sans GB.ttc")])
    } else {
        let mut primary: Vec<PathBuf> = font_config_match("sans-serif").into_iter().collect();
        primary.extend(
            [
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/TTF/DejaVuSans.ttf",
                "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
                "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            ]
            .iter()
            .map(PathBuf::from),
        );
        (primary, font_config_match("sans-serif:lang=ja").into_iter().collect())
    }
}

/// `fc-match`'s file for a pattern, when fontconfig is installed.
fn font_config_match(pattern: &str) -> Option<PathBuf> {
    let output = std::process::Command::new("fc-match").args(["-f", "%{file}", pattern]).output().ok()?;
    let path = PathBuf::from(String::from_utf8(output.stdout).ok()?.trim());
    (output.status.success() && path.is_file()).then_some(path)
}

pub fn system_fonts() -> Result<FontDefinitions, String> {
    let (primary, fallbacks) = candidates();
    let primary = primary.iter().find_map(|path| Some((path, std::fs::read(path).ok()?)));
    let Some((primary_path, primary)) = primary else {
        return Err("No usable system font was found".to_string());
    };
    let mut definitions = FontDefinitions::empty();
    let mut names = Vec::new();
    let mut add = |name: String, bytes: Vec<u8>| {
        definitions.font_data.insert(name.clone(), Arc::new(FontData::from_owned(bytes)));
        names.push(name);
    };
    add(primary_path.to_string_lossy().into_owned(), primary);
    for path in fallbacks.iter().filter(|path| *path != primary_path) {
        if let Ok(bytes) = std::fs::read(path) {
            add(path.to_string_lossy().into_owned(), bytes);
        }
    }
    definitions.families.insert(FontFamily::Proportional, names.clone());
    definitions.families.insert(FontFamily::Monospace, names);
    Ok(definitions)
}

#[cfg(test)]
mod tests {
    #[cfg(windows)]
    #[test]
    fn windows_has_a_system_font() {
        let definitions = super::system_fonts().unwrap();
        assert!(!definitions.families[&eframe::egui::FontFamily::Proportional].is_empty());
    }
}
