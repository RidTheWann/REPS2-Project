// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Status;

constexpr std::uint32_t kDStat = reps2::runtime::kEeDmacDStatAddress;

int test_unknown_state_fails_closed() {
    EeState state{};
    state.gpr[3].low = 0xA5A5'5A5AU;
    if (reps2::generated::execute(state) != Status::unsupported_memory_access) return 1;
    if (state.pc != 0x0000'F008U || state.gpr[3].low != 0xA5A5'5A5AU) return 2;

    std::uint32_t value = 0xA5A5'5A5AU;
    if (reps2::runtime::load_u32(state.memory, kDStat, value)
        != Status::unsupported_memory_access) return 1;
    if (value != 0xA5A5'5A5AU) return 2;
    if (reps2::runtime::store_u32(state.memory, kDStat, 0x20U)
        != Status::unsupported_memory_access) return 3;
    if (state.memory.dmac.d_stat_known || state.memory.dmac.d_stat != 0U) return 4;
    return 0;
}

int test_seeded_read_and_guest_w1c_path() {
    EeState state{};
    // SIF0 is EE DMAC channel 5: status bit 5, interrupt mask bit 21.
    // Also seed read-only status and mask fields to verify preservation.
    constexpr std::uint32_t initial =
        (1U << 0U) | (1U << 5U) | (1U << 14U) | (1U << 15U)
        | (1U << 21U) | (1U << 29U) | (1U << 31U) | 0x00001C00U;
    state.memory.dmac.seed_d_stat(initial);

    if (reps2::generated::execute(state) != Status::ok) return 10;

    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kDStat, value) != Status::ok) return 11;
    const auto expected = initial & reps2::runtime::kEeDmacDStatReadableMask;
    // Guest sequence LW/ANDI 0x20/SW clears SIF0 status only.
    if (value != (expected & ~(1U << 5U))) return 12;
    if ((value & (1U << 0U)) == 0U) return 13;
    if ((value & (1U << 21U)) == 0U || (value & (1U << 29U)) == 0U) return 14;
    if ((value & (1U << 14U)) == 0U || (value & (1U << 15U)) == 0U) return 15;
    // Reserved bits never appear in the read value.
    if ((value & (1U << 31U)) != 0U || (value & 0x00001C00U) != 0U) return 16;
    return 0;
}

int test_channel_ack_and_mask_toggle_are_distinct() {
    EeState state{};
    constexpr std::uint32_t initial =
        (1U << 0U) | (1U << 5U) | (1U << 21U) | (1U << 29U);
    state.memory.dmac.seed_d_stat(initial);

    if (reps2::runtime::store_u32(state.memory, kDStat, 1U << 0U) != Status::ok)
        return 20;
    if (state.memory.dmac.d_stat != ((1U << 5U) | (1U << 21U) | (1U << 29U)))
        return 21;

    // Bit 21 is SIF0's mask; write-one toggles it rather than acknowledging it.
    if (reps2::runtime::store_u32(state.memory, kDStat, 1U << 21U) != Status::ok)
        return 22;
    if ((state.memory.dmac.d_stat & (1U << 21U)) != 0U) return 23;
    if ((state.memory.dmac.d_stat & (1U << 5U)) == 0U) return 24;
    if (reps2::runtime::store_u32(state.memory, kDStat, 1U << 21U) != Status::ok)
        return 25;
    if ((state.memory.dmac.d_stat & (1U << 21U)) == 0U) return 26;

    // The special stall/MFIFO mask bits use the same write-one-to-toggle rule.
    if (reps2::runtime::store_u32(state.memory, kDStat, 1U << 29U) != Status::ok)
        return 27;
    if ((state.memory.dmac.d_stat & (1U << 29U)) != 0U) return 28;
    if (reps2::runtime::store_u32(state.memory, kDStat, 1U << 29U) != Status::ok)
        return 29;
    if ((state.memory.dmac.d_stat & (1U << 29U)) == 0U) return 30;

    // Status flags outside channel bits 0..9 are preserved by this bounded model.
    const auto before_unsupported_status_write = state.memory.dmac.d_stat;
    if (reps2::runtime::store_u32(state.memory, kDStat, 1U << 14U)
        != Status::unsupported_memory_access) return 31;
    if (state.memory.dmac.d_stat != before_unsupported_status_write) return 32;
    return 0;
}

} // namespace

int main() {
    if (const auto result = test_unknown_state_fails_closed(); result != 0) return result;
    if (const auto result = test_seeded_read_and_guest_w1c_path(); result != 0) return result;
    if (const auto result = test_channel_ack_and_mask_toggle_are_distinct(); result != 0) return result;
    return 0;
}
