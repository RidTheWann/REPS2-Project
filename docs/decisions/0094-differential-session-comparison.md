# ADR 0094: Differential session comparison and a minimal JSON reader

- Status: Accepted
- Date: 2026-11-08
- Supersedes: None. ADRs 0080, 0081, 0086, 0090, and 0093 remain the session, report, capability, summary, and reproduction contracts.

## Context

Regression triage needs to answer a specific question: after changing the frontend or a title's inputs, did the set of failures change? Today a developer must run a command, inspect the session, run it again, and diff by eye. The structured `summary.json` (ADR 0090) already exposes each warning/error record with the stable `code`, failure `scope`, and `stage_id` — everything needed for a deterministic comparison — but no command consumes it.

Consuming it requires reading JSON, and the frontend deliberately depends on no JSON crate; writers emit JSON by hand and only validate it.

## Decision

Add a deterministic differential comparison and the minimal reader it needs.

1. A small, bounded, fail-closed JSON reader (`developer_json`, `pub(crate)`) parses a JSON document into a `Value` (null/bool/number/string/array/object). It rejects malformed input instead of coercing it: trailing content, duplicate object keys, control characters in strings, invalid escapes, lone surrogates, bad numbers, and nesting beyond a fixed depth (64). Numbers keep their exact source text. No dependency is added.
2. `reps2 dev compare <base> <candidate>` loads each session's `summary.json`, parses it with the reader, and emits one JSON document (`kind: reps2.developer.session_comparison`): the `base` and `candidate` views (`session_id`, `command`, `outcome`, `diagnostic_count`, `failure_count`, and the sorted `failure_signatures`), flags `identical`, `command_changed`, and `outcome_changed`, and the `added`/`removed` signatures as a sorted-multiset difference.
3. A failure signature is the deterministic `code|scope|stage_id|event` tuple. Message text is excluded so transient path text does not make otherwise-identical failures differ; multiplicities are preserved so "failed twice" is distinguishable from "failed once".
4. Failures are explicit: an unloadable session returns `REPS2_DEV_SESSION_INSPECTION_FAILED`; a missing or malformed summary returns `REPS2_DEV_SUMMARY_UNAVAILABLE`. Comparison never fabricates a result. The reader is reused for these `dev` reports; it does not change how the frontend writes JSON.

## Consequences

- Regression triage becomes a single deterministic command instead of a manual diff.
- The reader is internal infrastructure the inspector can reuse; it is `pub(crate)` so it is not part of the crate's public surface, and being dependency-free it preserves the repository's build and MSRV guarantees.
- Comparison is over the summary contract, so it inherits the writer's guarantees and cannot disagree with the event stream about which codes were emitted.
- This does not add runtime tracing, a network service, or a serialization dependency.

## Alternatives considered

- Add `serde`/`serde_json`. Rejected: the repository is deliberately dependency-free; a reader for a handful of known value shapes does not justify the dependency and its supply-chain surface.
- Compare the summary documents as text. Rejected: session identifiers and timestamps differ every run, so textual diff is noise.
- Compare raw `events.jsonl`. Rejected: it duplicates the summary projection and re-introduces the parsing burden the summary removed.
- Emit a producer-side comparison. Rejected: comparison is a read-side, on-demand operation over already-written sessions.

## Validation

Reader unit tests cover valid documents, escapes (including surrogate pairs), Unicode, and a rejection matrix (trailing content, duplicate keys, control characters, bad escapes, lone surrogates, invalid numbers, excessive nesting). Comparison unit tests cover an added/removed signature pair, identical summaries, multiset multiplicity, and fail-closed parsing. A CLI integration test asserts every advertised envelope kind appears in the JSON schema. The repository CI matrix validates the Rust workspace on Windows, Linux, and MSRV 1.88.
