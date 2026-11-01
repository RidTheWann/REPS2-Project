// SPDX-License-Identifier: GPL-3.0-or-later
//! Minimal deterministic JSON string rendering.
//!
//! The frontend writes JSON by hand rather than depending on a serializer. All
//! writers share this escaping routine so control characters, quotes and
//! backslashes are emitted identically in every manifest.

/// Renders a string as a quoted JSON string literal.
#[must_use]
pub fn json_string(value: &str) -> String {
    let mut result = String::with_capacity(value.len() + 2);
    result.push('"');
    for character in value.chars() {
        match character {
            '"' => result.push_str("\\\""),
            '\\' => result.push_str("\\\\"),
            '\n' => result.push_str("\\n"),
            '\r' => result.push_str("\\r"),
            '\t' => result.push_str("\\t"),
            character if character <= '\u{1F}' => {
                result.push_str(&format!("\\u{:04X}", u32::from(character)));
            }
            character => result.push(character),
        }
    }
    result.push('"');
    result
}

/// Renders an optional string as a JSON string or `null`.
#[must_use]
pub fn json_optional_string(value: Option<&str>) -> String {
    value.map(json_string).unwrap_or_else(|| "null".to_owned())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn json_strings_escape_control_characters_and_quotes() {
        assert_eq!(json_string("a\"b\\c\n"), "\"a\\\"b\\\\c\\n\"");
        assert_eq!(json_string("\u{1f}"), "\"\\u001F\"");
        assert_eq!(json_string("plain"), "\"plain\"");
    }

    #[test]
    fn optional_strings_render_null() {
        assert_eq!(json_optional_string(None), "null");
        assert_eq!(json_optional_string(Some("x")), "\"x\"");
    }
}
