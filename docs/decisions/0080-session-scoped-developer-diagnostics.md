# ADR 0080: Session-scoped developer diagnostics

- Status: Accepted
- Date: 2026-11-07
- Owners: REPS2 maintainers

## Context

REPS2 is an alpha recompiler whose most useful failures often occur across several host-side stages: disc inspection, import, ELF analysis, code generation, native build orchestration, and bounded runtime probes. Console output alone is ephemeral and difficult to correlate with a specific command invocation. A diagnostics foundation is needed before adding a graphical developer mode or end-user launcher.

The current C++ runtime is not a complete PS2 runtime. Logging must not imply that unsupported devices, guest execution, or title compatibility are being observed when they are not.

## Decision

1. The first diagnostics layer is owned by the Rust frontend because it orchestrates the current command lifecycle.
2. --dev enables session-scoped diagnostic capture. --log-dir <directory> selects an explicit diagnostics root and also enables capture. Global flags appear before the command.
3. Each invocation creates a unique session directory with:
   - session.log: human-readable host events.
   - events.jsonl: one versioned JSON event per line.
   - session.json: session metadata and final outcome.
4. Events carry a schema version, session ID, sequence number, Unix timestamp, elapsed milliseconds, severity, category, subsystem, event name, message, and string-valued context.
5. A session records start and terminal outcome, including the command failure text when applicable. The command name is recorded; the full argument vector is not copied into the session manifest.
6. The implementation uses the Rust standard library and the existing JSON string escaper. It does not add a serialization dependency.
7. This layer does not add per-instruction logging, guest trace calls, or a Rust/C++ FFI boundary. Runtime tracing will be a separate bounded facility with filtering/sampling and explicit subsystem ownership.
8. Diagnostics errors are surfaced when capture is explicitly requested; a command must not claim a complete diagnostic record when the log could not be finalized.

## Consequences

- Developers can attach a self-contained session directory to a bug report and correlate its events without relying only on terminal history.
- The JSONL event contract can be extended in later ADRs for library/build/session identifiers and coarse-grained runtime events.
- Local paths may appear in failure messages. Diagnostic artifacts should be reviewed before public sharing.
- A generated-code smoke test and CI artifact capture verify that the actual executable produces all three files on Windows and Linux.
- This decision is not a GUI design and does not move the M7 desktop product milestone forward. It establishes a diagnostic primitive that future CLI and GUI services can share.

## Validation

- Unit tests verify JSON escaping, event ordering, context preservation, readable output, success/failure manifests, and unique session directories.
- CI smoke tests invoke the built CLI in developer mode on Windows and Linux and verify the emitted files and successful terminal event.
