# ADR 0082: Frontend pipeline-stage diagnostics for agent triage

- Status: Accepted
- Date: 2026-11-07
- Supersedes: None. ADRs 0080 and 0081 remain the session and report contracts.

## Context

The developer diagnostics sessions and dev inspect report expose command-level failures, but a failing analyze-elf, profile-elf, or recompile-elf command can contain several independent frontend stages. A single command error does not tell an IDE or coding agent whether the failure occurred while reading input, parsing ELF metadata, discovering the CFG, lifting IR, generating C++, or writing artifacts.

The diagnostic stream is intended to support deterministic triage without introducing per-instruction disk logging or a Rust/C++ per-instruction FFI boundary.

## Decision

Add an optional stage wrapper to the existing session logger. When developer diagnostics are enabled, the instrumented commands emit three event names:

- pipeline.stage.started
- pipeline.stage.succeeded
- pipeline.stage.failed

Each event is written to the existing version-1 JSONL stream. Successful stages identify the stable stage_id, subsystem, and stage duration. Failed stages additionally identify:

- error_code = REPS2_PIPELINE_STAGE_FAILED
- failure_scope = pipeline_stage
- the original stage error text
- stage_elapsed_ms

The first instrumented commands are:

- analyze-elf: input read, ELF parse, target validation, and CFG discovery.
- profile-elf: input read, ELF parse, target validation, and decoder coverage profiling.
- recompile-elf: input read, ELF parse, target validation, EE RAM image construction, relocated-code preparation, CFG discovery, IR lifting, C++ generation, RAM/C++/manifest artifact writes, and optional native-project generation.

Stable stage IDs are part of the event contract. They are deliberately stage-level rather than function-name-level so internal refactors do not unnecessarily break IDE integrations.

When diagnostics are not enabled, the same stage closures execute directly and do not allocate a diagnostics session or emit events. A diagnostic write failure is surfaced rather than silently discarding the requested diagnostic evidence.

## Consequences

- dev inspect latest automatically exposes warning/error stage events in its existing diagnostics projection; the report envelope and schema version do not change.
- A failed command now preserves both its stage failure and the enclosing command failure.
- Stage timing is measured on the host and is diagnostic only; it is not a guest-cycle measurement.
- No per-instruction, per-basic-block, per-memory-access or C++ runtime event logging is introduced.
- This work does not claim that a PS2 title boots, that an unowned ELF was validated, or that M1-M4 exit criteria have been met.

## Validation

Unit tests verify successful and failed stage event sequences, stable error context, preserved original errors, and session finalization. The repository CI matrix validates the Rust workspace on Linux, Windows, and MSRV 1.88.
