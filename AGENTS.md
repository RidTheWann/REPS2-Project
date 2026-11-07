# REPS2 Repository Instructions

## Mission and architecture

REPS2 is a game-independent PlayStation 2 static AOT recompiler and Windows PC platform. The intended user input is one lawfully owned PS2 ISO; disc parsing, boot ELF extraction, analysis, translation, native build, and runtime selection are internal implementation stages. One title is a vertical-slice validation target, never the architecture.

Respect the existing boundary:
- Rust owns ISO/ELF parsing, R5900 decoding, function/CFG analysis, typed IR, verification, deterministic C++ generation, build manifests, and CLI orchestration.
- C++20 owns EE architectural state, memory/device models, the generated-code ABI, and shared platform/runtime services.
- Generated source plus a deterministic machine-readable manifest is the frontend/runtime boundary. Do not add per-instruction Rust/C++ FFI.
- Title-specific metadata and narrowly evidenced compatibility profiles may vary by title; instruction/device semantics and core recompiler behavior stay shared.

Read `docs/PRODUCT_VISION.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/RELEASE_GATES.md`, and the relevant ADRs before changing architecture or acceptance gates.

## Evidence and reverse engineering

Keep four categories separate in code review and documentation:
1. **Observed:** exact trace, bytes, address, digest, provider output, or test result.
2. **Derived:** a deterministic conclusion calculated from observations.
3. **Inferred:** a hypothesis supported by observations but not yet directly verified.
4. **Unknown:** facts the available sources do not establish.

Every non-obvious guest-visible behavior must have a reproducible evidence trail in `docs/RESEARCH.md` and an ADR when it affects an architectural or runtime contract. Record artifact SHA-256, target revision, REPS2 commit, load address/coordinate system, provider/language/profile, exact instruction words, and test setup where relevant. A Ghidra/REA address is not automatically a runtime address; reconcile ELF file offset, ELF virtual address, guest runtime address, and relocated address explicitly.

Do not guess indirect control flow, kernel service behavior, device reset values, DMA events, interrupt state, or guest-visible return values. Fail closed with an explicit status when evidence is insufficient. Do not turn an unsupported operation into a zero read, no-op write, successful syscall, or fake boot stage merely to advance a trace.

Use REA only when target-binary evidence, decompilation, xrefs, or comparison with shipped behavior can answer a specific question. REA is an analysis workbench, not an emulator or PS2 runtime. Follow `docs/REA_WORKFLOW.md`; check provider/format admission before attempting a deep MIPS analysis and preserve the complete Evidence record or exported bundle. Never alter an ELF header or flags simply to bypass provider admission.

## Legal and repository hygiene

- Analyze only owner-supplied or otherwise lawfully authorized game material.
- Keep ISO images, BOOT.ELF/title ELF files, BIOS/firmware, proprietary SDK binaries, extracted assets, and copyrighted pseudocode/decompiler dumps outside Git.
- Never download, add, or redistribute commercial game data or proprietary firmware as fixtures.
- Tests use small synthetic instruction words, synthetic memory, and clearly identified fixtures only. Document fixture provenance.
- Commit hashes, public references, derived metadata, and short independently written behavioral summaries when appropriate; do not commit source artifacts or copied third-party implementation code.
- Preserve licenses and attribution. Use external emulator code as a behavioral cross-check only; do not copy/adapt implementation code.

## Engineering workflow

- Start with current `origin/main`, inspect the latest commits, open PRs, worktrees/branches visible to the environment, and relevant roadmap/ADR state. Do not overwrite uncommitted owner work.
- Create a dedicated feature branch from the exact main SHA. Keep one cohesive, reviewable vertical slice per PR.
- Parallelize independent research and read-only audits. Do not let multiple agents edit the same file or worktree concurrently; assign one owner per write set and integrate sequentially.
- Before changing semantics, inspect the producer, all consumers, tests, and relevant sibling pathways. Prefer a correction at the owning abstraction over downstream special cases.
- Add exact semantic tests for normal, boundary, aliasing, failure, and no-partial-mutation behavior as applicable. Include an end-to-end generated-code smoke test when the behavior is consumed by generated code.
- Run `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`, and the CMake/CTest matrix described in `CONTRIBUTING.md`. On Windows, retain MSVC/Release evidence; do not treat host-only GCC results as a Windows pass.
- Inspect the full diff, ensure documentation agrees with implementation, verify CI and review feedback, then create/update the PR. Never merge with failing, missing-required, or materially incomplete checks. Do not state that a test passed unless its output or a successful CI run proves it.
- Do not delete branches, worktrees, or owner artifacts as cleanup unless ownership and merge status are verified.

## Compatibility and release claims

Keep these states distinct: Imported, Analyzed, Translated, Native-built, Boot-tested, and Playable. Parser success, IR coverage, generated C++, successful linking, a synthetic smoke test, or a bounded native probe do not prove that a title boots. A Boot-tested claim requires an exact real-title digest, REPS2 revision, runtime configuration, repeatable trace, and a guest-visible stage. Playable additionally requires a documented representative gameplay plan covering rendering, input, audio, session stability, and saves.

Never check off a roadmap gate based only on a plausible implementation or a synthetic test when the gate requires real-title evidence. Keep unknowns and limitations visible.

## Agent responsibilities

When a multi-agent team is available, use distinct responsibility boundaries:
- **Principal Architect / Lead:** owns scope, interfaces, integration, evidence ledger, and final decision; does not delegate final acceptance.
- **Reverse-Engineering Analyst:** inspects only authorized local binaries, records addresses/xrefs/control flow and evidence provenance; does not modify runtime semantics.
- **Compiler / IR Engineer:** owns decoding, effects, CFG/IR, verification and deterministic code generation.
- **EE Runtime / Device Engineer:** owns architectural state, memory, MMIO, HLE, DMA/SIF and shared runtime contracts.
- **Independent QA / Reviewer:** independently derives expected results, adds negative/boundary tests, checks documentation and runs validation; does not rubber-stamp the implementation.

Agents may recommend a next milestone, but the lead must reconcile evidence and ensure all tests/claims remain honest. A task is not complete merely because an agent has stopped or opened a PR.
