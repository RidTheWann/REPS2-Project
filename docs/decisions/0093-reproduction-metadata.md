# ADR 0093: Record the invocation for reproduction in the session summary

- Status: Accepted
- Date: 2026-11-08
- Supersedes: None. ADRs 0080, 0081, and 0090 remain the session, report, and summary contracts.

## Context

A diagnostics session records the command name (`session.json.command`, e.g. `recompile-elf`) but not the arguments that were passed. A developer or agent inspecting a failed session therefore knows *what* failed but not *how to re-run it*: the input path, output path, and flags are absent. The developer-diagnostics directive asks for reproduction metadata and "how to reproduce the issue" as first-class outcomes, and regression triage needs to re-run a known-failing invocation deterministically.

## Decision

Record the invocation in the structured session summary.

1. `SessionLogger::start` now takes the filtered command line (the subcommand first, with the executable path and the global `--dev`/`--log-dir` options already removed) instead of only the subcommand name. It derives `command` from the first element and stores the full argument vector.
2. `summary.json` gains a `reproduction` object: `{ "program": "reps2", "arguments": [ ... ] }`, where `arguments` is the recorded command line. `program` is the literal label `reps2` rather than the executed binary path, so the artifact does not disclose where the tool is installed. The existing `command` field is unchanged.
3. `reps2 dev summary` already echoes `summary.json`, so the reproduction data is available without a new command; `session.json` and `events.jsonl` are unchanged.
4. The JSON schema `sessionSummary` variant gains `reproduction` (required). The capabilities privacy note and `docs/CLI.md` state that `summary.json` records the exact arguments, which may contain local paths.

The arguments are recorded verbatim: reproduction requires the exact input/output paths and flags, and sanitizing them would defeat the purpose. This is consistent with existing diagnostics, which already carry local paths from error messages.

## Consequences

- An agent can reconstruct and re-run a failing invocation from one small document.
- The reproduction record may contain local paths; the documented privacy guidance now names this explicitly, and users are reminded to review artifacts before sharing them.
- Relative input paths are recorded as given, not resolved against the working directory; a re-run from a different working directory would need the original directory. Recording the working directory was rejected as unnecessary personal data for the common case of absolute paths.
- This does not add runtime tracing, change the event stream, or alter the session manifest.

## Alternatives considered

- A separate `invocation.txt` artifact. Rejected: it fragments the agent-facing summary and needs its own contract.
- A separate `dev reproduce` command. Rejected: `summary.json` already carries the data and `dev summary` already surfaces it; a second command adds surface without new information.
- Store the arguments in `session.json`. Rejected: `session.json` is the completion manifest; the summary is the agent-facing triage document.

## Validation

Unit tests assert the recorded arguments appear verbatim in `summary.json` (including a path with a space and multiple flags) and that `program` is `reps2`. The schema test in CI requires every advertised envelope kind and the new `reproduction` field to remain consistent. The repository CI matrix validates the Rust workspace on Windows, Linux, and MSRV 1.88.
