# ADR 0137: Deci2Call reports the documented failure (no DECI2 device)

- **Status:** Proposed (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE syscall HLE service table (`ee_hle.hpp`), one service: 0x7C.

## Context

The runtime-ABI-5 Downhill Domination probe (ADR 0136 branch, `--extra-root
0x00246CD0`) runs 4,409,370 instructions, completes 78/78 SIF RPC end-function
callbacks, serves 78 `CD_NCMD_READ` calls, and then stops as
`unhandled_syscall` at `pc=0x0025AB14` with `$v1 = 0x7C`, `$a0 = 1`,
`$a1 = 0x0093B440`, `$a2 = 0x0025B390`, `$a3 = 1`.

Evidence for what the call is:

- **Observed (target words, decoded with the REPS2 frontend):** the trapping
  stub at `0x0025AB10` is `addiu $v1, $zero, 0x7C; syscall; jr $ra; nop` — a
  bare kernel-service stub, with sibling stubs `0x78`/`0x79`/`0x7A`/`0x7B` at
  `0x0025AAC0`..`0x0025AB08`. JAL sites targeting `0x0025AB10` are
  `0x0025F438`, `0x0025F474`, `0x0025F498`, `0x0025F4D0`, `0x0025F508`,
  `0x0025F540`. The reached caller at `0x0025F400` stores a 4-word argument
  block on its stack, sets `$a0 = 1`, `jal 0x0025AB10`, then returns
  (`ld $ra, 0x10($sp); jr $ra`) without reading `$v0`.
- **Derived (pinned PS2SDK `ee/kernel/include/syscallnr.h`):** `__NR_Deci2Call
  = 0x7c`, so service `0x7C` is `Deci2Call(function, args)`. PS2SDK
  `ee/kernel/src/deci2.c` maps `sceDeci2Open` to `Deci2Call(1, args)`, matching
  the observed `$a0 = 1`.

## Decision

1. Add service `0x7C` (`Deci2Call`) to the HLE table. REPS2 models no DECI2
   device, so the handler returns `DECI2_ERR_INVALID` (`-1`,
   `ee/kernel/include/deci2.h`) sign-extended into `$v0`, and nothing else:
   no DECI2 state is invented and no success is fabricated. The observed
   caller ignores the result, so this is safe for the traced path; any caller
   that branches on success sees the documented failure.
2. No `EeState`/`EeHleState` layout change, so `REPS2_RUNTIME_ABI_VERSION`
   stays 5.

## Consequences

- The title advances past the `0x0025AB14` stop; the next probe boundary is
  recorded in `docs/PROJECT_STATE.md`. DECI2 logging (`kputs`), sessions and
  transfers remain unmodelled and would still fail closed if reached through
  other numbers.

## Validation

- `reps2_hle_syscall_codegen_smoke` gains `test_deci2_call_reports_no_deci2_device`
  (136 cases): dispatch of service `0x7C` resumes at the continuation and
  returns `0xFFFFFFFFFFFFFFFF` in `$v0`.
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test
  (377 passed) on Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF `adfda7b7…f0cf0c`,
  ISO `0fe2b777…a7eb86`, Windows x64/MSVC 19.51.36260.0, runtime ABI 5, module
  rebuilt with this runtime, `D:\REPS2-DevTests\DownhillDomination\PostADR136b\run-report-deci2.json`):
  the `0x7C` stop at `0x0025AB14` is gone; the run exhausts the 80M budget
  (79,999,942 instructions, 16 batches) in a pure delay loop at `0x0025E880`,
  with `rpc_bind_sid=2147483654=0x80000006 had=0` (written as `0x80000596` in the
  original record — a decimal mis-transcription, corrected in ADR 0138) — the
  next frontier. Budget exhaustion
  is not a boot stage. Details in `docs/PROJECT_STATE.md` / `docs/RESEARCH.md`.