# ADR 0078: VU0 Macro-Mode Quadword Data Movement

- **Status:** Accepted
- **Date:** 2026-11-07
- **Scope:** R5900 decoder, instruction effects, typed IR, deterministic C++20 AOT backend, EE runtime ABI, syscall guest-handler context frames, and native regression fixture

## Context

The Downhill Domination (SCUS_971.77) native probe with both observed relocated-code regions stopped at `0x00169EC4`, raw instruction `0xF8800010`: `SQC2 $vf0, 0x10($a0)`. Its containing function writes a 128-bit vector constant into a guest structure. The previous boundary was at the start of the relocated alarm code; the second relocated region exposed the next path. A RAM census recorded 3,124 LQC2 and 1,778 SQC2 encodings, making vector data movement a demonstrated execution blocker rather than an arbitrary opcode expansion.

## Evidence and semantics

The PS2 EE instruction encoding assigns primary opcode `0x36` to LQC2 and `0x3E` to SQC2. They use the ordinary 16-bit signed displacement plus the low 32 bits of the base GPR; the runtime reuses the checked EE quadword accessors, which align the effective address down to a 16-byte boundary. LQC2 transfers all 128 memory bits to the selected VU0 vector register; SQC2 transfers all 128 vector bits to memory. The R5900/EE hardware references and PS2SDK headers provide the instruction/register context; PCSX2 is an independent behavioral cross-check only, not a normative source. No VU arithmetic or VI-register semantics are inferred by this decision.

VF0 is architecturally constant `(0.0f, 0.0f, 0.0f, 1.0f)`. The VU vector layout maps X/Y/Z/W to successive 32-bit words in increasing bit significance. In the runtime's `Gpr128` convention, the low 64 bits therefore hold X and Y, and the high 64 bits hold Z and W; VF0 is `{low=0, high=0x3F80000000000000}`. Writes to VF0 are ignored. The same behavior applies whether the access is a vector arithmetic source or a data movement; only data movement is implemented here.

## Decision

- Decode LQC2/SQC2 only at primary opcodes `0x36`/`0x3E`; lift to explicit `LoadVu0Quadword`/`StoreVu0Quadword` IR operations with a typed VF index.
- Add a 32-register, 128-bit VU0 VF file to `EeState`. Access passes through `vu0_read_vf`/`vu0_write_vf` so VF0 cannot become writable storage.
- Use the shared checked `load_u128`/`store_u128` helpers. Do not reinterpret these as GPR quadword transfers.
- Preserve VU0 state across translated guest syscall-handler calls, consistent with the existing handler-frame policy that restores user architectural state while retaining the handler's `$v0` result.
- Track VF source/destination dependencies and memory/exception effects. A store from VF0 reads constant register 0; a load into VF0 has no VF write dependency.
- Add a synthetic native test for VF0 constant stores, VF5 round-trip, load-to-VF0 ignored, and the generated memory path.

## Alternatives rejected

- Treating COP2 opcodes as unsupported after identifying them would leave the observed real-title blocker in place.
- Mapping VU registers to EE GPRs or FPRs would conflate independent architectural files.
- Implementing a mutable VF0 slot would make the stored constant depend on initialization and prior writes.
- Adding VU arithmetic, VI registers, VIF, or micro-mode behavior without an observed requirement would exceed the evidence and this bounded slice.
- Ignoring memory faults or returning success for unsupported VU operations would fabricate guest behavior.

## Validation

Rust decoder/effect/lifting tests assert the exact encodings, operands, VF0 dependency behavior, and signed displacement handling. `reps2_cop2_codegen_smoke` executes generated native code against a synthetic memory span and checks literal 16-byte results. Full Windows/MSVC CMake/CTest and Rust quality gates are run for the implementation.

## Limitations

Only LQC2/SQC2 and the VF file are represented. VU arithmetic, VI integer registers, VU0 micro mode, VU1, COP2 control registers, pipeline/timing, and VU interrupts remain unsupported and must continue to stop with explicit diagnostics. The synthetic test proves these two instruction forms only; it does not establish that Downhill Domination boots or that vector execution is generally supported.
