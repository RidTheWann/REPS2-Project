# ADR 0151: Decode, lift and AOT-lower the R5900 MDU1 multiply/divide family

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-16.
- **Scope:** R5900 decoder (`rust/frontend/src/mips.rs`), typed scalar IR
  (`rust/frontend/src/ir.rs`), C++ AOT lowering (`rust/frontend/src/codegen.rs`),
  CMake fixture/smoke (`CMakeLists.txt`, `tests/mdu1_multiply_smoke.cpp`). No
  runtime ABI change: `hi1`/`lo1` already exist in `EeState` and the lowering
  reuses the existing multiply/divide emit shapes.

## Context

ADR 0150 left the real-title frontier on the MDU1 multiply word `0x72631818`.

- **Observed (owner's boot RAM image, `PostADR151/boot.ram.bin`, guest word at
  `0x00244FD4`):** `0x72631818`.
- **Derived (R5900 encoding, `r5900-opcodes` table):** `mult1 31..26=28 rs rt rd
  10..6=0 5..0=0b011000`, so the word is primary `0x1C`, funct `0x18`,
  `rs == 19`, `rt == 3`, `rd == 3`, `sa == 0` — `mult1 $19, $3`. The same table
  pins `multu1` (funct `0x19`), `div1` (`0x1A`, `15..6=0`) and `divu1`
  (`0x1B`, `15..6=0`). `md` in the reference table is the MIPS `md` split; the
  R5900 reads it as `rd`, exactly like the already-supported MULT/MULTU
  (`rs rt rd 10..6=0 5..0=0b011000`).
- **Derived (repo state):** REPS2 already decoded the MDU1 HI1/LO1 movers
  (`mfhi1`/`mthi1`/`mflo1`/`mtlo1`) but no operation could write HI1/LO1, so
  the pipeline was reachable in name only.

## Decision

1. Four new opcodes: `MultiplyDualSigned`, `MultiplyDualUnsigned`,
   `DivideDualSigned`, `DivideDualUnsigned` (`mult1`, `multu1`, `div1`,
   `divu1`).
2. Decoding requires primary `0x1C`, `sa == 0`, and — for the divides — `rd ==
   0`. Anything else stays `Unknown`, so the runtime keeps failing closed on
   encodings the reference table does not pin.
3. Effects: `mult1`/`multu1` read `rs|rt`, write `rd`, and set `writes_hi1` /
   `writes_lo1`; `div1`/`divu1` read `rs|rt` and set only `writes_hi1` /
   `writes_lo1`. The base `hi`/`lo` pair is never touched.
4. Typed IR gains `MultiplyDualWordSigned`, `MultiplyDualWordUnsigned`,
   `DivideDualWordSigned`, `DivideDualWordUnsigned`. Signedness, product width
   and the divide edge cases mirror the existing MULT/MULTU and DIV/DIVU
   operations exactly.
5. Codegen reuses `emit_multiply_word` / `emit_divide_word` with a `dual` flag
   that selects the `state.lo1` / `state.hi1` accumulator pair. The four
   existing call sites pass `dual = false`, so the R5900 base-pipeline output is
   byte-identical to before this change.
6. `rd` follows the existing R5900 MULT convention: on a nonzero destination the
   low 64 bits of the product are written to `GPR[rd]`; `GPR[0]` stays
   suppressed.

## Consequences

- The Downhill Domination probe no longer stops on an unsupported instruction:
  it executes 79,999,923 instructions over 16 batches and ends
  `budget_limit_reached` at `pc=0x002012B0`.
- **Observed (run report, `PostADR151/run-report-mdu1.json`):** the final
  instruction-trace window contains only six PCs, `0x002012B0..0x002012C4`, a
  tight delay loop (`bne $2, $3, -6` against `$3 = -1` with `addiu $2, $2, -1`
  in the delay slot; `$2 = 9,767` at the stop). The surrounding code re-binds
  SIF RPC sid `0x00123456` (`lui $5, 0x12` / `ori $5, $5, 0x3456` at
  `0x00201258`/`0x00201268`), delays 10,000 iterations, then retries while
  `lw $2, 0x24($20)` (`$20 = 0x004C8940`, so the word at `0x004C8964`) is zero.
- **Observed:** `rpc_bind_sid=0x00123456` and `rpc_bind_had=0` — REPS2's server
  table has no entry for that sid, so the bind is answered with `had = 0`. The
  mcserv/pad/Cdvd paths are unaffected (`mc_calls=1 mc_result=0`, callbacks
  78/78). **Unknown:** what server `0x00123456` denotes and whether it lives in
  a title-specific IOP module; establishing that is the next unit, not this one.
- Frontend counts for the same image: 60,935 basic blocks (was 60,904), 317,135
  guest instructions (was 316,880), 632 unsupported stops (was 652), 625
  unresolved indirect targets (was 623).
- No compatibility state is upgraded. A budget exhaustion is not a boot stage;
  the title remains Imported/Analyzed/Native-built only.

## Validation

- Decoder test `decodes_r5900_mdu1_multiply_and_divide`: the observed
  `0x7263_1818` decodes as `mult1` with `rs=19 rt=3 rd=3`, `multu1`/`div1`/
  `divu1` decode with their operands, nonzero `rd` on the divides and nonzero
  `sa` on every form stay `Unknown`.
- Effects test `effects_model_r5900_mdu1_multiply_and_divide`: read/write masks,
  `writes_hi1`/`writes_lo1` set, base `writes_hi`/`writes_lo` clear, `rd == 0`
  writes nothing.
- IR test `lifts_r5900_mdu1_multiply_and_divide`: operand mapping for all four
  operations plus `verify()`.
- Codegen test `emits_r5900_mdu1_multiply_and_divide_against_hilo1`: `lo1`/
  `hi1` assignments, the `rd` write, the unsigned product shape, and no
  `state.lo`/`state.hi` writes from dual-pipeline operations.
- `tests/mdu1_multiply_smoke.cpp` (CMake `reps2_mdu1_multiply_codegen_smoke`,
  `compile-words` fixture at `0x0000B000`, nine words): `mult1 $19, $3` with
  `rd == rt`, `multu1` 64-bit product (`HI1 = 1`, `LO1 = 0`), `div1` `-7 / 2`
  observed through `mflo1`/`mfhi1` (`-3`, `-1`), `divu1` `7 / 2` (`3`, `1`),
  `rd == 0` suppression, and an untouched bystander register.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` (355 frontend lib tests plus the
  integration suites) and CMake Release build + `ctest -C Release` (43/43) on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF
  `adfda7b7...f0cf0c`, ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI
  14 unchanged, frontend rebuilt from this branch, `--discover --tu-count 8
  --native-build --extra-root 0x00246CD0` and both relocated regions,
  `D:\REPS2-DevTests\DownhillDomination\PostADR151\run-report-mdu1.json`):
  `budget_limit_reached`, 16 batches, 79,999,923 instructions,
  `timer3.cycles=79999923`, callbacks 78/78, mcserv INIT still served. Not a
  boot stage.