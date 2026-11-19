# ADR 0136: Run SIF RPC end functions as guest callbacks

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE SIF RPC reply completion, generated dispatcher, runtime ABI,
  engine run report.

## Context

The runtime-ABI-4 Downhill Domination probe stops as `semaphore_wait_blocked`
at `0x0025A724` (`WaitSema`, service `0x44`, `$a0 = 7`) after 3,890,370
instructions. The report shows the NCmd bind completed, the first
`CD_NCMD_READ` (LBN 16, one 2048-byte sector) was served from the host media
source, and the call reply was delivered through RPC_END (`ncmd_read_replied=1`,
`rpc_call_replied=1`). Semaphore 7 is libcdvd's `nCmdSemaId`; nothing in the
current model raises it.

PS2SDK `ee/kernel/src/sifrpc.c` `_request_end` explains why:

```c
if (request->cid == SIF_CMD_RPC_CALL) {
    if (cd->end_function)
        cd->end_function(cd->end_param);
} else if (request->cid == SIF_CMD_RPC_BIND) { ... }

if (cd->hdr.sema_id >= 0)
    iSignalSema(cd->hdr.sema_id);
```

`SifRpcClientData_t.end_function` is at client offset `0x1C` and `end_param` at
`0x20` (`common/include/sifrpc-common.h`). `sceSifCallRpc` stores both and, for
`SIF_RPC_M_NOWAIT`, leaves `sema_id` at `-1`, so the kernel signals no semaphore
of its own: the end function is the only producer. `sceCdRead`
(`ee/rpc/cdvd/src/ncmd.c`) passes `_CdAlignReadBuffer` as the end function and
`_rd_intr_data` as `end_param`; `_CdAlignReadBuffer` runs its aligned-buffer
copy and then `_CdGenericCallbackFunction(&CdCallbackNum)`, which raises
`nCmdSemaId`.

REPS2 therefore models the reply but not the callback, so the guest blocks on a
semaphore whose only producer is translated guest code the dispatcher never
ran. This is a waiting-on-an-unmodelled-producer boundary, not an unimplemented
service (same class as ADR 0119).

## Decision

1. `process_ee_sif_rpc_end` (the EE-side `_request_end` model) reads
   `end_function`/`end_param` from the client object for an
   `SIF_CMD_RPC_CALL` reply and queues the callback with
   `request_ee_guest_callback`. The existing semaphore raise and packet free
   behaviour is unchanged.
2. The callback runs as translated guest code through the generated
   dispatcher. `execute_cfg` checks for a queued callback at the top of its
   loop and calls `enter_ee_guest_callback`, which:
   - saves the full architectural context in the existing syscall-handler frame
     stack (the same stack, with `is_guest_callback` set),
   - passes `end_param` in `$a0` and the kseg3 sentinel in `$ra`,
   - sets `PC` to the published callback address.
   The callback returns through `jr $ra` to the sentinel; the dispatcher
   restores the saved context (including `$v0`) and resumes at the interrupted
   guest PC. Unlike a syscall handler, the callback's result is not the
   caller's service result.
3. Callback admission is bounded and fail-closed. An address that is zero or
   not word-aligned, the dispatcher sentinel itself, `0xFFFFFFFF`, a full
   queue (8 entries) or a full frame stack refuses the callback, records the
   refusal, and does not execute anything. A refused callback leaves the guest
   blocked; it never receives a fabricated producer.
4. The queue is FIFO so replies complete in order, and a queue entry is
   consumed when it is entered (not when it returns), so a resumable budget
   stop inside a callback resumes the callback rather than re-entering it.
5. `EeHleState` gains the queue, frame kind, and observation counters, so
   `REPS2_RUNTIME_ABI_VERSION` moves 4 -> 5. The engine run report gains
   `callbacks.{requested,started,completed,refused,last_address,last_parameter}`.
6. The dispatcher hook is emitted by the Rust frontend; a module built against
   ABI 4 is rejected at admission, so no module can silently skip the hook.

## Consequences

- The real title's `WaitSema(7)` can be released by the actual guest end
  function, provided that address is translated. If it is not, the probe stops
  at the callback address as an honest control-flow boundary and the next
  increment is root discovery for that published pointer.
- The callback runs synchronously, before the interrupted caller continues,
  because REPS2 delivers SIF replies synchronously with no interrupt
  controller. On hardware it runs in the SIF0 interrupt handler; the queued
  model preserves ordering and context isolation but not interrupt timing.
- The callback runs on the current guest stack, the same boundary ADR 0069
  records for guest syscall handlers. No scheduler, privilege ring or
  interrupt preemption is implied.
- The aligned-buffer staging copy inside `_CdAlignReadBuffer` reads
  `_rd_intr_data`, which the NCmd read model leaves untouched (ADR 0135). A
  zeroed buffer makes both copies no-ops; if a title depends on staged bytes,
  the next probe stop shows it.

## Validation

- `reps2_hle_syscall_codegen_smoke` gains two cases (135 total):
  `test_rpc_call_end_function_runs_guest_callback` (M_NOWAIT call with
  end function `0xE800` and `end_param = 7`; the callback signals semaphore 7
  through host `SignalSema`, the saved `$v1`/`$a0` and the caller PC are
  restored, and execution resumes at the untranslated continuation) and
  `test_rpc_call_end_function_refuses_an_unusable_address` (misaligned address:
  reply delivered, no callback queued, semaphore unchanged).
- `control_codegen::tests::dispatcher_enters_pending_guest_callbacks` pins the
  emitted hook; the fixture adds block `0xE800`.
  `analysis::tests::extra_roots_become_authoritative_entry_points` pins the
  `--extra-root` behaviour (translated block, `extra-root` label, misaligned
  root rejected) and `parses_extra_roots_and_rejects_misaligned` pins the CLI.
- Rust `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets
  -- -D warnings`, `cargo test --workspace --locked` (377 passed); CMake Release
  build and `ctest -C Release` (41/41) pass locally on Windows x64/MSVC.
- Real-title probes (Downhill Domination USA, boot ELF `adfda7b7…f0cf0c`, ISO
  `0fe2b777…a7eb86`, Windows x64/MSVC 19.51.36260.0, runtime ABI 5, branch
  `feat/engine-guest-rpc-end-callback`, artifacts under
  `D:\REPS2-DevTests\DownhillDomination\PostADR136*`):
  - without the extra root, the dispatcher enters the queued callback at
    `0x00246CD0` and stops as `unresolved_control_flow` there after 3,890,166
    instructions (`callbacks={requested:1,started:1,completed:0}`);
  - with `--extra-root 0x00246CD0`, the run executes 4,409,370 instructions,
    completes 78/78 callbacks, serves 78 `CD_NCMD_READ` calls from the ISO
    (last LBN 337), passes the old `WaitSema(7)` block, and stops as
    `unhandled_syscall` `0x7C` at `0x0025AB14` — the next frontier.
  Neither result is a guest-visible boot stage. Details in
  `docs/PROJECT_STATE.md` and `docs/RESEARCH.md`.