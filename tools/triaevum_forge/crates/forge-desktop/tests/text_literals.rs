// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The literal-string probe of the text table (UX plan WP-10): every text
//! the window shows goes through `texts::Msg`, so the window's code
//! (`src/app.rs`, `src/pages/*.rs`) holds no other string literal than
//! identifiers (widget ids, kinds, keys, paths), formats made of arguments
//! and symbols, and the allowlist below (log tags, data keys, fixture
//! values). Tests and comments are not checked.

use std::path::Path;

/// Literals that are not texts of the window, with the reason.
const ALLOWLIST: &[(&str, &str)] = &[
    // Credit groups of the mod catalogue (data keys, not shown).
    ("Key Authors", "catalogue credit group"),
    ("Testing", "catalogue credit group"),
    ("Tool Creation", "catalogue credit group"),
    // Action names in the probe's keyboard log.
    ("OpenFolder", "probe action log"),
    ("Navigate({})", "probe action log"),
    // Panic and expect messages (developer-facing).
    ("created above", "expect message"),
    ("the footer changed height", "probe assertion"),
    ("a repair state is explained", "probe assertion"),
    ("an incompatible state is explained", "probe assertion"),
    // Environment names, file names and the probe's fixture values.
    ("CARGO_PKG_VERSION", "build environment"),
    ("APPIMAGE", "environment variable"),
    ("TriAevum.AppImage", "probe fixture"),
    ("TriAevum.android.json", "file name"),
    ("TriAevum-backup-{stamp}.zip", "file name"),
    ("TriAevum-backup-20261020-1842.zip", "probe fixture"),
    ("2026-10-20T18:42:07Z", "probe fixture"),
];

/// The string literals of `source` outside comments and the test module,
/// with their line, as written (escapes kept).
fn literals(source: &str) -> Vec<(usize, String)> {
    let source = match source.find("#[cfg(test)]\nmod tests") {
        Some(end) => &source[..end],
        None => source,
    };
    let bytes = source.as_bytes();
    let mut found = Vec::new();
    let (mut index, mut line) = (0, 1);
    while index < bytes.len() {
        match bytes[index] {
            b'\n' => line += 1,
            b'/' if bytes.get(index + 1) == Some(&b'/') => {
                while index < bytes.len() && bytes[index] != b'\n' {
                    index += 1;
                }
                continue;
            }
            b'\'' => {
                // A char literal ('"', '\'', '\u{2022}') or a lifetime.
                let rest = &source[index + 1..];
                let end =
                    if rest.starts_with('\\') { rest[1..].find('\'').map(|end| end + 2) } else { rest.find('\'') };
                if let Some(end) = end.filter(|end| *end <= 10 && !rest[..*end].contains(['\n', ' ', ','])) {
                    index += end + 2;
                    continue;
                }
            }
            b'r' if source[index..].starts_with("r#\"") => {
                let end = source[index + 3..].find("\"#").expect("raw string end");
                let text = &source[index + 3..index + 3 + end];
                found.push((line, text.to_string()));
                line += text.matches('\n').count();
                index += end + 5;
                continue;
            }
            b'"' => {
                let start = index + 1;
                let mut end = start;
                while bytes[end] != b'"' {
                    end += if bytes[end] == b'\\' { 2 } else { 1 };
                }
                let text = &source[start..end];
                found.push((line, text.to_string()));
                line += text.matches('\n').count();
                index = end + 1;
                continue;
            }
            _ => {}
        }
        index += 1;
    }
    found
}

/// A literal that cannot be a text for people: an identifier, a key, a
/// path, a URL, or a format made only of arguments, symbols and units.
fn is_not_text(literal: &str) -> bool {
    if literal.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || "_.:/-#".contains(c)) {
        return true;
    }
    // URLs and absolute paths (the probe's fixture folders).
    let bytes = literal.as_bytes();
    let drive = bytes.len() > 2 && bytes[0].is_ascii_uppercase() && bytes[1] == b':' && bytes[2] == b'\\';
    if literal.starts_with("https://") || literal.starts_with('/') || drive {
        return true;
    }
    // Remove the format arguments; what remains must hold no word.
    let mut rest = String::new();
    let mut depth = 0;
    for c in literal.chars() {
        match c {
            '{' => depth += 1,
            '}' if depth > 0 => depth -= 1,
            _ if depth == 0 => rest.push(c),
            _ => {}
        }
    }
    let rest = rest.replace("\\n", " ").replace("\\u", " ");
    !rest.split(|c: char| !c.is_alphabetic()).any(|word| word.chars().count() >= 2)
}

#[test]
fn the_window_shows_no_text_outside_the_table() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("src");
    let mut files = vec![root.join("app.rs")];
    let mut pages: Vec<_> = std::fs::read_dir(root.join("pages"))
        .unwrap()
        .map(|entry| entry.unwrap().path())
        .filter(|path| path.extension().is_some_and(|extension| extension == "rs"))
        .collect();
    pages.sort();
    files.extend(pages);
    let mut found = Vec::new();
    for file in &files {
        let source = std::fs::read_to_string(file).unwrap().replace("\r\n", "\n");
        for (line, literal) in literals(&source) {
            if is_not_text(&literal) || ALLOWLIST.iter().any(|(allowed, _)| *allowed == literal) {
                continue;
            }
            found.push(format!("{}:{line}: \"{literal}\"", file.strip_prefix(&root).unwrap().display()));
        }
    }
    assert!(found.is_empty(), "texts outside the message table (move them to texts/en.rs):\n{}", found.join("\n"));
}

#[test]
fn the_probe_finds_texts() {
    let source = "fn f() {\n    label(\"Choose ROM\");\n    id(\"home_rom\");\n    // \"a comment\"\n    let c = '\"';\n    format!(\"{a} \\u{2014} {b}\");\n}\n";
    let literals = literals(source);
    let texts: Vec<_> = literals.iter().filter(|(_, literal)| !is_not_text(literal)).collect();
    assert_eq!(texts, [&(2, "Choose ROM".to_string())]);
    assert_eq!(literals.len(), 3);
}
