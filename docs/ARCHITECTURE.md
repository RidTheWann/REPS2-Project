# REPS2 architecture

## Design objective

REPS2 is a game-independent static recompilation toolchain. It must not require complete reverse engineering of all PS2 hardware before the first real executable can be translated. Development proceeds through vertical slices: choose an executable, discover the exact code and runtime behaviors it needs, implement those behaviors behind stable interfaces, then expand coverage.

## Language boundaries

### Rust frontend

Rust owns:
- Disc and ELF parsing.
- Program/section mapping, symbols, relocations, and executable metadata.
- EE instruction decoding and control-flow discovery.
- Function/basic-block construction and typed IR.
- IR verification and optimization.
- Deterministic C++20 source and build-manifest generation.
- CLI orchestration, diagnostics, and frontend tests.

This is the correctness-critical input and transformation pipeline. Rust's checked indexing, explicit Result propagation, enums, and exhaustive pattern matching are used to make invalid input and unsupported operations explicit.

### C++20 runtime

C++20 owns the native runtime and platform-facing subsystem implementations:
- EE state and memory interfaces.
- VU0/VU1 execution interfaces.
- GS submission and rendering integration.
- IOP, DMA, SPU2/audio, controller, optical media, and memory-card services.
- Generated-code helper ABI and host integration.

The current runtime code is only a small EE GPR state/helper ABI used by the first generated-code integration test. It is not a functional EE emulator or complete PS2 runtime.

### Boundary

The primary boundary is generated source plus a machine-readable manifest, not per-instruction FFI calls. The Rust frontend emits C++ and metadata; the host C++ compiler compiles that output and links it with the C++ runtime. This keeps complex Rust IR ownership inside Rust and avoids passing IR graphs or vectors across a language ABI.

A narrow C ABI or CXX bridge may be introduced later only when a measured use case requires in-process calls. Such a bridge must use coarse-grained operations and stable POD/opaque handles; it must not cross the boundary for every guest instruction.

CMake orchestrates the build, Cargo builds/tests the Rust frontend, and CMake's C++20 toolchain compiles generated code and runtime components. Corrosion is available for importing Rust executables or libraries into CMake; it is not required merely to invoke a standalone Rust frontend.

## Pipeline

    Owned disc image (local only)
            |
            v
    Disc / filesystem inspection
            |
            v
    ELF loader ----> sections / symbols / relocations / load map
            |
            v
    R5900 decoder ----> function discovery / CFG / delay slots
            |
            v
    Typed IR ----> verifier ----> optimization
            |
            v
    C++20 AOT backend ----> generated C++ + build manifest
            |
            v
    Host C++ compiler / linker
            |
            v
    Native game module + REPS2 C++20 runtime
            |
            v
    Runtime profiles: EE / VU / GS / IOP / DMA / audio / input / storage

## Product-facing game library

The user-facing workflow is **select one ISO → add game → later press Play**. Users are not expected to extract BOOT.ELF, create title-specific folders, or invoke developer-only compiler steps. The Rust frontend owns the import transaction and records a stable, versioned manifest that a future desktop library UI can consume.

Each imported image receives a content-addressed identity derived from a streaming SHA-256 digest. The managed layout is:

    <library>/
      games/
        sha256-<iso-digest>/
          game.json
          boot.elf
          recompiled.cpp       (optional)
          ee-main-ram.bin      (optional)

The ISO itself remains at its canonical source path to avoid duplicating multi-gigabyte images. A future UI must detect a missing source and provide a relink flow; it must not silently substitute another image. Re-importing unchanged content is idempotent and validates the stored boot ELF before returning the existing record. Staging and atomic directory publication prevent partially written records from appearing in the library.

The manifest separates discovery facts from execution claims. It records disc/ELF identity, loadable ranges, analysis coverage, unresolved edges, and optional generated artifacts. The current command always reports `playable: false`: parser success, a valid PT_LOAD image, and generated C++ are not evidence that the game can boot. This distinction is a required invariant for the launcher and compatibility catalog.

The current import command defaults to the per-user application-data library (Windows: `%LOCALAPPDATA%\\REPS2\\Library`; Linux: `$XDG_DATA_HOME/reps2/Library` or `$HOME/.local/share/reps2/Library`). Set `REPS2_LIBRARY_DIR` or use the optional `--library` argument to override the path. A normal user supplies only the ISO path.\n\nThe current import command is:

    reps2 add-game <disc-image.iso> [--library <directory>]

This is the first library/backend slice, not yet the desktop shell, a complete compiler, or a game launcher.

## Module invariants

### ELF and disc readers

Validate every offset, size, table count, cross-table index, and arithmetic operation before access or allocation. Distinguish file size from memory size and zero-fill BSS tails. Extended ELF numbering and uncommon PS2-specific ABI cases must remain explicit until supported and tested.

The optical-disc reader is a generic `Read + Seek` consumer and never buffers the whole image. Its current supported container is a raw ISO 9660 image with 2048-byte logical sectors. It scans the descriptor sequence from logical sector 16, validates `CD001`/version fields and the terminator, requires one single-volume Primary Volume Descriptor, and bounds every extent against both the declared volume and actual image length. Directory parsing honors logical-block padding and record boundaries, limits directory size/count/path depth, rejects parent traversal, and refuses unsupported secondary-volume, multi-extent, interleaved, or reserved descriptor cases when encountered. File reads are separately capped. SYSTEM.CNF parsing resolves `BOOT2` first and `BOOT` as a compatibility fallback; only `cdrom0:`/`cdrom:` paths are accepted for this optical-image workflow. Boot extraction validates the target as little-endian ELF32 EM_MIPS ET_EXEC and creates a new output file without opening the source image for writing.

The reader deliberately does not claim BIN/CUE raw-track sector deinterleaving, CHD, UDF, Joliet, Rock Ridge, path-table acceleration, multi-volume spanning, multi-extent concatenation, interleaved file sections, HDD/PFS boot targets, CDVD subchannel handling, or optical-drive timing/command emulation.

### EE decoder and analysis

Decoding is pure and deterministic. Every input word produces a classified instruction or an explicit unsupported result. Static analysis must not assume every byte in an executable segment is code.

The initial analyzer is reachability-guided. Roots come from the ELF entry point and executable STT_FUNC symbols. It scans file-backed bytes in executable PT_LOAD segments, constructs basic blocks, records direct conditional taken/fallthrough edges, direct jump/call edges, return continuations, and unresolved indirect targets. Control instructions and their delay-slot instructions remain explicit in terminators; they are not flattened into ordinary instruction sequences.

The analyzer rejects targets that enter another instruction's delay slot and currently rejects control transfers or unsupported encodings in delay slots. It does not infer dynamic jump targets from data flow, and its CFG is not yet lowered to executable branch IR. It uses bounded instruction/block limits and rejects inconsistent externally constructed segment metadata.

### IR

The scalar IR represents a verified straight-line sequence with sequential guest PCs. Its operations are typed variants, not embedded C++ strings. The first subset models word arithmetic, 64-bit logical operations, immediate operations, LUI, and word shifts.

The CFG-aware ControlProgram composes those scalar operations into basic blocks and adds typed terminators for fallthrough, conditional branches, direct jumps/calls, indirect jumps, returns, unsupported instructions, and segment ends. Delay-slot operations remain separate scalar IR nodes. Verification checks block layout, terminator PC, delay-slot PC+4, entry roots, and register indices. Branches are never flattened into a linear instruction sequence.

IR nodes retain guest PC and side-effect information. Word arithmetic restrictions, overflow outcomes, GPR[0], low/high GPR lanes, and unsupported behavior are explicit. Unsupported scalar operations become explicit stop terminators where safe; unsupported delay-slot semantics fail the lift instead of being approximated.

### AOT backend

Generated code uses explicit-width types and helper functions for guest arithmetic and architectural state. It must not rely on host signed-overflow behavior, pointer punning, or undefined behavior. Code generation is deterministic for the same IR. Generated scalar operations update GPR[63:0] while preserving the upper 64-bit lane.

The first CFG backend emits a PC-based dispatcher over translated basic blocks. It evaluates branch predicates before link/delay-slot side effects, writes branch-link PC+8 independently of the predicate, captures indirect targets before potentially aliased link writes, runs the delay slot, then commits the next PC. The runtime state carries the current 32-bit guest PC. A finite guest-instruction budget prevents an emitted loop from running without a bound; the dispatcher admits complete basic blocks atomically. Dynamic or out-of-graph targets return external_control_flow, and unsupported instructions return unsupported_instruction.

### Runtime

Runtime interfaces are versioned and narrow. Every unimplemented system service returns a structured unsupported result or controlled guest-visible failure; it must not return fake success.

The first EE memory slice uses a caller-owned `std::span<std::uint8_t>` for 32 MiB of main RAM. It supports the documented base, uncached, accelerated, KSEG0 and KSEG1 aliases, all resolving to one backing store. Byte/halfword/word/doubleword helpers assemble little-endian values explicitly, validate bounds and natural alignment, and never use unaligned typed pointer dereferences. LB/LH/LW sign-extend into the low 64-bit GPR lane; LBU/LHU/LWU zero-extend; LD writes the full low 64-bit lane while preserving the upper 64-bit lane; SB/SH/SW/SD write only their respective low bits. LD/SD require 8-byte effective-address alignment. LWL/LWR and LDL/LDR align the checked backing read down to the containing 4-byte/8-byte unit and merge only the selected byte lanes with the prior destination. SWL/SWR and SDL/SDR read the old aligned unit, replace only the selected bytes, and write it back through the checked helper; these opcodes do not issue an unaligned host access. LWL sign-extends its merged 32-bit result into GPR[63:0]; LWR sign-extends at byte offset zero and otherwise preserves GPR[63:32]. The upper 64-bit GPR lane is preserved by all word/doubleword partial loads. LQ/SQ transfer both 64-bit lanes of a 128-bit GPR using explicit little-endian byte assembly; their effective addresses are silently rounded down to a 16-byte boundary, matching the EE-specific behavior, rather than raising an alignment fault. Memory helper failures are distinct: address_error, unmapped_memory, and unsupported_memory_access.

This is not a full virtual-memory implementation. COP0/TLB mappings, cache behavior, exceptions/EXL state, bus error delivery, scratchpad storage, BIOS reads, IOP RAM, VU memories, GS/IPU/timers/DMA/SIF registers and all other MMIO are not implemented. Known device/reserved regions are blocked explicitly rather than aliased to main RAM. The host must attach a valid main-RAM span before executing generated code that uses these instructions.

The ELF image stage converts an eligible static MIPS ET_EXEC into a 32 MiB physical main-RAM snapshot. Each PT_LOAD segment is translated through the supported direct/uncached/accelerated/KSEG aliases; zero-filled BSS tails are preserved. Segment ranges that overlap after alias translation, cross an alias boundary, or touch unsupported regions are rejected. Generated C++ contains an entry-specific initializer which validates the supplied snapshot and backing span, copies the bytes, resets GPR state and assigns the ELF entry PC. The command writes the snapshot beside the C++ file as a .ram.bin file. It rejects ET_DYN/relocatable images and allocated REL/RELA relocations: no relocation is silently skipped or guessed. This image loader is still not a full PS2 ELF loader; it does not model TLB/COP0, exception state, cache, expansion RAM, or device initialization.

### Validation

Synthetic Rust tests, generated-code compilation/execution, instruction semantic tests, differential checks, and game-stage regression are separate layers. Parsing a real ELF or compiling a generated translation unit is not evidence that a game boots or is playable.

## Current implementation

- Rust read-only ISO 9660 reader for raw 2048-byte-sector images with SYSTEM.CNF boot-target resolution and bounded file extraction.
- Rust ELF32 little-endian parser with section names, symbol tables, and REL/RELA metadata.
- Bounded PT_LOAD materialization with zero-filled tails and overlap detection.
- Initial base-MIPS decoder subset, including selected REGIMM branches.
- Reachability-guided CFG discovery with explicit branch/delay-slot terminators and unresolved indirect-target reporting.
- Executable instruction reads restricted to file-backed PT_LOAD bytes, with BSS kept non-executable by default.
- Typed linear IR for a bounded scalar instruction subset.
- Deterministic scalar C++20 emitter and CFG-aware C++20 dispatcher with a guest PC and structured stop statuses.
- Synthetic generated control-flow code compiled and executed against the C++20 runtime on Windows/MSVC and Linux/GCC.
- Typed LB/LBU/LH/LHU/LW/LWU and SB/SH/SW IR with signed-offset effective-address generation.
- Typed LWL/LWR/SWL/SWR and LDL/LDR/SDL/SDR IR with checked aligned-unit merge helpers and preserved unaffected GPR lanes.
- Typed DADDU/DSUBU/DADDIU and immediate DSLL/DSRL/DSRA plus DSLL32/DSRL32/DSRA32 lowering into the low 64-bit EE GPR lane; arithmetic right shift uses an explicit sign-fill helper.
- Typed register-variable SLLV/SRLV/SRAV and DSLLV/DSRLV/DSRAV lowering with architectural 5-bit/6-bit count masks, sign-extended word writes, and preserved upper GPR lanes.

- Checked little-endian EE main-RAM accesses over a caller-owned 32 MiB backing span with documented aliases and explicit MMIO/unmapped failures.
- Default-map ET_EXEC PT_LOAD image construction with BSS zeroing, physical alias overlap rejection, and allocated-relocation refusal.
- Generated ELF-entry RAM initializer, validated against synthetic 32 MiB image snapshots on Windows/MSVC and Linux/GCC.
- Generated memory code executed in CI for signed/unsigned extension, write widths, all implemented RAM aliases, address alignment, unaligned byte-lane merges, unsupported devices and backing-span bounds.
- CMake-generated C++ smoke test executed on Windows and Linux CI.

No complete R5900 semantic engine, full game ELF AOT translation, memory-operation lowering, or PS2 runtime is claimed yet.
