# ADR 0170: Bounded per-syscall census and optional trace ring

- **Status:** Accepted (validated locally; see Validation).
- **Date:** 2026-11-19.
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (census and trace
  storage), `runtime/include/reps2/runtime/ee_hle.hpp`
  (`dispatch_ee_syscall` accounting), `engine/include/rg_reps2_engine/session.hpp`
  (`SessionConfig::syscall_trace_capacity`), `engine/src/main.cpp`
  (`--syscall-trace`, `syscall_census` run-report section), and the new tests
  `tests/syscall_census_smoke.cpp` and `tests/engine_runner_census_report.cmake`.
- **Related:** ADR 0169 (ABI 20 covers this layout change).

## Context

The recorded Downhill Domination frontier executes 39,999,993 instructions with
3,219,747 syscall events in four batches, but the run report only carries two
aggregates: `syscalls_seen` and `syscalls_handled`. Those totals cannot say
*which* service dominates the tight polling loop or *where* it is called from,
so the frontier cannot be attributed to a producer. An aggregate counter is not
an investigation.

Two new requirements follow:

1. Per-service attribution with a per-site answer: which syscall number is
   dominant, and from which guest PC(s) is it issued.
2. Bounded storage: three million events must not become three million records
   in memory, in the report, or in committed evidence.

## Decision

Record every dispatched boundary in a fixed census, and keep the detailed event
ring opt-in.

### Storage (inside `EeHleState`)

- `syscall_census[256]` — one `{seen, handled, refused}` counter triple per
  normalized service number `0..255`. Numbers outside that range share
  `syscall_census_overflow`, so nothing is dropped silently.
- `syscall_hot_sites[8]` — a bounded Space-Saving table of
  `{pc, number, count}` call sites.
- `syscall_trace[64]` plus `capacity`, `next`, `count`, `dropped` — the optional
  ring. `capacity == 0` (the default) retains nothing.

The census lives in `EeHleState` rather than a host-side collector because the
dispatch boundary is an inline header function that generated guest code calls
without a host callback: the alternatives would be a new virtual/host service on
the hot path or a per-instruction FFI edge, both of which contradict the
generated-code ABI. The storage is bounded and the layout change is covered by
ABI 20 (ADR 0169).

### Accounting

`dispatch_ee_syscall` increments the census exactly once per boundary, after the
outcome is known:

- `seen` — every dispatched boundary, including the guest-handler path.
- `handled` — the host service returned `Status::ok`, or a registered guest
  handler was entered.
- `refused` — an unknown number (`unhandled_syscall`) or any other non-`ok`
  status.

`seen == handled + refused` therefore holds for every slot after each dispatch.
This is deliberately a different quantity from `syscall_handled_count`, which
still counts only successful host services.

### Call-site attribution

Hot sites are a Space-Saving estimate over at most eight residents: a site that
stays resident has an exact count; after churn, counts are lower bounds. This is
the honest trade for bounded storage. The dominant polling loop has one or two
call sites, which stay resident with exact counts, and the trace ring covers
what the table cannot.

The report states the method as `"attribution": "space_saving_hot_sites"` so a
reader does not mistake the table for an exhaustive site map.

### Configuration

- Census: always maintained when `--hle-syscalls` is enabled. Cost is a few
  integer increments per boundary, negligible against service body work, and the
  whole point of the flag is diagnosis.
- Trace ring: `--syscall-trace <1..64>` (engine runner), clamped by the session
  to `EeHleState::kSyscallTraceCapacity`. Disabled by default.

### Run report

`rg.reps2.engine.run_report.v1` gains one additive section; existing keys are
unchanged, so existing consumers keep working:

```json
"syscall_census": {
  "version": 1,
  "attribution": "space_saving_hot_sites",
  "tracked_numbers": 256,
  "total_seen": 0,
  "total_handled": 0,
  "overflow_seen": 0,
  "overflow_handled": 0,
  "overflow_refused": 0,
  "services": [{"number": 47, "name": "GetThreadId", "seen": 2, "handled": 2, "refused": 0}],
  "hot_sites": [{"pc": 1051648, "number": 47, "count": 2}],
  "last": {"pc": 1051664, "code": 0, "number": 0, "v0_before": 0, "v0_after": 0,
           "resume_pc": 0},
  "trace": {"capacity": 8, "retained": 3, "dropped": 0,
            "events": [{"pc": 1051648, "number": 47, "arguments": [0, 0, 0, 0],
                        "v0_before": 0, "v0_after": 0, "path": "host_service",
                        "refused": 0}]}
}
```

Only occupied service slots are emitted, in ascending number order, so a report
stays small when few services are called. The section contains guest addresses
and syscall numbers only: no host paths, no ISO content, no argv, so it is safe
to keep in frontier records.

`last` correlates the census with the report's `final_pc` and `outcome`: it is
the final boundary the guest dispatched, not a sample.

## Consequences

- The Downhill frontier can now be attributed: the dominant service number, its
  counted boundaries, the hottest sites, and the last boundary before the stop.
- `EeState` grows by 8,632 bytes (ADR 0169). That is the price of a
  self-contained runtime census and is paid only once per session.
- The trace ring is a debugging aid, not an evidence format: it retains at most
  64 boundaries and reports `dropped`, so it must never be used to claim a
  complete call sequence.
- Nothing here invents scheduling, interrupt, or service behaviour. The census
  only reports what dispatch already observed.

## Validation

- `tests/syscall_census_smoke.cpp`: service and site attribution, the
  `seen == handled + refused` invariant, two sites sharing one service, the
  negated `i`-form folding to the same slot, the overflow bucket, ring capacity
  and chronological order, and the disabled-trace path.
- `tests/engine_runner_census_report.cmake`: drives the runner over a native
  fixture that dispatches two serviced calls at one PC and one refused number at
  another, then asserts totals, per-service accounting, hot-site attribution,
  trace capacity/order, and the additive JSON shape.
- Local gates (Windows x64/MSVC Release): `ctest -C Release` 47/47.
- No real-title probe is part of this decision. The census is the instrument for
  the next one.