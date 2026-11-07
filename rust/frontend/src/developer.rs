// SPDX-License-Identifier: GPL-3.0-or-later
//! Agent-oriented inspection of REPS2 developer diagnostic sessions.
//!
//! The `reps2 dev` command namespace writes one JSON document to stdout so
//! IDEs and coding agents can consume session summaries and event records
//! without scraping human terminal output.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use crate::diagnostics;
use crate::json::json_string;

const REPORT_SCHEMA_VERSION: u32 = 1;
const DEFAULT_SESSION_LIMIT: usize = 20;
const MAX_SESSION_LIMIT: usize = 100;

struct SessionData {
    id: String,
    directory: PathBuf,
    manifest_json: Option<String>,
    events: Vec<String>,
    diagnostics: Vec<String>,
    integrity_errors: Vec<String>,
}

impl SessionData {
    fn is_complete(&self) -> bool {
        self.manifest_json.is_some()
            && self
                .events
                .last()
                .is_some_and(|event| event.contains("\"event\":\"session.finished\""))
    }
}

pub fn run_command(arguments: &[String], log_root: &Path) -> Result<(), String> {
    match arguments.first().map(String::as_str) {
        Some("sessions") => {
            let limit = match parse_session_limit(&arguments[1..]) {
                Ok(limit) => limit,
                Err(error) => {
                    return report_error("REPS2_DEV_INVALID_ARGUMENTS", &error);
                }
            };
            match session_list_json(log_root, limit) {
                Ok(report) => {
                    println!("{report}");
                    Ok(())
                }
                Err(error) => report_error(
                    "REPS2_DEV_SESSION_LIST_FAILED",
                    &format!("cannot list REPS2 diagnostic sessions: {error}"),
                ),
            }
        }
        Some("inspect") if arguments.len() == 2 => {
            let requested = &arguments[1];
            match resolve_session_id(log_root, requested)
                .and_then(|id| load_session(log_root, &id))
            {
                Ok(session) => {
                    println!("{}", session_inspection_json(&session));
                    Ok(())
                }
                Err(error) => report_error(
                    "REPS2_DEV_SESSION_INSPECTION_FAILED",
                    &error,
                ),
            }
        }
        Some("help") | None => {
            print_usage();
            Ok(())
        }
        _ => report_error(
            "REPS2_DEV_INVALID_ARGUMENTS",
            "usage: reps2 [--log-dir <directory>] dev <sessions [--limit N] | inspect <latest|session-id> | help>",
        ),
    }
}

fn parse_session_limit(arguments: &[String]) -> Result<usize, String> {
    if arguments.is_empty() {
        return Ok(DEFAULT_SESSION_LIMIT);
    }
    if arguments.len() != 2 || arguments[0] != "--limit" {
        return Err("usage: reps2 dev sessions [--limit N]".to_owned());
    }
    let limit = arguments[1]
        .parse::<usize>()
        .map_err(|_| "--limit must be a positive integer".to_owned())?;
    if limit == 0 || limit > MAX_SESSION_LIMIT {
        return Err(format!(
            "--limit must be between 1 and {MAX_SESSION_LIMIT}"
        ));
    }
    Ok(limit)
}

fn print_usage() {
    println!("REPS2 Developer CLI (machine-readable JSON on stdout)");
    println!();
    println!("  reps2 [--log-dir <directory>] dev sessions [--limit N]");
    println!("  reps2 [--log-dir <directory>] dev inspect <latest|session-id>");
    println!();
    println!("sessions lists recent diagnostic sessions.");
    println!("inspect returns the manifest, complete event stream, diagnostics, and integrity warnings.");
    println!("The default diagnostics root follows the platform-specific REPS2 log directory.");
}

fn report_error(code: &str, message: &str) -> Result<(), String> {
    println!(
        concat!(
            "{{\"schema_version\":{},",
            "\"kind\":\"reps2.developer.error\",",
            "\"status\":\"error\",",
            "\"error\":{{\"code\":{},\"message\":{}}}}}"
        ),
        REPORT_SCHEMA_VERSION,
        json_string(code),
        json_string(message),
    );
    Err(message.to_owned())
}

fn resolve_session_id(root: &Path, requested: &str) -> Result<String, String> {
    if requested == "latest" {
        return session_directories(root)
            .map_err(|error| format!("cannot read diagnostics root {}: {error}", root.display()))?
            .last()
            .and_then(|path| path.file_name())
            .and_then(|name| name.to_str())
            .map(str::to_owned)
            .ok_or_else(|| format!("no REPS2 diagnostic sessions exist under {}", root.display()));
    }

    if !valid_session_id(requested) {
        return Err(format!(
            "invalid session identifier {requested:?}; expected latest or a session-* identifier"
        ));
    }
    Ok(requested.to_owned())
}

fn valid_session_id(value: &str) -> bool {
    value.starts_with("session-")
        && value.len() > "session-".len()
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
}

fn session_directories(root: &Path) -> io::Result<Vec<PathBuf>> {
    let entries = match fs::read_dir(root) {
        Ok(entries) => entries,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(Vec::new()),
        Err(error) => return Err(error),
    };
    let mut directories = Vec::new();
    for entry in entries {
        let entry = entry?;
        let file_type = entry.file_type()?;
        let name = entry.file_name();
        if file_type.is_dir()
            && name
                .to_str()
                .is_some_and(|name| valid_session_id(name))
        {
            directories.push(entry.path());
        }
    }
    directories.sort_by(|left, right| left.file_name().cmp(&right.file_name()));
    Ok(directories)
}

fn load_session(root: &Path, id: &str) -> Result<SessionData, String> {
    if !valid_session_id(id) {
        return Err(format!("invalid REPS2 diagnostic session identifier: {id:?}"));
    }
    let directory = root.join(id);
    if !directory.is_dir() {
        return Err(format!(
            "diagnostic session {id:?} was not found under {}",
            root.display()
        ));
    }

    let mut integrity_errors = Vec::new();
    let manifest_path = directory.join("session.json");
    let manifest_json = match fs::read_to_string(&manifest_path) {
        Ok(contents) if is_json_value(contents.trim()) => Some(contents.trim().to_owned()),
        Ok(_) => {
            integrity_errors.push("session.json is not valid JSON".to_owned());
            None
        }
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            integrity_errors.push("session.json is missing; the process may have terminated before finalization".to_owned());
            None
        }
        Err(error) => {
            integrity_errors.push(format!("cannot read session.json: {error}"));
            None
        }
    };

    let mut events = Vec::new();
    let mut diagnostic_events = Vec::new();
    let events_path = directory.join("events.jsonl");
    match fs::read_to_string(&events_path) {
        Ok(contents) => {
            for (line_number, line) in contents.lines().enumerate() {
                let line = line.trim();
                if line.is_empty() {
                    continue;
                }
                if !is_json_value(line) || !line.starts_with('{') {
                    integrity_errors.push(format!(
                        "events.jsonl line {} is not a valid JSON object",
                        line_number + 1
                    ));
                    continue;
                }
                let event = line.to_owned();
                if event.contains("\"severity\":\"warning\"")
                    || (event.contains("\"severity\":\"error\"")
                        && !event.contains("\"event\":\"session.finished\""))
                {
                    diagnostic_events.push(event.clone());
                }
                events.push(event);
            }
        }
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            integrity_errors.push("events.jsonl is missing".to_owned());
        }
        Err(error) => integrity_errors.push(format!("cannot read events.jsonl: {error}")),
    }

    Ok(SessionData {
        id: id.to_owned(),
        directory,
        manifest_json,
        events,
        diagnostics: diagnostic_events,
        integrity_errors,
    })
}

fn session_list_json(root: &Path, limit: usize) -> io::Result<String> {
    let mut sessions = session_directories(root)?;
    sessions.reverse();
    sessions.truncate(limit);

    let mut entries = Vec::with_capacity(sessions.len());
    for directory in sessions {
        let Some(id) = directory.file_name().and_then(|name| name.to_str()) else {
            continue;
        };
        let session = load_session(root, id).map_err(io::Error::other)?;
        let manifest = session
            .manifest_json
            .as_deref()
            .unwrap_or("null");
        let integrity_errors = render_string_array(&session.integrity_errors);
        entries.push(format!(
            concat!(
                "{{\"session_id\":{},\"directory\":{},\"complete\":{},",
                "\"event_count\":{},\"diagnostic_count\":{},",
                "\"manifest\":{},\"integrity_errors\":{}}}"
            ),
            json_string(&session.id),
            json_string(&session.directory.to_string_lossy()),
            session.is_complete(),
            session.events.len(),
            session.diagnostics.len(),
            manifest,
            integrity_errors,
        ));
    }

    Ok(format!(
        concat!(
            "{{\"schema_version\":{},",
            "\"kind\":\"reps2.developer.session_list\",",
            "\"status\":\"ok\",\"log_root\":{},",
            "\"count\":{},\"sessions\":[{}]}}"
        ),
        REPORT_SCHEMA_VERSION,
        json_string(&root.to_string_lossy()),
        entries.len(),
        entries.join(","),
    ))
}

fn session_inspection_json(session: &SessionData) -> String {
    let manifest = session.manifest_json.as_deref().unwrap_or("null");
    format!(
        concat!(
            "{{\"schema_version\":{},",
            "\"kind\":\"reps2.developer.session_inspection\",",
            "\"status\":\"ok\",\"session_id\":{},",
            "\"session_directory\":{},\"complete\":{},",
            "\"manifest\":{},\"event_count\":{},\"events\":[{}],",
            "\"diagnostic_count\":{},\"diagnostics\":[{}],",
            "\"integrity_errors\":{}}}"
        ),
        REPORT_SCHEMA_VERSION,
        json_string(&session.id),
        json_string(&session.directory.to_string_lossy()),
        session.is_complete(),
        manifest,
        session.events.len(),
        session.events.join(","),
        session.diagnostics.len(),
        session.diagnostics.join(","),
        render_string_array(&session.integrity_errors),
    )
}

fn render_string_array(values: &[String]) -> String {
    format!(
        "[{}]",
        values
            .iter()
            .map(|value| json_string(value))
            .collect::<Vec<_>>()
            .join(",")
    )
}

fn is_json_value(source: &str) -> bool {
    let mut parser = JsonValidator {
        bytes: source.as_bytes(),
        position: 0,
    };
    parser.skip_whitespace();
    if !parser.parse_value() {
        return false;
    }
    parser.skip_whitespace();
    parser.position == parser.bytes.len()
}

struct JsonValidator<'a> {
    bytes: &'a [u8],
    position: usize,
}

impl JsonValidator<'_> {
    fn skip_whitespace(&mut self) {
        while self
            .bytes
            .get(self.position)
            .is_some_and(u8::is_ascii_whitespace)
        {
            self.position += 1;
        }
    }

    fn consume(&mut self, expected: u8) -> bool {
        if self.bytes.get(self.position) == Some(&expected) {
            self.position += 1;
            true
        } else {
            false
        }
    }

    fn parse_value(&mut self) -> bool {
        self.skip_whitespace();
        match self.bytes.get(self.position).copied() {
            Some(b'{') => self.parse_object(),
            Some(b'[') => self.parse_array(),
            Some(b'"') => self.parse_string(),
            Some(b't') => self.parse_literal(b"true"),
            Some(b'f') => self.parse_literal(b"false"),
            Some(b'n') => self.parse_literal(b"null"),
            Some(b'-' | b'0'..=b'9') => self.parse_number(),
            _ => false,
        }
    }

    fn parse_object(&mut self) -> bool {
        self.position += 1;
        self.skip_whitespace();
        if self.consume(b'}') {
            return true;
        }
        loop {
            if !self.parse_string() {
                return false;
            }
            self.skip_whitespace();
            if !self.consume(b':') || !self.parse_value() {
                return false;
            }
            self.skip_whitespace();
            if self.consume(b'}') {
                return true;
            }
            if !self.consume(b',') {
                return false;
            }
            self.skip_whitespace();
        }
    }

    fn parse_array(&mut self) -> bool {
        self.position += 1;
        self.skip_whitespace();
        if self.consume(b']') {
            return true;
        }
        loop {
            if !self.parse_value() {
                return false;
            }
            self.skip_whitespace();
            if self.consume(b']') {
                return true;
            }
            if !self.consume(b',') {
                return false;
            }
            self.skip_whitespace();
        }
    }

    fn parse_string(&mut self) -> bool {
        if !self.consume(b'"') {
            return false;
        }
        while let Some(byte) = self.bytes.get(self.position).copied() {
            self.position += 1;
            match byte {
                b'"' => return true,
                b'\\' => {
                    let Some(escaped) = self.bytes.get(self.position).copied() else {
                        return false;
                    };
                    self.position += 1;
                    match escaped {
                        b'"' | b'\\' | b'/' | b'b' | b'f' | b'n' | b'r' | b't' => {}
                        b'u' => {
                            for _ in 0..4 {
                                let Some(hex) = self.bytes.get(self.position).copied() else {
                                    return false;
                                };
                                if !hex.is_ascii_hexdigit() {
                                    return false;
                                }
                                self.position += 1;
                            }
                        }
                        _ => return false,
                    }
                }
                0x00..=0x1F => return false,
                _ => {}
            }
        }
        false
    }

    fn parse_number(&mut self) -> bool {
        self.consume(b'-');
        match self.bytes.get(self.position).copied() {
            Some(b'0') => self.position += 1,
            Some(b'1'..=b'9') => {
                self.position += 1;
                while self
                    .bytes
                    .get(self.position)
                    .is_some_and(u8::is_ascii_digit)
                {
                    self.position += 1;
                }
            }
            _ => return false,
        }
        if self.consume(b'.') {
            let start = self.position;
            while self
                .bytes
                .get(self.position)
                .is_some_and(u8::is_ascii_digit)
            {
                self.position += 1;
            }
            if self.position == start {
                return false;
            }
        }
        if self
            .bytes
            .get(self.position)
            .is_some_and(|byte| matches!(byte, b'e' | b'E'))
        {
            self.position += 1;
            if self
                .bytes
                .get(self.position)
                .is_some_and(|byte| matches!(byte, b'+' | b'-'))
            {
                self.position += 1;
            }
            let start = self.position;
            while self
                .bytes
                .get(self.position)
                .is_some_and(u8::is_ascii_digit)
            {
                self.position += 1;
            }
            if self.position == start {
                return false;
            }
        }
        true
    }

    fn parse_literal(&mut self, literal: &[u8]) -> bool {
        if self.bytes.get(self.position..self.position + literal.len()) == Some(literal) {
            self.position += literal.len();
            true
        } else {
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    struct TestDirectory(PathBuf);

    impl TestDirectory {
        fn new() -> Self {
            static NEXT: AtomicUsize = AtomicUsize::new(0);
            let path = std::env::temp_dir().join(format!(
                "reps2-developer-cli-{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir_all(&path).expect("create temporary log root");
            Self(path)
        }
    }

    impl Drop for TestDirectory {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn write_session(root: &Path, id: &str, outcome: &str, events: &[&str]) {
        let directory = root.join(id);
        fs::create_dir_all(&directory).expect("create session directory");
        fs::write(
            directory.join("session.json"),
            format!(
                "{{\"schema_version\":1,\"session_id\":{},\"command\":\"version\",\"outcome\":{}}}",
                json_string(id),
                json_string(outcome),
            ),
        )
        .expect("write session manifest");
        fs::write(
            directory.join("events.jsonl"),
            events.join("\n") + "\n",
        )
        .expect("write event stream");
    }

    #[test]
    fn validates_json_values_and_rejects_malformed_records() {
        for valid in [
            r#"{"x":[1,true,null,{"text":"escaped \" quote and 🧪"}]}"#,
            r#""plain string""#,
            r#"[-1,0,1.25,1e+2]"#,
        ] {
            assert!(is_json_value(valid), "{valid}");
        }
        for invalid in [
            r#"{"x":}"#,
            r#"{"x":1,}"#,
            r#"{"x":"bad\q"}"#,
            r#"{"x":01}"#,
            r#"{"x":[1,]}"#,
            r#"{"x":"unterminated}"#,
            r#"{} trailing"#,
        ] {
            assert!(!is_json_value(invalid), "{invalid}");
        }
    }

    #[test]
    fn inspection_returns_manifest_event_objects_and_diagnostics() {
        let temp = TestDirectory::new();
        let id = "session-1000-42-0";
        write_session(
            &temp.0,
            id,
            "failed",
            &[
                r#"{"severity":"info","event":"command.started"}"#,
                r#"{"severity":"error","event":"command.failed","context":{"error":"bad ELF"}}"#,
                r#"{"severity":"error","event":"session.finished"}"#,
            ],
        );
        let session = load_session(&temp.0, id).expect("load session");
        let report = session_inspection_json(&session);
        assert!(session.is_complete());
        assert!(report.contains(r#""manifest":{"schema_version":1"#));
        assert!(report.contains(r#""events":[{"severity":"info""#));
        assert!(report.contains(r#""diagnostics":[{"severity":"error","event":"command.failed""#));
        assert!(report.contains(r#""event_count":3"#));
        assert!(report.contains(r#""diagnostic_count":1"#));
        assert!(is_json_value(&report));
    }

    #[test]
    fn interrupted_sessions_are_reported_as_partial_and_keep_valid_events() {
        let temp = TestDirectory::new();
        let id = "session-2000-42-0";
        let directory = temp.0.join(id);
        fs::create_dir_all(&directory).expect("create session directory");
        fs::write(
            directory.join("events.jsonl"),
            r#"{"severity":"info","event":"session.started"}"#,
        )
        .expect("write partial events");
        let session = load_session(&temp.0, id).expect("load partial session");
        assert!(!session.is_complete());
        assert!(session.manifest_json.is_none());
        assert_eq!(session.events.len(), 1);
        assert_eq!(session.integrity_errors.len(), 1);
    }

    #[test]
    fn malformed_event_lines_are_excluded_and_reported() {
        let temp = TestDirectory::new();
        let id = "session-3000-42-0";
        write_session(
            &temp.0,
            id,
            "succeeded",
            &[
                r#"{"severity":"info","event":"session.started"}"#,
                r#"{"severity":"info","event":}"#,
            ],
        );
        let session = load_session(&temp.0, id).expect("load session");
        assert_eq!(session.events.len(), 1);
        assert_eq!(session.integrity_errors.len(), 1);
        let report = session_inspection_json(&session);
        assert!(is_json_value(&report));
        assert!(report.contains("events.jsonl line 2"));
    }

    #[test]
    fn latest_session_is_selected_by_timestamped_session_identifier() {
        let temp = TestDirectory::new();
        write_session(&temp.0, "session-1000-1-0", "succeeded", &[]);
        write_session(&temp.0, "session-2000-1-0", "succeeded", &[]);
        let latest = resolve_session_id(&temp.0, "latest").expect("resolve latest");
        assert_eq!(latest, "session-2000-1-0");
    }

    #[test]
    fn session_identifiers_cannot_escape_the_diagnostics_root() {
        assert!(!valid_session_id("../session-1"));
        assert!(!valid_session_id("session-../secret"));
        assert!(!valid_session_id("session-"));
        assert!(valid_session_id("session-1234-42-0"));
    }

    #[test]
    fn missing_log_root_lists_as_an_empty_machine_readable_report() {
        let temp = TestDirectory::new();
        let missing = temp.0.join("not-created");
        let report = session_list_json(&missing, DEFAULT_SESSION_LIMIT)
            .expect("missing root should be an empty list");
        assert!(report.contains(r#""count":0"#));
        assert!(is_json_value(&report));
    }
}
