# ADR 0172: EE Timer 2 model and INTC handler delivery

- **Status:** Accepted (validated locally; see Validation).
- **Date:** 2026-11-20.
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (Timer 2 registers and
  progression, `EeIntcHandler`, the `intc_*` observation fields,
  `EeSyscallHandlerFrame::is_intc_interrupt`, Timer 2/INTC entries in
  `EeMemory::kMmioDevices`), `runtime/include/reps2/runtime/ee_hle.hpp`
  (`hle_add_intc_handler` 0x10, `hle_remove_intc_handler` 0x11,
  `hle_enable_intc` 0x14, `hle_disable_intc` 0x15,
  `select_ee_intc_interrupt_cause`, `ee_intc_line_asserted`,
  `enter_ee_intc_interrupt`, the return path in
  `return_from_ee_syscall_handler`), `rust/frontend/src/control_codegen.rs`
  (the dispatcher hook), `engine/include/rg_reps2_engine/session.hpp` (guest-time
  advance), `engine/src/main.cpp` (`timer2` and `intc` run-report sections), and
  the new tests `tests/intc_timer2_smoke.cpp` and
  `tests/intc_timer2_codegen_smoke.cpp`.
- **Related:** ADR 0166 (the Black `AddIntcHandler` frontier), ADR 0171 (ABI 21
  covers this layout change), ADR 0142 (Timer 3 progression), ADR 0103 (INTC
  register model), ADR 0153 (DMAC interrupt delivery — the pattern this follows),
  ADR 0115 (DMAC enable/disable).

## Context

The Black (USA) frontier stops at `AddIntcHandler` (syscall `0x10`): at
`0x00367554` the guest passes cause `0xB` (INTC_TIM2), handler `0x0036DFF0`,
`next = 0` and `arg = 0`, and then checks the returned id with `bgezl $3`
(ADR 0166). REPS2 returned the documented failure value because no INTC handler
registry existed. Two things are therefore missing before the title can progress
past that call and before the Timer 2 polling loop behind it can be explained:

1. An EE Timer 2 register model, so a program that arms T2_COMP and waits for
   INTC source 11 has a source of interrupts.
2. An INTC handler registry and a delivery path on the EE's own IP2 line, so a
   registered handler actually runs.

ADR 0166 explicitly deferred both rather than fabricating a handler id.

## Decision

### Timer 2

Timer 2 mirrors Timer 3 (ADR 0142) rather than introducing a second time model:

- Registers at `0x10001000` (count), `0x10001010` (mode), `0x10001020`
  (compare), from PS2SDK `common/include/ee_regs.h` (`A_EE_T2_*`). ADR 0166
  resolved `0x10001000`/`0x10001010` on the frontier trace.
- One writable mask, the same write-one-to-clear status bits (EQUF bit 10, OVFF
  bit 11), ZRET, CUE and CMPE, and the same clock-select table (1/16/256/9370
  bus cycles per tick). `ee_timer2_cycles_per_tick` delegates to
  `ee_timer3_cycles_per_tick`, so a correction to the documented divisors
  cannot land in one timer and miss the other.
- Progression is materialised from the same `guest_bus_cycles` source by
  `advance_ee_timers`, which advances both counters once. The pre-existing
  `advance_ee_timer3` remains as a forwarding alias so no caller can add guest
  time twice.
- A CMPE crossing of T2_COMP latches EQUF and raises INTC source 11 exactly once
  per crossing. ZRET resets the counter at the match, re-arming a periodic
  timer. Overflow still latches OVFF but raises nothing: OVFE is not modelled,
  and a fabricated overflow interrupt would be a guess.

### Fail-closed accounting

`timer2_matches`, `timer2_irq_raises` and `timer2_irq_refusals` count the match
path. When the INTC state was never seeded, `raise_source` fails and the refusal
counter increments: the match is recorded as a match, never converted into a
pending interrupt that hardware never produced.

### Handler registry

One `EeIntcHandler` record per documented cause `0..15`
(`kEeMaxIntcHandlers`), mirroring the DMAC registry (ADR 0153):

- `AddIntcHandler` (0x10) refuses `invalid_cause`, `invalid_handler` (null,
  misaligned, or outside main RAM), `chained_handler_unsupported`
  (`next != 0`), `cause_already_registered`, and
  `handler_argument_unsupported` (non-zero `arg`).
- The last refusal exists because syscall `0x10` is also `AddIntcHandler2`,
  which passes a fourth argument the plain handler signature never receives. The
  observed Black call sets `$a3 = 0` (ADR 0166), so only that documented form is
  served; a non-zero argument is refused instead of being delivered to a handler
  that cannot see it.
- Ids are HLE-local handles equal to the cause index, the same convention the
  DMAC registry uses. The kernel's own id values are not observable from the
  target, and reproducing them would be a guess.
- `RemoveIntcHandler` (0x11) removes only when the cause is documented, its
  record is in use, and the id matches; otherwise it refuses and mutates
  nothing.
- `_EnableIntc` (0x14) / `_DisableIntc` (0x15) set or clear one I_MASK bit and
  write it only when the current state disagrees, because I_MASK is
  write-one-to-toggle. The path is unusable without them. Neither can invent a
  pending interrupt: delivery still requires I_STAT to hold the source.

### Delivery

`select_ee_intc_interrupt_cause` picks the lowest pending, mask-enabled cause
that also has a registered handler, or `kEeMaxIntcHandlers` when none qualifies.
`ee_intc_line_asserted` is true only when the master interrupt enable is set
with no exception in progress and that selection succeeds. The predicate and the
entry share the selection, so the dispatcher can never enter an interrupt that
cannot make progress, and an unseeded controller asserts nothing.

`enter_ee_intc_interrupt` follows `enter_ee_dmac_interrupt` (ADR 0153)
exactly:

1. acknowledge the source's I_STAT bit first (write-one-to-clear), so the line
   deasserts before the handler runs;
2. save the whole context in an `EeSyscallHandlerFrame`, marking
   `is_intc_interrupt`;
3. latch EPC with the restart PC, set EXL so no nested interrupt is taken, and
   report Cause IP2 (bit 10) — never the DMAC's IP3;
4. set `$a0 = cause` (the handler is `s32 handler(s32 cause)`),
   `$ra = kEeSyscallHandlerReturnPc`, `pc = handler`.

`return_from_ee_syscall_handler` restores the whole context, keeps the handler's
`$v0` out of the interrupted guest's registers, and counts
`intc_interrupts_completed`. The two interrupt flags are mutually exclusive and
are cleared on every frame reuse, next to `is_guest_callback`.

### Dispatcher

`EXECUTE_CFG_TAIL` emits the INTC check immediately after the DMAC check. The
DMAC's IP3 has priority over the INTC's IP2, and that ordering is asserted by a
frontend test so it cannot drift.

### Host integration

The engine session calls `advance_ee_timers` after each execution step, and the
run report gains additive `timer2` (mode, compare, count, matches, raises,
refusals) and `intc` (stat, mask, known, line asserted, handler count, delivered,
completed, last cause/handler, last outcome of each service) objects.

## Consequences

- Black's `AddIntcHandler(0xB, 0x0036DFF0, 0, 0)` now returns id 11 and the
  frontier can move to whatever the guest does next. That is not a boot claim:
  the id is REPS2's own, and nothing here proves the handler runs correctly for
  that title.
- Timer 2 interrupts are only produced when the guest arms CUE/CMPE; a title
  that instead uses an HLE-served kernel alarm (INTC_TIM3, cause 12) still has no
  path, because that source is reserved by the kernel.
- Overflow interrupts (OVFE) remain unmodelled and are visible as a latched OVFF
  bit with no delivery.
- `EeState` grows by 488 bytes (ADR 0171). ABI 21 refuses ABI 20 modules.

## Validation

Synthetic only; no real-title evidence is claimed by this decision.

- `tests/intc_timer2_smoke.cpp` (runtime API):
  - add/remove: acceptance and the HLE-local id, duplicate refusal, invalid
    cause, null/misaligned/out-of-RAM handler, chained handler, non-zero
    `AddIntcHandler2` argument, wrong-id removal, invalid-cause removal, reuse
    after removal;
  - enable/disable: set and clear, idempotent repeat (no toggle-back), invalid
    cause, unseeded controller fails closed with
    `unsupported_memory_access`;
  - Timer 2 register model: disabled at reset, CLKS 0 progression, one raise per
    crossing, EQUF write-one-to-clear, OVFF on overflow, independence from
    Timer 3;
  - match edge cases: ZRET periodic re-raise, CMPE-gated match, unseeded INTC
    counted as a refusal with I_STAT still unknown;
  - delivery predicate: interrupts disabled, masked source, no handler, EXL set,
    unseeded controller, and that masking does not clear pending state;
  - entry and return: `$a0 = cause`, `$ra` sentinel, I_STAT acknowledged and line
    deasserted, EPC/EXL/IP2 set without IP3, whole-context restore including FPU
    and VU0, completion counted, and the depth-overflow refusal that
    acknowledges nothing.
- `tests/intc_timer2_codegen_smoke.cpp` (AOT blocks, fixture `0x6600`/`0x6700`):
  the compiled guest registers the handler, enables the source, the host model
  raises INTC_TIM2, the emitted dispatcher hook enters `0x6700` with
  `$a0 = 11`, the handler's store lands at `0x00100000`, the interrupted `$v0`
  survives, and a second entry pins the fail-closed case (pending source, no
  handler: not entered, I_STAT untouched).
- `control_codegen::tests::dispatcher_checks_the_intc_after_the_dmac`.
- Local gates (Windows x64, MSVC Release): `cargo fmt --check`,
  `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` (363 frontend tests plus the other crates),
  `cmake --build build --config Release`, `ctest --test-dir build -C Release`
  49/49.
- Measured `sizeof(EeState)` 33,896 on MSVC 14.51, Windows x64, `/std:c++20`
  (ADR 0171).