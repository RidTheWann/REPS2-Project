# ADR 0140: Register the multitap port-open RPC server (0x80000901) fail-closed

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE SIF RPC server table (`ee_hle.hpp`, `ee_state.hpp`), one sid:
  0x80000901. Runtime ABI 7 -> 8 (two counters added to `EeSifCmdState`).

## Context

With the loadfile version query answered (ADR 0139), the Downhill Domination
probe leaves the loadfile path and retries a bind for sid `0x80000901`:
`rpc_bind_sid=2147485953=0x80000901`, `rpc_bind_had=0`, then it exhausts the 80M
instruction budget in the retry delay loop at `pc=0x00245B10`.

Evidence for what the sid is:

- **Observed (probe report, runtime ABI 7):** the last SIF RPC bind targets sid
  `0x80000901` and is answered no-server; the retry loop at
  `0x00245B00..0x00245B14` is the same `nopdelay`-style countdown shape as the
  loadfile bind loop.
- **Observed (pinned PS2SDK `ee/rpc/multitap/src/libmtap.c`):** the multitap
  client binds `MTAPSERV_PORT_OPEN 0x80000901` (and `MTAPSERV_PORT_CLOSE`,
  `MTAPSERV_GET_CONNECTION`), then calls it with function 1:
  `sceSifCallRpc(&clientPortOpen, 1, 0, mtapRpcBuffer, 4, mtapRpcBuffer, 8, NULL, NULL)`.
  `0x80000901` is therefore the multitap port-open server, which the resident
  IOP `mtapman` registers, so on real hardware the title's bind succeeds.
- **Unknown:** which multitap function the title issues and what a port-open
  reply would carry. No multitap adapter exists in REPS2's host model.

## Decision

1. Register sid `0x80000901` (`kEeSifServerMtapPortOpen`) in
   `initialize_hle_boot_environment`, so the bind succeeds exactly as on the
   resident IOP. A registered server answers with its own sid as the `sd` token.
2. The call handler `serve_ee_mtap_call` models no function: it records
   `mtap_call_count` / `last_mtap_number` and refuses the call (no reply, no
   RPC_END, no semaphore signal). REPS2 models no multitap hardware, so a
   fabricated "port opened" answer is refused and the caller stays blocked with
   an honest boundary. Same convention as ADRs 0121/0125/0138.
3. `EeSifCmdState` gains the two counters, so `REPS2_RUNTIME_ABI_VERSION` moves
   7 -> 8; modules built against ABI 7 are rejected at admission.
4. The modelled server-table bound `kEeMaxSifRpcServers` moves 8 -> 16: the boot
   environment now installs eight servers (CdInit, CdSCmd, both DiskReady sids,
   NCmd, SearchFile, loadfile, multitap port-open), which left no room for a
   session- or test-registered server at the old bound. The bound stays explicit
   and bounded; a registration beyond it still fails closed by returning false.

## Consequences

- The title's bind for 0x80000901 completes; the next probe shows which
  multitap function it issues, naming the next unit, and whether it continues
  to another server (the client stub also names 0x80000902/0x80000903).

## Validation

- `reps2_hle_syscall_codegen_smoke` gains
  `test_boot_environment_registers_the_mtap_port_open_server`,
  `test_bind_to_the_mtap_server_reaches_a_registered_server` and
  `test_mtap_calls_are_recorded_and_refused` (144 cases): the server is found, a
  bind reaches it and writes the sid as the `sd` token, and a function-1 call is
  recorded with no reply, no RPC_END and an untouched semaphore.
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF `adfda7b7...f0cf0c`,
  ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI 10, module rebuilt with
  this runtime, `D:\REPS2-DevTests\DownhillDomination\PostADR140_142\run-report-abi10.json`):
  the `0x80000901` bind no longer appears as the last bind — the title moved past
  it — and the run instead retries `rpc_bind_sid=2147485954=0x80000902`
  (`MTAPSERV_PORT_CLOSE`, ps2sdk `libmtap.c`), `had=0`, exhausting the 80M budget
  (79,999,980 instructions, 16 batches) in the retry loop at `pc=0x00245B68`.
  `timer3.cycles=79999980` shows the new deterministic guest-time source
  advancing. Budget exhaustion is a weaker evidence class than a semantic stop;
  not a boot stage. The next unit is registering `0x80000902` (and likely
  `0x80000903`).
