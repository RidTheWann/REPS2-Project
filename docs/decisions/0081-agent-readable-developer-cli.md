# ADR 0081: Agent-readable developer CLI reports

- Status: Accepted
- Date: 2026-11-07
- Owners: REPS2 maintainers

## Context

The session-scoped diagnostics introduced by ADR 0080 are useful to a person opening log files, but IDE integrations and coding agents need a stable command surface that does not require scraping terminal prose or guessing which session files to open. They also need explicit failure and integrity states: a process interrupted before finalization must not look successful.

## Decision

1. Add a `reps2 dev` command namespace for diagnostic inspection:
   - `reps2 dev sessions [--limit N]` lists recent sessions.
   - `reps2 dev inspect <latest|session-id>` returns one session's manifest, event stream, diagnostic events, and artifact-integrity warnings.
2. These inspection commands emit exactly one JSON document to stdout. They do not create a diagnostics session of their own, so `inspect latest` cannot select the inspection process as its target.
3. The global `--log-dir <directory>` option selects the inspected root when placed before `dev`. Otherwise the platform-specific default diagnostics root is used.
4. The report envelope uses `schema_version: 1` and a `kind` discriminator. The contract is published in `docs/schemas/developer-report-v1.schema.json`.
5. Session inspection validates the manifest and each JSONL event record. Missing or malformed records are surfaced through `integrity_errors`; a session is complete only when it has a valid manifest and terminal `session.finished` event.
6. Regular commands captured with `--dev` now emit `command.started`, `command.succeeded`, or `command.failed` pipeline events. The event identifies the top-level command's owning subsystem and, on failure, includes `REPS2_COMMAND_FAILED`, the command name, failure scope, and original error text.
7. This is command-level attribution only. It does not claim to identify an internal function, individual pipeline stage, IR pass, guest instruction, or C++ runtime failure. Finer instrumentation must be added at the owning subsystem as those services become observable.
8. The implementation remains dependency-free and keeps the existing session event schema. The report envelope has its own schema version so it can evolve independently.

## Consequences

- An IDE, agent, or model can run a normal REPS2 command with `--dev`, then call `reps2 dev inspect latest` and consume structured JSON.
- Diagnostic events are available as both a complete `events` array and a filtered `diagnostics` array containing warning/error events.
- Interrupted or damaged sessions remain inspectable but are explicitly marked incomplete and include integrity errors.
- Local paths and original error strings may be present. Reports must be reviewed before being shared publicly.
- The new commands provide an automation boundary, not a TUI or graphical Developer Inspector.

## Validation

- Unit tests cover the JSON validator, report structure, diagnostic filtering, latest-session resolution, missing/incomplete sessions, malformed event records, and identifier path-traversal rejection.
- CI smoke tests invoke the built CLI on Windows and Linux, create both successful and failed diagnostic sessions, then parse `dev sessions` and `dev inspect latest` output as JSON and verify subsystem/error attribution.
