// SPDX-License-Identifier: GPL-3.0-or-later
//
// EE INTC (I_STAT / I_MASK) register-model smoke test.
//
// The generated fixture is a seven-word program that touches both registers:
//   0x0000F000  lui  $v0, 0x1000
//   0x0000F004  ori  $v0, $v0, 0xF000   ; I_STAT
//   0x0000F008  lw   $v1, 0($v0)        ; read pending status
//   0x0000F00C  sw   $v1, 0($v0)        ; write-one-to-clear the bits read
//   0x0000F010  ori  $v0, $v0, 0x0010   ; I_MASK
//   0x0000F014  lw   $v1, 0($v0)        ; read the enable mask
//   0x0000F018  sw   $v1, 0($v0)        ; write-one-to-toggle the bits read
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

constexpr std::uint32_t kIntcStat = reps2::runtime::kEeIntcStatAddress;
constexpr std::uint32_t kIntcMask = reps2::runtime::kEeIntcMaskAddress;
constexpr std::uint32_t kFirstAccessPc = 0x0000'F008U;

int test_unknown_state_fails_closed() {
    EeState state{};
    state.gpr[3].low = 0xA5A5'5A5AU;

    // The guest's first MMIO access is the I_STAT load; an unseeded INTC must
    // stop there without writing the destination.
    if (reps2::generated::execute(state) != Status::unsupported_memory_access) return 1;
    if (state.pc != kFirstAccessPc || state.gpr[3].low != 0xA5A5'5A5AU) return 2;

    std::uint32_t value = 0xA5A5'5A5AU;
    if (reps2::runtime::load_u32(state.memory, kIntcStat, value) != Status::unsupported_memory_access)
        return 3;
    if (value != 0xA5A5'5A5AU) return 4;
    if (reps2::runtime::store_u32(state.memory, kIntcStat, 0x01U) != Status::unsupported_memory_access)
        return 5;
    if (reps2::runtime::load_u32(state.memory, kIntcMask, value) != Status::unsupported_memory_access)
        return 6;
    if (reps2::runtime::store_u32(state.memory, kIntcMask, 0x01U) != Status::unsupported_memory_access)
        return 7;
    if (state.memory.intc.i_stat_known || state.memory.intc.i_mask_known) return 8;
    if (state.memory.intc.i_stat != 0U || state.memory.intc.i_mask != 0U) return 9;
    // An unseeded controller never reports an asserted line.
    if (state.memory.intc.line_asserted()) return 10;
    return 0;
}

int test_seeded_guest_path() {
    EeState state{};
    constexpr std::uint32_t initial_stat = (1U << 0U) | (1U << 13U);
    constexpr std::uint32_t initial_mask = (1U << 0U) | (1U << 1U) | (1U << 13U);
    state.memory.intc.seed_stat(initial_stat);
    state.memory.intc.seed_mask(initial_mask);

    if (reps2::generated::execute(state) != Status::ok) return 20;

    // The guest read I_STAT and wrote the value back: write-one-to-clear removes
    // exactly the bits it read, so the pending register ends at zero.
    std::uint32_t value = 0xFFFF'FFFFU;
    if (reps2::runtime::load_u32(state.memory, kIntcStat, value) != Status::ok) return 21;
    if (value != 0U) return 22;

    // The guest read I_MASK and wrote the value back: write-one-to-toggle flips
    // those bits, so the mask ends at zero.
    if (reps2::runtime::load_u32(state.memory, kIntcMask, value) != Status::ok) return 23;
    if (value != 0U) return 24;
    return 0;
}

int test_write_semantics_are_distinct() {
    EeState state{};
    state.memory.intc.seed_stat(0x0000'2003U); // bits 0, 1, 13
    state.memory.intc.seed_mask(0x0000'0000U);

    // I_STAT write-one-to-clear clears only the written bits.
    if (reps2::runtime::store_u32(state.memory, kIntcStat, 1U << 1U) != Status::ok) return 30;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(state.memory, kIntcStat, value) != Status::ok) return 31;
    if (value != ((1U << 0U) | (1U << 13U))) return 32;
    // Writing zero changes nothing.
    if (reps2::runtime::store_u32(state.memory, kIntcStat, 0U) != Status::ok) return 33;
    if (reps2::runtime::load_u32(state.memory, kIntcStat, value) != Status::ok) return 34;
    if (value != ((1U << 0U) | (1U << 13U))) return 35;

    // I_MASK write-one-to-toggle flips only the low 16 bits; a bit-16 write is
    // ignored entirely.
    if (reps2::runtime::store_u32(state.memory, kIntcMask, (1U << 2U)) != Status::ok) return 36;
    if (reps2::runtime::load_u32(state.memory, kIntcMask, value) != Status::ok) return 37;
    if (value != (1U << 2U)) return 38;
    if (reps2::runtime::store_u32(state.memory, kIntcMask, (1U << 2U)) != Status::ok) return 39;
    if (reps2::runtime::load_u32(state.memory, kIntcMask, value) != Status::ok) return 40;
    if (value != 0U) return 41;
    if (reps2::runtime::store_u32(state.memory, kIntcMask, (1U << 16U)) != Status::ok) return 42;
    if (reps2::runtime::load_u32(state.memory, kIntcMask, value) != Status::ok) return 43;
    if (value != 0U) return 44;
    return 0;
}

int test_source_raise_and_line() {
    EeState state{};
    state.memory.intc.seed_stat(0U);
    state.memory.intc.seed_mask(0U);

    // A pending source with no enable bit does not assert the line.
    if (!state.memory.intc.raise_source(reps2::runtime::kEeIntcSourceSbus)) return 50;
    if (state.memory.intc.line_asserted()) return 51;

    // Enabling the source asserts the line once both bits are set.
    if (reps2::runtime::store_u32(state.memory, kIntcMask, 1U << reps2::runtime::kEeIntcSourceSbus)
        != Status::ok) return 52;
    if (!state.memory.intc.line_asserted()) return 53;

    // Acknowledging the source clears the line.
    if (reps2::runtime::store_u32(state.memory, kIntcStat, 1U << reps2::runtime::kEeIntcSourceSbus)
        != Status::ok) return 54;
    if (state.memory.intc.line_asserted()) return 55;

    // Raising on an unseeded controller fails closed.
    EeState fresh{};
    if (fresh.memory.intc.raise_source(0U)) return 56;
    return 0;
}

int test_idle_boot_state_is_readable() {
    EeState state{};
    // The HLE boot environment seeds the kernel-initialized idle INTC state.
    reps2::runtime::initialize_hle_boot_environment(state);

    std::uint32_t value = 0xA5A5'5A5AU;
    if (reps2::runtime::load_u32(state.memory, kIntcStat, value) != Status::ok) return 60;
    if (value != 0U) return 61;
    if (reps2::runtime::load_u32(state.memory, kIntcMask, value) != Status::ok) return 62;
    if (value != 0U) return 63;
    if (state.memory.intc.line_asserted()) return 64;

    // Pending but masked stays deasserted until the source is enabled.
    if (!state.memory.intc.raise_source(reps2::runtime::kEeIntcSourceSfifo)) return 65;
    if (state.memory.intc.line_asserted()) return 66;
    if (reps2::runtime::store_u32(state.memory, kIntcMask, 1U << reps2::runtime::kEeIntcSourceSfifo)
        != Status::ok) return 67;
    if (!state.memory.intc.line_asserted()) return 68;
    return 0;
}

} // namespace

int main() {
    if (const auto result = test_unknown_state_fails_closed(); result != 0) {
        std::fprintf(stderr, "unknown-state INTC test failed at check %d\n", result);
        return 10 + result;
    }
    if (const auto result = test_seeded_guest_path(); result != 0) {
        std::fprintf(stderr, "seeded INTC guest-path test failed at check %d\n", result);
        return 30 + result;
    }
    if (const auto result = test_write_semantics_are_distinct(); result != 0) {
        std::fprintf(stderr, "INTC write-semantics test failed at check %d\n", result);
        return 50 + result;
    }
    if (const auto result = test_source_raise_and_line(); result != 0) {
        std::fprintf(stderr, "INTC source/line test failed at check %d\n", result);
        return 70 + result;
    }
    if (const auto result = test_idle_boot_state_is_readable(); result != 0) {
        std::fprintf(stderr, "idle boot-state INTC test failed at check %d\n", result);
        return 90 + result;
    }
    return 0;
}