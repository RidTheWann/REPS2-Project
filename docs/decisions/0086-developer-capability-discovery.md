# ADR 0086: Developer capability discovery and stable diagnostic-code catalog

- Status: Accepted
- Date: 2026-11-08
- Supersedes: None. ADRs 0080, 0081, and 0082 remain the session, report, and stage contracts.

## Context

The `reps2 dev` namespace already reports diagnostic sessions (`dev sessions`) and a single session's full record (`dev inspect`). Those commands are readable, but the surrounding contract was implicit: an IDE, coding agent, or automated pipeline had to hardcode the report schema version, the envelope `kind` discriminators, the artifact file names, the command grammar, and the set of stable `REPS2_*` error codes. Any of those could drift from the implementation without detection.

The stable `REPS2_*` codes were also scattered as bare string literals across `diagnostics.rs`, `developer.rs`, and `main.rs`, so there was no single place describing which codes exist or what each means. The directive for the developer-diagnostics workstream requires stable capability discovery and machine-readable error codes, and explicitly prefers a local CLI contract over adding a network service.

## Decision

Publish a deterministic, versioned capability-discovery document through `reps2 dev capabilities`, and make the diagnostic codes a first-class catalog.

1. `reps2 dev capabilities` writes exactly one JSON document to stdout (no new diagnostics session is started), with `kind = reps2.developer.capabilities` under the existing report envelope schema version 1. It records: tool version; report, event, and session-manifest schema versions; the artifact file names; every envelope `kind`; the machine-readable `dev` command grammar; the stable diagnostic-code catalog; the log-root resolution policy; and a privacy note.
2. The stable diagnostic codes move into `diagnostics::codes` as named constants plus a catalog (`codes::ALL`) of `{ id, severity, scope, surface, description }`. The emit sites in `diagnostics.rs`, `developer.rs`, and `main.rs` reference the constants, so there is a single definition per code. New codes may be added; an existing code's meaning must not change without a schema-version bump.
3. Envelope `kind` strings become shared constants in `developer.rs` and are both emitted and published by `dev capabilities`.
4. `docs/schemas/developer-report-v1.schema.json` gains a `capabilities` definition and adds it to the `oneOf` set. The report envelope version stays 1: this is an additive variant, and existing variants are unchanged.

## Consequences

- Tooling can discover the diagnostics contract at runtime instead of copying constants from documentation, and can detect a schema-version change.
- The code catalog is authoritative: a code cannot be emitted without appearing in `codes::ALL`, and the catalog test rejects duplicates, missing descriptions, or an unknown surface.
- A consistency test ties the published envelope kinds to the JSON schema file, so adding a kind without updating the schema fails CI.
- No new process, socket, MCP server, or network surface is introduced; discovery is a local stdout document like the other `dev` commands.
- This does not change the emitted event stream, the session manifest, or any existing report fields.

## Alternatives considered

- Document the contract only in prose. Rejected: prose drifts and cannot be validated in CI.
- Expose discovery through an MCP server. Rejected: the directive prefers a local CLI interface, and no network/service dependency is warranted for a static document.
- Bump the report envelope to version 2. Rejected: the change is additive; the existing `oneOf` envelope admits a new variant without breaking current consumers.

## Validation

Unit tests verify the capability document is valid JSON, deterministic across calls, and lists every envelope kind and diagnostic code. A CLI integration test invokes the built `reps2` binary and asserts a single valid JSON document with exit code 0. A schema test asserts each published envelope kind appears in `developer-report-v1.schema.json`. The catalog test rejects duplicate or under-specified codes. The repository CI matrix validates the Rust workspace on Windows, Linux, and MSRV 1.88.
