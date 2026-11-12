// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_hle.hpp"
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>
#include <cstdio>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Status;

constexpr std::uint32_t kDStat      = reps2::runtime::kEeDmacDStatAddress;
constexpr std::uint32_t kCtrl       = reps2::runtime::kEeDmacCtrlAddress;
constexpr std::uint32_t kPcr        = reps2::runtime::kEeDmacPcrAddress;
constexpr std::uint32_t kChcr5      = reps2::runtime::kEeDmacChcr5Address;
constexpr std::uint32_t kMadr5      = reps2::runtime::kEeDmacMadr5Address;
constexpr std::uint32_t kQwc5       = reps2::runtime::kEeDmacQwc5Address;
constexpr std::uint32_t kTadr5      = reps2::runtime::kEeDmacTadr5Address;
constexpr std::uint32_t kTimer3Mode = reps2::runtime::kEeTimer3ModeAddress;

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

    // New registers also fail closed when not seeded.
    if (reps2::runtime::load_u32(state.memory, kCtrl, value) != Status::unsupported_memory_access) return 5;
    if (reps2::runtime::store_u32(state.memory, kCtrl, 0x01U) != Status::unsupported_memory_access) return 6;
    if (reps2::runtime::load_u32(state.memory, kPcr, value) != Status::unsupported_memory_access) return 7;
    if (reps2::runtime::store_u32(state.memory, kPcr, 0x01U) != Status::unsupported_memory_access) return 8;
    if (reps2::runtime::load_u32(state.memory, kChcr5, value) != Status::unsupported_memory_access) return 9;
    if (reps2::runtime::store_u32(state.memory, kChcr5, 0x100U) != Status::unsupported_memory_access) return 10;
    if (reps2::runtime::load_u32(state.memory, kMadr5, value) != Status::unsupported_memory_access) return 11;
    if (reps2::runtime::store_u32(state.memory, kMadr5, 0x0028'31A0U) != Status::unsupported_memory_access) return 12;
    if (reps2::runtime::load_u32(state.memory, kQwc5, value) != Status::unsupported_memory_access) return 13;
    if (reps2::runtime::store_u32(state.memory, kQwc5, 0x4CU) != Status::unsupported_memory_access) return 14;
    if (reps2::runtime::load_u32(state.memory, kTadr5, value) != Status::unsupported_memory_access) return 15;
    if (reps2::runtime::store_u32(state.memory, kTadr5, 0x00U) != Status::unsupported_memory_access) return 16;

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

int test_new_registers_seeded_read_write() {
    EeState state{};

    // Seed all new registers.
    state.memory.dmac.seed_ctrl(0x0000'0001U);  // enable ch0
    state.memory.dmac.seed_pcr(0x0000'0020U);  // mask SIF0
    state.memory.dmac.seed_chcr5(0x0000'0000U); // STR=0
    state.memory.dmac.seed_madr5(0x0028'31A0U);
    state.memory.dmac.seed_qwc5(0x4CU);         // 76 quadwords
    state.memory.dmac.seed_tadr5(0x0000'0000U);

    std::uint32_t value = 0U;

    // D_CTRL read/write
    if (reps2::runtime::load_u32(state.memory, kCtrl, value) != Status::ok) return 40;
    if (value != 0x0000'0001U) return 41;
    if (reps2::runtime::store_u32(state.memory, kCtrl, 0x0000'0003U) != Status::ok) return 42;
    if (reps2::runtime::load_u32(state.memory, kCtrl, value) != Status::ok) return 43;
    if (value != 0x0000'0003U) return 44;

    // D_PCR read/write
    if (reps2::runtime::load_u32(state.memory, kPcr, value) != Status::ok) return 45;
    if (value != 0x0000'0020U) return 46;
    if (reps2::runtime::store_u32(state.memory, kPcr, 0x0000'0040U) != Status::ok) return 47;
    if (reps2::runtime::load_u32(state.memory, kPcr, value) != Status::ok) return 48;
    if (value != 0x0000'0040U) return 49;

    // CHCR5 read/write
    if (reps2::runtime::load_u32(state.memory, kChcr5, value) != Status::ok) return 50;
    if (value != 0x0000'0000U) return 51;
    if (reps2::runtime::store_u32(state.memory, kChcr5, 0x0000'0100U) != Status::ok) return 52; // STR=1
    if (reps2::runtime::load_u32(state.memory, kChcr5, value) != Status::ok) return 53;
    if (value != 0x0000'0100U) return 54;

    // MADR5 read/write
    if (reps2::runtime::load_u32(state.memory, kMadr5, value) != Status::ok) return 55;
    if (value != 0x0028'31A0U) return 56;
    if (reps2::runtime::store_u32(state.memory, kMadr5, 0x0028'3200U) != Status::ok) return 57;
    if (reps2::runtime::load_u32(state.memory, kMadr5, value) != Status::ok) return 58;
    if (value != 0x0028'3200U) return 59;

    // QWC5 read/write
    if (reps2::runtime::load_u32(state.memory, kQwc5, value) != Status::ok) return 60;
    if (value != 0x4CU) return 61;
    if (reps2::runtime::store_u32(state.memory, kQwc5, 0x80U) != Status::ok) return 62;
    if (reps2::runtime::load_u32(state.memory, kQwc5, value) != Status::ok) return 63;
    if (value != 0x80U) return 64;

    // TADR5 read/write
    if (reps2::runtime::load_u32(state.memory, kTadr5, value) != Status::ok) return 65;
    if (value != 0x0000'0000U) return 66;
    if (reps2::runtime::store_u32(state.memory, kTadr5, 0x0000'0100U) != Status::ok) return 67;
    if (reps2::runtime::load_u32(state.memory, kTadr5, value) != Status::ok) return 68;
    if (value != 0x0000'0100U) return 69;

    return 0;
}

int test_chcr5_str_sets_dstat_sif0() {
    EeState state{};

    // Seed D_STAT with SIF0 bit clear, and CHCR5 with STR=0.
    state.memory.dmac.seed_d_stat(0x0000'0000U); // no status bits set
    state.memory.dmac.seed_chcr5(0x0000'0000U);  // STR=0

    // Initial read: SIF0 bit should be 0.
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kDStat, value) != Status::ok) return 70;
    if ((value & (1U << 5U)) != 0U) return 71; // SIF0 bit not set yet

    // Write CHCR5 with STR=1 (start channel).
    if (reps2::runtime::store_u32(state.memory, kChcr5, 0x0000'0100U) != Status::ok) return 72;

    // D_STAT should now have SIF0 bit set (simulated transfer complete / interrupt pending).
    if (reps2::runtime::load_u32(state.memory, kDStat, value) != Status::ok) return 73;
    if ((value & (1U << 5U)) == 0U) return 74; // SIF0 bit should be set

    // Guest W1C acknowledge: write 0x20 back to D_STAT should clear SIF0 bit.
    if (reps2::runtime::store_u32(state.memory, kDStat, 1U << 5U) != Status::ok) return 75;

    // Read D_STAT again: SIF0 bit should be clear.
    if (reps2::runtime::load_u32(state.memory, kDStat, value) != Status::ok) return 76;
    if ((value & (1U << 5U)) != 0U) return 77; // SIF0 bit should be clear after W1C

    // Other bits preserved.
    if (value != 0x0000'0000U) return 78;

    return 0;
}

int test_idle_boot_state_is_readable() {
    EeState state{};
    // The HLE boot environment seeds the kernel-initialized idle DMAC state.
    reps2::runtime::initialize_hle_boot_environment(state);

    std::uint32_t value = 0xA5A5'5A5AU;
    if (reps2::runtime::load_u32(state.memory, kCtrl, value) != Status::ok) return 80;
    if (value != 0U) return 81;
    if (reps2::runtime::load_u32(state.memory, kDStat, value) != Status::ok) return 82;
    if (value != 0U) return 83;
    if (reps2::runtime::load_u32(state.memory, kPcr, value) != Status::ok) return 84;
    if (value != 0U) return 85;
    if (reps2::runtime::load_u32(state.memory, kChcr5, value) != Status::ok) return 86;
    if (value != 0U) return 87;
    if (reps2::runtime::load_u32(state.memory, kMadr5, value) != Status::ok) return 88;
    if (value != 0U) return 89;
    if (reps2::runtime::load_u32(state.memory, kQwc5, value) != Status::ok) return 90;
    if (value != 0U) return 91;
    if (reps2::runtime::load_u32(state.memory, kTadr5, value) != Status::ok) return 92;
    if (value != 0U) return 93;

    // SIF0 CHCR STR=1 produces the SIF0 completion event on D_STAT.
    if (reps2::runtime::store_u32(state.memory, kChcr5, 0x0000'0100U) != Status::ok) return 94;
    if (reps2::runtime::load_u32(state.memory, kDStat, value) != Status::ok) return 95;
    if ((value & (1U << 5U)) == 0U) return 96;
    if (reps2::runtime::store_u32(state.memory, kDStat, 1U << 5U) != Status::ok) return 97;
    if (reps2::runtime::load_u32(state.memory, kDStat, value) != Status::ok) return 98;
    if (value != 0U) return 99;
    return 0;
}

int test_dmac_interrupt_condition() {
    using reps2::runtime::ee_dmac_interrupt_pending;
    constexpr std::uint32_t cis5 = 1U << 5U;   // SIF0 status
    constexpr std::uint32_t cim5 = 1U << 21U;  // SIF0 interrupt mask

    // A channel status bit whose matching mask bit is set asserts the line.
    if (!ee_dmac_interrupt_pending(cis5 | cim5, true, false)) return 80;
    // Status alone or mask alone does not.
    if (ee_dmac_interrupt_pending(cis5, true, false)) return 81;
    if (ee_dmac_interrupt_pending(cim5, true, false)) return 82;
    // The bus-error status bit notifies without a matching mask bit.
    if (!ee_dmac_interrupt_pending(
            reps2::runtime::kEeDmacDStatBusErrorStatusBit, true, false)) {
        return 83;
    }
    // The stall pair (SIS 13, SIM 29) uses the same half-word comparison.
    if (!ee_dmac_interrupt_pending((1U << 13U) | (1U << 29U), true, false)) return 84;
    // A clear master enable or a held controller suppresses the line.
    if (ee_dmac_interrupt_pending(cis5 | cim5, false, false)) return 85;
    if (ee_dmac_interrupt_pending(cis5 | cim5, true, true)) return 86;
    // An unused upper bit alone asserts nothing, and so does an idle register.
    if (ee_dmac_interrupt_pending(1U << 31U, true, false)) return 87;
    if (ee_dmac_interrupt_pending(0U, true, false)) return 88;
    return 0;
}

} // namespace

int main() {
    if (const auto result = test_unknown_state_fails_closed(); result != 0) {
        std::fprintf(stderr, "unknown-state D_STAT test failed at check %d\n", result);
        return 10 + result;
    }
    if (const auto result = test_seeded_read_and_guest_w1c_path(); result != 0) {
        std::fprintf(stderr, "seeded D_STAT guest path test failed at check %d\n", result);
        return 20 + result;
    }
    if (const auto result = test_channel_ack_and_mask_toggle_are_distinct(); result != 0) {
        std::fprintf(stderr, "D_STAT write semantics test failed at check %d\n", result);
        return 30 + result;
    }
    if (const auto result = test_new_registers_seeded_read_write(); result != 0) {
        std::fprintf(stderr, "new registers seeded read/write test failed at check %d\n", result);
        return 40 + result;
    }
    if (const auto result = test_chcr5_str_sets_dstat_sif0(); result != 0) {
        std::fprintf(stderr, "CHCR5 STR sets D_STAT SIF0 test failed at check %d\n", result);
        return 70 + result;
    }
    if (const auto result = test_idle_boot_state_is_readable(); result != 0) {
        std::fprintf(stderr, "idle boot-state DMAC test failed at check %d\n", result);
        return 80 + result;
    }
    if (const auto result = test_dmac_interrupt_condition(); result != 0) {
        std::fprintf(stderr, "DMAC interrupt condition test failed at check %d\n", result);
        return 90 + result;
    }
    return 0;
}