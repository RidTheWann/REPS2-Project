# ADR 0104: IOP SIF RPC bind reply with no modelled IOP server

- **Status:** Accepted
- **Date:** 2026-11-09
- **Scope:** shared C++20 runtime (`runtime/include/reps2/runtime/ee_hle.hpp`, `ee_state.hpp`), HLE SIF command dispatch, generated probe report
- **Depends on:** ADR 0098 (IOP-side SIFCMD command model), ADR 0100 (IOP-to-EE SIF0 RPCINIT handshake), ADR 0101 (EE semaphore services and the honest `WaitSema` block)

## Context

The M4 frontier is a `WaitSema` inside `sceSifBindRpc`. The probe on this
revision stopped there (`status=16`, `pc=0x0025A724`, semaphore id 3 with
`init_count = 0`, `max_count = 1`, `count = 0`) after the EE had already sent the
`SIF_CMD_RPC_BIND` packet: the last observed `SifSetDma` carried a 64-byte
`SIF_CMD_RPC_BIND` (`sif_cmd_cid=0x80000009`). No IOP side answered it, so the
wait could not be released without inventing a completion.

This ADR models the reply a resident IOP SIFRPC module sends when it holds no
matching server, which is the truthful state of an IOP on which REPS2 runs no
module.

## Evidence

- **Observed (PS2SDK `ee/kernel/src/sifrpc.c`, `sceSifBindRpc`):** the client
  allocates a packet, clears `cd->server`, stores `sid`, `pkt_addr` and `cd` in
  the request, creates a semaphore with `max_count = 1` and `init_count = 0`,
  sends `SIF_CMD_RPC_BIND` with `RPC_PACKET_SIZE` (64) bytes, `WaitSema`s,
  `DeleteSema`s, and returns 0. The caller decides success by testing
  `cd->server`, not the return value.
- **Observed (same file, `_request_end`, the `SIF_CMD_RPC_END` handler):** reads
  `request->cd`, copies `request->sd`/`buf`/`cbuf` into the client object for a
  bind, then `iSignalSema(cd->hdr.sema_id)` when the id is non-negative, frees the
  request packet (`rpc_id = 0`, `rec_id &= ~PACKET_F_ALLOC`), and clears
  `cd->hdr.pkt_addr`.
- **Observed (PS2SDK `iop/system/sifcmd/src/sifrpc.c`, `_request_bind`):** looks
  the sid up with `_search_svdata`; with no active queue the result is NULL, and
  the handler **still sends** `SIF_CMD_RPC_END` with `sd = NULL`,
  `buf = NULL` for the request's `cd`, `pkt_addr` and `cid`.
- **Observed (PS2SDK `common/include/sifrpc-common.h`):** `SifRpcBindPkt_t` has
  `cd` at 0x1C and `sid` at 0x20; `SifRpcRendPkt_t` has `cd` at 0x1C, the
  originating `cid` at 0x20, `sd` at 0x24, `buf` at 0x28, `cbuf` at 0x2C;
  `SifRpcClientData_t` has `pkt_addr` at 0x00, `sema_id` at 0x08, `buf` at 0x14,
  `cbuf` at 0x18 and `server` at 0x24. `PACKET_F_ALLOC` is 0x01.
- **Observed (real-title probe on this revision, after the change):**
  `sif_rpc_bind=1 sif_rpc_bind_sid=0x80000592 sif_rpc_bind_sema=3
  sif_rpc_bind_signalled=1`, the `WaitSema` no longer blocks, the following
  `DeleteSema(3)` is serviced, and the title continues to
  `budget_exhausted` at `pc=0x002478F0` with three later bind replies.
- **Inferred (high confidence):** on real hardware this title talks to an IOP
  module that registers the server for sid `0x80000592`; REPS2 has no such
  module, so a failed bind is the correct answer here and the title's own error
  path is what executes next.
- **Observed (target disassembly, owner-supplied boot ELF):** the `budget_exhausted`
  PC `0x002478F0` is a ~65,536-iteration delay loop inside a retry; `0x00247828`
  calls the `sceSifBindRpc`-shaped wrapper at `0x0025CDB8` with
  `sid = 0x80000592`, then tests `cd->server` and loops back when it is NULL.
  The title therefore waits for that module and cannot progress on budget alone.
- **Unknown:** the module behind sid `0x80000592`, and its reply payloads; the
  EE kernel's own DMAC/interrupt bookkeeping (`sceSifSetDma` is a syscall into
  rom0, so its internals are not public).

## Decision

1. `process_sif_command` treats an outgoing `SIF_CMD_RPC_BIND` as a request to
   the resident IOP SIFRPC module and answers it with `SIF_CMD_RPC_END` carrying
   `sd = buf = cbuf = NULL`, echoing the request's `cd`, `pkt_addr` and `rpc_id`
   and setting the reply's originating `cid` to `SIF_CMD_RPC_BIND`.
2. The reply is written into the EE receive buffer the guest published with
   `SIF_CMD_INIT_CMD`, because REPS2 models no IOP memory; the SIF0 completion
   event is published to `D_STAT` so device state matches an inbound transfer.
   With no published or writable buffer nothing is delivered.
3. The EE-side receive dispatch handles `SIF_CMD_RPC_END` as `_request_end`
   does: it writes `sd`/`buf`/`cbuf` into the client object for a bind, raises
   the client semaphore through the shared
   `raise_ee_sema_count` rule, and frees the request packet.
4. `raise_ee_sema_count` is extracted from `hle_signal_sema` so a modelled
   kernel event and the `SignalSema` syscall share one raise rule: an id with no
   live object or a semaphore already at `max_count` is not raised.
5. Observation fields (`rpc_bind_replies`, `last_rpc_bind_sid`,
   `last_rpc_bind_cd`, `last_rpc_bind_sema_id`, `last_rpc_bind_sema_signalled`)
   are exposed in the probe report as `sif_rpc_bind*`.

## Alternatives considered

- **Signal the semaphore on DMA completion without a reply packet.** Rejected:
  the EE kernel signals from `_request_end` after reading a real reply; raising
  the count without one is fabrication, and it was the reason the earlier probe
  stopped honestly instead.
- **Model an IOP server that reports a successful bind.** Rejected: a successful
  bind requires an `sd` pointing at SIFRPC server state (buffer, function
  vectors) that REPS2 does not have, and the title would then issue
  `SIF_CMD_RPC_CALL`s whose replies we could not produce. A no-server reply is
  the honest answer for an empty IOP.
- **Leave the wait blocked until an IOP subsystem exists.** Rejected as the only
  option: the documented no-server reply is available now, is what this IOP
  state actually implies, and it exercises real guest error handling instead of
  a stop.
- **Deliver the reply through a SIF0 DMAC interrupt and the guest kernel
  handler.** Deferred: the EE DMAC has its own COP0 interrupt line, it is not an
  INTC source, and no EE interrupt delivery exists yet (see ADR 0103 and the
  DMAC research note). The established HLE receive dispatch stands in, as for
  `SET_SREG`.

## Consequences

- A bind to a sid with no modelled module completes with the documented failure:
  `cd->server` is left NULL, the caller's semaphore is signalled, the request
  packet is freed, and the caller continues on its real error path.
- The title no longer stops on an unanswered bind; it now issues further binds
  and runs to the instruction budget, which is a measurable advance in
  guest-visible execution.
- Because binds now fail, the runtime cannot yet reach a state where the title
  successfully talks to an IOP service. That is stated, not hidden.

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked`.
- CMake Release build and `ctest -C Release` — 34/34.
- `reps2_hle_syscall_codegen_smoke` — 96 cases, including three new ones: a full
  bind round trip (reply fields, client update, semaphore raise, packet free, and
  the subsequent `WaitSema` consuming the count), fail-closed behaviour without a
  published EE receive buffer, and replies addressed to an unknown semaphore id
  or a null client.
- Real-title probe: `status=3 stage=budget_exhausted pc=0x002478F0
  executed=4999994` at a 5M budget and `executed=19999999` at 20M, with three
  bind replies observed. Before this change the same title stopped at
  `status=16 stage=semaphore_wait_blocked pc=0x0025A724 executed=3169504`.

## Limitations and next evidence

- No IOP module is modelled, so no bind can succeed and `SIF_CMD_RPC_CALL`
  (0x8000000A) is not answered.
- The reply writes the 48-byte `SifRpcRendPkt_t` prefix; the trailing 16 bytes of
  the 64-byte slot are IOP packet-slot scratch and are left untouched rather than
  invented.
- No EE interrupt is delivered: the EE kernel's DMAC handler chain does not run,
  and the HLE receive dispatch performs the `_request_end` effects instead.
- Bounded execution now stops on `budget_exhausted` rather than a semantic
  boundary. That is a weaker evidence class than a specific stop and must not be
  read as a boot stage: no title boot, menu, or Playable claim is made.