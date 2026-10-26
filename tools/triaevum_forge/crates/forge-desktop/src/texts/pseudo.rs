// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The pseudo-locale: English with accented letters, about 40 % longer and
//! bracketed, so a text that escapes the table or a layout that clips a
//! longer translation shows at a glance. Developer use only.

pub fn localize(english: &str) -> String {
    let mut text = String::from("[");
    let mut letters = 0usize;
    for c in english.chars() {
        text.push(match c {
            'a' => '\u{e1}',
            'e' => '\u{e9}',
            'i' => '\u{ed}',
            'o' => '\u{f6}',
            'u' => '\u{fc}',
            'A' => '\u{c5}',
            'E' => '\u{c9}',
            'O' => '\u{d6}',
            'n' => '\u{f1}',
            'c' => '\u{e7}',
            other => other,
        });
        if c.is_alphabetic() {
            letters += 1;
            // Every fifth letter doubled: the extra length of German or French.
            if letters.is_multiple_of(5) && c.is_ascii_lowercase() {
                text.push(c);
            }
        }
    }
    let padding = english.chars().count() / 6;
    text.push(' ');
    text.extend(std::iter::repeat_n('\u{b7}', padding.max(1)));
    text.push(']');
    text
}

#[cfg(test)]
mod tests {
    #[test]
    fn pseudo_texts_are_longer_and_marked() {
        let text = super::localize("Prepare");
        assert!(text.starts_with('[') && text.ends_with(']'), "{text}");
        assert!(text.chars().count() as f32 >= "Prepare".len() as f32 * 1.3, "{text}");
    }
}
