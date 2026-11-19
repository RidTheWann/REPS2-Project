# ADR 0149: Register the memory-card server (0x80000400) and serve XMCSERV INIT with the module-version words

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE SIF RPC server table (`ee_hle.hpp`) handler for sid
  0x80000400; `EeSifCmdState` observation fields (`ee_state.hpp`); run-report
  fields (`engine/src/main.cpp`). Runtime ABI 13 -> 14.

## Context

With the pad path cleared (ADR 0148), the Downhill Domination probe's frontier
was a bind retry loop for SIF sid `0x80000400`
(`rpc_bind_sid=2147484672`, `had=0`) at `pc=0x00245F00`, exhausting the 80M
budget (79,999,922 instructions, 16 batches).

- **Derived (pinned ps2sdk + ps2tek):** `0x80000400` is the memory-card SIF RPC
  server — ps2sdk `ee/rpc/memorycard/src/libmc.c` binds `rpc_id = 0x80000400`
  for the SIO2 interface (DEV9 would be `0x80000480`), served by the resident
  IOP `mcserv` (`iop/memorycard/mcserv/src/mcserv.c`:
  `sceSifRegisterRpc(..., MCSERV_RPC_ID /* 0x80000400 */, ...)`); ps2tek's
  System Server IDs list `80000400h - Memory cards (MCSERV)`. mcserv dispatches
  on the RPC **function number** (`cb_rpc_S_0400(u32 fno, ...)`, switch on
  `fno`): XMCSERV INIT = `0xFE`, GET_INFO = `0x01`, OPEN = `0x02`, ...;
  legacy MCSERV INIT = `0x70`, GET_INFO = `0x78`, ... Normal calls reply a
  4-byte `s32 result`; INIT replies the 12-byte `mcRpcStat_t`
  `{result, mcserv_version, mcman_version}` (`common/include/libmc-common.h`).
- **Observed (target disassembly, capstone LE MIPS64 on the owner's boot ELF
  `adfda7b7...f0cf0c`; runtime VAs):** the title's `sceMcInit` at `0x00245E80`
  binds `0x80000400` (retry loop `0x00245EF8..0x00245F68` on
  `client+0x24 == 0`), then issues the INIT call inline at `0x00245F94`:
  fno `0xFE`, mode 0, send `0x30` (48-byte `mcDescParam_t`), recv `0x0C` into
  `0x00760780`. It then requires reply word 1 (mcserv version) `>= 0x20A`
  (`slti` at `0x00245FBC`; else prints `"libmc: too old release of
  mcserv.irx\n"` at `0x0029A3C8` and returns -120) and reply word 2 (mcman
  version) `>= 0x20E` (`slti` at `0x00245FE0`; else -121), and returns reply
  word 0. The next step is a poll loop at `0x0021F840` calling `0x00246030`
  (fno `0x15`, port in request word 1, send `0x30`, recv 4) for ports 0 and 1.
- **Version words:** ps2sdk's own mcserv/mcman report `0x208`/`0x20B`
  (`mcserv.h MODVER`, `IRX_ID(MODNAME, 2, 11)`) — below the title's thresholds.
  **Derived:** REPS2 has no MCSERV/MCMAN build to name, so it answers the
  minimum words the title's own checks accept: `0x20A`/`0x20E`. **Unknown:**
  the exact retail MCSERV/MCMAN module version strings.

## Decision

1. Register `0x80000400` with a dedicated handler `serve_ee_mcserv_call`, so
   the title's `sceMcInit` bind succeeds exactly as against the resident IOP
   `mcserv`.
2. `serve_ee_mcserv_call` serves only fno `0xFE` (XMCSERV INIT): it requires
   a request word (`send_size >= 4`) and room for the 12-byte reply
   (`recv_addr != 0`, `recv_addr <= 0xFFFFFFF7`, `recv_size >= 12`, so a
   wrapping reply buffer is refused instead of mutating RAM[0]), and answers
   `{result = 0 (sceMcResSucceed), mcserv_version = 0x20A, mcman_version =
   0x20E}`. INIT touches no card hardware (`McRetOnly` returns 0), so this
   fabricates nothing. Every other function number — including the fno `0x15`
   get-info poll the title issues next, whose identity is not pinned in
   ps2sdk — is recorded and refused, leaving the caller blocked.
3. `EeSifCmdState` gains `mc_call_count`/`last_mc_number`, the
   `mc_init_*`/`last_mc_*` observation fields; the run report serializes them.
   Runtime ABI 13 -> 14.

## Consequences

- The title's `sceMcInit` bind succeeds, the version checks pass with the
  minimum accepted words, and INIT returns success 0, so the title leaves the
  mcserv handshake. No memory card is modelled: any later card command stays
  fail-closed and blocks its caller.
- The observed frontier moves past the handshake to the title's next stop.

## Validation

- `reps2_hle_syscall_smoke` adds six cases (171 total): the server is
  registered; a bind to `0x80000400` reaches it; fno `0xFE` answers reply
  words `{0, 0x20A, 0x20E}`, signals the semaphore and records
  `last_mc_result`/`last_mc_serv_version`/`last_mc_man_version`; fno `0x15`,
  a missing request word, and a reply buffer whose word 2 would wrap past
  4 GiB (`recv_addr = 0xFFFFFFF8`) are refused (no reply, no RPC_END, RAM word
  0 untouched).
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF
  `adfda7b7...f0cf0c`, ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI
  14, ABI-14 module rebuilt from these headers,
  `PostADR140_142/run-report-mcserv.json`): the `0x80000400` bind succeeds
  (`rpc_bind_had=1`), INIT is served (`mc_calls=1 mc_number=254=0xFE
  mc_init_calls=1 mc_init_replies=1 mc_result=0 mc_serv_version=522=0x20A
  mc_man_version=526=0x20E mc_init_replied=1`), and the title leaves
  `sceMcInit`; the run then stops as `unsupported_instruction` (guest word
  `0x70081EE9`, a BSHFL-family byte swap inside the memcpy at `0x00254204`)
  at `pc=0x00254230` after 4,416,745 instructions, 1 batch (callbacks 78/78,
  `timer3.cycles=4416745`). No guest-visible boot stage.
