// SPDX-License-Identifier: GPL-3.0-or-later
//
// EE GS CSR (0x12001000) / IMR (0x12001010) register-model smoke test (ADR 0175).
//
// Exercises the MMIO dispatch path directly through load_u32/store_u32, the same
// entry points generated guest stores use. REPS2 models no GPU, so CSR reads
// return the stored command word with the low status half forced clear and IMR
// is a plain 16-bit mask. A guest that resets/reads the GS must not stop on
// unsupported_memory_access (the ABI-23 frontier at pc=0x002434B4).
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>
#include <cstdio>

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Status;

constexpr std::uint32_t kGsCsrAddr = reps2::runtime::kEeGsCsrAddress;
constexpr std::uint32_t kGsImrAddr = reps2::runtime::kEeGsImrAddress;

int test_gs_csr_reset_write_is_not_unsupported() {
    EeState state{};
    // The ABI-23 frontier wrote wRESET (0x200, bit 9) to CSR via `sd`.
    if (reps2::runtime::store_u32(state.memory, kGsCsrAddr, 0x0000'0200U)
        != Status::ok) {
        return 1;
    }
    // CSR stores the command word; reset value before any write was 0.
    if (state.memory.gs_csr != 0x0000'0200U) return 2;
    return 0;
}

int test_gs_csr_read_clears_status_half() {
    EeState state{};
    // A guest writes a full word; the low 16 status bits are never reported set
    // because no GPU raises SIGNAL/FINISH/HSINT/VSINT/EDWINT/FLUSH.
    state.memory.gs_csr = 0xDEAD'BEEFU;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kGsCsrAddr, value) != Status::ok) return 3;
    if (value != 0xDEAD'0000U) return 4;
    return 0;
}

int test_gs_imr_is_16bit_mask() {
    EeState state{};
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kGsImrAddr, value) != Status::ok) return 5;
    if (value != 0U) return 6; // reset 0
    // A write above 16 bits stores only the low half.
    if (reps2::runtime::store_u32(state.memory, kGsImrAddr, 0xFFFF'1234U) != Status::ok) return 7;
    value = 0U;
    if (reps2::runtime::load_u32(state.memory, kGsImrAddr, value) != Status::ok) return 8;
    if (value != 0x0000'1234U) return 9;
    return 0;
}

// A KSEG0/KSEG1 alias of the GS address dispatches to the same register.
int test_gs_csr_alias_dispatches() {
    EeState state{};
    if (reps2::runtime::store_u32(state.memory, 0xA000'0000U | kGsCsrAddr, 0x0001'0000U)
        != Status::ok) {
        return 10;
    }
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, 0x8000'0000U | kGsCsrAddr, value) != Status::ok)
        return 11;
    if (value != 0x0001'0000U) return 12;
    return 0;
}

} // namespace

int main() {
    int fails = 0;
    auto run = [&](const char* name, int (*fn)()) {
        const int r = fn();
        if (r != 0) {
            printf("FAIL: %s (code %d)\n", name, r);
            ++fails;
        } else {
            printf("PASS: %s\n", name);
        }
    };

    run("gs_csr_reset_write_is_not_unsupported", test_gs_csr_reset_write_is_not_unsupported);
    run("gs_csr_read_clears_status_half", test_gs_csr_read_clears_status_half);
    run("gs_imr_is_16bit_mask", test_gs_imr_is_16bit_mask);
    run("gs_csr_alias_dispatches", test_gs_csr_alias_dispatches);

    if (fails == 0) {
        printf("All GS CSR/IMR tests passed\n");
        return 0;
    }
    printf("%d test(s) failed\n", fails);
    return 1;
}
