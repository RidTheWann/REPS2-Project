# ADR 0174: Serve the 989snd loader bank-load RPC with the documented failure word (runtime ABI 23)

- **Status:** Accepted (validated locally; see Validation).
- **Date:** 2026-11-21.
- **Scope:** `runtime/include/reps2/runtime/ee_hle.hpp`
  (`serve_ee_snd_loader_call`, constants `kEeSndCmdSlLoadBank`/`kEeSndCmdSlLoadBankByLoc`),
  `runtime/include/reps2/runtime/ee_state.hpp` (`snd_loader_*` observation fields),
  `engine/src/main.cpp` (run-report `snd_loader_*` fields),
  `tests/hle_syscall_smoke.cpp` (loader refusal + served bank-load cases),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 22 -> 23).
- **Related:** ADR 0152 (server registration), ADR 0155 (SL_INIT / RPC_END),
  ADR 0173 (scheduler, ABI 22), ADR 0136 (end-function callbacks).

## Context

The Downhill Domination ABI-22 frontier (probe `abi22-01`, reps2 commit
`0f75405`, ISO `0fe2b777…a7eb86`, boot ELF `adfda7b7…f0cf0c`) stops as
`budget_limit_reached` at `pc=0x00201888` after 79,999,980 instructions with
6,854,902 `FlushCache` calls from one hot site `0x0025A964`. The ABI-22 probe
also showed the last SIF RPC call was `rpc_call_server=0x00123457`
(`rpc_call_number=3`, `rpc_call_replied=0`) — the 989snd **loader** server —
and thread 2 (`func=0x0025B0D0`) still READY, never dispatched.

Static disassembly of the frontier region (boot RAM image, file offset ==
guest VA) establishes the loop at `0x00201888`:

```
0x00201888: jal 0x0025A960      ; FlushCache wrapper (syscall 0x64)
0x0020188C: move $a0, $zero     ; delay slot
0x00201890: lw   $v0, -0x2C00($s2)   ; $s2=0x004D0000 -> word at 0x004CD400
0x0020189C: beq  $v0, $s0, 0x00201888 ; $s0=0xFFFFFFFF
```

The frontier function `0x00201718` arms `0x004CD400 = 0xFFFFFFFF` at
`0x002017BC`, then calls `sceSifCallRpc` (`0x0025CF88`) at `0x00201830` with
`$a0 = 0x004CD3C0` (the loader client `gSLClientLoaderData`), `$a1 = 3`,
`$a2 = 1`, recv buffer `0x004CD400` (4 bytes), `rmode`-style NOWAIT. This is
the public 989snd driver's `snd_BankLoadByLoc` busy-wait:

```c
SifCallRpc(&gSLClientLoaderData, SL_LOADBANKBYLOC, SIF_RPC_M_NOWAIT, ...);
while (gLoadReturnValue[0] == -1) { FlushCache(WRITEBACK_DCACHE); }
```

(`SL_LOADBANK = 2`, `SL_LOADBANKBYLOC = 3` in the 989snd `SL_*` enum.)

**Observed:** REPS2's loader handler (`serve_ee_snd_loader_call`) returned
`false` for every call, so no reply was written to `0x004CD400` and no
`SIF_CMD_RPC_END` was delivered; the word stayed `0xFFFFFFFF` forever and the
boot thread spun on the instruction budget. The scheduler (ADR 0173) is not
involved: this is a NOWAIT busy-wait, not a semaphore block, so the boot
thread never voluntarily blocks and thread 2 is never dispatched.

## Decision

Serve the two loader commands the recorded path issues — `SL_LOADBANK` (2)
and `SL_LOADBANKBYLOC` (3) — by writing the **documented load-failure result
word 0** into the first word of the caller's reply buffer, then letting the
normal served-call path deliver `SIF_CMD_RPC_END` (which also releases the
client's semaphore and queues any `end_function` callback). The 989snd IOP
handler writes `snd_BankLoadEx`'s return value into the reply buffer; that
function returns `0` (and sets `gLastLoadError = 256`) when the data source
cannot be opened. REPS2 has no IOP filesystem and no SPU memory, so 0 is the
honest modelled result: the EE client treats `gLoadReturnValue[0] == 0` as
"load failed", not as "still pending", so the busy-wait exits and the title
continues on its own failed-bank-load path. No bank pointer, sound data or
success status is fabricated.

Every other loader function number stays recorded (`snd_loader_calls`,
`snd_loader_number`) and refused — no reply, no RPC_END, no semaphore signal —
so any caller waiting on an unmodelled loader command stays blocked. A
malformed bank-load request (no reply buffer or `recv_size < 4`) is likewise
refused with no mutation.

## Consequences

- `EeSifCmdState` gains four `snd_loader_*` fields; `sizeof(EeState)` changes,
  so `REPS2_RUNTIME_ABI_VERSION` moves 22 -> 23 and every generated module must
  be rebuilt. ABI-22 modules are refused.
- The run report gains `snd_loader_calls`/`snd_loader_replies`/
  `snd_loader_number`/`snd_loader_replied`.
- This completes the synchronous RPC the frontier loop waits on. It does not
  claim any sound output, any successful bank load, or any boot stage; the
  title's own error path for a failed bank load runs next, and the next
  frontier (if any) is whatever that path observes.

## Validation

- `tests/hle_syscall_smoke.cpp`: the existing loader-refusal case now also
  asserts the new `snd_loader_*` counters, and a new case
  (`test_989snd_loader_bank_load_is_served_with_failure_word`) drives fn 2 and
  fn 3 through `process_sif_command` and asserts: reply word 0 written over
  the armed `0xFFFFFFFF`, `SIF_CMD_RPC_END` present in the EE packet buffer,
  the client semaphore released, and the counters incremented.
- `cmake --build build --config Release --parallel` (MSVC, Windows x64) and
  `ctest -C Release`: 49/49 pass.
- Rust gates from `rust/`: `cargo fmt --all -- --check`,
  `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` all pass.

## Real-title result

ABI-23 probe `D:\REPS2-DevTests\DownhillDomination\abi23-01` (this branch's
headers, rebuilt frontend + native module, same ISO/ELF digests, same extra
root `0x00246CD0`, both relocated regions, 5,000,000-instruction budget,
16 batches). See `docs/RESEARCH.md` / `docs/PROJECT_STATE.md` for the recorded
outcome; no boot stage or Playable claim is made by this ADR.
