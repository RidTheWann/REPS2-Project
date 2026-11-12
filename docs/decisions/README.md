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
| [0067](0067-ee-setsyscall-registration.md) | EE SetSyscall registration boundary | Accepted |
| [0068](0068-defer-ee-findaddress-hle.md) | Defer EE FindAddress HLE pending ABI evidence | Accepted |
| [0069](0069-ee-guest-syscall-handlers.md) | EE guest syscall handlers invoked through SetSyscall | Accepted |
| [0070](0070-section-permission-pointer-discovery.md) | Section-declared executable ranges drive pointer-table discovery | Accepted |
| [0071](0071-syscall-sequence-trace.md) | Bounded EE syscall-boundary sequence trace | Accepted |
| [0072](0072-guest-visible-ee-syscall-table.md) | Guest-visible EE syscall table written by SetSyscall | Accepted |
| [0073](0073-ee-initalarm-kernel-patch-path.md) | EE InitAlarm kernel-patch path (`T3_MODE` guard and 0x5B) | Accepted |
| [0074](0074-relocated-code-regions.md) | Relocated code regions for runtime-copied guest code | Accepted |
| [0075](0075-ee-createthread-dormant-records.md) | EE CreateThread dormant-thread records | Accepted |
| [0076](0076-ee-start-thread-ready-queue.md) | EE StartThread ready-queue model with GetThreadId and ChangeThreadPriority | Accepted |
| [0077](0077-ee-osd-config-host-store.md) | EE OSD configuration services as a documented host-session store | Accepted |
| [0078](0078-vu0-data-movement.md) | VU0 Macro-Mode Quadword Data Movement | Accepted |
| [0079](0079-ee-dmac-dstat-contract.md) | Guarded EE DMAC D_STAT Register Contract | Accepted |
| [0080](0080-session-scoped-developer-diagnostics.md) | Session-scoped developer diagnostics | Accepted |
| [0081](0081-agent-readable-developer-cli.md) | Agent-readable developer CLI reports | Accepted |
| [0082](0082-pipeline-stage-diagnostics.md) | Frontend pipeline-stage diagnostics for agent triage | Accepted |
| [0083](0083-native-build-identity-fingerprint.md) | Native build identity and configuration-specific cache fingerprint | Accepted |
| [0084](0084-ee-mmi0-pext5-ppac5.md) | EE MMI0 PEXT5/PPAC5 5-bit color conversion | Accepted |
| [0085](0085-fatal-cfg-analysis-limits.md) | Keep CFG resource and invariant failures fatal on speculative paths | Accepted |
| [0086](0086-developer-capability-discovery.md) | Developer capability discovery and stable diagnostic-code catalog | Accepted |
| [0090](0090-structured-session-summary.md) | Structured session summary for agent triage | Accepted |
| [0091](0091-ee-dmac-boot-state-and-mmio-dispatch.md) | MMIO register dispatch table and the HLE DMAC boot state | Accepted |
| [0092](0092-hle-sce-sif-set-d-chain.md) | HLE `sceSifSetDChain` starts the SIF0 channel | Accepted |
| [0093](0093-reproduction-metadata.md) | Record the invocation for reproduction in the session summary | Accepted |
| [0094](0094-differential-session-comparison.md) | Differential session comparison and a minimal JSON reader | Accepted |
| [0095](0095-hle-add-dmac-handler.md) | HLE `AddDmacHandler` records a DMAC channel handler | Accepted |
| [0096](0096-hle-enable-dmac.md) | HLE `_EnableDmac` sets the D_STAT channel interrupt mask | Accepted |
| [0097](0097-hle-sif-get-reg.md) | HLE `sceSifGetReg` and the SIF register file | Accepted |
| [0098](0098-iop-sifcmd-command-model.md) | IOP-side SIFCMD command model and the SIF register/DMA services | Accepted |
| [0099](0099-resident-iop-sifcmd.md) | Resident IOP SIFCMD device and SIF register ownership | Accepted |
| [0100](0100-iop-to-ee-sif0-rpcinit.md) | IOP-to-EE SIF0 RPCINIT handshake | Accepted |
| [0101](0101-ee-semaphore-signal-wait.md) | EE semaphore signal/wait services and the honest WaitSema block | Accepted |
| [0102](0102-mips-section-classification.md) | MIPS processor-specific section classification | Accepted |
| [0103](0103-ee-intc-register-model.md) | EE INTC register model (I_STAT / I_MASK) | Accepted |
| [0104](0104-iop-sif-rpc-bind-reply.md) | IOP SIF RPC bind reply with no modelled IOP server | Accepted |
| [0105](0105-ee-dmac-register-naming-and-interrupt-condition.md) | EE DMAC register naming and the interrupt-line condition | Accepted |
| [0106](0106-host-media-boundary.md) | Host media boundary for disc I/O | Accepted |
| [0107](0107-rg-reps2-engine-native-module-boundary.md) | RG REPS2 ENGINE native-module boundary | Accepted |
| [0108](0108-rg-reps2-engine-session-and-budget.md) | RG REPS2 ENGINE session and execution-budget contract | Accepted |
| [0109](0109-rg-reps2-engine-dynamic-module-loading.md) | RG REPS2 ENGINE dynamic-module ABI, loading and lifetime | Accepted |
| [0110](0110-rg-reps2-engine-bounded-host-runner.md) | RG REPS2 ENGINE bounded host runner and JSON execution report | Accepted |
| [0111](0111-recompile-iso-native-preparation.md) | Single-command ISO-to-native artifact preparation | Accepted |
| [0113](0113-windows-x64-installed-toolchain-and-unicode-loading.md) | Windows x64 installed toolchain and Unicode native-module loading | Accepted |
| [0114](0114-sifrpc-server-table-and-call-dispatch.md) | SIFRPC server table, opaque server token and HLE call dispatch | Accepted |
| [0115](0115-disable-dmac-and-dstat-mask-symmetry.md) | `_DisableDmac` and symmetric D_STAT interrupt-mask model | Accepted |
| [0116](0116-remove-dmac-handler.md) | `RemoveDmacHandler` and bounded handler-record lifetime | Accepted |
| [0117](0117-sif-stop-dma.md) | `sceSifStopDma` and the SIF0 start-bit stop model | Accepted |
| [0118](0118-modelled-iop-reset-completion.md) | Modelled IOP reset completion and the post-reset SIF flags | Accepted |
| [0119](0119-pollsema-non-blocking-acquire.md) | `PollSema` and non-blocking semaphore acquisition | Accepted |
| [0120](0120-refer-thread-status.md) | `ReferThreadStatus` and the thread status block | Accepted |
| [0121](0121-register-cdvd-scmd-server.md) | Register the CDVDFSV S-command RPC server (`0x80000593`) | Accepted |
| [0122](0122-cdvd-scmd-mmode.md) | Implement the first CDVD S-command `CD_SCMD_MMODE` (0x22) | Accepted |
| [0123](0123-engine-session-hle-boot-environment.md) | `EngineSession` seeds the modelled HLE boot environment when HLE is enabled | Accepted |
| [0124](0124-cdvd-diskready-rpc-server.md) | Serve the CDVDFSV DiskReady RPC servers (`0x8000059A`/`0x8000059C`) from host media presence | Accepted |
| [0125](0125-cdvd-ncmd-searchfile-registry-and-ncmd-diskready.md) | Register NCmd/SearchFile servers; serve NCmdDiskReady (fno 14) from host media presence | Accepted |
| [0126](0126-admit-delay-slot-entry-targets.md) | Admit control-flow targets that enter another instruction's delay slot as ordinary block starts | Accepted |
| [0127](0127-discovery-scans-relocated-regions.md) | Function discovery also scans declared relocated-code regions | Accepted |
| [0128](0128-run-report-gpr-snapshot.md) | Engine run report includes a 32-lane GPR snapshot for stop triage | Accepted |
| [0129](0129-ee-timer3-count-frontier.md) | Correct the Downhill Timer 3 MMIO frontier (`T3_COUNT`) | Accepted |
| [0130](0130-kseg-alias-mmio-dispatch.md) | Normalize KSEG0/KSEG1 aliases before EE MMIO dispatch | Accepted |
| [0131](0131-ee-timer3-compare-register.md) | EE Timer 3 compare register and run-report timer state | Accepted |
| [0132](0132-r5900-madd-maddu.md) | R5900 MADD/MADDU HI/LO accumulation decode, lift, and AOT lowering | Accepted |
