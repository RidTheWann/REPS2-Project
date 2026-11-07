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

use crate::json::json_string;

const DIAGNOSTICS_SCHEMA_VERSION: u32 = 1;

pub struct SessionLogger {
    session_id: String,
    command: String,
    tool_version: String,
    host_platform: String,
    process_id: u32,
    started_unix_ms: u64,
    started: Instant,
    sequence: u64,
    directory: PathBuf,
    text_log: BufWriter<File>,
    events_log: BufWriter<File>,
}

impl SessionLogger {
    /// Creates an isolated session directory and starts its diagnostic record.
    pub fn start(
        root: &Path,
        command: &str,
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

        let mut logger = Self {
            session_id,
            command: command.to_owned(),
            tool_version: tool_version.to_owned(),
            host_platform: host_platform.to_owned(),
            process_id,
            started_unix_ms,
            started: Instant::now(),
            sequence: 0,
            directory,
            text_log: BufWriter::new(text_file),
            events_log: BufWriter::new(events_file),
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
            DIAGNOSTICS_SCHEMA_VERSION,
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
                        ("error_code", "REPS2_PIPELINE_STAGE_FAILED"),
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
        self.write_session_manifest(Some(unix_time_ms()), outcome)
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
            DIAGNOSTICS_SCHEMA_VERSION,
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

        let temporary_path = self.directory.join("session.json.tmp");
        let manifest_path = self.directory.join("session.json");
        fs::write(&temporary_path, manifest)?;
        if manifest_path.exists() {
            fs::remove_file(&manifest_path)?;
        }
        fs::rename(temporary_path, manifest_path)
    }
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
        let mut logger = SessionLogger::start(&temp.0, "version", "0.7.0", "test-platform")
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
        let mut logger = SessionLogger::start(&temp.0, "disc-info", "0.7.0", "test-platform")
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
        let mut logger = SessionLogger::start(&temp.0, "recompile-elf", "0.7.0", "test-platform")
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
        let mut logger = SessionLogger::start(&temp.0, "recompile-elf", "0.7.0", "test-platform")
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
        let first = SessionLogger::start(&temp.0, "version", "0.7.0", "test-platform")
            .expect("start first session");
        let second = SessionLogger::start(&temp.0, "version", "0.7.0", "test-platform")
            .expect("start second session");
        assert_ne!(first.directory(), second.directory());
    }
}
