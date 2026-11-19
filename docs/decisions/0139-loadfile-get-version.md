# ADR 0139: Serve the loadfile version query (`LF_F_GET_VERSION`, 0xFF)

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** SIF loadfile server handler (`ee_hle.hpp`, `ee_state.hpp`), one
  function: 0xFF. Runtime ABI 6 -> 7 (three observation fields added to
  `EeSifCmdState`).

## Context

With the loadfile server registered (ADR 0138), the Downhill Domination probe
reaches the server and issues exactly one call: `rpc_call_number = 255`
(`LF_F_GET_VERSION`, PS2SDK `common/include/loadfile-common.h`). The ADR 0138
handler refused it, so the caller blocked and the run stopped as
`guest_stopped` at `pc=0x0025A724` after 4,410,459 instructions.

Evidence for the answer shape:

- **Observed (pinned PS2SDK `iop/system/loadfile/src/loadfile.c`):** the RPC
  dispatcher routes `LF_F_GET_VERSION` to `loadfile_get_version`, which writes
  one `u32` into the caller's output buffer — `((u32 *)outbuffer)[0] = 0x30303133`
  — and returns it, so the client receives a 4-byte reply.
- **Observed (pinned PS2SDK `ee/kernel/src/loadfile.c`):** the client stub sends
  `<fno>` and receives the handler's reply through its own argument buffer.
- **Unknown:** the retail IOP `loadfile` module's own version word. `0x30303133`
  is the PS2SDK reference module's version ("3100"), not established to be the
  retail module's; no available evidence names the retail value.

## Decision

1. Serve `LF_F_GET_VERSION` (0xFF) on the loadfile server: validate that the
   caller supplied a reply buffer of at least 4 bytes, write the PS2SDK
   reference's version word `kEeLoadfileVersionWord = 0x30303133`, record
   `loadfile_replies` / `last_loadfile_replied`, `last_loadfile_send_size` and
   `last_loadfile_recv_size`, and answer, which releases the caller's
   semaphore through the normal RPC_END path.
2. Every other loadfile function is still recorded and refused: REPS2 executes
   no IOP module, so it cannot report that a module was loaded. The observed
   call sizes are recorded so the next probe names the next unit.
3. The version word is the PS2SDK reference's and is documented as such: it is
   a behavioural cross-check, not an established retail value. The ADR and the
   compatibility matrix keep the retail module's version in the **Unknown**
   column. If the title is later seen to compare the value against an expected
   range, this choice must be revisited with real-title evidence.
4. `EeSifCmdState` gains the three observation fields, so
   `REPS2_RUNTIME_ABI_VERSION` moves 6 -> 7.

## Consequences

- The title's version probe completes; the next probe shows whether it proceeds
  to a module load (`LF_F_MOD_LOAD`, 0) — which stays fail-closed — or to
  another boundary.

## Validation

- `reps2_hle_syscall_codegen_smoke` gains
  `test_sif_loadfile_get_version_answers_the_reference_word` and
  `test_sif_loadfile_get_version_refuses_without_a_reply_buffer`; the existing
  refusal test now also asserts that no reply is recorded (141 cases).
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF `adfda7b7...f0cf0c`,
  ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI 7, module rebuilt with
  this runtime, `D:\REPS2-DevTests\DownhillDomination\PostADR139\run-report-abi7-loadfile.json`):
  the version query is answered (`loadfile_calls=1 loadfile_replies=1
  loadfile_number=255 loadfile_send=0 loadfile_recv=4 loadfile_replied=1
  rpc_call_replied=1`), the caller is released, and the title leaves the
  loadfile path without ever issuing a module load. It then binds another
  server, `rpc_bind_sid=2147485953=0x80000901` (`had=0`), and exhausts the 80M
  budget (79,999,979 instructions, 16 batches, `syscalls_seen=1671 handled=1654`)
  in the bind retry delay loop at `pc=0x00245B10`. `0x80000901` is
  `MTAPSERV_PORT_OPEN` in ps2sdk `ee/rpc/multitap/src/libmtap.c` (the multitap
  port-open server) — the next unit. Budget exhaustion is a weaker evidence
  class than a semantic stop; not a boot stage.
