# REPS2 compatibility matrix

Only evidence-backed states are recorded. Synthetic fixtures are not retail-title compatibility evidence.

| Title identity | Serial | Revision evidence | ISO/ELF | Analysis | Native | Boot stage | Gameplay | Blockers | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| Downhill Domination USA | SCUS_971.77 | ISO SHA-256 `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`; boot ELF SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c` | Imported and analyzed | Latest generated artifact: 60,466 blocks; 314,732 instructions; 7,146 accepted roots; two relocated regions | Bounded native probe rebuilt after COP2 support | Reaches `0x0025C274` after 3,168,708 instructions, then stops on unsupported read of `D_STAT` (`0x1000E010`); no guest-visible boot stage | Not tested | DMAC/SIF status semantics and VU arithmetic remain unsupported | Local probe, 2026-11-07; ADRs 0068–0078 |
| Black USA | SLUS_213.76 | ISO SHA-256 `65768cade88bfbaf566b67a3ef7a0bda5a9669779833914ffe7cd73a3e45cccd`; boot ELF SHA-256 `141b05209570458af9c8e27ab1911f258da6353909d813cd572df026254ffe4c` | Imported and analyzed | 101,956 blocks; 504,661 instructions | Built and executed bounded native probe (HLE on) | Starts at entry `0x00100008`; `290,344` guest instructions executed natively; services `0x3C`/`0x3D`/`0x40`/`0x74` serviced; stops at unhandled `FindAddress` 0x83 at PC `0x0036D664` (`$a0=0x80000000 $a1=0x80080000`); not re-probed since the 0x74/table changes | Not tested | `FindAddress` 0x83 ABI, return value, and memory effects unresolved; no local reference-execution oracle | Probe output 2026-11-05, ADR 0068 |

No title is currently marked Playable. Add rows only with exact disc digest/revision, REPS2 revision, host configuration, and repeatable evidence.
