# REPS2 autonomous engineering state

Updated: 2026-11-05

## Current objective

Advance M4 real-title native execution without inventing the unresolved EE `FindAddress` (0x83) ABI, while preserving the shared multi-game architecture.

## Verified repository state

- Branch: `feat/m4-syscall-boundary-diagnostics`
- HEAD: `739bd3a`
- PR: `RidTheWann/REPS2-Project#79` (open; CI checks green; merge state clean)
- Working tree: clean
- Latest delivered work: optional syscall-boundary memory diagnostics and opt-in bounded instruction-PC tracing.

## Verified validation

- Rust formatting: passed
- Rust workspace tests: passed
- Release native build: passed
- Full CTest: 30/30 passed
- `git diff --check`: passed
- GitHub CI: MSRV, Linux, and Windows checks passed

## M4 evidence status

- Real-title native execution evidence exists for Downhill Domination.
- Syscall boundary and bounded trace infrastructure are implemented.
- `FindAddress` service 0x83 ABI, return value, and memory effects: **UNKNOWN**.
- Trustworthy local reference-execution oracle: not available.
- Repeatable guest-visible boot stage: not demonstrated.
- M4: **INCOMPLETE**.

## Active backlog

1. Add a bounded rolling instruction trace so long retail runs expose the final 16 guest PCs, then regenerate and rerun Downhill Domination with `REPS2_INSTRUCTION_TRACE=1`.
2. Recover the FindAddress caller's post-call control flow and register/memory use from actual executable evidence; do not implement the service without an oracle.
3. Add any required trace-field regression assertions and document the resulting evidence in ADR 0068.
4. After M4 evidence is exhausted, prioritize shared runtime services required by observed traces, then second-title validation.

## Release-gate status

- Alpha: not claimed; end-to-end native execution exists, but supported guest-program scope and runtime coverage remain limited.
- Public Beta: not met; no three-title gameplay validation, installable desktop application, or sustained supported sessions.
- REPS2 1.0: not met.

This document records verified state, not intentions or compatibility claims.
