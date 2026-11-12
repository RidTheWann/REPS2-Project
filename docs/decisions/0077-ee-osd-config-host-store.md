# ADR 0077: EE OSD configuration services as a documented host-session store

- Status: Accepted
- Date: 2026-11-06
- Related: ADR 0076 (thread chain), ADR 0062 and 0064 (EE syscall HLE)

## Context

After ADR 0076 the Downhill Domination probe stops at `GetOsdConfigParam`
(0x4B) at `pc=0x0025A794` with `$a0=0x0093C7C0` and `$sp=0x0093C7C0`, so the
guest passes a 4-byte stack buffer. The caller at `0x0025F148` is:

- `GetOsdConfigParam($sp)`.
- Load the word, clear bits 13..15 (`& 0xFFFF1FFF`), set bit 13 (`| 0x2000`).
- `SetOsdConfigParam($sp+4)` with the modified word.
- `GetOsdConfigParam($sp+8)` and read the result back.
- `SetOsdConfigParam($sp)` to restore the original.
- Return `(((value >> 13) & 7) < 1)`.

ps2sdk `ee/kernel/include/kernel.h` declares `void GetOsdConfigParam(void *addr)`
and `void SetOsdConfigParam(void *addr)`; both return void. The wrappers at
`0x0025A790` and `0x0025A780` are the observed `0x4B` and `0x4A` entries in the
game's own syscall table.

The kernel keeps this word in NVRAM/EEPROM. REPS2 has no NVRAM, no BIOS and no
EEPROM image, and must not claim persistence it does not provide.

## Decision

1. Maintain one 32-bit host-session word, `osd_config_param`, initialized to 0.
   The value is not persisted across sessions and no board-specific value is
   invented.
2. `GetOsdConfigParam` (0x4B) writes that word to the guest buffer through the
   normal EE address path (4 bytes, 4-byte alignment, mapped region). On a
   refused buffer it records `unwritable_buffer` and leaves memory unchanged.
3. `SetOsdConfigParam` (0x4A) reads four bytes from the same validated path and
   stores them verbatim. No bit masking or policy is applied because no public
   source observed here defines one. On a refused buffer it records
   `unreadable_buffer` and leaves the host word unchanged.
4. Both services report a refusal as `-1` in `$v0` for diagnostics. The
   documented return type is void, so this is not a guest-visible contract and no
   caller in the observed path tests it.
5. `kEeSyscallServices` must stay strictly ascending by service number because
   lookup stops at the first larger number. A `static_assert` now enforces this,
   after a mis-ordered insertion in this change silently hid `SetupThread` from
   lookup.

## Consequences

- The observed read-back probe is coherent: the guest sets bit 13, reads it back
  and restores the original. Bits 13..14 therefore read back as `01`, so the
  helper's `(((value >> 13) & 7) < 1)` expression is false.
- Services that later depend on language (bits 0..3) or aspect see the zeroed
  host default. That is a recorded limitation, not a hardware claim.
- Running two sessions does not share configuration. A future settings service
  can persist the same word with explicit provenance.

## Verification

- `reps2_hle_syscall_codegen_smoke` grew from 47 to 52 cases: default read, a
  set/get round trip preserving arbitrary bits, a replay of the observed
  read-back probe, refusals for an unsupported physical window and a misaligned
  buffer (with state unchanged), and dispatcher routing for 0x4A and 0x4B.
- The service-table ordering guard is compile-time; the full 30-test CTest suite
  still passes.
- Real-title probe (2026-11-06): `status=4` (`unresolved_control_flow`),
  `pc=0x80075000`, `executed=2,948,141`, `syscalls=42`, `hle_handled=30`. The
  sequence contains `0x0025A794:75:H:0x0093C7C0:...`,
  `0x0025A784:74:H:0x0093C7C4:...:0xFFFF1FFF`,
  `0x0025A794:75:H:0x0093C7C4:...` and
  `0x0025A784:74:H:0x0093C7C0:...`, then boot copies a second blob to
  `0x80075000` and dispatches into it. The stop is untranslated relocated code,
  not a missing service.
