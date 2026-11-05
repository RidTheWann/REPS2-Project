# REPS2 autonomous engineering state

Updated: 2026-11-05

## Current objective

Advance M4 real-title native execution without inventing the unresolved EE `FindAddress` (0x83) ABI, while preserving the shared multi-game architecture. M4 probe validation for Black is complete; execution stops at `FindAddress` 0x83 (same boundary as Downhill Domination) with no local reference-execution oracle available.

## Verified repository state

- Branch: `feat/m4-syscall-boundary-diagnostics`
- HEAD: `3e6b789` (pushed; PR #79 updated; CI checks pending after push)
- Working tree: clean
- Latest delivered work: rolling bounded instruction-PC tracing with PC-before-trace ordering fix, trace regression assertions, FindAddress caller correlation, ADR 0068 extended with retail trace evidence, Black title added to compatibility matrix.

## Verified validation

- Rust formatting: passed
- Rust workspace tests: passed (270 tests, 0 failures)
- Rust clippy: passed (no warnings)
- Release native build: passed
- Full CTest: 30/30 passed
- `git diff --check`: passed

## M4 evidence status

### Black (USA) — SLUS_213.76 — native probe validated (2026-11-05)

- ISO SHA-256 `65768cade88bfbaf566b67a3ef7a0bda5a9669779833914ffe7cd73a3e45cccd`; boot ELF SHA-256 `141b05209570458af9c8e27ab1911f258da6353909d813cd572df026254ffe4c`
- Entry: `0x00100008` (matches ELF entry)
- Analysis: 101,956 blocks; 504,661 instructions; 1,068 unsupported stops; 243 syscall/break traps
- HLE-on bounded probe (budget 10,000,000): `290,344` guest instructions executed natively
- Services serviced in order: `0x3C` SetupThread, `0x3D` SetupHeap, `0x40` CreateSema (x2), `0x74` SetSyscall
- Stops at unhandled `FindAddress` 0x83 at PC `0x0036D664` (`$a0=0x80000000`, `$a1=0x80080000`, `$a2=0x0036D620`, `$a3=0x0040EC80`, `hle_v0_before=0x003D0000`)
- `syscalls=7`, `hle_handled=6`, `sema_live=2`, `sema_id=1`
- No guest-visible boot stage demonstrated

### Downhill Domination (USA) — SCUS_971.77 — previously recorded

- Stops at unhandled `FindAddress` 0x83 at PC `0x0025EF7C`; no guest-visible stage

### M4 boundary (both titles)

- `FindAddress` service 0x83 ABI, return value, and memory effects: **UNKNOWN**.
- ps2sdk names service 0x83 `FindAddress` but exposes no declaration or ABI; ps2tek does not define its contract.
- Trustworthy local reference-execution oracle: not available.
- Repeatable guest-visible boot stage: not demonstrated.
- M4: **INCOMPLETE** — blocked on `FindAddress` without an oracle; will not invent the ABI.

## Active backlog

1. Record Black probe evidence (done: COMPATIBILITY_MATRIX.md + PROJECT_STATE.md).
2. Preserve caller correlation and trace regression assertions (delivered in PR #79); do not implement `FindAddress` without an oracle.
3. Advance independent compiler/runtime workstreams not blocked by `FindAddress` (M4 is a priority, not the whole mission).
4. Remove `path/to/filename.js` placeholder from main (separate cleanup PR; does not block M4).

## Release-gate status

- Alpha: not claimed; end-to-end native execution exists, but supported guest-program scope and runtime coverage remain limited.
- Public Beta: not met; no three-title gameplay validation, installable desktop application, or sustained supported sessions.
- REPS2 1.0: not met.

This document records verified state, not intentions or compatibility claims.
