// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Where and why Python 3.13's `json.loads` rejects a document: a scanner
//! with the C scanner's grammar, messages and positions (code points),
//! used only after serde has rejected the text. Failure receipts and user
//! messages carry these strings (`str(JSONDecodeError)`).

type Failure = (&'static str, usize);

struct Scanner {
    text: Vec<char>,
}

fn is_digit(character: Option<&char>) -> bool {
    character.is_some_and(char::is_ascii_digit)
}

impl Scanner {
    fn at(&self, index: usize) -> Option<&char> {
        self.text.get(index)
    }

    fn starts_with(&self, index: usize, literal: &str) -> bool {
        let literal: Vec<char> = literal.chars().collect();
        self.text.get(index..index + literal.len()) == Some(literal.as_slice())
    }

    fn whitespace(&self, mut index: usize) -> usize {
        while matches!(self.at(index), Some(' ' | '\t' | '\n' | '\r')) {
            index += 1;
        }
        index
    }

    /// `scan_once`: the end of the value at `index`.
    fn value(&self, index: usize) -> Result<usize, Failure> {
        let literal = |word: &str| self.starts_with(index, word).then(|| index + word.chars().count());
        let matched = match self.at(index) {
            None => None,
            Some('"') => return self.string(index + 1),
            Some('{') => return self.object(index + 1),
            Some('[') => return self.array(index + 1),
            Some('n') => literal("null"),
            Some('t') => literal("true"),
            Some('f') => literal("false"),
            Some('N') => literal("NaN"),
            Some('I') => literal("Infinity"),
            Some('-') if self.starts_with(index, "-Infinity") => literal("-Infinity"),
            Some(_) => self.number(index),
        };
        matched.ok_or(("Expecting value", index))
    }

    fn number(&self, index: usize) -> Option<usize> {
        let mut end = index;
        if self.at(end) == Some(&'-') {
            end += 1;
        }
        match self.at(end) {
            Some('0') => end += 1,
            Some('1'..='9') => {
                while is_digit(self.at(end)) {
                    end += 1;
                }
            }
            _ => return None,
        }
        if self.at(end) == Some(&'.') && is_digit(self.at(end + 1)) {
            end += 2;
            while is_digit(self.at(end)) {
                end += 1;
            }
        }
        if matches!(self.at(end), Some('e' | 'E')) {
            let mut exponent = end + 1;
            if matches!(self.at(exponent), Some('+' | '-')) {
                exponent += 1;
            }
            if is_digit(self.at(exponent)) {
                while is_digit(self.at(exponent)) {
                    exponent += 1;
                }
                end = exponent;
            }
        }
        Some(end)
    }

    fn hex4(&self, index: usize) -> bool {
        (index..index + 4).all(|position| self.at(position).is_some_and(char::is_ascii_hexdigit))
    }

    /// `scanstring` from just after the opening quote.
    fn string(&self, start: usize) -> Result<usize, Failure> {
        let begin = start - 1;
        let mut index = start;
        loop {
            match self.at(index) {
                None => return Err(("Unterminated string starting at", begin)),
                Some('"') => return Ok(index + 1),
                Some('\\') => match self.at(index + 1) {
                    None => return Err(("Unterminated string starting at", begin)),
                    Some('u') => {
                        if !self.hex4(index + 2) {
                            return Err(("Invalid \\uXXXX escape", index + 1));
                        }
                        let high = u32::from_str_radix(&self.text[index + 2..index + 6].iter().collect::<String>(), 16)
                            .unwrap_or_default();
                        index += 6;
                        if (0xd800..0xdc00).contains(&high) && self.starts_with(index, "\\u") {
                            if !self.hex4(index + 2) {
                                return Err(("Invalid \\uXXXX escape", index + 1));
                            }
                            let low =
                                u32::from_str_radix(&self.text[index + 2..index + 6].iter().collect::<String>(), 16)
                                    .unwrap_or_default();
                            if (0xdc00..0xe000).contains(&low) {
                                index += 6;
                            }
                        }
                    }
                    Some('"' | '\\' | '/' | 'b' | 'f' | 'n' | 'r' | 't') => index += 2,
                    Some(_) => return Err(("Invalid \\escape", index)),
                },
                Some(character) if (*character as u32) < 0x20 => return Err(("Invalid control character at", index)),
                Some(_) => index += 1,
            }
        }
    }

    fn object(&self, start: usize) -> Result<usize, Failure> {
        let mut index = self.whitespace(start);
        match self.at(index) {
            Some('}') => return Ok(index + 1),
            Some('"') => {}
            _ => return Err(("Expecting property name enclosed in double quotes", index)),
        }
        loop {
            index = self.whitespace(self.string(index + 1)?);
            if self.at(index) != Some(&':') {
                return Err(("Expecting ':' delimiter", index));
            }
            index = self.whitespace(self.value(self.whitespace(index + 1))?);
            let separator = self.at(index).copied();
            index += 1;
            match separator {
                Some('}') => return Ok(index),
                Some(',') => {}
                _ => return Err(("Expecting ',' delimiter", index - 1)),
            }
            let comma = index - 1;
            index = self.whitespace(index);
            match self.at(index) {
                Some('"') => {}
                Some('}') => return Err(("Illegal trailing comma before end of object", comma)),
                _ => return Err(("Expecting property name enclosed in double quotes", index)),
            }
        }
    }

    fn array(&self, start: usize) -> Result<usize, Failure> {
        let mut index = self.whitespace(start);
        if self.at(index) == Some(&']') {
            return Ok(index + 1);
        }
        loop {
            index = self.whitespace(self.value(index)?);
            let separator = self.at(index).copied();
            index += 1;
            match separator {
                Some(']') => return Ok(index),
                Some(',') => {}
                _ => return Err(("Expecting ',' delimiter", index - 1)),
            }
            let comma = index - 1;
            index = self.whitespace(index);
            if self.at(index) == Some(&']') {
                return Err(("Illegal trailing comma before end of array", comma));
            }
        }
    }
}

/// `str(JSONDecodeError)` for `text`, or `None` when Python would accept
/// it (NaN/Infinity literals, which serde refuses).
pub fn locate(text: &str) -> Option<String> {
    let scanner = Scanner { text: text.chars().collect() };
    let failure = match scanner.value(scanner.whitespace(0)) {
        Err(failure) => failure,
        Ok(end) => {
            let end = scanner.whitespace(end);
            if end == scanner.text.len() {
                return None;
            }
            ("Extra data", end)
        }
    };
    let (message, position) = failure;
    let before = &scanner.text[..position.min(scanner.text.len())];
    let line = before.iter().filter(|character| **character == '\n').count() + 1;
    let column = match before.iter().rposition(|character| *character == '\n') {
        Some(newline) => position - newline,
        None => position + 1,
    };
    Some(format!("{message}: line {line} column {column} (char {position})"))
}

#[cfg(test)]
mod tests {
    use super::locate;

    #[test]
    fn messages_match_python_3_13() {
        let bs = '\\';
        for (text, expected) in [
            ("".to_string(), "Expecting value: line 1 column 1 (char 0)"),
            ("{broken".into(), "Expecting property name enclosed in double quotes: line 1 column 2 (char 1)"),
            ("[1 2]".into(), "Expecting ',' delimiter: line 1 column 4 (char 3)"),
            ("{\"a\" 1}".into(), "Expecting ':' delimiter: line 1 column 6 (char 5)"),
            ("{\"a\":".into(), "Expecting value: line 1 column 6 (char 5)"),
            ("[1,]".into(), "Illegal trailing comma before end of array: line 1 column 3 (char 2)"),
            ("{\"a\":1,}".into(), "Illegal trailing comma before end of object: line 1 column 7 (char 6)"),
            ("\"abc".into(), "Unterminated string starting at: line 1 column 1 (char 0)"),
            ("[\"a\nb\"]".into(), "Invalid control character at: line 1 column 4 (char 3)"),
            ("tru".into(), "Expecting value: line 1 column 1 (char 0)"),
            ("01".into(), "Extra data: line 1 column 2 (char 1)"),
            ("  ".into(), "Expecting value: line 1 column 3 (char 2)"),
            ("\n {}\n\n]".into(), "Extra data: line 4 column 1 (char 6)"),
            (format!("\"a{bs}x\""), "Invalid \\escape: line 1 column 3 (char 2)"),
            (format!("\"ab{bs}u12zz\""), "Invalid \\uXXXX escape: line 1 column 5 (char 4)"),
            (format!("\"{bs}ud800{bs}u12\""), "Invalid \\uXXXX escape: line 1 column 9 (char 8)"),
            ("{\"\u{e9}\": x}".into(), "Expecting value: line 1 column 7 (char 6)"),
            ("{\"a\":1, x}".into(), "Expecting property name enclosed in double quotes: line 1 column 9 (char 8)"),
        ] {
            assert_eq!(locate(&text).as_deref(), Some(expected), "{text:?}");
        }
        assert_eq!(locate("NaN"), None);
    }
}
