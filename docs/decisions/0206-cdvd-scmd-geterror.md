# ADR 0206: Serve CD_SCMD_GETERROR on the CDVD S-command RPC server (0x80000593, fno 4) (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Scope:** `runtime/include/reps2/runtime/ee_hle.hpp` (SCMD dispatch and
  GETERROR handler), `tests/hle_syscall_smoke.cpp`, `docs/RESEARCH.md`.
- **Related:** ADR 0205 (READCLOCK, the immediately preceding frontier),
  ADR 0122 (MMODE), ADR 0121 (S-command server registration).

## Context

ABI-42 probe `abi57-01` (runtime ABI 41, after ADR 0205) advanced the Downhill
frontier from `pc=0x0025A724` past the READCLOCK call: instructions rose from
4,722,454 to 5,059,993 (+337,539) but the run still ends `guest_stopped` /
`semaphore_wait_blocked` at `pc=0x0025A724`. The report now shows the last
refused call as `rpc_call_server=0x80000593` (`CD_SERVER_SCMD`),
`rpc_call_number=4`, `rpc_call_send_size=0`, `rpc_call_recv_size=4`,
`rpc_call_replied=0`; `scmd_calls=3 scmd_replies=2 scmd_number=4`. So READCLOCK
(fno 1) was answered and the title immediately polled the error status.

Evidence: PS2SDK `ee/rpc/cdvd/src/scmd.c` enum — `CD_SCMD_READCLOCK = 0x01`,
`CD_SCMD_GETDISKTYPE = 0x03`, `CD_SCMD_GETERROR = 0x04`;
`sceCdGetError()` sends no payload and reads a 4-byte reply.
`libcdvd-common.h` `SCECdvdErrorCode { SCECdErFAIL = -1, SCECdErNO = 0x00, ... }`.
The OPL cdvdfsv handler for `CD_SCMD_GETERROR` is `*(int *)buf = sceCdGetError()`.

## Decision

1. Add `serve_ee_cdvd_scmd_geterror` dispatched at `rpc_number == 4` in
   `serve_ee_cdvd_scmd_call`, alongside MMODE (0x22) and READCLOCK (0x01).
2. Requires `recv_addr != 0` and `recv_size >= 4`; writes one word
   `SCECdErNO` (0) to the reply buffer. REPS2 models no physical drive and no
   failed CDVD transfer, so "no error" is the only truthful value — not a
   fabricated failure.
3. Every other S-command number stays refused (no RPC_END).
4. No new runtime state, no ABI bump. Existing `scmd_*` report fields cover it.

## Consequences

- Frontier should advance past the GETERROR poll at `pc=0x0025A724`.
- GETDISKTYPE (fno 3) and the remaining S-commands stay refused until
  evidenced.
- No boot/playable claim.

## Validation

- `test_cdvd_scmd_geterror_replies_no_error` in `tests/hle_syscall_smoke.cpp`;
  201 HLE smoke cases pass. CTest 51/51, cargo fmt/clippy/test (400 lib).
- Fresh probe (abi58) recorded in `docs/RESEARCH.md`.