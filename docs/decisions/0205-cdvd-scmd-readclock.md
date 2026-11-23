# ADR 0205: Serve CD_SCMD_READCLOCK on the CDVD S-command RPC server (0x80000593, fno 1) (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Scope:** `runtime/include/reps2/runtime/ee_hle.hpp` (SCMD dispatch and
  READCLOCK handler), `tests/hle_syscall_smoke.cpp`, `docs/RESEARCH.md`.
- **Related:** ADR 0121/0122 (S-command server + MMODE), ADR 0204 (previous frontier).

## Context

ABI-42 probe `abi56-01` (main `936c7a2`, runtime ABI 41) advanced the Downhill
frontier from `pc=0x0022FFC8` (VIAND) to `pc=0x0025A724`, the `WaitSema` stub,
stopping as `semaphore_wait_blocked` after 4,722,454 instructions. This is the
first frontier in this sequence that is a service gap, not an unsupported
instruction. The report shows `rpc_call_server=0x80000593` (=2147485075),
`rpc_call_number=1`, `rpc_call_send_size=0`, `rpc_call_recv_size=16`,
`rpc_call_replied=0`: the CDVD S-command server, function 1. The registered
handler only served `CD_SCMD_MMODE` (0x22, ADR 0122), so function 1 was refused
with no RPC_END and the caller blocked forever.

Evidence:
- PS2SDK `ee/rpc/cdvd/src/scmd.c`: `CD_SERVER_SCMD 0x80000593`,
  `enum CD_SCMD_CMDS { CD_SCMD_READCLOCK = 0x01, ... CD_SCMD_MMODE = 0x22 }`.
  `sceCdReadClock` calls `sceSifCallRpc(&clientSCmd, CD_SCMD_READCLOCK, 0,
  NULL, 0, sCmdRecvBuff, 16, NULL, NULL)` — no request, 16-byte reply.
- OpenPS2Loader `modules/iopcore/cdvdfsv/scmd.c` `rpcSCmd_cdreadclock` writes
  `cdvdreadclock_res_t { int result; sceCdCLOCK rtc; }` with
  `r->result = sceCdReadClock(&r->rtc)`.
- PS2SDK `common/include/libcdvd-common.h`: `sceCdCLOCK` is eight `u8` fields
  in order `{ stat, second, minute, hour, pad, day, month, year }`, all BCD;
  `sceCdReadClock` returns 1 on success.

## Decision

1. Split the SCMD handler: `serve_ee_cdvd_scmd_mmode` (unchanged semantics,
   ADR 0122) and new `serve_ee_cdvd_scmd_readclock`. The registered
   `serve_ee_cdvd_scmd_call` dispatches by function number and keeps the
   fail-closed rule: any other S-command returns false with no RPC_END.
2. READCLOCK requires `recv_addr != 0` and `recv_size >= 16`; writes
   `{ result = 1, clock = {0,0,0,0,0,1,1,0} }` (little-endian) to the reply
   buffer. That clock is 2000-01-01 00:00:00, stat=0, in BCD (0 and 1 have
   identical binary/BCD encodings).
3. The clock is a fixed constant, not the host clock: REPS2 frontier records
   must be byte-reproducible (`tools/frontier/frontier.py compare` walks every
   compared field). A host-derived value would make two identical runs differ.
   This is a bounded model of an IOP service, not a claim of RTC emulation.
4. No new runtime state, no ABI bump. Existing `scmd_call_count`,
   `scmd_replies`, `last_scmd_number` report fields cover the new function.

## Consequences

- Frontier should advance past `pc=0x0025A724`.
- Other S-commands remain refused and recorded; no fabricated result.
- No boot/playable claim.

## Validation

- `test_cdvd_scmd_readclock_replies_with_bcd_clock` in
  `tests/hle_syscall_smoke.cpp` checks the result word, the eight clock bytes,
  reply count and RPC_END completion. 200 HLE smoke cases pass.
- `cargo fmt/clippy/test` (400 lib tests) and CTest 51/51.
- Fresh probe (abi57) recorded in `docs/RESEARCH.md`.