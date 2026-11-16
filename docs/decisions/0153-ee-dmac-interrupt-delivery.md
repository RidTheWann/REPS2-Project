# ADR 0153: Deliver the DMAC interrupt on the EE's own COP0 line (IP3) and run the registered AddDmacHandler handler

- **Status:** Proposed (validated locally; real-title probe recorded below).
- **Date:** 2026-11-16.
- **Scope:** runtime EE DMAC delivery: a device predicate
  (`EeDmacState::interrupt_pending()`), the interrupt-enable predicate
  (`ee_interrupts_enabled`), the channel selector and delivery entry
  (`select_ee_dmac_interrupt_channel`, `ee_dmac_line_asserted`,
  `enter_ee_dmac_interrupt`) in `ee_hle.hpp`, new Cause IP2/IP3 constants and a
  `is_dmac_interrupt` handler-frame kind in `ee_state.hpp`, HLE observation
  fields, a dispatcher hook in the generated `execute_cfg`
  (`control_codegen.rs`), and smoke tests. Runtime ABI 15 -> 16 (the
  `EeState`/`EeHleState` layout grows, so ABI-15 modules are rejected at
  admission).

## Context

S3 of the M4 story set is "deliver shared events: SIF0 completion -> DMAC IP3
line -> EE exception path and semaphore producer". The pieces that already
existed:

- **Observed (ADR 0100):** a completed inbound IOP->EE SIF transfer publishes
  the D_STAT channel-5 status bit; `EeDmacState::publish_sif0_event()` models
  that and the RPC reply paths call it (`ee_hle.hpp` at the three
  `ee_sif0_event_published = state.memory.dmac.publish_sif0_event()` sites).
- **Observed (ADR 0105):** the DMAC interrupt condition is
  `((D_STAT[15:0] & D_STAT[31:16]) != 0) || BEIS`, gated by `D_CTRL.DMAE`; the
  line is the EE's own COP0 `Cause` IP3 (bit 11, `0x800`), not the INTC's IP2
  (bit 10, `0x400`). `ee_dmac_interrupt_pending(d_stat, dmae, held)` already
  computed that condition, but **nothing consumed it**: no delivery path existed
  and `docs/decisions/0095` records "nothing here delivers an interrupt".
- **Observed (ADR 0095):** `AddDmacHandler` (0x12) records one handler per
  channel (HLE-local id == channel index; a chained `next` is refused).
  `RemoveDmacHandler` (0x13) clears the record.
- **Observed (ADR 0025):** `EI`/`DI` toggle `Status.EIE` (bit 16, mask
  `0x00010000`); the HLE boot seed sets `Status = 0x10400004` (EIE clear) and
  `D_CTRL = D_STAT = 0` via `seed_idle_boot_state()`.

The kernel's own DMAC interrupt entry is at the EE general interrupt vector
`0x80000200` with a `Cause.ExcCode == 0` exception; a real EE kernel dispatches
its registered handler chain from there. REPS2 is the kernel and executes no
kernel code, so jumping to `0x80000200` would fetch an untranslated address.

## Decision

1. **Device predicate.** `EeDmacState::interrupt_pending()` applies the ADR 0105
   condition to the modelled registers and is **fail-closed**: it reports
   `false` unless both `D_CTRL` and `D_STAT` were seeded, so an unseeded device
   never fabricates an interrupt. The EE register map has no hold register, so
   the condition's `controller_held` input is a constant `false` here.
2. **Interrupt-enable predicate.** `ee_interrupts_enabled(state)` is true only
   when `Status.EIE` (bit 16, the bit `EI`/`DI` toggle per ADR 0025) is set and
   neither `EXL` nor `ERL` is set. The INTC and DMAC lines each gate on it
   before delivering.
3. **IP3, never IP2.** The delivery sets `Cause` bit 11 (`kCop0CauseIp3Mask`); it
   never touches the INTC register pair. New named constants
   `kCop0CauseIp2Mask`/`kCop0CauseIp3Mask` document the two external lines. SIF0
   completion is never routed into the INTC (it is not an INTC source).
4. **Kernel-side dispatch of the registered handler.** When the line is asserted,
   interrupts are enabled, the asserting channel has a registered handler, and
   there is handler-frame room, `enter_ee_dmac_interrupt`:
   - acknowledges exactly that channel's D_STAT status bit (`write_d_stat` W1C),
     so the line deasserts;
   - saves the interrupted context whole (the same frame as a SIF end-function
     callback) and marks it `is_dmac_interrupt`;
   - latches `EPC = resume PC`, sets `EXL`, and sets `Cause` IP3 — applied on
     top of the saved COP0 image, so they revert when the handler returns;
   - passes the channel in `$a0` (the handler is `s32 handler(s32 channel)`),
     points `$ra` at the dispatcher sentinel, and transfers to the handler PC.
   On return, `return_from_ee_syscall_handler` restores the whole saved context
   (including EPC/EXL/Cause) and does not keep the handler's `$v0`, exactly like
   a guest callback: the handler ran in interrupt context.
5. **Dispatcher hook.** `execute_cfg` checks `ee_dmac_line_asserted` at the
   instruction boundary, after the syscall-handler-return and SIF-callback
   checks and before the fetch, so an interrupt is taken at a block boundary and
   the handler runs before the interrupted guest continues.
6. **Observability.** `EeHleState` gains `dmac_interrupts_delivered`,
   `dmac_interrupts_completed`, `last_dmac_interrupt_channel` and
   `last_dmac_interrupt_handler`.

### Deliberate non-decisions

- REPS2 does not fetch the kernel's general interrupt vector at `0x80000200`.
  Kernel code is not part of the recompiled title, so a jump there would fetch an
  untranslated address; REPS2 is the kernel and invokes the `AddDmacHandler`
  handler the guest registered directly, after establishing the same interrupt
  frame state (`EPC` latch, `EXL` set, `Cause` IP3, `ExcCode == 0` semantics) the
  vector entry would have. The keystone is the frame, not the fetch.

- The real EE also requires `Status.IE` (bit 0) and the `Cause` IM/IM2 (and
  `D_STAT` CIM) mask bits before an asynchronous interrupt is taken. REPS2 models
  the interrupt-enable bit established by ADR 0025 (`Status.EIE`, the bit the
  guest's own `EI`/`DI` toggle) and the per-channel CIM half of the ADR 0105
  condition; it does not model `Status.IE` or `Cause.IM`/`IM2`. This is a
  documented limitation, not a claim of hardware-exact gating.
- The semaphore-producer half of S3 (a DMAC handler that signals a semaphore the
  interrupted guest is waiting on) is **not** implemented: REPS2 has no
  scheduler, so there is no thread to make runnable. A handler that signals a
  semaphore raises its count (ADR 0101); whether that releases a waiter is not
  modelled and is not claimed.

## Consequences

- SIF0 completion can now produce a guest-visible interrupt: the registered
  `AddDmacHandler` handler runs as translated guest code in interrupt context,
  with the interrupt's EPC/EXL/Cause state and the guest's own register file
  preserved across it.
- The DMAC line is delivered on its own COP0 line and is never confused with the
  INTC line; the INTC register pair is untouched by DMAC delivery.
- In an HLE boot the DMAC stays idle (`D_CTRL.DMAE` clear) and `Status.EIE` is
  clear until the guest enables it, so the delivery is inert unless the guest
  both programs the DMAC and enables interrupts.
- The `EeState`/`EeHleState` layout grows, so Runtime ABI rises 15 -> 16; ABI-15
  native modules are rejected at admission.
- No compatibility state changes.
- Handler frames are reused at the same depth. Every frame-entry point now sets
  both context-kind flags explicitly (`enter_ee_syscall_handler`,
  `enter_ee_guest_callback`, `enter_ee_dmac_interrupt`); previously a frame's
  kind could survive into a later invocation at the same depth, so this ADR also
  fixes that latent leak. The `dmac interrupt dispatches the registered handler`
  smoke case covers it by queueing a guest callback after an interrupt.

## Validation

- `reps2_hle_syscall_codegen_smoke` adds three cases (177 total):
  - `dmac interrupt needs the master enable and a handler`: the delivery
    predicate is false with the enable clear, with EXL or ERL set, when the
    asserting channel has no handler, and on an unseeded device; the channel
    selector returns the asserting channel or the sentinel.
  - `dmac interrupt dispatches the registered handler`: a fired SIF0/DMAC event
    with interrupts enabled runs the `0xE800` handler (which signals semaphore 0
    from `$a0` = channel), acknowledges the channel's D_STAT bit, leaves the mask
    half untouched, restores the interrupted `Cause`/`EXL`/`EPC` and the caller's
    `$a0`, and returns to the unreachable fetch PC as `external_control_flow`.
  - `dmac interrupt is not an intc source`: an asserted INTC line is not a DMAC
    delivery, and a DMAC delivery leaves `I_STAT`/`I_MASK` untouched while
    setting `Cause` IP3 and not IP2.
- `reps2_dmac_dstat_codegen_smoke` adds `test_dmac_device_interrupt_pending`:
  the device predicate is false with either register unseeded, false with
  `DMAE` clear, true with `DMAE` set and a matched status/mask pair, true on the
  bus-error bit, and false for an unmatched status bit.
- The native-build driver smoke expects runtime ABI 16.
- CMake Release build and `ctest -C Release` (43/43); Rust `fmt`/`clippy`/`test`
  on Windows x64/MSVC.
- Real-title probe: see below.

## Real-title probe

Same title and digests (boot ELF SHA-256 `adfda7b7...f0cf0c`, parent ISO SHA-256
`0fe2b777...a7eb86`), Windows x64/MSVC, runtime ABI 16, native module rebuilt
from this branch (`D:\REPS2-DevTests\DownhillDomination\PostADR153\run-report-dmac-interrupt.json`).
The run report gains a `dmac` object (`handlers`, `add_handler_calls`,
`interrupts_delivered`, `interrupts_completed`, `last_interrupt_channel`,
`last_interrupt_handler`).

- **Observed:** the trace is unchanged from ADR 0152 — `budget_limit_reached`
  after 79,999,984 instructions over 16 batches at `pc=0x00201FC0`,
  `syscalls_seen=1841052 handled=1841035`, callbacks 78/78, `timer3.cycles=79999984`,
  the 989snd binds still succeed (`rpc_bind_sid=0x00123457 rpc_bind_had=1`), the
  first 989snd call is still recorded and refused (`rpc_call_number=0
  rpc_call_replied=0`), and the 78 `CD_NCMD_READ`s (last LBN 337) and mcserv INIT
  are still served.
- **Observed (the new fields):** `dmac.handlers=1`,
  `dmac.add_handler_calls=2`, **`dmac.interrupts_delivered=0`**,
  `dmac.interrupts_completed=0`.
- **Derived:** the delivery never fires because the device predicate never
  reported the line: delivery is impossible unless `D_CTRL.DMAE` is set with a
  matched `D_STAT` status/mask pair, and the HLE boot seeds `D_CTRL = 0`
  (`seed_idle_boot_state`) while the observed path does not program the DMAC
  master enable. The new path is therefore inert under the current HLE boot —
  the correct fail-closed behaviour (no interrupt is fabricated) — and the
  frontier is unchanged: the driver still spins at `0x00201FC0` waiting for IOP
  returns a refused 989snd call never produces. This slice is a capability
  advance (a real DMAC interrupt now delivers to guest code when the guest
  enables one), not a frontier advance on this title.

