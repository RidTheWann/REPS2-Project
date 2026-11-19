# ADR 0145: Serve the multitap module-version query (0x80000905, function 1)

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE SIF RPC server table (`ee_hle.hpp`) handler for sid 0x80000905;
  `EeSifCmdState` observation fields (`ee_state.hpp`). Runtime ABI 10 -> 11.

## Context

With all five multitap binds registered (ADRs 0140/0143/0144), the Downhill
Domination probe's frontier is one call: `rpc_call_number=1` on sid `0x80000905`,
recorded and refused (`mtap_calls=1 mtap_number=1 rpc_call_replied=0`), which
leaves the caller blocked as `guest_stopped` at `pc=0x0025A724` after 4,413,234
instructions. Serving that call is the next unit.

Evidence for what the call is and what it must answer:

- **Observed (owner's title boot ELF, SHA-256 `adfda7b7...f0cf0c`):** the title's
  `sceMtapInit` binds `0x80000901`..`0x80000905` in sequence (each with a
  `nopdelay` retry), then calls a helper at `0x00245E10` that issues
  `sceSifCallRpc(client=0x76F0E0, fno=1, rmode=0, send=0x76F140, 0x80,
  recv=0x76F140, 0x80, 0, 0)` and returns reply word 0 (`[0x76F140]`). The block
  at `0x00245CC0` then requires `(reply_word0 >> 8) == 3`: a mismatch prints a
  message and returns 0, a match returns 1. The 128-byte (`0x80`) send/receive
  size matches `sizeof(rpc_buffer)` in Sony's `libmtap`.
- **Observed (public Sony `libmtap` decode, Digimon World 4 project):**
  `MTAP_GET_MOD_VERSION 0x80000905`, `MTAP_CHANGE_THREAD_PRIORITY 0x80000904`;
  `sceMtapGetModVersion` issues function 1 and returns `rpc_buffer[0]`; the
  library's `sceMtapInit` requires `(version >> 8) == LIBMTAP_VERSION_MAJOR` with
  `LIBMTAP_VERSION_MAJOR 3` and `LIBMTAP_VERSION_MINOR 0`, printing
  `mtapman.irx = <major>.<minor>` on a mismatch. The title's observed bind
  sequence and check match this library verbatim.
- **Corroborated (independent Downhill Domination port):** its multitap HLE
  answers this query with `0x0300` ("mtapman 3.0: the version the library
  checks"). A PS2SDK `mtapman` replacement further notes the retail `MTAPMAN.IRX`
  reports module version `0x03,0x10` (major 3, minor 16) for SDK 3.0.3/3.1.0;
  the title's check constrains only the major byte.

## Decision

1. Register sid `0x80000905` with a dedicated handler
   `serve_ee_mtap_version_call`. Function 1 (`MTAP_GET_MOD_VERSION`) with a
   writable reply buffer of at least 4 bytes writes the 4-byte version word
   `0x0300` (Sony `libmtap`'s own `3.0`) into reply word 0 and answers
   (`SIF_CMD_RPC_END`, caller semaphore raised), so `sceMtapInit`'s
   `(version >> 8) == 3` check passes exactly as it does against the resident
   module. Any other function number, or no usable reply buffer, is recorded and
   refused (no reply, no RPC_END). REPS2 still models no multitap adapter and no
   `MTAPMAN` module: the handler serves only the version check the SDK client
   performs and fabricates no controller or port state.
2. `EeSifCmdState` gains `mtap_version_calls`, `mtap_version_replies`,
   `last_mtap_version` and `last_mtap_version_replied`, so
   `REPS2_RUNTIME_ABI_VERSION` moves 10 -> 11; modules built against ABI 10 are
   rejected at admission.

## Consequences

- All five multitap binds complete and `sceMtapInit` returns success, so the title
  leaves its multitap initialization. The probe's frontier moves on to the next
  multitap call (`sceMtapPortOpen`, function 1 on `0x80000901`), which is still
  refused: serving the port/connection bodies is a separate unit.
- The served `0x0300` is the `libmtap` library version, not an attested retail
  `MTAPMAN.IRX` build string. The retail module's exact minor is **unknown**, and
  the title's check constrains only the major byte.

## Validation

- `reps2_hle_syscall_codegen_smoke` adds two cases (150 total): a function-1 call
  on `0x80000905` records `mtap_version_calls=1 mtap_version_replies=1
  last_mtap_version=0x0300`, writes `0x0300` into the reply buffer, signals the
  caller's semaphore via `SIF_CMD_RPC_END`, and `(version >> 8) == 3`; a
  function-2 call is refused (no reply word written, no RPC_END, counters
  unchanged).
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF `adfda7b7...f0cf0c`,
  ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI 11, the generated
  sources unchanged and the module rebuilt from these headers,
  `D:\REPS2-DevTests\DownhillDomination\PostADR140_142\run-report-mtap905.json`):
  all five binds succeed (`rpc_bind_server=0x80000905`), the version query is
  served (`mtap_version_calls=1 mtap_version_replies=1 mtap_version=768
  mtap_version_replied=1`, i.e. `0x0300`), and `sceMtapInit`'s `(version >> 8) == 3`
  check passes. The title then issues its next multitap call —
  `rpc_call_server=2147485953=0x80000901`, `rpc_call_number=1` (`sceMtapPortOpen`)
  — which is refused, so the run stops as `guest_stopped` at `pc=0x0025A724`
  after 4,413,559 instructions (`mtap_calls=2`, `syscalls_seen=1038
  handled=1020`, `callbacks={requested:78, completed:78}`,
  `timer3.cycles=4413559`). Not a boot stage.
