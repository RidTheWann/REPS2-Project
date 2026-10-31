# ADR 0020: COP1 Data Movement Against the Verified EE Transfer Format

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** R5900 decoder, instruction-effect metadata, typed scalar IR, C++20 AOT backend, runtime FPU state ABI, and the native COP1 smoke fixture

## Context

Every retail ELF entry scan surveyed so far stopped at the same word after 34 decoded instructions: `0x44800000`, reported as `unsupported (cop1)`. Nothing downstream of an ELF entry point can be recompiled until that word is understood, so this was the critical path regardless of which family looked most interesting on paper.

The census agreed: across eight titles, `cop1` was the largest unsupported family (520,703 words under primary `0x11`, plus 443,052 `LWC1`/`SWC1` words the profiler files under `load-store`).

An initial draft of this increment was built on two wrong assumptions, both caught by reading the encodings instead of a generic MIPS table:

1. that bit 25 separates COP1 from COP2, so `0x11` with bit 25 set would be a different coprocessor's transfer;
2. that `SWC1` is primary `0x35`.

Verified against GNU binutils 2.45 `opcodes/mips-opc.c`, the Sony EE Core Instruction Set Manual and EE Core User's Manual, and the `chaoticgd/ghidra-emotionengine-reloaded` R5900 SLASPEC, both are false. Bit 25 is part of a five-bit format selector in bits 25..21 (COP2/VU0 has its own primary opcode `0x12`), `SWC1` is `0x39`, and `0x35` is `LDC1` - which the EE does not implement, because its FPU is MIPS I single-precision only.

## Decision

**Implement exactly the transfers the EE can perform, and reject the rest rather than modeling a generic MIPS32 FPU.**

### Runtime state (`runtime/include/reps2/runtime/ee_state.hpp`)

- `EeState` gains `std::array<std::uint32_t, 32> fpr` and one `std::uint32_t fcsr`. There is no double-width destination anywhere in the EE FPU, so a wider lane would be invented state.
- `kFpuImplementationRegister` is the read-only FIR value `0x2E00`; `kFpuControlResetValue` is `0x0100'0001`, kept as a named constant because the manual's reset figure and emulator behaviour disagree about bit 24.
- FCR31 bits 1..0 read as `0b01` and are not programmable, so control writes go through `store_fpu_control(control, value)`, which forces `(value & ~0x3) | 0x1`. A named helper is used instead of an inline mask so the recompiler cannot forget it.

### Decoder (`rust/frontend/src/mips.rs`)

- `0x11` with `(word & 0x7FF) == 0` dispatches on bits 25..21: `0` MFC1, `2` CFC1, `4` MTC1, `6` CTC1; every other format is `Unknown`. Formats `1`/`5` (DMFC1/DMTC1) and `3`/`7` are absent hardware, and format `16`+ is the arithmetic class, which is the next slice.
- CFC1 accepts only control numbers `0` (FIR) and `31` (FCSR); CTC1 accepts only `31`, matching the manual's single implemented control register.
- `LWC1` is primary `0x31`, `SWC1` is `0x39`, both typed as word memory transfers.
- Effect metadata gains `fpr_read_mask`, `fpr_write_mask`, `reads_fcr` and `writes_fcr`. `fpr_bit` keeps bit 0, unlike `gpr_bit`, because FPR[0] is a real register. `may_raise_exception` is set only for the load/store forms: the EE FPU has no floating-point exception mechanism at all.

### Typed IR (`rust/frontend/src/ir.rs`)

- New `Fpr(u8)` and `FpuControl { Fir, Fcsr }`, plus six operations: `MoveFromFpuRegister`, `MoveToFpuRegister`, `MoveFromFpuControl`, `MoveToFpuControl`, `LoadFpuWord`, `StoreFpuWord`.
- Lifting matches the control number again and returns `UnsupportedOpcode` for anything but FIR/FCR31. The decoder already rejects those, so this guard exists to stop a future widening from silently folding `CFC1 $t, $5` onto FCSR - a wrong-register read the emitter cannot detect.

### Backend (`rust/frontend/src/codegen.rs`)

- MFC1 and CFC1 lower through `sign_extend_word`, writing `GPR[rd].low` and leaving `GPR[127:64]` intact, with the `Gpr::is_zero()` guard.
- MTC1 truncates with `static_cast<std::uint32_t>(GPR[rs].low)` and is deliberately **not** guarded on the destination index, because FPR[0] is writable.
- CTC1 calls `store_fpu_control`; `debug_assert!` pins that FIR never reaches the write path.
- LWC1/SWC1 reuse the existing effective-address and `load_u32`/`store_u32` helpers, so alignment, address translation and unmapped-memory behaviour are the same as every other load and store.

### Fixture (`tests/cop1_codegen_smoke.cpp`)

Fifteen words: `MTC1`/`MFC1` round-trips through FPR[6], `MFC1 $0` executed while FPR[6] holds a non-zero value, `SWC1` to an aligned scratch word, two `LWC1` forms (positive and negative offset), `MTC1`/`LWC1` into **FPR[0]**, the retail blocker word `0x44800000` itself, `CFC1` of FIR before `CTC1` of FCSR, and `CFC1` of FCSR after it. Sources carry bits above 31 so truncation is observable, and every destination register starts dirty so a skipped write is observable.

## Consequences

- The word that halted all eight entry scans now decodes, lifts, lowers, compiles and executes. Entry scans advance from 34 to 66 decoded words and stop at `0x46010018` (`ADDA.S`, format `0x10`, funct `0x18`), which makes COP1 arithmetic the next critical path with a measured reason rather than a guess.
- Aggregate decoder coverage over the eight images moves from 6,193,386/7,814,471 (79.26%) to 6,771,673/7,814,471 (86.66%), i.e. +578,287 words, which reconciles exactly against the family counts.
- ACC is deliberately not added yet. No slice-1 operation reads or writes it, and `docs/RESEARCH.md` already records that the EE FPU's arithmetic is not IEEE-754 faithful, so the arithmetic slice needs its own decisions about flag writes and non-IEEE data semantics before extra state is introduced.
- FCR31's condition and cause bits stay unmodeled, so BC1F/BC1T are not enabled by this increment; the format-8 encodings remain unsupported.
- Game-specific observations stay in `docs/RESEARCH.md`; the toolchain implementation is generic and does not special-case any title.

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings` clean.
- `cargo test --workspace --locked` 121 passing, including seven new COP1 tests: decoder field and reserved-bit rejection, `LWC1`/`SWC1` offsets, effect masks for the FPR file and FCR flags, lift mapping plus the reserved-control-number rejection, and emitter assertions.
- `cmake --build build --config Release` with `/W4 /permissive-`, then `ctest --test-dir build -C Release` passes 8/8 native targets including the new `reps2_cop1_codegen_smoke`.
- Mutation evidence, each rebuilt into the generated `build/generated/cop1_program.cpp` and run:
  | mutation | result |
  | --- | --- |
  | drop the MFC1 `GPR[0]` guard | exit 9 (fails) |
  | zero-extend instead of sign-extend on MFC1 | exit 3 (fails) |
  | let CTC1 program the rounding bits | exit 17 (fails) |
  | guard MTC1 on `FPR[0]` like `GPR[0]` | exit 7 (fails) |
  | fold CFC1 of FIR onto FCSR | exit 15 (fails) |
  | make LWC1 write the GPR file | exit 6 (fails) |
  | make SWC1 store the GPR value | exit 5 (fails) |
- The fourth row is the reason the fixture grew. The first version of the block never issued `MTC1` into FPR[0], so treating FPR[0] as hard-wired zero passed with exit 0; the assertion set was not falsifiable for the claim being made. `0x44820000`/`0x440A0000`/`0x44800000`/`0x440B0000` were added and the mutation now fails.
- `profile-elf` re-run over all eight images into `out/census-v3`, with the per-title and aggregate numbers recorded in `docs/RESEARCH.md`.

## References

- GNU binutils 2.45 `opcodes/mips-opc.c` - `mfc1 0x44000000/0xffe007ff`, `cfc1 0x44400000`, `mtc1 0x44800000`, `ctc1 0x44c00000`, `lwc1 0xc4000000/0xfc000000`, `swc1 0xe4000000`; `dmfc1`/`dmtc1`/`ldc1`/`sdc1` carry the `SF` (non-r5900) exclusions.
- Sony EE Core Instruction Set Manual v6.0 Ch.5 and §7.4 (COP1 format tables, reserved fields, word-alignment AddressError).
- Sony EE Core User's Manual v6.0 §8.1-§8.8 (32 x 32-bit FPRs plus ACC, FIR layout, FCR31 fields, hard-wired rounding, no FP exception mechanism, non-IEEE data semantics).
- `chaoticgd/ghidra-emotionengine-reloaded` `data/languages/{cop1.sinc,base.sinc,r5900.slaspec}` - third cross-check of `format=(21,25)`, `ft=(16,20)`, `fs=(11,15)`, `fd=(6,10)`.
- Related: `docs/decisions/0019-mmi-sa-and-qfsrv.md` (fixture falsifiability and mutation evidence), `docs/RESEARCH.md` "R5900 COP1 (FPU) data-movement cross-checks" and "Measured decoder coverage over lawfully owned retail images".
