// SPDX-License-Identifier: GPL-3.0-or-later
// VU0 VU Mem0 MMIO (0x11004000..0x11004FF0) smoke test (ADR 0211).
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Gpr128;
using reps2::runtime::Status;
using reps2::runtime::vu_mem_read_u128;
using reps2::runtime::vu_mem_write_u128;

constexpr std::uint32_t kVuMemBase = 0x1100'4000U;
constexpr std::uint32_t kVuMemSize = 0x1000U;

int run_checks(EeState& state) {
    // Bind VU Mem0 to the state's existing data_mem storage (the engine session
    // does the same in production).
    state.memory.vu_mem = std::span<std::uint8_t>(
        reinterpret_cast<std::uint8_t*>(state.vu0.data_mem.data()),
        state.vu0.data_mem.size() * sizeof(Gpr128));

    constexpr Gpr128 kWriteValue{0x1122'3344'5566'7788ULL,
                                 0x99AA'BBCC'DDEE'FF00ULL};

    if (vu_mem_write_u128(state.memory, kVuMemBase, kWriteValue) != Status::ok) {
        return 101;
    }
    Gpr128 read_back{0ULL, 0ULL};
    if (vu_mem_read_u128(state.memory, kVuMemBase, read_back) != Status::ok) {
        return 102;
    }
    if (read_back.low != kWriteValue.low || read_back.high != kWriteValue.high) {
        return 103;
    }

    // Address below the window must fail closed.
    Gpr128 dummy{0ULL, 0ULL};
    if (vu_mem_read_u128(state.memory, kVuMemBase - 1U, read_back) == Status::ok) {
        return 104;
    }
    if (vu_mem_write_u128(state.memory, kVuMemBase - 1U, kWriteValue) == Status::ok) {
        return 105;
    }

    // Last qword of the window must succeed.
    constexpr std::uint32_t kLastAddr = kVuMemBase + kVuMemSize - 16U;
    if (vu_mem_write_u128(state.memory, kLastAddr, kWriteValue) != Status::ok) {
        return 106;
    }
    read_back = Gpr128{0ULL, 0ULL};
    if (vu_mem_read_u128(state.memory, kLastAddr, read_back) != Status::ok) {
        return 107;
    }
    if (read_back.low != kWriteValue.low || read_back.high != kWriteValue.high) {
        return 108;
    }

    // A write through the public store path must reach data_mem.
    constexpr Gpr128 kPublicValue{0x0102'0304'0506'0708ULL,
                                  0x1112'1314'1516'1718ULL};
    if (reps2::runtime::store_u128(state.memory, kVuMemBase + 0x10U, kPublicValue) != Status::ok) {
        return 109;
    }
    if (state.vu0.data_mem[1].low != kPublicValue.low
        || state.vu0.data_mem[1].high != kPublicValue.high) {
        return 110;
    }

    return 0;
}

}  // namespace

int main() {
    EeState state{};
    std::vector<std::uint8_t> main_ram(reps2::runtime::kEeMainRamSize, std::uint8_t{0});
    state.memory.main_ram = std::span<std::uint8_t>(main_ram.data(), main_ram.size());

    const int result = run_checks(state);
    if (result != 0) {
        std::printf("vu_mem_smoke FAILED: %d\n", result);
        return result;
    }
    std::printf("vu_mem_smoke passed\n");
    return 0;
}
