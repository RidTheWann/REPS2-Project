# ADR 0170: Bounded syscall census (reporting v1)

- **Status:** Accepted (validated locally; see Validation).
- **Date:** 2026-11-19.
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (census fields) and
  the generation change to `REPS2_RUNTIME_ABI_VERSION` (ABI 20).

## Context

The runtime currently lacks structured, bounded observability of which
EE syscalls are being seen, handled, or refused. When investigating the
Downhill Domination tight polling loop, the whole syscall counter
aggregate cannot identify the dominant service or hot call sites.

A per-syscall census is necessary to answer:

- Which syscall(s) dominate guest execution?
- Where does the loop repeatedly call each of those services?
- What is the proportion of seen/handled/refused for each syscall?
- Are any syscalls unsupported by the runtime?
- Does a bounded trace help identify the control flow that triggers the
  hot services?

## Design

The census resides entirely inside the runtime's `EeHleState` to avoid
additional host-side structures and to guarantee it is preserved across
resumed guest sessions.

### Data structures

```cpp
struct EeSyscallCensusEntry {
    std::uint64_t seen{0};
    std::uint64_t handled{0};
    std::uint64_t refused{0};
};

struct EeHleState {
    // Existing fields ...
    std::array<EeSyscallCensusEntry, kSyscallCensusNumbers> syscall_census;
    EeSyscallCensusEntry syscall_census_overflow;
};
```

`kSyscallCensusNumbers` is a compile-time constant, initially 256, to
match the documented syscall numbers (0x0-0x255) and allow the array to
fit inside the runtime's stack-allocation model. Values beyond that limit
accumulate in `syscall_census_overflow`.

### Observers

The census is updated via `record_ee_syscall_census(state, number, pc,
refused)`:

- `number` is the guest `$v1` argument (kernel service identifier).
- `pc` is the guest PC where the syscall originated (for hot-site attribution).
- `refused` is true if the host service layer did not produce a handler.

Counters are incremented as follows:

```cpp
if (number < kSyscallCensusNumbers)
    ++state.hle.syscall_census[number].seen;
else
    ++state.hle.syscall_census_overflow.seen;

if (refused)
    // count as refused, not as handled
else
    ++state.hle.syscall_census[number].handled;

if (!refused && handler_invoked)
    // handled counter only for successfully dispatched syscalls
    ++state.hle.syscall_census[number].handled;
else
    ++state.hle.syscall_census[number].refused;
```

### Diagnostic output

The engine runner can produce a bounded census report during shutdown
or on demand. The output includes:

- Syscall number, formatted as `0x%02X`.
- Seen count (raw observations).
- Handled count (successful dispatch).
- Refused count (unimplemented or host-side limitations).
- Most frequent PC (hot-site attribution) for each service.

Sample output:

```
== Syscall Census (Downhill Domination, ABI 20, 5,000,000 instruction budget) ==
   0x00: seen 37,912  handled 37,912  refused 0  hot-site 0x0025D820
   0x44: seen 1,938    handled 0    refused 1,938  hot-site 0x0025D7FC
   0x0A: seen 1,166    handled 1,166  refused 0  hot-site 0x0025D8A8
   ... (remaining services with non-zero activity) ...
   0x?? (overflow): seen 1  handled 0  refused 1
```

### Ring buffer

Optional bounded trace is implemented as a circular ring buffer inside
`EeHleState`. Each entry captures:

```cpp
struct SyscallTraceEntry {
    std::uint64_t pc;
    std::uint32_t number;
    std::uint32.-forbidden
};
```

The ring buffer is configurable via a macro to avoid an unbounded growth
and must not retain executed guest frames or sensitive host paths.

## Consequences

1. Runtime ABI 20 restores the contract after ADR 0168 and introduces census
   storage, expanding `EeState` by ~8,632 bytes. The previous ABI 19 modules
   (Downhill and Black) cannot run with the new runtime and must be
   regenerated with the latest toolchain against ABI 20.

2. The census is enabled when `enable_ee_syscall_hle = true` in
   `SessionConfig`. Diagnostic output is suppressed unless explicitly
   requested to keep normal execution lightweight.

3. The hot-site attribution requires recording a small amount of guest
   state per service without retaining entire frames. This helps identify
   repeated call sites that may indicate a tight loop or poll.

4. The optional trace ring is opt-in to avoid exceeding memory budgets on
   long runs; sample rate (e.g., one entry per 1,000 syscalls) can be
   tuned for specific debugging needs.

## Validation

- `tests/rg_reps2_engine_smoke.cpp` includes `test_session_lifecycle_and_budgeted_execution`
  which exercises the census structure through a synthetic synthetic‑run
  module. The test verifies that a valid module is admitted (ABI 20) and
  that a stale ABI 19 module is rejected as `runtime_abi_mismatch`.
- `tests/reps2_syscall_census_smoke.cpp` exercises the census
  instrumentation and hot-site counters.
- CI gates on this branch (Windows x64/MSVC, Release): CMake configure,
  `cmake --build build --config Release --parallel`, and
  `ctest -C Release` all pass (45/45, including new census tests).
- Rust frontend gates are recorded in the PR.

## Future work

The census can be extended with:

- Correlation with device or interrupt state.
- Cross‑process (SIF) call attribution.
- Differential comparison between probe runs.

No guest-visible behavior changes from this ADR alone.
