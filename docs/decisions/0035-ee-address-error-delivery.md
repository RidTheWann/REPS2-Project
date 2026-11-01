# ADR 0035: EE data-address exception delivery (AdEL / AdES)

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** EE main-RAM load/store lowering, LWC1/SWC1, COP0 BadVAddr, precise branch-delay exception entry, and native regression coverage

## Context

The runtime's checked memory helpers already reject naturally misaligned halfword, word and doubleword accesses with `Status::address_error`. Generated guest code previously surfaced that value as a host-side status and stopped at the faulting PC. It did not create the guest-visible COP0 state needed by a translated exception handler: Cause.ExcCode, BadVAddr, EPC, Cause.BD, Status.EXL and the selected common exception vector.

The existing Level-1 helper already centralizes synchronous exception entry and the nested-EXL rule. Data-alignment faults can therefore use the same transition without widening this increment into TLB translation, privilege checks, bus-error delivery or a general memory-system rewrite.

## Architectural evidence

Sony's *Emotion Engine Core User's Manual*, Version 6.0, section 4.2.9 (Address Error Exception), specifies:

- AdEL uses Cause.ExcCode 4 for a misaligned instruction fetch or load.
- AdES uses Cause.ExcCode 5 for a misaligned store.
- BadVAddr receives the virtual address that directly caused the address error.
- The synchronous common vector is `0x80000180` when BEV=0 and `0xBFC00380` when BEV=1.
- Exception entry preserves the established EPC/BD restart convention when the fault occurs in a branch delay slot.

PS2Tek's COP0 reference provides an independent cross-check for the BadVAddr and Cause fields.

References:

- Sony, *Emotion Engine Core User's Manual*, Version 6.0, section 4.2.9: https://docs.alexrp.com/mips/ee.pdf
- PS2Tek, COP0 / exception reference: https://psi-rockin.github.io/ps2tek/

## Decision

### 1. Keep address-error cause codes distinct from the existing arithmetic/trap enum

Add `AddressErrorCode::Load = 4` and `AddressErrorCode::Store = 5`. This makes the load/store distinction explicit at the call site while reusing the existing Level-1 entry mechanism.

The runtime helper `raise_ee_address_error` first writes the original 32-bit guest virtual address to COP0.BadVAddr, then delegates to `raise_ee_exception`. The shared helper selects V_COMMON, sets EXL and records EPC/BD when EXL was clear. When EXL is already set, the first EPC/BD frame remains intact, while the new ExcCode and BadVAddr are published.

### 2. Promote only checked natural-alignment failures

Generated code translates `Status::address_error` to a guest exception:

- Naturally aligned-width loads: LH, LHU, LW, LWU and LD raise AdEL (4).
- Naturally aligned-width stores: SH, SW and SD raise AdES (5).
- LWC1/SWC1 use the same checked 32-bit memory helpers and follow AdEL/AdES respectively.

The effective address is computed once and passed unchanged to BadVAddr. No destination register or memory bytes are committed after a fault.

All other memory failures retain their existing explicit statuses. In particular, `unmapped_memory`, `unsupported_memory_access`, short backing spans, and unimplemented device/MMIO regions are not relabeled as AdEL/AdES. This does not claim TLB translation, TLB refill/invalid handling, privilege checking, instruction-fetch faults or bus-error delivery.

### 3. Preserve EE operations that are intentionally unaligned

- Byte loads/stores have no natural-alignment fault.
- LWL/LWR/LDL/LDR and SWL/SWR/SDL/SDR continue using their existing aligned merge accesses.
- LQ/SQ retain the EE-specific 16-byte effective-address rounding already modeled by the runtime; they are not changed to raise an alignment exception.

### 4. Carry delay-slot context into generated memory accesses

The ordinary memory emitters receive the existing `in_branch_delay` flag. A checked data-alignment fault inside an admitted branch delay slot calls the shared helper with that flag, so EPC identifies the owning branch and Cause.BD is set. FPU word loads/stores use the same policy. The CFG's existing rejection of SYSCALL/BREAK/ERET control terminators in delay slots is unchanged.

## Validation

The synthetic native regression fixtures cover:

- LW and LD AdEL entry with ExcCode 4, exact EPC, BadVAddr and clear BD.
- SW AdES entry with ExcCode 5, exact EPC/BadVAddr and no partial memory write.
- SW AdES in a taken BEQ delay slot, with EPC set to the BEQ, BD set, BadVAddr equal to the unaligned effective address, and memory unchanged.
- Nested address error behavior: BadVAddr/ExcCode update while the previous EPC/BD restart frame is preserved.
- Existing MMIO, unmapped-memory and low-level helper tests remain separate and continue expecting their distinct statuses.
- Rust code-generation assertions verify that load and store paths emit the architectural helper instead of leaking `Status::address_error`.

Native generated-code and Rust tests are required on both Windows/MSVC and Linux/GCC CI. The fixtures are synthetic and prove only these isolated semantics; they do not establish title compatibility or playability.

## Consequences and remaining limits

- The currently implemented EE data memory slice now enters a guest-visible Level-1 exception for supported natural-alignment faults.
- The exception helper and generated code share one EPC/BD/vector policy rather than duplicating exception state logic.
- BadVAddr records the virtual effective address, not the host backing-store offset.
- Instruction-fetch AdEL, TLB faults, privilege faults, bus errors, cache semantics, asynchronous interrupts, device/MMIO models, BIOS/HLE services and full COP0 semantics remain incomplete.
- REPS2 remains an experimental multi-game AOT foundation; no real-title boot or Playable claim follows from these synthetic tests.
