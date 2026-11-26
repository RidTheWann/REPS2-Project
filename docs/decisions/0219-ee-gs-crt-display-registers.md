# ADR 0219: Model GS CRT display registers DISPFB2/DISPLAY2/BGCOLOR (runtime ABI 43)

- **Status:** Accepted (validated locally and by real-title probe `frontier-abi43-01` on merged `main` `e00ab68`, runtime ABI 43, Windows x64 / MSVC 19.51.36260.0)
- **Date:** 2026-11-26
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (new `kEeGsDispfb2Address`/`kEeGsDisplay2Address`/`kEeGsBgcolorAddress` constants,
  `EeMemory` gains `gs_dispfb2`/`gs_display2`/`gs_bgcolor`,
  `mmio_read/write_gs_dispfb2`/`mmio_read/write_gs_display2`/`mmio_read/write_gs_bgcolor`,
  MMIO dispatch entries, whole-register 64-bit SD/LD handling in `store_u64`/`load_u64`),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 42 -> 43),
  `tests/gs_csr_smoke.cpp` (4 new cases: three 64-bit round-trips and one
  low-word-write-preserves-high test),
  `docs/RESEARCH.md`, `docs/PROJECT_STATE.md`, `docs/COMPATIBILITY_MATRIX.md`.
- **Related:** ADR 0175 (GS CSR/IMR), ADR 0176 (64-bit MMIO decomposition),
  ADR 0178 (PMODE/SMODE2), ADR 0218 (VBLANK model), CLAUDE.md Golden Rules.

## Context

The `frontier-vblank-01` probe (runtime ABI 42, Windows x64/MSVC, Downhill Domination USA ISO `0fe2b777...a7eb86`, boot ELF `adfda7b7...f0cf0c`) advanced past the VBLANK wait loop at `pc=0x0025ABB0` and stopped at `pc=0x0021F05C` after 10,000,298 instructions as `unsupported_memory_access` on a 64-bit store (`SD`) to `0x12000090`.

**Observed register setup in the trace (guest PC 0x0021F000..0x0021F05C; final GPR values from `run-report.json`):**

| GPR | Value | Purpose |
|-----|-------|---------|
| gpr[9] | 0x12000000 | PMODE address |
| gpr[8] | 0x12000020 | SMODE2 address |
| gpr[7] | 0x12000090 | DISPFB2 address |
| gpr[4] | 0x120000A0 | DISPLAY2 address |
| gpr[5] | 0x120000E0 | BGCOLOR address |
| gpr[10] | 0x0075BB00 | Source data base |

**Guest instruction sequence (from generated `block_0x0021F000`, `boot.part0007.cpp`):**

1. `0x0021F044` — `LD $3, 0($10)` loads 64-bit from `0x0075BB00` → `$3`
2. `0x0021F04C` — `SD $3, 0($9)` stores to PMODE (`0x12000000`) — **already modelled (ADR 0178)**
3. `0x0021F050` — `LD $2, 8($10)` loads 64-bit from `0x0075BB08` → `$2`
4. `0x0021F054` — `SD $2, 0($8)` stores to SMODE2 (`0x12000020`) — **already modelled (ADR 0178)**
5. `0x0021F058` — `LD $3, 0x10($10)` loads 64-bit from `0x0075BB10` → `$3`
6. `0x0021F05C` — `SD $3, 0($7)` stores to DISPFB2 (`0x12000090`) — **FAILS HERE**
7. `0x0021F060` — `LD $2, 0x18($10)` loads from `0x0075BB18` → `$2`
8. `0x0021F064` — `SD $2, 0($4)` stores to DISPLAY2 (`0x120000A0`) — next unmodelled store
9. `0x0021F068` — `LD $3, 0x20($10)` loads from `0x0075BB20` → `$3`
10. `0x0021F06C` — `SD $3, 0($5)` stores to BGCOLOR (`0x120000E0`) — next unmodelled store

**Classification of the three target registers (independent verification against the probe trace):**

- **DISPFB2 (0x12000090): OBSERVED.** It is the exact stop address of the recorded run.
- **DISPLAY2 (0x120000A0) and BGCOLOR (0x120000E0): OBSERVED as the immediately following
  instructions in the same straight-line block.** They are reached the moment the DISPFB2
  store succeeds, with their effective addresses held in `$4`/`$5` at the stop. They are the
  next stores after the stop, not speculative additions.
- **SMODE1 (0x12000010): NOT modelled.** No access to `0x12000010` appears in this block or
  anywhere in the recorded trace. Including it now would be speculation, so it stays
  unmodelled and fail-closed exactly as ADR 0178 leaves it.

**Derived from PS2SDK `common/include/gs_privileged.h` and PS2Tek `EE I/O Maps` / `GS Register List`:**

| Register | Address | PS2SDK name | Width |
|----------|---------|-------------|-------|
| PMODE | 0x12000000 | `GS_REG_PMODE` | 64-bit |
| SMODE2 | 0x12000020 | `GS_REG_SMODE2` | 64-bit |
| DISPFB2 | 0x12000090 | `GS_REG_DISPFB2` | 64-bit |
| DISPLAY2 | 0x120000A0 | `GS_REG_DISPLAY2` | 64-bit |
| BGCOLOR | 0x120000E0 | `GS_REG_BGCOLOR` | 64-bit |

The guest accesses all three new registers with 64-bit `SD`/`LD`. A 32-bit store to one of
these addresses (should any appear later) must reach only the low word and preserve the high
word; whole-register writes come through the 64-bit path.

**Inferred (from guest code pattern + register map):** The guest initializes a contiguous
display setup block writing PMODE, SMODE2, DISPFB2, DISPLAY2, BGCOLOR in sequence from a
prepared data structure in main RAM. This is a standard GS display initialization pattern.

**Unknown:** the exact 64-bit value the guest stores to DISPFB2 at the stop (the run report
records only final register state, and `boot.ram.bin` is the pre-run image), the bit layout
of each register, and any guest-visible side effect beyond storage. REPS2 models no GPU,
display, vsync, or GS DMA; bounded storage lets the guest read back exactly what it wrote
without fabricating GPU behavior, consistent with ADR 0175/0178.

## Decision

1. **Add three GS CRT display registers as bounded 64-bit storage** in `EeMemory`:
   - `gs_dispfb2` at `0x12000090` (`GS_REG_DISPFB2`)
   - `gs_display2` at `0x120000A0` (`GS_REG_DISPLAY2`)
   - `gs_bgcolor` at `0x120000E0` (`GS_REG_BGCOLOR`)

   Each is a plain `std::uint64_t`, reset 0. Reads return the stored value; writes store the
   value. No display, vsync, or GPU state is fabricated. SMODE1 (0x12000010) is deliberately
   not added.

2. **Handle the 64-bit registers in `load_u64`/`store_u64` directly**, before the 32-bit
   decomposition block: a 64-bit `LD`/`SD` reads/writes the whole register. The 32-bit MMIO
   handlers (`mmio_read/write_gs_*`) read/write only the low word and preserve the high word,
   so a stray 32-bit store never zeroes the upper half. This differs from the ADR 0176
   decomposition used for the 32-bit GS CSR/IMR/PMODE/SMODE2 registers: those *are* 32-bit
   and a 64-bit access legitimately splits into two 32-bit halves, whereas these are native
   64-bit registers whose upper half must survive.

3. **Add MMIO read/write handlers and `EeMemory::kMmioDevices` entries** for all three
   addresses, following the ADR 0178 pattern, so 32-bit accesses also dispatch rather than
   stop.

4. **Bump `REPS2_RUNTIME_ABI_VERSION` from 42 to 43** because `sizeof(EeState)` grows (three
   new `uint64_t` fields). ABI-42 modules are refused at admission and must be rebuilt.

5. **Tests** (`tests/gs_csr_smoke.cpp`): three 64-bit `store_u64`/`load_u64` round-trips
   (one per register) and one test proving a 32-bit `store_u32` reaches the low word only and
   preserves the high word of a 64-bit register.

6. **No display output, no vsync, no GPU state, no GS DMA, no field parity.** If the guest
   later reads these registers it sees exactly what it wrote — that is the guest-visible
   contract.

## Consequences

- The `frontier-vblank-01` probe should pass the DISPFB2 stop at `pc=0x0021F05C` and advance
  through the DISPLAY2 and BGCOLOR stores.
- The next frontier will be whatever the guest does after this display-register block
  (candidate: the `BEQ $6,$0` at `0x0021F070` then `block_0x0021F078`/`block_0x0021F094`,
  likely further GS/VIF1/GIF setup or a DMA kick).
- No boot-tested or playable claim. REPS2 still models no GPU rendering, GS DMA, or display.

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` (Rust gates pass; no Rust changes required by this slice).
- CMake Release build + `ctest --test-dir build -C Release --output-on-failure` passes
  (including the new `gs_csr_smoke` cases).
- Real-title probe `frontier-abi43-01` verified on merged `main` `e00ab68115e69aec227763c921e764b2c88f1642`
  (runtime ABI 43, Windows x64, MSVC 19.51.36260.0, C++20, Downhill Domination USA
  `SCUS_971.77`, ISO SHA-256 `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`,
  boot ELF SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`,
  `--discover --tu-count 8 --probe-budget 5000000 --max-batches 16 --hle-syscalls`):
  - The DISPFB2 stop at `pc=0x0021F05C` is passed; DISPFB2 (`0x12000090`), DISPLAY2 (`0x120000A0`),
    and BGCOLOR (`0x120000E0`) stores all execute.
  - Outcome: `budget_limit_reached` (`budget_exhausted`), `pc=0x0025ABB0` after `79,999,968`
    instructions over `16` batches; `syscalls_seen=1736`, `syscalls_handled=1719`,
    `callbacks_completed=82`, `vblank={starts:16, ends:16, irq_refusals:0, in_blank:0}`,
    `intc.stat=0xC` (`12`), `intc.handlers=0`.
  - Run-report excerpt: `outcome: "budget_limit_reached"`, `session.pc_hex: "0x0025ABB0"`,
    `session.instructions_executed: 79999968`, `session.run_batches: 16`,
    `session.gprs[3]: "0x000000001000F000"`, `session.gprs[4]: "0x0000000000000004"`.
  - Frontier advance (clears the `0x0021F05C` stop and enters the frame-sync poll loop), not
    a boot stage; budget exhaustion is a weaker evidence class than a semantic stop and
    promotes no compatibility state. (See ADR 0220 for the diagnosis of the `0x0025ABB0`
    intra-batch VBLANK polling stall.)
- Updated `docs/RESEARCH.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, and `docs/COMPATIBILITY_MATRIX.md`.

## References

- Sony EE Core User's Manual / PS2Tek `EE I/O Maps` / `GS Register List`
- PS2SDK `common/include/gs_privileged.h`: `GS_REG_DISPFB2`, `GS_REG_DISPLAY2`, `GS_REG_BGCOLOR`
- ADR 0175: GS CSR/IMR bounded storage, no GPU
- ADR 0176: 64-bit MMIO decomposition for 32-bit registers (contrasted here with native 64-bit registers)
- ADR 0178: PMODE/SMODE2 model and SetGsCrt; SMODE1 and the other 0x12000000-page registers stay unmodelled
- ADR 0218: VBLANK model that unblocked this frontier
- CLAUDE.md Golden Rules: fail closed, no fabrication, evidence before assertion