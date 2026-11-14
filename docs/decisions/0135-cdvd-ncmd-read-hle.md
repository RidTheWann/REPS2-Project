# ADR 0135: Serve CD_NCMD_READ from the host media source

- **Status:** Accepted.
- **Date:** 2026-11-14.
- **Scope:** NCmd server (0x80000595) function 1 HLE, SIF RPC_END delivery for
  NOWAIT calls with an end function, engine run report, runtime ABI.

## Context

A fresh bounded probe on runtime ABI 3 (branch `feat/ee-timer3-count-storage`,
Downhill Domination USA, ISO SHA-256
`0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`, boot ELF
`adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`, Windows
x64/MSVC) advanced past the `T3_COUNT` read (3,890,370 > 3,890,274 instructions)
and stopped as `semaphore_wait_blocked` at PC `0x0025A724`. The run report
shows the NCmd bind for sid `0x80000595` completed (`rpc_bind_had=1`), the
first NCmd call (function 14, `CD_NCMD_DISKREADY`) was answered
(`ncmd_replies=1, ncmd_retres=2`), and a second NCmd call with
`rpc_number=1, rpc_call_replied=0` was refused, so the caller's WaitSema
never resolves.

PS2SDK `ee/rpc/cdvd/src/ncmd.c` numbers function 1 as `CD_NCMD_READ`
(`sceCdRead`): a 24-byte `readData[6]` request (lbn, sectors, buf, the
`sceCdRMode` word packed as `trycount | spindlctrl<<8 | datapattern<<16`, the
callback data buffer, `&curReadPos`), sent `SIF_RPC_M_NOWAIT` with no reply
buffer and an end function (`_CdAlignReadBuffer`). The IOP DMAs
`sectors * 2048` user bytes into `buf` and sends `SIF_CMD_RPC_END`; the EE
kernel's `_request_end` raises the client semaphore and invokes the end
function. `libcdvd-common.h` defines `SCECdSecS2048 = 0` (2048-byte user data),
`SCECdSecS2328 = 1`, `SCECdSecS2340 = 2`.

References:
- PS2SDK `ee/rpc/cdvd/src/ncmd.c` (CD_NCMD_CMDS enum, sceCdRead)
- PS2SDK `common/include/libcdvd-common.h` (SCECdvdSectorType, sceCdRMode)
- PS2SDK `ee/kernel/src/sifrpc.c` (`_request_end`: RPC_END raises the client
  semaphore and calls the end function)

## Decision

1. `serve_ee_cdvd_ncmd_call` serves function 1 when the request is exactly the
   documented 24-byte `readData` and `datapattern == SCECdSecS2048`: the read
   is served straight from the installed `HostMediaSource` into EE main RAM
   at `buf` (bounds-checked via `resolve_ee_memory_access`), the completed
   byte count is stored at `curReadPos` when the pointer is valid, and the
   handler returns true so the RPC completes. Any other datapattern, an absent
   source, an out-of-range LBN (the host adapter validates its image length and
   writes nothing on failure), a request that did not transfer, or an
   unwritable destination fails closed: no reply, no memory mutation.
2. In `deliver_iop_rpc_call_reply`, a call with `rmode == M_NOWAIT` now skips
   the synthesized RPC_END only when the client also published no end function
   (`end_function == 0`). A NOWAIT call with an end function completes through
   RPC_END, which raises the client semaphore via the existing
   `process_ee_sif_rpc_end` path — the same `_request_end` behaviour PS2SDK
   documents. Calls with `rmode != 0` are unchanged, as are refused calls.
3. The run report's `sif` object gains `ncmd_read_calls`, `ncmd_read_lbn`,
   `ncmd_read_sectors`, `ncmd_read_bytes`, `ncmd_read_replied`.
4. `EeSifCmdState` gains the observation fields, so
   `REPS2_RUNTIME_ABI_VERSION` is incremented from 3 to 4.

## Consequences

- The real-title probe can advance past the `CD_NCMD_READ` wait when the title
  issues a 2048-byte sector read, with the sectors coming from the owner-supplied
  ISO through the host media boundary. This is a modelled-boundary advance,
  not a boot stage: `SCECdSecS2328/2340` reads, CDDA/DVD reads, chain reads,
  the uncached-buffer alignment path (`_rd_intr_data`) and SearchFile remain
  fail-closed, and no compatibility state is promoted without a fresh probe.
- The callback data buffer (`readData[4]`) is intentionally left untouched:
  REPS2 writes the sectors directly to `buf` and does not model the IOP-side
  uncached staging buffer. If a title depends on that staging path, the next
  probe stop will show it.
- ABI-3 modules are rejected at admission until rebuilt against ABI 4.

## Validation

- `reps2_hle_syscall_codegen_smoke` gains `test_ncmd_read_serves_sectors_from_host_media`:
  a 2-sector read at LBN 16 lands as the exact synthetic pattern in EE RAM,
  `curReadPos` reads 4096, RPC_END is delivered and the client semaphore is
  raised for a NOWAIT call with an end function; datapattern 2328, an
  out-of-range LBN, and a NOWAIT call without an end function are each
  asserted fail-closed or no-reply with no memory mutation.
- The native build driver smoke asserts runtime ABI 4.
- Local verification (Windows x64): `cargo fmt --all -- --check`,
  `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked`, CMake Release build and
  `ctest -C Release` all pass.
- A fresh real-title probe on this branch is recorded in PROJECT_STATE.md /
  RESEARCH.md with its exact stop; it is evidence for the next boundary, not a
  boot or playable claim.
