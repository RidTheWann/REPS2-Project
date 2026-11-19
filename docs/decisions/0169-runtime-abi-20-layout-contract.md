# ADR 0169: Runtime ABI 20 — restore the layout contract after ADR 0168

- **Status:** Accepted (validated locally; see Validation).
- **Date:** 2026-11-19.
- **Scope:** `runtime/include/reps2/runtime/abi_version.hpp` (ABI 19 -> 20),
  `runtime/include/reps2/runtime/ee_state.hpp` (census fields, see ADR 0170),
  and the admission expectations in `tests/rg_reps2_engine_smoke.cpp`. No guest
  behaviour change.

## Context

ADR 0164 made the `EeState` layout part of the module contract and raised the
runtime ABI to 19. ADR 0168 then added the FILEIO virtual descriptor table
(`EeSifCmdState::FileioFd fileio_fds[4]`, the `fileio_*` open/close/read
counters and three reply flags) without raising `REPS2_RUNTIME_ABI_VERSION`.
That is the same violation ABI 19 exists to prevent: an ABI-19 module built
before ADR 0168 is layout-incompatible with the current runtime.

- **Observed (MSVC 14.51, Windows x64, `/std:c++20`, probe compiled against each
  revision's `runtime/include`):**

  | Revision | Declared runtime ABI | `sizeof(EeState)` | `sizeof(EeSifCmdState)` | `sizeof(EeHleState)` |
  |---|---|---|---|---|
  | `7f5110d` (ABI 19, pre-ADR 0168) | 19 | 24,672 | 1,256 | 23,112 |
  | `af51115` (ABI 19, ADR 0168 merged) | 19 | 24,776 | 1,360 | 23,216 |
  | this branch (ABI 20) | 20 | 33,408 | 1,360 | 31,848 |

- **Derived:** `af51115` and `7f5110d` are layout-incompatible but declare the
  same runtime ABI. A module built from `7f5110d` would pass the version check
  and be rejected only by `kToolchainAbiFingerprint` as
  `toolchain_abi_mismatch`, which misdiagnoses a state-layout change as a
  compiler/STL mismatch. No such module is known to be in circulation, but that
  is a fact about local artifacts, not about the contract.
- **Derived:** the ABI 20 delta is 8,632 bytes: 6,144 for the 256-entry service
  census, 24 for the overflow record, 128 for the eight hot sites, and 2,336 for
  the optional trace ring plus its indices (ADR 0170).

## Decision

1. Raise `REPS2_RUNTIME_ABI_VERSION` from 19 to 20.
2. Record both layout changes in `abi_version.hpp`: ADR 0168 (unbumped, now
   covered) and ADR 0170 (the census and trace ring).
3. Keep ADR 0164 and ADR 0168 as the historical record; this ADR restates the
   contract only, and ADR 0164 is marked superseded for the current version.
4. Make the admission test version-relative: the previous generation is
   `REPS2_RUNTIME_ABI_VERSION - 1` with a `static_assert` that the value is 20 or
   later, so a future bump cannot silently reuse the old constant.
5. Do not touch the engine ABI (`kEngineAbiVersion`), the native module
   descriptor ABI, the toolchain fingerprint, or the manifest/IR schemas. These
   remain independent version contracts.

## Consequences

- Every generated native module, including the ABI-19 modules used by the
  recorded Downhill and Black probes, must be rebuilt before it can be admitted.
  No further retail-title probe may reuse an ABI-19 module.
- A stale module now fails as `runtime_abi_mismatch`, the correct diagnosis.
- The runtime state grew by 8,632 bytes. `EeState` stays stack-friendly
  (33,408 bytes), but the census storage is a fixed cost paid even when the
  census is empty; ADR 0170 explains why it stays inside `EeHleState` rather
  than in a host-side structure.
- This change alone promotes no compatibility claim. The recorded frontier
  evidence stays labelled with the ABI it used.

## Validation

- `tests/rg_reps2_engine_smoke.cpp`: a descriptor at the previous generation is
  rejected with `EngineError::runtime_abi_mismatch`; the current version is
  admitted; the previous-generation constant carries a `static_assert`.
- `tests/native_build_driver_smoke.cmake` and
  `rust/frontend/src/native_build.rs` read the expected runtime ABI from
  `abi_version.hpp`, so the toolchain report follows the bump without a second
  edit.
- Local gates on this branch (Windows x64/MSVC, Release): CMake configure,
  `cmake --build build --config Release --parallel`, and `ctest -C Release`
  (47/47, including the new census tests). Rust gates are recorded in the PR.
- No real-title probe is part of this decision.