# ADR 0221: Record mcserv request head for unmodelled functions

- **Status:** Proposed (diagnostic-only; no HLE behaviour change)
- **Date:** 2026-11-27
- **Scope:**
  - `runtime/include/reps2/runtime/ee_state.hpp` (`EeSifCmdState` gains `last_mc_request_head[4]`, `mc_refused_calls`)
  - `runtime/include/reps2/runtime/ee_hle.hpp` (`serve_ee_mcserv_call` records first four request words for refused calls)
  - `engine/src/main.cpp` (run report gains `mc_refused_calls`, `mc_request_head`)
  - `runtime/include/reps2/runtime/abi_version.hpp` (`REPS2_RUNTIME_ABI_VERSION` 44 -> 45)
  - `tests/hle_syscall_smoke.cpp` (`test_mcserv_refusal_records_request_head`)
  - `tests/rg_reps2_engine_smoke.cpp` (ABI-44 module refused as `runtime_abi_mismatch`)
- **Related:** ADR 0149 (mcserv INIT server), ADR 0156 (SearchFile head recording — same convention), ADR 0164/0169 (ABI-bump discipline).

## Context

Downhill Domination probe (`abi44-*`, ABI 44) ends `budget_limit_reached` at `pc=0x0025ABB0` (VBLANK poll, legitimate per-frame wait, ADR 0220). The run report's SIF block shows:

```
rpc_call_server=0x80000400 rpc_call_number=1 rpc_call_replied=0
mc_calls=2 mc_number=1
```

`0x80000400` is the memory-card SIF RPC server (MCSERV, PS2SDK `libmc`). After the served XMCSERV INIT (fno `0xFE`), the title issues fno `1` (refused, `rpc_call_replied=0`) and fno `0x15` (GET_INFO, refused). Neither is currently served.

## Corrected diagnosis (ADR 0223, capstone 5.0.7 MIPS64 LE on `boot.elf` adfda7b7…f0cf0c)

**This section replaces the earlier "fno 1 / sentinel" attribution, which was wrong on both counts.**

- Wrapper `0x246030` is **fno `0x15` (GET_INFO)**, not fno 1: `0x246090: 24050015  addiu $a1, $zero, 0x15` sets the function-number argument to `sceSifCallRpc` (`jal 0x25CF88` at `0x2460A8`), with send_size 48, recv_size 4, recv_buf `0x760780`.
- The actual **fno 1** call is at `0x2468FC` via wrapper `0x2467D8`: `0x2468F0: 24050001  addiu $a1, $zero, 1`, send_size 48, recv_size 4, end_function `0x246780` (confirmed by the ABI-45 probe fields `rpc_call_number=1`, `rpc_call_end_function=0x246780`).
- Poll loop `0x21F820` (called from `0x182974`, `0x18df84`, `0x1b59f0`) does **not** compare against a caller-supplied sentinel: the delay slot at `0x21F844` is `move $a0, $s1` where `$s1` is the retry counter (0, then 1); `0x21F84C` compares `lw($s0)` (reply word 0 at `0x29E2B8`) against the wrapper return `$v0`; max 2 attempts (`slti $v0, $s1, 2`).
- Reproducible ABI-45 probes (`ap-abi45-mcprobe-02`/`-03`, `frontier.py compare` exit 0): 1,279,999,986 instructions, 260 VBLANKs, `pc=0x0025ABB0`; `mc_calls=2` (INIT served + fno 1 refused), `mc_refused_calls=1`, `mc_request_head=[0,0,0,1]`. The fno 1 refusal happens early and the title continues for 260 VBLANKs to the legitimate per-frame wait — **fno 1 is not the cumulative frontier blocker**.

## Decision

**Do not implement mcserv fno 1 (or 0x15) HLE yet.** The request payload (48 bytes) and reply contract (4 bytes) are unknown, and fabricating a reply would violate fail-closed. Instead:

1. **Diagnostic recording (this ADR):** record the first four words of the last refused mcserv request in the run report, using the same bounded convention as SearchFile head (ADR 0156). A probe can then identify the payload shape from evidence instead of a guess.
2. **Next unit is wait-target attribution of the `0x25ABB0` loop** (ADR 0223 / Q1), not sentinel resolution: derive what the per-frame loop polls and who produces it.
3. Only if that attribution shows the missing producer is a mcserv reply, and the request/reply layout is derived from the title's own code with an evidenced "no card" answer (precedent ADR 0147/0174), consider modelling the call with that exact reply. Otherwise leave it refused.

## ABI

`REPS2_RUNTIME_ABI_VERSION` 44 -> 45: `EeSifCmdState` grows by `last_mc_request_head[4]` (16 bytes) + `mc_refused_calls` (4 bytes), so `sizeof(EeState)` changes. ABI-44 modules are refused at admission as `runtime_abi_mismatch`.

## Tests

- `tests/hle_syscall_smoke.cpp`: `test_mcserv_refusal_records_request_head` — a refused call records up to four payload words, replies nothing, signals no semaphore, touches no receive buffer.
- `tests/rg_reps2_engine_smoke.cpp`: ABI-44 module rejected as `runtime_abi_mismatch` (asserts `REPS2_RUNTIME_ABI_VERSION > 44`).

## Validation

- Rust gates: `cargo fmt --all -- --check`, `cargo clippy ... -D warnings`, `cargo test --workspace --locked`.
- C++: Release build + `ctest --test-dir build -C Release --output-on-failure`.
- Real-title probe at ABI 45: same digests, same run, `mc_refused_calls` and `mc_request_head` visible in `run-report.json` SIF block. No compatibility claim promoted.

## References

- PS2SDK `ee/rpc/memorycard/src/libmc.c`: binds `rpc_id = 0x80000400`.
- ADR 0149 (mcserv INIT), ADR 0156 (bounded head recording convention), ADR 0164/0169 (ABI discipline).
