# ADR 0213: word-shift operand scope (SRL/SLL/SRA read low 32 bits only)

- **Status:** Accepted
- **Date:** 2026-11-25
- **Scope:** `rust/frontend/src/codegen.rs` (`emit_shift`,
  `emit_variable_word_shift`).
- **Related:** ADR 0212 (CFC2/CTC2 `.I`), ADR 0002 (initial word-value
  guard, refined here for word-shift sources), RESEARCH.md abi65-01 probe.

## Context

ABI-43 probe `abi65-01` (main `56adce1` + ADR 0212, runtime ABI 41) advanced
the frontier from `pc=0x0021B7E8` to `pc=0x0021B9B8`, stopping
`undefined_word_operand` on word `0x00087482` (`srl $14, $8, 18`:
`rs=0`, `rt=8`, `rd=14`, `sa=18`, `funct=0x02`) after 5,178,388 instructions.

The generated code for a fixed word shift is (codegen.rs, pre-ADR):

```cpp
if (!is_word_value(state.gpr[8].low)) return Status::undefined_word_operand;
if (14 != 0) state.gpr[14].low = sign_extend_word(
    static_cast<std::uint32_t>(state.gpr[8].low) >> 18U);
```

The guard fired because `gpr[8]` held a full 64-bit value whose high 32 bits
are not the sign extension of its low 32 bits. An audit of every emitter that
writes `state.gpr[].low` (all 36 sites in codegen.rs) found that all *word*
operations already write `sign_extend_word(...)`; the only writers of full
64-bit values are the *doubleword* instructions (DADD/DSUB/DADDU/DSUBU/DADDI/
DADDIU, DSLL/DSRL/DSRA and variable forms, 64-bit AND/OR/XOR/NOR, MFHI/MFLO/
MFHI1/MFLO1, MFSA, MOVN/MOVZ, LWL/LWR/LDL/LDR merges, QMFC2), which are
*supposed* to produce 64-bit values per the EE ISA. So the non-word value in
`gpr[8]` is legal guest state, produced by a legal doubleword instruction —
not a producer bug.

## Decision

1. **Word shifts (SLL/SRL/SRA, fixed and variable) consume only the low
   32 bits of the source operand.** Per MIPS64 Vol. II, SLL/SRL/SRA operate
   on the 32-bit word in GPR[63:0] and sign-extend the 32-bit result into
   GPR[63:0]; the upper bits of the source operand are architecturally
   irrelevant. Therefore the `is_word_value` guard on the *source* of a word
   shift is over-strict and is removed.
2. The generated code becomes:
   `state.gpr[d].low = sign_extend_word(static_cast<std::uint32_t>(
   state.gpr[s].low) >> amt)` — the result is still always a word value, so
   downstream word operations that *do* require `is_word_value` operands
   remain protected.
3. The `is_word_value` guards on genuinely word-typed consumers (word
   arithmetic, word variable shifts' amount register is also only low 32
   bits — same rule) are unchanged elsewhere; only the shift *source* guard
   is removed. Variable-shift amount already masks `& 0x1FU` from the low
   word, so its guard is removed by the same rule.
4. No runtime state change, no ABI bump, no decoder change.

## Consequences

- The `pc=0x0021B9B8` stop clears on the next probe; the frontier advances to
  whatever follows. No boot stage is claimed.
- Fail-closed is preserved: the shift result is still a sign-extended word;
  nothing is approximated. Removing the source guard does not weaken any
  check on shift *results*.
- Other word operations keep their `is_word_value` operand guards, because
  for those the full 64-bit operand genuinely matters (they are modelled as
  word operations and a non-word operand there still indicates a model gap).

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets --
  -D warnings`, `cargo test --workspace --locked` (410 lib tests, including
  `emits_word_shifts_from_non_word_operands_without_guard`).
- CMake Release build + `ctest -C Release`: 52/52 pass on Windows x64/MSVC.
- Fresh real-title probe (`abi65` rerun on main `d20afd9`, runtime ABI 41,
  Downhill Domination USA ISO `0fe2b777...a7eb86`, boot ELF
  `adfda7b7...f0cf0c`): the `0x0021B9B8` stop is gone; the run executes
  5,179,512 instructions in 2 batches and stops at `pc=0x00217D8C` as
  `unsupported_instruction` on word `0x41010003`. **Correction (ADR 0214):**
  this word is COP0 **BC0T** (primary `0x10`, `rs=0x08`, `rt=1`), not `TLBP`
  as first written here; BC0T branches on the COP0 condition bit, which is
  not modeled. Census unchanged: 63,937 blocks, 347,734 guest instructions,
  77 unsupported stops. Evidence recorded in `docs/RESEARCH.md` and
  `docs/PROJECT_STATE.md`. No boot-tested or playable claim.
