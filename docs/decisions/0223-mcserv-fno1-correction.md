# ADR 0223: Correct mcserv fno 1 identity and reassess frontier significance

- **Status:** Accepted (autopilot) — owner may overrule
- **Date:** 2026-11-27
- **Scope:** `docs/decisions/0223-mcserv-fno1-correction.md`, `docs/RESEARCH.md`, `docs/PROJECT_STATE.md`
- **Supersedes:** ADR 0222's claim that mcserv `0x80000400` fno 1 is the cumulative frontier blocker
- **Related:** ADR 0149 (mcserv INIT), ADR 0221 (mcserv request head), ADR 0220 (VBLANK timing)

## Context

ADR 0221 and ADR 0222 claimed:
1. The frontier blocker is mcserv `0x80000400` **function 1**
2. Wrapper `0x246030` sends 48 bytes / receives 4 bytes
3. Poll loop `0x21F820` compares reply word 0 against a caller-supplied sentinel in `$a0`
4. Three call sites may pass different sentinels

All four claims were contradicted by verified disassembly of the owner-supplied ELF.

## Evidence (read-only; no repo code changed)

### Tooling
- Capstone 5.0.7 (`5.0.1280`), arch=MIPS, mode=MIPS64|LITTLE_ENDIAN (required because the R5900
  toolchain emits `move` as the 64-bit `daddu`, funct `0x2D`, which MIPS32 silently drops)
- Artifact: owner-supplied Downhill Domination boot ELF
  SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`

### Observation 1 — poll loop is not a sentinel compare (VERIFIED)

```
0021F82C: 3C10002A  lui    $s0, 0x2a
0021F830: 2610E2B8  addiu  $s0, $s0, -0x1d48    ; $s0 = 0x0029E2B8 (reply-word array base)
0021F838: 0000882D  move   $s1, $zero           ; retry counter = 0
0021F840: 0C09180C  jal    0x246030             ; wrapper
0021F844: 0220202D  move   $a0, $s1             ; delay slot: $a0 = retry counter (0, then 1)
0021F848: 8E030000  lw     $v1, ($s0)           ; reply word 0 for this port
0021F84C: 10620003  beq    $v1, $v0, 0x21F85C   ; reply word == wrapper return?
0021F850: 26310001  addiu  $s1, $s1, 1
0021F854: 0C087D88  jal    0x21F620
0021F858: AE020000  sw     $v0, ($s0)
0021F85C: 2A220002  slti   $v0, $s1, 2           ; at most 2 attempts
0021F860: 1440FFF7  bnez   $v0, 0x21F840
0021F864: 26100004  addiu  $s0, $s0, 4           ; advance reply pointer AFTER the loop
```

- `$a0` is the **retry counter** (0, then 1), not a caller-supplied sentinel.
- `$s0` advances by 4 **only after the loop exits**, not per attempt.
- Comparison at `0x21F84C`: `reply_word_0 == wrapper_return($v0)`, not `reply_word_0 == $a0`.

### Observation 2 — wrapper `0x246030` is fno `0x15` (GET_INFO), not fno 1 (VERIFIED)

```
00246088: 0240202D  move   $a0, $s2        ; a0 = sd (client object)
00246090: 24050015  addiu  $a1, $zero, 0x15 ; a1 = fno = 0x15 (GET_INFO)
00246098: 0000302D  move   $a2, $zero       ; mode = 0
0024609C: 24080030  addiu  $t0, $zero, 0x30 ; send_size = 48
002460A0: 26690780  addiu  $t1, $s3, 0x780  ; recv_buf = 0x760780
002460A4: 240A0004  addiu  $t2, $zero, 4     ; recv_size = 4
002460A8: 0C0973E2  jal    0x25CF88          ; sceSifCallRpc(sd, 0x15, 0, client, 48, 0x760780, 4, 0, 0)
```

`0x24050015` is `addiu $a1, $zero, 0x15`; `$a1` is the function-number argument to `sceSifCallRpc`
at `0x25CF88`. So wrapper `0x246030` is fno `0x15`.

### Observation 3 — the actual fno 1 call site is `0x2468FC` via wrapper `0x2467D8` (VERIFIED)

```
002468E0: 26C4F1C0  addiu  $a0, $s6, -0xe40 ; a0 = sd
002468E4: 2627F240  addiu  $a3, $s1, -0xdc0 ; a3 = client
002468F0: 24050001  addiu  $a1, $zero, 1     ; fno = 1
002468F4: 24060001  addiu  $a2, $zero, 1     ; mode = 1
002468F8: 24080030  addiu  $t0, $zero, 0x30  ; send_size = 48
002468FC: 0C0973E2  jal    0x25CF88          ; sceSifCallRpc
00246900: 240A0004  addiu  $t2, $zero, 4     ; recv_size = 4
```

Post-call: on success (`$v0 == 0`) store `1` to `0x27F7C8` and return 0; on failure, call
`0x25A700` (syscall 0x44 = `WaitSema`) then return the result.

### Observation 4 — fresh ABI-45 probe confirms fno 1 is not the cumulative frontier blocker (VERIFIED)

`ap-abi45-mcprobe-02`, revision `b04b1cb`, runtime ABI 45, ISO SHA-256
`0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`:

| Field | Value |
|---|---|
| outcome | `budget_limit_reached` |
| `pc` | `0x0025ABB0` |
| `instructions_executed` | 1,279,999,986 (16 batches × 80M) |
| `vblank.starts` / `.ends` | 260 / 260 |
| `mc_calls` | 2 (INIT served + fno 1 refused) |
| `mc_number` | 1 |
| `mc_refused_calls` | 1 |
| `mc_request_head` | `[0, 0, 0, 1]` |
| `rpc_call_server` | `0x80000400` |
| `rpc_call_number` | 1 |
| `rpc_call_replied` | 0 |
| `rpc_call_send_size` / `recv_size` | 48 / 4 |
| `rpc_call_end_function` | `0x246780` |

The refused fno 1 call happens early; the title proceeds through **260 VBLANKs** (~1.28B instructions)
and stops at `pc=0x0025ABB0` — the legitimate per-frame wait loop ADR 0220 already identified.
The fno 1 refusal is an early error path the title recovers from.

### Observation 5 — reply semantics of fno 1 are NOT established (NOT VERIFIED)

The RPC end function `0x246780` propagates three reply words from uncached memory (`0x20000000|a0`)
to `0x76F1E8..0x76F1F4`, but the request payload layout (48 bytes) and the reply word meanings are
not documented. A zero reply cannot be confirmed as `sceMcResNoFile`/`sceMcResFailDetect` against
the title's own error handling.

## Decision

1. **Correct the ADR 0221/0222 claim:** wrapper `0x246030` is fno `0x15` (GET_INFO), not fno 1.
   The fno 1 call is at `0x2468FC` via wrapper `0x2467D8`.
2. **Do not serve fno 1 with a fabricated reply.** Reply semantics are unestablished; serving zeros
   risks wrong behavior per the title's error handling. Leave it refused (fail closed).
3. **Do not treat fno 1 refusal as the cumulative frontier blocker.** The fresh ABI-45 probe shows
   it happens early and the title continues for 260 VBLANKs to `pc=0x0025ABB0`. Q1's sentinel-resolution
   item is closed as **not the frontier blocker**.
4. **No runtime ABI change.** The `mc_request_head`/`mc_refused_calls` diagnostics already record
   both calls; no schema change needed.
5. **Q3 becomes the critical path:** DMAC channel 2 + GIF + GS software renderer + PNG dump (TD-0),
   per MASTER_PROMPT.md section 8.

## Evidence ledger

| Claim | Label | Evidence |
|---|---|---|
| Poll loop compares reply word to wrapper return, not `$a0` | **VERIFIED** | `0x21F844` delay-slot `move $a0, $s1`; `0x21F848` `lw $v1, ($s0)`; `0x21F84C` `beq $v1, $v0` |
| Wrapper `0x246030` is fno `0x15` | **VERIFIED** | `0x24050015` at `0x246090` |
| fno 1 call is at `0x2468FC` via wrapper `0x2467D8` | **VERIFIED** | `0x24050001` at `0x2468F0`; probe `mc_number=1`, `send_size=48`, `recv_size=4`, `end_fn=0x246780` |
| Refused fno 1 is not the cumulative frontier blocker | **VERIFIED** | Fresh ABI-45 probe: 260 VBLANKs, 1.28B instr, `pc=0x0025ABB0` |
| Reply semantics of fno 1 | **NOT VERIFIED** | No documented request/reply layout; caller passes client-object fields |
| Sentinel values per call site (ADR 0222) | **NOT VERIFIED** | No sentinel argument exists; `$a0` is retry counter |

## Files changed (in worktree `.worktrees/ap-adr0223`)

None yet — this ADR is documentation-only. ADR 0222 remains on branch `docs/adr-0222-mcserv-sentinel`; this ADR supersedes its claim without modifying it. ADR 0221 wording (fno 1) is refuted but unchanged here; a follow-up may correct it.

## Next unit

Q3: DMAC channel 2 chain-mode transfer + GIF tag parsing + GS software renderer + PNG dump (TD-0),
then Q5/Q6 host window/pacing + pad HLE, then Q7 stability soak, then Q8 TD-1 package.
