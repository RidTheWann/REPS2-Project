# REPS2 project state

Updated: 2026-11-07

## Objective

Advance the multi-title PlayStation 2 static AOT recompiler toward a repeatable, guest-visible boot stage from an owner-supplied ISO. Downhill Domination USA (SCUS_971.77) is the current M4 vertical-slice target; it is evidence for the shared toolchain, not a title-specific product architecture.

The architectural split remains:
- Rust frontend: ISO/ELF parsing, R5900 decoding, discovery/CFG, typed IR, verification, deterministic C++ generation, manifests and CLI.
- C++20 runtime: EE state and memory, MMIO/device models, shared generated-code ABI and future PS2 subsystems.
- Generated native title module + manifest: explicit, versioned boundary between frontend output and shared runtime.

The product goal remains broader than one title and broader than an ELF-launch utility. Importing an ISO, extracting BOOT.ELF, translating functions, building C++ or passing synthetic tests does not establish that a PS2 game boots or is playable.

## Repository baseline and handoff branch

At the start of PR #22, default branch `main` was at `9653ad128a6c6a5187f038b6e927bee66fe0febe` (`9653ad1`), with PRs #17–#21 merged. The current change set is isolated in PR #22:

- PR: https://github.com/RidTheWann/REPS2-Project/pull/22
- Branch: `feat/dmac-dstat-contract-rea-handoff`
- Scope: bounded EE DMAC D_STAT register contract, generated-code regression test, ADR/research/roadmap update, REA workflow, and repository agent instructions.
- This state note is part of the same PR branch. CI must pass before the PR is merged.

Do not use the baseline SHA above as a claim about a later `main` after PR #22 is merged. Re-read the actual GitHub default-branch head when starting a new task.

## M4 real-title evidence

Current target identity recorded by the local investigation:

- Game: Downhill Domination USA, serial SCUS_971.77.
- Boot ELF SHA-256: `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`.
- ISO SHA-256: `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`.
- Entry PC: `0x0010A008`.
- Both explicitly declared copied-code regions are required by the current trace:
  - `0x80076000 <- 0x002829F8 (0x740 bytes)`
  - `0x80075000 <- 0x002831A0 (0x330 bytes)`

The latest recorded bounded probe, rebuilt after VU0 LQC2/SQC2 support, executed 3,168,708 guest instructions and stopped at PC `0x0025C274` with `unsupported_memory_access`. The guest path constructs EE address `0x1000E010` (DMAC D_STAT), reads it, masks SIF0 status bit `0x20`, and conditionally writes that bit back. This is a device-state boundary, not a boot stage. The title has not been shown to reach a repeatable guest-visible boot stage and is not Playable.

The actual D_STAT value at this point, the source of a pending SIF0 event, whether channel 5 is already running, required IOP/SIF interaction, and the next guest-visible service remain unknown. Keep them unknown until target-specific evidence establishes them.

## Implemented M4 path on the baseline

The merged M4 path includes:
- EE syscall dispatch through the guest-visible service table and translated guest syscall handlers.
- Observed early kernel HLE calls: SetupThread, SetupHeap, CreateSema, SetSyscall, GetEntryAddress and FlushCache.
- The bounded InitAlarm patch path and explicitly declared relocated code.
- CreateThread dormant records, StartThread READY queue bookkeeping, boot-thread id and priority changes. This is not a context-switching scheduler.
- Host-session GetOsdConfigParam/SetOsdConfigParam behavior. This is not a full physical OSD/board configuration model.
- VU0 LQC2/SQC2 quadword data movement with a separate VF file and architectural VF0 constant. This is not VU arithmetic or general VU execution.

See `docs/ROADMAP.md`, `docs/CAPABILITIES.md`, `docs/COMPATIBILITY_MATRIX.md`, and the relevant ADRs for exact scope and validation.

## PR #22 implementation boundary

PR #22 adds `runtime/include/reps2/runtime/ee_dmac.hpp` as the owner for D_STAT state and W1C/mask-toggle behavior. A default register state is unknown. Synthetic tests may explicitly seed a known state to verify the documented register transformations, but production runtime does not assume a hardware reset value and does not synthesize DMA events.

The PR adds a generated-code smoke test for the currently observed `lui/ori/lw/andi/sw` access sequence. The new test is not a real-title probe and does not prove the SIF0 pending bit is present. Unknown D_STAT reads/writes and writes to the unmodelled status fields remain explicit failures. The follow-up M4 requirement is a verified SIF0 event producer and its CHCR/IOP interaction, not merely a register accessor.

No local Windows/MSVC execution was available to this GitHub-only handoff. Use PR #22 CI to validate Ubuntu, Windows, MSRV and dashboard checks before merge.

## REA investigation handoff

`docs/REA_WORKFLOW.md` describes the local Windows workflow and current provider constraints. REA is a static reverse-engineering workbench, not a PS2 emulator. Its documented Windows Ghidra P0 path does not admit MIPS; its generic MIPS Ghidra path is documented for Linux/macOS and restricts ELF/profile declarations. A PS2 R5900 machine-specific ELF may therefore be refused. Do not modify its `e_flags` to bypass admission, and do not assume WSL removes target-format restrictions.

Keep the ISO, extracted ELF, BIOS/firmware, REA Evidence bundles and full decompiler output outside Git. Commit only lawful, compact, independently written observations, digests, public references and explicit unknowns.

## Remaining M4 exit criteria

1. Identify the exact SIF0 status producer and initial state for this title from the authorized ELF and repeatable trace.
2. Establish channel-5 CHCR transitions, IOP/SIF handshake and D_STAT read/write interactions without fabricated events.
3. Implement the required shared device/event behavior with evidence-led tests; preserve explicit unsupported behavior for unmodelled cases.
4. Produce a native title module linked to the shared runtime ABI and capture a repeatable guest-visible boot/entry stage with exact artifact digests and configuration.

M4 completion is one evidenced real-title stage, not proof of playability or multi-game compatibility. Later roadmap phases must expand shared EE/IOP, VU, GS, CDVD, SPU2, controller, memory-card and launcher subsystems before broad compatibility or a release claim.
