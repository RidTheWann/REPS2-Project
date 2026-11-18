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
#define REPS2_RUNTIME_ABI_VERSION 19