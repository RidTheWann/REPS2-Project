# ADR 0144: Register the Sony multitap extension servers (0x80000904, 0x80000905)

- **Status:** Proposed (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE SIF RPC server table (`ee_hle.hpp`), two sids: 0x80000904 and
  0x80000905. No state layout change, so `REPS2_RUNTIME_ABI_VERSION` stays 10.

## Context

With the three ps2sdk multitap servers registered (ADR 0143), the Downhill
Domination probe's last bind moved to `rpc_bind_sid=2147485956=0x80000904`
(`had=0`). The title's `sceMtapInit` binds a contiguous `0x80000901..0x80000905`
range, so two more servers exist on the module it was built against.

Evidence:

- **Observed (probe report, runtime ABI 10):** after 0x80000902/0x80000903 are
  served, the unanswered bind is `0x80000904`, then `0x80000905`.
- **Inferred (not in ps2sdk; from mirrors of Sony's proprietary `libmtap` and a
  public decomp note of this same title):** `0x80000904` is
  `MTAP_CHANGE_THREAD_PRIORITY` and `0x80000905` is `MTAP_GET_MOD_VERSION`, both
  served by `MTAPMAN.IRX`; the version query is function 1. The sid space is
  shared across SDK generations, so this identity rests on the contiguous
  multitap bind sequence, not on ps2sdk. **Unknown:** the exact reply encoding of
  the version query and how the title branches on it.

## Decision

1. Register `kEeSifServerMtapSony4` (0x80000904) and `kEeSifServerMtapSony5`
   (0x80000905) alongside the ps2sdk multitap servers, handled by
   `serve_ee_mtap_call`, which records `mtap_call_count` / `last_mtap_number` and
   refuses every call. REPS2 models no multitap adapter and no `MTAPMAN` module,
   so no version or priority answer is fabricated.
2. No `EeSifCmdState` change, so the runtime ABI stays 10 and the ABI-10 module
   loads unchanged.

## Consequences

- All five multitap binds complete. The probe now shows the title issuing
  `rpc_call_number=1` on `0x80000905` (the version query) and blocking when it is
  refused, so the frontier is a semantic semaphore-wait stop, not budget
  exhaustion. Serving that query needs evidence about the reply encoding and the
  title's branch on it; until then it stays fail-closed.

## Validation

- `reps2_hle_syscall_codegen_smoke` extends the multitap registration test to all
  five sids (148 cases).
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF `adfda7b7...f0cf0c`,
  ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI 10, ABI-10 module reused
  unchanged, `D:\REPS2-DevTests\DownhillDomination\PostADR140_142\run-report-mtap904.json`):
  `rpc_bind_sid=0x80000905 rpc_bind_had=1 rpc_call_number=1 mtap_calls=1
  mtap_number=1 rpc_call_replied=0`; the run stops as `guest_stopped` at
  `pc=0x0025A724` after 4,413,234 instructions (the semaphore-wait boundary,
  `syscalls_seen=1034 handled=1016`, `callbacks={requested:78, completed:78}`,
  `timer3.cycles=4413234`). Not a boot stage.
