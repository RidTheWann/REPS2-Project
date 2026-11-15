# ADR 0138: Register the SIF loadfile RPC server (0x80000006) fail-closed

- **Status:** Proposed (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE SIF RPC server table (`ee_hle.hpp`, `ee_state.hpp`), one sid:
  0x80000006. Runtime ABI 5 -> 6 (two counters added to `EeSifCmdState`, which
  shifts the offsets of the fields that follow them).

## Context

The runtime-ABI-5 Downhill Domination probe on merged main (`de37824`,
ADR 0136 + 0137, `--extra-root 0x00246CD0`, relocated regions
`0x80076000:0x2829F8:0x740` and `0x80075000:0x2831A0:0x330`) passes the former
`unhandled_syscall 0x7C` stop at `0x0025AB14` and exhausts the full 80M
instruction budget (79,999,942 instructions, 16 batches) in a spin loop at
`pc=0x0025E880`. The run report records `rpc_bind_sid = 2147483654 = 0x80000006`
with `rpc_bind_had = 0`.

Correction: ADR 0137 and an earlier draft of this record wrote that sid as
`0x80000596`; the report's decimal `2147483654` is `0x80000006`. The
mis-transcription is corrected here and in the affected documents.

Evidence for what the sid is:

- **Observed (probe report, merged main `de37824`):** the last SIF RPC bind
  targets sid `0x80000006` and is answered no-server (`had=0`); the guest then
  spins in the `nopdelay` at `0x25E874..0x25E894` inside the bind retry loop
  around `0x0025E78C` (decoded from the boot ELF RAM image: `lui $2,0x0010;
  addiu $3,$zero,-1; … addiu $2,$2,-1; bnel $2,$3`).
- **Observed (pinned PS2SDK `ee/kernel/src/loadfile.c`):** the loadfile client
  stub binds `0x80000006` in a retry loop — `while ((res = sceSifBindRpc(&_lf_cd,
  0x80000006, 0)) >= 0 && !_lf_cd.server) nopdelay();` — and issues
  `sceSifCallRpc(&_lf_cd, <fno>, ...)`. This matches the observed sid and the
  observed retry-with-delay shape, so the bind is the SIF **loadfile** server
  (the resident IOP `loadfile` module).
- **Derived (pinned PS2SDK `common/include/loadfile-common.h`):** function
  numbers are `LF_F_MOD_LOAD = 0` (`SifLoadModule`), `LF_F_ELF_LOAD`,
  `LF_F_SET_ADDR`, `LF_F_GET_ADDR`, `LF_F_MOD_BUF_LOAD`, `LF_F_MOD_STOP`,
  `LF_F_MOD_UNLOAD`, `LF_F_GET_VERSION = 0xFF`. The exact function the title
  issues and its arguments are **unknown** until the probe records them.

## Decision

1. Register sid `0x80000006` (`kEeSifServerLoadFile`) in
   `initialize_hle_boot_environment`, so the bind succeeds exactly as on the
   resident IOP. A registered server answers with its own sid as the `sd` token
   (`deliver_iop_rpc_bind_reply`), so the title's retry loop completes.
2. The call handler `serve_ee_loadfile_call` models no function: it records
   `loadfile_call_count` / `last_loadfile_number` and refuses the call (no
   reply, no RPC_END, no semaphore signal). REPS2 executes no IOP module, so a
   fabricated "module loaded" answer is refused; the caller stays blocked with
   an honest boundary. Same register-but-refuse convention as ADRs 0121/0125.
   (ADR 0139 then answers the observed `LF_F_GET_VERSION` query; every other
   function still refuses.)
3. `EeSifCmdState` gains the two counters, so `REPS2_RUNTIME_ABI_VERSION` moves
   5 -> 6; modules built against ABI 5 are rejected at admission.

## Consequences

- The title's bind for 0x80000006 completes; the next probe shows which loadfile
  function number the guest issues, naming the next unit. Actually serving a
  module load would require an IOP module loader and IOP RAM, which do not
  exist, so loadfile stays fail-closed beyond the bind.

## Validation

- `reps2_hle_syscall_codegen_smoke` gains
  `test_boot_environment_registers_the_sif_loadfile_server`,
  `test_bind_to_the_sif_loadfile_server_reaches_a_registered_server` and
  `test_sif_loadfile_calls_are_recorded_and_refused` (139 cases): the server is
  in the table, a bind reaches it and writes the sid as the `sd` token, and a
  call is recorded by number with no reply, no RPC_END and an untouched
  semaphore.
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF `adfda7b7...f0cf0c`,
  ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI 6, module rebuilt with
  this runtime, `D:\REPS2-DevTests\DownhillDomination\PostADR138\run-report-abi6-loadfile.json`):
  the bind for `0x80000006` now completes (`rpc_bind_sid=0x80000006
  rpc_bind_server=0x80000006 rpc_bind_had=1`), so the delay loop is gone. The
  title then issues one loadfile call, `rpc_call_number=255` (`LF_F_GET_VERSION`,
  0xFF), which the handler records (`loadfile_calls=1 loadfile_number=255`) and
  refuses, so the caller blocks: the run stops as `guest_stopped` at
  `pc=0x0025A724` after 4,410,459 instructions (the semaphore-wait boundary,
  same class as ADR 0119/0135), not as budget exhaustion. The next unit is
  `LF_F_GET_VERSION` (ps2sdk `iop/system/loadfile/src/loadfile.c` answers
  `0x30303133` in a 4-byte reply). Not a boot stage.
