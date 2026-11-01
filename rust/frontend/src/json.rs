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

    #[test]
    fn every_control_character_is_escaped_and_output_has_no_raw_controls() {
        for code in 0u32..=0x1F {
            let character = char::from_u32(code).expect("control code point");
            let rendered = json_string(&character.to_string());
            assert!(
                rendered.chars().all(|c| c > '\u{1F}'),
                "raw control character leaked for U+{code:04X}: {rendered:?}"
            );
            assert!(rendered.starts_with('"') && rendered.ends_with('"'));
        }
        assert_eq!(json_string("\u{8}"), "\"\\u0008\"");
    }

    #[test]
    fn non_ascii_text_is_passed_through_unchanged() {
        assert_eq!(json_string("ゲーム 🎮"), "\"ゲーム 🎮\"");
    }
}
