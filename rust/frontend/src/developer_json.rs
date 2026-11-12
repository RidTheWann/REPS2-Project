// SPDX-License-Identifier: GPL-3.0-or-later
//! Minimal, fail-closed JSON reader for developer-session artifacts.
//!
//! The frontend deliberately depends on no JSON crate, so writers emit JSON by
//! hand. Reading a session artifact back (for `reps2 dev compare`) needs a
//! parser, so this module provides a small, bounded reader limited to the JSON
//! value shapes the developer CLI inspects. It rejects malformed input rather
//! than coercing it, and it is not a serializer.

use std::collections::BTreeMap;

/// Maximum nesting depth accepted before the reader fails closed.
const MAX_DEPTH: usize = 64;

/// A parsed JSON value.
#[derive(Clone, Debug, PartialEq)]
pub(crate) enum Value {
    Null,
    Bool(bool),
    /// The exact source text of a validated JSON number.
    Number(String),
    String(String),
    Array(Vec<Value>),
    Object(BTreeMap<String, Value>),
}

impl Value {
    pub(crate) fn as_str(&self) -> Option<&str> {
        match self {
            Value::String(value) => Some(value),
            _ => None,
        }
    }

    pub(crate) fn as_array(&self) -> Option<&[Value]> {
        match self {
            Value::Array(values) => Some(values),
            _ => None,
        }
    }

    pub(crate) fn as_i64(&self) -> Option<i64> {
        match self {
            Value::Number(text) => text.parse().ok(),
            _ => None,
        }
    }

    pub(crate) fn get(&self, key: &str) -> Option<&Value> {
        match self {
            Value::Object(map) => map.get(key),
            _ => None,
        }
    }
}

/// Parses one JSON document. Rejects trailing content, duplicate object keys,
/// control characters in strings, invalid escapes, and excessive nesting.
pub(crate) fn parse(source: &str) -> Result<Value, String> {
    let mut parser = Parser {
        bytes: source.as_bytes(),
        position: 0,
    };
    let value = parser.parse_value(0)?;
    parser.skip_whitespace();
    if parser.position != parser.bytes.len() {
        return Err(format!("trailing content at byte {}", parser.position));
    }
    Ok(value)
}

struct Parser<'a> {
    bytes: &'a [u8],
    position: usize,
}

impl Parser<'_> {
    fn skip_whitespace(&mut self) {
        while let Some(byte) = self.bytes.get(self.position) {
            match byte {
                b' ' | b'\t' | b'\n' | b'\r' => self.position += 1,
                _ => break,
            }
        }
    }

    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.position).copied()
    }

    fn parse_value(&mut self, depth: usize) -> Result<Value, String> {
        if depth > MAX_DEPTH {
            return Err("maximum nesting depth exceeded".to_owned());
        }
        self.skip_whitespace();
        match self.peek() {
            Some(b'{') => self.parse_object(depth),
            Some(b'[') => self.parse_array(depth),
            Some(b'"') => Ok(Value::String(self.parse_string()?)),
            Some(b't') => {
                self.expect_literal("true")?;
                Ok(Value::Bool(true))
            }
            Some(b'f') => {
                self.expect_literal("false")?;
                Ok(Value::Bool(false))
            }
            Some(b'n') => {
                self.expect_literal("null")?;
                Ok(Value::Null)
            }
            Some(b'-' | b'0'..=b'9') => Ok(Value::Number(self.parse_number()?)),
            _ => Err(format!("unexpected byte at {}", self.position)),
        }
    }

    fn expect_literal(&mut self, literal: &str) -> Result<(), String> {
        if self.bytes.get(self.position..self.position + literal.len()) == Some(literal.as_bytes())
        {
            self.position += literal.len();
            Ok(())
        } else {
            Err(format!("invalid literal at {}", self.position))
        }
    }

    fn parse_object(&mut self, depth: usize) -> Result<Value, String> {
        self.position += 1;
        let mut map = BTreeMap::new();
        self.skip_whitespace();
        if self.peek() == Some(b'}') {
            self.position += 1;
            return Ok(Value::Object(map));
        }
        loop {
            self.skip_whitespace();
            if self.peek() != Some(b'"') {
                return Err(format!("expected object key at {}", self.position));
            }
            let key = self.parse_string()?;
            self.skip_whitespace();
            if self.peek() != Some(b':') {
                return Err(format!("expected ':' at {}", self.position));
            }
            self.position += 1;
            let value = self.parse_value(depth + 1)?;
            if map.insert(key.clone(), value).is_some() {
                return Err(format!("duplicate object key {key:?}"));
            }
            self.skip_whitespace();
            match self.peek() {
                Some(b',') => self.position += 1,
                Some(b'}') => {
                    self.position += 1;
                    return Ok(Value::Object(map));
                }
                _ => return Err(format!("expected ',' or '}}' at {}", self.position)),
            }
        }
    }

    fn parse_array(&mut self, depth: usize) -> Result<Value, String> {
        self.position += 1;
        let mut values = Vec::new();
        self.skip_whitespace();
        if self.peek() == Some(b']') {
            self.position += 1;
            return Ok(Value::Array(values));
        }
        loop {
            values.push(self.parse_value(depth + 1)?);
            self.skip_whitespace();
            match self.peek() {
                Some(b',') => self.position += 1,
                Some(b']') => {
                    self.position += 1;
                    return Ok(Value::Array(values));
                }
                _ => return Err(format!("expected ',' or ']' at {}", self.position)),
            }
        }
    }

    fn parse_string(&mut self) -> Result<String, String> {
        self.position += 1;
        let mut output = String::new();
        loop {
            let Some(byte) = self.peek() else {
                return Err("unterminated string".to_owned());
            };
            self.position += 1;
            match byte {
                b'"' => return Ok(output),
                b'\\' => {
                    let Some(escaped) = self.peek() else {
                        return Err("unterminated escape".to_owned());
                    };
                    self.position += 1;
                    match escaped {
                        b'"' => output.push('"'),
                        b'\\' => output.push('\\'),
                        b'/' => output.push('/'),
                        b'b' => output.push('\u{8}'),
                        b'f' => output.push('\u{C}'),
                        b'n' => output.push('\n'),
                        b'r' => output.push('\r'),
                        b't' => output.push('\t'),
                        b'u' => output.push(self.parse_unicode_escape()?),
                        _ => return Err(format!("invalid escape at {}", self.position)),
                    }
                }
                0x00..=0x1F => return Err("control character in string".to_owned()),
                _ => {
                    let start = self.position - 1;
                    let end = start + utf8_width(byte);
                    if end > self.bytes.len() {
                        return Err("truncated UTF-8 sequence".to_owned());
                    }
                    let scalar = std::str::from_utf8(&self.bytes[start..end])
                        .map_err(|_| "invalid UTF-8 in string".to_owned())?;
                    output.push_str(scalar);
                    self.position = end;
                }
            }
        }
    }

    fn parse_unicode_escape(&mut self) -> Result<char, String> {
        let code = self.parse_hex4()?;
        if (0xD800..=0xDBFF).contains(&code) {
            if self.peek() != Some(b'\\') {
                return Err("lone high surrogate".to_owned());
            }
            self.position += 1;
            if self.peek() != Some(b'u') {
                return Err("lone high surrogate".to_owned());
            }
            self.position += 1;
            let low = self.parse_hex4()?;
            if !(0xDC00..=0xDFFF).contains(&low) {
                return Err("invalid low surrogate".to_owned());
            }
            let scalar = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            char::from_u32(scalar).ok_or_else(|| "invalid surrogate pair".to_owned())
        } else if (0xDC00..=0xDFFF).contains(&code) {
            Err("lone low surrogate".to_owned())
        } else {
            char::from_u32(code).ok_or_else(|| "invalid unicode escape".to_owned())
        }
    }

    fn parse_hex4(&mut self) -> Result<u32, String> {
        let mut value = 0u32;
        for _ in 0..4 {
            let Some(byte) = self.peek() else {
                return Err("truncated unicode escape".to_owned());
            };
            let digit = (byte as char)
                .to_digit(16)
                .ok_or_else(|| format!("invalid hex digit at {}", self.position))?;
            value = value * 16 + digit;
            self.position += 1;
        }
        Ok(value)
    }

    fn parse_number(&mut self) -> Result<String, String> {
        let start = self.position;
        if self.peek() == Some(b'-') {
            self.position += 1;
        }
        match self.peek() {
            Some(b'0') => self.position += 1,
            Some(b'1'..=b'9') => {
                while self.peek().is_some_and(|byte| byte.is_ascii_digit()) {
                    self.position += 1;
                }
            }
            _ => return Err(format!("invalid number at {start}")),
        }
        if self.peek() == Some(b'.') {
            self.position += 1;
            let fraction_start = self.position;
            while self.peek().is_some_and(|byte| byte.is_ascii_digit()) {
                self.position += 1;
            }
            if self.position == fraction_start {
                return Err(format!("invalid fraction at {start}"));
            }
        }
        if matches!(self.peek(), Some(b'e' | b'E')) {
            self.position += 1;
            if matches!(self.peek(), Some(b'+' | b'-')) {
                self.position += 1;
            }
            let exponent_start = self.position;
            while self.peek().is_some_and(|byte| byte.is_ascii_digit()) {
                self.position += 1;
            }
            if self.position == exponent_start {
                return Err(format!("invalid exponent at {start}"));
            }
        }
        std::str::from_utf8(&self.bytes[start..self.position])
            .map(str::to_owned)
            .map_err(|_| format!("invalid UTF-8 in number at {start}"))
    }
}

fn utf8_width(lead: u8) -> usize {
    match lead {
        0x00..=0x7F => 1,
        0xC0..=0xDF => 2,
        0xE0..=0xEF => 3,
        0xF0..=0xF7 => 4,
        _ => 1,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_objects_arrays_and_scalars() {
        let value = parse(r#"{"a":[1,true,null,"x"],"b":-1.5e+2}"#).expect("valid document");
        assert_eq!(
            value
                .get("a")
                .and_then(Value::as_array)
                .map(|values| values.len()),
            Some(4)
        );
        assert_eq!(value.get("b").and_then(Value::as_i64), None);
        assert_eq!(parse("true").expect("bool"), Value::Bool(true));
        assert_eq!(parse(" null ").expect("null"), Value::Null);
        assert_eq!(parse("\"x\"").expect("string").as_str(), Some("x"));
    }

    #[test]
    fn decodes_escapes_and_surrogate_pairs() {
        let value = parse(r#""a\"b\\c\n\t\u0041\uD83D\uDE00""#).expect("escaped string");
        assert_eq!(value.as_str(), Some("a\"b\\c\n\tA😀"));
        assert!(parse(r#""\uD800""#).is_err(), "lone high surrogate");
        assert!(parse(r#""\uDC00""#).is_err(), "lone low surrogate");
        assert!(parse(r#""\uD800\u0041""#).is_err(), "bad low surrogate");
        assert!(parse(r#""\u12""#).is_err(), "truncated escape");
    }

    #[test]
    fn rejects_malformed_documents() {
        for invalid in [
            "",
            "{",
            "{\"a\":}",
            "{\"a\":1,}",
            "{\"a\":1}x",
            "{\"a\":01}",
            "{\"a\":-}",
            "{\"a\":1.}",
            "{\"a\":1e}",
            "{\"a\":\"unterminated}",
            "{\"a\":\"bad\\q\"}",
            "{\"a\":\"raw\nnewline\"}",
            "[1,]",
            "tru",
            "{\"a\":1,\"a\":2}",
        ] {
            assert!(
                parse(invalid).is_err(),
                "accepted malformed input: {invalid:?}"
            );
        }
    }

    #[test]
    fn rejects_excessive_nesting() {
        let deep = format!(
            "{}0{}",
            "[".repeat(MAX_DEPTH + 2),
            "]".repeat(MAX_DEPTH + 2)
        );
        assert!(parse(&deep).is_err());
    }

    #[test]
    fn accepts_unicode_and_multibyte_text() {
        assert_eq!(
            parse(r#""ゲーム 🎮""#).expect("unicode").as_str(),
            Some("ゲーム 🎮")
        );
    }
}
