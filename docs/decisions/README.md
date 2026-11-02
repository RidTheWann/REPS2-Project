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
| [0013](0013-r5900-instruction-effect-metadata.md) | conservative R5900 instruction-effect metadata | — |
| [0014](0014-r5900-mult-hilo.md) | base EE HI/LO and R5900 three-operand MULT/MULTU | — |
| [0015](0015-r5900-compare-nor.md) | R5900 64-bit comparisons and NOR | — |
| [0016](0016-r5900-trapping-doubleword-alu.md) | trapping R5900 64-bit DADD, DSUB and DADDI | — |
| [0017](0017-r5900-branch-likely-annulment.md) | R5900 branch-likely annulment and unconditional link writes | — |
| [0018](0018-ee-address-alias-policy.md) | EE Address Alias Policy and Main-RAM Segment Translation | — |
| [0019](0019-mmi-sa-and-qfsrv.md) | MMI SA Register Modeling and QFSRV Funnel Lowering | — |
| [0020](0020-cop1-data-movement.md) | COP1 Data Movement Against the Verified EE Transfer Format | — |
| [0021](0021-cop1-arithmetic.md) | COP1 Single-Precision Arithmetic on the EE's Non-IEEE Unit | — |
| [0022](0022-cop1-compare-and-bc1.md) | COP1 Compares, the FCR31 Condition Bit, and BC1 Branches | — |
| [0023](0023-special-fences-div-and-conditional-moves.md) | Memory Barrier (SYNC), Integer Division (DIV/DIVU), and Conditional Moves (MOVN/MOVZ) | — |
| [0024](0024-coalesce-pure-bss-pt-load-segments.md) | Coalesce Pure BSS PT_LOAD Segments for SN Systems ProDG Compatibility | — |
| [0025](0025-cop0-register-transfers-and-status-manipulation.md) | COP0 Register Transfers (MFC0/MTC0) and Interrupt Control (EI/DI/ERET) | — |
| [0026](0026-static-function-discovery.md) | Static function-root discovery for stripped retail executables | — |
| [0027](0027-multi-translation-unit-codegen.md) | Per-block functions and multi-translation-unit CFG codegen | — |
| [0028](0028-deterministic-build-manifest.md) | Deterministic build manifest for `recompile-elf` | — |
| [0029](0029-native-build-driver.md) | Generated native build project and host probe | — |
| [0030](0030-analysis-limit-policy.md) | Explicit analysis-limit policy and CLI overrides | — |
| [0031](0031-guest-trap-status.md) | Explicit SYSCALL/BREAK guest-trap statuses | — |
| [0032](0032-native-build-toolchain-provenance.md) | Native host-toolchain provenance | — |
| [0033](0033-ee-level1-exception-and-eret.md) | EE Level-1 SYSCALL/BREAK exception delivery and ERET | — |
| [0034](0034-ee-overflow-exception-delivery.md) | EE signed-overflow exception delivery and general-vector correction | — |
| [0035](0035-ee-address-error-delivery.md) | EE data-address exception delivery (AdEL / AdES) | — |
| [0036](0036-pt-load-ee-ram-image.md) | PT_LOAD to default EE main-RAM image | Accepted |
| [0037](0037-ee-traps-in-delay-slots.md) | EE SYSCALL/BREAK delay-slot exception delivery and branch-likely annulment | Accepted |
| [0038](0038-elf-symbol-table-validation.md) | ELF string, symbol-table and section cross-reference validation | Accepted |
| [0039](0039-mips-elf-flag-diagnostics.md) | Diagnostic MIPS CPU, ABI and ISA decoding from ELF e_flags | Accepted |
