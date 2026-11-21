// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded VIF1 FIFO path regression tests (ADR 0181).
#include "reps2/runtime/ee_state.hpp"

#include <cstdio>
#include <vector>

int main() {
    using reps2::runtime::EeState;
    using reps2::runtime::Gpr128;
    using reps2::runtime::Status;
    using reps2::runtime::kEeVif1FifoAddress;
    using reps2::runtime::kEeVif1FifoCapacityQwords;

    EeState state{};
    std::vector<std::uint8_t> ram(reps2::runtime::kEeMainRamSize);
    state.memory.main_ram = ram;

    // Empty reads fail closed and preserve caller output and FIFO state.
    Gpr128 out{0xDEAD'BEEFULL, 0xBAD0BAD0'BAD0BAD0ULL};
    if (reps2::runtime::load_u128(state.memory, kEeVif1FifoAddress, out)
        != Status::unsupported_memory_access) return 1;
    if (out.low != 0xDEAD'BEEFULL || out.high != 0xBAD0BAD0'BAD0BAD0ULL) return 2;
    if (state.memory.vif1_fifo_count != 0U) return 3;

    // SQ enqueues one full 128-bit word. The load path mutates FIFO state, so
    // call the non-const helper directly to mirror generated-code behavior.
    constexpr Gpr128 first{0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL};
    if (reps2::runtime::store_u128(state.memory, kEeVif1FifoAddress, first) != Status::ok) return 4;
    if (state.memory.vif1_fifo_count != 1U) return 5;
    if (reps2::runtime::vif1_fifo_pop(state.memory, out) != Status::ok) return 6;
    if (out.low != first.low || out.high != first.high) return 7;
    if (state.memory.vif1_fifo_count != 0U) return 8;

    // FIFO ordering survives wrap; capacity is 16 qwords; overflow refuses
    // without mutation and existing entries remain readable in order.
    for (std::size_t i = 0; i < kEeVif1FifoCapacityQwords; ++i) {
        const Gpr128 word{static_cast<std::uint64_t>(i + 1U),
                          static_cast<std::uint64_t>(0x100U + i)};
        if (reps2::runtime::store_u128(state.memory, kEeVif1FifoAddress, word) != Status::ok) return 9;
    }
    if (state.memory.vif1_fifo_count != kEeVif1FifoCapacityQwords) return 10;
    if (reps2::runtime::store_u128(state.memory, kEeVif1FifoAddress, first)
        != Status::unsupported_memory_access) return 11;
    if (state.memory.vif1_fifo_count != kEeVif1FifoCapacityQwords) return 12;
    for (std::size_t i = 0; i < kEeVif1FifoCapacityQwords; ++i) {
        if (reps2::runtime::vif1_fifo_pop(state.memory, out) != Status::ok) return 13;
        if (out.low != i + 1U || out.high != 0x100U + i) return 14;
    }
    if (state.memory.vif1_fifo_count != 0U) return 15;
    std::puts("VIF1 FIFO smoke passed: 4 scenarios");
    return 0;
}
