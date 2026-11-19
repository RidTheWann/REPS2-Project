# ADR 0163: Observe the IOP FILEIO init handshake (fno 255) and keep it fail-closed

- **Status:** Superseded by ADR 0167 (the fno 255 handshake is now served) and
  ADR 0168 (the file functions `fno 0..3` are modelled). The observation is kept
  as history; its static-dataflow claim and its quoted request head are
  **superseded by ADR 0165**.
- **Date:** 2026-11-18.
- **Scope:** `serve_ee_fileio_call` in `runtime/include/reps2/runtime/ee_hle.hpp`
  records a bounded request observation and still refuses every function; new
  report-only fields in `EeSifCmdState` (`ee_state.hpp`, ABI 18 on this branch)
  and their serialization in `engine/src/main.cpp`.

## Context

ADR 0160 registered the IOP FILEIO server (`sid 0x80000001`) so the bind in the
title's `sceFileioInit` (`0x0025D790`) succeeds. With the bind satisfied the
frontier moved: the next stop is `semaphore_wait_blocked` at `pc=0x0025A724`,
the `WaitSema` stub (`0x0025A720 addiu $v1,$zero,0x44; syscall`) with
`$a0=11`, `$ra=0x0025D138`.

- **Observed:** the caller is the title's own `sceSifCallRpc` helper spanning
  `0x0025CF88..0x0025D178`. It is reached from `0x0025D900` (`jal
  0x0025CF88`) inside `sceFileioInit`, immediately after the bind helper call
  at `0x0025D84C` (`jal 0x0025CDB8`). That helper creates a semaphore, sends the
  call, waits on it, and deletes it, so the `WaitSema(11)` waits for the
  `SIF_CMD_RPC_END` of the call issued at `0x0025D900`.
- **Observed (run report, runtime ABI 18, `PostMerge_e3b7447/run-report5.json`):**
  the call targets server `0x80000001` with `rpc_number=255`, `send=8`,
  `recv=8`, `replied=0`. The bounded request observation gives
  `fileio_send_addr=0x00763540`, `fileio_recv_addr=0x007641C0`, and
  `fileio_request_head=[0x00764200, 0x00764640, 0, 0]`; `fileio_name_present=0`
  (`0x00764200`/`0x00764640` are zeroed BSS work buffers at the call, so no path
  is present). `fileio_calls=1`. The run stops `guest_stopped`
  (`semaphore_wait_blocked`) after 4,586,235 instructions in 1 batch
  (`syscalls_seen=1221 handled=1203`, callbacks 78/78).
- **Observed (static disassembly of the same boot ELF, words read from
  `boot.ram.bin` where file offset equals guest virtual address):** after the
  call returns (`bgez $v0` at `0x0025D908`) the success path (`0x0025D91C..
  0x0025D970`) stores `1` to `0x002821EC` and derives a flag by comparing a
  reply word against `2` (`xori $v1,$v1,2` / `sltiu $v1,$v1,1`) before storing
  it to `0x002821F0`.
  *Corrected by ADR 0165:* the compared operand is reply **word 1** at
  `0x007641C4` (uncached alias `0x207641C4`), and `0x00764CA8` is the
  destination of the `swl`/`swr` pair that copies reply **word 0** out of
  `0x007641C0`. The original wording of this bullet had the direction reversed.
  The failure path (`0x0025D910..0x0025D918`) returns `0xFFFEFFFF` (`-2`).
- **Derived:** `ps2sdk` `fileio.c` uses function numbers `0..16` (`FIO_F_OPEN`
  = 0), so `255` is **not** a `ps2sdk` FILEIO request. It is the title's own
  (Sony `libfileio`) initialisation/handshake command issued by
  `sceFileioInit` right after the bind. Its 8-byte request carries two pointers
  to the library's zeroed BSS work buffers, not a pathname, and the post-call
  code consumes a reply word (compared to `2`).
- **Unknown:** the reply format of this handshake (the word compared to `2`, the
  meaning of the two work buffers, and whether the resident IOP module returns a
  version or a status). Because none of this is established, answering it would
  invent device behavior.

## Decision

1. Keep the FILEIO handler fail-closed: every function, including 255, is
   recorded and refused with no reply payload, no `SIF_CMD_RPC_END` and no
   semaphore signal, so the caller stays blocked at its own `WaitSema` rather
   than receiving a fabricated file result. This is the existing ADR 0160
   behavior, now explained.
2. Record a bounded request observation even for refused calls: function number,
   request/reply sizes, source/destination addresses, the first 16 request bytes,
   and any printable, NUL-terminated ASCII pathname reachable directly or one
   pointer-hop through the first two request words. This is report-only and has
   no guest-visible effect; it lets a future FILEIO function that does carry a
   path (an actual open) be identified from the run report instead of requiring
   another diagnostic rebuild.
3. The observation is reset at the start of every FILEIO call so a stale path is
   never reported for a call that carries none.

## Consequences

- The frontier is now documented and reproducible: the title hard-blocks at
  `WaitSema(11)` (`pc=0x0025A724`) waiting for a FILEIO init handshake (fno 255)
  whose reply format is unknown. This is a fail-closed boundary, not a boot
  stage.
- The next FILEIO slice can serve the handshake only once an independent source
  establishes its reply; until then serving it would be fabrication. No
  boot/playable claim is made.

## Validation

- `reps2_hle_syscall_codegen_smoke`: 186 cases pass, including the new
  `test_fileio_init_call_is_observed_and_refused` (records the request head,
  resolves a pathname only when one is present, reports none for two zeroed
  pointers, and never replies).
- Real-title probe (owner's ISO SHA-256 `0fe2b777...a7eb86`, boot ELF
  `adfda7b7...f0cf0c`, Windows x64/MSVC, runtime ABI 18, frontend and native
  module rebuilt from this branch — `PostMerge_e3b7447/run-report5.json`):
  `fileio_calls=1, fno=255, send=8, recv=8, send_addr=0x00763540,
  recv_addr=0x007641C0, head=[0x00764200,0x00764640,0,0], name_present=0,
  replied=0`; outcome `guest_stopped` (`semaphore_wait_blocked`) at
  `pc=0x0025A724` after 4,586,235 instructions.
- CMake Release build and `ctest -C Release`; Rust `fmt`/`clippy`/`test`.