// SPDX-License-Identifier: GPL-3.0-or-later
//! Agent-oriented inspection of REPS2 developer diagnostic sessions.
//!
//! The `reps2 dev` command namespace writes one JSON document to stdout so
//! IDEs and coding agents can consume session summaries and event records
//! without scraping human terminal output.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use crate::developer_json::Value;
use crate::diagnostics::codes;
use crate::json::json_string;

const REPORT_SCHEMA_VERSION: u32 = 1;
const DEFAULT_SESSION_LIMIT: usize = 20;
const MAX_SESSION_LIMIT: usize = 100;

/// Envelope discriminator emitted by `reps2 dev sessions`.
const KIND_SESSION_LIST: &str = "reps2.developer.session_list";
/// Envelope discriminator emitted by `reps2 dev inspect`.
const KIND_SESSION_INSPECTION: &str = "reps2.developer.session_inspection";
/// Envelope discriminator emitted for a `reps2 dev` error.
const KIND_ERROR: &str = "reps2.developer.error";
/// Envelope discriminator emitted by `reps2 dev capabilities`.
const KIND_CAPABILITIES: &str = "reps2.developer.capabilities";
/// Envelope discriminator of the structured session summary (writer-owned).
const KIND_SESSION_SUMMARY: &str = crate::diagnostics::SUMMARY_REPORT_KIND;
/// Envelope discriminator emitted by `reps2 dev compare`.
const KIND_SESSION_COMPARISON: &str = "reps2.developer.session_comparison";

/// Every `dev` envelope kind, in deterministic order. Published by
/// `dev capabilities` and checked against the JSON schema in tests.
const ENVELOPE_KINDS: &[&str] = &[
    KIND_CAPABILITIES,
    KIND_SESSION_LIST,
    KIND_SESSION_INSPECTION,
    KIND_SESSION_SUMMARY,
    KIND_SESSION_COMPARISON,
    KIND_ERROR,
];

/// Artifact file names that make up one diagnostics session directory.
const ARTIFACT_NAMES: &[&str] = &[
    "session.log",
    "events.jsonl",
    "session.json",
    "summary.json",
];

/// Path, relative to the repository root, of the published report JSON schema.
const REPORT_SCHEMA_PATH: &str = "docs/schemas/developer-report-v1.schema.json";

const LOG_ROOT_POLICY: &str = "Diagnostics default to the platform-specific REPS2 log \
directory (%LOCALAPPDATA%\\REPS2\\Logs on Windows, ~/Library/Logs/REPS2 on macOS, \
$XDG_STATE_HOME/reps2/logs or ~/.local/state/reps2/logs on Linux); --log-dir selects an \
explicit root and also enables capture; REPS2_LOG_DIR overrides the default root.";

const PRIVACY_NOTE: &str = "Session logs, events, and reports can include local file paths \
copied from error messages; summary.json also records the exact command arguments you invoked. \
Review diagnostic artifacts before sharing them publicly. Game bytes, ISO images, dump/firmware \
content, and credentials are never written to diagnostics.";

struct SessionData {
    id: String,
    directory: PathBuf,
    manifest_json: Option<String>,
    summary_json: Option<String>,
    events: Vec<String>,
    diagnostics: Vec<String>,
    integrity_errors: Vec<String>,
}

impl SessionData {
    fn is_complete(&self) -> bool {
        self.manifest_json.is_some()
            && self.integrity_errors.is_empty()
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
                    return report_error(codes::DEV_INVALID_ARGUMENTS, &error);
                }
            };
            match session_list_json(log_root, limit) {
                Ok(report) => {
                    println!("{report}");
                    Ok(())
                }
                Err(error) => report_error(
                    codes::DEV_SESSION_LIST_FAILED,
                    &format!("cannot list REPS2 diagnostic sessions: {error}"),
                ),
            }
        }
        Some("inspect") if arguments.len() == 2 => {
            let requested = &arguments[1];
            match resolve_session_id(log_root, requested).and_then(|id| load_session(log_root, &id))
            {
                Ok(session) => {
                    println!("{}", session_inspection_json(&session));
                    Ok(())
                }
                Err(error) => report_error(codes::DEV_SESSION_INSPECTION_FAILED, &error),
            }
        }
        Some("summary") if arguments.len() == 2 => {
            let requested = &arguments[1];
            match resolve_session_id(log_root, requested).and_then(|id| load_session(log_root, &id))
            {
                Ok(session) => match session.summary_json.as_deref() {
                    Some(summary) => {
                        println!("{summary}");
                        Ok(())
                    }
                    None => report_error(
                        codes::DEV_SUMMARY_UNAVAILABLE,
                        &format!(
                            "diagnostic session {} has no readable summary.json (older REPS2 build, or the session was not finalized)",
                            session.id
                        ),
                    ),
                },
                Err(error) => report_error(codes::DEV_SESSION_INSPECTION_FAILED, &error),
            }
        }
        Some("capabilities") if arguments.len() == 1 => {
            println!("{}", capabilities_json());
            Ok(())
        }
        Some("compare") if arguments.len() == 3 => {
            match compare_sessions(log_root, &arguments[1], &arguments[2]) {
                Ok(report) => {
                    println!("{report}");
                    Ok(())
                }
                Err((code, message)) => report_error(&code, &message),
            }
        }
        Some("help") | None => {
            print_usage();
            Ok(())
        }
        _ => report_error(
            codes::DEV_INVALID_ARGUMENTS,
            "usage: reps2 [--log-dir <directory>] dev <sessions [--limit N] | inspect <latest|session-id> | summary <latest|session-id> | compare <base> <candidate> | capabilities | help>",
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
        return Err(format!("--limit must be between 1 and {MAX_SESSION_LIMIT}"));
    }
    Ok(limit)
}

fn print_usage() {
    println!("REPS2 Developer CLI (machine-readable JSON on stdout)");
    println!();
    println!("  reps2 [--log-dir <directory>] dev sessions [--limit N]");
    println!("  reps2 [--log-dir <directory>] dev inspect <latest|session-id>");
    println!("  reps2 [--log-dir <directory>] dev summary <latest|session-id>");
    println!("  reps2 [--log-dir <directory>] dev compare <base> <candidate>");
    println!("  reps2 dev capabilities");
    println!();
    println!("sessions lists recent diagnostic sessions.");
    println!(
        "inspect returns the manifest, complete event stream, diagnostics, and integrity warnings."
    );
    println!(
        "summary returns the compact structured summary (outcome, counts, and warning/error records with stable codes, scopes, and stage ids)."
    );
    println!(
        "capabilities describes the diagnostics contract: schema versions, artifacts, envelope kinds, commands, and stable diagnostic codes."
    );
    println!("The default diagnostics root follows the platform-specific REPS2 log directory.");
}

fn report_error(code: &str, message: &str) -> Result<(), String> {
    println!(
        concat!(
            "{{\"schema_version\":{},",
            "\"kind\":{},",
            "\"status\":\"error\",",
            "\"error\":{{\"code\":{},\"message\":{}}}}}"
        ),
        REPORT_SCHEMA_VERSION,
        json_string(KIND_ERROR),
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
            .ok_or_else(|| {
                format!(
                    "no REPS2 diagnostic sessions exist under {}",
                    root.display()
                )
            });
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
        if file_type.is_dir() && name.to_str().is_some_and(valid_session_id) {
            directories.push(entry.path());
        }
    }
    directories.sort_by(|left, right| left.file_name().cmp(&right.file_name()));
    Ok(directories)
}

fn load_session(root: &Path, id: &str) -> Result<SessionData, String> {
    if !valid_session_id(id) {
        return Err(format!(
            "invalid REPS2 diagnostic session identifier: {id:?}"
        ));
    }
    let directory = root.join(id);
    let metadata = fs::symlink_metadata(&directory).map_err(|error| {
        if error.kind() == io::ErrorKind::NotFound {
            format!(
                "diagnostic session {id:?} was not found under {}",
                root.display()
            )
        } else {
            format!("cannot inspect diagnostic session {id:?}: {error}")
        }
    })?;
    if !metadata.file_type().is_dir() {
        return Err(format!(
            "diagnostic session {id:?} is not a real directory under {}",
            root.display()
        ));
    }

    let mut integrity_errors = Vec::new();
    let manifest_path = directory.join("session.json");
    let manifest_json = match fs::read_to_string(&manifest_path) {
        Ok(contents) if contents.trim().starts_with('{') && is_json_value(contents.trim()) => {
            Some(contents.trim().to_owned())
        }
        Ok(_) => {
            integrity_errors.push("session.json is not valid JSON".to_owned());
            None
        }
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            integrity_errors.push(
                "session.json is missing; the process may have terminated before finalization"
                    .to_owned(),
            );
            None
        }
        Err(error) => {
            integrity_errors.push(format!("cannot read session.json: {error}"));
            None
        }
    };

    // summary.json is absent for sessions written before the structured summary
    // existed, which is not an integrity error; a present malformed summary is.
    let summary_path = directory.join("summary.json");
    let summary_json = match fs::read_to_string(&summary_path) {
        Ok(contents) if contents.trim().starts_with('{') && is_json_value(contents.trim()) => {
            Some(contents.trim().to_owned())
        }
        Ok(_) => {
            integrity_errors.push("summary.json is not valid JSON".to_owned());
            None
        }
        Err(error) if error.kind() == io::ErrorKind::NotFound => None,
        Err(error) => {
            integrity_errors.push(format!("cannot read summary.json: {error}"));
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
        summary_json,
        events,
        diagnostics: diagnostic_events,
        integrity_errors,
    })
}

fn session_list_json(root: &Path, limit: usize) -> io::Result<String> {
    let mut sessions = session_directories(root)?;
    sessions.reverse();
    let available_count = sessions.len();
    sessions.truncate(limit);

    let mut entries = Vec::with_capacity(sessions.len());
    for directory in sessions {
        let Some(id) = directory.file_name().and_then(|name| name.to_str()) else {
            continue;
        };
        let session = load_session(root, id).map_err(io::Error::other)?;
        let manifest = session.manifest_json.as_deref().unwrap_or("null");
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
            "\"kind\":{},",
            "\"status\":\"ok\",\"log_root\":{},",
            "\"count\":{},\"available_count\":{},\"truncated\":{},",
            "\"sessions\":[{}]}}"
        ),
        REPORT_SCHEMA_VERSION,
        json_string(KIND_SESSION_LIST),
        json_string(&root.to_string_lossy()),
        entries.len(),
        available_count,
        available_count > entries.len(),
        entries.join(","),
    ))
}

fn session_inspection_json(session: &SessionData) -> String {
    let manifest = session.manifest_json.as_deref().unwrap_or("null");
    format!(
        concat!(
            "{{\"schema_version\":{},",
            "\"kind\":{},",
            "\"status\":\"ok\",\"session_id\":{},",
            "\"session_directory\":{},\"complete\":{},",
            "\"manifest\":{},\"event_count\":{},\"events\":[{}],",
            "\"diagnostic_count\":{},\"diagnostics\":[{}],",
            "\"integrity_errors\":{}}}"
        ),
        REPORT_SCHEMA_VERSION,
        json_string(KIND_SESSION_INSPECTION),
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

/// Renders the machine-readable developer diagnostics contract.
///
/// This is the capability-discovery document: it tells an IDE or coding agent
/// which schema versions, artifacts, envelope kinds, commands, and stable
/// diagnostic codes the `dev` namespace currently provides, so the agent does
/// not have to hardcode them. The content is deterministic.
fn capabilities_json() -> String {
    let artifacts: Vec<String> = ARTIFACT_NAMES
        .iter()
        .map(|name| json_string(name))
        .collect();
    let envelope_kinds: Vec<String> = ENVELOPE_KINDS
        .iter()
        .map(|kind| json_string(kind))
        .collect();

    let commands: Vec<String> = [
        (
            "sessions",
            "reps2 [--log-dir <directory>] dev sessions [--limit N]",
            "List recent diagnostic sessions with manifest summaries, event counts, diagnostic counts, and integrity warnings.",
        ),
        (
            "inspect",
            "reps2 [--log-dir <directory>] dev inspect <latest|session-id>",
            "Return one session's manifest, full event stream, warning/error diagnostics, and integrity errors.",
        ),
        (
            "summary",
            "reps2 [--log-dir <directory>] dev summary <latest|session-id>",
            "Return the compact structured summary: outcome, counts, warning/error records with stable code, scope, and stage id, and the recorded invocation for reproduction.",
        ),
        (
            "compare",
            "reps2 [--log-dir <directory>] dev compare <base> <candidate>",
            "Deterministically compare two session summaries: outcome/command changes and the failure signatures added or removed.",
        ),
        (
            "capabilities",
            "reps2 dev capabilities",
            "Describe this developer diagnostics contract: schema versions, artifacts, envelope kinds, commands, and stable codes.",
        ),
        (
            "help",
            "reps2 dev help",
            "Print human-readable usage for the dev namespace.",
        ),
    ]
    .iter()
    .map(|&(name, usage, description)| {
        format!(
            "{{\"name\":{},\"usage\":{},\"description\":{}}}",
            json_string(name),
            json_string(usage),
            json_string(description),
        )
    })
    .collect();

    let diagnostic_codes: Vec<String> = codes::ALL
        .iter()
        .map(|code| {
            format!(
                "{{\"code\":{},\"severity\":{},\"scope\":{},\"surface\":{},\"description\":{}}}",
                json_string(code.id),
                json_string(code.severity),
                json_string(code.scope),
                json_string(code.surface),
                json_string(code.description),
            )
        })
        .collect();

    format!(
        concat!(
            "{{\"schema_version\":{},",
            "\"kind\":{},",
            "\"status\":\"ok\",",
            "\"tool_version\":{},",
            "\"report_schema_version\":{},",
            "\"event_schema_version\":{},",
            "\"session_manifest_schema_version\":{},",
            "\"report_schema_path\":{},",
            "\"artifact_names\":[{}],",
            "\"envelope_kinds\":[{}],",
            "\"commands\":[{}],",
            "\"diagnostic_codes\":[{}],",
            "\"log_root_policy\":{{\"env_override\":\"REPS2_LOG_DIR\",\"description\":{}}},",
            "\"privacy\":{}",
            "}}"
        ),
        REPORT_SCHEMA_VERSION,
        json_string(KIND_CAPABILITIES),
        json_string(crate::VERSION),
        REPORT_SCHEMA_VERSION,
        crate::diagnostics::EVENT_SCHEMA_VERSION,
        crate::diagnostics::SESSION_MANIFEST_SCHEMA_VERSION,
        json_string(REPORT_SCHEMA_PATH),
        artifacts.join(","),
        envelope_kinds.join(","),
        commands.join(","),
        diagnostic_codes.join(","),
        json_string(LOG_ROOT_POLICY),
        json_string(PRIVACY_NOTE),
    )
}

/// The subset of a session summary needed to compare two sessions.
struct SummaryView {
    session_id: String,
    command: String,
    outcome: String,
    diagnostic_count: i64,
    failure_signatures: Vec<String>,
}

/// Loads and validates one session summary for comparison, failing closed.
fn load_summary_view(root: &Path, requested: &str) -> Result<SummaryView, (String, String)> {
    let session = resolve_session_id(root, requested)
        .and_then(|id| load_session(root, &id))
        .map_err(|error| (codes::DEV_SESSION_INSPECTION_FAILED.to_owned(), error))?;
    let Some(document) = session.summary_json.as_deref() else {
        return Err((
            codes::DEV_SUMMARY_UNAVAILABLE.to_owned(),
            format!(
                "diagnostic session {} has no readable summary.json (older REPS2 build, or the session was not finalized)",
                session.id
            ),
        ));
    };
    summarize_document(document).map_err(|error| {
        (
            codes::DEV_SUMMARY_UNAVAILABLE.to_owned(),
            format!(
                "diagnostic session {} summary is not a valid REPS2 summary: {error}",
                session.id
            ),
        )
    })
}

/// Extracts the comparison view from a summary document.
///
/// A failure signature is the deterministic `code|scope|stage_id|event` tuple;
/// message text is intentionally excluded so transient path text does not make
/// otherwise-identical failures look different.
fn summarize_document(document: &str) -> Result<SummaryView, String> {
    let value = crate::developer_json::parse(document)?;
    let string_field = |name: &str| {
        value
            .get(name)
            .and_then(Value::as_str)
            .map(str::to_owned)
            .ok_or_else(|| format!("missing string field {name}"))
    };
    let session_id = string_field("session_id")?;
    let command = string_field("command")?;
    let outcome = string_field("outcome")?;
    let diagnostic_count = value
        .get("diagnostic_count")
        .and_then(Value::as_i64)
        .ok_or_else(|| "missing integer field diagnostic_count".to_owned())?;
    let failures = value
        .get("failures")
        .and_then(Value::as_array)
        .ok_or_else(|| "missing array field failures".to_owned())?;

    let mut failure_signatures = Vec::with_capacity(failures.len());
    for failure in failures {
        let field = |name: &str| failure.get(name).and_then(Value::as_str).unwrap_or("");
        failure_signatures.push(format!(
            "{}|{}|{}|{}",
            field("code"),
            field("scope"),
            field("stage_id"),
            field("event")
        ));
    }
    failure_signatures.sort();

    Ok(SummaryView {
        session_id,
        command,
        outcome,
        diagnostic_count,
        failure_signatures,
    })
}

/// Sorted-multiset difference: the items of `left` not matched in `right`.
fn multiset_difference(left: &[String], right: &[String]) -> Vec<String> {
    let mut right_index = 0;
    let mut difference = Vec::new();
    for item in left {
        while right_index < right.len() && &right[right_index] < item {
            right_index += 1;
        }
        if right_index < right.len() && &right[right_index] == item {
            right_index += 1;
        } else {
            difference.push(item.clone());
        }
    }
    difference
}

fn summary_view_json(view: &SummaryView) -> String {
    format!(
        concat!(
            "{{\"session_id\":{},\"command\":{},\"outcome\":{},\"diagnostic_count\":{},",
            "\"failure_count\":{},\"failure_signatures\":[{}]}}"
        ),
        json_string(&view.session_id),
        json_string(&view.command),
        json_string(&view.outcome),
        view.diagnostic_count,
        view.failure_signatures.len(),
        view.failure_signatures
            .iter()
            .map(|signature| json_string(signature))
            .collect::<Vec<_>>()
            .join(","),
    )
}

fn comparison_json(base: &SummaryView, candidate: &SummaryView) -> String {
    let added = multiset_difference(&candidate.failure_signatures, &base.failure_signatures);
    let removed = multiset_difference(&base.failure_signatures, &candidate.failure_signatures);
    let command_changed = base.command != candidate.command;
    let outcome_changed = base.outcome != candidate.outcome;
    let identical = !command_changed && !outcome_changed && added.is_empty() && removed.is_empty();
    format!(
        concat!(
            "{{\"schema_version\":{},",
            "\"kind\":{},",
            "\"status\":\"ok\",",
            "\"base\":{},",
            "\"candidate\":{},",
            "\"identical\":{},",
            "\"command_changed\":{},",
            "\"outcome_changed\":{},",
            "\"added\":[{}],",
            "\"removed\":[{}]}}"
        ),
        REPORT_SCHEMA_VERSION,
        json_string(KIND_SESSION_COMPARISON),
        summary_view_json(base),
        summary_view_json(candidate),
        identical,
        command_changed,
        outcome_changed,
        added
            .iter()
            .map(|signature| json_string(signature))
            .collect::<Vec<_>>()
            .join(","),
        removed
            .iter()
            .map(|signature| json_string(signature))
            .collect::<Vec<_>>()
            .join(","),
    )
}

fn compare_sessions(root: &Path, base: &str, candidate: &str) -> Result<String, (String, String)> {
    let base = load_summary_view(root, base)?;
    let candidate = load_summary_view(root, candidate)?;
    Ok(comparison_json(&base, &candidate))
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
        depth: 0,
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
    depth: usize,
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
            Some(b'{') => {
                if self.depth >= 128 {
                    return false;
                }
                self.depth += 1;
                let valid = self.parse_object();
                self.depth -= 1;
                valid
            }
            Some(b'[') => {
                if self.depth >= 128 {
                    return false;
                }
                self.depth += 1;
                let valid = self.parse_array();
                self.depth -= 1;
                valid
            }
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
        fs::write(directory.join("events.jsonl"), events.join("\n") + "\n")
            .expect("write event stream");
    }

    #[test]
    fn capabilities_report_is_valid_deterministic_json() {
        let report = capabilities_json();
        assert!(is_json_value(&report), "{report}");
        assert!(report.contains(r#""kind":"reps2.developer.capabilities""#));
        assert!(report.contains(r#""report_schema_version":1"#));
        assert!(report.contains(r#""event_schema_version":1"#));
        assert!(report.contains(r#""session_manifest_schema_version":1"#));
        assert!(report.contains(
            r#""artifact_names":["session.log","events.jsonl","session.json","summary.json"]"#
        ));
        for kind in ENVELOPE_KINDS {
            assert!(report.contains(&json_string(kind)), "missing kind {kind}");
        }
        for code in codes::ALL {
            assert!(
                report.contains(&json_string(code.id)),
                "missing code {}",
                code.id
            );
        }
        assert_eq!(
            report,
            capabilities_json(),
            "capabilities output is not stable"
        );
    }

    #[test]
    fn summary_is_exposed_when_present_and_absent_without_integrity_error() {
        let temp = TestDirectory::new();
        let with_summary = "session-1000-1-0";
        write_session(&temp.0, with_summary, "failed", &[]);
        let summary = r#"{"schema_version":1,"kind":"reps2.developer.session_summary","status":"ok","failures":[]}"#;
        fs::write(temp.0.join(with_summary).join("summary.json"), summary)
            .expect("write summary.json");
        let loaded = load_session(&temp.0, with_summary).expect("load session with summary");
        assert_eq!(loaded.summary_json.as_deref(), Some(summary));
        assert!(loaded.integrity_errors.is_empty());

        let without = "session-2000-1-0";
        write_session(&temp.0, without, "succeeded", &[]);
        let loaded = load_session(&temp.0, without).expect("load session without summary");
        assert!(loaded.summary_json.is_none());
        assert!(
            loaded.integrity_errors.is_empty(),
            "a missing summary.json must not be an integrity error: {:?}",
            loaded.integrity_errors
        );
    }

    #[test]
    fn malformed_summary_is_reported_as_an_integrity_error() {
        let temp = TestDirectory::new();
        let id = "session-3000-1-0";
        write_session(&temp.0, id, "succeeded", &[]);
        fs::write(temp.0.join(id).join("summary.json"), "{not json")
            .expect("write malformed summary");
        let loaded = load_session(&temp.0, id).expect("load session");
        assert!(loaded.summary_json.is_none());
        assert!(
            loaded
                .integrity_errors
                .iter()
                .any(|error| error.contains("summary.json"))
        );
    }

    #[test]
    fn comparison_reports_added_and_removed_failure_signatures() {
        let base = summarize_document(
            r#"{"session_id":"session-1","command":"recompile-elf","outcome":"failed","diagnostic_count":2,"failures":[
                {"event":"pipeline.stage.failed","code":"REPS2_PIPELINE_STAGE_FAILED","scope":"pipeline_stage","stage_id":"cfg.discover"},
                {"event":"command.failed","code":"REPS2_COMMAND_FAILED","scope":"command","stage_id":null}
            ]}"#,
        )
        .expect("base summary");
        let candidate = summarize_document(
            r#"{"session_id":"session-2","command":"recompile-elf","outcome":"failed","diagnostic_count":2,"failures":[
                {"event":"pipeline.stage.failed","code":"REPS2_PIPELINE_STAGE_FAILED","scope":"pipeline_stage","stage_id":"ir.lift"},
                {"event":"command.failed","code":"REPS2_COMMAND_FAILED","scope":"command","stage_id":null}
            ]}"#,
        )
        .expect("candidate summary");

        let report = comparison_json(&base, &candidate);
        assert!(is_json_value(&report), "{report}");
        assert!(report.contains(r#""identical":false"#));
        assert!(report.contains(r#""command_changed":false"#));
        assert!(report.contains(r#""outcome_changed":false"#));
        assert!(report.contains(
            r#""added":["REPS2_PIPELINE_STAGE_FAILED|pipeline_stage|ir.lift|pipeline.stage.failed"]"#
        ));
        assert!(report.contains(
            r#""removed":["REPS2_PIPELINE_STAGE_FAILED|pipeline_stage|cfg.discover|pipeline.stage.failed"]"#
        ));
    }

    #[test]
    fn identical_summaries_compare_as_identical() {
        let document = r#"{"session_id":"a","command":"analyze-elf","outcome":"succeeded","diagnostic_count":0,"failures":[]}"#;
        let view = summarize_document(document).expect("summary");
        let report = comparison_json(&view, &view);
        assert!(report.contains(r#""identical":true"#));
        assert!(report.contains(r#""added":[]"#));
        assert!(report.contains(r#""removed":[]"#));
    }

    #[test]
    fn multiset_difference_respects_multiplicity() {
        let left = vec!["a".to_owned(), "a".to_owned(), "b".to_owned()];
        let right = vec!["a".to_owned()];
        assert_eq!(
            multiset_difference(&left, &right),
            vec!["a".to_owned(), "b".to_owned()]
        );
        assert!(multiset_difference(&right, &left).is_empty());
    }

    #[test]
    fn summarize_document_fails_closed_on_invalid_summaries() {
        assert!(summarize_document("{").is_err());
        assert!(summarize_document(r#"{"session_id":"a"}"#).is_err());
        assert!(summarize_document(
            r#"{"session_id":"a","command":"c","outcome":"failed","diagnostic_count":"x","failures":[]}"#
        )
        .is_err());
    }

    #[test]
    fn validates_session_list_limits() {
        assert_eq!(
            parse_session_limit(&[]).expect("default limit"),
            DEFAULT_SESSION_LIMIT
        );
        assert_eq!(
            parse_session_limit(&["--limit".to_owned(), "5".to_owned()]).expect("explicit limit"),
            5
        );
        for arguments in [
            vec!["--limit".to_owned()],
            vec!["--limit".to_owned(), "0".to_owned()],
            vec!["--limit".to_owned(), "101".to_owned()],
            vec!["--other".to_owned(), "5".to_owned()],
            vec!["--limit".to_owned(), "many".to_owned()],
            vec!["--limit".to_owned(), "5".to_owned(), "extra".to_owned()],
        ] {
            assert!(parse_session_limit(&arguments).is_err(), "{arguments:?}");
        }
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
        let deeply_nested = format!("{}0{}", "[".repeat(129), "]".repeat(129));
        assert!(!is_json_value(&deeply_nested));

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
        assert!(!session.is_complete());
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

    #[cfg(unix)]
    #[test]
    fn inspection_rejects_session_directory_symlinks() {
        use std::os::unix::fs::symlink;

        let temp = TestDirectory::new();
        let outside = temp.0.join("outside");
        fs::create_dir_all(&outside).expect("create outside directory");
        fs::write(outside.join("session.json"), "{}").expect("write outside manifest");
        fs::write(outside.join("events.jsonl"), "").expect("write outside event stream");
        symlink(&outside, temp.0.join("session-4000-42-0")).expect("create session symlink");

        let error = match load_session(&temp.0, "session-4000-42-0") {
            Ok(_) => panic!("session symlink must not be followed"),
            Err(error) => error,
        };
        assert!(error.contains("not a real directory"));
    }

    #[test]
    fn session_identifiers_cannot_escape_the_diagnostics_root() {
        assert!(!valid_session_id("../session-1"));
        assert!(!valid_session_id("session-../secret"));
        assert!(!valid_session_id("session-"));
        assert!(valid_session_id("session-1234-42-0"));
    }

    #[test]
    fn session_list_reports_when_the_requested_limit_truncates_history() {
        let temp = TestDirectory::new();
        write_session(&temp.0, "session-1000-1-0", "succeeded", &[]);
        write_session(&temp.0, "session-2000-1-0", "succeeded", &[]);
        write_session(&temp.0, "session-3000-1-0", "failed", &[]);

        let report = session_list_json(&temp.0, 2).expect("bounded session list");
        assert!(report.contains(r#""count":2"#));
        assert!(report.contains(r#""available_count":3"#));
        assert!(report.contains(r#""truncated":true"#));
        assert!(report.contains(r#""session_id":"session-3000-1-0""#));
        assert!(!report.contains("session-1000-1-0"));
        assert!(is_json_value(&report));
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
