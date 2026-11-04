# REPS2 compatibility matrix

Only evidence-backed states are recorded. Synthetic fixtures are not retail-title compatibility evidence.

| Title identity | Serial | Revision evidence | ISO/ELF | Analysis | Native | Boot stage | Gameplay | Blockers | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| Downhill Domination USA | SCUS_971.77 | ISO SHA-256 `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`; boot ELF SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c` | Imported and analyzed | 56,181 blocks; 291,599 instructions | Built and executed bounded native probe | Stops at unhandled `FindAddress` 0x83 at PC `0x0025EF7C`; no guest-visible stage | Not tested | ABI, return value, and memory effects unresolved | Probe output 2026-11-05, ADR 0068 |

No title is currently marked Playable. Add rows only with exact disc digest/revision, REPS2 revision, host configuration, and repeatable evidence.
