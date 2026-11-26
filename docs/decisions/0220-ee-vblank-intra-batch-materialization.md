# ADR 0220: Intra-batch timer/VBLANK materialization in generated dispatch

- **Status:** Accepted (validated locally; real-title probe `abi44-*` on Downhill Domination USA)
- **Date:** 2026-11-27
- **Scope:**
  - `runtime/include/reps2/runtime/ee_state.hpp` (`EeMemory::timer_synced_instructions`, `timer_next_event`, `timer2_needs_rebase`, `timer3_needs_rebase`; `compute_next_timer_event`, `sync_ee_timers`; Timer 2/3 mode writes set the rebase flags)
  - `runtime/include/reps2/runtime/abi_version.hpp` (`REPS2_RUNTIME_ABI_VERSION` 43 -> 44)
  - `rust/frontend/src/control_codegen.rs` (`execute_cfg` deadline-guarded `sync_ee_timers`, new `0x7600` VBLANK poll fixture segment)
  - `rust/frontend/src/main.rs` (`0x7600` fixture segment/entry/root)
  - `engine/include/rg_reps2_engine/session.hpp` (`EngineSession::run` calls `sync_ee_timers` idempotently)
  - `tests/vblank_poll_codegen_smoke.cpp` (new: intra-batch polling guest), `tests/vblank_timing_smoke.cpp` (mid-batch arm rebase case), `tests/rg_reps2_engine_smoke.cpp` (ABI-43 refusal)
- **Related:** ADR 0142 (Timer 3 BUSCLK model & batch-boundary limit — **superseded in part**), ADR 0172 (Timer 2 / INTC delivery), ADR 0218 (deterministic VBLANK model — **batch-boundary note superseded**), ADR 0219 (GS CRT display registers), ADR 0103 (INTC register file), ADR 0164/0169 (ABI-bump discipline).

## Context

Probe `frontier-abi43-01` (merged `main` `e00ab68`, runtime ABI 43, Windows x64 / MSVC 19.51.36260.0, Downhill Domination USA `SCUS_971.77`, ISO SHA-256 `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`, boot ELF SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`) passed the `pc=0x0021F05C` DISPFB2 stop and ended `budget_limit_reached` at `pc=0x0025ABB0` after `79,999,968` instructions over `16` batches (`vblank.starts=16`, `intc.stat=12`, `intc.handlers=0`).

### Diagnosis of the `0x0025ABB0` loop (observed vs derived)

1. **Observed (generated code):** `0x0025AB88` writes `4` (`1 << kEeIntcSourceVblankStart`) to `0x1000F000` (`I_STAT`, write-one-to-clear); `0x0025ABB0..0x0025ABC8` polls `lw $2, 0($3)` (`$3 = 0x1000F000`), `andi $2, $2, 4`, `beq $2, $0, 0x0025ABB0`; `0x0025ABE0` clears bit 2 again before returning. `intc.handlers == 0` and no `AddIntcHandler` appears in `syscall_census`, so the title polls `I_STAT` directly (ADR 0172's IP2 handler path exists, unused here).

2. **Observed (discriminating experiments on the exact ABI-43 module + ISO):**

   | Run config | Total instr | Batches | `vblank.starts` | `syscalls_seen` | Final PC |
   |---|---:|---:|---:|---:|---|
   | `80M x 1` (media) | 79,999,996 | 1 | 16 | 1636 | `0x0025ABB0` |
   | `5M x 16` (`frontier-abi43-01`) | 79,999,968 | 16 | 16 | 1736 | `0x0025ABB0` |
   | `5M x 30` (media) | 149,999,938 | 30 | 30 | 1834 | `0x0025ABB0` |
   | `20M x 20` (media, 400M instr) | 399,999,953 | 20 | 81 | 1771 | `0x0025ABB0` |
   | `1M x 100` (100M instr) | 99,999,854 | 100 | 20 | 1771 | `0x0025ABB0` |
   | `100k x 1000` (100M instr) | 99,995,080 | 1000 | 20 | 1771 | `0x0025ABB0` |
   | `80M x 1` (no media) | 79,999,996 | 1 | 16 | 974,665 | `0x00246E2C` |

3. **Derived (H1 confirmed / H2 frame progression / H3 refuted):** Guest progress through `0x0025ABB0` scaled with batch boundaries, not instructions (`150M / 30 batches` -> 1834 syscalls; `400M / 20 batches` -> 1771). Root cause: `EngineSession::run` (`engine/include/rg_reps2_engine/session.hpp`) called `advance_ee_timers` **only after `execute_cfg` returned**, never inside `execute_cfg` or on `I_STAT` reads (`mmio_read_intc_stat` takes `const EeMemory&` and does not advance `guest_bus_cycles`). A guest that W1C-cleared bit 2 and polled could observe at most one `VBLANK_START` per batch. H3 refuted: `0x0025AB88` is the wait routine's own W1C ack.

## Sync-strategy comparison (evidence: same machine, same 80M-instruction module, median of 3 runs)

| Strategy | 80M no-media (`engine.elapsed_microseconds`) | 80M media | Notes |
|---|---:|---:|---|
| Baseline, ABI 43 module (batch-end only) | 1,102,162 us | 365,043 us (1 frame) | the stall being fixed |
| **S1** unconditional per-block `sync_ee_timers` | 1,168,489 us (+6.0%) | 453,773 us (16 frames) | every step calls `compute_next_timer_event` |
| **S2** deadline-guarded (`instructions_executed >= timer_next_event`) | **1,093,138 us (-0.8%, in noise)** | **400,087 us** (16 frames) | one `std::uint64_t` compare per step; recompute only when the deadline is reached |
| S3 lazy on MMIO access + batch end | — | — | rejected: does not bound the cycle source at block boundaries, so a polling guest (exactly the observed case) still sees one edge per batch |

**Chosen: S2.** It matches the baseline cost within measurement noise on the media-free workload (which exercises the full 974,665-syscall path) while restoring per-frame progress on the media workload, and its per-step cost is a single compare instead of a virtual-call-free but still non-trivial recomputation. S3 is rejected because the observed guest polls `I_STAT` in a tight loop, which is precisely the case lazy materialization cannot cover.

Requirement checks:
- **Guest-time only:** the deadline derives from `instructions_executed` vs `guest_bus_cycles`; no host clock or address is read.
- **Ordering inside a window:** `sync_ee_timers` runs *after* the block function returns and *before* the dispatcher's DMAC/INTC/scheduler checks, so a source raised at the boundary is visible to the interrupt checks in the same iteration. A guest W1C write executed inside the block is still applied by `mmio_write_intc_stat` at the guest's observed time; the following `sync_ee_timers` cannot resurrect a cleared bit (it only ORs newly-crossed transitions).
- **Latched until W1C:** `I_STAT` bits stay set until the guest writes 1 to them; the model never clears a pending bit itself.
- **Idempotence:** `timer_synced_instructions` is a watermark; `sync_ee_timers` with no instruction progress is a no-op and never rewinds. `EngineSession::run` calls it after the dispatcher, so the two call sites never double-count.
- **Mid-batch arming:** a Timer 2/3 mode write that enables CUE while the cycle source lags `instructions_executed` sets a rebase flag; the next `sync_ee_timers` re-bases `*_cycles_at_base` to the current cycle count instead of attributing pre-arm instructions to the timer (covered by `test_timer2_mid_batch_arm_rebase`).
- **Supersedes:** ADR 0142's "the host advances the cycle source once per execution batch ... finer attribution would need a per-instruction runtime hook" and ADR 0218's "calls `advance_ee_timers` after each execution boundary; no call-site change needed" are superseded by this ADR for the batch-attribution limit. Reads between steps still see the value as of the last block boundary (not per instruction); that granularity is unchanged and is not claimed.

## ABI

`REPS2_RUNTIME_ABI_VERSION` 43 -> 44: `EeMemory` gains `timer_synced_instructions`, `timer_next_event` and the two `*_needs_rebase` flags, so `sizeof(EeState)` changes. ABI-43 modules are refused at admission as `runtime_abi_mismatch` (verified: the ABI-43 `frontier-abi43-01` module is rejected with `engine_error: runtime_abi_mismatch`, exit 4). Every generated module must be rebuilt. Nothing is silently admitted.

## Tests

- `tests/vblank_poll_codegen_smoke.cpp` (new): a `0x7600` fixture guest W1C-clears `I_STAT` bit 2 and polls it for 8 frames inside **one** batch, plus deterministic replay, sync idempotence, unseeded-INTC fail-closed, and batch-size invariance (45,000,000 instructions as 1 / 16 / 450 batches must observe the same frame count and VBLANK count +/-1).
- `tests/vblank_timing_smoke.cpp`: adds `test_timer2_mid_batch_arm_rebase`.
- `rust/frontend/src/control_codegen.rs`: `emits_deadline_guarded_intra_batch_timer_sync` asserts the emitted gate.
- **Falsification:** removing the gate from `execute_cfg` makes `reps2_vblank_poll_codegen_smoke` fail (0% -> pass). Restoring it passes.

## Validation

- Rust: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked` — all pass (416 lib tests + suites).
- C++: `cmake -S . -B build -DBUILD_TESTING=ON`, Release build, `ctest --test-dir build -C Release --output-on-failure` — **54/54 pass** (was 53; the new `reps2_vblank_poll_codegen_smoke` is #48).
- Real-title probes (`abi44-*`, rebuilt frontend + native module at ABI 44, same ISO/ELF digests, Windows x64 / MSVC 19.51.36260.0):

  | Run | Outcome | PC | `vblank.starts` | `syscalls_seen` |
  |---|---|---|---:|---:|
  | `80M_5Mx16` | `budget_limit_reached` | `0x0025ABB0` | 16 | 1743 |
  | `80M_80Mx1` | `budget_limit_reached` | `0x0025ABB0` | 16 | 1743 |
  | `80M_1Mx80` | `budget_limit_reached` | `0x0025ABB0` | 16 | 1743 |
  | `400M_20Mx20` | `budget_limit_reached` | `0x0025ABB0` | 81 | 2198 |
  | `2B_80Mx25` | `budget_limit_reached` | `0x0025ABB0` | 406 | 4473 |

  **Batch-size invariance holds** on the real title: 1 / 16 / 80 batches at 80M instructions all give `vblank.starts=16` and `syscalls_seen=1743`. Only `DisableDmac` (23) and `FlushCache` (100) grow with frames (`+650` / `+1625` from 400M to 2B), i.e. ~2.5 syscalls per frame of steady-state frame work.

## Consequences

- The `pc=0x0025ABB0` loop is a **legitimate per-frame wait** whose progress is no longer throttled to one frame per batch. Its floor is now `frame_cycles` of guest time, not the host batch size.
- The remaining M4 blocker at this frontier is the refusal, not the wait: the title's last SIF RPC is server `0x80000400` (`mcserv`) function 1 with `rpc_call_replied=0`, so the frame loop is waiting on an unmodelled memory-card response. That is a separate service-gap unit.
- Not claimed: cycle-accurate hardware VBLANK timing (1 instruction = 1 BUSCLK cycle remains the ADR 0142 synthetic model); per-instruction (sub-block) granularity; a boot or playable stage for any title.

## References

- Sony EE Core Instruction Set Manual v6.0 / EE Core User's Manual: INTC source numbering (`INTC_VBLANK_S` = 2), `INTC_STAT = 0x1000F000` write-one-to-clear.
- ADR 0142, ADR 0172, ADR 0218, ADR 0219; ADR 0164/0169 (ABI-bump discipline); CLAUDE.md Golden Rules (fail closed, evidence before assertion, ABI discipline).
