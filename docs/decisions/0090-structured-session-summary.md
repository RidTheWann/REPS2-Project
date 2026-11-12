# ADR 0090: Structured session summary for agent triage

- Status: Accepted
- Date: 2026-11-08
- Supersedes: None. ADRs 0080, 0081, 0082, and 0086 remain the session, report, stage, and capability contracts.

## Context

`reps2 dev inspect` returns a session's raw event stream plus a `diagnostics` projection of warning/error events. Those events are JSONL strings whose stable `error_code`, `failure_scope`, and `stage_id` live inside a string-valued `context` map. To triage a failure, an IDE or coding agent must therefore parse each event's nested context to recover fields the writer already knows exactly.

Two problems follow. First, recovering structured fields from raw event strings requires a JSON reader, and no such reader exists in the frontend (it writes JSON by hand and only validates it). Second, the terminal `session.finished` error duplicates the real failure and complicates a naive scan.

## Decision

Have the diagnostics writer emit a compact, structured session summary and expose it through a dedicated `reps2 dev summary` command.

1. `SessionLogger` retains, in memory, the warning/error events it records (excluding the terminal `session.finished` event), bounded to `MAX_RETAINED_DIAGNOSTIC_RECORDS` (256) with a `diagnostic_records_dropped` counter. The retained record carries the event's `sequence`, `severity`, `category`, `subsystem`, `event`, `message`, and the `error_code`, `failure_scope`, and `stage_id` context entries when present — read from the same in-memory data the writer already holds, so no parsing is involved.
2. At finalization, `SessionLogger::finish` writes `summary.json` atomically alongside the other artifacts. It is a proper report envelope: `schema_version`, `kind = reps2.developer.session_summary`, `status = ok`, plus the session identity, `outcome`, `event_count`, `diagnostic_count`, `diagnostic_records_dropped`, and a `failures` array of structured records.
3. `reps2 dev summary <latest|session-id>` reads `summary.json` and echoes it as the single stdout document. It starts no session. When the artifact is absent (a session from before this feature, or an interrupted session) it returns the stable `REPS2_DEV_SUMMARY_UNAVAILABLE` error envelope instead of a fabricated summary.
4. `summary.json` is added to the advertised artifact list and its envelope kind to the advertised kinds in `reps2 dev capabilities`; the JSON schema gains a `sessionSummary` variant. The event and session-manifest schema versions are unchanged: `summary.json` is a new artifact and the existing artifacts are untouched.

A missing `summary.json` is not an integrity error (older sessions remain valid); a present but malformed one is reported as an integrity error like the other artifacts.

## Consequences

- An agent triages a failure from one small document with first-class `code`, `scope`, and `stage_id` fields, without parsing the event stream.
- The summary is derived by the producer from authoritative structured data; it cannot disagree with the event stream about which codes were emitted.
- Memory use is bounded regardless of session length.
- Runtime tracing stays a separate bounded concern; this does not add per-instruction logging or a Rust/C++ FFI boundary.
- This does not claim a title boots or that any M-milestone gate is met.

## Alternatives considered

- Parse the event context in the reader. Rejected: it needs a JSON reader, is redundant with data the writer already holds, and is more fragile than emitting structured fields directly.
- Change `dev inspect`'s `diagnostics` array to structured objects. Rejected: it breaks the existing `sessionInspection` contract for current consumers.
- Fold the summary into `session.json`. Rejected: `session.json` is the completion manifest and is written before the summary data is finalized; a separate, agent-facing artifact keeps both contracts clean.

## Validation

Unit tests assert the summary captures the structured code, scope, and stage id, excludes the terminal event, and records the diagnostic count; that `load_session` exposes a present summary, tolerates a missing one, and flags a malformed one. A CLI integration test (existing capability test) asserts every advertised envelope kind appears in the JSON schema. The repository CI matrix validates the Rust workspace on Windows, Linux, and MSRV 1.88.
