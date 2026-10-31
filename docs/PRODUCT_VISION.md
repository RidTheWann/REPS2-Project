# REPS2 product vision

## Product definition

REPS2 is intended to become a **Windows PC platform for native recompilation and execution of multiple PlayStation 2 games**. Its end-user experience should be controller-first and closer to Steam Big Picture than to a developer command-line utility.

The core promise is: **one owned ISO is the user's only required game input; one shared REPS2 platform handles many PS2 titles.** For a normal user, REPS2 automatically identifies the disc and boot executable, extracts internally, analyzes and recompiles supported guest code, builds and registers a native title module, and presents the title in a library with a truthful status. The user then selects Play.

1. The user selects an owned PS2 ISO.
2. REPS2 identifies the disc and boot executable automatically.
3. REPS2 analyzes and statically recompiles supported guest code.
4. REPS2 links the generated native module with its shared PS2 runtime.
5. The game appears in a persistent library and can be launched from the UI when its compatibility state permits it.
6. Subsequent launches reuse only validated per-game build artifacts whose full cache identity matches the source and toolchain.

The intended user input is the ISO. Users should not need to manually extract BOOT.ELF, prepare a game-specific directory, run developer CLI commands, or understand ELF, IR, compiler, or runtime details.

## Multi-game is a platform property

REPS2 must not become one hand-written recompiler per game. The core disc reader, ELF loader, R5900 decoder, analysis, typed IR, C++ backend, build system, runtime ABI, and shared host services should be shared across titles.

Per-game records hold only data that is genuinely title-specific: disc identity, boot executable identity, analysis coverage, unresolved behavior, required runtime-service profile, build artifacts, compatibility evidence, settings, saves, and measured validation results. A game-specific workaround must be isolated, documented, and tested; it must not silently fork instruction semantics or shared runtime behavior.

A single real game can be the first vertical-slice target, but it is a test target, not the architecture. The multi-game premise must be demonstrated with at least two genuinely different PS2 title identities/revisions reaching independently repeatable native boot-tested stages through the same shared analysis, code-generation, and runtime path. The longer-term compatibility catalog should grow across titles, revisions, and hardware feature profiles. A stable multi-game release targets at least two independently validated Playable titles.

## End-to-end product pipeline

```
One owned ISO
  -> content fingerprint
  -> ISO 9660 / SYSTEM.CNF discovery
  -> internal boot ELF extraction and validation
  -> EE load-map construction
  -> R5900 function discovery and CFG recovery
  -> typed IR and semantic verification
  -> deterministic native-code generation
  -> host C++ compile and link
  -> shared PS2 runtime + title module
  -> game session
  -> controller-first Windows launcher
```

The launcher is the final product surface, not the engine. The engine must expose stable library, build, and session APIs plus structured state transitions so the UI can display import, analysis, translation, build, launch, crash, and compatibility states without interpreting log text.

## PS2 runtime requirements

Static recompilation translates CPU code; it does not turn the PS2 hardware environment into a normal PC process. Recompiled code still expects the machine state and services it used on the console. The runtime therefore needs explicit, independently testable subsystem boundaries for:

- **Emotion Engine core:** R5900 integer/MMI/FPU/COP0 behavior, 128-bit GPR semantics, exceptions, interrupts, address translation, timers, and main/scratchpad memory.
- **Vector Units:** VU0 macro mode, VU0/VU1 microprogram execution, VIF command processing, vector memory, and synchronization.
- **Graphics path:** DMA channels, SIF/SIF RPC, GIF packet processing, GS registers/state, local GS memory, rendering, and presentation.
- **I/O system:** IOP/R3000A execution, IOP RAM, IRX module loading, CD/DVD services, controllers, memory cards, and SPU2 audio.
- **Host integration:** windowing, input mapping, audio output, filesystem-backed optical media, save storage, diagnostics, and clean shutdown.

These boundaries must preserve observable guest behavior. Returning success for an unimplemented device operation is not acceptable. Unsupported behavior should produce a precise compatibility diagnostic or controlled guest-visible failure.

The implementation order is evidence-driven. A first vertical slice should implement only the services required by its observed execution path; broader EE, VU, IOP, DMA/SIF, GS, audio, media, and input coverage expands as real titles demand it. The project will use independently developed implementations and public behavioral evidence; it will not copy third-party emulator or SDK source.

## BIOS and user-owned content

The desired user experience should not require users to locate or install a separate proprietary BIOS image. REPS2 should implement the necessary boot/kernel-facing behavior through clean-room high-level emulation where technically and legally appropriate. It must not distribute Sony firmware, proprietary SDKs, game executables, or game assets.

The original ISO remains user-owned. The current library backend stores its canonical source path instead of copying a multi-gigabyte image; if the file is moved, the UI must offer an explicit relink operation and must never bind the record to a different disc silently.

## Game identity and build-cache correctness

An ISO content digest identifies an exact input image. A native build cache must use more than the title serial: the cache key must include at least the ISO digest, REPS2 frontend/compiler version, IR/manifest schema, runtime ABI version, host target, compiler identity, and relevant build configuration. A same-serial revision or modified disc image must not reuse stale generated code.

The per-game manifest must distinguish these states:

- **Imported:** disc and boot ELF validated and stored.
- **Analyzed:** executable ranges and CFG coverage recorded.
- **Translated:** a documented subset has typed IR/codegen output.
- **Native-built:** generated code has been compiled and linked with a specific shared runtime ABI.
- **Boot-tested:** the title reaches a precisely documented, repeatable guest-visible boot stage.
- **Playable:** a documented representative gameplay acceptance plan passes, covering relevant rendering, controls, audio, session stability, and save/load behavior; untested areas and known limitations remain disclosed.

A previous stage never implies a later one. In particular, parsing an ISO, generating C++, compiling a host executable, or reaching a boot logo/menu does not alone mean a title is Playable.

## Desktop experience

The future launcher should support a game grid/list, controller-first navigation, search and filtering, per-game status and settings, play/stop, logs/crash reports, save paths, and a straightforward Add Game workflow. The normal Add Game workflow accepts one ISO and orchestrates the pipeline; it must not ask users to extract files or use internal developer tools. Metadata and cover art should be optional enrichment; library operation must not depend on scraping services or network access.

The desktop shell should call the same library/build/session API as the CLI. This ensures automated tests can exercise the exact path the UI uses and avoids implementing a parallel GUI-only game pipeline.

## Development sequence

1. Reproducible foundation, managed one-ISO import, and stable per-game manifest.
2. Complete executable analysis and broad R5900/MIPS EE semantic coverage.
3. Typed IR, deterministic native code generation, and compiler validation.
4. The first real-title native execution slice with traceable runtime-service requirements.
5. Shared EE, VU, IOP, DMA/SIF, GS, audio, media, and input services expanded by observed title needs.
6. Multi-title compatibility proof: at least two different titles boot-tested through the same shared toolchain/runtime; build evidence, profiling, and regression automation.
7. Controller-first Windows desktop platform, integrated one-ISO workflow, packaging, settings, diagnostics, and stable releases. The stable multi-game release target is at least two titles independently validated as Playable.

This order intentionally prioritizes a genuine end-to-end native execution path and a multi-title proof before presenting a polished launcher as though it were already a functioning PS2 platform. UI prototyping may proceed in parallel, but it cannot replace runtime and compatibility gates.

## Compatibility and evidence policy

Every compatibility claim must be tied to a disc digest/revision, REPS2 revision, runtime configuration, host platform, and repeatable test evidence. Synthetic fixtures validate isolated semantics; they do not establish compatibility with a retail title.

The minimum multi-game proof requires two distinct titles to reach independently repeatable native boot-tested stages through the same shared frontend, native backend, and runtime ABI. A stable multi-game release targets at least two titles marked Playable against explicit representative gameplay test plans. A compatibility record must preserve the distinction between unsupported instructions, unresolved dynamic control flow, missing runtime services, and observed title-specific defects. Imported, analyzed, translated, native-built, boot-tested, and playable must remain separate states.
