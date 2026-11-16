# ADR 0152: Register the title's 989snd sound-driver RPC servers (0x00123456/0x00123457); calls recorded and refused

- **Status:** Proposed (validated locally; real-title probe recorded below).
- **Date:** 2026-11-16.
- **Scope:** EE SIF RPC server table (`ee_hle.hpp`) constants, a refuse handler
  and two registrations for sids 0x00123456/0x00123457; the table bound
  (`ee_state.hpp`, `kEeMaxSifRpcServers` 16 -> 18); a hand-edit of the
  native-build driver smoke's expected ABI. Runtime ABI 14 -> 15.

## Context

With the MMI3 PCPYH and MDU1 words decoded and AOT-lowered (ADRs 0150/0151),
the Downhill Domination probe no longer stops on an instruction: it executes
79,999,923 instructions over 16 batches and ends `budget_limit_reached` at
`pc=0x002012B0`, spinning in a delay loop that retries a SIF RPC bind
(`rpc_bind_sid=0x00123456`, `rpc_bind_had=0`). The frontier is therefore a
runtime server, not a decoder gap.

- **Observed (target disassembly of the owner's boot ELF `adfda7b7...f0cf0c`;
  runtime VAs; words read from the generated module's RAM image where file
  offset equals guest virtual address):** the routine at `0x00201180` calls
  `0x0025C7D0` and then binds through `0x0025CDB8` at `0x00201268` with
  `$4 = 0x004C8940` (the client data, `$20`), `$5 = 0x00123456` (built by
  `lui $5,0x12` at `0x00201258` + `ori $5,$5,0x3456` at `0x00201264`) and
  `$6 = 0`. On a non-negative return (`bgez $2, 0x002012A4` at `0x00201270`)
  it delays at `0x002012A4..0x002012C8` (`addiu $2,$2,-1` from `0x2710` = 10,000
  against `$3 = -1`) and re-reads `clientdata + 0x24` at `0x002012CC`
  (`lw $2, 0x24($20)`); when that word is zero it branches back to `0x00201260`
  and re-binds. The bind helper's failure prints name the source file: the
  format string at `0x002977A0` is
  `"error: sceSifBindRpc in %s, at line %d\n"` and the argument at `0x002977C8`
  is `"989snd.c"`, printed with line 178 at `0x0020127C..0x00201280`. The
  success path then binds a **second** server at `0x00201308..0x00201310` with
  `$4 = 0x004CD3C0` and `$5 = 0x00123457` (`ori $5,$5,0x3457` at `0x0020130C`),
  printing the same `"989snd.c"` file with line 199 on failure. The two `ori`
  immediates are the only `0x3456`/`0x3457` words in the image, and the only
  `0x00120000`-based sid construction.
- **Observed (ASCII strings in the owner's boot ELF):** the title's IOP module
  list references `cdrom0:\IOPRP270.IMG;1` and
  `MOD\989SND.IRX` alongside the other modules (`docs/RESEARCH.md`). The
  owner's ISO contains a `MOD\989SND.IRX` directory entry.
- **Derived:** sid `0x00123456` (and its sibling `0x00123457`) is the SIF RPC
  server the title's **989snd** sound driver binds. The title's own
  `sceSifIopReset` loads `MOD\989SND.IRX`, which registers the server; REPS2
  executes no IOP module, so without a modelled entry the bind is answered with
  the documented no-server reply (ADR 0104), `clientdata + 0x24` stays zero and
  the delay-and-recheck loop never exits. A budget increase cannot advance it.
- **Unknown:** which of the two ids belongs to which module (`989snd.irx` versus
  its `989ERR.irx` companion, both in the module list), and the 989snd RPC
  protocol (function numbers, request/reply encoding). Neither is established
  by the pinned sources and neither is claimed.

## Decision

1. Register `0x00123456` and `0x00123457` with a handler `serve_ee_snd_call`,
   so the two binds in the title's sound-initialisation routine succeed exactly
   as they do once the title's own IOP reset has loaded `MOD\989SND.IRX`.
2. `serve_ee_snd_call` models **no** command: it returns `false`, so the call is
   recorded by the shared SIF dispatch (`last_rpc_call_number`,
   `last_rpc_call_server_sid`, `last_rpc_call_request_word`,
   `last_rpc_call_send_size`, `last_rpc_call_recv_size`, `rpc_call_count`) and
   refused — no reply payload is written, no `SIF_CMD_RPC_END` is queued and the
   caller's semaphore is not raised. A caller that waits on a sound command
   therefore stays blocked instead of receiving a fabricated reply. REPS2
   executes no IOP module, so no sound command can be answered.
3. The IOP SIFRPC server table bound `kEeMaxSifRpcServers` rises from 16 to 18:
   the two new servers fill the boot set to 17 entries and a session (including
   the end-function-callback smoke tests) registers servers on top of it, which
   the 16-entry table no longer had room for. The added array storage shifts the
   `EeSifCmdState` layout, so Runtime ABI 14 -> 15.

## Consequences

- The title's sound bind loop completes: both binds now return their sid as the
  opaque `sd` token and `clientdata + 0x24` becomes non-zero, so the routine
  leaves the retry loop and reaches its first 989snd RPC call.
- That first call is recorded but refused, so the caller blocks; the frontier
  moves from an opaque budget-exhausting retry loop to a named RPC call whose
  function number the run report records.
- No sound is modelled and no sound state is fabricated. Any later call stays
  fail-closed. No compatibility state changes.

## Validation

- `reps2_hle_syscall_codegen_smoke` adds three cases (174 total): both servers
  are registered by the boot environment; a bind to `0x00123456` reaches the
  registered server and writes its sid into `clientdata + 0x24`; a call on
  `0x00123457` is recorded (`rpc_call_count`, `last_rpc_call_number`,
  `last_rpc_call_server_sid`) and refused (no reply, no RPC_END, reply untouched,
  semaphore untouched). The native-build driver smoke expects runtime ABI 15.
- CMake Release build and `ctest -C Release` (43/43); Rust `fmt`/`clippy`/`test`
  on Windows x64/MSVC.
- Real-title probe (owner's ISO SHA-256 `0fe2b777...a7eb86`, boot ELF
  `adfda7b7...f0cf0c`, Windows x64/MSVC, runtime ABI 15, frontend rebuilt from
  this branch — `PostADR152/run-report-989snd.json`): the last recorded bind is
  `rpc_bind_sid=1193047=0x00123457` with `rpc_bind_server=0x00123457` and
  `rpc_bind_had=1`, so both binds in the routine at `0x00201180` now succeed
  (previously `rpc_bind_sid=0x00123456`, `had=0`). The driver issues its first
  989snd RPC call, recorded as `rpc_call_server=1193046=0x00123456`,
  `rpc_call_number=0`, `rpc_call_replied=0`: the call is refused and no reply
  or `SIF_CMD_RPC_END` is produced. The run then ends `budget_limit_reached`
  after 79,999,984 instructions over 16 batches at `pc=0x00201FC0`. That PC is
  `jal 0x00201640` with `beq $2, $0, -6` at `0x00201FD4` looping while the
  callee returns zero; `0x00201640` polls the 989snd RPC client state through
  `jal 0x0025D178` with `$4 = 0x004C8940` (`0x0025D178` reads
  `*(clientdata)`, `*(clientdata + 4)` against `*(server + 0x18)` and bit 0 of
  `*(server + 0x10)`), so the loop is the driver's own wait for IOP returns
  that a served reply would satisfy. This is a caller-blocked-on-a-refused-
  command boundary (budget exhaustion on a poll loop), the same class as the
  earlier recorded/refused servers, not a boot stage. Callbacks stay 78/78,
  `mcserv` INIT is still served (`mc_calls=1 mc_result=0`), and the 78
  `CD_NCMD_READ`s are still served (`ncmd_read_calls=78`,
  `ncmd_read_lbn=337`).
