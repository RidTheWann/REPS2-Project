# ADR 0157: Parse the retail cdvd SearchFile packet layout (300-byte request, name at offset 36)

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-17.
- **Scope:** `serve_ee_cdvd_searchfile_call` in `runtime/include/reps2/runtime/ee_hle.hpp`
  selects the name field offset from the observed send size (>= 300 -> 36, else
  ps2sdk's 32) and the `sceCdlFILE` destination pointer likewise (292 vs 288);
  new diagnostic fields `last_searchfile_name[64]` and `last_searchfile_head[16]`
  in `EeSifCmdState` (`ee_state.hpp`, ABI 18 on this branch) recorded even for
  refused calls, printed sanitized in the run report (`engine/src/main.cpp`).

## Context

ADR 0156 served EE syscall `EndOfHeap` (0x3E); the fresh probe
(`PostADR156/run-report-endofheap.json`) then stopped as
`semaphore_wait_blocked` at `pc=0x0025A724` — the `WaitSema` stub `0x44` with
`$a0=8`, `$ra=0x0025D138` — because the cdvd SearchFile RPC
(`0x80000597`, fno 0) was refused: `searchfile_calls=1, send=300, found=0,
replied=0`. The handler read the path at ps2sdk's offset 32 and saw an empty
string.

- **Observed:** the title sends a **300-byte** SearchFile request. The captured
  packet head (run report `searchfile_head`) shows bytes 0..35 zero and the
  path `\SKAT\DHSKAT.SKX;1` beginning at **offset 36**; the destination pointer
  follows the 256-byte name at offset 292. ps2sdk's `SearchFilePkt`
  (`ee/rpc/cdvd/src/libcdvd.c` / `internal.h`) is 292 bytes:
  `{u8 padding[32]; char name[256]; void* dest;}` with the name at 32 and the
  pointer at 288 — so the retail packet is the ps2sdk layout plus 4 extra
  prefix bytes.
- **Derived:** the name offset is `send_size >= 300 ? 36 : 32`, and the
  destination-pointer offset is `send_size >= 296 ? 292 : 288` (already
  present). Both are selected from the observed send size, not invented.
- **Unknown:** why the retail cdvdfsv request carries 4 extra leading bytes
  (likely a version/flags word the retail IOP module expects; the observed
  bytes are zero); whether other cdvd RPC packets of this title have shifted
  layouts too.

## Decision

1. Select the name-field offset from the observed send size as above; keep the
   fail-closed behavior (empty path, unwritable structure -> no reply, the
   caller's own wait stays blocked and the run stops with evidence).
2. Record `last_searchfile_name` (truncated path) and `last_searchfile_head`
   (first 64 payload bytes) even for refused calls, and print them sanitized
   in the run report, so a layout mismatch is diagnosable from the report.
3. The found path is resolved against the host media's own ISO 9660 metadata
   exactly as ADR 0141 already does; the 32-byte `sceCdlFILE`
   (`{u32 lsn; u32 size; char name[16]; u8 date[8];}`, ps2sdk
   `libcdvd-common.h`) is written to the caller's structure before the 4-byte
   result word.

## Consequences

- The title's `sceCdSearchFile("\\SKAT\\DHSKAT.SKX;1")` now answers
  `found=1, lsn=0x000AC0CA, size=0x06096AD0` and the caller's `WaitSema(8)` is
  released; the frontier moves to a new stop (recorded in `docs/RESEARCH.md`:
  `unsupported_instruction` at `pc=0x0025432C`).
- No boot/playable claim: this serves one more RPC reply, not a boot stage.

## Validation

- `reps2_hle_syscall_codegen_smoke`: 183 cases pass (unchanged by this ADR;
  the SearchFile path is exercised end-to-end by the real-title probe).
- Real-title probe (owner's ISO SHA-256 `0fe2b777...a7eb86`, boot ELF
  `adfda7b7...f0cf0c`, Windows x64/MSVC, runtime ABI 18, frontend and native
  module rebuilt from this branch — `PostADR156/run-report-endofheap.json`):
  `searchfile_calls=1, send=300, name="\\SKAT\\DHSKAT.SKX;1", found=1,
  lsn=705194, size=101288144, replied=1`; outcome `guest_stopped`
  (`unsupported_instruction`) at `pc=0x0025432C` after 4,577,576 instructions.
- CMake Release build and `ctest -C Release` (43/43); Rust
  `fmt`/`clippy`/`test`.
