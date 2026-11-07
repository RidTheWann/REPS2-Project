# REPS2 project state

Updated: 2026-11-07

## Objective

Advance the multi-title PlayStation 2 static AOT recompiler toward a repeatable, guest-visible boot stage from an owner-supplied ISO. Downhill Domination USA (SCUS_971.77) is the current M4 vertical-slice target; it is evidence for the shared toolchain, not a title-specific product architecture.

The architectural split remains:
- Rust frontend: ISO/ELF parsing, R5900 decoding, discovery/CFG, typed IR, verification, deterministic C++ generation, manifests and CLI.
- C++20 runtime: EE state and memory, MMIO/device models, shared generated-code ABI and future PS2 subsystems.
- Generated native title module + manifest: explicit, versioned boundary between frontend output and shared runtime.

The product goal remains broader than one title and broader than an ELF-launch utility. Importing an ISO, extracting BOOT.ELF, translating functions, building C++ or passing synthetic tests does not establish that a PS2 game boots or is playable.

## Verified repository state

- Default branch: `main` at `4d1420588fafdc9e85793036280108caf08a36f9` (PR #22 squash-merged).
- PR #22: https://github.com/RidTheWann/REPS2-Project/pull/22 — merged on 2026-11-07.
- The merge adds the guarded EE DMAC D_STAT contract, generated-code CTest coverage, ADR 0079, updated research/roadmap, the REA workflow, and root agent instructions.
- No open PRs or issues were returned by the repository search at this snapshot.
- PR #22 CI completed successfully on the merged head: Rust frontend Ubuntu, Windows, MSRV 1.88, and progress dashboard. Ubuntu and Windows both passed CMake configure/build and CTest; Windows also passed the `reps2.exe version` CLI smoke test. The PR's generated D_STAT test is included in that CTest run.
- The optional Codex PR review could not execute because the repository reached the review usage limit. It did not produce a code review result; CI validation is separate from that optional review.

Re-read the actual GitHub default-branch head and live PR/issue state for every later task rather than treating this timestamped snapshot as permanent.

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

## Integrated D_STAT register contract

PR #22 is merged. `runtime/include/reps2/runtime/ee_dmac.hpp` owns D_STAT state and the bounded W1C/mask-toggle behavior. A default register state is unknown. Synthetic tests explicitly seed a known value to verify documented register transformations, but production runtime does not assume a hardware reset value and does not synthesize DMA events.

A generated-code CTest fixture exercises the observed `lui/ori/lw/andi/sw` access sequence. It verifies fail-closed behavior for unknown state, SIF0 bit-5 acknowledgement for a seeded synthetic state, mask-toggle semantics, and no partial mutation for unmodelled status writes. It is not a real-title probe and does not prove that the SIF0 pending bit is present during the target trace.

PR #22 CI passed on Ubuntu and Windows, including CMake build, CTest, and platform CLI smoke tests, plus the MSRV and progress-dashboard jobs. The follow-up M4 requirement remains a verified SIF0 event producer and its CHCR/IOP interaction, not merely a register accessor.

## REA investigation handoff

`docs/REA_WORKFLOW.md` describes the local Windows workflow and current provider constraints. REA is a static reverse-engineering workbench, not a PS2 emulator. Its documented Windows Ghidra P0 path does not admit MIPS; its generic MIPS Ghidra path is documented for Linux/macOS and restricts ELF/profile declarations. A PS2 R5900 machine-specific ELF may therefore be refused. Do not modify its `e_flags` to bypass admission, and do not assume WSL removes target-format restrictions.

Keep the ISO, extracted ELF, BIOS/firmware, REA Evidence bundles and full decompiler output outside Git. Commit only lawful, compact, independently written observations, digests, public references and explicit unknowns.

## Remaining M4 exit criteria

1. Identify the exact SIF0 status producer and initial state for this title from the authorized ELF and repeatable trace.
2. Establish channel-5 CHCR transitions, IOP/SIF handshake and D_STAT read/write interactions without fabricated events.
3. Implement the required shared device/event behavior with evidence-led tests; preserve explicit unsupported behavior for unmodelled cases.
4. Produce a native title module linked to the shared runtime ABI and capture a repeatable guest-visible boot/entry stage with exact artifact digests and configuration.

M4 completion is one evidenced real-title stage, not proof of playability or multi-game compatibility. Later roadmap phases must expand shared EE/IOP, VU, GS, CDVD, SPU2, controller, memory-card and launcher subsystems before broad compatibility or a release claim.
