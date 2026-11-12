// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace reps2::runtime {

// EE Interrupt Controller (INTC) register addresses (PS2 EE register map).
// Verified against PCSX2 `pcsx2/Hw.h` (INTC_STAT = 0x1000F000,
// INTC_MASK = 0x1000F010).
inline constexpr std::uint32_t kEeIntcStatAddress{0x1000'F000U};
inline constexpr std::uint32_t kEeIntcMaskAddress{0x1000'F010U};

// I_MASK is write-one-to-toggle over its low 16 bits only; bits 16..31 of a
// store are ignored. Evidence: PCSX2 `pcsx2/HwWrite.cpp`
// (`psHu32(INTC_MASK) ^= (u16)value`).
inline constexpr std::uint32_t kEeIntcMaskToggleMask{0x0000'FFFFU};

// Documented INTC source numbers (PS2SDK `ee/include/kernel.h`, enum INTC_*).
// INTC_TIM3 (12) is reserved by the EE kernel for alarms and is intentionally
// not given a public constant here.
inline constexpr std::uint32_t kEeIntcSourceGs{0};
inline constexpr std::uint32_t kEeIntcSourceSbus{1};
inline constexpr std::uint32_t kEeIntcSourceVblankStart{2};
inline constexpr std::uint32_t kEeIntcSourceVblankEnd{3};
inline constexpr std::uint32_t kEeIntcSourceVif0{4};
inline constexpr std::uint32_t kEeIntcSourceVif1{5};
inline constexpr std::uint32_t kEeIntcSourceVu0{6};
inline constexpr std::uint32_t kEeIntcSourceVu1{7};
inline constexpr std::uint32_t kEeIntcSourceIpu{8};
inline constexpr std::uint32_t kEeIntcSourceTimer0{9};
inline constexpr std::uint32_t kEeIntcSourceTimer1{10};
inline constexpr std::uint32_t kEeIntcSourceTimer2{11};
inline constexpr std::uint32_t kEeIntcSourceSfifo{13};
inline constexpr std::uint32_t kEeIntcSourceVu0Watchdog{14};

// The EE interrupt controller register file.
//
// I_STAT holds pending interrupt sources (write-one-to-clear); I_MASK holds the
// per-source enable bits (write-one-to-toggle over the low 16 bits). This model
// keeps the documented guest-visible register contract only. It does NOT deliver
// a CPU exception and does not decide which CP0 interrupt pin (IP2/IP3) a source
// drives; `line_asserted()` reports the INTC pending-and-enabled condition a
// future exception-delivery path would consume.
//
// Every access fails closed until the register state is established: the HLE
// boot environment seeds the kernel-initialized idle state, and production code
// that never opted into that layer sees `unsupported_memory_access` rather than a
// guessed value. The idle seed (no pending source, all sources masked) is an
// explicitly documented HLE model of the state the replaced EE kernel presents,
// not a measured hardware reset value. See ADR 0103.
struct EeIntcState {
    // I_STAT: pending interrupt status. Full 32-bit register; write-one-to-clear.
    std::uint32_t i_stat{};
    bool i_stat_known{};

    // I_MASK: per-source enable mask. Write-one-to-toggle over bits 0..15.
    std::uint32_t i_mask{};
    bool i_mask_known{};

    // HLE boot environment: the replaced kernel-initialized idle INTC state.
    void seed_idle_boot_state() noexcept {
        i_stat = 0;
        i_stat_known = true;
        i_mask = 0;
        i_mask_known = true;
    }

    // Seed I_STAT from device/test input. No hardware reset default.
    void seed_stat(std::uint32_t value) noexcept {
        i_stat = value;
        i_stat_known = true;
    }

    // Seed I_MASK from device/test input.
    void seed_mask(std::uint32_t value) noexcept {
        i_mask = value;
        i_mask_known = true;
    }

    // Read I_STAT. Fails if not seeded.
    [[nodiscard]] bool read_stat(std::uint32_t& value) const noexcept {
        if (!i_stat_known) return false;
        value = i_stat;
        return true;
    }

    // Write I_STAT: write-one-to-clear. Bits written as 1 clear the matching
    // pending status; bits written as 0 are left unchanged. Evidence: PCSX2
    // `pcsx2/HwWrite.cpp` (`psHu32(INTC_STAT) &= ~value`). Fails if not seeded.
    [[nodiscard]] bool write_stat(std::uint32_t value) noexcept {
        if (!i_stat_known) return false;
        i_stat &= ~value;
        return true;
    }

    // Read I_MASK. Fails if not seeded.
    [[nodiscard]] bool read_mask(std::uint32_t& value) const noexcept {
        if (!i_mask_known) return false;
        value = i_mask;
        return true;
    }

    // Write I_MASK: write-one-to-toggle over the low 16 bits. Evidence: PCSX2
    // `pcsx2/HwWrite.cpp` (`psHu32(INTC_MASK) ^= (u16)value`). Fails if not
    // seeded.
    [[nodiscard]] bool write_mask(std::uint32_t value) noexcept {
        if (!i_mask_known) return false;
        i_mask ^= value & kEeIntcMaskToggleMask;
        return true;
    }

    // Raise a documented INTC source: set its pending status bit. This records
    // pending state only; it delivers no CPU exception. Fails closed when I_STAT
    // is unknown or the source number is outside the modelled 0..15 range.
    [[nodiscard]] bool raise_source(std::uint32_t source) noexcept {
        if (!i_stat_known || source >= 32U) return false;
        i_stat |= (std::uint32_t{1} << source);
        return true;
    }

    // True when a pending source is enabled by the mask, i.e. the INTC line a
    // future exception-delivery path would assert. False when either register is
    // unknown, so an unseeded device never reports an interrupt.
    [[nodiscard]] bool line_asserted() const noexcept {
        if (!i_stat_known || !i_mask_known) return false;
        return (i_stat & i_mask) != 0U;
    }
};

} // namespace reps2::runtime