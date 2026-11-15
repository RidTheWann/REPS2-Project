# ADR 0147: Serve the multitap port-open/close/connection commands as "no adapter"

- **Status:** Proposed (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE SIF RPC server table (`ee_hle.hpp`) handlers for sids
  0x80000901/0x80000902/0x80000903; `EeSifCmdState` observation fields
  (`ee_state.hpp`). Runtime ABI 11 -> 12.

## Context

With the multitap module-version query served (ADR 0145), the Downhill Domination
probe's frontier moved to the title's next multitap call: function 1 on
`0x80000901` (`rpc_call_server=2147485953=0x80000901`, `rpc_call_number=1`,
`rpc_call_replied=0`), which was recorded and refused, leaving the caller blocked
as `guest_stopped` at `pc=0x0025A724` after 4,413,559 instructions.

Evidence:

- **Observed (owner's boot ELF, SHA-256 `adfda7b7...f0cf0c`):** after
  `sceMtapInit` succeeds, the title calls the wrapper at `0x00245D28` four times
  with `$a0 = 0, 1, 2, 3` (call sites at `0x0021F4F0`, `0x0021F4F8`, `0x0021F500`,
  `0x0021F508`). The wrapper stores the port in request word 0
  (`sw $a0, -0xec0($v0)`, buffer `0x76F140`), issues
  `sceSifCallRpc(cd=0x76F040, fno=1, send=0x76F140, 0x80, recv=0x76F140, 0x80, 0,
  0)`, and returns reply word 1 (`lw $v0, 4($s0)`).
- **Observed (PS2SDK `ee/rpc/multitap/src/libmtap.c` and the Sony `libmtap`
  decode):** `sceMtapPortOpen`/`sceMtapPortClose`/`sceMtapGetConnection` set
  `rpc_buffer[0] = port`, call function 1, and `return rpc_buffer[1]`. So reply
  word 1 is the operation result.
- **Inferred:** with no multitap adapter, there is no port to open and no
  connections to count, so the truthful result of each of those operations is 0.
  An independent port of this title likewise reports "not on" whenever no
  multitap is configured.

## Decision

1. Register `0x80000901`/`0x80000902`/`0x80000903` with a dedicated handler
   `serve_ee_mtap_port_call`. Function 1 requires the documented one-word request
   (the port) and a reply buffer of at least two words; it writes 0 into reply
   word 1 (leaving reply word 0, the caller's port echo, untouched) and answers
   (`SIF_CMD_RPC_END`, caller semaphore raised). That is the honest result of a
   port open/close or a connection count with no adapter: the port is not open and
   nothing is connected. No controller, port, or connection state is fabricated.
   Any other function number, a missing request word, or too small a reply buffer
   is recorded and refused.
2. `EeSifCmdState` gains `mtap_port_calls`, `mtap_port_replies`, `last_mtap_port`,
   `last_mtap_result` and `last_mtap_port_replied`, so
   `REPS2_RUNTIME_ABI_VERSION` moves 11 -> 12; modules built against ABI 11 are
   rejected at admission.

## Consequences

- The `sceMtapPortOpen(0..3)` calls complete with result 0 and any
  `sceMtapGetConnection` reports 0 connections, so the title sees "no multitap"
  rather than blocking.
- This does not emulate a multitap; it models its absence. The probe shows the
  title leaving the multitap path entirely and reaching a new bind retry loop for
  SIF sid `0x80000100`, which is the next frontier.

## Validation

- `reps2_hle_syscall_smoke` adds five cases (155 total): port-open answers reply
  word 1 = 0, leaves reply word 0 untouched, signals the semaphore and records
  `last_mtap_port`/`last_mtap_result`; get-connection answers 0 with the requested
  port recorded; a non-1 function, a missing request word, and a reply buffer whose
  word 1 would wrap past 4 GiB (`recv_addr = 0xFFFFFFFC`) are refused (no reply, no
  RPC_END, RAM word 0 untouched).
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF `adfda7b7...f0cf0c`,
  ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI 12, the generated
  sources unchanged and the module rebuilt from these headers,
  `D:\REPS2-DevTests\DownhillDomination\PostADR140_142\run-report-mtapport.json`):
  all four `sceMtapPortOpen(0..3)` calls are served
  (`mtap_port_calls=4 mtap_port_replies=4 mtap_port=3 mtap_port_result=0
  mtap_port_replied=1`) with `rpc_call_replied=1`, and the title is no longer
  blocked on the multitap semaphore. It then reaches a bind retry loop for sid
  `0x80000100` (`rpc_bind_sid=2147483904=0x80000100 rpc_bind_had=0`) at
  `pc=0x00244DB8..0x00244DF8` and the run exhausts the 80M instruction budget
  (79,999,970 instructions, 16 batches) at `pc=0x00244DC0`
  (`mtap_calls=5`, `mtap_version_replied=1`, `callbacks={requested:78,
  completed:78}`, `timer3.cycles=79999970`). Budget exhaustion is a weaker
  evidence class than a semantic stop and is not a boot stage.