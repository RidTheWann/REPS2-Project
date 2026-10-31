# ADR 0019: MMI SA Register Modeling and QFSRV Funnel Lowering

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** R5900 decoder, instruction-effect metadata, typed scalar IR, C++20 AOT backend, runtime ABI, and the native SA/QFSRV smoke fixture

## Context

The work in progress at the start of this increment added four MMI shift-amount operations to the decoder, IR and backend, but stopped with the state unreadable and the semantics approximated. Two defects mattered for hardware fidelity:

1. `EeState::sa` was a `std::uint32_t`, so `MTSA` truncated the documented 64-bit source lane and `MFSA` could never return what hardware returns.
2. `MTSAB` and `MTSAH` dropped the documented scale factors, emitting the XOR result directly (`MTSAB`) and as a one-bit shift (`MTSAH << 1`) instead of `* 8` and `* 16`.

The native fixture also did not match its own instruction block: it described `GPR[3]` as the `MTSAB` input while the compiled words read `GPR[4]`, and its final assertion (`state.sa == 8`) contradicted the comment above it. Most importantly, nothing in the toolchain read `SA`, so the increment added state without adding a capability.

Research against the EE Core Instruction Set Manual, GNU binutils `opcodes/mips-opc.c`, PCSX2 and two Ghidra R5900 languages confirmed the encodings already in the patch were right and the spellings are `MFSA`, `MTSA`, `MTSAB`, `MTSAH`, with a single SA register and exactly one consumer: `QFSRV`.

## Decision

**Model SA as a 64-bit bit count and lower QFSRV in the same increment.**

### Decoder (`rust/frontend/src/mips.rs`)

- `MFSA` is `SPECIAL` funct `0x28` with `rs == rt == 0`, `format == 0`, and the destination in `rd` (bits 15..11); `MTSA` is funct `0x29` with bits 20..6 reserved zero and the source in `rs`.
- `MTSAB`/`MTSAH` are `REGIMM` (`0x01`) with the function in bits 20..16 (`0x18`/`0x19`), the source in `rs`, and the immediate in bits 15..0. Because these are not branches, target and delay-slot computation is now restricted to the branch conditions of the `REGIMM` family instead of applying to every decoded `REGIMM` opcode.
- `QFSRV` is primary opcode `0x1C`, funct `0x28`, MMI group selector bits 10..6 = `0x1B`. The explicit selector check keeps it distinct from PADDUW, which shares funct `0x28` with selector `0x10`.
- Effect metadata gains `reads_sa`/`writes_sa` alongside the existing HI/LO1 flags. `QFSRV` reads SA; the movers write it.

### Typed IR (`rust/frontend/src/ir.rs`)

`MoveFromSa`, `MoveToSa`, `MoveToSaByte`, `MoveToSaHalfword` and `FunnelShiftRightVec { dst, lhs, rhs }`, where `lhs` is `GPR[rs]` (funnel high 128) and `rhs` is `GPR[rt]` (funnel low 128). No static shift invariant applies because the amount comes from processor state, so the existing verifier rules are unchanged.

### Backend and runtime ABI

- `MFSA` writes only `GPR[rd].low` and honors the hard-wired zero register; `MTSA` reads `GPR[rs].low` without narrowing.
- `MTSAB` emits `((GPR[rs].low & 0x0F) ^ (imm & 0x0F)) * 8` and `MTSAH` emits `((GPR[rs].low & 0x07) ^ (imm & 0x07)) * 16`.
- `QFSRV` calls `reps2::runtime::funnel_shift_right_vec`, which shifts the four-word funnel and returns the low 128 bits. Word and bit shifts are split so no C++ shift ever uses a count of 64 or more, which would be undefined behavior.

### Fixture

The compiled word block is `MTSA $1` / `MFSA $0` / `MFSA $2` / `MTSAB $3, 0x0F` / `QFSRV $6, $5, $7` / `MFSA $9` / `MTSAH $8, 0x07` / `QFSRV $10, $5, $7` / `MTSAB $11, 0x05` / `QFSRV $12, $5, $7` / `MTSAB $3, 0x03`. Funnel operands are `{1,2}` and `{3,4}` so the expected results for SA = 88, 64 and 0 are hand-computable, `MFSA $9` after `QFSRV` proves SA is read rather than consumed, and `MFSA $0` sits where SA is still non-zero so the zero-register guard is observable.

## Consequences

- SA becomes an executable capability instead of dead state, and the MMI1 group gains a second lowered operation.
- Two behaviors are implemented but explicitly **unverified**: the observable effect of `MFSA` (Sony scales at definition time, PCSX2 stores bytes and scales at use) and `QFSRV` with `SA >= 128`, which only `MTSA` can produce. Both are recorded in `docs/RESEARCH.md` and must be settled by differential testing before any compatibility claim depends on them.
- Branch-likely-delay-slot illegality and the three-cycle SA hazard are documented hardware restrictions that REPS2 does not yet enforce or model.
- The static shift-amount verifier cannot validate dynamic SA, so QFSRV correctness rests on the runtime helper and the native fixture rather than on an IR invariant.

## Validation

- `cargo test --workspace --locked` reports 114 passing unit tests, including new decoder, effect, lift and emitter cases for the SA family and QFSRV.
- `cargo clippy --workspace --all-targets -- -D warnings` is clean; this also required replacing a `Default::default()` plus field-reassignment pattern in `analysis.rs` that failed the CI lint gate.
- `cmake --build build --config Release` plus `ctest --test-dir build -C Release` pass all 7 native targets on Windows/MSVC with `/W4 /permissive-`, including the regenerated `build/generated/sa_program.cpp`. Run from the repository root, plain `ctest -C Release` discovers no tests and must not be quoted as evidence.

## Amendment (post-merge review, same day)

An independent review of the landed head found that one native assertion was presented as evidence it could not provide, and that one research claim was too broad.

- `tests/sa_codegen_smoke.cpp` checked `GPR[0] == 0` after a trailing `MFSA $0`, but SA was already zero at that point, so deleting the emitter's zero-register guard left the test green. The fixture word block now issues `MFSA $0` immediately after `MTSA $1`, while SA holds `0xDEAD'BEEF'CAFE'0001`, so the assertion fails if the guard is removed. The final `state.sa == 0` check had the same weakness in a different way - `(0x5 ^ 0x5) * 8` is zero under any scaling - and is replaced by a trailing `MTSAB $3, 0x03` that must leave SA at 56, which no missing scale factor or wrong nibble mask reproduces. Verified by mutation: with the emitter guard removed, the native target fails; restored, `ctest --test-dir build -C Release` is 7/7.
- `docs/RESEARCH.md` stated that the Sony and PCSX2 readings "produce the same QFSRV result". That holds only for `MTSAB`/`MTSAH`, whose PCSX2 rec paths defer the `<< 3`. `recMTSA` stores `GPR[rs][31:0] & 0xF`, so `MTSA` feeding `QFSRV` differs from REPS2 by truncation and a factor of eight. The claim is now scoped and the `MTSA` divergence recorded as an open differential-testing item.

## References

- Sony Computer Entertainment, *EE Core Instruction Set Manual*, Version 6.0: https://github.com/ninjadynamics/PS2Docs/blob/main/EE_Core_Instruction_Set_Manual.pdf
- Sony Computer Entertainment, *EE Core User's Manual*, Version 6.0 (SA register state): https://github.com/ninjadynamics/PS2Docs/blob/main/EE_Core_Users_Manual.pdf
- GNU binutils, `opcodes/mips-opc.c` (R5900 operand masks): https://sourceware.org/git/?p=binutils-gdb.git;a=blob_plain;f=opcodes/mips-opc.c;hb=HEAD
- PCSX2, `pcsx2/MMI.cpp` and `pcsx2/x86/iR5900Misc.cpp` (behavioral cross-check only): https://github.com/PCSX2/pcsx2
- chaoticgd, *ghidra-emotionengine-reloaded*, `data/languages/eecore.sinc`: https://github.com/chaoticgd/ghidra-emotionengine-reloaded
