// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Removes the user name from paths in text Forge writes to its log or shows
//! as copyable details: `C:\Users\<name>`, `/home/<name>`, `/Users/<name>`,
//! `/var/home/<name>`, and any path component equal to the current user
//! name, become `<user>`. Only whole path components are replaced, so
//! ordinary words in a message are kept.

pub const PLACEHOLDER: &str = "<user>";

/// [`redact_with`] for the user running this process.
pub fn redact(text: &str) -> String {
    redact_with(text, current_user().as_deref())
}

/// The user name of this process, from the environment or the home folder.
pub fn current_user() -> Option<String> {
    let named = ["USERNAME", "USER", "LOGNAME"].iter().find_map(|name| std::env::var(name).ok());
    let home = ["USERPROFILE", "HOME"]
        .iter()
        .find_map(|name| std::env::var(name).ok())
        .and_then(|home| std::path::Path::new(&home).file_name().map(|name| name.to_string_lossy().into_owned()));
    named.or(home).filter(|name| name.chars().count() >= 2)
}

fn is_separator(character: char) -> bool {
    character == '/' || character == '\\'
}

/// Characters that end a path component in running text.
fn ends_component(character: char) -> bool {
    is_separator(character) || "\"'`\n\r\t:;,()<>|[]{}".contains(character)
}

/// The end of the component starting at `start`: up to the next separator
/// when one follows on the same line (names may contain spaces), otherwise
/// up to the first space.
fn component_end(chars: &[char], start: usize) -> usize {
    let stop = (start..chars.len()).find(|&index| ends_component(chars[index])).unwrap_or(chars.len());
    if stop < chars.len() && is_separator(chars[stop]) {
        return stop;
    }
    (start..stop).find(|&index| chars[index].is_whitespace()).unwrap_or(stop)
}

fn names_user(previous: Option<&str>, component: &str, user: Option<&str>) -> bool {
    if component.is_empty() || component == PLACEHOLDER {
        return false;
    }
    let after_home = match previous {
        Some(previous) => previous.eq_ignore_ascii_case("users") || previous == "home",
        None => false,
    };
    let is_user = user.is_some_and(|user| {
        if cfg!(windows) {
            component.to_lowercase() == user.to_lowercase()
        } else {
            component == user
        }
    });
    after_home || is_user
}

/// Replaces user-name components of absolute paths in `text`.
pub fn redact_with(text: &str, user: Option<&str>) -> String {
    let chars: Vec<char> = text.chars().collect();
    let mut output = String::with_capacity(text.len());
    let mut previous: Option<String> = None;
    let mut index = 0;
    while index < chars.len() {
        let character = chars[index];
        output.push(character);
        index += 1;
        if !is_separator(character) {
            previous = None;
            continue;
        }
        let end = component_end(&chars, index);
        let component: String = chars[index..end].iter().collect();
        if names_user(previous.as_deref(), &component, user) {
            output.push_str(PLACEHOLDER);
            previous = Some(PLACEHOLDER.to_string());
        } else {
            output.push_str(&component);
            previous = Some(component);
        }
        index = end;
        if index < chars.len() && !is_separator(chars[index]) {
            previous = None;
        }
    }
    output
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn home_folders_lose_the_user_name() {
        let cases = [
            (
                r"Cannot write C:\Users\Jane Doe\Games\TriAevum\data: denied",
                r"Cannot write C:\Users\<user>\Games\TriAevum\data: denied",
            ),
            ("C:/Users/jdoe/x.cci", "C:/Users/<user>/x.cci"),
            (r"\\?\C:\USERS\jdoe\x", r"\\?\C:\USERS\<user>\x"),
            ("open /home/jdoe/.local/share/TriAevum failed", "open /home/<user>/.local/share/TriAevum failed"),
            ("/var/home/jdoe/a and /Users/jdoe", "/var/home/<user>/a and /Users/<user>"),
            ("in /home/jdoe", "in /home/<user>"),
        ];
        for (text, expected) in cases {
            assert_eq!(redact_with(text, None), expected, "{text}");
        }
    }

    #[test]
    fn the_current_user_is_replaced_only_as_a_path_component() {
        let text = r"D:\jdoe\TriAevum and jdoe's ROM at /mnt/jdoe/x; jdoesmith/y";
        assert_eq!(redact_with(text, Some("jdoe")), r"D:\<user>\TriAevum and jdoe's ROM at /mnt/<user>/x; jdoesmith/y");
        assert_eq!(redact_with("no paths here", Some("here")), "no paths here");
        assert_eq!(redact_with("/storage/emulated/0/Download/x.3ds", Some("u0")), "/storage/emulated/0/Download/x.3ds");
    }
}
