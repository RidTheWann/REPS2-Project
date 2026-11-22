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
constexpr std::uint32_t kGsPmodeAddr = reps2::runtime::kEeGsPmodeAddress;
constexpr std::uint32_t kGsSmode2Addr = reps2::runtime::kEeGsSmode2Address;
constexpr std::uint32_t kVif1StatAddr = reps2::runtime::kEeVif1StatAddress;
constexpr std::uint32_t kVif1FbrstAddr = reps2::runtime::kEeVif1FbrstAddress;
constexpr std::uint32_t kVif1ErrAddr = reps2::runtime::kEeVif1ErrAddress;
constexpr std::uint32_t kGifCtrlAddr = reps2::runtime::kEeGifCtrlAddress;
constexpr std::uint32_t kGifModeAddr = reps2::runtime::kEeGifModeAddress;
constexpr std::uint32_t kGifStatAddr = reps2::runtime::kEeGifStatAddress;
constexpr std::uint32_t kT1CountAddr = reps2::runtime::kEeTimer1CountAddress;
constexpr std::uint32_t kT1ModeAddr = reps2::runtime::kEeTimer1ModeAddress;
constexpr std::uint32_t kT1CompAddr = reps2::runtime::kEeTimer1CompareAddress;
constexpr std::uint32_t kT1HoldAddr = reps2::runtime::kEeTimer1HoldAddress;
constexpr std::uint32_t kDmacChcr1Addr = reps2::runtime::kEeDmacChcr1Address;
constexpr std::uint32_t kDmacMadr1Addr = reps2::runtime::kEeDmacMadr1Address;
constexpr std::uint32_t kDmacQwc1Addr = reps2::runtime::kEeDmacQwc1Address;
constexpr std::uint32_t kDmacTadr1Addr = reps2::runtime::kEeDmacTadr1Address;
constexpr std::uint32_t kEeScratchpadBaseAddress = reps2::runtime::kEeScratchpadBaseAddress;
constexpr std::uint32_t kDmacChcr2Addr = reps2::runtime::kEeDmacChcr2Address;
constexpr std::uint32_t kDmacMadr2Addr = reps2::runtime::kEeDmacMadr2Address;
constexpr std::uint32_t kDmacQwc2Addr = reps2::runtime::kEeDmacQwc2Address;
constexpr std::uint32_t kDmacTadr2Addr = reps2::runtime::kEeDmacTadr2Address;

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

// A 64-bit store (SD) to GS CSR writes only the low 32-bit word to the register.
// ADR 0176: 64-bit stores to 32-bit registers must reach the MMIO handler.
int test_gs_csr_store_u64_decomposes() {
    EeState state{};
    // 64-bit value 0x200 (wRESET) in low 32 bits, high 32 bits are ignored
    if (reps2::runtime::store_u64(state.memory, kGsCsrAddr, 0x0000'0001'0000'0200ULL)
        != Status::ok) {
        return 13;
    }
    // Only low 32 bits stored; high 32 bits dropped
    if (state.memory.gs_csr != 0x0000'0200U) return 14;
    // IMR unaffected by CSR write
    if (state.memory.gs_imr != 0U) return 15;
    return 0;
}

// A 64-bit load (LD) from GS CSR returns the register value zero-extended.
// ADR 0176: 64-bit loads from 32-bit registers must reach the MMIO handler.
int test_gs_csr_load_u64_decomposes() {
    EeState state{};
    // Pre-load a value in the register (command word 0xDEAD in high 16, low 16 clear)
    state.memory.gs_csr = 0xDEAD'0000U;
    std::uint64_t value = 0ULL;
    if (reps2::runtime::load_u64(state.memory, kGsCsrAddr, value) != Status::ok) return 16;
    // 32-bit read returns high 16 only (command word); zero-extended to 64 bits
    if (value != 0xDEAD'0000ULL) return 17;
    return 0;
}

// 64-bit store to GS IMR writes the low 16 bits (mask) and ignores the rest.
int test_gs_imr_store_u64_decomposes() {
    EeState state{};
    if (reps2::runtime::store_u64(state.memory, kGsImrAddr, 0x0000'0000'0000'1234ULL)
        != Status::ok) {
        return 18;
    }
    // Only low 16 bits stored
    if (state.memory.gs_imr != 0x0000'1234U) return 19;
    return 0;
}

// 64-bit load from GS IMR returns the 16-bit mask zero-extended.
int test_gs_imr_load_u64_decomposes() {
    EeState state{};
    state.memory.gs_imr = 0xABCDU;
    std::uint64_t value = 0ULL;
    if (reps2::runtime::load_u64(state.memory, kGsImrAddr, value) != Status::ok) return 20;
    // Low 16 bits zero-extended to 64 bits
    if (value != 0xABCDULL) return 21;
    return 0;
}

// PMODE (0x12000000) is a bounded storage register: write stores, read returns.
// ADR 0178.
int test_gs_pmode_storage() {
    EeState state{};
    if (reps2::runtime::store_u32(state.memory, kGsPmodeAddr, 0x0000'0003U) != Status::ok)
        return 22;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kGsPmodeAddr, value) != Status::ok) return 23;
    if (value != 0x0000'0003U) return 24;
    return 0;
}

// SMODE2 (0x12000020) is a bounded storage register. ADR 0178.
int test_gs_smode2_storage() {
    EeState state{};
    if (reps2::runtime::store_u32(state.memory, kGsSmode2Addr, 0x0000'0001U) != Status::ok)
        return 25;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kGsSmode2Addr, value) != Status::ok) return 26;
    if (value != 0x0000'0001U) return 27;
    return 0;
}

// 64-bit store to PMODE decomposes to 32-bit store (low word). ADR 0178.
int test_gs_pmode_store_u64_decomposes() {
    EeState state{};
    if (reps2::runtime::store_u64(state.memory, kGsPmodeAddr, 0x0000'0001'0000'0003ULL)
        != Status::ok)
        return 28;
    if (state.memory.gs_pmode != 0x0000'0003U) return 29;
    return 0;
}

// VIF1_FBRST (0x10003C10) accepts writes and reads return the stored word.
// ADR 0179: bounded storage, no VIF FIFO reset.
// VIF1_STAT (0x10003C00) bounded storage, ADR 0186.
int test_vif1_stat_storage() {
    EeState state{};
    std::uint32_t value = 1U;
    if (reps2::runtime::load_u32(state.memory, kVif1StatAddr, value) != Status::ok || value != 0U)
        return 140;
    if (reps2::runtime::store_u32(state.memory, kVif1StatAddr, 0x0000'00FFU) != Status::ok)
        return 141;
    if (reps2::runtime::load_u32(state.memory, kVif1StatAddr, value) != Status::ok || value != 0x0000'00FFU)
        return 142;
    return 0;
}

int test_vif1_fbrst_storage() {
    EeState state{};
    if (reps2::runtime::store_u32(state.memory, kVif1FbrstAddr, 0x0000'0001U) != Status::ok)
        return 30;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kVif1FbrstAddr, value) != Status::ok) return 31;
    if (value != 0x0000'0001U) return 32;
    return 0;
}

// VIF1_ERR (0x10003C20) accepts writes and reads return the stored word.
// ADR 0179.
int test_vif1_err_storage() {
    EeState state{};
    if (reps2::runtime::store_u32(state.memory, kVif1ErrAddr, 0x0000'0002U) != Status::ok)
        return 33;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kVif1ErrAddr, value) != Status::ok) return 34;
    if (value != 0x0000'0002U) return 35;
    return 0;
}

// 64-bit store to VIF1_FBRST decomposes to 32-bit store (low word). ADR 0179.
int test_vif1_fbrst_store_u64_decomposes() {
    EeState state{};
    if (reps2::runtime::store_u64(state.memory, kVif1FbrstAddr, 0x0000'0001'0000'0001ULL)
        != Status::ok)
        return 36;
    if (state.memory.vif1_fbrst != 0x0000'0001U) return 37;
    return 0;
}

// GIF_CTRL (0x10003000) bounded storage, ADR 0182.
int test_gif_ctrl_storage() {
    EeState state{};
    if (reps2::runtime::store_u32(state.memory, kGifCtrlAddr, 0x0000'0003U) != Status::ok)
        return 100;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kGifCtrlAddr, value) != Status::ok) return 101;
    if (value != 0x0000'0003U) return 102;
    return 0;
}

// GIF_MODE (0x10003010) bounded storage, ADR 0182.
int test_gif_mode_storage() {
    EeState state{};
    if (reps2::runtime::store_u32(state.memory, kGifModeAddr, 0x0000'0005U) != Status::ok)
        return 110;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kGifModeAddr, value) != Status::ok) return 111;
    if (value != 0x0000'0005U) return 112;
    return 0;
}

// GIF_STAT (0x10003020) bounded storage, ADR 0182.
int test_gif_stat_storage() {
    EeState state{};
    if (reps2::runtime::store_u32(state.memory, kGifStatAddr, 0x0000'00FFU) != Status::ok)
        return 120;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kGifStatAddr, value) != Status::ok) return 121;
    if (value != 0x0000'00FFU) return 122;
    return 0;
}

// 64-bit store to GIF_CTRL decomposes to 32-bit store (low word). ADR 0182.
int test_gif_ctrl_store_u64_decomposes() {
    EeState state{};
    if (reps2::runtime::store_u64(state.memory, kGifCtrlAddr, 0x0000'0001'0000'0002ULL)
        != Status::ok)
        return 130;
    if (state.memory.gif_ctrl != 0x0000'0002U) return 131;
    return 0;
}

// Timer 1 registers are bounded storage. ADR 0183.
int test_timer1_registers_storage() {
    EeState state{};
    if (reps2::runtime::store_u32(state.memory, kT1ModeAddr, 0x0000'0082U) != Status::ok) return 200;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kT1ModeAddr, value) != Status::ok) return 201;
    if (value != 0x0000'0082U) return 202;
    if (reps2::runtime::store_u32(state.memory, kT1CountAddr, 0x0000'1234U) != Status::ok) return 203;
    if (reps2::runtime::load_u32(state.memory, kT1CountAddr, value) != Status::ok) return 204;
    if (value != 0x0000'1234U) return 205;
    if (reps2::runtime::store_u32(state.memory, kT1CompAddr, 0x0000'5678U) != Status::ok) return 206;
    if (reps2::runtime::load_u32(state.memory, kT1CompAddr, value) != Status::ok) return 207;
    if (value != 0x0000'5678U) return 208;
    if (reps2::runtime::store_u32(state.memory, kT1HoldAddr, 0x0000'9ABCU) != Status::ok) return 209;
    if (reps2::runtime::load_u32(state.memory, kT1HoldAddr, value) != Status::ok) return 210;
    if (value != 0x0000'9ABCU) return 211;
    return 0;
}

// VIF0 DMAC channel-1 registers are bounded storage. ADR 0184.
int test_dmac_ch1_registers_storage() {
    EeState state{};
    // CHCR/MADR/QWC/TADR are writable storage; reset reads are zero.
    std::uint32_t value = 1U;
    if (reps2::runtime::load_u32(state.memory, kDmacChcr1Addr, value) != Status::ok || value != 0U)
        return 300;
    if (reps2::runtime::store_u32(state.memory, kDmacChcr1Addr, 0x0000'0101U) != Status::ok)
        return 301;
    if (reps2::runtime::store_u32(state.memory, kDmacMadr1Addr, 0x0027'F710U) != Status::ok)
        return 302;
    if (reps2::runtime::store_u32(state.memory, kDmacQwc1Addr, 0x0000'0010U) != Status::ok)
        return 303;
    if (reps2::runtime::store_u32(state.memory, kDmacTadr1Addr, 0x0027'F800U) != Status::ok)
        return 304;
    if (reps2::runtime::load_u32(state.memory, kDmacChcr1Addr, value) != Status::ok || value != 0x0000'0101U)
        return 305;
    if (reps2::runtime::load_u32(state.memory, kDmacMadr1Addr, value) != Status::ok || value != 0x0027'F710U)
        return 306;
    if (reps2::runtime::load_u32(state.memory, kDmacQwc1Addr, value) != Status::ok || value != 0x0000'0010U)
        return 307;
    if (reps2::runtime::load_u32(state.memory, kDmacTadr1Addr, value) != Status::ok || value != 0x0027'F800U)
        return 308;
    return 0;
}

// GIF DMAC channel-2 registers are bounded storage, no transfer effect. ADR 0185.
int test_dmac_ch2_registers_storage() {
    EeState state{};
    std::uint32_t value = 1U;
    if (reps2::runtime::load_u32(state.memory, kDmacChcr2Addr, value) != Status::ok || value != 0U)
        return 320;
    if (reps2::runtime::store_u32(state.memory, kDmacChcr2Addr, 0x0000'0101U) != Status::ok)
        return 321;
    if (reps2::runtime::store_u32(state.memory, kDmacMadr2Addr, 0x0027'F710U) != Status::ok)
        return 322;
    if (reps2::runtime::store_u32(state.memory, kDmacQwc2Addr, 0x0000'0010U) != Status::ok)
        return 323;
    if (reps2::runtime::store_u32(state.memory, kDmacTadr2Addr, 0x0027'F800U) != Status::ok)
        return 324;
    if (reps2::runtime::load_u32(state.memory, kDmacChcr2Addr, value) != Status::ok || value != 0x0000'0101U)
        return 325;
    if (reps2::runtime::load_u32(state.memory, kDmacMadr2Addr, value) != Status::ok || value != 0x0027'F710U)
        return 326;
    if (reps2::runtime::load_u32(state.memory, kDmacQwc2Addr, value) != Status::ok || value != 0x0000'0010U)
        return 327;
    if (reps2::runtime::load_u32(state.memory, kDmacTadr2Addr, value) != Status::ok || value != 0x0027'F800U)
        return 328;
    return 0;
}

// EE scratchpad RAM 0x70000000..0x70003FFF: bounded LE byte storage. ADR 0187.
int test_scratchpad_accesses() {
    EeState state{};
    std::uint32_t v = 0U;
    if (reps2::runtime::load_u32(state.memory, kEeScratchpadBaseAddress + 0x2CU, v) != Status::ok || v != 0U)
        return 400;
    if (reps2::runtime::store_u32(state.memory, kEeScratchpadBaseAddress + 0x2CU, 0x80000000U) != Status::ok)
        return 401;
    if (reps2::runtime::load_u32(state.memory, kEeScratchpadBaseAddress + 0x2CU, v) != Status::ok || v != 0x80000000U)
        return 402;
    if (reps2::runtime::store_u32(state.memory, kEeScratchpadBaseAddress + 0x3FFCU, 0xDEADBEEFU) != Status::ok)
        return 403;
    if (reps2::runtime::load_u32(state.memory, kEeScratchpadBaseAddress + 0x3FFCU, v) != Status::ok || v != 0xDEADBEEFU)
        return 404;
    if (reps2::runtime::store_u32(state.memory, kEeScratchpadBaseAddress + 0x3FFDU, 1U) != Status::address_error)
        return 405;
    if (reps2::runtime::store_u32(state.memory, kEeScratchpadBaseAddress + 0x4000U, 1U) != Status::unmapped_memory)
        return 406;
    std::uint64_t v64 = 0U;
    if (reps2::runtime::store_u64(state.memory, kEeScratchpadBaseAddress + 0x10U, 0x0102'0304'0506'0708ULL) != Status::ok)
        return 407;
    if (reps2::runtime::load_u64(state.memory, kEeScratchpadBaseAddress + 0x10U, v64) != Status::ok || v64 != 0x0102'0304'0506'0708ULL)
        return 408;
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
    run("gs_csr_store_u64_decomposes", test_gs_csr_store_u64_decomposes);
    run("gs_csr_load_u64_decomposes", test_gs_csr_load_u64_decomposes);
    run("gs_imr_store_u64_decomposes", test_gs_imr_store_u64_decomposes);
    run("gs_imr_load_u64_decomposes", test_gs_imr_load_u64_decomposes);
    run("gs_pmode_storage", test_gs_pmode_storage);
    run("gs_smode2_storage", test_gs_smode2_storage);
    run("gs_pmode_store_u64_decomposes", test_gs_pmode_store_u64_decomposes);
    run("vif1_stat_storage", test_vif1_stat_storage);
    run("vif1_fbrst_storage", test_vif1_fbrst_storage);
    run("vif1_err_storage", test_vif1_err_storage);
    run("gif_ctrl_storage", test_gif_ctrl_storage);
    run("gif_mode_storage", test_gif_mode_storage);
    run("gif_stat_storage", test_gif_stat_storage);
    run("gif_ctrl_store_u64_decomposes", test_gif_ctrl_store_u64_decomposes);
    run("timer1_registers_storage", test_timer1_registers_storage);
    run("dmac_ch1_registers_storage", test_dmac_ch1_registers_storage);
    run("dmac_ch2_registers_storage", test_dmac_ch2_registers_storage);

    run("scratchpad_accesses", test_scratchpad_accesses);
    run("vif1_fbrst_store_u64_decomposes", test_vif1_fbrst_store_u64_decomposes);

    if (fails == 0) {
        printf("All GS CSR/IMR tests passed\n");
        return 0;
    }
    printf("%d test(s) failed\n", fails);
    return 1;
}
