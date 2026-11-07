// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace reps2::runtime {

// EE DMAC D_STAT at 0x1000E010.
//
// The bounded contract below is backed by the PS2Tek EE register map:
// channel status bits 0..9 are write-one-to-clear, while channel interrupt
// mask bits 16..25 and the stall/MFIFO mask bits 29..30 are write-one-to-toggle.
// It intentionally does not invent a reset value or generate DMA events.
inline constexpr std::uint32_t kEeDmacDStatAddress{0x1000'E010U};
inline constexpr std::uint32_t kEeDmacDStatChannelStatusMask{0x0000'03FFU};
inline constexpr std::uint32_t kEeDmacDStatUnmodelledStatusWriteMask{0x0000'E000U};
inline constexpr std::uint32_t kEeDmacDStatInterruptMaskMask{0x63FF'0000U};
inline constexpr std::uint32_t kEeDmacDStatReadableMask{0x63FF'E3FFU};

struct EeDmacState {
    std::uint32_t d_stat{};
    bool d_stat_known{};

    // Explicit device/test input, never a hardware reset default. Reserved
    // fields are discarded; documented read fields are retained.
    void seed_d_stat(std::uint32_t value) noexcept {
        d_stat = value & kEeDmacDStatReadableMask;
        d_stat_known = true;
    }

    [[nodiscard]] bool read_d_stat(std::uint32_t& value) const noexcept {
        if (!d_stat_known) return false;
        value = d_stat & kEeDmacDStatReadableMask;
        return true;
    }

    [[nodiscard]] bool write_d_stat(std::uint32_t value) noexcept {
        if (!d_stat_known
            || (value & kEeDmacDStatUnmodelledStatusWriteMask) != 0U) {
            return false;
        }

        // Acknowledge only the documented channel status fields. Writes of one
        // to interrupt-mask fields reverse those bits; writes of zero preserve.
        // Status writes for bits 13..15 remain unsupported pending evidence.
        d_stat &= ~(value & kEeDmacDStatChannelStatusMask);
        d_stat ^= value & kEeDmacDStatInterruptMaskMask;
        d_stat &= kEeDmacDStatReadableMask;
        return true;
    }
};

} // namespace reps2::runtime
