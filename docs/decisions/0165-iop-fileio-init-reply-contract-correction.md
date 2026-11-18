# ADR 0165: Correct the FILEIO init handshake dataflow — the compared operand is reply word 1

- **Status:** Accepted (documentation correction, verified from the boot ELF's
  instruction words; no code change).
- **Date:** 2026-11-18.
- **Scope:** `docs/RESEARCH.md`, `docs/PROJECT_STATE.md`, and the correction
  notice on ADR 0163. The runtime handler in
  `runtime/include/reps2/runtime/ee_hle.hpp` is unchanged: it is still
  fail-closed. ADR 0163 stays as the historical record of the observation it
  made.
- **Supersedes:** the *static dataflow* claim in ADR 0163 ("copies a word out of
  the static buffer `0x00764CA8` ... compares **that** word against `2`") and the
  request-head value it quotes. It does **not** supersede ADR 0163's decision to
  refuse the call.

## Context

ADR 0163 explained the frontier at `WaitSema(11)` (`pc=0x0025A724`) and asserted
that the post-call code reads the static buffer `0x00764CA8` and compares that
word with `2`. That assertion decides where a future investigation would look:
"who writes `0x00764CA8`" versus "what is in the reply buffer". It was never
checked against the instruction words, and it is wrong in a way that redirects the
research. This ADR re-reads the exact words from the same boot ELF and restates
the dataflow.

## Observed

Boot ELF SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`,
ELF32, `e_machine=8` (MIPS), one `PT_LOAD` with `p_vaddr=0x0010a000`,
`p_filesz=0x193cf0`. Words were read straight from the file (the region is not
relocated by the runtime).

- Region `0x0025D8C0..0x0025D97B` (188 bytes, 47 words, file offset `0x1548C0`),
  SHA-256 `79d64040c02fe406d299ab85ec94dd9eb8c8298453874a91300c3ef6db3d9839`.
- All addresses below are guest virtual addresses equal to the ELF address; no
  relocation applies.

Instruction words, verbatim:

| Address | Word | Instruction |
| --- | --- | --- |
| `0x0025D8C0` | `0x3C100076` | `lui $16, 0x0076` |
| `0x0025D8CC` | `0x261041C0` | `addiu $16, $16, 0x41C0` |
| `0x0025D8D0` | `0x26234200` | `addiu $3, $17, 0x4200` |
| `0x0025D8D4` | `0x26473540` | `addiu $7, $18, 0x3540` |
| `0x0025D8D8` | `0xAE433540` | `sw $3, 0x3540($18)` |
| `0x0025D8DC` | `0x26644C80` | `addiu $4, $19, 0x4C80` |
| `0x0025D8E0` | `0x24630440` | `addiu $3, $3, 0x0440` |
| `0x0025D8E4` | `0x240500FF` | `addiu $5, $0, 0x00FF` |
| `0x0025D8E8` | `0xACE30004` | `sw $3, 0x0004($7)` |
| `0x0025D8EC` | `0x0000302D` | `or $6, $0, $0` |
| `0x0025D8F0` | `0xAFA00000` | `sw $0, 0x0000($29)` |
| `0x0025D8F4` | `0x24080008` | `addiu $8, $0, 0x0008` |
| `0x0025D8F8` | `0x0200482D` | `or $9, $16, $0` |
| `0x0025D8FC` | `0x240A0008` | `addiu $10, $0, 0x0008` |
| `0x0025D900` | `0x0C0973E2` | `jal 0x0025CF88` |
| `0x0025D904` | `0x0000582D` | `or $11, $0, $0` |
| `0x0025D908` | `0x04410004` | `bgez $2, 0x0025D91C` |
| `0x0025D90C` | `0x3C042000` | `lui $4, 0x2000` |
| `0x0025D910` | `0x3C02FFFE` | `lui $2, 0xFFFE` |
| `0x0025D914` | `0x10000017` | `beq $0, $0, 0x0025D974` |
| `0x0025D918` | `0x3442FFFF` | `ori $2, $2, 0xFFFF` |
| `0x0025D91C` | `0x26020004` | `addiu $2, $16, 0x0004` |
| `0x0025D920` | `0x02041825` | `or $3, $16, $4` |
| `0x0025D924` | `0x00441025` | `or $2, $2, $4` |
| `0x0025D928` | `0x3C050076` | `lui $5, 0x0076` |
| `0x0025D92C` | `0x24A84CA8` | `addiu $8, $5, 0x4CA8` |
| `0x0025D930` | `0x88640003` | `lwl $4, 0x0003($3)` |
| `0x0025D934` | `0x98640000` | `lwr $4, 0x0000($3)` |
| `0x0025D938` | `0xA9040003` | `swl $4, 0x0003($8)` |
| `0x0025D93C` | `0xB9040000` | `swr $4, 0x0000($8)` |
| `0x0025D940` | `0x88430003` | `lwl $3, 0x0003($2)` |
| `0x0025D944` | `0x98430000` | `lwr $3, 0x0000($2)` |
| `0x0025D948` | `0xABA30013` | `swl $3, 0x0013($29)` |
| `0x0025D94C` | `0xBBA30010` | `swr $3, 0x0010($29)` |
| `0x0025D950` | `0x3C060028` | `lui $6, 0x0028` |
| `0x0025D954` | `0x3C050028` | `lui $5, 0x0028` |
| `0x0025D958` | `0x8FA30010` | `lw $3, 0x0010($29)` |
| `0x0025D95C` | `0x24040001` | `addiu $4, $0, 0x0001` |
| `0x0025D960` | `0xACA421EC` | `sw $4, 0x21EC($5)` |
| `0x0025D964` | `0x0000102D` | `or $2, $0, $0` |
| `0x0025D968` | `0x38630002` | `xori $3, $3, 0x0002` |
| `0x0025D96C` | `0x2C630001` | `sltiu $3, $3, 0x0001` |
| `0x0025D970` | `0xACC321F0` | `sw $3, 0x21F0($6)` |
| `0x0025D974` | `0xDFBF0070` | `ld $31, 0x0070($29)` |

Run report (ABI 19, same configuration as the committed Downhill records),
`run-report.json` SHA-256
`70ae72a714da1395d3dd38fe715ea86c4b61e89d119c7128302f701830545012`:
`fileio_calls=1`, `fileio_number=255`, `fileio_send_size=8`, `fileio_recv_size=8`,
`fileio_send_addr=7746880` (`0x00763540`), `fileio_recv_addr=7750080`
(`0x007641C0`), `fileio_request_head=[7750144, 7751232, 0, 0]`
(`[0x00764200, 0x00764640, 0, 0]`), `fileio_request_words=4`,
`fileio_name_present=0`, `instructions_executed=4586235`, `pc_hex=0x0025A724`.

## Derived

1. `$s0` becomes `0x007641C0` at `0x0025D8CC` (word 0), and the helper at
   `0x0025CF88` is called with `$9=$s0` and `$10=8`, so `0x007641C0` is the
   **reply buffer** and `8` is its size. The same pattern gives the request:
   `0x00763540` receives `0x00764200` at offset 0 (`0x0025D8D8`) and
   `0x00764640` at offset 4 (`0x0025D8E8`, `0x00764200 + 0x440`), so the request
   is **8 bytes holding two pointers**.
2. `0x0025D908` is `bgez $2`: `$v0 >= 0` branches **forward to `0x0025D91C`**
   (success). Falling through to `0x0025D910` is the failure path, which builds
   `0xFFFEFFFF` (`-65537`) and returns at `0x0025D974`.
3. `0x0025D90C` (`lui $4, 0x2000`) is the delay slot of that branch and always
   executes, but its value is consumed only on the success path: `0x0025D920`
   and `0x0025D924` OR it into the reply addresses, producing the **uncached
   aliases** `0x207641C0` and `0x207641C4`. Both reply words are read through
   those aliases.
4. `0x0025D92C` builds `0x00764CA8` into `$8`, and `0x0025D938`/`0x0025D93C` are
   `swl`/`swr` with `$8` as the **base**: the word loaded from
   `0x207641C0` (reply word 0) is **written to** `0x00764CA8`.
   `0x00764CA8` is a write destination in this function, not a source.
5. `0x0025D940`/`0x0025D944` load from `$2 = 0x207641C4`, i.e. **reply word 1**.
   It is round-tripped through the stack (`0x0025D948`/`0x0025D94C` store it at
   `$sp+0x10`, `0x0025D958` reloads it), then `0x0025D968`/`0x0025D96C` compare
   **that** register with `2` (`xori` + `sltiu`, i.e. `==2` as an unsigned
   predicate) and `0x0025D970` stores the boolean to `0x002821F0`.
   `0x0025D960` stores the constant `1` to `0x002821EC` immediately before it.
6. `fileio_request_words=4` is the recorder's **read window**, not the request
   size: `serve_ee_fileio_call` reads four words from `send_addr` and counts each
   successful read, independent of `send_size`. The request is `8` bytes, so
   words 2 and 3 of the recorded head are the bytes after the request, not part
   of it.
7. The frontier stops inside the `WaitSema` of the call made at `0x0025D900`
   (`pc=0x0025A724`), so `0x0025D908` and everything after it has **not
   executed**. The whole post-call analysis above is static: the guest has not
   yet compared anything with `2`, and no value has yet been written to
   `0x00764CA8` at runtime.

## Corrected claims

| Claim in ADR 0163 | Corrected |
| --- | --- |
| the success path "copies a word out of the static buffer `0x00764CA8`" | the success path copies reply word 0 **into** `0x00764CA8` (`swl`/`swr` with `0x00764CA8` as the base) |
| "derives a second flag by comparing **that** word against `2`" | the word compared against `2` is **reply word 1**, read from `0x007641C4` via its uncached alias `0x207641C4` |
| `fileio_request_head=[0x00764200, 0x00764300, 0, 0]` | the observed head is `[0x00764200, 0x00764640, 0, 0]`; `0x00764640 = 0x00764200 + 0x440` per `0x0025D8E0` |
| `lui $4, 0x2000` explains the uncached alias "on the failure path" | it is the `bgez` delay slot and is consumed **on the success path**; the failure path returns without reading memory |

## Unknown

- The reply contract of function 255: what reply word 0 (copied to
  `0x00764CA8`) and reply word 1 (compared to `2`) mean. Nothing here
  establishes a version, a status, or whether `2` is an expected value.
- What `0x00764CA8` and `0x00764C80` (`$a0` at `0x0025D8DC`) are inside the
  library's work area; the two are `0x28` bytes apart, which is observed, not
  explained.
- Who writes the reply buffer at runtime. In the recorded probe the run never
  returns from the call, so the buffer content and any reader of `0x00764CA8`
  after this function are unobserved. Reachable generated code forms the
  address `0x00764CA8` in two further blocks (`0x0025D998`, `0x0025DA28`), both
  downstream of the blocked call, so neither has been executed.
- Whether any IOP module, HLE path or DMA channel other than the SIF reply
  writes `0x007641C0..0x007641C7`.

## Decision

1. Record the corrected dataflow and treat `0x00764CA8` as an **output** of the
   handshake. The open question becomes "what does the IOP FILEIO server put in
   the 8-byte reply at `0x007641C0`", not "who writes `0x00764CA8`".
2. Keep the FILEIO handler fail-closed exactly as decided in ADR 0163. The
   correction changes where to look, not what may be fabricated: a reply that
   is not established by independent evidence would still be invention.
3. Do not add diagnostic fields to `EeSifCmdState` in this change. Any new
   report-only field changes the state layout and therefore requires an ABI
   revision under ADR 0164; the watchpoints below are a separate slice, planned
   as: writes to `0x007641C0..0x007641C7` (split by CPU store, HLE, SIF/DMA) and
   reads of `0x00764CA8`.

## Consequences

- The next FILEIO slice can be scoped honestly: establish the reply format of
  function 255 from an independent source (IOP module behavior or a
  hardware-faithful reference), then serve it. Until then the guest stays
  blocked at `WaitSema(11)` and this is a documented boundary, not a boot stage.
- ADR 0163 remains valid as the record of the observed request and the decision
  to refuse it; only its static-dataflow sentence and its quoted request head
  are superseded, and ADR 0163 now says so.

## Validation

- Instruction words and the region digest above were re-read from the owner's
  `boot.elf` with a standalone ELF32 reader (single `PT_LOAD`,
  `vaddr=0x0010a000`, file offset of `0x0025D8C0` = `0x1548C0`); the decode
  matches the generated C++ for the same addresses in
  `boot.part0008.cpp` (blocks `0x0025D8C8`, `0x0025D908`, `0x0025D91C`).
- The request-head values come from the ABI 19 `run-report.json` quoted above,
  not from a retyped constant.
- No code or runtime behaviour changed, so no test result is claimed here. The
  existing `test_fileio_init_call_is_observed_and_refused` smoke case still
  describes the handler as it stands.