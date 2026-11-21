# ADR 0180: Model evidenced VU0 CFC2/CTC2 control registers (runtime ABI 29)

- **Status:** Accepted (bounded control-file storage; VU operation remains unmodelled)
- **Date:** 2026-11-22
- **Scope:** COP2 decoder/IR/C++ emission for CFC2/CTC2 formats 2/6;
  `Vu0State::control`, control register helpers; ABI 28 -> 29; decoder,
  lifting, codegen, runtime tests; probe evidence in `docs/RESEARCH.md`.
- **Related:** ADR 0078 (VU0 data transfers), ADR 0179 (VIF1 reset/error
  storage), ADR 0175–0178 (GS storage/CRT syscall).

## Context

ABI-28 probe `abi28-03` (Downhill Domination USA, same ISO/ELF digests,
5,000,000 instruction budget, 16 batches) passed VIF1_FBRST and VIF1_ERR
writes at `pc=0x00243318` and `pc=0x00243324`, then stopped at `pc=0x00243330`
with `unsupported_instruction` on word `0x4844E000`.

Decode: primary opcode `0x12` = COP2, format bits 25..21 = 2, `rt=4`, `rd=28`,
reserved low bits zero: `CFC2 $a0, $28`. Next guest instructions are
`CTC2 $a0, $28` (`0x48C4E000`), `CFC2 $a0, $29` (`0x4844E800`), and further
VU0 control transfers. These are control-register transfers, not COP0 and not
R5900 control registers. The earlier ABI-28 notes that called this COP0 MTC0
were incorrect: COP0 is primary opcode `0x10`; this word is COP2 primary `0x12`.

Evidence for control register indices:

- Sony **VU User’s Manual**, sections 5.1.3 and 5.5.5: VU control registers
  include Status, MAC, Clip, reserved, R, I, Q, reserved fields, TPC, CMSAR0,
  FBRST, VPU_STAT, reserved, CMSAR1. Public reference copy:
  <https://docs.alexrp.com/mips/ee_vu.pdf>.
- Independently authored Ghidra EE language definition `cop2.sinc` assigns
  COP2 register indices 16=Status, 17=MAC, 18=Clip, 20=R, 21=I, 22=Q,
  26=TPC, 27=CMSAR0, 28=FBRST, 29=VPU_STAT, 31=CMSAR1:
  <https://github.com/chaoticgd/ghidra-emotionengine-reloaded/blob/c709245e/data/languages/cop2.sinc>.
- PCSX2’s `VU.h` names the same register indices (`REG_FBRST=28`,
  `REG_VPU_STAT=29`) and `VU0micro.cpp` stores VU operation state there. Used
  only as behavioral cross-check, not copied implementation:
  <https://github.com/PCSX2/pcsx2/blob/master/pcsx2/VU.h>.

## Decision

1. Decode COP2 format 2 as CFC2 and format 6 as CTC2 only when bits 10..0 are
   zero; preserve fail-closed results for malformed encodings, BC2, and VU
   arithmetic formats.
2. Add bounded 32-word VU0 control storage in `Vu0State`. `CFC2` reads the
   selected word and sign-extends it to the GPR; writes to GPR0 are suppressed.
   `CTC2` stores the low 32-bit GPR word to selected control slot.
3. Implement storage semantics for the observed FBRST (28) and VPU_STAT (29)
   control transfers. This does **not** reset VIF/VU, launch/stop a microprogram,
   publish DMA completion, or raise interrupts. No inferred asynchronous side
   effects are introduced. Other control indices are not admitted until they
   have dedicated semantics and evidence.
4. Bump runtime ABI 28 -> 29 because `Vu0State` changes `sizeof(EeState)` and
   generated code gains new runtime-visible state behavior.
5. Keep whole-file decoder support bounded to COP2 control transfer encodings;
   unsupported VU micro/macro instructions remain explicit failures.

## Consequences and limits

- The ABI-29 probe should pass the CFC2/CTC2 access to control register 28 and
  read register 29 without fabricating FBRST reset or VPU execution behavior.
- Registers remain storage-only, so any following VU-status polling may still
  stop or loop. That next observed behavior will determine whether additional
  VU state is supportable; this ADR does not claim VU execution.
- No boot-stage or compatibility-state promotion follows from this bounded
  capability.

## Validation

- Exact synthetic decode, IR, codegen, helper, and generated-code tests for
  CFC2/CTC2 including GPR0, alias, and reserved-field cases.
- Rust fmt, clippy, and workspace tests; CMake Release and full CTest.
- Fresh ABI-29 Downhill probe recorded in `docs/RESEARCH.md`.