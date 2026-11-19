# ADR 0156: Serve the EE `EndOfHeap` syscall (0x3E): extend the recorded heap range and return the new end

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-17.
- **Scope:** a new `hle_end_of_heap` syscall service (`ee_hle.hpp`), the
  `heap_current_end`/`end_of_heap_*` observation fields (`ee_state.hpp`, with
  `hle_setup_heap` now resetting the current end to the range start), the
  `heap` run-report object (`engine/src/main.cpp`), three new smoke cases
  (183 total) and the driver-smoke ABI expectation. Runtime ABI 17 -> 18.

## Context

ADR 0155 moved the Downhill Domination frontier off the 989snd poll loop; the
probe now stops as `unhandled_syscall` at `pc=0x0025A6C4` — the `syscall`
inside the stub `0x0025A6C0 addiu $v1,$zero,0x3E; syscall` — with
`$a0=0xE4D0`, `$v0=0x008A0400`, `$s0=0x008AE8D0`, `$sp=0x0093C3D0`,
`$ra=0x0025AD88`. ps2sdk's `syscallnr.h` assigns service `0x3E` to
`__NR_EndOfHeap`; ps2tek documents `3Eh void* EndOfHeap()`.

- **Observed (target disassembly of the owner's boot ELF `adfda7b7...f0cf0c`;
  runtime VAs):** the caller is the title's newlib `_sbrk` at `0x0025AD30`.
  It loads its cached heap end from the global at `0x0028214C` into `$v0`
  (`0x0025AD7C`), then `jal 0x0025A6C0` with the delay slot
  `addu $s0, $v0, $a0` (`0x0025AD84`), so the proposed new end is
  `cached + $a0` (`0x008A0400 + 0xE4D0 = 0x008AE8D0`, matching `$s0` in the
  stop). After the trap it computes `sltu $v0, $v0, $s0` (`0x0025AD88`) and
  branches `beqz` (`0x0025AD8C`, delay slot reloading the cached end):
  a returned value **>= `cached + $a0`** takes the success path — the cached
  global is stored `proposed` (`0x0025ADBC`) and the old end is returned in
  `$v0` — while anything smaller calls `__errno` (`0x0025AD94` ->
  `0x00254040`, which returns `&0x0028191C`), stores errno 12 (ENOMEM)
  (`0x0025AD9C`/`0x0025ADA4`) and returns -1. So the kernel consumes the
  increment in `$a0` and answers with the **new** end; ps2tek's no-argument
  signature and its "heap base" wording do not match this use.
  Separately, `SetupHeap(0x008A0400, -1)` was already served (ADR 0064 era)
  with the recorded range `[0x008A0400, setup_thread_stack_pointer)`; the
  guest-side cached end starts at `0x008A0400` = the range start.
- **Derived:** `EndOfHeap(incr)` semantics for this title: `$v0` = new heap end
  (`current + incr`) on success, and any value `< current + incr` signals
  failure to the caller (answered here with 0). The observed caller never
  treats `0xFFFFFFFF` as failure, so -1 must not be the modelled failure
  value.
- **Unknown:** the real kernel's failure return value (0 is chosen because the
  observed `_sbrk` treats any result below `cached + incr` as ENOMEM), the
  kernel's alignment/rounding of the increment, and per-thread heap
  inheritance beyond the single boot thread. No allocator exists behind the
  modelled counter and no guest memory is handed out.

## Decision

1. Register `{0x3E, "EndOfHeap", &hle_end_of_heap}` in the syscall table.
2. `hle_setup_heap` additionally resets `state.hle.heap_current_end` to the
   recorded range start.
3. `hle_end_of_heap` computes `proposed = heap_current_end + (u32)$a0`
   (32-bit modular). It fails closed — `$v0 = 0`, no state change — when no
   `SetupHeap` ran, when the addition wraps, or when `proposed` leaves the
   recorded range `[setup_heap_start, setup_heap_end]`. Otherwise it sets
   `heap_current_end = proposed` and returns `proposed` in `$v0`
   (sign-extended), recording the call, increment, result and ok flag.
4. Runtime ABI 17 -> 18 (`EeHleState` gains the heap observation fields); the
   driver smoke expects 18 and the run report gains a `heap` object.

## Consequences

- The title's first `_sbrk(0xE4D0)` now succeeds: `EndOfHeap` returns
  `0x008AE8D0`, `_sbrk` updates its cached end and returns `0x008A0400` as
  the allocated block; allocation continues until a call exceeds the
  recorded range, which fails closed with ENOMEM rather than a fabricated
  pointer.
- No compatibility-state change: no heap memory is modelled as allocated and
  no sound/graphics claim follows.

## Validation

- `reps2_hle_syscall_codegen_smoke`: three new cases (183 total) — the retail
  shape (`SetupHeap(0x008A0400,-1)` then `EndOfHeap(0xE4D0)` -> `$v0` =
  `0x008AE8D0`, accumulation on a second call), fail-closed past the limit /
  below the range start / without `SetupHeap` / on 32-bit wrap, with
  no-partial-mutation assertions. Driver smoke expects runtime ABI 18.
- CMake Release build and `ctest -C Release` (43/43); Rust
  `fmt`/`clippy`/`test`.
- Real-title probe (owner's ISO SHA-256 `0fe2b777...a7eb86`, boot ELF
  `adfda7b7...f0cf0c`, Windows x64/MSVC, runtime ABI 18, frontend and native
  module rebuilt from this branch — `PostADR156/run-report-endofheap.json`):
  `outcome=guest_stopped`, `pc=0x0025432C` (after the ADR 0157 SearchFile reply
  released `WaitSema(8)`; see `docs/RESEARCH.md` for the new frontier),
  `instructions_executed=4577576`, `syscalls_seen=1191 handled=1174`,
  callbacks 78/78, and
  `heap={setup_called:1, start:0x008A0400, limit:0x0093C3D0, current_end:0x008AF000, end_of_heap_calls:2, last_increment:0x730, last_result:0x008AF000, last_ok:1}`
  — both observed `_sbrk` calls (+0xE4D0, +0x730) were served and the
  frontier left the `0x0025A6C4` stop.
