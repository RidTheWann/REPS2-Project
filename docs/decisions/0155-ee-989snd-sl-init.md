# ADR 0155: Serve the title's 989snd SL_INIT RPC and deliver RPC_END for every served call

- **Status:** Proposed (validated locally; real-title probe recorded below).
- **Date:** 2026-11-17.
- **Scope:** EE SIF RPC server handlers (`ee_hle.hpp`): a `serve_ee_snd_call`
  SL_INIT body for the 989snd main server (0x00123456), a distinct refusing
  `serve_ee_snd_loader_call` for the 989snd loader server (0x00123457), and the
  removal of the `rmode == M_NOWAIT && end_function == 0` short-circuit in
  `deliver_iop_rpc_call_reply`; the 989snd run-report fields
  (`ee_state.hpp`, `engine/src/main.cpp`); a corrected ADR 0135-era expectation
  in `reps2_hle_syscall_codegen_smoke`. Runtime ABI 16 -> 17.

## Context

ADR 0152 registered the title's two 989snd servers (0x00123456/0x00123457) with
a single handler that refused every call. That moved the frontier off the bind
retry loop onto the driver's first 989snd RPC call, but the call was refused, so
the driver stayed blocked in its own poll loop and the probe ended
`budget_limit_reached` at `pc=0x00201FC0`.

- **Observed (target disassembly of the owner's boot ELF `adfda7b7...f0cf0c`;
  runtime VAs; words read from the generated module's RAM image where file
  offset equals guest virtual address):**
  - `0x00201180` `snd_StartSoundSystemEx` binds both servers. The first bind
    (`0x00201260`) uses client `$4 = 0x004C8940` and sid `0x00123456`; the
    second (`0x00201308`) uses client `$4 = 0x004CD3C0` and sid `0x00123457`.
  - The SL_INIT send site is inside `0x00201180`..`0x002013C4`: `0x00201394`
    `move $a0,$zero` (command 0), `0x002013A4` `addiu $a1,$zero,8` (8-byte
    request), `0x002013AC` `sw $s7,4($sp)` (flags) and `0x002013B0`
    `sw $a3,($sp)` with `$a3 = 0x004CD380` (the `gStats` EE address), then
    `0x002013B4` `move $a2,$sp` and `0x002013C0` `jal 0x00201E90`. `$a3` is set
    at `0x0020138C` `addiu $a3,$s6,-0x2C80` with `$s6 = 0x004D0000`
    (`0x00201248` `lui $s6,0x4d`), giving `0x004CD380`.
  - `0x00201E90` `snd_SendIOPCommandAndWait` copies the 8 request bytes into the
    send buffer `0x004C89C0` (`0x00201ED0` `addiu $a3,$v0,-0x7640` with
    `$v0 = 0x004D0000`), prepares the return buffer at `0x00201F1C`
    (`jal 0x002016E8` `snd_PrepareReturnBuffer`, `$a0 = 0x004C8980`
    `gSyncBuffer`, mode 1), and polls via `jal 0x00201640` at `0x00201FC0`.
  - `0x00201640` `snd_GotReturns` first calls the SDK's `_sceSifCheckRpc`
    (`0x0020166C` `jal 0x0025D178`, `$a0 = 0x004C8940`) and returns ready only
    when it returns 0, then requires `gCommBusy[0] == 0xFFFFFFFF`
    (`0x00201688`) and `gCommBusy[gAwaitingInts+1] == 0xFFFFFFFF`
    (`0x002016A8`). `snd_PrepareReturnBuffer` zeroes both slots and sets
    `gCommBusy = 0x004C8980`, `gAwaitingInts = 1`.
  - `0x0025D178` `_sceSifCheckRpc` returns 1 iff `cd->pkt_addr != 0`, the
    client's rpc_id matches the packet server and bit 0 of `pkt+0x10` is set;
    it returns 0 once `cd->pkt_addr` is clear. `0x0025CF88` `sceSifCallRpc`
    writes `cd->pkt_addr` and the packet fields this model already reads.
- **Observed (989snd EE/IOP decompilation, behavioral reference only — no code
  copied):** the IOP `snd_CMD_SL_INIT` handler stores request word 0 in
  `gEEStatusAddr`, starts the sound system, and writes no return of its own, so
  only the return-buffer completion handshake is returned, `{-1, 0, -1}`
  (`snd_EEMessageParser` sets `buffer[2] = -1`; `snd_StartEEMessaging` sets
  `buffer[0] = -1`). The EE client's `snd_GotReturns` accepts exactly that.
- **Derived:** serving SL_INIT alone is insufficient. `snd_GotReturns` gates on
  `_sceSifCheckRpc`, i.e. on `cd->pkt_addr` being cleared, which happens only
  when the EE-side `_request_end` (`process_ee_sif_rpc_end`, `rpc_packet_free`)
  runs — which requires the IOP to have sent `SIF_CMD_RPC_END`. ADR 0135's
  short-circuit modelled "nothing consumes the reply" for `M_NOWAIT` without an
  end function, but the IOP sends RPC_END for **every** completed call and the
  EE-side handler completes the client regardless of `rmode`; a NOWAIT caller
  with no end function simply observes completion by polling the client. The
  short-circuit was therefore wrong for the title's SL_INIT call (NOWAIT, no end
  function) and left `pkt_addr` set, so the frontier never moved.
- **Unknown:** the remaining 989snd function set beyond SL_INIT (the main server
  is a command dispatch table; only slot 0 is evidenced), the loader server
  protocol (`SL_LOADBANK`, ...), and the 989snd return encodings beyond the
  init handshake. None is claimed.

## Decision

1. Replace the single 989snd handler with two: `serve_ee_snd_call` for the main
   server (0x00123456) and `serve_ee_snd_loader_call` for the loader server
   (0x00123457).
2. `serve_ee_snd_call` serves only function 0 (`SL_INIT`) with the evidenced
   8-byte request `{EEStatusAddr, flags}` and 12-byte reply buffer. It writes
   the completion handshake `{-1, 0, -1}` into the caller's reply buffer and
   records `last_snd_status_addr`/`last_snd_flags`. Every other function number,
   a malformed request, an unreadable request, or a too-small reply buffer is
   recorded and **refused** with no mutation, so a caller that waits on an
   unmodelled sound command stays blocked instead of receiving a fabricated
   reply.
3. `serve_ee_snd_loader_call` records and refuses every call: the loader's bank
   and MMD loads need a real IOP filesystem and sound allocator, neither of
   which is modelled.
4. Remove the `rmode == M_NOWAIT && end_function == 0` short-circuit in
   `deliver_iop_rpc_call_reply`: a served call always delivers `SIF_CMD_RPC_END`,
   whatever its `rmode`, matching the IOP and the EE-side `_request_end`. This
   supersedes ADR 0135's heuristic. A refused call still delivers no RPC_END.
5. Adding the `snd_*` fields to `EeSifCmdState` shifts the `EeState` layout, so
   Runtime ABI 16 -> 17; the native-build driver smoke's expected ABI follows.

## Consequences

- The title's SL_INIT call now completes: the reply handshake lands in the
  caller's return buffer, RPC_END clears the client `pkt_addr`, and
  `snd_GotReturns` returns ready, so `snd_SendIOPCommandAndWait` returns and the
  frontier leaves the `0x00201FC0` poll loop.
- A served NOWAIT call with no end function now raises the client semaphore and
  increments `rpc_call_replies` where it previously did neither; the shared SIF
  dispatch is consistent for all served calls.
- No sound is modelled and no sound state is fabricated: SL_INIT only completes
  the synchronous handshake and records the two request words. Any later 989snd
  call stays fail-closed. No compatibility state changes.

## Validation

- `reps2_hle_syscall_codegen_smoke` adds three cases (180 total): the 989snd
  main server serves SL_INIT (reply `{-1,0,-1}`, `snd_replies`, recorded status
  address and flags); a malformed SL_INIT is refused with no mutation; the main
  server refuses any other function number. The ADR 0135-era NOWAIT-without-end-
  function expectation is corrected: the served call now delivers RPC_END and
  raises the client semaphore. The native-build driver smoke expects runtime
  ABI 17.
- CMake Release build and `ctest -C Release` (43/43); Rust `fmt`/`clippy`/`test`
  on Windows x64/MSVC.
- Real-title probe (owner's ISO SHA-256 `0fe2b777...a7eb86`, boot ELF
  `adfda7b7...f0cf0c`, Windows x64/MSVC, runtime ABI 17, frontend and native
  module rebuilt from this branch —
  `PostADR155/run-report-989snd-sl-init.json`): the SL_INIT call is served
  (`snd_calls=2 snd_replies=1`, `snd_status_addr=5034880=0x004CD380`,
  `snd_flags=1`) and the frontier leaves the ADR 0152/0153/0154 `0x00201FC0`
  poll loop: the run now ends `guest_stopped` as `unhandled_syscall` at
  `pc=0x0025A6C4` after only 4,568,571 instructions in 1 batch
  (`syscalls_seen=1146 handled=1128`, callbacks 78/78,
  `dmac.interrupts_delivered=0`, `timer3.cycles=4568571`). `$v1=0x3E` is the
  ps2sdk `EndOfHeap` service (`0x0025A6C0` `addiu $v1,$zero,0x3E`), reached from
  the newlib `_sbrk` path with `$a0=0xE4D0` (a ~57 KiB allocation) and
  `$v0=0x008A0400` (the heap start `SetupHeap` selected). The 989snd main server
  then issues a second command, fno `77` (`SL_COMMAND_BATCH` in the 989snd
  command table), 16-byte request, which is recorded and refused
  (`rpc_call_server=0x00123456 rpc_call_number=77 rpc_call_replied=0
  snd_number=77 snd_send=16 snd_replied=0`); the loader server stays refused.
  A semantic stop is stronger evidence than the previous budget exhaustion, but
  it is still not a boot stage: no graphics, no IOP, and no sound are modelled.
