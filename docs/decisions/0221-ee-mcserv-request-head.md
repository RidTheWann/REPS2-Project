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

`0x80000400` is the memory-card SIF RPC server (MCSERV, PS2SDK `libmc`). Function 1 is the title's next call after XMCSERV INIT (function `0xFE`, already served). It is currently refused (`rpc_call_replied=0`), so the caller stays blocked in its retry loop.

## Diagnosis of mcserv fno 1 (observed via capstone disassembly of `boot.elf`)

**Caller chain:** `0x182974`, `0x18df84`, `0x1b59f0` each `jal 0x21F820` with an argument in `$a0`.

**Function `0x21F820(expected_reply_word0)`** (capstone, fully decoded):

```
0x21F820: sp -= 0x20; save s0,s1,ra
0x21F82C: s0 = 0x2A0000 - 0x1D48 = 0x282DB8   // reply buffer base
0x21F838: s1 = 0                                // retry counter
0x21F840: jal 0x246030   delay: a0 = s1          // mcserv fno 1 wrapper
0x21F848: v1 = lw (s0)                            // read reply word 0
0x21F84C: if (v1 == v0) goto exit                 // sentinel match
0x21F850: s1 += 1
0x21F854: jal 0x21F620   delay: sw v0, (s0)     // wait between attempts
0x21F85C: if (s1 < 2) goto 0x21F840               // at most 2 attempts
exit:
0x21F864: s0 += 4                                 // advance reply pointer
0x21F868: restore; jr ra
```

- The wrapper `0x246030` sends `send_size=48`, `recv_size=4`, `recv_addr` derived from `$s9`, and reads reply word 0 on the success path.
- **The loop exits by itself** after at most 2 attempts, or earlier when reply word 0 equals the sentinel. It is **not** an unbounded wait.
- `mc_calls=2` in the probe matches the two attempts. `rpc_call_replied=0` means no IOP replied — the loop still exits by retry exhaustion.
- Post-exit (`0x21F868`) is only an epilogue; the *caller* decides what to do with a failed attempt.

**What is NOT known:**
- The sentinel value (`$a0` argument of `0x21F820`) at each of the three call sites — requires resolving caller state at `0x182974`/`0x18df84`/`0x1b59f0`.
- What the caller does after `0x21F820` returns with reply word 0 == 0 (no-card path vs retry elsewhere).

## Decision

**Do not implement mcserv fno 1 HLE yet.** The request payload (48 bytes) and reply contract (4 bytes) are unknown, and fabricating a reply would violate fail-closed. Instead:

1. **Diagnostic recording (this ADR):** record the first four words of the last refused mcserv request in the run report, using the same bounded convention as SearchFile head (ADR 0156). A probe can then identify the payload shape from evidence instead of a guess.
2. **Reserve sentinel resolution** as the next unit: read the caller argument at each `jal 0x21F820` site and trace what happens when reply word 0 is zero.
3. Only then, if the caller treats reply word 0 == 0 as "no card / error" and exits its own loop, consider modelling mcserv fno 1 with that exact error result. Otherwise leave it refused.

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
