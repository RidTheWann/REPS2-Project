# ADR 0220: Intra-batch timer/VBLANK materialization in generated dispatch

- **Status:** Proposed (draft; changes runtime ABI 43 -> 44 — awaiting go-ahead before implementation)
- **Date:** 2026-11-26
- **Scope:**
  - `runtime/include/reps2/runtime/ee_state.hpp` (`EeState::timer_synced_instructions`, `sync_ee_timers(EeState&)`)
  - `runtime/include/reps2/runtime/abi_version.hpp` (`REPS2_RUNTIME_ABI_VERSION` 43 -> 44)
  - `rust/frontend/src/control_codegen.rs` (`execute_cfg` calls `reps2::runtime::sync_ee_timers(state)` per block step)
  - `engine/include/rg_reps2_engine/session.hpp` (`EngineSession::run` calls `sync_ee_timers(state_)` idempotently instead of raw `advance_ee_timers`)
  - `tests/vblank_timing_smoke.cpp` and a new synthetic polling-guest generated-code test (`tests/vblank_poll_codegen_smoke.cpp` or addition to `cfg_codegen_smoke` / `vblank_timing_smoke`)
- **Related:** ADR 0142 (Timer 3 BUSCLK model & batch-boundary limit), ADR 0172 (Timer 2 / INTC handlers), ADR 0218 (deterministic VBLANK model, ABI 42), ADR 0219 (GS CRT display registers, ABI 43).

## Context

Probe `frontier-abi43-01` (merged `main` `e00ab68`, runtime ABI 43, Windows x64 / MSVC 19.51.36260.0, Downhill Domination USA `SCUS_971.77`, ISO SHA-256 `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`, boot ELF SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`) passed the `pc=0x0021F05C` DISPFB2 stop and ended `budget_limit_reached` at `pc=0x0025ABB0` after `79,999,968` instructions over `16` batches (`vblank.starts=16`, `intc.stat=12`, `intc.handlers=0`).

### Diagnosis of the `0x0025ABB0` loop (observed vs derived)

1. **Observed (generated code):**
   - `0x0025AB88` writes `4` (`1 << kEeIntcSourceVblankStart`) to `0x1000F000` (`I_STAT`, write-one-to-clear).
   - `0x0025ABB0..0x0025ABC8` polls `lw $2, 0($3)` (`$3 = 0x1000F000`), `andi $2, $2, 4`, `beq $2, $0, 0x0025ABB0`.
   - `0x0025ABE0` clears bit 2 again before returning.
   - `intc.handlers == 0` and no `AddIntcHandler`/`EnableIntc` appears in `syscall_census`: the title polls `I_STAT` directly instead of registering an INTC IP2 handler (ADR 0172's handler registry/delivery exists, unused by this title here).

2. **Observed (discriminating experiments on the exact ABI-43 module + ISO):**

   | Run config | Total instr | Batches | `vblank.starts` | `syscalls_seen` | Final PC | Last SIF RPC |
   |---|---:|---:|---:|---:|---|---|
   | `80M x 1` (with `--media`) | 79,999,996 | 1 | 16 | 1636 | `0x0025ABB0` | `0x80000593` fn 4 replied 1 |
   | `5M x 1` (with `--media`) | 4,999,999 | 1 | 1 | 1531 | `0x0020E5F0` | `0x8000059C` fn 0 replied 1 |
   | `5M x 2` (with `--media`) | 9,999,996 | 2 | 2 | 1636 | `0x0025ABB0` | `0x80000593` fn 4 replied 1 |
   | `5M x 3` (with `--media`) | 14,999,995 | 3 | 3 | 1643 (+7) | `0x0025ABB0` | `0x80000593` fn 4 replied 1 |
   | `5M x 16` (`frontier-abi43-01`) | 79,999,968 | 16 | 16 | 1736 | `0x0025ABB0` | `0x80000400` fn 1 replied 0 |
   | `5M x 30` (with `--media`) | 149,999,938 | 30 | 30 | 1834 | `0x0025ABB0` | `0x80000400` fn 1 replied 0 |
   | `20M x 20` (with `--media`, trace) | 399,999,953 | 20 | 81 | 1771 | `0x0025ABB0` | (trace: 6 PCs `0x25ABB0..0x25ABC4`) |
   | `1M x 100` (with `--media`) | 99,999,854 | 100 | 20 | 1771 | `0x0025ABB0` | — |
   | `100k x 1000` (with `--media`) | 99,995,080 | 1000 | 20 | 1771 | `0x0025ABB0` | — |
   | `80M x 1` (without `--media`) | 79,999,996 | 1 | 16 | 974665 | `0x00246E2C` | — |

3. **Derived conclusions (H1 / H2 / H3):**
   - **H1 confirmed (root cause of intra-batch stall):** `EngineSession::run` (`engine/include/rg_reps2_engine/session.hpp:217`) calls `advance_ee_timers` (and thus `materialize_ee_vblank`) **only after `execute_cfg` returns at the end of a batch**. Neither `execute_cfg` (`rust/frontend/src/control_codegen.rs`) nor `mmio_read_intc_stat` (`const EeMemory&` in `runtime/include/reps2/runtime/ee_state.hpp:2995`) advances `guest_bus_cycles` or materializes VBLANK. Therefore, regardless of batch budget (`5M`, `20M`, or `80M`), a guest that W1C-clears `I_STAT` bit 2 and polls `0x0025ABB0` can never observe a new `VBLANK_START` during that batch; it exits the wait at most once per batch boundary where a new `VBLANK_START` crossed (producing +7 syscalls of frame work before re-entering the wait). Notice `150M / 30 batches` reaches `1834` syscalls while `400M / 20 batches` stalls at `1771`.
   - **H2 (legitimate frame wait, progressing across VBLANKs):** The guest is not deadlocked in logic; across batch boundaries where `VBLANK_START` flips from 0->1, it exits `0x0025ABB0`, executes ~7 syscalls (`DisableDmac`, `FlushCache`), even dispatches `mcserv` (`0x80000400` fn 1), and then calls the next frame wait. Its progress is artificially throttled to one frame per external batch boundary by H1.
   - **H3 refuted:** No hidden writer clears bit 2; `0x0025AB88` is the wait function's own W1C ack, and `0x0025ABB0` tests bit 2 (`0x4`).

## Proposed Design (Minimal Change)

1. **Add `std::uint64_t timer_synced_instructions{}` to `EeState`** (`runtime/include/reps2/runtime/ee_state.hpp`) and define an inline helper:

   ```cpp
   inline void sync_ee_timers(EeState& state) noexcept {
       if (state.instructions_executed > state.timer_synced_instructions) {
           const std::uint64_t delta =
               state.instructions_executed - state.timer_synced_instructions;
           state.timer_synced_instructions = state.instructions_executed;
           advance_ee_timers(state, delta);
       }
   }
   ```

   - **Why a separate watermark instead of reading `guest_bus_cycles` directly:** Existing unit tests (`tests/vblank_timing_smoke.cpp`, `tests/intc_timer2_smoke.cpp`, `tests/hle_syscall_smoke.cpp`) call `advance_ee_timers(state, delta)` directly without advancing `state.instructions_executed`. Keeping `timer_synced_instructions` separate guarantees those tests remain exact while making `sync_ee_timers(state)` strictly idempotent across `execute_cfg` and `EngineSession::run`.
   - **Fail-closed:** `advance_ee_timers` / `materialize_ee_vblank` continue to respect `intc.i_stat_known`; when `I_STAT` is unseeded (`seed_idle_boot_state()` not called), `raise_source` refuses and increments `vblank_irq_refusals`, and `mmio_read_intc_stat` fails closed with `unsupported_memory_access`. When `instructions_executed <= timer_synced_instructions`, `sync_ee_timers` is a no-op (no wrap, no negative delta).

2. **Invoke `reps2::runtime::sync_ee_timers(state)` inside `execute_cfg`** (`rust/frontend/src/control_codegen.rs`):
   - Call `reps2::runtime::sync_ee_timers(state);` immediately after `found->function(state, remaining)` returns (whether `Status::ok` or non-`ok` — so `guest_bus_cycles`, VBLANK, Timer 2, and Timer 3 match `state.instructions_executed` on every stop, budget exhaustion, and block transition), **before** the next iteration's `ee_dmac_line_asserted` / `ee_intc_line_asserted` checks.
   - Consequence: a tight `I_STAT` polling block (like `0x0025ABB0..0x0025ABC8`) sees `VBLANK_START` inside a **single** `execute_cfg` call as soon as `instructions_executed` crosses `4,920,954 - 487,443 = 4,433,511`, and Timer 2 / VBLANK interrupts registered via `AddIntcHandler` can also fire at the exact block boundary where the cycle threshold crosses.

3. **Update `EngineSession::run`** (`engine/include/rg_reps2_engine/session.hpp`):
   - Replace the post-batch `advance_ee_timers(state_, state_.instructions_executed - before)` with `::reps2::runtime::sync_ee_timers(state_);`.
   - Hand-written test modules that bypass `execute_cfg` and mutate `state.instructions_executed` still advance at batch end; codegen modules that already called `sync_ee_timers` per block are not double-counted.

4. **Bump `REPS2_RUNTIME_ABI_VERSION` from 43 to 44** (`runtime/include/reps2/runtime/abi_version.hpp`):
   - `EeState` grows by 8 bytes (`timer_synced_instructions`). All ABI-43 native modules are rejected at admission and must be rebuilt.

## Tests Planned

- Synthetic unit tests in `tests/vblank_timing_smoke.cpp`:
  - Idempotency of `sync_ee_timers(state)` when called repeatedly or after `execute_cfg`.
  - No-op / no underflow when `instructions_executed < timer_synced_instructions`.
- Synthetic **polling-guest generated-code test** (compiled from MIPS words via the Rust frontend or matching generated `execute_cfg` structure and registered in CTest):
  - Guest W1C-clears `I_STAT` bit 2 (`0x1000F000 <- 4`), then polls `(I_STAT & 4) == 0` in a tight loop, then exits cleanly (or stops on sentinel) with a single batch budget > `kEeVblankCyclesPerFrame`.
  - Assert that **without** intra-batch `sync_ee_timers` it exhausts the single-batch budget inside the loop, and **with** the change it observes `VBLANK_START` and exits the polling loop within that single batch.

## What Is NOT Claimed

- Not claiming cycle-accurate hardware VBLANK timing (`1 instruction = 1 BUSCLK cycle` remains the ADR 0142 synthetic model).
- Not claiming that clearing this intra-batch throttle makes Downhill Domination boot or playable; the probe will simply advance past the throttle to whatever real blocker (e.g., `mcserv` fno 1, GIF/VIF/GS transfers, or another service/instruction gap) lies downstream.
