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
- The R5900 branch-likely family evaluates its predicate first. A taken branch executes the delay-slot instruction before transferring to the target; a not-taken branch annuls the delay slot and continues at PC+8. BLTZALL/BGEZALL write the PC+8 link address to GPR[31] independently of the predicate. The decoder recognizes BEQL, BNEL, BLEZL, BGTZL, BLTZL, BGEZL, BLTZALL, and BGEZALL, and the CFG/IR/codegen path carries the annulment policy explicitly as `AnnulWhenNotTaken`.
- The CFG AOT dispatcher captures JALR's source target before the link write, then executes its delay slot before committing the next PC. This ordering is covered by synthetic tests and must remain explicit in future optimizations.
- Native smoke tests exercise taken and not-taken branch-likely paths, including absence of delay-slot side effects after annulment and the unconditional link value for BLTZALL. These establish the implemented subset only; hardware differential testing remains future work.

## ISO 9660 and PS2 boot-file discovery

- ECMA-119 (ISO 9660), 6th edition, December 2025, is the current public volume/file-structure baseline ([Ecma International standard page](https://ecma-international.org/publications-and-standards/standards/ecma-119/)). Its descriptor-set model starts at logical sector 16 and includes a Primary Volume Descriptor, optional supplementary/partition/boot descriptors, and a descriptor-set terminator. REPS2 scans this ordered sequence, requires a PVD and terminator, verifies both-endian numerical fields, and currently accepts only a single-volume set with 2048-byte logical blocks.
- The public PS2 Developer Wiki's SYSTEM.CNF description documents BOOT2 as the CD/DVD executable path (for example `cdrom0:\\SCPS_110.04;1`) and identifies VER/VMODE as the other common metadata keys ([SYSTEM.CNF reference](https://www.psdevwiki.com/ps2/System.cnf)). A PS2 boot configuration example and open-source homebrew reader corroborate the BOOT2 key and the CDVD device path ([PS2-Engine example](https://github.com/nicholas477/PS2-Engine/blob/master/SYSTEM.CNF), [wLaunchELF parser](https://github.com/ps2homebrew/wLaunchELF/blob/master/main.c)). REPS2 prefers BOOT2 and retains BOOT as a fallback; HDD-specific PFS targets are rejected by this optical ISO workflow.
- The parser reads descriptor sectors, directory extents, SYSTEM.CNF, and the selected boot file through bounded seek/read operations. It does not load the full ISO into memory. Per-directory, path-depth, SYSTEM.CNF, and executable limits constrain allocations on untrusted images.
- Only the PVD namespace is consumed. Joliet supplementary name tables, Rock Ridge, UDF, path-table shortcuts, multi-volume spanning, multi-extent concatenation, interleaved data, raw 2352-byte BIN/CUE sector decoding, CHD containers, and CDVD device behavior are explicit non-goals for this increment.
- The boot extraction command validates the selected file as ELF32 little-endian EM_MIPS ET_EXEC before creating an output path with exclusive create semantics. Extraction is not evidence that PT_LOAD placement, relocations, instruction coverage, or a retail title is supported by the current AOT backend.

## EE MMI0 PEXT5/PPAC5 color conversion

- Normative encoding/bit concatenation: EE Core Instruction Set Manual v6.0, printed pages 202 (PEXT5) and 254 (PPAC5); see [ADR 0084](decisions/0084-ee-mmi0-pext5-ppac5.md). The manual was consulted outside Git, not redistributed.
- Non-normative cross-check: [PCSX2 MMI.cpp at d983b2b](https://github.com/PCSX2/pcsx2/blob/d983b2b0665c40acb0455433eb656eebcb9efaf2/pcsx2/MMI.cpp), four independent words and destination-zero suppression. No third-party implementation was copied.
- Synthetic fixture words include `0x70021F88` (PEXT5 r3,r2), `0x70033FC8` (PPAC5 r7,r3), and `0x700127C8` (PPAC5 r4,r1), at guest PC `0xD000`. Source bits are routed independently using the manual's concatenation positions, covering all 128 bits and discarded bits. The CFG fixture at `0xD200` covers taken/annulled likely and return delays in both output layouts.
- Continued remote implementation `b5c3140` on base `eca41b5`; review corrected a PEXEW sibling expectation, a fixture destination encoding, and an entry-GPR0 postcondition. Verified by synthetic generated-code native fixtures locally on Windows/MSVC (Release CTest 33/33) and in Ubuntu/Linux CI (CMake build and CTest); Rust frontend checks additionally verified on Rust MSRV 1.88 in CI. This is synthetic semantic evidence, not hardware differential validation or retail boot evidence.

## ELF PT_LOAD BSS coalescing and overlap validation

- Source-derived defect at base `eca41b50332a536194ace65ca3f1d4785b335d26`: `elf::load_segments` enlarged an existing same-base pure-BSS PT_LOAD and updated range accounting without checking collisions with other PT_LOAD ranges. Header order `[BSS A, initialized B, expanded A]` accepted overlapping virtual ranges, while another order rejected them. The synthetic regression uses A=`[0x00100010,0x00100014)`, B=`[0x00100020,0x00100024)`, then extends A to `0x00100040`; the defect was reproduced before correction.
- Fix validates the proposed maximal BSS range against all other admitted ranges before allocating or mutating state. Tests cover all six program-header permutations, initialized/BSS B, legal exact adjacency, zero fill, merged permissions, and load-size boundaries. This establishes loader consistency only; existing RAM-image mapping has separate validation.

## EE memory and load/store cross-checks

- The public PS2 memory map identifies 32 MiB main RAM at virtual 0x00000000, an uncached alias at 0x20000000, and an accelerated alias at 0x30100000 for the upper 31 MiB. It separately identifies EE I/O registers, VU memories, privileged GS registers, IOP RAM, BIOS and scratchpad.
- PCSX2 1.6.0's R5900 load/store implementation is used only as an independent behavioural reference for the bounded subset ([source](https://sources.debian.org/src/pcsx2/1.6.0%2Bdfsg-2/pcsx2/R5900OpcodeImpl.cpp/)): LB/LH/LW sign-extend into the low 64-bit GPR lane; LBU/LHU/LWU zero-extend; SB/SH/SW transfer only their selected low bits; halfword and word accesses check natural alignment. For LD/SD, the interpreter forms the effective address from the low 32-bit base plus signed immediate, checks 8-byte alignment, transfers only GPR lane 0, and still performs an LD access for GPR0 through a dummy destination (LD around lines 1767–1775; SD around lines 1896–1903). This emulator source is a non-normative implementation cross-check, not a hardware specification; REPS2's implementation and synthetic tests are independently authored.
- PCSX2's virtual-memory documentation notes that EE mappings depend on COP0/TLB state and that the memory map is more complex than the direct aliases shown in simplified tables. REPS2's initial memory slice deliberately does not claim to implement TLB remapping or the complete physical bus map.
- The generated backend performs effective-address arithmetic in 32-bit unsigned arithmetic after sign-extending the 16-bit offset. The CPU memory helper validates alignment and backing bounds before access. Device/reserved regions are surfaced as unsupported, never mapped to ordinary RAM.
- The public MIPS64 Architecture for Programmers, Volume I-A, Revision 6.01 (2014), §§5.1.2–5.1.3 identifies LD/SD as CPU doubleword load/store operations and documents natural-alignment requirements for these instructions before Release 6 (§5.1.3). It is used as the general ISA baseline; EE-specific address translation, 128-bit GPR lane effects, and exceptions remain bounded by REPS2's explicit runtime model and require target-specific conformance tests.

## EE VU0 quadword data movement

- Downhill Domination SCUS_971.77 probe-r2, using both explicit relocated-code regions, stopped at guest PC `0x00169EC4` on raw word `0xF8800010` (`SQC2 $vf0, 0x10($a0)`) after 3,126,787 guest instructions. The containing routine stores a constant vector into a caller-owned structure. A static RAM census found 3,124 LQC2 and 1,778 SQC2 words. This is title-specific execution evidence, not proof of boot.
- The public PS2 EE Core instruction references identify LQC2/SQC2 as quadword transfers between EE memory and VU0 vector registers; PS2SDK headers provide the VU vector register naming/context. The encoding uses primary opcodes `0x36`/`0x3E`. PCSX2's R5900/VU0 implementation is useful as a separate behavioral cross-check but is not treated as a hardware specification or copied. These sources establish only the two transfer forms addressed by ADR 0078; VU arithmetic, VI registers, micro mode, and timing remain out of scope.
- VU0 VF0 is the architectural constant `(0.0f, 0.0f, 0.0f, 1.0f)`, writes are ignored, and vector words are represented in increasing X/Y/Z/W order. The runtime represents the 128-bit transfer as two little-endian 64-bit lanes, placing W's IEEE single-precision bit pattern `0x3F800000` in bits 96..127. This layout has a synthetic native test for store and reload. It has not yet been checked against hardware or an independent title trace.
- REPS2 uses existing checked aligned quadword RAM helpers for address resolution and transfers. Unsupported COP2 arithmetic remains an explicit stop; LQC2/SQC2 support does not imply VU program execution.

## Downhill Domination DMAC/SIF boundary

- A rebuilt post-COP2 probe using the local SCUS_971.77 ELF and relocated regions `0x80076000 <- 0x002829F8 (0x740)` and `0x80075000 <- 0x002831A0 (0x330)` reaches `0x0025C274` after 3,168,708 executed guest instructions, then stops with `unsupported_memory_access`. The local ELF SHA-256 is `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`; the trace budget is 5,000,000. This is not a boot stage.
- The translated path builds `0x1000E010` (`lui $v0,0x1000; ori $v0,$v0,0xE010`) and executes `lw $v1,0($v0)`. PS2SDK `common/include/ee_regs.h` defines `A_EE_D_STAT` at `0x1000e010`; its DMA register documentation names it the DMAC status register. Following code masks `$v1` with `0x20`, the SIF0 status bit, and conditionally writes the value back to D_STAT.
- PS2SDK's `ee/kernel/src/sifcmd.c` independently names `0x1000e010` `DMAC_COMM_STAT` and `0x20` `STAT_SIF0`. In `sceSifInitCmd`, it reads D_STAT, writes `STAT_SIF0` back when present, then checks SIF0 channel control register `0x1000c000` for `CHCR_STR` `0x100`; when the channel is not running, it starts the SIF0 DMA chain. This is evidence for the SDK initialization pattern, not proof that the retail target has the same initial event or is executing this exact source sequence.
- PS2Tek's public EE register map describes D_STAT channel status bits 0..9 as write-one-to-clear, channel interrupt masks in bits 16..25 as write-one-to-toggle, and the stall/MFIFO mask fields in bits 29..30. It separately identifies status fields in bits 13..15; the present runtime does not claim write semantics for those fields. The channel-5 SIF0 status flag is bit 5 (`0x20`), and its interrupt-mask field is bit 21.
- PCSX2's legacy `Dmac.h` independently corroborates the named D_STAT status/mask fields, including SIS/MEIS/BEIS and SIM/MEIM. This is a non-normative implementation cross-check, not a hardware specification and not copied implementation code.
- ADR 0079 introduces an isolated `EeDmacState` owner in `runtime/include/reps2/runtime/ee_dmac.hpp`. A default state is explicitly unknown, not a declared hardware reset value. For an explicitly seeded known value, the model reads documented fields, returns reserved bits as zero, applies W1C to bits 0..9, and toggles mask fields 16..25/29..30 on writes of one. Writes setting bits 13..15 and every access while the state is unknown remain `unsupported_memory_access` without partial mutation.
- A generated native smoke test covers the exact guest `LW/ANDI 0x20/SW` pattern with an explicitly seeded synthetic state and verifies that SIF0 bit 5 is cleared without changing unrelated fields. This establishes the bounded register transform only. It does not seed production state, generate DMA events, emulate SIF0/IOP, model CHCR, raise interrupts, or advance the real-title probe.
- Remaining unknowns: the actual D_STAT value when this title reaches the read; which target path or device event sets SIF0 bit 5; whether the SIF0 DMA channel is already running; required CHCR and IOP synchronization; and which service or guest-visible stage follows the register test. Keep production state unknown until these facts are evidenced. Do not return fabricated zero, invent a pending bit, or claim the clear completed as a real device action.

References:
- PS2Tek EE register map: https://israpps.github.io/ps2tek/
- PS2SDK SIF initialization: https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/sifcmd.c
- PCSX2 legacy DMAC field-layout cross-check: https://sources.debian.org/src/pcsx2/1.6.0%2Bdfsg-2/pcsx2/Dmac.h

## EE left/right partial load/store cross-checks

- MIPS64 Architecture for Programmers, Volume I-A, Revision 5.04 and Volume II instruction definitions classify LWL/LWR/SWL/SWR and LDL/LDR/SDL/SDR as complementary left/right partial transfers. The left/right pair is used to assemble or store unaligned words and doublewords without requiring an unaligned host-language access.
- The current PCSX2 R5900 interpreter is used only as a non-normative behavioural cross-check ([source](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp)); the older 1.6.0 source is available at [Debian Sources](https://sources.debian.org/src/pcsx2/1.6.0%2Bdfsg-2/pcsx2/R5900OpcodeImpl.cpp/). The implementation records the byte-offset-dependent masks and shifts for all eight operations. It also shows the EE-specific word-load detail: LWL sign-extends its merged 32-bit result, while LWR sign-extends at offset zero and otherwise preserves the upper 32 bits of GPR lane 0. Partial word/doubleword operations do not change the upper 64-bit EE GPR lane.
- REPS2 derives the byte offset from the low address bits, aligns the backing read down to a 4-byte or 8-byte unit, and performs explicit byte-lane merging. Partial stores read the original aligned unit, merge the selected source bytes, and use the normal checked aligned store helper. The full aligned unit must fit the caller-owned main-RAM span before guest state or memory is modified.
- The public MIPS64 Architecture for Programmers, Volume I-A, Revision 6.01 (2014), §§5.1.2–5.1.3 remains the general ISA reference; R5900 GPR lane behaviour is cross-checked independently and is not inferred solely from the generic MIPS64 description.
- These operations remain scoped to the current direct/uncached/accelerated/KSEG main-RAM aliases. No COP0/TLB remapping, cache/coherency, MMIO, bus exception delivery, or complete hardware compatibility is claimed.

## R5900 trapping 64-bit addition and subtraction

- PCSX2's R5900 interpreter implements `DADD`, `DSUB`, and `DADDI` as signed 64-bit operations with integer-overflow exceptions. Its handlers compute and check overflow before suppressing writes to GPR[0], so an overflowing instruction still raises the exception when the destination field is zero: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp (see `_add64_Overflow`, `DADDI`, `DADD`, and `DSUB`).
- `DADDI` sign-extends its 16-bit immediate before the 64-bit addition. `DADDU`, `DSUBU`, and `DADDIU` remain separate modulo-2^64 operations that do not raise signed-overflow exceptions.
- REPS2 generated code computes result bits in `std::uint64_t` and uses sign-bit overflow identities for addition and subtraction. This avoids evaluating an overflowing signed C++ expression, which would invoke host-language undefined behavior. A signed overflow is delivered as EE Level-1 `Cause.ExcCode 12` through the shared exception helper before committing its destination register (ADR 0034).
- Native tests cover normal results, `INT64_MAX + 1`, `INT64_MIN - 1`, `INT64_MIN + sign_extend(-2)`, source/destination aliasing, prior-instruction commit behavior, and overflow with `rd = 0`.

## R5900 64-bit compare and NOR cross-checks

- PCSX2's R5900 interpreter compares the full low 64-bit GPR lane for `SLT` and `SLTU`, returning exactly 0 or 1 in that lane. `SLTI` compares the signed low 64-bit GPR against the sign-extended 16-bit immediate; `SLTIU` sign-extends the same immediate and then compares the bit pattern as unsigned 64-bit. `NOR` complements the 64-bit OR result. Reference: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp (SLTI/SLTIU around lines 2662–2663; NOR/SLT/SLTU around lines 2744–2748).
- The same implementation stores the result into GPR low lane only. REPS2 mirrors this register-lane boundary and does not modify GPR[127:64].
- Signed comparisons are lowered using C++20 `std::bit_cast<std::int64_t>` from the raw low-lane bits. This avoids relying on implementation-defined conversion of out-of-range unsigned values to signed integers. Unsigned comparisons remain `std::uint64_t` comparisons.
- The synthetic native fixture tests `INT64_MIN/MAX`, the signed/unsigned ordering reversal around bit 63, `SLTIU` with -1 and -32768, GPR[0] suppression, NOR's exact 64-bit result, and high-lane preservation. This establishes the tested subset only; it does not establish full R5900 compatibility.

## EE HI/LO and R5900 multiply cross-checks

- QEMU's R5900-specific implementation description defines `MULT[U] rd, rs, rt` and its state transfer as `(rd, LO, HI) <- rs * rt`: the low 32-bit result is written to both `rd` and LO, and the high 32-bit result is written to HI. Omitting `rd` means register zero, but HI/LO still update. The implementation captures both sources before writing any destination: [QEMU R5900 three-operand MULT/MULTU patch](https://patchew.org/QEMU/cover.1540134918.git.noring@nocrew.org/ff1edd3989b06e9cae67c962cec79081e4d7af0a.1540134918.git.noring@nocrew.org/).
- The associated QEMU TCG test checks several positive and boundary products, including `17 * 19`, `77773 * 99991`, `12207031 * 305175781`, and the INT32_MIN edge cases. REPS2's independent native smoke fixture now includes the INT32_MIN/INT32_MAX combinations, destination-zero behavior, and source/destination aliasing: [QEMU three-operand MULT test](https://patchew.org/QEMU/cover.1540134918.git.noring@nocrew.org/b72eb39dd9c465d110e3df2e3660c32c979d0e4d.1540134918.git.noring@nocrew.org/).
- PCSX2's R5900 interpreter independently shows `MULT` multiplying the low 32-bit signed operands and `MULTU` multiplying the low 32-bit unsigned operands; both sign-extend each 32-bit result lane into the 64-bit HI/LO representation and copy LO into non-zero `rd`. It also documents base `MFHI/MFLO/MTHI/MTLO` transfers: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp. This is a non-normative behavioral reference, not a hardware specification.
- REPS2 stores only base EE HI/LO in the current runtime ABI. `MFHI/MFLO` write GPR[63:0] while preserving GPR[127:64]; `MTHI/MTLO` read GPR[63:0]. Signed and unsigned multiplication use the low 32 bits of the input GPRs; the 32-bit high/low product lanes are sign-extended into their 64-bit destinations as cross-checked above.
- This increment does not model multiply pipeline latency, HI1/LO1, MMI pipeline-1 operations, MADD/MADDU, MULT1/MULTU1, DIV/DIVU semantics, or exception timing. The public implementation references note disagreements over DIVU sign-extension and special division cases; those operations stay out of the executable IR until separately researched and differential-tested.

## R5900 MMI PADDUW cross-checks

- PADDUW is encoded under primary opcode `0x1C` in the MMI1 group: the low six function bits are `0x28` and the group selector bits 10:6 are `0x10`. REPS2 additionally verifies both fields so another MMI group with the same low function bits is not mistaken for PADDUW.
- QEMU's R5900 instruction overview names PADDUW as "Parallel Add with Unsigned saturation Word": https://lists.nongnu.org/archive/html/qemu-devel/2018-10/msg04741.html. PCSX2's MMI interpreter performs four independent unsigned 32-bit additions and clamps each overflowing lane to `0xFFFFFFFF`: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp. These are non-normative behavioural references, not a hardware specification.
- REPS2 therefore updates all four 32-bit lanes of the destination GPR and does not let carry or saturation cross a lane boundary. The native fixture cross-checks a lane-0 overflow that saturates while the upper lanes retain ordinary sums, plus the hard-wired zero destination.
- Only PADDUW from the MMI families is implemented in this increment. Other MMI0–MMI3 operations, MMI pipeline-1 HI1/LO1 state, and MMI latency remain unsupported and are counted by the coverage profiler.

## R5900 MMI SA register and QFSRV cross-checks

- The EE Core Instruction Set Manual defines four SA-register movers, not the `MFSAS`/`MTSAS`/`MTSAC` spellings that circulate in some secondary notes: `MFSA` (`SPECIAL` funct `0x28`), `MTSA` (`SPECIAL` funct `0x29`), `MTSAB` (`REGIMM` function `0x18` in bits 20..16) and `MTSAH` (`REGIMM` function `0x19`). GNU binutils `opcodes/mips-opc.c` masks confirm the same field layout: `mfsa 0x00000028/0xffff07ff`, `mtsa 0x00000029/0xfc1fffff`, `mtsab 0x04180000/0xfc1f0000`, `mtsah 0x04190000/0xfc1f0000`. REPS2 reproduces those reserved-bit masks, so `MFSA` takes its destination from bits 15..11 while `MTSA` reserves bits 20..6.
- There is one SA register, and it holds a **bit count** rather than a byte index. `MTSA` transfers the whole `GPR[rs][63:0]` lane into it, and `MFSA` copies `SA` verbatim into `GPR[rd][63:0]` while leaving `GPR[127:64]` intact. The runtime ABI therefore stores `sa` as `std::uint64_t`; an earlier draft used a 32-bit field and silently truncated the `MTSA` source lane. The lane-width choice follows the manual and is not contradicted by any reference; the *scaling* of `MTSA` is contested and tracked in the two entries below.
- `MTSAB` computes `(GPR[rs][3:0] XOR imm[3:0]) * 8` and `MTSAH` computes `(GPR[rs][2:0] XOR imm[2:0]) * 16`. The scale factors are part of the documented operation, not an implementation detail: the maximum MTSAB value is 120 bits and the maximum MTSAH value is 112 bits, which matches a 16-byte quadword expressed in bytes and halfwords. Omitting the factor produces a value that only looks correct when SA is never read.
- **QFSRV is the only documented consumer of SA** (MMI group selector bits 10..6 = `0x1B` with funct `0x28`, so the full 11-bit selector is `0x6E8`; binutils `qfsrv 0x700006e8/0xfc0007ff`). It shifts the 256-bit funnel `GPR[rs]:GPR[rt]` right by `SA` bits and keeps the low 128 bits, which REPS2 lowers through `reps2::runtime::funnel_shift_right_vec`. Implementing the movers without QFSRV would have added unread processor state instead of an executable capability.
- Reference disagreement, recorded rather than hidden: the Sony manual scales at definition time (`SA = (...)*8`), while PCSX2 keeps `cpuRegs.sa` as a **byte count** (`std::uint32_t`) and multiplies by eight inside its QFSRV implementation (`u32 sa_amt = cpuRegs.sa << 3`, `pcsx2/MMI.cpp`). The two readings agree for `MTSAB` and `MTSAH`: PCSX2's `recMTSAB` stores `(GPR[rs][3:0] ^ imm[3:0])` and `recMTSAH` stores `((GPR[rs][2:0] ^ imm[2:0]) << 1)`, so the deferred `<< 3` reproduces the manual's `* 8` and `* 16`.
- **`MTSA` is where the references genuinely diverge, and REPS2 is not proven right there.** PCSX2's `recMTSA` writes only `GPR[rs][31:0] & 0xF` into SA, so a program that loads a bit count through `MTSA` and then executes `QFSRV` shifts by `(value & 0xF) * 8` bits in PCSX2 but by `value` bits in REPS2 - a difference of up to a factor of eight plus truncation, and it is observable through `MFSA` as well. REPS2 follows the manual's wording, which makes the *movers* consistent but leaves `MTSA` as the one SA operation with no second implementation agreeing on its effect. A differential conformance case against real titles is required before any compatibility claim depends on `MTSA`.
- Second open item: the manual does not state how `QFSRV` behaves when `MTSA` loads `SA >= 128`, and PCSX2's `cpuRegs.sa` masking makes the case unreachable in that implementation (`(value & 0xF) << 3` caps at 120). REPS2 evaluates the documented funnel formula literally, so shift counts above 127 continue to draw bits from `GPR[rs]` and `SA >= 256` yields zero. `docs/decisions/0019-mmi-sa-and-qfsrv.md` marks this specific sub-case as unverified; `MTSAB`/`MTSAH` cannot produce it.
- The manual additionally lists scheduling restrictions rather than encoding restrictions: `MTSA`/`MTSAB`/`MTSAH` are illegal in a branch-likely delay slot and must not follow an SA-touching instruction within three cycles. REPS2 does not model those timing constraints yet; the straight-line native fixture keeps the register roles disjoint so the tests do not depend on them.
- Coverage after this increment: one MMI0-3 group operation (PADDUW), the four SA movers, and QFSRV. All other MMI0-MMI3, COP0 and vector encodings stay unsupported and are still counted by `profile-elf`; the FPU population that used to dominate the unsupported `cop1` family is covered in the COP1 sections below.

## R5900 MMI2 PCPYLD cross-checks

- PCPYLD is encoded under primary opcode `0x1C` in the MMI2 group: the low six function bits are `0x09` and the operation selector in bits 10..6 is `0x0E`. REPS2 verifies both fields so the same selector in MMI0/MMI1/MMI3 or a neighboring MMI2 selector is not mistaken for PCPYLD.
- The operation is a pure lane copy: `GPR[rd][63:0] = GPR[rt][63:0]` and `GPR[rd][127:64] = GPR[rs][63:0]`. PCSX2's `pcsx2/MMI.cpp` implements those two assignments, writes the high half first to survive `rd` aliasing `rs` or `rt`, and returns without writing when `rd` is zero: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp. This is a non-normative behavioural reference, not a hardware specification.
- REPS2 therefore computes the complete 128-bit result in `parallel_copy_low_doubleword` before assigning it. The dedicated native fixture checks a non-aliasing destination, `rd == rs`, `rd == rt`, `rd == rs == rt`, and GPR[0] suppression; the both-alias case expects the original low doubleword in both halves.
- The seven-title reachable census counted 851 PCPYLD words, present in every title and ahead of POR (554) and PEXTLW (489). Coverage after this increment adds one MMI2 data-movement operation; all other MMI0–MMI3 operations, MMI pipeline-1 state, and MMI latency remain unsupported and are still counted by `profile-elf`.

## R5900 MMI3 POR cross-checks

- POR is encoded under primary opcode `0x1C` in the MMI3 group: the low six function bits are `0x29` and the operation selector in bits 10..6 is `0x12`. The group selector is **`0x29`, not `0x39`**: the MMI dispatch table routes function `0x28` to MMI1 and `0x29` to MMI3, which is what the existing `encoding_family` classification in `mips.rs` already assumed. REPS2 verifies both fields so selector `0x12` in MMI0 (PEXTLW), MMI2 (PAND) or MMI1, or a neighboring MMI3 selector, is not mistaken for POR.
- The operation is the plain 128-bit bitwise OR `GPR[rd][127:0] = GPR[rs][127:0] | GPR[rt][127:0]`. PS2Tek's MMI3 decoding table lists `POR` at selector `0x12` and `PNOR` at `0x13`, and QEMU's R5900/TX79 instruction tables mask the encoding to the major opcode plus bits 10..0, so the whole encoding is the two selectors plus the standard register fields and nothing else. PCSX2's `pcsx2/MMI.cpp` implements the 128-bit OR and, like every other MMI destination write, returns without storing when `rd` is zero: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp. These are non-normative behavioural references, not a hardware specification.
- REPS2 therefore computes the complete 128-bit result in `parallel_or` before assigning it. POR is commutative in a way PCPYLD is not, so the emitted operand order `(rs, rt)` only mirrors the IR and no guest-visible result depends on it. The dedicated native fixture checks a non-aliasing destination, `rd == rs`, `rd == rt`, `rd == rs == rt`, GPR[0] suppression and an untouched bystander register; compile-time assertions check the helper directly, including a split-half operand pair that a one-half implementation cannot reproduce.
- The same seven-title census placed POR second at 554 words after PCPYLD's 851. Coverage after this increment adds one MMI3 bitwise operation; PNOR, PEXCH, PEXCW, PCPYH, PCPYUD, PINTEH, the MMI3 multiply/divide group, PEXTLW and all other MMI0–MMI3 operations, MMI pipeline-1 state, and MMI latency remain unsupported and are still counted by `profile-elf`.

## R5900 MMI0 PEXTLW cross-checks

- PEXTLW is encoded under primary opcode `0x1C` in the MMI0 group: the low six function bits are `0x08` and the operation selector in bits 10..6 is `0x12`. REPS2 verifies both fields so selector `0x12` in MMI2 (`PAND`) or MMI3 (`POR`) is not mistaken for PEXTLW, and the neighboring MMI0 selectors `0x11` (PSUBSW), `0x13` (PPACW), `0x1A` (PEXTLB) and `0x1B` (PPACB) stay unknown.
- The operation interleaves the low words of both sources: `GPR[rd][31:0] = GPR[rt][31:0]`, `GPR[rd][63:32] = GPR[rs][31:0]`, `GPR[rd][95:64] = GPR[rt][63:32]` and `GPR[rd][127:96] = GPR[rs][63:32]`, so the result words are `[rt0, rs0, rt1, rs1]` and both source high doublewords are ignored. PCSX2's `pcsx2/MMI.cpp` and `ps2homebrew/ps2-unpacker`'s `MMI.c` agree on this layout and on suppressing the write when `rd` is zero: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp. These are non-normative behavioural references, not a hardware specification.
- QEMU's `gen_pextw` helper was cross-checked against PCSX2's word ordering during this increment and agrees with it on the `[rt0, rs0, rt1, rs1]` layout and on ignoring both source high doublewords; QEMU's encoding table was used for encoding confirmation. The dedicated native fixture checks the word order, ignored source high doublewords, `rd == rs`, `rd == rt`, `rd == rs == rt`, GPR[0] suppression and an untouched bystander register, and asserts that swapping the operands changes the result.
- The same seven-title census placed PEXTLW third at 489 words after POR's 554. Coverage after this increment added one MMI0 data-movement operation; PAND, PXOR, PNOR, the MMI0 saturation forms, PPACW, PEXTLB, PPACB and all other MMI0–MMI3 operations, MMI pipeline-1 state, and MMI latency remained unsupported and were still counted by `profile-elf` until the PPACW/PCPYUD increment.

## R5900 MMI0 PPACW and MMI3 PCPYUD cross-checks

- PPACW is encoded under primary opcode `0x1C` in the MMI0 group: low six function bits `0x08`, operation selector in bits 10..6 `0x13`, standard `rs`, `rt`, `rd`, no reserved field. PCPYUD is encoded in the MMI3 group: low six function bits `0x29`, operation selector in bits 10..6 `0x0E`, standard `rs`, `rt`, `rd`, no reserved field. Selector `0x13` also names MMI2 PXOR and MMI3 PNOR, and selector `0x0E` also names MMI2 PCPYLD, so the group selector stays part of the match; REPS2 verifies both fields. The neighboring MMI0 selectors `0x11` (PSUBSW), `0x14` (PADDSH), `0x15` (PSUBSH), `0x1A` (PEXTLB) and `0x1B` (PPACB) and the neighboring MMI3 selectors `0x0C` (PMULTUW), `0x0D` (PDIVUW) and `0x0F` stay unknown.
- PPACW packs the low word of each 64-bit half: `GPR[rd][31:0] = GPR[rt][31:0]`, `GPR[rd][63:32] = GPR[rt][95:64]`, `GPR[rd][95:64] = GPR[rs][31:0]` and `GPR[rd][127:96] = GPR[rs][95:64]`, so the result words are `[rt0, rt2, rs0, rs2]`. Each result doubleword comes from one source; the second word of each pair is the low word of that source's upper doubleword. It is the opposite of PEXTLW, which pairs the two sources inside each doubleword. PCPYUD copies the upper doublewords: `GPR[rd][63:0] = GPR[rs][127:64]` and `GPR[rd][127:64] = GPR[rt][127:64]`, so the result doublewords are `[rs.high, rt.high]`. Unlike MMI2 PCPYLD the halves are not crossed, so the first operand supplies the low half.
- Neither operation saturates or raises an exception, and neither reads the second or fourth word of any source. PCSX2's `pcsx2/MMI.cpp` (`PPACW`, `PCPYUD`) and the PCSX2 recompiler `pcsx2/x86/iMMI.cpp` agree on both layouts, including the non-crossing of PCPYUD, and on suppressing the write when `rd` is zero: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp. These are non-normative behavioural references, not a hardware specification.
- QEMU's `target/mips/tcg/tx79-translate.c` provides `trans_PPACW` and `trans_PCPYUD`, which agree with PCSX2 on the lane selection above (including the asymmetry of PCPYUD) and on the `rd == 0` nop. QEMU's encoding table was used for encoding confirmation.
- The file-backed census placed PPACW first at 649 words and PCPYUD fifth at 355 words after PEXTLW. Coverage after this increment removes 1,004 words; PROT3W, PADDUB, PEXEW, PEXTLB, PAND, PNOR and the rest of the MMI0-MMI3 set, MMI pipeline-1 state, and MMI latency remain unsupported and are still counted by `profile-elf`.

## R5900 MMI2 PEXEW and PROT3W cross-checks
- PEXEW is encoded under primary opcode `0x1C` in MMI2 group: low six function bits `0x09`, operation selector in bits 10..6 `0x1E`, `rs` reserved zero, standard `rt` and `rd`. PROT3W uses the same group with selector `0x1F` and the same reserved `rs`. A nonzero `rs` is not a legal encoding.
- PEXEW result words are `[rt1, rt2, rt3, rt0]`: the low word of each 64-bit half is exchanged with the low word of the other half. PROT3W result words are `[rt1, rt2, rt0, rt3]`: the four words rotate left by one position.
- Neither operation saturates or raises an exception, and neither reads architectural state beyond `rt`. QEMU `target/mips/tcg/tx79_translate.c` (`trans_PEXEW`, `trans_PROT3W`) and PCSX2 `pcsx2/MMI.cpp` (`PEXEW`, `PROT3W`) agree on these layouts and on `rd == 0` suppression.
- The file-backed census placed PEXEW at 356 words and PROT3W at 648 words. This increment removes 1,004 words; PADDUB, PEXTLB, PAND, PNOR and the rest of the MMI0-MMI3 set remain unsupported and are still counted by `profile-elf`.
## R5900 MMI0 PEXTLB cross-checks

- PEXTLB is encoded under primary opcode `0x1C` in the MMI0 group: the low six function bits are `0x08` and the operation selector in bits 10..6 is `0x1A`. REPS2 verifies both fields, so selector `0x1A` in another MMI group or neighboring MMI0 selectors remain unsupported.
- The operation interleaves the low eight bytes of the two source GPRs as `[rt0, rs0, ..., rt7, rs7]`. The high doubleword of each source is ignored. The complete 128-bit result is formed before assignment, preserving source/destination alias safety; writes to `GPR[0]` are suppressed.
- The file-backed census placed PEXTLB at 307 words. Coverage after this increment removes 307 words; the remaining MMI operations, MMI pipeline-1 state and MMI latency remain unsupported and are still counted by `profile-elf`.
- The dedicated native fixture uses hand-derived vectors for both result halves, high-byte ignoring, `rd == rs`, `rd == rt`, `rd == rs == rt`, zero-register suppression and an untouched bystander. These synthetic results validate the independent decoder → typed IR → C++ AOT path; they do not establish retail title boot compatibility.

## R5900 MMI2 PAND cross-checks

- PAND is encoded under primary opcode `0x1C` in the MMI2 group: the low six function bits are `0x09` and the operation selector in bits 10..6 is `0x12`. Selector `0x12` also names MMI0 PEXTLW and MMI3 POR, so REPS2 verifies both fields; the same selector in MMI1, the neighboring MMI2 selectors `0x10` (PMADDH), `0x11` (PHMADH) and `0x13` (PXOR), and every other MMI group selector stay unsupported.
- The operation is the plain 128-bit bitwise AND `GPR[rd][127:0] = GPR[rs][127:0] & GPR[rt][127:0]`. PS2Tek's MMI2 decoding table lists `PAND` at selector `0x12` and `PXOR` at `0x13`; QEMU's R5900/TX79 instruction tables mask the encoding to the major opcode plus bits 10..0; PCSX2's `tbl_MMI2` dispatch table (`pcsx2/R5900OpcodeTables.cpp`) routes opcode `0x1C`/function `0x09` to the MMI2 table with `PAND` in the `0x12` slot, and `pcsx2/MMI.cpp` (`PAND`) implements the two doubleword ANDs and returns without storing when `rd` is zero. These are non-normative behavioural references, not a hardware specification.
- PAND saturates nothing and raises no exception, and it reads no architectural state beyond `rs` and `rt`. Coverage after this increment adds MMI2 PAND alongside the previously lowered MMI bitwise OR (`POR`); PNOR and PXOR are lowered in the following cross-check section and PEXTLB in the MMI0 PEXTLB section, while the rest of the MMI0–MMI3 set, MMI pipeline-1 state, and MMI latency remain unsupported and are still counted by `profile-elf`.

## R5900 MMI2 PXOR and MMI3 PNOR cross-checks

- PXOR is encoded under primary opcode `0x1C` in the MMI2 group: low six function bits `0x09`, operation selector in bits 10..6 `0x13`. PNOR shares selector `0x13` in the MMI3 group (low six function bits `0x29`). Selector `0x13` also names MMI0 PPACW, so REPS2 verifies both fields; the same selector in another group and the neighboring MMI2/MMI3 selectors stay unsupported.
- PXOR is the plain 128-bit bitwise XOR `GPR[rd][127:0] = GPR[rs][127:0] ^ GPR[rt][127:0]`; PNOR is the 128-bit NOT-OR `GPR[rd][127:0] = ~(GPR[rs][127:0] | GPR[rt][127:0])`. Neither saturates, raises an exception, or reads state beyond `rs` and `rt`. PCSX2's `pcsx2/MMI.cpp` (`PXOR`, `PNOR`) and QEMU's R5900/TX79 instruction tables agree on both layouts and on `rd == 0` suppression: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp. These are non-normative behavioural references, not a hardware specification.
- REPS2 computes the complete 128-bit result in `parallel_xor`/`parallel_nor` before assigning it, so source/destination aliasing is safe. The dedicated native fixtures check normal, `rd == rs`, `rd == rt`, `rd == rs == rt`, zero-register suppression and an untouched bystander register, with compile-time helper assertions that include split-half cases which a one-half implementation cannot reproduce.
- PXOR and PNOR complete the MMI2/MMI3 bitwise family alongside PAND and POR. The remaining MMI operations, MMI pipeline-1 state and MMI latency remain unsupported and are still counted by `profile-elf`.

## R5900 COP1 (FPU) data-movement cross-checks

- COP1 is primary opcode `0x11`, and **bit 25 is not a COP1/COP2 flag**: the transfer format lives in bits 25..21 (the `rs` position). GNU binutils `opcodes/mips-opc.c` (2.45) masks confirm the four register-transfer formats - `mfc1 0x44000000/0xffe007ff`, `cfc1 0x44400000`, `mtc1 0x44800000`, `ctc1 0x44c00000` - so formats are `0`, `2`, `4`, `6`, the GPR operand is bits 20..16, the FPR number or FCR number is bits 15..11, and bits 10..0 are reserved zero. REPS2 requires `(word & 0x7FF) == 0` before accepting any of the four, so a non-zero reserved field stays unsupported instead of silently assembling. COP2 (VU0) has its own primary opcode `0x12`, which is why `0x11` is unambiguously the FPU.
- `LWC1` is primary `0x31` and `SWC1` is primary `0x39` (`lwc1 0xc4000000/0xfc000000`, `swc1 0xe4000000`). `0x35` is not a COP1 load: it is `LDC1`, which the EE does not implement. Sony's EE Core User's Manual §8.2 describes a **MIPS I single-precision-only** unit - 32 x 32-bit FPRs plus a 32-bit ACC - and the instruction-set manual's opcode matrix marks `LDC1`/`SDC1` and the doubleword control moves as unsupported or reserved, so REPS2 treats them as absent hardware rather than deferred work. `LQC1`/`SQC1` do not exist on the EE at all (they are a Loongson extension and appear in neither the Sony manuals nor binutils for r5900).
- Only **FCR0 (FIR) and FCR31 (FCSR)** are implemented: FIR reads `0x2E00` (Imp `0x2E` in bits 15..8, Rev in bits 7..0) and is read-only; FCR31 holds the condition and cause flags. What `CFC1`/`CTC1` do with control numbers 1..30 is not documented by any source reached, so lifting rejects them explicitly. Folding a reserved control number onto FCSR would execute a different instruction than the guest asked for, which the emitter cannot see.
- `MFC1` and `CFC1` **sign-extend the 32-bit result into `GPR[63:0]` and leave `GPR[127:64]` untouched**, matching the other 32-bit-writing operations already modeled; `MTC1` and `CTC1` take only `GPR[31:0]`.
- `FPR[0]` is an architectural register. The GPR effect masks skip bit 0 because `GPR[0]` is hard-wired zero; the new `fpr_read_mask`/`fpr_write_mask` include it, and the native fixture writes and reads `FPR[0]` through both `LWC1` and `MTC1` so an implementation that treats it like `GPR[0]` fails.
- `CTC1` cannot program the rounding mode: FCR31 bits 1..0 read as `0b01` and §8.7 states the mode is fixed to round toward zero. REPS2 therefore routes every control write through `store_fpu_control`, which forces those bits rather than trusting the recompiled guest.
- The FPU **never raises a floating-point exception**: §8.1 records no hardware exception mechanism affecting the instruction stream and §8.8 says processing continues by setting a flag, and there is no enable register to model. The only synchronous exception reachable from slice 1 is the address error that `LWC1`/`SWC1` share with the other memory operations, so effect metadata sets `may_raise_exception` only for the load/store forms.
- Items that were open when slice 1 landed are now resolved against the primary source and against retail field usage, so they are recorded as settled rather than as caveats:
  - **`SQRT.S` takes its source from bits 20..16, with bits 15..11 reserved zero.** Sony's instruction diagram and Ghidra say this; binutils names bits 15..11 as its operand and would reject a real retail word such as `0x46010004`. The census settles it: across 3,977 `0x11`/format `0x10`/funct `0x04` words in the eight images, bits 15..11 are zero in **100%** of them while bits 20..16 are non-zero in 35.5% (the rest are `sqrt($f0)`). The same census confirms the reverse placement for `ABS.S`/`MOV.S`/`NEG.S`/`CVT.W.S` (funct `0x05`/`0x06`/`0x07`/`0x24`: bits 20..16 zero in 100% of words, source in bits 15..11) and that the accumulator forms (`0x18`/`0x19`/`0x1A`/`0x1E`/`0x1F`) and the four compares (`0x32`/`0x34`/`0x36`) never write `fd` (bits 10..6 zero in 100% of words).
  - **`±Fmax` is `0x7FFFFFFF` / `0xFFFFFFFF`, not the IEEE largest finite `0x7F7FFFFF`.** The EE format (§8.1.1) allows any biased exponent `0 < e <= 255` and reserves nothing for NaN or infinity, so the maximum representable number is `e = 255` with an all-ones fraction. §8.5 Table 8-4 writes the abnormal results as `±Fmax` with the footnote "*Fmax is the maximum number in a single-precision floating-point format*", and §8.6 Table 8-5 gives the same cases as the literal encodings `7FFFFFFF` and `FFFFFFFF`. Secondary emulator sources use `0x7F7FFFFF`; REPS2 implements the manual, and the divergence is an open differential case, not a resolved one.
  - **FCR31's bit 24 is a documented one-bit field that reads as 1**, not an inference from the reset figure: §8.3.2 Figure 8-5 and Table 8-2 lay the register out as `0`(31..25) `1`(24) `C`(23) `0`(22..18) `I D O U`(17..14) `0`(13..7) `SI SD SO SU`(6..3) `001`(2..0), with reset `0x0100_0001`. Cause flags are updated per instruction: Table 8-2's "cleared to 0 otherwise" wording conflicts with the per-instruction boxes, and the arithmetic section below records the box-first model the runtime implements (named causes set, explicitly cleared causes cleared, unnamed causes untouched, sticky bits set-only until software clears them). FCR0 is `Imp = 0x2E` in bits 15..8 with the revision in bits 7..0 and is read-only, so `0x2E00` is the revision-zero value the native fixture now asserts literally.
  - Arithmetic work that remains open is tracked in the arithmetic section and ADR 0021: the manual does not define the bits lost beyond "toward zero, no guard/round/sticky", and the CVT.W.S clamp-flag and RSQRT.S zero-divisor flag conflicts are recorded as open differential cases rather than left implicit.

## R5900 COP1 single-precision arithmetic

- **Field placement is settled by retail usage, not by choosing a table.** `fs` is bits 15..11, `ft` is bits 20..16, `fd` is bits 10..6 and funct is bits 5..0. Sony's diagrams and the Ghidra SLASPEC put `SQRT.S`'s source in `ft` and reserve `fs`, while binutils names `fs`; the census shows bits 15..11 are zero in **100%** of 3,977 SQRT.S words, so the manual/Ghidra reading is the one retail code was assembled against. ABS/MOV/NEG/CVT.W.S reserve `ft` (zero in 100% of words), the accumulate forms (`0x18`/`0x19`/`0x1A`/`0x1E`/`0x1F`) reserve `fd` (zero in 100%), and the compares (`0x32`/`0x34`/`0x36`) write no `fd`. The decoder rejects every non-zero reserved field instead of masking it away.
- **ACC is a separate 32-bit register**, not a 33rd FPR and not addressable through COP1. ADDA/SUBA/MULA overwrite it, MADDA/MSUBA read-modify-write it, and MADD/MSUB read it as an operand. `EeState::acc` is the only representation, and the instruction-effect metadata exposes `reads_acc`/`writes_acc` because no register field names this dependency.
- **The numeric core uses integer arithmetic only.** Each operand decodes to sign/exponent/mantissa and results are re-encoded with truncation toward zero; the runtime never uses host `float`, so compiler contraction and host rounding modes cannot change guest results. The EE format allows biased exponent 255 as a normal value, giving `±Fmax` = `0x7FFFFFFF`/`0xFFFFFFFF` and no NaN, infinity or denormal encodings. Table 8-5 states the complete signed-zero table, including the rule that makes subtraction non-delegable: the difference's zero sign is `minuend AND NOT subtrahend`, so `(-0)-(-0)` is `+0` while `(+0)-(-0)` is also `+0` and only `(-0)-(+0)` is `-0`. Table 8-5 also makes Max `+0` unless both operands are `-0`, and Min `-0` whenever either operand is `-0`, so both comparisons special-case a zero pair instead of trusting `+0 == -0`.
- **Four manual conflicts are resolved and recorded rather than hidden.** (1) Table 8-2's "flags are cleared to 0 otherwise" cannot hold together with the per-instruction boxes, so the boxes win: ABS/NEG/MAX/MIN clear O and U, SQRT clears D, and an unnamed cause is left alone (Ch.5's opening flag-scope note). (2) Table 8-4 says conversion overflow produces ±Fmax with no cause, while the CVT.W.S instruction box produces the clamped integer and Table 8-6's conversion-of-floating-point-to-integer row names I/SI; the implementation follows the box for the value and Table 8-6 for the flag. (3) RSQRT.S's box names D/SD whenever `ft` is zero, while Table 8-5's `0/SQRT(±0)` rows name I; the box wins, and the result sign is the sign of `fs / SQRT(ft)`, which reproduces all four table rows. (4) MAX.S/MIN.S's `>=`/`<=` pseudo-code returns `fs` when the operands compare equal, which would make `Max(-0,+0)` negative; Table 8-5 wins. All four choices are named in `docs/decisions/0021-cop1-arithmetic.md`.
- **The zero cases are decided from the boxes, not defaulted.** A zero dividend with a non-zero divisor returns a signed zero without setting U: the DIV.S box names a flag only for a zero divisor, Table 8-4 reserves U for an exponent underflow, and PCSX2's `DIV_S` agrees. A zero dividend with a zero divisor remains the I case. MAX.S/MIN.S follow Table 8-5 for a zero pair. RSQRT.S with a zero divisor takes its sign from the quotient, which reproduces Table 8-5's rows; PCSX2's `RSQRT_S` instead uses the sign of `ft` alone, so it matches the `+0` numerator rows and flips the two `-0` ones, and it is recorded as a disagreement rather than cited as support. None of these has a silicon differential yet.
- **The MADD/MSUB pseudo-code's short circuits are an open differential.** The manual's algebraic description is `ACC ± fs*ft`, but its operation box adds a state machine: an ACC exponent of `0xFF` counts as already overflowed, an overflowing product is replaced by `±Fmax` before the add, a product exponent of `0x00` skips the add, and a signed-zero product sets SU. `fpu_accumulate` computes the product and the sum plainly, as PCSX2's `MADD_S`/`MSUB_S` do. The falsifier is `ACC=-1, fs=Fmax, ft=2`: REPS2 returns `0x7FFFFFFE`, the box returns `0x7FFFFFFF`. Silicon measurement would settle it and change the oracle, the runtime and ADR 0021 together.
- **Evidence chain.** `tools/ee_fpu_model.py` is an independent arbitrary-precision rational model; `--self-check` runs 113 hand-derived expectations and `--emit-vectors` produces 426 rows that `tests/cop1_arith_smoke.cpp` executes against the runtime, including FCR31 expectations and preset-flag cases that observe each helper's own cause clears. The fixture asserts the row count exactly. Reserved-field rejection is proven in the `mips.rs` decoder tests rather than in the vector rows. A second 27-instruction generated block (`tests/cop1_arith_codegen_smoke.cpp`) checks decode → IR → C++ with hand-computed results, ACC routing, and a divide-by-zero flag read back through CFC1. Mutation evidence: 20 numeric-core mutants and 10 wiring mutants, all killed by the committed harnesses `tools/mutate_cop1_numeric.py` and `tools/mutate_cop1_wiring.py`, which restore the sources and rebuild the full suite before reporting; the `MOV.S` sign flip in the runtime is invisible to the Rust suite and is killed only natively, and the five clear-removal mutants plus the unnamed-cause mutant are invisible to the reset-state vector rows and are killed only by the preset-flag cases.
- **Retail population.** The 20 newly supported mnemonics account for 284,702 census words: ADD 43,911, SUB 40,605, MUL 67,970, DIV 14,990, SQRT 3,977, ABS 2,891, MOV 37,531, NEG 6,554, RSQRT 261, ADDA 3,660, SUBA 58, MULA 7,195, MADD 9,894, MSUB 1,005, MADDA 4,264, MSUBA 34, MAX 2,279, MIN 1,965, CVT.W.S 11,155, CVT.S.W 24,503.

## R5900 COP1 compare and BC1 cross-checks

- The EE defines only four compare predicates, with `fd` reserved zero: funct `0x30` `C.F.S`, `0x32` `C.EQ.S`, `0x34` `C.LT.S`, and `0x36` `C.LE.S`. The odd functs (`C.UN`, `C.UEQ`, `C.OLT`, `C.ULE`, `C.SEQ`, ...) and `0x38..0x3F` are IEEE-only and stay unknown. The retail census agrees with the field layout: across 44,952 compare words in the eight images, `fd` is zero in every one.
- The comparison is the same exact integer ordering already used by MAX.S/MIN.S (ADR 0021): `+0 == -0`, exponent field zero reads as zero rather than an IEEE denormal, exponent `0xFF` is normal, and there is no NaN. The compare writes only FCR31 bit 23 and names no cause or sticky flag; it cannot raise a floating-point exception. `C.F.S` ignores both operands and always clears the bit.
- `BC1` is primary `0x11`, format/rs `0x08`, with the variant in `rt`: `0` `BC1F`, `1` `BC1T`, `2` `BC1FL`, `3` `BC1TL`. Bits 15..0 are a signed word offset added to `PC + 4`. Selectors `4..31` are reserved. The likely forms reuse the branch-likely not-taken annulment rule rather than a second implementation.
- The CFG models the condition as `BranchCondition::FpuCondition { when_set }`, and the generated expression tests FCR31 bit 23 directly. The compare is a straight-line IR operation, so a compare in one block and a BC1 in a later block still share the architectural FCSR state.
- **Evidence chain.** `tools/ee_fpu_model.py --emit-compare-vectors` produces 136 rows over four predicates and 34 operand pairs, including signed zero, exponent-0 flush, equal encodings and the EE top-of-range order. `tests/cop1_compare_smoke.cpp` executes every row from a preset `0x0101'0041` (C clear, D cause and SI sticky set), asserts the exact row count, and separately proves that a condition update changes only bit 23. The generated control-flow fixture covers `C.LT.S`/`BC1T` taken and not-taken, `C.F.S`/`BC1F`, and `C.EQ.S`/`BC1FL` and `C.LE.S`/`BC1TL` taken and annulled, including a `+0` versus `-0` equality case. The committed wiring harness now runs 21 mutants; all were killed, and the BC1 predicate/polarity and runtime condition-update mutants are observable only in the native fixtures.
- **Retail population.** The compare predicates account for 44,952 words (`C.EQ.S` 5,884, `C.LT.S` 22,769, `C.LE.S` 16,299, `C.F.S` 0) and the BC1 family for 45,035 (`BC1F` 21,441, `BC1T` 15,686, `BC1FL` 3,856, `BC1TL` 4,052). `C.F.S` has no retail population, so its behavior is proven synthetically only.

## Measured decoder coverage over lawfully owned retail images

Extracted ELF entry images from eight titles, profiled with `profile-elf` over every complete, naturally aligned word in file-backed executable PT_LOAD ranges. These are address-space census numbers, not reachability claims, and the images stay outside the repository. **Two of the eight images are the same boot executable** (a localized release whose extracted ELF is byte-identical to the US release, md5 `25028a54d4d1aac041557be7b6ca5e53`), so the table counts that core twice; it is kept because it is what the eight-image census actually measured, and the deduplicated column shows the seven-distinct-core view.

| census unit | after COP1 moves, before COP1 arithmetic (8 images) | after COP1 moves, before COP1 arithmetic (7 distinct cores) |
| --- | --- | --- |
| scanned words | 7,814,471 | 6,393,607 |
| supported | 6,771,673 (86.66%) | 5,549,891 (86.80%) |
| `SYSCALL`/`BREAK` traps | 13,729 (0.18%) | 11,336 (0.18%) |
| unsupported | 1,029,069 (13.17%) | 832,380 (13.02%) |

| census unit | before COP1 moves (8 images) | after COP1 moves (8 images) |
| --- | --- | --- |
| scanned words | 7,814,471 | 7,814,471 |
| supported | 6,193,386 (79.26%) | 6,771,673 (86.66%) |
| `SYSCALL`/`BREAK` traps | 13,729 (0.18%) | 13,729 (0.18%) |
| unsupported | 1,607,356 (20.57%) | 1,029,069 (13.17%) |

| census unit | before COP1 arithmetic (8 images) | after COP1 arithmetic (8 images) | after arithmetic (7 distinct cores) |
| --- | --- | --- | --- |
| scanned words | 7,814,471 | 7,814,471 | 6,393,607 |
| supported | 6,771,673 (86.66%) | 7,056,375 (90.30%) | 5,757,796 (90.06%) |
| `SYSCALL`/`BREAK` traps | 13,729 (0.18%) | 13,729 (0.18%) | 11,336 (0.18%) |
| unsupported | 1,029,069 (13.17%) | 744,367 (9.53%) | 624,475 (9.77%) |

| census unit | before COP1 compare/BC1 (8 images) | after COP1 compare/BC1 (8 images) | after compare/BC1 (7 distinct cores) |
| --- | --- | --- | --- |
| scanned words | 7,814,471 | 7,814,471 | 6,393,607 |
| supported | 7,056,375 (90.30%) | 7,146,362 (91.45%) | 5,825,265 (91.11%) |
| `SYSCALL`/`BREAK` traps | 13,729 (0.18%) | 13,729 (0.18%) | 11,336 (0.18%) |
| unsupported | 744,367 (9.53%) | 654,380 (8.37%) | 557,006 (8.71%) |

| census unit | before SPECIAL fences/DIV/moves (8 images) | after SPECIAL fences/DIV/moves (8 images) | after fences/DIV/moves (7 distinct cores) |
| --- | --- | --- | --- |
| scanned words | 7,814,471 | 7,814,471 | 6,393,607 |
| supported | 7,146,362 (91.45%) | 7,164,374 (91.68%) | 5,841,092 (91.36%) |
| `SYSCALL`/`BREAK` traps | 13,729 (0.18%) | 13,729 (0.18%) | 11,336 (0.18%) |
| unsupported | 654,380 (8.37%) | 636,368 (8.14%) | 541,179 (8.46%) |

- The SYNC/DIV/DIVU/MOVN/MOVZ/CACHE/PREF slice accounts for +18,012 words leaving unsupported (supported up to 91.68% on 8 images, 91.36% on 7 distinct cores).
- The bounded entry scan reaches a major milestone: **100% of sampled retail executables** (Bully, Downhill Domination, God of War II, GTA San Andreas, Need for Speed Most Wanted, Resident Evil 4) now decode cleanly through their entire startup sequence (92 to 112 consecutive words) and successfully arrive at their first kernel/BIOS `SYSCALL` trap (`0x0000000C`) without encountering any unsupported opcode.

- The compare and BC1 slice accounts for the entire +89,987-word change: 44,952 compare words (`C.EQ.S` 5,884, `C.LT.S` 22,769, `C.LE.S` 16,299, `C.F.S` 0) and 45,035 `BC1` words (`BC1F` 21,441, `BC1T` 15,686, `BC1FL` 3,856, `BC1TL` 4,052). The remaining `cop1` family falls from 100,766 to 10,779 (1.65% of unsupported), and that remainder is dominated by data patterns such as `0x44455243` (`"CRED"` as little-endian bytes) rather than executable compares.
- Current remaining unsupported families: `special` 381,245 (58.3% of unsupported), `cop2` 87,297 (13.3%), `load-store` 55,533 (8.5%), `other` 28,058, `immediate` 21,674, `cop0` 21,654, `mmi` 14,645, `branch` 10,899, `cop1` 10,779, `mmi2` 10,429, then `mmi0` 5,005, `regimm` 3,146, `mmi3` 2,870 and `mmi1` 1,146.

- The five supported COP1 formats plus `LWC1`/`SWC1` account for the entire +578,287-word change, and the split confirms the encoding layout above: 135,235 words left the `cop1` family (the `0x11` register and control transfers) and 443,052 left `load-store` (`LWC1` `0x31` and `SWC1` `0x39`, which the profiler attributes by primary opcode). Treating `SWC1` as `0x35` would have left most of that population unsupported and mis-filed.
- The 20 arithmetic mnemonics account for the entire +284,702-word change, and the remaining `cop1` family falls from 385,468 to 100,766. That remainder is dominated by the compares (`0x46000834` C.OLT, `0x46000836` C.OLE, `0x46000832` C.EQ), `BC1` (format `0x08`, e.g. `0x45000003`) and reserved-field data patterns.
- After the arithmetic slice, remaining unsupported families were: `special` 381,245 (51.2% of unsupported), `cop1` 100,766 (13.5%), `cop2` 87,297 (11.7%), `load-store` 55,533 (7.5%), `other` 28,058, `immediate` 21,674, `cop0` 21,654, `mmi` 14,645, `branch` 10,899, `mmi2` 10,429, then the remaining MMI groups and `regimm`.
- The bounded entry scan moves again: all eight images now decode **67** words and stop at the same word, `0x0000040F` - SPECIAL funct `0x0F`, `SYNC`. The previous stop at word 66 was ADDA.S `0x46010018` in every image, so arithmetic was the blocker; the next blocker out of an entry point is now a memory-barrier instruction, not the FPU. The compare/BC1 slice leaves this scan unchanged at 67 words.

## Static function-root discovery on stripped retail executables

- Every extracted boot ELF in the local library has **zero `STT_FUNC` symbols**, so symbol-seeded analysis reduces to the ELF entry. On Black that is 9 blocks / 113 decoded instructions before the first `SYSCALL`. The opt-in `--discover` pass adds two encoding-level candidate sources over file-backed executable bytes: `JAL` targets that are themselves file-backed executable, and aligned words that decode as `addiu $sp, $sp, -N`, `N < 0`. Both are candidates, not function boundaries or reachability proofs.
- Discovery is deterministic and bounded. The scan reports examined words, evidence counts, distinct candidates, and whether the candidate budget truncated the scan. A discovered root is labeled `discovered`; heuristic-only blocks that cannot decode are skipped and counted, while the ELF entry and `STT_FUNC` roots still fail hard. Authoritative reachability that later reaches a skipped heuristic address retries it with hard-error semantics.
- Black evidence split: 801,120 scanned words produced 7,324 distinct `JAL` targets and 7,893 stack-prologue addresses, for 10,457 distinct candidates. 10,394 roots produced a block (including the ELF entry), 64 candidates were rejected, and 221 heuristic-only block starts were skipped. The resulting CFG has 97,678 blocks, 473,292 decoded instructions, and 1,471 explicit unsupported block stops.
- All eight images complete without budget truncation or hard analysis failure. Block counts range from 53,800 (Downhill Domination) to 213,942 (GTA San Andreas), and decoded instructions from 274,227 to 1,040,666. The 250,000-block default budget leaves only ~15% headroom on GTA San Andreas. ADR 0030 keeps that safe default and adds validated `--max-blocks`, `--max-instructions` and `--max-roots` overrides with ceilings above the physical 32 MiB address space, actionable limit failures, and manifest-recorded values.
- `recompile-elf --discover` on Black completes in about 7 seconds and emits 97,678 blocks / 472,072 represented guest instructions / 1,471 explicit unsupported stops plus a 32 MiB RAM image. The C++ source is about 178 MB in one translation unit. That validates the discovery-to-IR-to-C++ path at retail scale in terms of analysis, but a single unit of that size is not a practical native build strategy.
- Emitting the same CFG as one dispatcher plus eight block parts (`--tu-count 8`) yields a 15.84 MiB dispatcher and eight parts of 17.89-23.87 MiB. MSVC 19.51 `/std:c++20 /O2 /W4 /permissive-` compiles the dispatcher in about 5 s, a representative ~20 MiB part in about 17 s, and the complete dispatcher-plus-parts set links in **148.9 s sequentially with zero warnings**. A runtime probe initializes from a 32 MiB RAM image, calls `execute_cfg(state, 0)`, receives `instruction_budget_exhausted`, and finds `state.pc` unchanged at the entry block start (`0x00100008`), confirming sorted-table dispatch and block-atomic budget accounting across the split units. Part compilation is independent, so parallel builds reduce wall time toward the slowest part; the exact speedup is machine-dependent and is not claimed. See ADR 0027.
- Because embedded data can decode as a plausible `JAL` or prologue, accepted candidates are not validated functions and may produce unreachable or nonsensical blocks. The measurements bound the heuristic's output, not its semantic correctness, and they do not establish playability.

## Guest exception delivery and ERET

- ADR 0031 documented the intermediate stage where SYSCALL/BREAK were distinguished from unknown encodings but still returned terminal host statuses. Its historical Black/USA probe (status=10 at PC 0x001001C8) predates the exception-delivery work below; it is not evidence for the new runtime result.
- ADR 0033 moves the instructions into typed `GuestException` control terminators, separate from `Unsupported`. SYSCALL enters with Cause.ExcCode 8 and BREAK with code 9. When Status.EXL is clear, exception entry records EPC (the trapping PC, or branch PC for a delay-slot exception), writes Cause.BD, and sets EXL. If EXL is already set, the first EPC/BD frame is preserved while the new ExcCode is recorded.
- BEV selects the bootstrap common vector `0xBFC00380`; otherwise the common vector is `0x80000180`. `Status::exception_delivered` is a host boundary that returns control with COP0 state and PC populated. It is not a BIOS/HLE system-call implementation and does not claim that the handler itself has executed.
- ERET is represented by a typed exception-return terminator. It restores PC from EPC when EXL is set or ErrorEPC when ERL is set, then clears the corresponding level bit. A missing or ambiguous context (including EXL and ERL both set) returns `invalid_exception_return` rather than guessing. The dispatcher can continue at the restored PC only when that target is part of the translated block table.
- The runtime helper's branch-delay EPC/BD rule, nested Level-1 behavior, BEV vector selection, EXL/ERL return selection, generated ERET continuation and invalid-return cases are covered by native C++ tests on Windows/MSVC and Linux/GCC. CFG lowering admits SYSCALL/BREAK in delay slots as typed guest exceptions; ERET and other control transfers remain unsupported there. Generated data-memory operations can independently deliver AdEL/AdES from an admitted branch delay slot.
- Build manifest schema v2 records `guest_exception_count` separately from `unsupported_stop_count`; the latter now counts only genuinely unsupported terminators. All generated manifests continue to record `playable: false`.

## EE kernel syscall interface and SetupThread

- The EE kernel syscall entry is a `syscall` instruction whose service number is carried in a register, not in the instruction's 20-bit code field. ps2sdk's wrappers expand to `li $3, __NR_<call>` followed by a bare `syscall` (https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/kernel.S), and its crt0 issues `addiu $3, $0, 60; syscall` before any other kernel call (https://github.com/ps2dev/ps2sdk/blob/master/ee/startup/src/crt0.c). The instruction still traps architecturally with Cause.ExcCode 8 into V_COMMON (https://psi-rockin.github.io/ps2tek/).
- Arguments use `$a0..$a3` (GPR[4..7]) and the result returns in `$v0` (GPR[2]). ps2sdk callers treat a negative result as failure. A specific negative error-code taxonomy is not reproducible from public sources, so REPS2 treats only "negative" as failure and never synthesizes an error code.
- The ps2sdk "i" variants (for example `__NR_iChangeThreadPriority`) pass the negated number (https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/syscallnr.h); dispatch therefore uses the magnitude.
- Service numbers on the observed boot path: 1 ResetEE, 2 SetGsCrt, 4 Exit, 6 LoadExecPS2, 0x10 AddIntcHandler, 0x20 CreateThread, 0x22 StartThread, 0x29 ChangeThreadPriority, 0x3C SetupThread, 0x3D SetupHeap, 0x40 CreateSema, 0x64 FlushCache, 0x74 SetSyscall, 0x7E MachineType, 0x7F GetMemorySize (ps2sdk `ee/kernel/include/kernel.h`, `syscallnr.h`; PCSX2's BIOS service-name table agrees on 0x3C, 0x3D and 0x40).
- SetupThread (0x3C) is declared `void *SetupThread(void *gp, void *stack, s32 stack_size, void *args, void *root_func)` (ps2sdk `kernel.h`). crt0 moves the result into `$sp` immediately afterwards, so the returned value is the initial stack pointer. Emulator implementations (Play!'s `PS2OS`) compute `stackAddr = (a1 == 0xFFFFFFFF) ? ramSize - 0x1000 : a1 + a2`, return `stackAddr - 0x2A0`, clear `$v1`, and give the first call thread id 1.
- Unverified and treated as emulator observations rather than documented kernel behavior: the placement of the fifth argument (`root_func` is not passed in a register by the o32 convention), the `0x2A0` reserve, the `$v1 = 0` return, the argv-block rewrite and the thread-id assignment. No public source implements the whole EE kernel: PCSX2 does not emulate it and instead vectors into the BIOS (`R5900OpcodeImpl` SYSCALL path), so the observable contract comes from ps2sdk prototypes plus emulators that reimplement individual services.
- REPS2 therefore implements only the guest-visible part it can defend (documented prototype, stack computation with the sentinel, `$v0`/`$v1` results) and records the rest as unobserved. A service number without a handler stops as `Status::unhandled_syscall` instead of returning fabricated success; see ADR 0062.

## EE data address errors (AdEL / AdES)

- Sony's EE Core User's Manual v6.0, section 4.2.9, defines AdEL as Cause.ExcCode 4 for a misaligned instruction fetch or load and AdES as code 5 for a misaligned store. V_COMMON is `0x80000180` when BEV=0 and `0xBFC00380` when BEV=1; BadVAddr receives the virtual address that directly caused the exception (https://docs.alexrp.com/mips/ee.pdf).
- Generated memory code delivers AdEL for naturally misaligned LH/LHU/LW/LWU/LD and LWC1, and AdES for SH/SW/SD and SWC1. Byte operations cannot fail natural alignment; LQ/SQ retain their 16-byte rounding behavior; LWL/LWR/LDL/LDR and their stores retain aligned merge accesses.
- Only `Status::address_error` is translated into a guest exception. `unmapped_memory`, `unsupported_memory_access`, short backing spans, TLB/translation, instruction-fetch faults, privilege faults and bus/MMIO failures are not relabeled as AdEL/AdES.
- Native CFG tests cover BadVAddr, Cause codes, no destination or memory commit, AdES in a taken branch delay slot, and nested-frame preservation.

## Decoder coverage profiling

- `profile-elf` treats every complete, naturally aligned 32-bit word in file-backed executable PT_LOAD ranges as a census unit. It does not claim those words are reachable code; embedded data, padding, and never-executed functions are included.
- Supported, `SYSCALL`/`BREAK` trap, and unsupported words are counted separately. Unsupported words are grouped by the broad encoding family derived from primary opcode/MMI selector fields and by exact 32-bit encoding. This makes the next instruction-family investment measurable without overstating semantic coverage.
- The entry scan is a separate bounded linear diagnostic. It stops at the first unsupported word or trap and reports the guest PC and raw word. It does not follow branches, and a clean scan does not establish CFG reachability or playability.


## ELF table validation

- ELF string tables are byte-indexed tables whose first and last bytes must be NUL when non-empty; an empty string table is permitted. Section and symbol names are resolved only after table bounds and sentinel checks (System V GABI, sections 3.4 and 3.6).
- The first section header must be SHT_NULL. For section counts at or above SHN_LORESERVE, e_shnum is zero and the actual count is read from section zero's sh_size; an e_shstrndx value of SHN_XINDEX resolves through section zero's sh_link. Section counts are bounded by ParseLimits.max_sections.
- When e_phnum is PN_XNUM (0xffff), the real program-header count is read from section zero's sh_info; otherwise section zero's sh_info must be zero. Program-header counts are bounded by ParseLimits.max_program_headers, and an absent section table cannot provide the extended count.
- A symbol table's sh_info marks the first non-local symbol: the prefix must be STB_LOCAL and every later symbol must be non-local. Ordinary st_shndx values must resolve to section headers; reserved special indices such as SHN_ABS and SHN_COMMON remain valid.
- SHT_SYMTAB_SHNDX links to exactly one SHT_SYMTAB/SHT_DYNSYM, uses four-byte Elf32_Word entries, has sh_info zero, and contains one entry per linked symbol. A SHN_XINDEX symbol resolves through its corresponding nonzero entry to a real section index at or above SHN_LORESERVE; every non-SHN_XINDEX slot must be zero. Duplicate/orphan tables, invalid links, count mismatches, malformed entries and out-of-range indices fail closed.
- REL/RELA sh_link and sh_info cross-references are validated before relocation records are exposed. Relocation application remains separate and allocated relocations remain rejected by the default EE image builder.
- These structural checks do not complete broader PS2 ABI metadata, dynamic linking, or R_MIPS relocation semantics.

References:
- System V Generic ABI, section headers and sh_link/sh_info: https://gabi.xinuos.com/v42/elf/03-sheader.html
- System V ELF string-table rules: https://refspecs.linuxfoundation.org/elf/elfspec.pdf

## MIPS ABI metadata: .reginfo and .MIPS.abiflags

- The MIPS ABI supplement defines SHT_MIPS_REGINFO (0x70000006) as a 24-byte ELF32_RegInfo record: one GPR mask, four coprocessor masks, and a signed 32-bit gp value. It describes register usage for an object and the global pointer value used by GP-relative relocations in relocatable objects.
- PT_MIPS_REGINFO (0x70000000) is the corresponding program-header type in the MIPS ABI supplement. When present, REPS2 decodes its one 24-byte ELF32_RegInfo payload, rejects duplicate headers, and requires it to precede PT_LOAD. The parser does not require the segment to exist: PS2SDK's ERL workflow documents stripping .reginfo from relocatable modules, so absence is retained as a diagnostic rather than an unconditional rejection.
- SHT_MIPS_ABIFLAGS (0x7000002a) and PT_MIPS_ABIFLAGS (0x70000003) carry the same ABI flags payload. Linux's MIPS VDSO linker script places the section in a dedicated PT_MIPS_ABIFLAGS segment before PT_MIPS_REGINFO and PT_LOAD. REPS2 applies one shared decoder to section and segment payloads: version zero must be exactly 24 bytes, while unknown versions retain only the version and byte size without being interpreted as v0.
- LLVM's R5900 target support assigns AFL_EXT_5900 the value 6. This is distinct from the e_flags CPU field value 0x92: the fields complement one another, but neither alone proves that a game ELF is playable or fully compatible.
- SHT_MIPS_OPTIONS (0x7000000d) contains a byte stream of variable-length Elf_Options descriptors. Each fixed header is eight bytes: kind, total descriptor size, affected section index (zero for global options), and a kind-specific info word. The parser requires size >= 8, verifies every record remains inside the section, bounds the aggregate descriptor count with ParseLimits.max_mips_option_descriptors, and checks nonzero affected-section indices against the section table.
- For ODK_REGINFO (kind 1), an ELF32 descriptor must include the 24-byte ELF32_RegInfo payload after the header; its GPR/coprocessor masks and signed gp value are decoded. Other known and unknown kinds retain their numeric kind, size, target section, info word and opaque-payload length without guessed interpretation.
- The parser decodes MIPS processor-specific sections only when e_machine is EM_MIPS. In SHT_MIPS_ABIFLAGS, flags1 bit 0 is AFL_FLAGS1_ODDSPREG (odd single-precision registers); remaining flags1 bits, flags2, and unrecognized ASE bits are retained numerically. Duplicate ABI-flags sections are rejected as ambiguous. No ABI mask or register-use mask is treated as enforcement policy.

References:
- System V MIPS ABI Supplement, section 4.14 Register Information and section 5.4 Program Header: https://refspecs.linuxfoundation.org/elf/mipsabi.pdf
- Linux MIPS ELF definitions for SHT_MIPS_REGINFO/SHT_MIPS_ABIFLAGS and Elf32_RegInfo/ABI flags: https://android.googlesource.com/kernel/common/+/b83b3fa78445387f351cef477a112e503d72b9f0/arch/mips/include/asm/elf.h
- GNU ELF headers define SHT_MIPS_OPTIONS and the eight-byte Elf_Options header/kind values: https://kernel.googlesource.com/pub/scm/utils/kernel/kexec/kexec-tools/+/master/include/elf.h
- GNU Binutils readelf decodes the ELF32 ODK_REGINFO payload immediately after the option header and validates variable descriptor sizes against the containing section: https://gnu.googlesource.com/binutils-gdb/+/b20a3440c9eb428a2e89efab2ec9fa1a39bd6a78/binutils/readelf.c
- LLVM MIPS ABI flags definitions, including AFL_EXT_5900=6: https://www.llvm.org/docs/doxygen/MipsABIFlags_8h.html
- LLVM R5900 target support discussion explaining AFL_EXT_5900: https://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20260119/1826509.html
- PS2SDK EE ERL documentation describes .reginfo as removable from relocatable modules: https://github.com/ps2dev/ps2sdk/blob/master/ee/erl/README

## ELF identification and object-type diagnostics

- ELF identification stores EI_OSABI in e_ident[7] and EI_ABIVERSION in e_ident[8]. OSABI identifies extensions/conventions, while the ABI-version byte is interpreted in the context of that OSABI; REPS2 therefore preserves both raw values and labels the version as raw metadata.
- Generic System V OSABI labels include NONE/unspecified (0), HP-UX (1), NetBSD (2), Linux (3), Solaris (6), AIX (7), IRIX (8), FreeBSD (9), Tru64 (10), Modesto (11), OpenBSD (12), OpenVMS (13), and NonStop Kernel (14). Values 64-255 are an architecture-specific range whose meaning depends on e_machine and its processor supplement. Other unassigned values remain reserved/unrecognized and are not rejected.
- e_type values 0-4 identify no file type, relocatable object, executable, shared object, and core file. ET_LOOS..ET_HIOS (0xFE00..0xFEFF) are OS-specific; ET_LOPROC..ET_HIPROC (0xFF00..0xFFFF) are processor-specific. Other values are reserved/unknown. The parser preserves the numeric e_type and reports a generic label only.
- These are diagnostics, not a broader loader: the current EE main-RAM image builder continues to require ET_EXEC and its existing static/PT_LOAD constraints. No OSABI or e_type label proves PS2 ABI compatibility or title bootability.

References:
- System V ABI, ELF Header / ELF Identification, including EI_OSABI, EI_ABIVERSION and e_type: https://web.stanford.edu/~ouster/cgi-bin/cs140-spring18/pintos/specs/sysv-abi-update.html/ch4.eheader.html
- Oracle Linkers and Libraries Guide, EI_ABIVERSION is interpreted in the context of EI_OSABI: https://docs.oracle.com/cd/E19683-01/817-1974/817-1974.pdf
- Binutils ELF constants for OSABI and e_type: https://gnu.googlesource.com/binutils-gdb/+/b4812cfe4dc394392e47e0489f38521da4a0b7ce/include/elf/common.h

## MIPS ELF header flags and PS2 EE identification

- ELF e_machine = EM_MIPS identifies the MIPS family but does not by itself distinguish a PlayStation 2 Emotion Engine executable from other MIPS objects.
- MIPS-specific e_flags divides information into feature bits, ABI bits (mask 0x0000F000), CPU subtype bits (mask 0x00FF0000) and architecture-level bits (mask 0xF0000000). The CPU code 0x92 within the CPU field is the R5900 marker used for PS2 EE boot executables.
- A commonly cited illustrative header value is 0x20924001: R5900 CPU marker, MIPS III architecture field, EABI64 ABI field and NOREORDER feature bit. The EABI64 subfield is a processor ABI tag; it does not change the ELF class from ELF32 to ELF64.
- REPS2 reports these fields as diagnostics only. Unknown CPU/ABI/ISA values and unclassified bits are preserved; no CPU-flag combination is currently treated as an automatic accept/reject decision or a playable-title signal.
- The PS2SDK EE ELF loader's documented header checks verify ELF identity, ELF32/little-endian, ET_EXEC, EM_MIPS and header sizes, but the inspected function does not validate e_flags. Therefore the R5900 marker is useful for classification and investigation, not a sufficient loader-compatibility proof.

References:
- ReverseEngineering.wiki, PS2 EE executable format: R5900 field mask/code and the example 0x20924001: https://reverseengineering.wiki/index.php/Format:PS2_EE_executable_(ELF)
- PCSX2 Elfheader.h, the illustrative e_flags = 0x20924001 comment: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Elfheader.h
- spimdisasm ELF32 constants, processor/ABI/ISA flag field definitions: https://github.com/Decompollaborate/spimdisasm/blob/master/spimdisasm/elf32/Elf32Constants.py
- PS2SDK EE ELF loader, header validation: https://github.com/ps2dev/ps2sdk/blob/master/ee/elf-loader2/src/elf.c

## ELF load-image policy

- REPS2 currently constructs an initial memory image only for little-endian MIPS ET_EXEC files whose PT_LOAD virtual addresses can be translated using the documented default 32 MiB EE main-RAM aliases.
- Segment placement is performed in physical RAM coordinates. A pair of virtual aliases that overlap physically is rejected rather than relying on program-header order.
- PT_LOAD file bytes are copied and BSS tails remain zero. The generated source gets a fixed ELF entry point; the companion raw image is a full 32 MiB snapshot.
- REL/RELA entries targeting SHF_ALLOC sections are rejected until their specific R_MIPS relocation semantics are implemented and differential-tested. Debug/non-allocated relocations do not change the executable image.
- ET_DYN, relocatable objects, expansion RAM, custom TOOL mappings, COP0/TLB mappings, and segments outside default main RAM are not silently accepted.

## EE syscall continuation discovery (M4 evidence)

- ADR 0062 added opt-in HLE dispatch for the EE `SYSCALL` boundary. The first real-title run answered Downhill Domination's `SetupThread` service (0x3C), but stopped at the following PC because the CFG treated a trap as having no successor.
- ADR 0063 adds a distinct `syscall_continuation` edge to `pc + 4`. This is not an architectural successor: an unserviced trap vectors through COP0. It is a translation candidate because a serviced HLE call resumes there.
- Continuations are queued leniently. Retail images can place data after a trap; an undecodable continuation is reported as a skipped heuristic block rather than a hard analysis failure.
- Reproduced on Downhill Domination USA (`SCUS_971.77`, local ISO only): before the change, `SetupThread` was handled and execution stopped at `pc=0x0010A17C`; after the change, execution resumed and reached the next service, `SetupHeap` (0x3D), at `pc=0x0010A194`. The observed bounded run was `executed=2756980`, `syscalls=2`, `hle_handled=1`, `hle_number=0x0000003D`, `status=unhandled_syscall`.
- This proves continuation translation and service-boundary progress only. It does not prove heap behavior, BIOS completion, a visible boot stage, or playability.

## EE SetupHeap research (next M4 runtime boundary)

Public sources fetched for the next service:

- ps2sdk `kernel.h`: https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/kernel.h
- ps2sdk `kernel.S`: https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/src/kernel.S
- ps2sdk `syscallnr.h`: https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/syscallnr.h
- ps2sdk startup `crt0.c`: https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/startup/src/crt0.c
- ps2sdk startup `linkfile`: https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/startup/src/linkfile
- ps2tek: https://psi-rockin.github.io/ps2tek/

Documented facts:

- ps2sdk declares `SetupHeap(void *heap_start, s32 heap_size)` and assigns service 0x3D. The wrapper places 0x3D in `$v1`, preserves `$a0/$a1`, and leaves the kernel result in `$v0`; the C declaration is `void`.
- ps2tek describes 0x3D as initializing the current thread's heap. For a non-sentinel address it documents the end as `heap + heap_size`; for its documented sentinel form it says the heap ends at the current thread's stack pointer and returns that end. The heap is per-thread; child threads inherit their parent's heap.
- ps2sdk startup calls `SetupThread` first, then calls `SetupHeap(&_end, -1)`; its own linker file sets `_heap_size = -1`, and `crt0.c` discards the result. No EE `CreateHeap` follows; `CreateHeap` references found in ps2sdk are IOP-side.
- ps2sdk's `_sbrk` uses `EndOfHeap()` as the upper bound for newlib allocations. ps2tek's 0x3E wording calls the result the heap base, contradicting its 0x3D wording and the `_sbrk` usage; the end interpretation is better supported.

Unresolved discrepancy:

- Downhill Domination calls `SetupHeap(0x008A0400, 0xFFFFFFFF)`, and ps2sdk's startup passes `-1` in the **size** parameter. ps2tek places its documented sentinel on the **heap address** parameter. The sources therefore do not prove that both slots are accepted equivalently. Treating `heap_size == -1` as "heap end equals the current thread's stack pointer" is an empirically required compatibility extension for this M4 path, not a settled hardware fact.

Safe implementation boundary:

- Explicit non-negative `(heap_start, heap_size)` can record a per-thread range and return its end in `$v0`.
- The observed `heap_size == -1` form can be implemented as a named, traceable profile extension using the stack pointer produced by `SetupThread`.
- Downhill Domination then reached syscall `0x40` (`CreateSema`) at `pc=0x0025A6E4` after the `SetupHeap` continuation. The captured boundary was `a0=0x0093C7E0`, `a1=0xFFFFFFFF`, `a2=0`, `a3=0`, `sp=0x0093C7E0`, and `v0_before=1`; the 24 bytes at `a0` in the initial RAM image are all zero, which says nothing about the call because a stack-allocated parameter block is only filled at run time. That boundary is now resolved by ADR 0066; no semaphore id or success result was fabricated to get past it.
- Do not invent error codes, an EE kernel allocator, overlap policy, alignment/slack rules, repeated-call semantics, or `CreateHeap`. Unknown or unsupported behavior must remain an explicit diagnostic.

## EE CreateSema research (M4 runtime boundary)

Public sources fetched:

- ps2sdk `kernel.h`: https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/kernel.h
- ps2sdk `syscallnr.h`: https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/syscallnr.h
- ps2sdk `kernel.S`: https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/src/kernel.S
- ps2tek: https://psi-rockin.github.io/ps2tek/

Documented facts:

- `ee_sema_t` is `int count, max_count, init_count, wait_threads; u32 attr, option;` — six 32-bit fields, 24 bytes. ps2sdk declares `s32 CreateSema(ee_sema_t *sema)` and assigns service 0x40; the wrapper is the ordinary `li $3, __NR_CreateSema; syscall` shape.
- ps2sdk documents `MAX_SEMAPHORES 256`. ps2tek describes the internal object as `{ free, count, max_count, attr, option, wait_threads, wait_next, wait_prev }`, i.e. the kernel keeps the count, the maximum, both guest attribute words and a waiter list.
- The public description says the call "returns the semaphore's id if successful and -1 if not", and that only `init_count` and `max_count` need to be specified; `attr` and `option` may be given but the same source marks them as not interpreted by the kernel. `count` and `wait_threads` are the fields `ReferSemaStatus` (0x47) reports back.
- `WaitSema` blocks when `count` is zero, `SignalSema` increments `count` or releases a waiter, and both force a reschedule; `PollSema` decrements and fails when the count is zero.

Unresolved:

- No public source states the kernel's id allocation order, whether it rewrites the guest parameter block, or which count pairs it rejects. The implementation therefore treats "lowest free slot" as a host choice and refuses only combinations that cannot be represented.
- EE kernel semaphores and IOP THSEMAP services share an API shape but their equivalence is not demonstrated, so no IOP behavior was reused.

Observed on the real title:

- Downhill Domination calls `CreateSema` twice (`sema_param=0x0093C800` on the second call, `init=1`, `max=1`, `attr=0`, `option=0`). The service returned ids 0 and 1 and the guest continued, which confirms the 24-byte layout and the returned-id contract: the read succeeded through the normal EE address-resolution path, and a wrong layout would have produced `init_exceeds_max`/`negative_counts` refusals instead of clean ids.
- The next boundary is syscall `0x74` (`SetSyscall`) at `pc=0x0025F094` with `a0=0x83`, `a1=0x0025EF38`, `a2=0`, `a3=0x0029E580`. ps2sdk declares `SetSyscall(s32 syscall_num, void *handler)` and its privileged dispatch indexes a 256-entry vector with the registered handler address. ADR 0067 implements registration only: the slot and handler are validated and stored, but arbitrary guest handler invocation is not guessed.
- After registration, Downhill Domination reaches syscall `0x83` (`FindAddress`) at `pc=0x0025EF7C`, with `a0=0x80000000`, `a1=0x80080000`, `a2=0x0025EF38`, `a3=0x0029E580`. The bounded run reports `executed=2757047`, `syscalls=7`, `hle_handled=6`. This proves registration-boundary progress only; handler execution, BIOS completion, visible boot and playability remain unproven.
- `FindAddress` is not safe to stub. ps2sdk names service `0x83` but exposes no public declaration or argument contract; `GetEntryAddress` (0x5B) is a separate service. The observed kernel-segment arguments do not distinguish address-range search, vector lookup or another kernel registry. ADR 0068 defers it pending return-register, memory-side-effect and caller-control-flow evidence.

### Downhill Domination `FindAddress` wrapper and call sites

`0x0025EF78` is a five-instruction wrapper for service `0x83` that passes
`$a0..$a3` through unchanged:

```
0025ef78: 24030083   addiu $v1, $zero, 0x83
0025ef7c: 0000000c   syscall
0025ef80: 03e00008   jr    $ra
0025ef84: 00000000   nop
```

Four call sites exist in the same routine. `$a1` is always
`lui 0x8008` → `0x80080000` and `$a3` is `0x0029E580`:

| site | `$a0` | `$a2` |
|---|---|---|
| `0x0025EFE4` | `lui $a0,0x8000` → `0x80000000` | `$s5 - 0x10C8` → `0x0025EF38` |
| `0x0025EFF8` | `0x80000000` | `$s4 - 0x1100` → `0x0025EF00` |
| `0x0025F024` | `$s3 + 4` | `0x0025EF38` |
| `0x0025F040` | `$s2 + 4` | `0x0025EF00` |

The bounded probe's syscall-sequence ring (ADR 0071) settles where `4` comes from.
Observed boundaries, oldest first, with `path` = host service (`H`) or registered
guest handler (`G`):

```
0x0010A178  0x3C H  SetupThread
0x0010A194  0x3D H  SetupHeap
0x0025A6E4  0x64 H  FlushCache
0x0025A6E4  0x64 H  FlushCache
0x0025F094  0x74 H  a0=0x00000083 a1=0x0025EF38   SetSyscall
0x0025F094  0x74 H  a0=0x0000005A a1=0x0025EF00   SetSyscall
0x0025EF7C  0x83 G  a0=0x80000000 a1=0x80080000 a2=0x0025EF38
0x0025EF7C  0x83 G  a0=0x80000000 a1=0x80080000 a2=0x0025EF00
0x0025EF7C  0x83 G  a0=0x00000004 a1=0x80080000 a2=0x0025EF38
```

Two conclusions follow directly from the bytes and the trace:

- `$a0` survives guest-handler entry and return unchanged: the first two
  dispatches carry `0x80000000`, matching call sites `0x0025EFE4` and
  `0x0025EFF8`. There is no register-restoration defect in the REPS2 path.
- The third dispatch matches call site `0x0025F024`, whose `addiu $a0,$s3,4`
  runs with `$s3 == 0`, so `4` is produced by guest code.

`$a1 - $a0` is 512 KiB for the first two calls but almost the whole KUSEG window
for the third, which is why only the third run exhausts the budget. On real
hardware the third scan would reach unmapped KUSEG long before the KSEG0 end
bound and fault, which suggests the path is not intended to be taken.

### The selector at `0x0025F00C` and the kernel-table dependency

The routine at `0x0025EF90` (prologue `addiu $sp,$sp,-0x80`) runs this sequence:

1. reads the descriptor table at `0x00282218` and calls the `SetSyscall` wrapper
   `0x0025F090` for `(0x83, 0x0025EF38)` and `(0x5A, 0x0025EF00)`, which are the
   two `0x74` boundaries in the trace;
2. calls the `0x83` wrapper `0x0025EF78` with `(0x80000000, 0x80080000,
   0x0025EF38)` and keeps the result in `$s3`, then repeats with `0x0025EF00`
   and keeps the result in `$s2`;
3. computes `$s1 = $s3 - 0x20C` and `$s0 = $s2 - 0x168`, then loops: while
   `$s1 != $s0`, the smaller one is advanced by another search starting at
   `$sN + 4` for the same handler value, recomputing `$sN - 0x20C` or
   `$sN - 0x168`;
4. stores the final `$s1` to `0x00282210`.

So both searches are meant to locate the *stored copy* of the registered handler
addresses, and the fixed offsets recover object headers from those slots.

Measured against the generated 32 MiB RAM image:

- `0x0025EF38` occurs exactly once, at `0x0028221C`; `0x0025EF00` occurs exactly
  once, at `0x00282224`. Both are the `.data` descriptor table.
- Neither value occurs anywhere in the 512 KiB window `[0, 0x80000)` that the
  search covers, and low RAM is zero-filled in the image.

The searches therefore return `0`, `$s1`/`$s0` become `0xFFFFFDF4`/`0xFFFFFE98`,
and the loop's first iteration starts at address `4` — the run that exhausts the
budget.

That is the root cause: the guest expects its registered handler addresses to be
present in **low EE RAM**, i.e. it expects `SetSyscall` to copy them into a
kernel-side table. ADR 0067 records the registration in host state and writes
nothing to guest memory, so no such table exists. The table address and layout
must come from evidence rather than a guess, because `$s3 - 0x20C` assumes the
slot's position inside a specific kernel object.

Unresolved:

- The kernel table's address, capacity and entry layout. This needs evidence
  from a public behavioural reference or from guest writes observed at the
  `0x74` boundary; it must not be invented.
- Whether the two initial lookups are expected to succeed at all. Every measured
  path says they cannot with the current RAM image, so the not-found fallback is
  what actually runs.

The routine the guest handler at `0x0025EF38` enters is this word scan:

```
0025ef38: 8c820000   lw    $v0, 0($a0)
0025ef3c: 1046000b   beq   $v0, $a2, found
0025ef40: 0085102b   sltu  $v0, $a0, $a1
0025ef44: 5040000a   beql  $v0, $zero, leave
0025ef48: 0002200a   movz  $a0, $zero, $v0
0025ef4c: 24840004   addiu $a0, $a0, 4
0025ef50: 8c820000   lw    $v0, 0($a0)      <- budget-exhausted PC window
0025ef54: 10460005   beq   $v0, $a2, found
0025ef58: 0085102b   sltu  $v0, $a0, $a1
0025ef5c: 5440fffc   bnel  $v0, $zero, 0x0025EF50
0025ef60: 24840004   addiu $a0, $a0, 4
```

It returns the address of the first word equal to `$a2`, or `0` when the range
ends. Because `$a0` starts at `4` in KUSEG while `$a1` is `0x80080000` in KSEG0,
the loop walks the whole KUSEG window instead of the 8 MiB of physical RAM the
`0x80000000` form addresses. REPS2 currently services those reads without a
fault, so the probe exhausts its budget rather than stopping.

Unresolved:

- Whether the two `$sN + 4` sites are legitimate (a module list whose base is
  zero) or reflect a guest register that should be non-zero. Distinguishing the
  two needs a syscall-sequence trace that records every boundary, not only the
  last one.
- Whether the real kernel faults on the unmapped KUSEG reads this range implies.

### EE kernel syscall table (resolved, ADR 0072)

The unresolved questions above are answered by the guest routine itself and by
public evidence:

- The guest offsets are exactly service-number scaled: `-0x20C = -0x83*4` and
  `-0x168 = -0x5A*4`. Subtracting the slot index from a found handler address
  yields the base of a flat array of 32-bit handler pointers.
- ps2sdk `ee/kernel/src/libosd_full.c` documents the same layout: "The address
  is relative to the start of the syscall table, and is in units of 32-bit
  pointers (4 bytes)", and its table comment names `0x80011F80` as "the start of
  the syscall table" (`0x80011F80 + (-15358*4) = 0x80002F88`).
- A ps2dev forum probe installs marker handlers for services `0xFE`/`0xFF`, scans
  `0x80000000..0x80080000` in 4-byte steps, and computes the base as
  `address - syscall_number * 4`. The same thread reports that the base varies
  between BIOS versions, which is consistent with the guest recovering it at
  runtime instead of hardcoding it.

Consequence for HLE: `SetSyscall` must write the registered handler pointer into
guest memory, not only into host state. REPS2 fixes the documented base
`0x80011F80` (1 KiB for 256 entries), writes `handler` at `base + slot*4` after
the existing validation, and refuses with `table_unwritable` if the slot cannot
be written. Entries REPS2 does not service stay zero; no kernel handler address
is fabricated.

Open issues:

- The HLE base is the documented ps2sdk value; a real BIOS may use another
  address. Because the title recovers the base from its own handlers, the
  recovery path is unaffected, but code that hardcodes a different BIOS base is
  not modelled.
- Direct guest writes to the table are visible in main RAM but are not yet
  consulted by HLE dispatch; a title that patches an unregistered slot directly
  needs a separate observed contract before REPS2 honors it.

### Downhill Domination `InitAlarm` boundary (ADR 0073)

Once the guest-visible syscall table exists, the `0x0025EF90` init completes:
both FindAddress searches find the registered handlers at
`0x80011F80 + 0x83*4` and `+ 0x5A*4`, compute the same base, and store it at
`0x00282210`. The next stop moved from the former scan loop to
`pc=0x0025F34C`, which is `lw $v1, 0($v0)` with `$v0 = 0x10001810` — the Timer 3
mode register.

The routine at `0x0025F330` is ps2sdk's `InitAlarm` (`ee/kernel/src/alarm.c`):

- it applies the patch only when `(*T3_MODE) & 0x100 == 0` (compare interrupt
  disabled);
- it registers `kCopy` as service 0x5A, copies a 0x740-byte blob to
  `0x80076000` and the 0x28-byte `eenull` to `0x00082000`, flushes the cache, and
  registers 0x5B;
- it then calls `setup(slot, GetEntryAddress(slot))` for 0xFC, 0xFE, 0xFD, 0xFF,
  0x12C (`Intc12Handler`) and 0x08.

The descriptor table at `0x00283160` matches that source exactly:
`(0x5A, 0x0025F2E8)`, `(0x5B, 0x80076000)`, then the six slots above. Slot
0x12C is beyond the 256 entries assumed in ADR 0067, which is why the host
bound was raised to 512.

`GetEntryAddress`'s arithmetic is pinned by ps2sdk `libosd.c`: its special slot
`0xFFFFC402` documents `0x80011F80 + (-15358*4) = 0x80002F88`, so the call
returns the slot address `base + number*4`, not the handler stored there.

Modelling only the documented Timer 3 mode word (reset 0, writable mode bits
retained, no counting or interrupts) and the 0x5B slot-address service lets the
patch path proceed. The blob itself is copied at run time and is data in the
ELF, so it was admitted with the frontend's explicit `--relocated-code`
`0x80076000:0x002829F8:0x740` input (ADR 0074).

With the blob translated, the probe executes the full patch:

- the translated `0x5B` handler searches the six-entry `(number, handler)`
  table embedded at `0x80076710` and returns 0xFC→`0x80076440`,
  0xFE→`0x80076440`, 0xFD→`0x800762A0`, 0xFF→`0x800762A0`,
  0x12C→`0x80076488`, 0x08→`0x800766C0`;
- each result is written back through `SetSyscall` at the matching slot;
- after another `FlushCache` and a third `CreateSema`, execution reaches
  `CreateThread` (0x20) at `pc=0x0025A4E4` with `$a0=0x0093C7B0`,
  `$a1=0x002A40F0`, `$a2=0x00000400`.

`CreateThread` was the next stop; it is now serviced and the probe stops one
service later. The earlier scan-loop and Timer 3 stops are gone. This is real
guest-path progress, not a visible boot stage.

### EE `CreateThread` boundary and dormant records (ADR 0075)

Facts:

- ps2sdk `kernel.h` declares `s32 CreateThread(ee_thread_t *thread)`, and
  `ee_thread_t` is nine 32-bit words: `status`, `func`, `stack`, `stack_size`,
  `gp_reg`, `initial_priority`, `current_priority`, `attr`, `option`. The same
  header documents `MAX_THREADS 256` and names thread 0 as the idle thread.
- The retail caller at `0x0025B1A8` (the game's `InitThread`) builds the block
  on its own stack, leaves `status` uninitialized, calls `CreateThread`, stores
  the returned id at `0x00282150`, and then calls `StartThread` with an argv
  block at `0x00761208`.
- The observed call is `pc=0x0025A4E4`, service `0x20`, with
  `$a0=0x0093C7B0` (the block, equal to `$sp`), `$a1=0x002A40F0`,
  `$a2=0x00000400`, `$a3=0x0029E580`. The block holds `func=0x0025B0D0`,
  `stack=0x00760E00`, `stack_size=0x400`, `gp=0x002A40F0`,
  `initial_priority=0`. All of those resolve inside EE main RAM.
- ps2sdk `kernel.S` wraps the service as an ordinary `syscall`, and callers test
  the `s32` result for `< 0`; no specific negative code is documented.

Decision and limits:

- REPS2 validates the block and returns an id from a bounded host record table
  (ids start at 1). Any refusal returns `-1`, allocates nothing, and records an
  explicit reason.
- No guest memory is written. The parameter block is input only; the guest's
  uninitialized `status` word is never read or rewritten.
- No thread status, stack image, argv block, ready-queue entry or scheduler
  state is created. `StartThread`, `ExitThread`, `ReferThreadStatus`, priority
  changes and deletion remain unimplemented; their guest-visible contracts are
  not inferred.

Observed result after servicing the call:

- The probe reports `syscalls=29`, `hle_handled=18`, `status=14`
  (`unhandled_syscall`) at `pc=0x0025A504`, `executed=2,946,363`.
- Sequence records:
  `0x0025A4E4:32:H:0x0093C7B0:0x002A40F0:0x00000400:0x0029E580:0x0025B0D0`
  (handled `CreateThread`), then
  `0x0025A504:34:U:0x00000002:0x00761208:0x00000400:0x0029E580:0x00760000`
  (unhandled `StartThread`, `$a0=2` = the returned id, `$a1` = argv).

Public behavioural references (retrieved 2026-11-06):

- ps2sdk `ee/kernel/include/kernel.h` fixes the struct offsets (`status` 0x00,
  `func` 0x04, `stack` 0x08, `stack_size` 0x0C, `gp_reg` 0x10,
  `initial_priority` 0x14, `current_priority` 0x18, `attr` 0x1C,
  `option` 0x20, documented "Do not use - officially documented to not work"),
  `MAX_THREADS 256` with "Thread 0 is always the idle thread", and the status
  encoding `THS_RUN 0x01`, `THS_READY 0x02`, `THS_WAIT 0x04`,
  `THS_SUSPEND 0x08`, `THS_WAITSUSPEND 0x0C`, `THS_DORMANT 0x10`.
- ps2sdk `ee/kernel/src/thread.c` leaves `ee_thread_t.status` unset, checks
  `(topId = CreateThread(&thread)) < 0`, calls
  `StartThread(topId, &topArg)` without testing the result, then
  `ChangeThreadPriority(GetThreadId(), 1)`; its guard uses `priority < 128`.
  The second call proves the running thread and the newly created thread must
  have distinct ids.
- ps2tek's kernel documentation states that boot "creates a thread with
  priority 0 (main thread)" before guest code runs.
- Open PS2 Loader patch notes record titles that hardcode the main thread id for
  `WakeupThread`, so exact id values can matter to titles outside this probe;
  REPS2 therefore keeps ids explicit and never derives them from an opaque
  pointer.

Open questions for the next increment:

- `StartThread` semantics: ps2sdk declares `s32 StartThread(s32 thid, void
  *args)` and documents a DORMANT→READY transition with ready-queue insertion;
  whether the call may switch immediately to a higher-priority thread is a
  scheduler property, not a return-value property.
- How argv is exposed to the new thread (`$a0`/`$sp` layout) and when
  `root_func` from `SetupThread` is consumed.
- `attr`/`option` bits, stack guard behaviour, and the exact negative failure
  codes beyond the documented "negative means failure".

### EE `StartThread`, `GetThreadId` and `ChangeThreadPriority` (ADR 0076)

Facts from the same caller (`0x0025B1A8`) and the syscall wrappers:

- `0x0025A4E0` is `li $v1,0x20; syscall` (CreateThread). `0x0025A500` is
  `li $v1,0x22; syscall` (StartThread). `0x0025A5D0` is `li $v1,0x2F; syscall`
  (GetThreadId). `0x0025A570` is `li $v1,0x29; syscall`
  (ChangeThreadPriority). Each wrapper returns immediately with `jr $ra`.
- After `CreateThread($sp)` the code tests the result with `bgez`; on success it
  stores the id at `0x00282150`, zeroes the two-word argv block at
  `0x00761208`, and calls `StartThread(id, 0x00761208)`.
- It then calls `GetThreadId()`, moves `$v0` into `$a0`, and calls
  `ChangeThreadPriority(that_id, 1)`.
- At creation time the guest passes `initial_priority=0`; ps2tek records that
  boot creates the main thread with priority 0, so the started thread and the
  running thread begin at equal priority. Equal priorities are queued FIFO, so
  no immediate switch is implied by the observed sequence.

Public references: see the ps2sdk and ps2tek entries in the CreateThread section.
ps2sdk defines `THS_DORMANT 0x10` and `THS_READY 0x02`, and its example calls
`ChangeThreadPriority(GetThreadId(), 1)` right after starting a thread, which
requires the running thread and the created thread to have different ids.

Decision and limits:

- `StartThread` accepts only an existing DORMANT record, stores `args` opaquely,
  sets READY and inserts by ascending current priority. Refusals return `-1`.
- `GetThreadId` returns the boot thread id (1) because REPS2 has one context.
- `ChangeThreadPriority` accepts 0..127 and re-orders a READY thread.
- No service switches context. The boot thread continues; a later blocking
  service is where a dispatcher becomes observable, and it stays unimplemented
  until that service's contract is established.

Observed result after servicing the chain (2026-11-06):

- `status=14`, `pc=0x0025A794`, `executed=2,946,399`, `syscalls=32`,
  `hle_handled=21`.
- Records: `StartThread`
  `0x0025A504:34:H:0x00000002:0x00761208:0x00000400:0x0029E580:0x00760000`,
  `GetThreadId`
  `0x0025A5D4:47:H:0x00000002:0x00761208:0x00000400:0x0029E580:0x00000000`,
  `ChangeThreadPriority`
  `0x0025A574:41:H:0x00000001:0x00000001:0x00000400:0x0029E580:0x00000001`,
  then unhandled `0x0025A794:75:U:0x0093C7C0:0x00000001:0x00000400:0x0029E580:0x00000002`.
- The `ChangeThreadPriority` arguments confirm `GetThreadId` returned 1 (the
  boot thread) and the guest lowered that thread to priority 1. The created
  thread record (id 2) stays at priority 0, so it would outrank the boot thread
  if a dispatcher existed. The guest then performs the OSD configuration probe.
- Next boundary: `GetOsdConfigParam` (0x4B) at `pc=0x0025A794` with
  `$a0=0x0093C7C0`. The caller at `0x0025F148` reads four bytes, clears bits
  13..15, sets bit 13, writes, reads back, restores, and reports whether bits
  13..14 are zero. `SetOsdConfigParam` (0x4A) is part of the same probe.

### EE OSD configuration services (ADR 0077)

Facts:

- ps2sdk declares `void GetOsdConfigParam(void *addr)` and
  `void SetOsdConfigParam(void *addr)`; both return void. The game's wrappers at
  `0x0025A790` and `0x0025A780` carry service numbers 0x4B and 0x4A.
- The observed call passes a 4-byte stack buffer at `$a0=0x0093C7C0`. The
  caller at `0x0025F148` reads it, clears bits 13..15 (`& 0xFFFF1FFF`), sets bit
  13 (`| 0x2000`), writes it with 0x4A, reads it back with 0x4B, restores the
  original with 0x4A again, and returns `(((value >> 13) & 7) < 1)`.
- The kernel's backing store is NVRAM/EEPROM, which REPS2 does not have and
  must not claim.

Decision and limits:

- One 32-bit host-session word, initialized to 0, is served by 0x4B and stored
  verbatim by 0x4A through the validated EE address path.
- Refusals (unmapped, unsupported physical window, misalignment) record a
  reason and change nothing. `$v0 = -1` is diagnostic only because the documented
  return type is void.
- Persistence, board-specific defaults and bit policy are not implemented or
  invented. Services depending on language/aspect bits see the zeroed default.

Observed result (2026-11-06):

- `status=4` (`unresolved_control_flow`), `pc=0x80075000`,
  `executed=2,948,141`, `syscalls=42`, `hle_handled=30`.
- Both directions of the OSD probe are handled twice, then boot installs its
  second copy path: `SetSyscall(0x5A, 0x0025F570)`, a translated guest copy of
  0x330 bytes from `0x002831A0` to `0x80075000`, two `FlushCache` calls,
  `SetSyscall(0x5B, 0x80075000)` and `SetSyscall(0x54, 0x0025F940)`.
- Service 0x5B then dispatches into the copied blob. The region is not declared
  for translation, so the probe reports unresolved control flow at
  `0x80075000`. The next dependency is `--relocated-code` coverage, not another
  service.

## Provenance rules


1. REPS2 source is independently authored.
2. Public specifications may be cited. Keep source notes adjacent to non-obvious semantics.
3. External tools may be queried for observable behavior; record test inputs and outputs, not copied implementation code.
4. Do not add game ISOs, BIOS images, proprietary SDKs, copyrighted assets, or extracted retail code to this repository.
5. Synthetic fixtures must be authored by REPS2 contributors or generated by tests.
6. If references disagree, record the disagreement and create a minimal reproducible test before choosing behavior.
