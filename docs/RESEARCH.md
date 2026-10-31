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

## ISO 9660 and PS2 boot-file discovery

- ECMA-119 (ISO 9660), 6th edition, December 2025, is the current public volume/file-structure baseline ([Ecma International standard page](https://ecma-international.org/publications-and-standards/standards/ecma-119/)). Its descriptor-set model starts at logical sector 16 and includes a Primary Volume Descriptor, optional supplementary/partition/boot descriptors, and a descriptor-set terminator. REPS2 scans this ordered sequence, requires a PVD and terminator, verifies both-endian numerical fields, and currently accepts only a single-volume set with 2048-byte logical blocks.
- The public PS2 Developer Wiki's SYSTEM.CNF description documents BOOT2 as the CD/DVD executable path (for example `cdrom0:\\SCPS_110.04;1`) and identifies VER/VMODE as the other common metadata keys ([SYSTEM.CNF reference](https://www.psdevwiki.com/ps2/System.cnf)). A PS2 boot configuration example and open-source homebrew reader corroborate the BOOT2 key and the CDVD device path ([PS2-Engine example](https://github.com/nicholas477/PS2-Engine/blob/master/SYSTEM.CNF), [wLaunchELF parser](https://github.com/ps2homebrew/wLaunchELF/blob/master/main.c)). REPS2 prefers BOOT2 and retains BOOT as a fallback; HDD-specific PFS targets are rejected by this optical ISO workflow.
- The parser reads descriptor sectors, directory extents, SYSTEM.CNF, and the selected boot file through bounded seek/read operations. It does not load the full ISO into memory. Per-directory, path-depth, SYSTEM.CNF, and executable limits constrain allocations on untrusted images.
- Only the PVD namespace is consumed. Joliet supplementary name tables, Rock Ridge, UDF, path-table shortcuts, multi-volume spanning, multi-extent concatenation, interleaved data, raw 2352-byte BIN/CUE sector decoding, CHD containers, and CDVD device behavior are explicit non-goals for this increment.
- The boot extraction command validates the selected file as ELF32 little-endian EM_MIPS ET_EXEC before creating an output path with exclusive create semantics. Extraction is not evidence that PT_LOAD placement, relocations, instruction coverage, or a retail title is supported by the current AOT backend.

## EE memory and load/store cross-checks

- The public PS2 memory map identifies 32 MiB main RAM at virtual 0x00000000, an uncached alias at 0x20000000, and an accelerated alias at 0x30100000 for the upper 31 MiB. It separately identifies EE I/O registers, VU memories, privileged GS registers, IOP RAM, BIOS and scratchpad.
- PCSX2 1.6.0's R5900 load/store implementation is used only as an independent behavioural reference for the bounded subset ([source](https://sources.debian.org/src/pcsx2/1.6.0%2Bdfsg-2/pcsx2/R5900OpcodeImpl.cpp/)): LB/LH/LW sign-extend into the low 64-bit GPR lane; LBU/LHU/LWU zero-extend; SB/SH/SW transfer only their selected low bits; halfword and word accesses check natural alignment. For LD/SD, the interpreter forms the effective address from the low 32-bit base plus signed immediate, checks 8-byte alignment, transfers only GPR lane 0, and still performs an LD access for GPR0 through a dummy destination (LD around lines 1767–1775; SD around lines 1896–1903). This emulator source is a non-normative implementation cross-check, not a hardware specification; REPS2's implementation and synthetic tests are independently authored.
- PCSX2's virtual-memory documentation notes that EE mappings depend on COP0/TLB state and that the memory map is more complex than the direct aliases shown in simplified tables. REPS2's initial memory slice deliberately does not claim to implement TLB remapping or the complete physical bus map.
- The generated backend performs effective-address arithmetic in 32-bit unsigned arithmetic after sign-extending the 16-bit offset. The CPU memory helper validates alignment and backing bounds before access. Device/reserved regions are surfaced as unsupported, never mapped to ordinary RAM.
- The public MIPS64 Architecture for Programmers, Volume I-A, Revision 6.01 (2014), §§5.1.2–5.1.3 identifies LD/SD as CPU doubleword load/store operations and documents natural-alignment requirements for these instructions before Release 6 (§5.1.3). It is used as the general ISA baseline; EE-specific address translation, 128-bit GPR lane effects, and exceptions remain bounded by REPS2's explicit runtime model and require target-specific conformance tests.

## EE left/right partial load/store cross-checks

- MIPS64 Architecture for Programmers, Volume I-A, Revision 5.04 and Volume II instruction definitions classify LWL/LWR/SWL/SWR and LDL/LDR/SDL/SDR as complementary left/right partial transfers. The left/right pair is used to assemble or store unaligned words and doublewords without requiring an unaligned host-language access.
- The current PCSX2 R5900 interpreter is used only as a non-normative behavioural cross-check ([source](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp)); the older 1.6.0 source is available at [Debian Sources](https://sources.debian.org/src/pcsx2/1.6.0%2Bdfsg-2/pcsx2/R5900OpcodeImpl.cpp/). The implementation records the byte-offset-dependent masks and shifts for all eight operations. It also shows the EE-specific word-load detail: LWL sign-extends its merged 32-bit result, while LWR sign-extends at offset zero and otherwise preserves the upper 32 bits of GPR lane 0. Partial word/doubleword operations do not change the upper 64-bit EE GPR lane.
- REPS2 derives the byte offset from the low address bits, aligns the backing read down to a 4-byte or 8-byte unit, and performs explicit byte-lane merging. Partial stores read the original aligned unit, merge the selected source bytes, and use the normal checked aligned store helper. The full aligned unit must fit the caller-owned main-RAM span before guest state or memory is modified.
- The public MIPS64 Architecture for Programmers, Volume I-A, Revision 6.01 (2014), §§5.1.2–5.1.3 remains the general ISA reference; R5900 GPR lane behaviour is cross-checked independently and is not inferred solely from the generic MIPS64 description.
- These operations remain scoped to the current direct/uncached/accelerated/KSEG main-RAM aliases. No COP0/TLB remapping, cache/coherency, MMIO, bus exception delivery, or complete hardware compatibility is claimed.

## EE HI/LO and R5900 multiply cross-checks

- QEMU's R5900-specific implementation description defines `MULT[U] rd, rs, rt` and its state transfer as `(rd, LO, HI) <- rs * rt`: the low 32-bit result is written to both `rd` and LO, and the high 32-bit result is written to HI. Omitting `rd` means register zero, but HI/LO still update. The implementation captures both sources before writing any destination: [QEMU R5900 three-operand MULT/MULTU patch](https://patchew.org/QEMU/cover.1540134918.git.noring@nocrew.org/ff1edd3989b06e9cae67c962cec79081e4d7af0a.1540134918.git.noring@nocrew.org/).
- The associated QEMU TCG test checks several positive and boundary products, including `17 * 19`, `77773 * 99991`, `12207031 * 305175781`, and the INT32_MIN edge cases. REPS2's independent native smoke fixture now includes the INT32_MIN/INT32_MAX combinations, destination-zero behavior, and source/destination aliasing: [QEMU three-operand MULT test](https://patchew.org/QEMU/cover.1540134918.git.noring@nocrew.org/b72eb39dd9c465d110e3df2e3660c32c979d0e4d.1540134918.git.noring@nocrew.org/).
- PCSX2's R5900 interpreter independently shows `MULT` multiplying the low 32-bit signed operands and `MULTU` multiplying the low 32-bit unsigned operands; both sign-extend each 32-bit result lane into the 64-bit HI/LO representation and copy LO into non-zero `rd`. It also documents base `MFHI/MFLO/MTHI/MTLO` transfers: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp. This is a non-normative behavioral reference, not a hardware specification.
- REPS2 stores only base EE HI/LO in the current runtime ABI. `MFHI/MFLO` write GPR[63:0] while preserving GPR[127:64]; `MTHI/MTLO` read GPR[63:0]. Signed and unsigned multiplication use the low 32 bits of the input GPRs; the 32-bit high/low product lanes are sign-extended into their 64-bit destinations as cross-checked above.
- This increment does not model multiply pipeline latency, HI1/LO1, MMI pipeline-1 operations, MADD/MADDU, MULT1/MULTU1, DIV/DIVU semantics, or exception timing. The public implementation references note disagreements over DIVU sign-extension and special division cases; those operations stay out of the executable IR until separately researched and differential-tested.


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
