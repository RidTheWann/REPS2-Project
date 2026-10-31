# Research references and provenance policy

This file identifies public technical materials and external projects that may guide independent implementation. It does not claim that every behavior has already been verified. Record the exact document revision, page/section, test vector, and observed result for each non-obvious behavior implemented.

## Primary technical references

- MIPS32 Architecture for Programmers, Volume II instruction definitions: https://pages.hmc.edu/harris/class/e155/11/MIPS32InstructionSet.pdf. Use it for general instruction behavior, including zero-extension of logical immediates. Cross-check PS2 EE/R5900-specific restrictions before treating base MIPS behavior as sufficient.
- PS2 hardware manuals and research documentation: https://github.com/4kbyter/ps2docs and https://github.com/ninjadynamics/PS2Docs. Verify each document's revision, provenance and redistribution status. Do not commit or redistribute restricted manuals.
- MIPS ELF ABI: https://refspecs.linuxfoundation.org/elf/mipsabi.pdf. Standard MIPS ELF conventions must be cross-checked with PS2-specific toolchain output.
- Rust language and safety references: https://doc.rust-lang.org/book/ and https://doc.rust-lang.org/nomicon/ffi.html.
- PS2 EE virtual/physical memory map: https://www.psdevwiki.com/ps2/Memory_Map. Cross-check the listed uncached and accelerated main-RAM aliases and device ranges against the selected EE execution mode.

## External projects for architectural study and behavioral testing

- PS2 Web Recomp: https://github.com/snuri00/ps2-web-recomp. Study the separation between EE AOT translation, code exploration, runtime support, vector-program compilation, and graphics execution.
- PS2Recomp: https://github.com/ran-j/PS2Recomp. Study its experimental ELF/Ghidra/configuration/code-generation workflow and documented limitations.
- PCSX2: https://github.com/PCSX2/pcsx2. May be used as an external behavioral reference and differential-test oracle. Do not copy or adapt its implementation code.

These projects have different goals and different completion levels. Their demos and feature lists do not establish equivalent REPS2 capability.

## Semantic discrepancy log

- **ORI immediate extension:** the public MIPS32 instruction reference defines ORI's 16-bit immediate as zero-extended. The currently consulted PCSX2 R5900 implementation also combines the source GPR with the zero-extended 16-bit immediate. One publicly accessible mirror of the Sony EE Core Instruction Set Manual contains prose describing ORI's immediate as sign-extended. REPS2 follows the MIPS instruction definition and observed PCSX2 behavior for this subset, records the discrepancy, and requires a future differential conformance case before expanding claims.

## Control-flow and delay-slot references

- MIPS32 Volume II defines the standard delayed-control-transfer instruction forms and branch offset calculation. Use it as a baseline only; validate EE-specific behavior and the supported R5900 encoding subset with PS2 documentation and differential tests.
- REPS2's initial CFG keeps the branch instruction and delay slot separate from block-body instructions. This is a representation decision that prevents a linear scalar IR from accidentally treating a delay slot as ordinary fallthrough.
- Indirect jump targets are unresolved unless statically proven; no target is guessed from a register name or common ABI convention.

## Control-flow semantic cross-checks

- MIPS delayed-control-transfer references describe the single instruction delay slot and PC+8 link value for link branches. The generic ISA is a baseline, not a claim of complete R5900 coverage.
- The PCSX2 R5900 dynamic-recompiler branch implementation is used only as an independent behavioural cross-check: BLTZAL/BGEZAL write the link before selecting the taken/fallthrough path, and the delay instruction is processed in either path for these non-likely variants. REPS2's code and tests are independently written.
- The CFG AOT dispatcher captures JALR's source target before the link write, then executes its delay slot before committing the next PC. This ordering is covered by synthetic tests and must remain explicit in future optimizations.
- Branch-likely/annul instructions are intentionally outside this first control-flow AOT subset; they must receive separate IR semantics and tests before decoding/lowering them as executable.

## EE memory and load/store cross-checks

- The public PS2 memory map identifies 32 MiB main RAM at virtual 0x00000000, an uncached alias at 0x20000000, and an accelerated alias at 0x30100000 for the upper 31 MiB. It separately identifies EE I/O registers, VU memories, privileged GS registers, IOP RAM, BIOS and scratchpad.
- PCSX2's R5900 load/store implementation is used only as an independent behavioural reference for the current subset: LB/LH/LW sign-extend into the low 64-bit GPR lane; LBU/LHU/LWU zero-extend; SB/SH/SW transfer only their selected low bits; halfword and word accesses check natural alignment. REPS2's implementation and synthetic tests are independently authored.
- PCSX2's virtual-memory documentation notes that EE mappings depend on COP0/TLB state and that the memory map is more complex than the direct aliases shown in simplified tables. REPS2's initial memory slice deliberately does not claim to implement TLB remapping or the complete physical bus map.
- The generated backend performs effective-address arithmetic in 32-bit unsigned arithmetic after sign-extending the 16-bit offset. The CPU memory helper validates alignment and backing bounds before access. Device/reserved regions are surfaced as unsupported, never mapped to ordinary RAM.
- The public MIPS64 Architecture for Programmers, Volume I-A, Revision 6.01 (2014), §§5.1.2–5.1.3 identifies LD/SD as CPU doubleword load/store operations and documents natural-alignment requirements for these instructions before Release 6 (§5.1.3). It is used as the general ISA baseline; EE-specific address translation, 128-bit GPR lane effects, and exceptions remain bounded by REPS2's explicit runtime model and require target-specific conformance tests.

## ELF load-image policy

- REPS2 currently constructs an initial memory image only for little-endian MIPS ET_EXEC files whose PT_LOAD virtual addresses can be translated using the documented default 32 MiB EE main-RAM aliases.
- Segment placement is performed in physical RAM coordinates. A pair of virtual aliases that overlap physically is rejected rather than relying on program-header order.
- PT_LOAD file bytes are copied and BSS tails remain zero. The generated source gets a fixed ELF entry point; the companion raw image is a full 32 MiB snapshot.
- REL/RELA entries targeting SHF_ALLOC sections are rejected until their specific R_MIPS relocation semantics are implemented and differential-tested. Debug/non-allocated relocations do not change the executable image.
- ET_DYN, relocatable objects, expansion RAM, custom TOOL mappings, COP0/TLB mappings, and segments outside default main RAM are not silently accepted.

## Provenance rules

1. REPS2 source is independently authored.
2. Public specifications may be cited. Keep source notes adjacent to non-obvious semantics.
3. External tools may be queried for observable behavior; record test inputs and outputs, not copied implementation code.
4. Do not add game ISOs, BIOS images, proprietary SDKs, copyrighted assets, or extracted retail code to this repository.
5. Synthetic fixtures must be authored by REPS2 contributors or generated by tests.
6. If references disagree, record the disagreement and create a minimal reproducible test before choosing behavior.
