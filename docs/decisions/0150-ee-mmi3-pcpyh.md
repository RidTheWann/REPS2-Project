# ADR 0150: Decode, lift and AOT-lower R5900 MMI3 PCPYH

- **Status:** Proposed (validated locally; real-title probe recorded below).
- **Date:** 2026-11-16.
- **Scope:** R5900 MMI3 decoder (`rust/frontend/src/mips.rs`), typed scalar IR
  (`rust/frontend/src/ir.rs`), C++ AOT lowering (`rust/frontend/src/codegen.rs`),
  128-bit GPR helper (`runtime/include/reps2/runtime/ee_state.hpp`), CMake
  fixture/smoke (`CMakeLists.txt`, `tests/mmi3_pcpyh_smoke.cpp`). No runtime ABI
  change: the helper is a `constexpr` function over `Gpr128`, and no engine
  state or report field was added.

## Context

The previous real-title frontier (ADR 0149) was an `unsupported_instruction`
stop at `pc=0x00254230` after 4,416,745 instructions. Documentation recorded
that word as a "BSHFL-family byte swap". That label was **wrong**.

- **Observed (owner's boot RAM image, `PostADR150/boot.ram.bin`, guest word at
  `0x00254230`):** `0x70081EE9`.
- **Derived (R5900 encoding):** `0x70081EE9` = primary opcode `0x1C` (MMI),
  funct `0x29` (MMI3), five-bit selector in bits 10..6 `0x1B`, `rs == 0`,
  `rt == 8`, `rd == 3` — the MMI3 **PCPYH** encoding, i.e. `pcpyh $3, $8`.
  The word is not BSHFL: BSHFL is primary opcode `0x1F` (`SPECIAL3`) with
  funct `0x20` and a `sa` selector, a different family that the R5900 does not
  use for PCPYH. Cross-checked against the public `r5900-opcodes` table
  (`pcpyh 31..26=28 ... 10..6=0b011011 5..0=0b101001`) and the PCSX2 /
  `ps2-unpacker` MMI3 dispatch (`Sa == 0x1B`).
- **Derived (surrounding routine, guest words `0x00254204..0x00254240`):**
  `sltiu $2, $6, 8`, an alignment split (`andi $2, $4, 0xF`), a byte pattern
  built with `andi $9, $5, 0xFF` / `dsll` / `or`, then `pcpyh $3, $8`,
  `pcpyld $8, $3, $3` and `sq $8, 0($7)`. That sequence broadcasts one byte
  into a repeated 16-byte pattern, so the routine is consistent with a
  16-byte block `memset`, not a code copy. PCPYH is the halfword half of that
  construction.

PCPYH is a single-source halfword splat: each 64-bit half of `rd` takes the
lowest 16-bit lane of the same half of `rt` and repeats it across that half's
four halfword lanes. `rs` is reserved zero. It writes the full 128-bit GPR and
suppresses a `GPR[0]` destination.

## Decision

1. `Opcode::ParallelCopyHalfword` decodes only under primary `0x1C`, funct
   `0x29`, selector `0x1B`, with `rs == 0`. Any other selector, funct or a
   nonzero `rs` stays `Unknown`/unsupported, so the runtime keeps failing
   closed on words the evidence does not pin.
2. The typed IR gains `Operation::ParallelCopyHalfword { dst, src }`. Effect
   masks read only `rt` and write only `rd` (empty for `GPR[0]`), matching the
   other single-source MMI3 permutations (PEXEW, PROT3W, PEXT5, PPAC5).
3. Codegen emits the existing single-source call shape
   `state.gpr[rd] = parallel_copy_halfword(state.gpr[rt]);` behind the usual
   nonzero-destination guard, with the helper imported from
   `reps2::runtime`.
4. `parallel_copy_halfword` forms the complete `Gpr128` before assignment, so
   `rd == rt` observes the original operand. No runtime state, ABI, device or
   report contract changes.

## Consequences

- The Downhill Domination probe executes the memset's PCPYH and continues:
  4,417,806 instructions instead of 4,416,745 (+1,061), 1 batch, callbacks
  78/78, `timer3.cycles=4417806`.
- The frontier moves to a new `unsupported_instruction` stop at
  `pc=0x00244FD4` with guest word `0x72631818`.
- **Derived (R5900 encoding):** `0x72631818` = primary `0x1C`, funct `0x18`,
  `rs == 19`, `rt == 3`, i.e. **`mult1 $19, $3`**, the second MDU pipeline.
  REPS2 already decodes `mfhi1`/`mthi1`/`mflo1`/`mtlo1` but not
  `mult1`/`multu1`/`div1`/`divu1`, so that family is the next instruction
  unit. It is deliberately out of scope here: this slice stays one cohesive
  instruction (decode → IR → lowering → native smoke).
- PCPYH is now represented, so the frontend's unsupported-stop count for this
  image drops from 653 to 652 and the block count rises from 60,898 to
  60,904.
- No compatibility state is upgraded. The probe is a semantic stop, not a
  boot stage, and the title remains Imported/Analyzed/Native-built only.

## Validation

- Decoder test `decodes_r5900_mmi3_pcpyh_with_reserved_rs`: asserts
  `0x7002_1EE9` decodes as `pcpyh` with `rs == 0`, the observed
  `0x7008_1EE9` decodes as `pcpyh $3, $8`, a nonzero `rs` is rejected, and
  the neighbouring MMI3 selector `0x1A` (PEXCH) stays unknown.
- IR test `lifts_r5900_mmi3_pcpyh`: dst/src mapping, `dst == src`, `GPR[0]`
  suppression, and `verify()`.
- Codegen test `emits_mmi3_pcpyh_as_a_single_source_runtime_call`: prelude
  import, destination guard, alias and zero-destination emissions.
- `tests/mmi3_pcpyh_smoke.cpp` (CMake `reps2_mmi3_pcpyh_codegen_smoke`,
  `compile-words` fixture at `0x0000A000`): hand-derived vectors
  `{0x1111'2222'3333'4444, 0xAAAA'BBBB'CCCC'DDDD} ->
  {0x4444'4444'4444'4444, 0xDDDD'DDDD'DDDD'DDDD}`, a second independent
  vector, `rd == rt`, `GPR[0]` suppression, source preservation and an
  untouched bystander register, plus `static_assert`s of the helper.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets --
  -D warnings`, `cargo test --workspace --locked` (351 frontend lib tests plus
  the integration suites) and CMake Release build + `ctest -C Release`
  (42/42) on Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF
  `adfda7b7...f0cf0c`, ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI
  14, frontend rebuilt from this branch, `--discover --tu-count 8
  --native-build --extra-root 0x00246CD0` and both relocated regions,
  `D:\REPS2-DevTests\DownhillDomination\PostADR150\run-report-pcpyh.json`):
  the run stops as `unsupported_instruction` at `pc=0x00244FD4` after
  4,417,806 instructions, 1 batch; mcserv INIT is still served
  (`mc_calls=1 mc_result=0`), callbacks are 78/78. Not a boot stage.