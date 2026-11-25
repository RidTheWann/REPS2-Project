# ADR 0216: COP0 BC0 branch family (BC0F/BC0T/BC0FL/BC0TL) on CPCOND0

- **Status:** Accepted
- **Date:** 2026-11-25
- **Scope:** Decoder, instruction effects, CFG branch conditions, typed IR, C++20 AOT codegen, EE DMAC runtime model, synthetic CFG fixture, ADR/RESEARCH evidence. No real-title probe here (DMAC not seeded → fail-closed); next frontier slice will advance past the current `pc=0x00217D8C` stop.
- **Related:** ADR 0214 (BC0T/BC0F frontier gate), ADR 0153 (DMAC interrupt), ADR 0105 (DMAC CPCOND0 predicate), Sony EE Core Instruction Set Manual v6.0 Ch.4 pp.290-293.

## Context

The `abi65` real-title probe (main `bdd582b`, runtime ABI 41) stopped at `pc=0x00217D8C` as `unsupported_instruction` on word `0x41010003`. This decodes as COP0 primary `0x10`, rs `0x08` (branch family), rt `0x01` (BC0T), offset `0x0003` (signed word offset, ×4 = +12 bytes). The title ELF contains 32+ BC0T words and 34+ CO words (`rs=0x10`), confirming BC0 is structurally present in Downhill Domination (SCUS_971.77), not a one-off encoding anomaly.

ADR 0214 left BC0T/BC0F as `Opcode::Unknown` (fail-closed) because the COP0 condition bit (`CPCOND0`) was not yet modeled. This slice models `CPCOND0` from the EE DMAC registers `D_PCR.CPC` and `D_STAT.CIS` and wires the BC0 family through the existing CFG branch-condition layer, mirroring the BC1/FPCR31 pattern from ADR 0022.

## Decision

1. **Decode (`rust/frontend/src/mips.rs`):**
   - `decode_cop0` adds `rs == 0x08` arm selecting variant by `rt` (0 `BC0F`, 1 `BC0T`, 2 `BC0FL`, 3 `BC0TL`). Offset is the full 16-bit immediate (bits 15..0), sign-extended, ×4, relative to `PC+4`. Reserved `rt >= 4` decode as `Unknown`. No reserved-field guard on bits 10..0 — they are part of the offset.
   - New `Opcode::Bc0f`, `Bc0t`, `Bc0fl`, `Bc0tl` with mnemonics, control-transfer and conditional-branch classification, and `is_branch_likely` for variants 2/3.

2. **Instruction effects:**
   - `Bc0*` have `control_flow = ConditionalDirect`, no GPR/FPR/ACC dependencies. They read the `CPCOND0` signal, not `FCR31` (which is FPU-only, ADR 0022).

3. **CFG branch condition (`rust/frontend/src/analysis.rs`):**
   - Added `BranchCondition::Cop0Condition { when_set: bool }`. `BC0F/BC0FL` → `when_set: false`; `BC0T/BC0TL` → `when_set: true`. Lifted by `branch_condition()` for the four opcodes.

4. **Runtime CPCOND0 predicate (`runtime/include/reps2/runtime/ee_dmac.hpp`):**
   - `ee_dmac_cop0_condition(d_stat, pcr)` returns `true` iff every DMA channel selected in `D_PCR.CPC` (bits 0..9) has its `D_STAT.CIS` bit set: `((cis | ~cpc) & 0x3FF) == 0x3FF`. Matches Sony EE Core User's Manual (D_PCR bit 0..9 = "COP0 condition control"), PS2Tek, and PCSX2 behavioral cross-check.
   - `EeDmacState::cop0_condition()` returns `std::optional<bool>`; `nullopt` when either `d_stat_known` or `pcr_known` is false, enforcing fail-closed at the generated-code boundary.

5. **Generated C++ emission (`rust/frontend/src/control_codegen.rs`):**
   - `Cop0Condition` terminator emits a dedicated fail-closed predicate:
     ```cpp
     const auto branch_cond_xxx = state.memory.dmac.cop0_condition();
     if (!branch_cond_xxx.has_value()) return Status::unsupported_memory_access;
     const bool branch_taken_xxx = when_set ? *branch_cond_xxx : !*branch_cond_xxx;
     ```
   - This stops the block with `unsupported_memory_access` instead of silently assuming `CPCOND0` is false (which would turn a `BC0F` wait loop into an infinite loop when DMA completes but `CPCOND0` was never signaled).

6. **Control-IR verifier (`rust/frontend/src/control_ir.rs`):**
   - `Cop0Condition` has no register operand to validate (reads DMAC state).

7. **Synthetic CFG fixture (`rust/frontend/src/main.rs`, `tests/control_flow_smoke.cpp`):**
   - Two new segments at `0x7400` (`BC0T`, ExecuteAlways) and `0x7500` (`BC0FL`, AnnulWhenNotTaken).
   - Both stop with `unsupported_memory_access` because the DMAC is unseeded in this synthetic fixture — the predicate returns `nullopt`, triggering the fail-closed return.
   - Tests assert the expected status and PC.

8. **Tests and gates:**
   - `cargo test --lib` adds `decodes_cop0_bc0_branches_on_cpcond0` (decode/offset/likely/reserved) and `emits_cop0_branch_condition_and_fail_closed_on_unknown_dmac` (codegen checks for `state.memory.dmac.cop0_condition()` call, fail-closed return, and correct targets).
   - `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`: 415 passed.
   - CMake Release build + `ctest --test-dir build -C Release --output-on-failure`: 52/52 pass on Windows x64/MSVC.

## Consequences

- The `pc=0x00217D8C` BC0T stop now **decodes** instead of being `Opcode::Unknown`. However, the frontier probe still stops there with `unsupported_memory_access` because the synthetic DMAC state is unseeded (the real-title run does not seed `D_PCR`/`D_STAT` either — that requires a DMA model and is out of scope for this slice). The stop category changes from "unsupported instruction" to "unsupported memory access (DMAC state unknown)" which is a fail-closed gate, not an approximation.
- `CPCOND0` is modeled exactly from `D_STAT`/`D_PCR`. The predicate is fail-closed when the DMAC is unseeded, satisfying "Fail Closed" and "Evidence Before Assertion".
- No runtime ABI bump (`REPS2_RUNTIME_ABI_VERSION` remains 41); the DMAC state was already in `EeMemory` (ADR 0153).
- Next gate: seed `D_PCR` from the title's ELF/DMAC usage and model at least one DMA channel completion (e.g., SIF0 or VIF0) so `CPCOND0` can be known. That will advance the frontier past this BC0T.

## Validation

- Rust gates: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked` (415 lib tests, including new BC0 decode and codegen tests).
- C++ gates: CMake Release build, `ctest --test-dir build -C Release --output-on-failure`: 52/52 pass on Windows x64/MSVC.
- No boot-tested or playable claim. The frontier remains at `pc=0x00217D8C` (`unsupported_memory_access` on unseeded DMAC).

## References

- Sony EE Core Instruction Set Manual v6.0 (SCEI, 2002) Ch.4 pp.290-293: BC0F/BC0FL/BC0T/BC0TL operation, `CPCOND0` = "DMA transfer termination signal", external/asynchronous.
- Sony EE Core User's Manual §3.2 Status CH bit 18 (historical R4400 cache condition); EE manual D_PCR bits 0..9 "COP0 condition control".
- PS2Tek EE DMAC I/O: D_PCR 0x1000E020 CPC 0..9 selects channels feeding CPCOND0; signal true iff all selected channels have CIS set.
- PCSX2 `tDMAC_STAT::TestForInterrupt()` / `dmacInterrupt()` behavioral cross-check: `((D_STAT.CIS | ~D_PCR.CPC) & 0x3FF) == 0x3FF`.
- PS2SDK `dma_wait_fast()` inline asm spin loop: `bc0t` / `bc0f` on CPCOND0.
- PS2Recomp issue #389: "BC0T/BC0TL are never taken" — confirms CPCOND0 behavior on hardware.
- ADR 0214: BC0T/BC0F frontier gate, COP0 condition bit not modeled.
- ADR 0022: BC1/FPCR31 pattern for conditional branches on coprocessor condition bits.
