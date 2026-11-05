# REPS2 autonomous engineering state

Updated: 2026-11-05

## Current objective

Advance M4 real-title native execution without inventing the unresolved EE `FindAddress` (0x83) ABI, while preserving the shared multi-game architecture. M4 probe validation for Black is complete (PR #79 merged); execution stops at `FindAddress` 0x83 (same boundary as Downhill Domination) with no local reference-execution oracle available.

## Verified repository state

- Branch: `main` at HEAD `627c71f5` (CI green); feature branch `feat/ee-guest-syscall-handlers` with unmerged changes (below).
- PR: `RidTheWann/REPS2-Project#79` (merged; all 6 CI checks passed: MSRV, Linux, Windows)
- Cleanup PR: `RidTheWann/REPS2-Project#80` (open; remove `path/to/filename.js` placeholder)
- Working tree: dirty on `feat/ee-guest-syscall-handlers` (6 files + ADR 0069 + docs, not yet committed)
- Latest delivered work: EE guest syscall handler dispatch through SetSyscall (ADR 0069) — registered slots take precedence over host services, bounded handler frames with kseg3 sentinel return (`kEeSyscallHandlerReturnPc`), `syscall_handler_overflow` status, host/service and guest-handler separation preserved; static data-pointer root discovery for non-executable loaded segments; cfg fixture gains handler/caller roots 0xE600/0xE700; HLE smoke suite grows to 23 cases.

## Verified validation

- Rust formatting: passed (`cargo fmt --check`)
- Rust workspace tests: passed (271 tests, 0 failures)
- Rust clippy: passed (no warnings, `-D warnings`)
- Release native build: passed
- Full CTest: 30/30 passed (incl. `reps2_hle_syscall_codegen_smoke` 23 cases)
- `git diff --check`: not re-run after uncommitted edits

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

1. Record Black probe evidence (done: COMPATIBILITY_MATRIX.md + PROJECT_STATE.md, PR #79 merged).
2. Preserve caller correlation and trace regression assertions (delivered in PR #79); do not implement `FindAddress` without an oracle.
3. Advance independent compiler/runtime workstreams not blocked by `FindAddress` (M4 is a priority, not the whole mission).
4. Remove `path/to/filename.js` placeholder from main (PR #80 open; does not block M4).
5. Commit/push `feat/ee-guest-syscall-handlers` and open its PR.

## Release-gate status

- Alpha: not claimed; end-to-end native execution exists, but supported guest-program scope and runtime coverage remain limited.
- Public Beta: not met; no three-title gameplay validation, installable desktop application, or sustained supported sessions.
- REPS2 1.0: not met.

This document records verified state, not intentions or compatibility claims.
