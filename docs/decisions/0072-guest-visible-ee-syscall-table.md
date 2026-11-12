# ADR 0072: Guest-visible EE syscall table written by SetSyscall

- **Status:** Accepted
- **Date:** 2026-11-06
- **Scope:** opt-in EE syscall HLE layer, M4 real-title execution

## Context

ADR 0067 implemented `SetSyscall` (0x74) as host-side registration: the slot and
handler were validated and recorded in `EeHleState`, but guest memory was left
untouched. ADR 0069 later dispatched registered slots into translated guest
handlers.

The Downhill Domination init routine at `0x0025EF90` shows that the guest
expects a second, memory-visible half of the same operation:

1. It calls the `0x74` wrapper for `(0x83, 0x0025EF38)` and
   `(0x5A, 0x0025EF00)`.
2. It calls `FindAddress` (0x83) twice over the window
   `[$a0=0x80000000, $a1=0x80080000)` looking for those two handler values.
3. It subtracts a fixed slot offset from each hit — `$s3 - 0x20C` for 0x83 and
   `$s2 - 0x168` for 0x5A — and iterates until the two computed bases agree, then
   stores that base at `0x00282210`.

The offsets are exactly `service_number * 4`: `0x83 * 4 = 0x20C`,
`0x5A * 4 = 0x168`. The guest is therefore recovering the base of a flat,
contiguous array of 32-bit handler pointers indexed by service number, and
expects `SetSyscall` to have copied the registered pointer into that array in
low EE main RAM.

Without that copy both searches return 0, the base computations underflow to
`0xFFFFFDF4`/`0xFFFFFE98`, and the loop's first iteration searches from
address 4 across almost the whole KUSEG window until the probe budget is
exhausted. The previous bounded probe stopped at `budget_exhausted` with
`pc=0x0025EF5C` for exactly this reason.

## Evidence

- ps2sdk `ee/kernel/src/libosd_full.c` states, above its syscall patch table:
  "This uses the Setup (aka \"SetSyscall\") syscall to patch the kernel. The
  address is relative to the start of the syscall table, and is in units of
  32-bit pointers (4 bytes)." The same file names the base: "`0x80011F80` is the
  start of the syscall table" (`0x80011F80 + (-15358 * 4) = 0x80002F88`).
- A ps2dev forum probe independently installs marker handlers for 0xFE/0xFF,
  scans `0x80000000..0x80080000` in 4-byte steps, and derives the base as
  `address - syscall_number * 4`. The thread also reports that the base varies
  between BIOS versions, which is why the guest recovers it at runtime rather
  than hardcoding it.
- The retail routine's own disassembly reproduces that formula: `-0x20C` and
  `-0x168` are exactly `-0x83*4` and `-0x5A*4`.

## Decision

1. Reserve the syscall handler table at the KSEG0 address `0x80011F80`
   (`kEeSyscallTableBase`) as a flat array of 32-bit pointers indexed by service
   number, matching the documented ps2sdk base. The guest-visible table covers
   **512 slots** (`kEeSyscallTableSlots`), i.e. 2 KiB ending at `0x80012780`.
   ps2sdk documents services beyond 0xFF — its `alarm.c` InitAlarm patches slot
   `0x12C` (`Intc12Handler`) — so the earlier 256-slot table was too small. The
   slot address is the arithmetic `base + number * 4`, not a separately bounded
   array. Host registrations stay indexed for 0..255 and use a small sparse list
   for extended numbers so `EeState` remains a compact value (tests hold many
   states on the stack).
2. After the existing slot/handler validation, `SetSyscall` stores the handler
   value verbatim at `kEeSyscallTableBase + slot * 4` through the normal EE
   address-resolution path (`store_u32`). The host registration is still
   recorded for dispatch.
3. If the table slot cannot be written, the call is refused with the new
   `EeSetSyscallRejection::table_unwritable` and no host registration is made.
   Refused slots and handlers write nothing.
4. Host-serviced slots keep zero entries. REPS2 does not fabricate kernel
   handler addresses for services it does not implement.

## Consequences

- A guest can now recover the table base from its own registered handlers, which
  is the behaviour the observed init depends on.
- Direct guest writes to the table become ordinary main-RAM writes. Dispatch
  still consults the host registration table, so a title that patches an
  unregistered slot directly in RAM is not yet honoured; detecting such patches
  requires observed evidence and is a separate decision.
- `0x80011F80..0x8001277F` is now kernel-reserved low RAM. It lies below the
  Downhill Domination load window (`0x0010A000`) and inside the 512 KiB window
  the title scans, so it cannot be mistaken for guest code or data.
- The table is addressed arithmetically. A negative or very large service
  number computes an address outside the modelled 512 slots and is refused by
  `SetSyscall` after the same address-resolution checks; the documented
  `0xFFFFC402` patch form from `libosd.c` is not yet honoured by registration
  (see ADR 0073 for `GetEntryAddress`, which does compute it).
- The fixed base is an HLE choice. A real BIOS may place the table elsewhere;
  because the guest derives the base from found handlers, the choice remains
  compatible with that recovery path.

## Verification

- `reps2_hle_syscall_codegen_smoke` covers: a successful `0x74` publishes the
  handler at the KSEG0 slot and its KUSEG alias; a rejected misaligned handler
  leaves a canary untouched; a KSEG0 handler is stored verbatim and readable
  through both aliases (32 cases total).
- Real-title evidence is recorded in `docs/PROJECT_STATE.md` and
  `docs/RESEARCH.md`; the probe output and exact test configuration are stored
  there rather than in this ADR.
