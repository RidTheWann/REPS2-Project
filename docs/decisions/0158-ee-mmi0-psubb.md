# ADR 0158: Decode, lift and AOT-lower R5900 MMI0 PSUBB

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-17.
- **Scope:** R5900 MMI0 decoder (`rust/frontend/src/mips.rs`), typed scalar IR
  (`rust/frontend/src/ir.rs`), C++ AOT lowering (`rust/frontend/src/codegen.rs`),
  128-bit GPR helper (`runtime/include/reps2/runtime/ee_state.hpp`), CMake
  fixture/smoke (`CMakeLists.txt`, `tests/mmi0_word_smoke.cpp`). No runtime ABI
  change: the helper is a `constexpr` function over `Gpr128`, and no engine
  state or report field was added.

## Context

The previous real-title frontier (ADR 0156/0157) was an
`unsupported_instruction` stop at `pc=0x0025432C` after 4,577,576 instructions.
The word was recorded as `0x70471A48` (`rs == 2`, `rt == 7`, `rd == 3`).

- **Observed (owner's boot RAM image, `PostADR158_psubb/boot.ram.bin`, guest word
  at `0x0025432C`):** `0x70471A48`.
- **Derived (R5900 encoding):** `0x70471A48` = primary opcode `0x1C` (MMI),
  funct `0x08` (MMI0), five-bit selector in bits 10..6 `0x09`, i.e. the MMI0
  **PSUBB** encoding — `psubb $3, $2, $7`. The public ps2tek MMI0 table lists
  `0x08 PADDB / 0x09 PSUBB / 0x0A PCGTB`, and REPS2 already implements the
  word/halfword siblings `0x00 PADDW / 0x01 PSUBW / 0x04 PADDH / 0x05 PSUBH`.
- **Derived (reachable-image census, `boot.ram.bin`):** 13 static PSUBB words in
  the loaded image, all in the `0x0025432C..0x00259048` range; `PADDB`
  (selector `0x08`) does not occur at all. Eight of the 13 sit on reachable
  translation paths (the census below), the rest are unreached.

PSUBB subtracts the sixteen corresponding 8-bit lanes of `rs` and `rt` modulo
2^8 with no borrow across byte boundaries. It is the **plain** (non-saturating)
MMI0 form; the saturating byte subtract is the distinct selector `0x19`
PSUBSB, which this slice leaves `Unknown` (as does `0x18` PADDSB), so the
decoder keeps failing closed on the words the evidence does not pin. The
existing `ParallelAddUnsignedByte` (`paddub`, funct `0x28`, selector `0x18`) is
a different, unsigned-saturating operation and is not reused.

## Decision

1. `Opcode::ParallelSubByte` decodes only under primary `0x1C`, funct `0x08`,
   selector `0x09`. Any other selector, funct, `0x18` PADDSB or `0x19` PSUBSB
   stays `Unknown`/unsupported.
2. The typed IR gains `Operation::ParallelSubByte { dst, lhs, rhs }`. Effect
   masks read `rs`/`rt` and write `rd` (empty for `GPR[0]`), matching the other
   three-source MMI0 lane arithmetic (`ParallelAddWord`/`ParallelSubWord`/
   `ParallelSubHalfword`).
3. Codegen emits the existing three-source call shape
   `state.gpr[rd] = parallel_sub_byte(state.gpr[rs], state.gpr[rt]);` behind the
   usual nonzero-destination guard, with the helper imported from
   `reps2::runtime`.
4. `parallel_sub_byte` forms the complete `Gpr128` before assignment, so
   `rd == rs` and `rd == rt` observe the original operands. No runtime state,
   ABI, device or report contract changes.

## Consequences

- The Downhill Domination probe passes the former stop:
  `PostADR158_psubb/boot.ram.bin` at `0x0025432C` now AOT-lowers to
  `if (3 != 0) state.gpr[3] = parallel_sub_byte(state.gpr[2], state.gpr[7]);`
  (observed in the generated `boot.part0008.cpp`), so the run no longer stops
  there.
- The frontier moves to a **budget-exhausting countdown retry loop** at
  `pc=0x0025D820` (weaker than the previous semantic stop), described below,
  and not to a new unsupported instruction.
- The frontend census for this image (same discovery, `--tu-count 8`,
  `--extra-root 0x00246CD0`, both relocated regions) moves from
  60,958 blocks / 317,514 instructions / 631 unsupported stops to
  60,984 blocks / 317,666 instructions / 623 unsupported stops: the eight
  reachable PSUBB words resolve and their blocks expand, while guest-trap stops
  (258) and unresolved indirect targets (625) are unchanged.
- **Correction (2026-11-18):** those totals are pre-#39. With the PCEQW/PCGTW
  decoding added in `e3b7447` the same image records 317,701 instructions and
  622 unsupported stops; block/root/exception/unresolved counts are unchanged.
  The observation in `docs/RESEARCH.md` carries the reproduction.
- No compatibility state is upgraded. The probe is a budget exhaustion, not a
  boot stage, and the title remains Imported/Analyzed/Native-built only.

## Validation

- Decoder test `decodes_r5900_mmi0_parallel_word_halfword_and_byte_add_and_subtract`:
  extends the MMI0 table with `(0x09, ParallelSubByte, "psubb")`, asserts the
  alias mapping and effect masks, and keeps the neighbouring unimplemented
  selector `0x0A` (PCGTB) plus `0x02` (PCGTW) `Unknown`.
- IR test `lifts_r5900_mmi0_parallel_word_halfword_and_byte_add_and_subtract`:
  dst/lhs/rhs mapping and `verify()`.
- Codegen test `emits_mmi0_parallel_byte_subtract_runtime_call`: prelude
  import, destination guard, alias and zero-destination emissions.
- `tests/mmi0_word_smoke.cpp` (CMake `reps2_mmi0_word_codegen_smoke`,
  `compile-words` fixture at `0x00007000`, now 15 fixture words): hand-derived
  `parallel_sub_byte` vectors (including the cross-lane-borrow boundaries
  `0x7F-0xFF = 0x80` and `0x00-0x01 = 0xFF`), `static_assert`s, `rd == rs` /
  `rd == rt` / `rd == rs == rt` alias coverage and non-mutation checks.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets --
  -D warnings`, `cargo test --workspace --locked` (357 frontend lib tests plus
  the integration suites) and CMake Release build + `ctest -C Release`
  (43/43) on Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF
  `adfda7b7...f0cf0c`, ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI
  18, frontend and native module rebuilt from this branch,
  `--discover --tu-count 8 --probe-budget 5000000 --hle-syscalls
  --extra-root 0x00246CD0` and both relocated regions,
  `D:\REPS2-DevTests\DownhillDomination\PostADR158_psubb\run-report-psubb.json`):
  the run ends `budget_limit_reached` after 79,999,931 instructions over 16
  batches at `pc=0x0025D820` (`syscalls_seen=1237 handled=1220`, callbacks
  78/78, `dmac.interrupts_delivered=0`); `mcserv` INIT, the 300-byte SearchFile
  (`searchfile_found=1 lsn=0x000AC0CA size=0x06096AD0
  name="\SKAT\DHSKAT.SKX;1" replied=1`) and both `EndOfHeap` calls
  (`heap.end_of_heap_calls=2 last_ok=1 current_end=0x008AF000`) are still
  served. A single batch at the same budget (`trace-5000000.json`) stops at
  `4,999,996` instructions with `$v0=0x000F14A0`, inside the countdown.

### Frontier characterization

- **Observed (16-thread trace tail, `run-report-psubb-trace.json`):** the last
  PCs are exactly `0x0025D820, 0x0025D824, 0x0025D828, 0x0025D82C, 0x0025D830,
  0x0025D834` repeated. With a single batch of 5,000,000 the same loop is the
  only thing executed after ~4.58M instructions, at all budgets 4,620,000…
  12,000,000, with `$v1 = 0xFFFFFFFF` and `$v0` always inside `[0, 0x100000]`.
- **Derived (guest words `0x0025D800..0x0025D860`):** a status-poll **retry**
  loop: `beq $s0,$zero,0x0025D83C`; `addiu $s0,$s3,0x4C80` (so `$s0` is
  `0x00764C80`); `jal 0x0025CDB8`/`bltz $v0,0x0025D974`; `lw $v0,0x24($s0)`
  (polls the word at `0x00764CA4`); `beq $v0,$zero,0x0025D818` with delay slot
  `lui $v0,0x0010`. The branch target `0x0025D818` sets `$v1 = -1` and enters a
  countdown `addiu $v0,$v0,-1` (four `nop`) `bne $v0,$v1,0x0025D820`
  (`0x0025D820..0x0025D834`) — a ~2^20-iteration `nopdelay`, the same shape as
  the ADR 0137 loop. The enclosing routine starts at `0x0025D790` and is called
  from `0x0025DACC`; only one branch in the image targets `0x0025D818`
  (`0x0025D860`).
- **Observed (`session.sif`):** the 989snd main server's second command, fno
  `77` (`SL_COMMAND_BATCH`), is recorded and refused at the same phase
  (`snd_calls=2 snd_replies=1 snd_number=77 snd_send=16 snd_replied=0`), the
  same class as ADR 0152's refused-command wait, while `SL_INIT` (fno 0) stays
  served.
- **Inferred (not established):** that the countdown loop is the EE side
  waiting for that refused `SL_COMMAND_BATCH` reply. Settling it needs the batch
  served; the honest next unit is therefore the 989snd `SL_COMMAND_BATCH`
  handler (or another unit that makes the poll word `0x00764CA4` non-zero).
  This probe does **not** prove the two are the same wait.
