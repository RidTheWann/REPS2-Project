# REPS2 architecture

## Design objective

REPS2 is a game-independent static recompilation toolchain, not a claim that every PS2 subsystem must be fully reproduced before the first executable can be translated. The implementation should grow through vertical slices: select a real executable, identify the exact CPU/runtime behaviors it needs, implement those behaviors behind stable interfaces, then expand coverage.

The implementation must remain independent of PCSX2 and other recompiler projects. Public specifications, test observations, and differential results may inform behavior; upstream implementation code must not be copied.

## Planned pipeline

    Owned disc image (local only)
            |
            v
    Disc / filesystem inspection
            |
            v
    ELF loader ----> symbols / relocations / load map
            |
            v
    EE code discovery <---- targeted dynamic exploration
            |
            v
    R5900 decoder ----> instruction semantics + delay slots
            |
            v
    Typed IR / basic blocks / CFG
            |
            v
    IR validation + optimization
            |
            v
    C++ AOT backend ----> host C++ compiler / linker
            |
            v
    Native game module
            |
            v
    REPS2 runtime interfaces (EE, VU, GS, IOP, DMA, audio, input, storage)

## Module boundaries

### disc

Read-only inspection of supported optical-disc filesystem structures. It must not modify user images. Multi-extent and unusual layout handling must be explicit and tested.

### elf

Parse ELF32 little-endian headers, program headers, section headers, symbols, and relocations. Validate every file-backed range and every size calculation before access. Preserve virtual addresses and distinguish file size from memory size (including zero-filled tails).

### analysis

Discover executable regions, known entry points, direct branch/jump targets, function boundaries, and unresolved control-flow edges. Static analysis must not assume every byte in an executable segment is code. Unknown or indirect targets remain recorded as unresolved until evidence is available.

### ee

Model R5900 architectural state and instruction semantics. GPR width behavior, HI/LO, COP0, FPU, MMI, exceptions, alignment, and delay-slot execution need explicit specifications and tests. Host C++ undefined behavior must never be used to approximate guest wraparound or overflow semantics.

### ir

Represent guest operations independently of the host backend. IR nodes carry source guest PC and side-effect/exception information. Control-flow blocks must preserve delay-slot semantics and observable ordering.

### backend-cpp

Lower validated IR into deterministic C++20. Generated code should use explicit-width types and helper operations for guest semantics. No host pointer punning or signed-overflow assumptions.

### runtime

Expose versioned interfaces for memory, syscalls, interrupts, DMA, timers, VU dispatch, GS submissions, IOP services, audio, input, and save devices. A target may begin with a narrow service profile, but every stub/fallback must be logged and measurable.

### validation

Keep synthetic unit tests, instruction semantic tests, generated-code tests, ELF corpus metadata (not binaries), differential tests, and game-stage regression separate. Never call a game playable based only on successful ELF parsing or generated-code compilation.

## Initial implementation in v0.1.0

- Defensive ELF32 little-endian header and table parser.
- Base-MIPS decoder subset with explicit unsupported-instruction reporting.
- Delay-slot metadata and direct branch/jump target calculation for the supported subset.
- reps2 elf-info, reps2 decode, and reps2 version commands.
- Synthetic tests plus Windows/Linux CI.

## Correctness invariants

1. Guest arithmetic width and signedness are explicit.
2. All guest addresses remain fixed-width guest addresses until mapped by a runtime abstraction.
3. A branch's delay-slot instruction is preserved and executed according to the guest ISA; it must not be treated as an ordinary fall-through instruction without CFG evidence.
4. An unsupported opcode never silently becomes a NOP.
5. ELF and ISO parsers reject malformed ranges and integer-overflow cases.
6. Reproducible builds never depend on a developer's local game or BIOS files.
