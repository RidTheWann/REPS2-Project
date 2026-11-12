// SPDX-License-Identifier: GPL-3.0-or-later
//! Session-scoped developer diagnostics for the REPS2 frontend.
//!
//! This records host-side command lifecycle events. It deliberately does not
//! add logging to translated guest instructions or create a Rust/C++ per-op
//! boundary; runtime tracing remains a separate, bounded runtime concern.

use std::env;
use std::fs::{self, File, OpenOptions};
use std::io::{self, BufWriter, Write};
use std::path::{Path, PathBuf};
use std::time::{Instant, SystemTime, UNIX_EPOCH};

use crate::json::{json_optional_string, json_string};

/// Version of the JSON event envelope written to `events.jsonl`.
pub const EVENT_SCHEMA_VERSION: u32 = 1;

/// Version of the `session.json` session manifest.
pub const SESSION_MANIFEST_SCHEMA_VERSION: u32 = 1;

/// Stable, machine-readable diagnostic codes emitted by the frontend.
///
/// Codes are part of the developer contract: agents, tests, and IDEs match on
/// them instead of scraping human-readable messages. New codes may be added,
/// but an existing code's meaning must not change without a schema version
/// bump. [`codes::ALL`] is deterministic and is surfaced by
/// `reps2 dev capabilities` so tooling can discover the catalog.
pub mod codes {
    /// Emitted as `error_code` on a `pipeline.stage.failed` event.
    pub const PIPELINE_STAGE_FAILED: &str = "REPS2_PIPELINE_STAGE_FAILED";
    /// Emitted as `error_code` on a top-level `command.failed` event.
    pub const COMMAND_FAILED: &str = "REPS2_COMMAND_FAILED";
    /// `reps2 dev` argument-validation error envelope.
    pub const DEV_INVALID_ARGUMENTS: &str = "REPS2_DEV_INVALID_ARGUMENTS";
    /// `reps2 dev sessions` failure error envelope.
    pub const DEV_SESSION_LIST_FAILED: &str = "REPS2_DEV_SESSION_LIST_FAILED";
    /// `reps2 dev inspect` failure error envelope.
    pub const DEV_SESSION_INSPECTION_FAILED: &str = "REPS2_DEV_SESSION_INSPECTION_FAILED";
    /// `reps2 dev summary` missing/unreadable summary error envelope.
    pub const DEV_SUMMARY_UNAVAILABLE: &str = "REPS2_DEV_SUMMARY_UNAVAILABLE";

    /// One entry in the stable diagnostic-code catalog.
    #[derive(Clone, Copy, Debug, Eq, PartialEq)]
    pub struct Code {
        /// The stable `REPS2_*` identifier.
        pub id: &'static str,
        /// Event severity the code is attached to.
        pub severity: &'static str,
        /// Attribution scope, e.g. `pipeline_stage`, `command`, or `cli`.
        pub scope: &'static str,
        /// Where the code appears: `event` (events.jsonl) or `report` (dev stdout envelope).
        pub surface: &'static str,
        /// Short human-readable meaning.
        pub description: &'static str,
    }

    /// Every stable code, in deterministic catalog order.
    pub const ALL: &[Code] = &[
        Code {
            id: PIPELINE_STAGE_FAILED,
            severity: "error",
            scope: "pipeline_stage",
            surface: "event",
            description: "A named frontend pipeline stage failed; the event carries stage_id, stage_elapsed_ms and the original error.",
        },
        Code {
            id: COMMAND_FAILED,
            severity: "error",
            scope: "command",
            surface: "event",
            description: "A top-level REPS2 command failed; the event carries the command name and the original error.",
        },
        Code {
            id: DEV_INVALID_ARGUMENTS,
            severity: "error",
            scope: "cli",
            surface: "report",
            description: "A reps2 dev subcommand received invalid or missing arguments.",
        },
        Code {
            id: DEV_SESSION_LIST_FAILED,
            severity: "error",
            scope: "cli",
            surface: "report",
            description: "reps2 dev sessions could not read the diagnostics root.",
        },
        Code {
            id: DEV_SESSION_INSPECTION_FAILED,
            severity: "error",
            scope: "cli",
            surface: "report",
            description: "reps2 dev inspect could not load the requested session.",
        },
        Code {
            id: DEV_SUMMARY_UNAVAILABLE,
            severity: "error",
            scope: "cli",
            surface: "report",
            description: "reps2 dev summary could not read a structured session summary (absent for older sessions, or malformed).",
        },
    ];
}

/// Version of the structured session summary written to `summary.json`.
pub const SUMMARY_SCHEMA_VERSION: u32 = 1;

/// Envelope `kind` of the structured session summary document.
///
/// The summary is both a session artifact (`summary.json`) and a `dev` report
/// variant, so the kind is shared with the developer CLI.
pub const SUMMARY_REPORT_KIND: &str = "reps2.developer.session_summary";

/// Upper bound on structured diagnostic records retained for the summary.
///
/// The JSONL event stream is unbounded for the life of a command; the summary
/// retains at most this many recent warning/error records so a pathological
/// session cannot grow the in-memory summary without limit.
const MAX_RETAINED_DIAGNOSTIC_RECORDS: usize = 256;

/// One warning/error event retained for the structured session summary.
struct DiagnosticRecord {
    sequence: u64,
    severity: String,
    category: String,
    subsystem: String,
    event: String,
    message: String,
    code: Option<String>,
    scope: Option<String>,
    stage_id: Option<String>,
}

/// Looks up one string-valued context entry by key.
fn context_value<'a>(context: &[(&'a str, &'a str)], key: &str) -> Option<&'a str> {
    context
        .iter()
        .find(|(candidate, _)| *candidate == key)
        .map(|(_, value)| *value)
}

pub struct SessionLogger {
    session_id: String,
    command: String,
    arguments: Vec<String>,
    tool_version: String,
    host_platform: String,
    process_id: u32,
    started_unix_ms: u64,
    started: Instant,
    sequence: u64,
    directory: PathBuf,
    text_log: BufWriter<File>,
    events_log: BufWriter<File>,
    diagnostic_records: Vec<DiagnosticRecord>,
    diagnostic_count: u64,
    diagnostic_records_dropped: u64,
}

impl SessionLogger {
    /// Creates an isolated session directory and starts its diagnostic record.
    ///
    /// `arguments` is the filtered command line without the executable path
    /// (the subcommand first). It is recorded verbatim so a session can be
    /// reproduced; it may therefore contain local paths the user passed in.
    pub fn start(
        root: &Path,
        arguments: &[String],
        tool_version: &str,
        host_platform: &str,
    ) -> io::Result<Self> {
        fs::create_dir_all(root)?;
        let started_unix_ms = unix_time_ms();
        let process_id = std::process::id();
        let (session_id, directory) = create_session_directory(root, started_unix_ms, process_id)?;

        let text_file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(directory.join("session.log"))?;
        let events_file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(directory.join("events.jsonl"))?;

        let command = arguments.first().map(String::as_str).unwrap_or("help");
        let mut logger = Self {
            session_id,
            command: command.to_owned(),
            arguments: arguments.to_vec(),
            tool_version: tool_version.to_owned(),
            host_platform: host_platform.to_owned(),
            process_id,
            started_unix_ms,
            started: Instant::now(),
            sequence: 0,
            directory,
            text_log: BufWriter::new(text_file),
            events_log: BufWriter::new(events_file),
            diagnostic_records: Vec::new(),
            diagnostic_count: 0,
            diagnostic_records_dropped: 0,
        };

        logger.write_session_manifest(None, "running")?;
        logger.record(
            "info",
            "session",
            "frontend",
            "session.started",
            "REPS2 command session started",
            &[
                ("command", command),
                ("tool_version", tool_version),
                ("host_platform", host_platform),
            ],
        )?;
        Ok(logger)
    }

    /// Returns the directory containing this session's diagnostic artifacts.
    #[must_use]
    pub fn directory(&self) -> &Path {
        &self.directory
    }

    /// Records one host-side event in both the readable log and JSONL stream.
    ///
    /// Context values are strings so the schema remains stable and does not
    /// silently coerce guest addresses, identifiers, or status codes.
    pub fn record(
        &mut self,
        severity: &str,
        category: &str,
        subsystem: &str,
        event: &str,
        message: &str,
        context: &[(&str, &str)],
    ) -> io::Result<()> {
        self.sequence = self.sequence.saturating_add(1);
        let timestamp_unix_ms = unix_time_ms();
        let elapsed_ms = self.started.elapsed().as_millis();
        let context_json = context
            .iter()
            .map(|(key, value)| format!("{}:{}", json_string(key), json_string(value)))
            .collect::<Vec<_>>()
            .join(",");

        let mut event_json = format!(
            "{{\"schema_version\":{},\"session_id\":{},\"sequence\":{},\"timestamp_unix_ms\":{timestamp_unix_ms},\"elapsed_ms\":{elapsed_ms},\"severity\":{},\"category\":{},\"subsystem\":{},\"event\":{},\"message\":{},\"context\":{{",
            EVENT_SCHEMA_VERSION,
            json_string(&self.session_id),
            self.sequence,
            json_string(severity),
            json_string(category),
            json_string(subsystem),
            json_string(event),
            json_string(message),
        );
        event_json.push_str(&context_json);
        event_json.push_str("}}");
        writeln!(self.events_log, "{event_json}")?;

        let single_line_message = message.replace(['\r', '\n'], " ");
        writeln!(
            self.text_log,
            "[{timestamp_unix_ms}] {severity:<5} {category}/{subsystem} {event}: {single_line_message}"
        )?;

        // Retain structured warning/error records for summary.json. The terminal
        // session.finished event is excluded so it is not double-counted.
        if matches!(severity, "warning" | "error") && event != "session.finished" {
            self.diagnostic_count = self.diagnostic_count.saturating_add(1);
            if self.diagnostic_records.len() < MAX_RETAINED_DIAGNOSTIC_RECORDS {
                self.diagnostic_records.push(DiagnosticRecord {
                    sequence: self.sequence,
                    severity: severity.to_owned(),
                    category: category.to_owned(),
                    subsystem: subsystem.to_owned(),
                    event: event.to_owned(),
                    message: message.to_owned(),
                    code: context_value(context, "error_code").map(str::to_owned),
                    scope: context_value(context, "failure_scope").map(str::to_owned),
                    stage_id: context_value(context, "stage_id").map(str::to_owned),
                });
            } else {
                self.diagnostic_records_dropped = self.diagnostic_records_dropped.saturating_add(1);
            }
        }

        self.events_log.flush()?;
        self.text_log.flush()
    }

    /// Runs one frontend pipeline stage and records its lifecycle when diagnostics are enabled.
    ///
    /// Stage events are deliberately coarse-grained: callers choose stable stage IDs
    /// around parsing, analysis, lowering and artifact creation. This avoids
    /// per-instruction logging and keeps the event stream useful for agent triage.
    pub fn run_stage<T>(
        &mut self,
        stage_id: &str,
        subsystem: &str,
        operation: impl FnOnce() -> Result<T, String>,
    ) -> Result<T, String> {
        self.record(
            "info",
            "pipeline",
            subsystem,
            "pipeline.stage.started",
            "Frontend pipeline stage started",
            &[("stage_id", stage_id)],
        )
        .map_err(|error| format!("cannot record pipeline stage {stage_id} start: {error}"))?;

        let stage_started = Instant::now();
        let result = operation();
        let stage_elapsed_ms = stage_started.elapsed().as_millis().to_string();

        match result {
            Ok(value) => {
                self.record(
                    "info",
                    "pipeline",
                    subsystem,
                    "pipeline.stage.succeeded",
                    "Frontend pipeline stage completed",
                    &[
                        ("stage_id", stage_id),
                        ("stage_elapsed_ms", &stage_elapsed_ms),
                    ],
                )
                .map_err(|error| {
                    format!("pipeline stage {stage_id} completed, but its result could not be recorded: {error}")
                })?;
                Ok(value)
            }
            Err(stage_error) => {
                if let Err(log_error) = self.record(
                    "error",
                    "pipeline",
                    subsystem,
                    "pipeline.stage.failed",
                    &stage_error,
                    &[
                        ("stage_id", stage_id),
                        ("stage_elapsed_ms", &stage_elapsed_ms),
                        ("error_code", codes::PIPELINE_STAGE_FAILED),
                        ("failure_scope", "pipeline_stage"),
                        ("error", &stage_error),
                    ],
                ) {
                    return Err(format!(
                        "{stage_error}; additionally, pipeline stage {stage_id} failure could not be recorded: {log_error}"
                    ));
                }
                Err(stage_error)
            }
        }
    }

    /// Completes the session and refreshes its small manifest.
    pub fn finish(&mut self, outcome: &str, error: Option<&str>) -> io::Result<()> {
        let severity = if outcome == "succeeded" {
            "info"
        } else {
            "error"
        };
        let message = match error {
            Some(error) => format!("Command {outcome}: {error}"),
            None => format!("Command {outcome}"),
        };
        if let Some(error) = error {
            self.record(
                severity,
                "session",
                "frontend",
                "session.finished",
                &message,
                &[("error", error)],
            )?;
        } else {
            self.record(
                severity,
                "session",
                "frontend",
                "session.finished",
                &message,
                &[],
            )?;
        }

        self.events_log.flush()?;
        self.text_log.flush()?;
        let finished_unix_ms = unix_time_ms();
        self.write_session_manifest(Some(finished_unix_ms), outcome)?;
        self.write_summary(outcome, finished_unix_ms)
    }

    /// Writes a compact, structured summary derived from the retained records.
    ///
    /// Unlike the raw event stream, the summary exposes the stable diagnostic
    /// code, failure scope, and stage id as first-class fields, so an agent or
    /// IDE can triage a session without parsing the JSONL context map.
    fn write_summary(&self, outcome: &str, finished_unix_ms: u64) -> io::Result<()> {
        let failures = self
            .diagnostic_records
            .iter()
            .map(|record| {
                format!(
                    concat!(
                        "{{\"sequence\":{},\"severity\":{},\"category\":{},\"subsystem\":{},",
                        "\"event\":{},\"message\":{},\"code\":{},\"scope\":{},\"stage_id\":{}}}"
                    ),
                    record.sequence,
                    json_string(&record.severity),
                    json_string(&record.category),
                    json_string(&record.subsystem),
                    json_string(&record.event),
                    json_string(&record.message),
                    json_optional_string(record.code.as_deref()),
                    json_optional_string(record.scope.as_deref()),
                    json_optional_string(record.stage_id.as_deref()),
                )
            })
            .collect::<Vec<_>>()
            .join(",");

        let reproduction_arguments = self
            .arguments
            .iter()
            .map(|argument| json_string(argument))
            .collect::<Vec<_>>()
            .join(",");

        let document = format!(
            concat!(
                "{{\n",
                "  \"schema_version\": {},\n",
                "  \"kind\": {},\n",
                "  \"status\": \"ok\",\n",
                "  \"session_id\": {},\n",
                "  \"command\": {},\n",
                "  \"tool_version\": {},\n",
                "  \"host_platform\": {},\n",
                "  \"outcome\": {},\n",
                "  \"finished_unix_ms\": {},\n",
                "  \"event_count\": {},\n",
                "  \"diagnostic_count\": {},\n",
                "  \"diagnostic_records_dropped\": {},\n",
                "  \"reproduction\": {{\n",
                "    \"program\": \"reps2\",\n",
                "    \"arguments\": [{}]\n",
                "  }},\n",
                "  \"failures\": [{}]\n",
                "}}\n"
            ),
            SUMMARY_SCHEMA_VERSION,
            json_string(SUMMARY_REPORT_KIND),
            json_string(&self.session_id),
            json_string(&self.command),
            json_string(&self.tool_version),
            json_string(&self.host_platform),
            json_string(outcome),
            finished_unix_ms,
            self.sequence,
            self.diagnostic_count,
            self.diagnostic_records_dropped,
            reproduction_arguments,
            failures,
        );
        write_atomic(&self.directory.join("summary.json"), &document)
    }

    fn write_session_manifest(
        &self,
        finished_unix_ms: Option<u64>,
        outcome: &str,
    ) -> io::Result<()> {
        let manifest = format!(
            concat!(
                "{{\n",
                "  \"schema_version\": {},\n",
                "  \"session_id\": {},\n",
                "  \"command\": {},\n",
                "  \"tool_version\": {},\n",
                "  \"host_platform\": {},\n",
                "  \"process_id\": {},\n",
                "  \"started_unix_ms\": {},\n",
                "  \"finished_unix_ms\": {},\n",
                "  \"outcome\": {},\n",
                "  \"artifacts\": {{\n",
                "    \"text_log\": \"session.log\",\n",
                "    \"event_stream\": \"events.jsonl\"\n",
                "  }}\n",
                "}}\n"
            ),
            SESSION_MANIFEST_SCHEMA_VERSION,
            json_string(&self.session_id),
            json_string(&self.command),
            json_string(&self.tool_version),
            json_string(&self.host_platform),
            self.process_id,
            self.started_unix_ms,
            finished_unix_ms
                .map(|value| value.to_string())
                .unwrap_or_else(|| "null".to_owned()),
            json_string(outcome),
        );

        write_atomic(&self.directory.join("session.json"), &manifest)
    }
}

/// Writes `contents` to `path` atomically through a temporary file and rename.
fn write_atomic(path: &Path, contents: &str) -> io::Result<()> {
    let temporary_path = path.with_extension("json.tmp");
    fs::write(&temporary_path, contents)?;
    if path.exists() {
        fs::remove_file(path)?;
    }
    fs::rename(temporary_path, path)
}

fn create_session_directory(
    root: &Path,
    started_unix_ms: u64,
    process_id: u32,
) -> io::Result<(String, PathBuf)> {
    for attempt in 0..128_u32 {
        let session_id = format!("session-{started_unix_ms}-{process_id}-{attempt}");
        let directory = root.join(&session_id);
        match fs::create_dir(&directory) {
            Ok(()) => return Ok((session_id, directory)),
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => continue,
            Err(error) => return Err(error),
        }
    }

    Err(io::Error::new(
        io::ErrorKind::AlreadyExists,
        "could not allocate a unique REPS2 diagnostics session directory",
    ))
}

fn unix_time_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
        .min(u128::from(u64::MAX)) as u64
}

/// Resolves the default per-user diagnostics root for the current platform.
pub fn default_log_root() -> Result<PathBuf, String> {
    if let Some(path) = env::var_os("REPS2_LOG_DIR").filter(|value| !value.is_empty()) {
        return Ok(PathBuf::from(path));
    }

    #[cfg(windows)]
    {
        let root = env::var_os("LOCALAPPDATA").ok_or_else(|| {
            "LOCALAPPDATA is unavailable; pass --log-dir to choose a diagnostics folder".to_owned()
        })?;
        Ok(Path::new(&root).join("REPS2").join("Logs"))
    }

    #[cfg(target_os = "macos")]
    {
        let home = env::var_os("HOME").ok_or_else(|| {
            "HOME is unavailable; pass --log-dir to choose a diagnostics folder".to_owned()
        })?;
        Ok(Path::new(&home).join("Library").join("Logs").join("REPS2"))
    }

    #[cfg(all(not(windows), not(target_os = "macos")))]
    {
        if let Some(root) = env::var_os("XDG_STATE_HOME").filter(|value| !value.is_empty()) {
            return Ok(Path::new(&root).join("reps2").join("logs"));
        }
        let home = env::var_os("HOME").ok_or_else(|| {
            "HOME is unavailable; pass --log-dir to choose a diagnostics folder".to_owned()
        })?;
        Ok(Path::new(&home)
            .join(".local")
            .join("state")
            .join("reps2")
            .join("logs"))
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
            let path = env::temp_dir().join(format!(
                "reps2-diagnostics-{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir_all(&path).expect("create temporary diagnostics root");
            Self(path)
        }
    }

    impl Drop for TestDirectory {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    #[test]
    fn writes_readable_and_structured_events_and_final_session_manifest() {
        let temp = TestDirectory::new();
        let mut logger =
            SessionLogger::start(&temp.0, &["version".to_owned()], "0.7.0", "test-platform")
                .expect("start diagnostics session");
        let directory = logger.directory().to_path_buf();

        logger
            .record(
                "warning",
                "runtime",
                "ee",
                "memory.unsupported",
                "Unsupported MMIO access: \"unknown\"\nno state changed",
                &[("guest_pc", "0x0010A008"), ("address", "0x1000E010")],
            )
            .expect("write diagnostic event");
        logger.finish("succeeded", None).expect("finish session");

        let events = fs::read_to_string(directory.join("events.jsonl"))
            .expect("read structured event stream");
        let event_lines: Vec<_> = events.lines().collect();
        assert_eq!(event_lines.len(), 3, "start, diagnostic, and finish events");
        assert!(event_lines[0].contains("\"event\":\"session.started\""));
        assert!(event_lines[1].contains("\"severity\":\"warning\""));
        assert!(event_lines[1].contains("\"guest_pc\":\"0x0010A008\""));
        assert!(event_lines[1].contains("\"address\":\"0x1000E010\""));
        assert!(event_lines[1].contains("\\\"unknown\\\"\\nno state changed"));
        assert!(event_lines[2].contains("\"event\":\"session.finished\""));

        let human =
            fs::read_to_string(directory.join("session.log")).expect("read human-readable log");
        assert!(human.contains("session.started"));
        assert!(human.contains("runtime/ee memory.unsupported"));
        assert!(human.contains("unknown\" no state changed"));

        let manifest =
            fs::read_to_string(directory.join("session.json")).expect("read session manifest");
        assert!(manifest.contains("\"outcome\": \"succeeded\""));
        assert!(manifest.contains("\"event_stream\": \"events.jsonl\""));
        assert!(manifest.contains("\"text_log\": \"session.log\""));
    }

    #[test]
    fn failed_sessions_preserve_the_error_and_finish_state() {
        let temp = TestDirectory::new();
        let mut logger =
            SessionLogger::start(&temp.0, &["disc-info".to_owned()], "0.7.0", "test-platform")
                .expect("start diagnostics session");
        let directory = logger.directory().to_path_buf();

        logger
            .finish("failed", Some("cannot inspect input file"))
            .expect("finish failed session");

        let events = fs::read_to_string(directory.join("events.jsonl"))
            .expect("read structured event stream");
        assert!(events.contains("\"severity\":\"error\""));
        assert!(events.contains("\"error\":\"cannot inspect input file\""));
        let manifest =
            fs::read_to_string(directory.join("session.json")).expect("read session manifest");
        assert!(manifest.contains("\"outcome\": \"failed\""));
        assert!(manifest.contains("\"finished_unix_ms\":"));
        assert!(!manifest.contains("\"finished_unix_ms\": null"));
    }

    #[test]
    fn records_successful_pipeline_stage_boundaries_and_duration() {
        let temp = TestDirectory::new();
        let mut logger = SessionLogger::start(
            &temp.0,
            &["recompile-elf".to_owned()],
            "0.7.0",
            "test-platform",
        )
        .expect("start diagnostics session");
        let directory = logger.directory().to_path_buf();

        let value = logger
            .run_stage("ir.lift", "ir", || Ok::<_, String>(42))
            .expect("pipeline stage succeeds");
        assert_eq!(value, 42);
        logger.finish("succeeded", None).expect("finish session");

        let events = fs::read_to_string(directory.join("events.jsonl"))
            .expect("read structured event stream");
        assert!(events.contains("\"event\":\"pipeline.stage.started\""));
        assert!(events.contains("\"event\":\"pipeline.stage.succeeded\""));
        assert!(events.contains("\"stage_id\":\"ir.lift\""));
        assert!(events.contains("\"subsystem\":\"ir\""));
        assert!(events.contains("\"stage_elapsed_ms\":"));
        assert!(!events.contains("\"pipeline.stage.failed\""));
    }

    #[test]
    fn records_failed_pipeline_stage_with_stable_agent_error_context() {
        let temp = TestDirectory::new();
        let mut logger = SessionLogger::start(
            &temp.0,
            &["recompile-elf".to_owned()],
            "0.7.0",
            "test-platform",
        )
        .expect("start diagnostics session");
        let directory = logger.directory().to_path_buf();

        let error = logger
            .run_stage::<()>("cfg.discover", "analysis", || {
                Err("CFG instruction limit exceeded at 0x0010A008".to_owned())
            })
            .expect_err("pipeline stage fails");
        assert_eq!(error, "CFG instruction limit exceeded at 0x0010A008");
        logger
            .finish("failed", Some(&error))
            .expect("finish failed session");

        let events = fs::read_to_string(directory.join("events.jsonl"))
            .expect("read structured event stream");
        assert!(events.contains("\"event\":\"pipeline.stage.failed\""));
        assert!(events.contains("\"stage_id\":\"cfg.discover\""));
        assert!(events.contains("\"subsystem\":\"analysis\""));
        assert!(events.contains("\"error_code\":\"REPS2_PIPELINE_STAGE_FAILED\""));
        assert!(events.contains("\"failure_scope\":\"pipeline_stage\""));
        assert!(events.contains("\"error\":\"CFG instruction limit exceeded at 0x0010A008\""));
    }

    #[test]
    fn concurrent_sessions_get_distinct_directories() {
        let temp = TestDirectory::new();
        let first =
            SessionLogger::start(&temp.0, &["version".to_owned()], "0.7.0", "test-platform")
                .expect("start first session");
        let second =
            SessionLogger::start(&temp.0, &["version".to_owned()], "0.7.0", "test-platform")
                .expect("start second session");
        assert_ne!(first.directory(), second.directory());
    }

    #[test]
    fn diagnostic_code_catalog_is_unique_and_namespaced() {
        let mut ids = std::collections::BTreeSet::new();
        for code in codes::ALL {
            assert!(
                code.id.starts_with("REPS2_"),
                "unnamespaced code {}",
                code.id
            );
            assert!(
                !code.description.is_empty(),
                "{} has no description",
                code.id
            );
            assert!(
                matches!(code.surface, "event" | "report"),
                "{} has an unknown surface {}",
                code.id,
                code.surface
            );
            assert!(ids.insert(code.id), "duplicate diagnostic code {}", code.id);
        }
        assert_eq!(ids.len(), codes::ALL.len());
        assert!(!codes::ALL.is_empty());
    }

    #[test]
    fn summaries_capture_structured_failure_fields() {
        let temp = TestDirectory::new();
        let mut logger = SessionLogger::start(
            &temp.0,
            &["recompile-elf".to_owned()],
            "0.7.0",
            "test-platform",
        )
        .expect("start diagnostics session");
        let directory = logger.directory().to_path_buf();

        logger
            .run_stage::<()>("cfg.discover", "analysis", || {
                Err("CFG limit exceeded at 0x0010A008".to_owned())
            })
            .expect_err("stage fails");
        logger
            .finish("failed", Some("CFG limit exceeded at 0x0010A008"))
            .expect("finish failed session");

        let summary =
            fs::read_to_string(directory.join("summary.json")).expect("read structured summary");
        assert!(summary.contains(r#""kind": "reps2.developer.session_summary""#));
        assert!(summary.contains(r#""outcome": "failed""#));
        assert!(summary.contains(r#""diagnostic_count": 1"#));
        assert!(summary.contains(r#""code":"REPS2_PIPELINE_STAGE_FAILED""#));
        assert!(summary.contains(r#""scope":"pipeline_stage""#));
        assert!(summary.contains(r#""stage_id":"cfg.discover""#));
        assert!(summary.contains(r#""program": "reps2""#));
        assert!(summary.contains(r#""arguments": ["recompile-elf"]"#));
        // The terminal session.finished event is excluded from the summary.
        assert!(!summary.contains("session.finished"));
        assert!(!summary.contains("REPS2_COMMAND_FAILED"));
    }

    #[test]
    fn summary_records_the_invocation_for_reproduction() {
        let temp = TestDirectory::new();
        let arguments = [
            "recompile-elf",
            "D:/List Games/title.elf",
            "out.cpp",
            "--tu-count",
            "8",
        ]
        .map(|argument| argument.to_owned());
        let mut logger = SessionLogger::start(&temp.0, &arguments, "0.7.0", "test-platform")
            .expect("start diagnostics session");
        let directory = logger.directory().to_path_buf();
        logger.finish("succeeded", None).expect("finish session");

        let summary =
            fs::read_to_string(directory.join("summary.json")).expect("read structured summary");
        assert!(summary.contains(r#""program": "reps2""#));
        assert!(summary.contains(
            r#""arguments": ["recompile-elf","D:/List Games/title.elf","out.cpp","--tu-count","8"]"#
        ));
    }
}
