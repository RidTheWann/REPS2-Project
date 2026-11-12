# REA Workflow for REPS2

## Purpose and boundary

REA is an agent-facing reverse-engineering workbench exposed through MCP and CLI. It can help inspect native binaries, identify functions, decode instructions, follow references, produce pseudocode, and preserve structured Evidence. REA does **not** execute PS2 guest code, emulate the EE/IOP, provide a PS2 BIOS, determine device reset state, or replace the REPS2 recompiler/runtime.

Use REA to answer one bounded question at a time: identify the routine containing an observed guest PC, recover direct callers/callees, inspect relevant load/store sequences, validate structure offsets, or compare independently observed behavior. Keep REPS2 source/repository architecture analysis in normal GitHub/repository tools; do not launch a binary provider for a question answered by the source tree.

## Current target and immutable identity

The current real-title vertical-slice target is Downhill Domination USA (SCUS_971.77), subject to the owner's lawful possession and authorization.

- Recorded boot ELF SHA-256: `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`
- Recorded ISO SHA-256: `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`
- Last recorded bounded probe: PC `0x0025C274`, after `3,168,708` guest instructions; stop `unsupported_memory_access` on `D_STAT` at `0x1000E010`.
- The guest tests/clears SIF0 status bit `0x20` on that path. This does not prove that the bit is set at entry, that the SIF/IOP channel is active, or that the full boot sequence is understood.

These hashes are repository evidence, not permission to acquire or redistribute the title. Before analysis, independently hash the local input and compare it with the recorded identity. Do not silently substitute another region/revision or a similarly named file.

## Windows-first local preparation

The user-facing environment is Windows. Find the actual ISO filename instead of assuming one:

```powershell
Get-ChildItem 'D:\List Games' -Filter '*.iso' -File |
    Select-Object FullName, Length
```

Use the REPS2 CLI built from the checked-out repository (or its already installed executable):

```powershell
reps2 disc-info '<absolute-path-to-owned-iso>'
Get-FileHash '<absolute-path-to-owned-iso>' -Algorithm SHA256
```

Compare the digest to the expected title/revision before proceeding. Extract the boot ELF to a location outside the Git working tree, for example a dedicated evidence directory under the user's local application-data directory:

```powershell
New-Item -ItemType Directory -Force "$env:LOCALAPPDATA\REPS2\Evidence" | Out-Null
reps2 extract-boot-elf '<absolute-path-to-owned-iso>' "$env:LOCALAPPDATA\REPS2\Evidence\SCUS_971.77.elf"
Get-FileHash "$env:LOCALAPPDATA\REPS2\Evidence\SCUS_971.77.elf" -Algorithm SHA256
```

The expected ELF digest is listed above. If a different digest is found, stop treating it as the current Downhill Domination evidence target: record the new identity and begin a separate title/revision investigation. Never place the ISO or extracted ELF in the REPS2 repository. The alternative `reps2 add-game <iso>` uses the content-addressed local game library; inspect its manifest and stored ELF identity before reusing it.

## REA provider admission: do not bypass it

Before a deep provider call, inspect REA's installed version and readiness once:

```powershell
rea --version
rea doctor --json
```

Use the exact tool names/schemas advertised by the connected REA MCP server if using MCP. Do not repeatedly run setup or provider discovery when a prior result already answers readiness.

**Important current compatibility constraint:** REA's generic Ghidra MIPS lane is not a general R5900/Emotion Engine profile. Its documented standard lane restricts accepted ELF32 inputs to specific generic MIPS ABI/ISA declarations and refuses machine-specific or ASE flags. REPS2 records the PS2 R5900 CPU marker as `0x92` in the MIPS CPU subtype field; the actual ELF header/ABI declarations must be inspected, not assumed. The lane also does not establish R5900 MMI, EE-specific 64/128-bit register, VU, or device semantics.

REA's documented Windows Ghidra P0 lane does not admit MIPS. Its generic MIPS Ghidra analysis lane is documented for Linux/macOS. WSL may provide a Linux host for Ghidra where an existing, supported installation is available, but it does **not** remove the MIPS ELF header/profile restrictions. Do not claim WSL solves R5900 admission.

If REA refuses the ELF:
1. Preserve the exact refusal and its declared constraint in the evidence notes.
2. Do not patch `e_flags`, byte order, ELF class, ABI tags, or machine metadata to trick the provider.
3. Do not reinterpret the refused file as a supported generic MIPS image.
4. Continue useful REPS2-native inspection (ELF header, section/program headers, symbol/relocation tables, raw bytes, static decoder census and bounded trace).
5. Record REA analysis as unavailable for this artifact/profile, not as an empty function inventory.

Official capability references:
- [REA MIPS through Ghidra](https://github.com/morluto/rea/blob/main/docs/ghidra-mips.md)
- [REA Windows Ghidra P0](https://github.com/morluto/rea/blob/main/docs/windows-ghidra-p0.md)
- [REA native investigation/provider boundaries](https://github.com/morluto/rea/blob/main/docs/native-investigation.md)
- [REA MCP Evidence contracts](https://github.com/morluto/rea/blob/main/docs/mcp-contracts.md)

## Bounded analysis workflow

Only after the installed REA version advertises an applicable provider and the exact target is admitted, use focused analysis. Example CLI calls for an admitted native ELF:

```bash
rea inspect /absolute/path/to/SCUS_971.77.elf --provider ghidra --json
rea analyze /absolute/path/to/SCUS_971.77.elf --provider ghidra --json
rea function /absolute/path/to/SCUS_971.77.elf --procedure=0x0025C274 --provider ghidra --json
rea instructions /absolute/path/to/SCUS_971.77.elf --procedure=0x0025C274 --provider ghidra --json
rea decompile /absolute/path/to/SCUS_971.77.elf --procedure=0x0025C274 --provider ghidra --json
```

These are examples, not a guarantee that the current artifact is admitted. Confirm command help and provider availability for the installed REA version. A PC in a trace may be a runtime/relocated address; first map it back to the ELF image or an explicit copied-code region. Do not feed an unmapped PC to a provider and assume the result names the correct routine.

Prefer the minimum sufficient sequence:
1. Inspect ELF identity, byte order, machine, ABI/e_flags, segments, sections, symbols, relocations and entry point.
2. Locate the function containing the exact guest PC and inspect its complete body, direct xrefs/callers/callees, instructions and pseudocode.
3. Follow only the relevant calls and data references that explain the observed MMIO access or SIF0 state.
4. Verify every structure field, register offset and write behavior against an independent public reference or an explicit PS2SDK source location.
5. Compare static findings with the exact REPS2 native trace. Static code establishes possible paths, not which path ran or what hardware state was present.

For MCP, use the connected server's actual tool catalog: open the exact local binary, list procedures, analyze the selected function, inspect instructions/references, and export the complete Evidence bundle before switching/closing the target. Preserve `evidence_id`, artifact SHA-256, provider/profile identity, coverage, omissions and limitations. Do not copy a short summary in place of the complete retained Evidence when later operations require the parent record.

## Evidence questions for the current D_STAT boundary

Answer these in order; do not jump to writing a fake initial value:

1. What ELF routine contains `0x0025C274` after reconciling load address and file coordinates?
2. What exact instructions follow the read at `0x1000E010`, including delay slots, branches and the first call/service after the clear?
3. Which paths can set SIF0 channel-5 status bit `0x20` before this read? Identify the corresponding SIF0 CHCR at `0x1000C000`, STR bit `0x100`, and relevant SIF/IOP setup calls.
4. Does the target itself initialize, enable, start, stop, or poll the SIF channel before reading D_STAT? What evidence distinguishes a real pending completion from an initial/stale condition?
5. Which D_STAT fields are read or written by the whole relevant function and its callers? Keep status bits, interrupt masks, and other status fields distinct.
6. Which later device interactions are necessary for the execution to advance? Do not infer that clearing one bit completes DMA or supplies an IOP response.

Cross-check public behavior without copying implementation:
- PS2Tek EE register map: [ps2tek](https://israpps.github.io/ps2tek/)
- PS2SDK SIF command initialization and `DMAC_COMM_STAT` / `STAT_SIF0`: [sifcmd.c](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/sifcmd.c)
- PCSX2 legacy DMAC field-layout cross-check (non-normative): [Dmac.h](https://sources.debian.org/src/pcsx2/1.6.0%2Bdfsg-2/pcsx2/Dmac.h)

A source-level name or register definition is not proof of the target's runtime device state. A D_STAT field model is not a SIF DMA engine; implement channel state/event generation only when the next target trace and independent references establish its contract.

## Durable handoff and repository policy

Store raw REA JSON/Evidence bundles, extracted ELF, logs, and other target material outside Git, under a local evidence folder. The checked-in repository may contain:
- exact SHA-256 digests and title/region identity;
- addresses, instruction words, small independently written observations;
- public source/document references and page/section identifiers;
- REA Evidence IDs only when they are meaningful in the retained/exported bundle;
- sanitized trace summaries with explicit coverage and unknowns.

Do not commit ISO/ELF binaries, firmware, title assets, large raw pseudocode/decompiler output, secrets, local absolute paths, or unlicensed material. A later agent must be able to see what is observed, inferred, and unknown without relying on an in-memory REA Evidence ID from an expired MCP session.

## Completion criteria

This investigation slice is complete only when:
- the local image and ELF identities are verified;
- provider admission or refusal is recorded exactly;
- the target routine, branch/call path and relevant state dependencies are evidenced;
- D_STAT read/write behavior and the SIF0 state producer are independently corroborated;
- the REPS2 change has negative and positive synthetic tests plus a real-title probe result;
- the remaining unknowns and compatibility claims are updated without claiming a boot stage that was not reached.
