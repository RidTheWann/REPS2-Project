# Architecture decision records

Each ADR is numbered once and never reused. Add the next record with the next unused number (highest + 1);
the `adr_numbers_are_unique_and_match_titles` test in `rust/frontend/tests/repo_hygiene.rs` enforces this.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-rust-frontend-cpp-runtime.md) | Rust frontend and C++20 runtime | Accepted |
| [0002](0002-scalar-ir-and-cpp-aot.md) | typed scalar IR and first C++ AOT vertical slice | Accepted |
| [0003](0003-reachability-cfg-and-delay-slots.md) | Reachability-guided CFG and explicit delay slots | Accepted |
| [0004](0004-cfg-aware-control-flow-aot.md) | CFG-aware typed IR and bounded C++ dispatcher | Accepted |
| [0005](0005-ee-main-ram-load-store.md) | Bounded EE main-RAM load/store slice | Accepted |
| [0006](0006-ee-quadword-memory.md) | EE quadword memory operations | Accepted |
| [0007](0007-ee-doubleword-load-store.md) | EE doubleword load/store operations | Accepted |
| [0008](0008-ee-unaligned-left-right-memory.md) | EE left/right partial memory operations | Accepted |
| [0009](0009-iso9660-boot-discovery.md) | Read-only ISO 9660 boot executable discovery | Accepted |
| [0010](0010-multi-game-library-import.md) | Content-addressed multi-game ISO library | Accepted |
| [0011](0011-r5900-doubleword-scalar-operations.md) | Non-trapping R5900 doubleword scalar lowering | Accepted |
| [0012](0012-r5900-register-variable-shifts.md) | Register-variable R5900 shifts | Accepted |
| [0013](0013-r5900-instruction-effect-metadata.md) | conservative R5900 instruction-effect metadata | Accepted |
| [0014](0014-r5900-mult-hilo.md) | base EE HI/LO and R5900 three-operand MULT/MULTU | Accepted |
| [0015](0015-r5900-compare-nor.md) | R5900 64-bit comparisons and NOR | Accepted |
| [0016](0016-r5900-trapping-doubleword-alu.md) | trapping R5900 64-bit DADD, DSUB and DADDI | Accepted |
| [0017](0017-r5900-branch-likely-annulment.md) | R5900 branch-likely annulment and unconditional link writes | Accepted |
| [0018](0018-ee-address-alias-policy.md) | EE Address Alias Policy and Main-RAM Segment Translation | Accepted |
| [0019](0019-mmi-sa-and-qfsrv.md) | MMI SA Register Modeling and QFSRV Funnel Lowering | Accepted |
| [0020](0020-cop1-data-movement.md) | COP1 Data Movement Against the Verified EE Transfer Format | Accepted |
| [0021](0021-cop1-arithmetic.md) | COP1 Single-Precision Arithmetic on the EE's Non-IEEE Unit | Accepted |
| [0022](0022-cop1-compare-and-bc1.md) | COP1 Compares, the FCR31 Condition Bit, and BC1 Branches | Accepted |
| [0023](0023-special-fences-div-and-conditional-moves.md) | Memory Barrier (SYNC), Integer Division (DIV/DIVU), and Conditional Moves (MOVN/MOVZ) | Accepted |
| [0024](0024-coalesce-pure-bss-pt-load-segments.md) | Coalesce Pure BSS PT_LOAD Segments for SN Systems ProDG Compatibility | Accepted |
| [0025](0025-cop0-register-transfers-and-status-manipulation.md) | COP0 Register Transfers (MFC0/MTC0) and Interrupt Control (EI/DI/ERET) | Accepted |
| [0026](0026-static-function-discovery.md) | Static function-root discovery for stripped retail executables | Accepted |
| [0027](0027-multi-translation-unit-codegen.md) | Per-block functions and multi-translation-unit CFG codegen | Accepted |
| [0028](0028-deterministic-build-manifest.md) | Deterministic build manifest for `recompile-elf` | Accepted |
| [0029](0029-native-build-driver.md) | Generated native build project and host probe | Accepted |
| [0030](0030-analysis-limit-policy.md) | Explicit analysis-limit policy and CLI overrides | Accepted |
| [0031](0031-guest-trap-status.md) | Explicit SYSCALL/BREAK guest-trap statuses | Accepted |
| [0032](0032-native-build-toolchain-provenance.md) | Native host-toolchain provenance | Accepted |
| [0033](0033-ee-level1-exception-and-eret.md) | EE Level-1 SYSCALL/BREAK exception delivery and ERET | Accepted |
| [0034](0034-ee-overflow-exception-delivery.md) | EE signed-overflow exception delivery and general-vector correction | Accepted |
| [0035](0035-ee-address-error-delivery.md) | EE data-address exception delivery (AdEL / AdES) | Accepted |
| [0036](0036-pt-load-ee-ram-image.md) | PT_LOAD to default EE main-RAM image | Accepted |
| [0037](0037-ee-traps-in-delay-slots.md) | EE SYSCALL/BREAK delay-slot exception delivery and branch-likely annulment | Accepted |
| [0038](0038-elf-symbol-table-validation.md) | ELF string, symbol-table and section cross-reference validation | Accepted |
| [0039](0039-mips-elf-flag-diagnostics.md) | Diagnostic MIPS CPU, ABI and ISA decoding from ELF e_flags | Accepted |
| [0040](0040-elf-extended-section-symbol-indices.md) | ELF extended section numbering and SHT_SYMTAB_SHNDX | Accepted |
| [0041](0041-elf-extended-program-header-count.md) | ELF PN_XNUM extended program-header count | Accepted |
| [0042](0042-elf-identification-and-type-diagnostics.md) | ELF OS ABI, ABI version, and object-type diagnostics | Accepted |
| [0043](0043-mips-reginfo-and-abiflags.md) | MIPS .reginfo and .MIPS.abiflags metadata diagnostics | Accepted |
| [0044](0044-mips-options-section-diagnostics.md) | MIPS SHT_MIPS_OPTIONS descriptor diagnostics | Accepted |
| [0045](0045-mips-pt-reginfo-segment-diagnostics.md) | MIPS PT_MIPS_REGINFO program-header diagnostics | Accepted |
| [0046](0046-mips-pt-abiflags-segment-diagnostics.md) | MIPS PT_MIPS_ABIFLAGS program-header diagnostics | Accepted |
| [0047](0047-ee-instruction-fetch-address-error.md) | EE instruction-fetch address error on misaligned PCs | Accepted |
| [0048](0048-ee-conditional-trap-instructions.md) | EE conditional trap instruction lowering and precise exceptions | Accepted |
| [0049](0049-ee-mmi0-parallel-word-add-sub.md) | EE MMI0 PADDW/PSUBW modulo lane arithmetic | Accepted |
| [0050](0050-ee-mmi0-parallel-halfword-add-sub.md) | EE MMI0 PADDH/PSUBH modulo lane arithmetic | Accepted |
| [0051](0051-elf-empty-symbol-table.md) | Accept empty ELF symbol tables | Accepted |
| [0052](0052-ee-mmi2-pcpyld.md) | EE MMI2 PCPYLD low-doubleword copy | Accepted |
| [0053](0053-ee-mmi3-por.md) | EE MMI3 POR 128-bit bitwise OR | Accepted |
| [0054](0054-ee-mmi0-pextlw.md) | EE MMI0 PEXTLW low-word interleave | Accepted |
| [0055](0055-ee-mmi0-ppacw-and-mmi3-pcpyud.md) | EE MMI0 PPACW and MMI3 PCPYUD word pack/copy | Accepted |
| [0056](0056-ee-mmi2-pexew-and-prot3w.md) | EE MMI2 PEXEW and PROT3W single-source word permutations | Accepted |
| [0057](0057-ee-mmi1-paddub.md) | EE MMI1 PADDUB unsigned byte saturation | Accepted |
| [0058](0058-ee-mmi2-pand.md) | EE MMI2 PAND 128-bit bitwise AND | Accepted |
| [0059](0059-ee-mmi0-pextlb.md) | EE MMI0 PEXTLB low-byte interleave | Accepted |
| [0060](0060-native-probe-execution-trace.md) | Native probe execution trace and configurable budget | Accepted |
| [0061](0061-ee-mmi2-pxor-and-mmi3-pnor.md) | EE MMI2 PXOR and MMI3 PNOR 128-bit bitwise XOR/NOR | Accepted |
| [0062](0062-ee-syscall-hle-dispatch.md) | EE syscall HLE dispatch and the SetupThread service | Accepted |
| [0063](0063-syscall-continuation-discovery.md) | Discover the continuation after a SYSCALL trap | Accepted |
| [0064](0064-ee-setupheap-hle.md) | Conservative EE SetupHeap HLE | Accepted |
| [0065](0065-ee-syscall-trace-arguments.md) | Capture EE syscall boundary arguments | Accepted |
| [0066](0066-ee-createsema-hle.md) | EE CreateSema HLE with a bounded semaphore table | Accepted |
