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

// The device-level predicate a delivery path consumes: the modelled DMAC asserts
// its own interrupt line only from seeded registers, and only when D_CTRL.DMAE
// allows it. See ADR 0153.
int test_dmac_device_interrupt_pending() {
    EeState state{};
    // Neither register seeded: nothing is asserted.
    if (state.memory.dmac.interrupt_pending()) return 1;

    // Seeded but idle: D_CTRL.DMAE is clear, so the line stays low even with a
    // matched status/mask pair.
    state.memory.dmac.seed_ctrl(0U);
    state.memory.dmac.seed_d_stat((1U << 5U) | (1U << 21U));
    if (state.memory.dmac.interrupt_pending()) return 2;

    // D_CTRL.DMAE set: the SIF0 status/mask pair asserts the line.
    state.memory.dmac.seed_ctrl(reps2::runtime::kEeDmacCtrlDmaeMask);
    if (!state.memory.dmac.interrupt_pending()) return 3;

    // The bus-error status bit asserts without a matching mask.
    state.memory.dmac.seed_d_stat(reps2::runtime::kEeDmacDStatBusErrorStatusBit);
    if (!state.memory.dmac.interrupt_pending()) return 4;

    // An idle seeded D_STAT asserts nothing, and neither does a status bit whose
    // mask is clear.
    state.memory.dmac.seed_d_stat(0U);
    if (state.memory.dmac.interrupt_pending()) return 5;
    state.memory.dmac.seed_d_stat(1U << 5U);
    if (state.memory.dmac.interrupt_pending()) return 6;

    // An unseeded D_CTRL still reports nothing even with an asserting D_STAT.
    EeState half{};
    half.memory.dmac.seed_d_stat((1U << 5U) | (1U << 21U));
    if (half.memory.dmac.interrupt_pending()) return 7;
    return 0;
}

// A guest starts DMAC channel 1 (VIF0) by setting STR. The documented transfer
// completion behavior clears STR and raises the channel status bit in D_STAT.
// Evidence: guest code at 0x00216DFC writes D1_CHCR=0x145 then waits for
// STR==0 at 0x002433D0. ps2sdk/EE manual: STR self-clears on completion.
int test_chcr1_str_sets_dstat_vif0() {
    EeState state{};
    state.memory.dmac.seed_d_stat(0U);
    std::uint32_t value = 0U;
    if (reps2::runtime::store_u32(state.memory, reps2::runtime::kEeDmacChcr1Address, 0x145U)
        != Status::ok) return 1;
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeDmacChcr1Address, value)
        != Status::ok || (value & 0x100U) != 0U) return 2;
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeDmacDStatAddress, value)
        != Status::ok || (value & (1U << 1U)) == 0U) return 3;
    return 0;
}

// Channel 0 (VIF0) is the next frontier after ADR 0188: the ABI-37 probe
// stopped at 0x002181BC on sw $v1,($s1) with $s1=0x10008020 (D0_QWC).
// PS2SDK ee_regs.h: D0_CHCR/MADR/QWC/TADR/ASR0/ASR1 at 0x10008000..0x10008050.
int test_chcr0_str_sets_dstat_vif0_ch0() {
    EeState state{};
    state.memory.dmac.seed_d_stat(0U);
    std::uint32_t value = 0U;
    if (reps2::runtime::store_u32(state.memory, 0x10008020U, 3U) != Status::ok) return 1;
    if (reps2::runtime::load_u32(state.memory, 0x10008020U, value) != Status::ok || value != 3U) return 2;
    if (reps2::runtime::store_u32(state.memory, 0x10008000U, 0x145U) != Status::ok) return 3;
    if (reps2::runtime::load_u32(state.memory, 0x10008000U, value) != Status::ok || (value & 0x100U) != 0U) return 4;
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeDmacDStatAddress, value)
        != Status::ok || (value & (1U << 0U)) == 0U) return 5;
    return 0;
}

// Same STR completion contract for channel 2 (GIF), guest writes D2_CHCR=0x145
// at 0x0021B748 before polling STR in the same loop.
int test_chcr2_str_sets_dstat_gif() {
    using Ram = std::array<std::uint8_t, 0x20000>;
    Ram ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.memory.dmac.seed_d_stat(0U);
    state.memory.dmac.seed_tadr2(0U); // valid RAM address; payload irrelevant for STR-only path
    state.memory.dmac.seed_madr2(0U);
    std::uint32_t value = 0U;
    if (reps2::runtime::store_u32(state.memory, reps2::runtime::kEeDmacChcr2Address, 0x145U)
        != Status::ok) return 1;
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeDmacChcr2Address, value)
        != Status::ok || (value & 0x100U) != 0U) return 2;
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeDmacDStatAddress, value)
        != Status::ok || (value & (1U << 2U)) == 0U) return 3;
    return 0;
}

// DMAC channel-2 chain-mode transfer pushes tag payloads into the GIF input
// FIFO. A chain is: tag quadword at TADR2 {NLOOP, EOP=0, FLG=PACKED, NREG,
// REGS, ADDR=next_tag} followed by NLOOP quadwords at MADR2; NLOOP==0/EOP
// terminates the chain. Fail-closed: unreadable tag or full FIFO returns an
// error with no partial state change.
// Guest code observed at 0x00210538 writes TADR2 then CHCR2=0x145
// (chain-mode + STR).
int test_chcr2_chain_transfer_pushes_gif_fifo() {
    using Ram = std::array<std::uint8_t, 0x20000>;
    Ram ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.memory.dmac.seed_d_stat(0U);
    constexpr std::uint32_t kTagAddr = 0x0001'0000U;
    constexpr std::uint32_t kDataAddr = 0x0001'0010U;
    constexpr std::uint32_t kNextTagAddr = 0x0001'0040U;
    // Tag word0 = NLOOP(2)=2 | EOP(0) | FLG(00=PACKED) | NREG(0) | REGS(0)
    constexpr std::uint32_t kTagWord0 = 2U;
    // Tag word1 = next tag address (chain continues at kNextTagAddr).
    constexpr std::uint64_t kTagQuad = (static_cast<std::uint64_t>(kNextTagAddr) << 32U)
        | static_cast<std::uint64_t>(kTagWord0);
    // Two payload quadwords at MADR2 (each 16 bytes = 128 bits).
    constexpr std::uint64_t kPayload0_lo = 0x1111'1111'2222'2222ULL;
    constexpr std::uint64_t kPayload0_hi = 0x5555'5555'6666'6666ULL;
    constexpr std::uint64_t kPayload1_lo = 0x3333'3333'4444'4444ULL;
    constexpr std::uint64_t kPayload1_hi = 0x7777'7777'8888'8888ULL;

    state.memory.dmac.seed_madr2(kDataAddr);
    state.memory.dmac.seed_tadr2(kTagAddr);
    state.memory.dmac.seed_chcr2(0x0000'0000U); // STR=0, mode=0 (will be overwritten)

    // Write tag quadword to EE RAM.
    const std::uint8_t* tag_bytes =
        reinterpret_cast<const std::uint8_t*>(&kTagQuad);
    std::copy(tag_bytes, tag_bytes + 16U,
              state.memory.main_ram.begin() + kTagAddr);
    // Write termination tag at kNextTagAddr: NLOOP==0.
    constexpr std::uint64_t kEndTagQuad = 0ULL;
    const std::uint8_t* end_bytes = reinterpret_cast<const std::uint8_t*>(&kEndTagQuad);
    std::copy(end_bytes, end_bytes + 16U,
              state.memory.main_ram.begin() + kNextTagAddr);
    // Write payload quadwords at MADR2 (16 bytes each).
    const std::uint8_t* p0_lo = reinterpret_cast<const std::uint8_t*>(&kPayload0_lo);
    const std::uint8_t* p0_hi = reinterpret_cast<const std::uint8_t*>(&kPayload0_hi);
    const std::uint8_t* p1_lo = reinterpret_cast<const std::uint8_t*>(&kPayload1_lo);
    const std::uint8_t* p1_hi = reinterpret_cast<const std::uint8_t*>(&kPayload1_hi);
    std::copy(p0_lo, p0_lo + 8U, state.memory.main_ram.begin() + kDataAddr);
    std::copy(p0_hi, p0_hi + 8U, state.memory.main_ram.begin() + kDataAddr + 8U);
    std::copy(p1_lo, p1_lo + 8U, state.memory.main_ram.begin() + kDataAddr + 16U);
    std::copy(p1_hi, p1_hi + 8U, state.memory.main_ram.begin() + kDataAddr + 24U);

    // Write CHCR2 with chain-mode + STR: 0x145 = MOD(chain=0x400) | STR(0x100) | DIR(1).
    if (reps2::runtime::store_u32(state.memory, reps2::runtime::kEeDmacChcr2Address, 0x145U)
        != Status::ok) return 10;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeDmacChcr2Address, value)
        != Status::ok || (value & 0x100U) != 0U) return 11; // STR self-cleared
    if (state.memory.gif_fifo_count != 2U) return 12;
    if (state.memory.gif_fifo[0].low != kPayload0_lo) return 13;
    if (state.memory.gif_fifo[0].high != kPayload0_hi) return 14;
    if (state.memory.gif_fifo[1].low != kPayload1_lo) return 15;
    if (state.memory.gif_fifo[1].high != kPayload1_hi) return 16;
    // CHCR STR bit should self-clear after completion.
    if ((value & 0x100U) != 0U) return 16;
    // D_STAT GIF status bit set.
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeDmacDStatAddress, value)
        != Status::ok || (value & (1U << 2U)) == 0U) return 17;
    return 0;
}

// DMAC ch2 chain-mode fails closed when the tag quadword cannot be read.
int test_chcr2_chain_fails_closed_on_unreadable_tag() {
    using Ram = std::array<std::uint8_t, 0x20000>;
    Ram ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.memory.dmac.seed_d_stat(0U);
    state.memory.dmac.seed_madr2(0x0076'0010U);
    state.memory.dmac.seed_tadr2(0xDEAD'0000U); // unmapped
    state.memory.dmac.seed_chcr2(0x0000'0000U);
    // An unreadable tag fails the transfer closed: the channel write reports
    // unsupported_memory_access and pushes nothing into the FIFO.
    if (reps2::runtime::store_u32(state.memory, reps2::runtime::kEeDmacChcr2Address, 0x145U)
        != Status::unsupported_memory_access) return 20;
    if (state.memory.gif_fifo_count != 0U) return 21;
    // STR still self-clears: the channel reached its terminal state even though
    // no data transferred (the error is reported to the guest, not swallowed).
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeDmacChcr2Address, value)
        != Status::ok || (value & 0x100U) != 0U) return 22;
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
    if (const auto result = test_chcr0_str_sets_dstat_vif0_ch0(); result != 0) {
        std::fprintf(stderr, "CHCR0 STR sets D_STAT VIF0 ch0 test failed at check %d\n", result);
        return 105 + result;
    }
    if (const auto result = test_chcr1_str_sets_dstat_vif0(); result != 0) {
        std::fprintf(stderr, "CHCR1 STR sets D_STAT VIF0 test failed at check %d\n", result);
        return 110 + result;
    }
    if (const auto result = test_chcr2_str_sets_dstat_gif(); result != 0) {
        std::fprintf(stderr, "CHCR2 STR sets D_STAT GIF test failed at check %d\n", result);
        return 120 + result;
    }
    if (const auto result = test_idle_boot_state_is_readable(); result != 0) {
        std::fprintf(stderr, "idle boot-state DMAC test failed at check %d\n", result);
        return 80 + result;
    }
    if (const auto result = test_dmac_interrupt_condition(); result != 0) {
        std::fprintf(stderr, "DMAC interrupt condition test failed at check %d\n", result);
        return 90 + result;
    }
    if (const auto result = test_dmac_device_interrupt_pending(); result != 0) {
        std::fprintf(stderr, "DMAC device interrupt-pending test failed at check %d\n", result);
        return 100 + result;
    }
    if (const auto result = test_chcr2_chain_transfer_pushes_gif_fifo(); result != 0) {
        std::fprintf(stderr, "CHCR2 chain transfer to GIF FIFO test failed at check %d\n", result);
        return 130 + result;
    }
    if (const auto result = test_chcr2_chain_fails_closed_on_unreadable_tag(); result != 0) {
        std::fprintf(stderr, "CHCR2 chain fail-closed test failed at check %d\n", result);
        return 140 + result;
    }
    return 0;
}