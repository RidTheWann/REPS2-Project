# ADR 0171: Runtime ABI 21 covers the Timer 2 and INTC handler state

- **Status:** Accepted (validated locally; see Validation).
- **Date:** 2026-11-20.
- **Scope:** `runtime/include/reps2/runtime/abi_version.hpp` only. The layout
  changes it covers are described in ADR 0172:
  `runtime/include/reps2/runtime/ee_state.hpp` (`EeMemory` Timer 2 fields,
  `EeIntcHandler`, the `intc_*` observation and registry fields in
  `EeHleState`, and `EeSyscallHandlerFrame::is_intc_interrupt`).
- **Related:** ADR 0164 (the original layout contract), ADR 0169 (ABI 20),
  ADR 0172 (the change this bump covers).

## Context

ADR 0172 adds a Timer 2 register model to `EeMemory` and an INTC handler
registry plus delivery counters to `EeHleState`. Both move `sizeof(EeState)`.
ADR 0164 makes the `EeState` layout a versioned contract precisely so that a
module compiled against an older layout cannot be admitted by a revision check
that only compares a number.

## Observed

MSVC 14.51, Windows x64, `/std:c++20`, probe compiled against each revision's
`runtime/include`:

| Revision | Declared runtime ABI | `sizeof(EeState)` | `sizeof(EeHleState)` | `sizeof(EeMemory)` |
|---|---|---|---|---|
| `a47482a` (ABI 20) | 20 | 33,408 | 31,848 | 128 |
| this branch (ABI 21) | 21 | 33,896 | 32,288 | 176 |

## Derived

- The ABI 21 delta is 488 bytes: 48 for the Timer 2 register file (three
  32-bit registers, the base count, one 64-bit base cycle stamp, and three
  observation counters) in `EeMemory`, and 440 in `EeHleState` — 16 × 20 bytes
  of `EeIntcHandler` records (320), the add/remove/enable/disable/delivery
  observation fields, and one more `bool` per `EeSyscallHandlerFrame` slot.
- An ABI 20 module sees `EeHleState::intc_handlers` at the offset where ABI 21
  now keeps the first handler record, so admitting it would not merely
  misreport counters: `dispatch_ee_syscall` would write handler records over
  state the older module still reads. The version check must reject it.

## Decision

1. Raise `REPS2_RUNTIME_ABI_VERSION` from 20 to 21.
2. Record the covered change in `abi_version.hpp` next to the earlier entries,
   so the header carries the full history rather than only the latest reason.
3. Keep ADR 0169 as the historical record for ABI 20. This ADR restates the
   contract only.
4. Leave the admission test version-relative: `REPS2_RUNTIME_ABI_VERSION - 1`
   is the refused generation, with a `static_assert` raised to 21 so a future
   bump cannot silently reuse the old constant.
5. Do not touch the engine ABI (`kEngineAbiVersion`), the native module
   descriptor ABI, the toolchain fingerprint, or the manifest/IR schemas. They
   are independent contracts.

## Consequences

- Every runtime module must be rebuilt for ABI 21. An ABI 20 module is refused
  with `runtime_abi_mismatch`, which is the truthful diagnosis.
- The Downhill frontier probe (Priority 4's real-title half) must use a module
  built against ABI 21 or later.
- `EeState` remains a single contiguous struct with no pointers into host
  memory, so the layout delta is a per-session cost only.

## Validation

- Local gates (Windows x64, MSVC Release): `cargo fmt --check`,
  `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` (363 frontend tests plus the other
  workspace crates), `cmake --build build --config Release`, and
  `ctest --test-dir build -C Release` 49/49.
- `tests/rg_reps2_engine_smoke.cpp` still builds a descriptor with
  `REPS2_RUNTIME_ABI_VERSION - 1` and asserts the loader refuses it; the
  accompanying `static_assert` now requires 21 or later.