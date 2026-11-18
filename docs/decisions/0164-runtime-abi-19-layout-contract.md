# ADR 0164: Runtime ABI 19 — the state layout is part of the ABI contract

- **Status:** Accepted (validated locally; see Validation).
- **Date:** 2026-11-18.
- **Scope:** `runtime/include/reps2/runtime/abi_version.hpp` (ABI 18 -> 19),
  the admission tests in `tests/rg_reps2_engine_smoke.cpp`, and the expected
  version check in `tests/native_build_driver_smoke.cmake`. No runtime
  behaviour, service, or generated-code change.

## Context

`REPS2_RUNTIME_ABI_VERSION` is the version the engine compares first when it
admits a generated module (`engine/include/rg_reps2_engine/engine.hpp`,
`Engine::prepare` line 108, before the fingerprint check at line 111). The
generated code receives `EeState` as an opaque pointer, so the *layout* of that
state is part of the contract, not an implementation detail.

Two merged changes altered that layout while the version stayed at 18:

- ADR 0160 grew the modelled SIF RPC server table, `kEeMaxSifRpcServers`, from
  18 to 24 (`ee_state.hpp`), inside `EeSifCmdState`.
- ADR 0163 added the bounded FILEIO request-observation fields
  (`fileio_*`) to `EeSifCmdState`.

- **Observed (MSVC on Windows x64, `/std:c++20`, probe compiled against each
  revision's `runtime/include` and `engine/include`):**

  | Revision | `REPS2_RUNTIME_ABI_VERSION` | `sizeof(EeState)` | `sizeof(EeSifCmdState)` | `kEeMaxSifRpcServers` | `kToolchainAbiFingerprint` |
  |---|---|---|---|---|---|
  | `c0af09c` (pre-#38, the ABI-18 state) | 18 | 24,464 | 1,048 | 18 | `76abeed739d2f474` |
  | `09a4b2a` = `f72fbbb` (post-#38/#40) | 18 | 24,672 | 1,256 | 24 | `16f839495b5be291` |

- **Derived:** the two revisions are layout-incompatible but declare the same
  runtime ABI version. A module built against the pre-#38 headers passes the
  `runtime_abi_version` check and is then rejected by
  `kToolchainAbiFingerprint`, which mixes `sizeof(EeState)` and
  `alignof(EeState)` (`toolchain_abi.hpp` lines 102-111), as
  `EngineError::toolchain_abi_mismatch`.

- **Correction to an earlier report:** it is therefore *not* true that such a
  module would be admitted. The proven defect is diagnostic, not a silent
  incompatibility: an outdated state layout is reported as a compiler/STL
  fingerprint mismatch, which sends the reader after a toolchain problem that
  does not exist. The fingerprint exists to catch cross-compiler/STL mismatch
  and cannot distinguish the two causes.

- **Unknown:** whether any additional layout drift slipped past an earlier
  version bump. Nothing in the repository records a per-version layout digest,
  so this ADR does not claim the ABI was otherwise correct.

## Decision

1. Raise `REPS2_RUNTIME_ABI_VERSION` from 18 to 19.
2. Record in `abi_version.hpp` that any added, removed, resized, or reordered
   field in `EeState` (including its members) requires a bump, and that the
   fingerprint is a coarser secondary guard rather than a substitute.
3. Keep ADRs 0160 and 0163 as the layout record; this ADR only restates the
   contract. No historical document is rewritten.
4. Make the native-build driver smoke read the expected version from
   `abi_version.hpp` instead of hardcoding it, so the check cannot drift again
   (mirrors what `rust/frontend/src/native_build.rs` already does when it emits
   the toolchain report).
5. Add an explicit admission test that the previous generation (ABI 18) is
   rejected as `runtime_abi_mismatch`, and keep the existing case that the
   current version is admitted.

## Consequences

- Modules built against ABI 18, including any pre-#38 module, are now rejected
  as `runtime_abi_mismatch`, which is the correct diagnosis.
- Every previously generated native module must be rebuilt against ABI 19.
  This is a rebuild requirement, not a compatibility change: no title data,
  manifest schema, or guest-visible behaviour changes.
- Future layout changes must bump the version. Failure to do so is still
  survivable because the fingerprint mixes `sizeof(EeState)`, but the diagnosis
  regresses as described above.
- The engine ABI (`kEngineAbiVersion`), the native module descriptor ABI, and
  the manifest/IR schema versions are untouched.

## Validation

- `tests/rg_reps2_engine_smoke.cpp` asserts a descriptor at ABI 18 is rejected
  with `EngineError::runtime_abi_mismatch` and that the current version is
  admitted; the previous-generation constant carries a `static_assert` so it
  cannot silently become the current one.
- `tests/native_build_driver_smoke.cmake` compares the generated toolchain
  report's `runtime.abi_version` against the header value.
- Local gates: `cargo fmt --all -- --check`, `cargo clippy --workspace
  --all-targets -- -D warnings`, `cargo test --workspace --locked`, CMake
  Release configure/build, and `ctest -C Release` (Windows x64/MSVC).
- No real-title probe is part of this decision. The recorded M4 probe evidence
  predates ABI 19 and stays labelled with the version it used.