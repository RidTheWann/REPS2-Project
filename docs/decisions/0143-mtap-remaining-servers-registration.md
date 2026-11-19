# ADR 0143: Register the remaining multitap RPC servers (0x80000902, 0x80000903)

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE SIF RPC server table (`ee_hle.hpp`), two sids: 0x80000902
  (`MTAPSERV_PORT_CLOSE`) and 0x80000903 (`MTAPSERV_GET_CONNECTION`). No state
  layout change, so `REPS2_RUNTIME_ABI_VERSION` stays 10.

## Context

With the multitap port-open server registered (ADR 0140), the Downhill
Domination probe's last bind moved from `0x80000901` to
`rpc_bind_sid=2147485954=0x80000902` with `rpc_bind_had=0`, and the run exhausts
the 80M budget in the retry delay loop at `pc=0x00245B68`.

Evidence:

- **Observed (pinned PS2SDK `ee/rpc/multitap/src/libmtap.c`):** the multitap
  client stub binds three servers from one init —
  `#define MTAPSERV_PORT_OPEN 0x80000901`, `MTAPSERV_PORT_CLOSE 0x80000902`,
  `MTAPSERV_GET_CONNECTION 0x80000903` — and calls each with function 1. The
  resident IOP `mtapman` registers all three, so on real hardware all three
  binds succeed.
- **Observed (probe report, runtime ABI 10, `PostADR140_142/run-report-abi10.json`):**
  after `0x80000901` is served, the title's next unanswered bind is
  `0x80000902`.

## Decision

1. Register `kEeSifServerMtapPortClose` (0x80000902) and
   `kEeSifServerMtapGetConnection` (0x80000903) alongside the port-open server,
   all handled by `serve_ee_mtap_call`, which records `mtap_call_count` /
   `last_mtap_number` and refuses every call: REPS2 models no multitap adapter,
   so no port-close or connection-count answer is fabricated.
2. No `EeSifCmdState` change, so the runtime ABI stays 10 and modules built
   against it keep loading.

## Consequences

- The title's binds for all three multitap servers complete; the next probe
  shows which multitap function it actually issues and whether the frontier
  moves past the multitap stub entirely.

## Validation

- `reps2_hle_syscall_codegen_smoke` extends the multitap registration and bind
  tests to cover all three sids (148 cases).
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF `adfda7b7...f0cf0c`,
  ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI 10, the ABI-10 module
  reused unchanged, `D:\REPS2-DevTests\DownhillDomination\PostADR140_142\run-report-mtap902.json`):
  the `0x80000902`/`0x80000903` binds succeed and the frontier moves on — the last
  bind is now `rpc_bind_sid=2147485956=0x80000904` (`had=0`), with the 80M budget
  (79,999,982 instructions, 16 batches) exhausted in the retry loop at
  `pc=0x00245C18`; `mtap_calls=0`, `timer3.cycles=79999982`. **Inferred** (Sony
  `libmtap`, not in ps2sdk): `0x80000904` is `MTAP_CHANGE_THREAD_PRIORITY` and
  `0x80000905` `MTAP_GET_MOD_VERSION` from `MTAPMAN.IRX`, which the title's
  `sceMtapInit` binds as a contiguous 0x80000901..0x80000905 sequence. Not a boot
  stage; the next unit is registering `0x80000904`/`0x80000905` (ADR 0144).
