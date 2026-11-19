# ADR 0148: Serve the pad RPC servers (0x80000100/0x80000101) — version query with the retail PADMAN word, INIT/OPEN with padman's success result

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE SIF RPC server table (`ee_hle.hpp`) handlers for sids
  0x80000100/0x80000101; `EeSifCmdState` observation fields (`ee_state.hpp`);
  run-report fields (`engine/src/main.cpp`). Runtime ABI 12 -> 13.

## Context

With the multitap path cleared (ADR 0147), the Downhill Domination probe's
frontier was a bind retry loop for SIF sid `0x80000100`
(`rpc_bind_sid=2147483904`, `had=0`) at `0x00244DB8..0x00244DF8`, exhausting
the 80M budget at `pc=0x00244DC0`.

- **Observed (target disassembly, capstone little-endian MIPS32 on the
  owner-supplied boot ELF `adfda7b7...f0cf0c`; runtime VAs):** the block at
  `0x00244DD4..0x00244DF8` is a `sceSifBindRpc` retry loop for sid
  `0x80000100` (immediate `lui $a1,0x8000; ori $a1,$a1,0x100`,
  `jal 0x25cdb8`, retry while `[client+0x24] == 0`); the block at
  `0x00244E10..0x00244E50` binds sid `0x80000101` the same way. It then calls
  the helper at `0x00245A58`, which stores `0x12` into request word 0 of
  buffer `0x75EFC0` and issues `sceSifCallRpc(cd=0x75ED80, fno=1, rmode=0,
  send=recv=0x80)`, returning reply word 3 (`lw $v0, 0xc($s0)`). At
  `0x00244E64..0x00244E68` the title checks `(version >> 8) == 4`.
- **Derived (pinned ps2sdk `ps2dev/ps2sdk`):** `0x80000100` is
  `PAD_BIND_RPC_ID1_NEW` and `0x80000101` is `PAD_BIND_RPC_ID2_NEW`
  (`ee/rpc/pad/src/libpad.c`), registered by the resident IOP `padman`
  (`iop/input/padman/src/rpcserver.c`:
  `sceSifRegisterRpc(&sdext[0], PAD_BIND_RPC_ID1, ...)` and `..._ID2...`).
  ps2tek's "SIF RPC System Server IDs" lists `80000100h - Pad (PADMAN)` and
  `80000101h - Pad extension? (PADMAN)`. Every padman call uses function 1;
  the command travels in request word 0 and the result in reply word 3
  (`data[3] = ...` in every `Rpc*` handler). The relevant commands are
  `PAD_RPCCMD_OPEN = 0x01`, `PAD_RPCCMD_INIT = 0x10`,
  `PAD_RPCCMD_GET_MODVER = 0x12`. `RpcGetModVersion` writes
  `data[3] = padGetModVersion()` (`_irx_id.v`); `RpcPadInit` writes
  `data[3] = padInit(...)` (1 on success — `padInit.c` returns 1); `RpcPadOpen`
  writes `data[3] = padPortOpen(port, slot, ...)` (1 once the port/slot is
  opened — even with no pad attached, since pad absence is detected
  asynchronously via INFO_MODE, not through this return).
- **Version word:** the title's check constrains only the major byte
  (`(ver >> 8) == 4`). ps2sdk `iop/input/padman/src/freepad.c` comments that
  the retail SDK 3.0.3/3.1.0 PADMAN reports version `0x04,0x22`, while the
  open-source ps2sdk padman is `IRX_ID("padman", 3, 6)` = `0x0306`.
  **Derived:** the served word is `0x0422` (retail SDK PADMAN version, major
  byte 4, matching the title's check). **Unknown:** the exact retail BOOTROM
  XPADMAN version string — ps2tek does not establish the GET_MODVER return
  value on real hardware.

## Decision

1. Register `0x80000100` with a dedicated handler `serve_ee_pad_call` and
   `0x80000101` with `serve_ee_pad_ext_call` (records and refuses every call),
   so the title's `scePadInit` binds succeed exactly as against the resident
   IOP `padman`.
2. `serve_ee_pad_call` requires function 1, a request word
   (`send_size >= 4`), and room for reply word 3
   (`recv_addr != 0`, `recv_addr <= 0xFFFFFFF3`, `recv_size >= 16`), so a
   wrapping reply buffer is refused instead of mutating RAM[0]. It serves:
   - `GET_MODVER` (0x12): reply word 3 = `0x0422` (retail PADMAN version word).
   - `INIT` (0x10): reply word 3 = 1 (`padInit` returns 1 on success).
   - `OPEN` (0x01): port/slot read from request words 1/2
     (`send_size >= 12`); reply word 3 = 1 (`padPortOpen` returns 1 once the
     port/slot is opened, even with no pad attached — pad absence is
     asynchronous and reported through INFO_MODE, which stays fail-closed).
     REPS2 models no controller and fabricates no pad data.
   - any other command: recorded and refused (no reply, no RPC_END, caller
     stays blocked).
3. `EeSifCmdState` gains `pad_call_count`/`last_pad_number`,
   `pad_ext_calls`/`last_pad_ext_number`, the `pad_version_*`, `pad_init_*`
   and `pad_open_*` observation fields; the run report serializes them.
   Runtime ABI 12 -> 13.

## Consequences

- The title's `scePadInit` binds both servers, the `(version >> 8) == 4`
  check passes as it does against retail PADMAN, and `padPortInit`'s INIT
  call completes with padman's success result 1. A later `scePadPortOpen`
  returns 1 exactly as `padPortOpen` does with no pad attached; the
  controller's absence is only discoverable through the asynchronous
  INFO_MODE path, which stays fail-closed — no pad is fabricated.
- Unmodelled pad commands and all calls on `0x80000101` stay fail-closed.
- The observed frontier moves past pad init to the title's next bind.

## Validation

- `reps2_hle_syscall_smoke` adds nine cases (165 total): both servers are
  registered; a bind to `0x80000100` reaches the server; GET_MODVER (fn 1,
  command 0x12) answers reply word 3 = `0x0422` and leaves reply word 0
  untouched, signals the semaphore and records
  `last_pad_version`/`last_pad_version_replied`; INIT (0x10) answers 1; OPEN
  (0x01, port 1 slot 0) answers result 1 and records
  `last_pad_port`/`last_pad_slot`/`last_pad_handle`; an unmodelled command, a
  non-1 function, a missing request word, and a reply buffer whose word 3
  would wrap past 4 GiB (`recv_addr = 0xFFFFFFF4`) are refused (no reply, no
  RPC_END, RAM word 0 untouched); calls on `0x80000101` are recorded and
  refused.
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF
  `adfda7b7...f0cf0c`, ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI
  13, ABI-13 module rebuilt from these headers,
  `PostADR140_142/run-report-pad.json`): the `0x80000100` bind succeeds, the
  GET_MODVER call is served (`pad_version_calls=1 pad_version_replies=1
  pad_version=1058=0x0422 pad_version_replied=1`), the title's
  `(version >> 8) == 4` check passes, and the INIT call is served
  (`pad_init_calls=1 pad_init_replies=1 pad_init_result=1 pad_init_replied=1`,
  `rpc_call_server=0x80000100 rpc_call_number=1 rpc_call_replied=1`); no OPEN
  call is issued yet (`pad_open_calls=0`). The title leaves the pad path and
  reaches a bind retry loop for sid `0x80000400` (MCSERV, memory cards;
  `rpc_bind_sid=2147484672=0x80000400 had=0`), exhausting the 80M budget
  (79,999,922 instructions, 16 batches) at `pc=0x00245F00`
  (`pad_calls=2`, callbacks 78/78, `timer3.cycles=79999922`). No
  guest-visible boot stage.
