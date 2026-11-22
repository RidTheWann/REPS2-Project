# ADR 0154: Decode, lift and AOT-lower the COP2 VU0 macro quadword transfers (`qmfc2`/`qmtc2`)

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-17.
- **Scope:** R5900 decoder (`rust/frontend/src/mips.rs`), typed scalar IR
  (`rust/frontend/src/ir.rs`), C++ AOT lowering (`rust/frontend/src/codegen.rs`),
  CMake fixture/smoke (`CMakeLists.txt`, `tests/cop2_codegen_smoke.cpp`). No
  runtime ABI change: the VU0 register file and the `vu0_read_vf` /
  `vu0_write_vf` accessors already exist, and the lowering reuses them exactly
  as the existing `LQC2`/`SQC2` path does.

## Context

ADR 0153 left the reachable code translated but stopping on unsupported
instructions. A reachability census of the generated probe (every emitted
`return Status::unsupported_instruction;` site mapped back to its guest word
through `boot.ram.bin`, file offset == guest VA, with the two relocated regions
`0x80076000<-0x002829F8:0x740` and `0x80075000<-0x002831A0:0x330`) produced the
ranked S4 work list:

- **Observed (`PostADR153/boot*.cpp`, 632 stop sites, 86 unique words):**
  primary `0x12` COP2 596 sites, `0x1C` MMI 28, `0x10` COP0 8.
- **Derived (COP2 `fmt` = bits 25..21):** `fmt5` (QMTC2) 176, `fmt1` (QMFC2) 8,
  `fmt2` (CFC2) 6, `fmt16`/`fmt17`/`fmt20`/`fmt24`/`fmt30`/`fmt31` (Special1)
  406. The Special1 rows are the VU floating-point arithmetic
  (VSUB 205, VADD 46, VMUL/VMINI/VADDw/VMULw ~16, VCALLMS 13, Special2 124).
- **Observed (`PostADR153/boot.ram.bin`):** the `fmt5` sites are genuine VU0
  macro code, e.g. around `0x0010B680`: `LQC2 $vf11, 0($s5)` /
  `LQC2 $vf12, 0($s4)` / `0x4BCC5AEC` / `SQC2 $vf11, 0($s5)`; around
  `0x0010EECC` a repeated inlined helper: `LQC2 $vf11, 0($t2)` / COP1 `cfc1` /
  `QMTC2 $2, $vf12` (`0x48A26000`) / `VSUB…` / `SQC2 $vf11, 0xC0($sp)`.
- **Derived (PS2Tek EE instruction decoding, COP2 table; Ghidra
  `ghidra-emotionengine-reloaded/data/languages/cop2.sinc`):** COP2 primary
  `0x12`, `fmt` = bits 25..21. `fmt` 1 = QMFC2, 2 = CFC2, 5 = QMTC2, 6 = CTC2;
  `fmt >= 0x10` is Special1 (Special2 when the function row is 7 with column
  >= 4). The four transfer formats reserve bits 10..0, and the VF index is at
  bits 15..11 (`rd`) with the GPR index at bits 20..16 (`rt`).

`QMTC2`/`QMFC2` move a whole 128-bit quadword between a GPR pair and a VF
register. They are exact data movement: no VU floating-point numeric model
(the VU arithmetic differs from the EE FPU) is required, so this family is the
lowest-risk, highest-count S4 slice.

## Decision

1. Two new opcodes: `Qmtc2` (`qmtc2`) and `Qmfc2` (`qmfc2`).
2. The decoder gains a `decode_cop2` arm for primary `0x12`. It accepts only
   `fmt == 5` (QMTC2) and `fmt == 1` (QMFC2), and both require bits 10..0 to be
   zero. Every other COP2 format — CFC2/CTC2, BC2, Special1, Special2 — stays
   `Unknown`, so the runtime keeps failing closed on encodings without a model.
3. Effects: `qmtc2` reads `GPR[rt]` and writes `VF[rd]` (VF0 writes ignored,
   `vf_write_mask` excludes bit 0); `qmfc2` reads `VF[rd]` (VF0 reads as the
   hardwired constant, bit 0 included) and writes `GPR[rt]` (`GPR[0]` writes
   discarded). Neither touches memory and neither raises a data-address
   exception.
4. Typed IR gains `MoveToVu0Quadword { dst: Vf, src: Gpr }` and
   `MoveFromVu0Quadword { dst: Gpr, src: Vf }`.
5. Codegen lowers them through the existing `vu0_write_vf` / `vu0_read_vf`
   accessors: `vu0_write_vf(state.vu0, rd, state.gpr[rt])`, and
   `if (rt != 0) state.gpr[rt] = vu0_read_vf(state.vu0, rd);`. The VF0 write
   suppression and the GPR0 write suppression are therefore the same
   guarantees `LQC2`/`SQC2` already use.

## Consequences

- The 182 `QMTC2`/`QMFC2` stop sites in the reachable probe are translated:
  `fmt5` stops 176 -> 0, and 6 of the 8 `fmt1` sites -> 0. The 2 remaining
  `fmt1` sites are the **interlocked** word `0x48220001`
  (QMFC2.I, format 1 with interlock bit 0 set), which was previously misclassified
  as malformed but is a valid QMFC2.I (interlock variant). After ADR 0190
  it decodes as `Qmfc2` with interlock flag set.
- The raw stop count barely moves because the newly-translated code continues
  into the next unsupported family. Frontend counts for the same image,
  `PostADR153` -> `PostADR154`: basic blocks 60,935 -> 60,958 (+23), guest
  instructions 317,135 -> 317,514 (+379), unsupported stops 632 -> 631,
  unresolved indirect targets 625 -> 625, guest trap stops 258 -> 258. The
  census confirms the churn: Special1 `fmt30` stops 372 -> 538 (+166), `fmt16`
  15 -> 20, `fmt17` 3 -> 4, `fmt24` 3 -> 5, `fmt31` 12 -> 18, MMI 28 -> 29.
  Resolving the 182 transfer sites revealed 181 new stops in the VU
  arithmetic that immediately follows.
- This is a capability/coverage advance, not a frontier advance. The probe still
  ends `budget_limit_reached` at `pc=0x00201FC0` (the 989snd poll loop) and
  never reaches the VU0 code, so the observable title stage is unchanged.
- The deferred families each need their own slice: the VU0 vector arithmetic
  (`vsub`/`vadd`/`vmul`/`vmini`/`vaddw`/`vmulw`, the `fmt30`/`fmt16`/`fmt17`/
  `fmt24`/`fmt31` rows) needs a VU numeric model; `Special2` (the VU integer
  rows), `VCALLMS`, CFC2/CTC2 and BC2 need their own models. The remaining
  `MMI` (29) and `COP0` (8) stops are separate.
- No compatibility state is upgraded. A budget exhaustion is not a boot stage;
  the title remains Imported/Analyzed/Native-built only.

## Validation

- Decoder test `decodes_cop2_quadword_register_transfers_and_reserved_fields`:
  `0x48A26000` decodes as `Qmtc2` with `rt=2 rd=12`, `0x48236000` as `Qmfc2`
  with `rt=3 rd=12`; the interlocked `0x48220001` (QMFC2.I, bit 0 = interlock,
  decoded as `Qmfc2` after ADR 0190), the genuinely malformed `0x4822_0002`
  (reserved bit 1 set), and the unimplemented formats CFC2 `0x48420002`, CTC2 `0x48C20006`, Special1
  `0x4BCC5AEC` all stay `Unknown`.
- Effects test `effects_track_cop2_vf_dependencies`: QMTC2 reads `GPR2`, writes
  `VF12`, no memory and no exception; QMTC2 to VF0 writes nothing; QMFC2 reads
  `VF12`, writes `GPR3`, no memory and no exception; QMFC2 to GPR0 writes
  nothing while still reading VF0 (bit 0).
- IR test `lifts_load_store_family_with_signed_offsets`: `MoveToVu0Quadword {
  dst=vf12, src=gpr2 }` and `MoveFromVu0Quadword { dst=gpr3, src=vf12 }` plus
  `verify()`.
- `tests/cop2_codegen_smoke.cpp` (CMake `reps2_cop2_codegen_smoke`,
  `compile-words` fixture at `0x0000E000`, extended from 6 to 15 words):
  `QMTC2 $2, $vf12` then `SQC2 $vf12, 0x60($a0)` proves the full GPR->VF
  quadword reaches memory (all 16 bytes, little-endian lanes);
  `QMFC2 $3, $vf12` proves VF->GPR; `QMFC2 $2, $vf0` proves the VF0 read reads
  the hardwired constant; `QMFC2 $0, $vf12` proves the GPR0 write is dropped
  (GPR0 stays zero); `QMTC2 $0, $vf12` followed by `SQC2 $vf0, 0x70($a0)`
  proves the VF0 write is dropped (VF0 still holds the constant), and
  `QMTC2 $2, $vf4` + `SQC2 $vf4, 0x80($a0)` proves the VF0 read path through a
  GPR round-trip. The scratch quadwords are pre-filled with `0x5A` so the
  mostly-zero VF0 constant store is an observable write.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` (356 frontend lib tests plus the
  integration suites) and CMake Release build + `ctest -C Release` (43/43) on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF
  `adfda7b7...f0cf0c`, ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI 16
  unchanged, frontend rebuilt from this branch, `--discover --tu-count 8
  --native-build --extra-root 0x00246CD0` and both relocated regions,
  `D:\REPS2-DevTests\DownhillDomination\PostADR154\run-report-qmfc2qmtc2.json`):
  `budget_limit_reached`, 16 batches, 79,999,984 instructions,
  `timer3.cycles=79999984`, callbacks 78/78, SIF/`mcserv` paths unchanged
  (`syscalls_seen=1841052`). Not a boot stage.
