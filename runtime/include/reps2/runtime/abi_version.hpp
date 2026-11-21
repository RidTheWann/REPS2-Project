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
#define REPS2_RUNTIME_ABI_VERSION 28