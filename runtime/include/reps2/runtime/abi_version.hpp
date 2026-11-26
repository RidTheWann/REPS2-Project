// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Increment when generated native modules and the shared C++ runtime become
// incompatible at their public state/entry-point boundary.
//
// That boundary includes the layout of the runtime state the generated code
// receives as an opaque pointer: any field added, removed, resized or
// reordered in EeState (and its members) is a contract change and requires a
// bump, even when no function signature moves. The compiler/STL fingerprint in
// engine/include/rg_reps2_engine/toolchain_abi.hpp is a second, coarser guard
// for cross-compiler mismatches; it is not a substitute, because it reports the
// same toolchain_abi_mismatch for an unrelated cause.
//
// History: 18 covered the ADR 0156/0157 state. ADR 0160 grew the modelled SIF
// RPC server table from 18 to 24 entries and ADR 0163 added the FILEIO
// observation fields to EeSifCmdState; both changed sizeof(EeState) while this
// value stayed 18, so those modules were rejected by the fingerprint with a
// misleading diagnosis. ABI 19 restates the contract (ADR 0164).
//
// ABI 20 covers ADR 0168, which added the FILEIO virtual descriptor table to
// EeSifCmdState without a bump (the same violation ABI 19 exists to prevent),
// and ADR 0170, which adds the bounded syscall census and optional trace ring
// to EeHleState. Those modules must be rebuilt; no ABI 19 module is admitted.
//
// ABI 21 covers ADR 0172: the EE Timer 2 register model in EeMemory and the
// EE INTC handler registry plus delivery counters in EeHleState. Both change
// sizeof(EeState), so modules built against ABI 20 are refused.
//
// ABI 22 covers ADR 0173: the cooperative scheduler. `EeThread` gains a saved
// register context and `waiting_on_sema`, and `EeHleState` gains the
// `active_thread_id`/`blocked_sema_id`/`main_context` fields plus the
// scheduler counters. Modules built against ABI 21 are refused.
//
// ABI 23 covers ADR 0174: the 989snd loader server's observation fields
// (`snd_loader_*`) in EeSifCmdState change sizeof(EeState). Modules built
// against ABI 22 are refused.
//
// ABI 24 covers ADR 0175: the GS CSR (0x12001000) and IMR (0x12001010) storage
// registers (`gs_csr`/`gs_imr`) in EeMemory plus their MMIO dispatch entries
// change sizeof(EeState). Modules built against ABI 23 are refused.
//
// ABI 25 covers ADR 0176: 64-bit (SD/LD) accesses to GS CSR/IMR are decomposed
// into 32-bit MMIO operations in store_u64/load_u64, so a 64-bit guest store
// no longer bypasses the MMIO table and fails with unsupported_memory_access.
// This changes generated-code-visible behavior (the store_u64/load_u64 inline
// bodies), so modules built against ABI 24 are refused.
//
// ABI 26 covers ADR 0177: the GsGetIMR (0x70) / GsPutIMR (0x71) syscall services
// are registered in the HLE service table, so the generated syscall dispatch
// resolves them instead of returning unhandled_syscall. No state-layout
// change (the table is compile-time), but generated modules embedding the
// old table are refused.
//
// ABI 27 covers ADR 0178: the GS CRT/video-mode registers PMODE (0x12000000)
// and SMODE2 (0x12000020) are modelled as bounded storage in EeMemory
// (`gs_pmode`/`gs_smode2`) plus MMIO dispatch entries, and the SetGsCrt (0x02)
// syscall service is registered. sizeof(EeState) changes, and 64-bit
// store_u64/load_u64 decomposition extends to the new GS addresses, so
// modules built against ABI 26 are refused.
// ABI 28 covers ADR 0179: the VIF1 command registers VIF1_FBRST (0x10003C10)
// and VIF1_ERR (0x10003C20) are modelled as bounded storage in EeMemory
// (`vif1_fbrst`/`vif1_err`) plus MMIO dispatch entries, and 64-bit SD/LD
// decomposition extends to them. sizeof(EeState) changes; ABI-27 modules are
// refused.
//
// ABI 29 covers ADR 0180: the VU0 COP2 control file gains a bounded 32-word
// storage array (`Vu0State::control`), CFC2/CTC2 are decoded, lifted and
// emitted, and generated code reads/writes the control slots. sizeof(EeState)
// changes; ABI-28 modules are refused.
// ABI 29 covers ADR 0180: the VU0 COP2 control file gains a bounded 32-word
// storage array (`Vu0State::control`), CFC2/CTC2 are decoded, lifted and
// emitted, and generated code reads/writes the control slots. sizeof(EeState)
// changes; ABI-28 modules are refused.
//
// ABI 30 covers ADR 0181: the VIF1 FIFO port (0x10005000) is modelled as a
// bounded 16-qword ring with qword-only push/pop through store_u128/load_u128;
// empty reads and full writes fail closed. sizeof(EeState) changes, so
// ABI-29 modules are refused.
//
// ABI 31 covers ADR 0182: GIF_CTRL (0x10003000), GIF_MODE (0x10003010) and
// GIF_STAT (0x10003020) are bounded storage registers with MMIO dispatch and
// 64-bit SD/LD decomposition. sizeof(EeState) changes; ABI-30 modules are
// refused.
//
// ABI 32 covers ADR 0183: EE Timer 1 registers T1_COUNT/T1_MODE/T1_COMP/
// T1_HOLD (0x10000800/10/20/30) are bounded 32-bit storage with MMIO dispatch.
// No counting, compare events, or interrupts are modelled. sizeof(EeState)
// changes; ABI-31 modules are refused.
//
// ABI 33 covers ADR 0184: VIF0 DMAC channel-1 registers CHCR/MADR/QWC/TADR
// (0x10009000/10/20/30) are bounded storage with MMIO dispatch. No DMA
// transfer, interrupt, or completion is modelled. sizeof(EeState) changes;
// ABI-32 modules are refused.
// ABI 33 covers ADR 0184: VIF0 DMAC channel-1 registers CHCR/MADR/QWC/TADR
// (0x10009000/10/20/30) are bounded storage with MMIO dispatch. The guest
// writes them without prior seeding; reads before first write fail closed.
// sizeof(EeState) changes; ABI-32 modules are refused.
//
// ABI 34 covers ADR 0185: GIF DMAC channel-2 registers CHCR/MADR/QWC/TADR
// (0x1000A000/10/20/30) are bounded storage with MMIO dispatch. No DMA
// transfer, interrupt, or completion is modelled. sizeof(EeState) changes;
// ABI-33 modules are refused.
//
// ABI 35 covers ADR 0186: VIF1_STAT (0x10003C00) is modelled as bounded
// 32-bit storage with MMIO dispatch and 64-bit decomposition. sizeof(EeState)
// changes; ABI-34 modules are refused.
//
// ABI 36 covers ADR 0187: EE Scratchpad RAM (0x70000000..0x70003FFF) is
// backed by a bounded 16 KiB byte array, with width/alignment-checked
// little-endian accessors from the standard load/store helpers.
// sizeof(EeState) changes; ABI-35 modules are refused.
//
// ABI 37 covers ADR 0188: DMAC channels 1 (VIF0) and 2 (GIF) CHCR writes that
// raise STR (bit 8) now self-clear STR and raise the channel's D_STAT status
// bit, mirroring the documented transfer-completion behavior already modelled
// for channel 5 (SIF0). sizeof(EeState) changes (ch2 gains *_known fields);
// ABI-36 modules are refused.
//
// ABI 38 covers ADR 0189: VIF0 DMAC channel-0 registers (0x10008000..0x10008050)
// are bounded storage with STR self-clear + D_STAT bit 0 on start. sizeof(EeState)
// changes; ABI-37 modules are refused.
// ABI 39 covers ADR 0193: VU0 accumulator (Vu0State::acc) added for VOPMULA.
// sizeof(EeState) changes; ABI-38 modules are refused.
// ABI 40 covers ADR 0201: VU0 integer register file (Vu0State::vi, 16x u16).
// sizeof(EeState) changes; ABI-39 modules are refused.
// ABI 41 covers ADR 0202: VU0 data memory (Vu0State::data_mem, 256x Gpr128).
// sizeof(EeState) changes; ABI-40 modules are refused.
// ABI 43 covers ADR 0219: GS CRT display registers DISPFB2 (0x12000090),
// DISPLAY2 (0x120000A0) and BGCOLOR (0x120000E0) are bounded 64-bit storage
// in EeMemory (`gs_dispfb2`/`gs_display2`/`gs_bgcolor`) with MMIO dispatch,
// whole-register SD/LD reads/writes and low-word-preserving 32-bit writes.
// sizeof(EeState) changes; ABI-42 modules are refused. SMODE1 stays unmodelled.

#define REPS2_RUNTIME_ABI_VERSION 43