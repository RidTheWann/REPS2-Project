// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <array>
#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

int main() {
    using reps2::runtime::EeState;
    using reps2::runtime::Status;

    EeState state{};
    state.gpr[0].low = 0xDEADBEEF;
    state.gpr[0].high = 0xBAD0BAD0BAD0BAD0ULL;

    state.gpr[1].low = 0x8000000000000000ULL; // INT64_MIN
    state.gpr[2].low = 0x7FFFFFFFFFFFFFFFULL; // INT64_MAX
    state.gpr[3].low = 0xFFFFFFFFFFFFFFFFULL; // UINT64_MAX
    state.gpr[4].low = 0x0F0F0F0F0F0F0F0FULL;
    state.gpr[5].low = 0x3333333333333333ULL;
    state.gpr[1].high = 0x1111222233334444ULL;
    state.gpr[2].high = 0x2222333344445555ULL;
    state.gpr[3].high = 0x3333444455556666ULL;
    state.gpr[4].high = 0x4444555566667777ULL;
    state.gpr[5].high = 0x5555666677778888ULL;

    constexpr std::array<std::uint64_t, 15> high_lanes{
        0xA000000000000006ULL, 0xA000000000000007ULL, 0xA000000000000008ULL,
        0xA000000000000009ULL, 0xA00000000000000AULL, 0xA00000000000000BULL,
        0xA00000000000000CULL, 0xA00000000000000DULL, 0xA00000000000000EULL,
        0xA00000000000000FULL, 0xA000000000000010ULL, 0xA000000000000011ULL,
        0xA000000000000012ULL, 0xA000000000000013ULL, 0xA000000000000014ULL
    };
    for (std::size_t index = 0; index < high_lanes.size(); ++index) {
        state.gpr[index + 6U].high = high_lanes[index];
    }

    state.gpr[15].low = 0xDEADC0DEDEADC0DEULL;

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // Signed and unsigned register comparisons use the complete low 64-bit lane.
    if (state.gpr[6].low != 1ULL) return 2;  // INT64_MIN < INT64_MAX
    if (state.gpr[7].low != 0ULL) return 3;  // 0x8000... > 0x7FFF... unsigned
    if (state.gpr[13].low != 0ULL) return 4; // INT64_MAX < INT64_MIN is false
    if (state.gpr[14].low != 1ULL) return 5; // 0x7FFF... < 0x8000... unsigned

    // SLTI sign-extends its immediate; SLTIU also sign-extends before unsigned compare.
    if (state.gpr[8].low != 1ULL) return 6;  // INT64_MIN < +1
    if (state.gpr[9].low != 0ULL) return 7;  // INT64_MAX < -1 is false
    if (state.gpr[10].low != 1ULL) return 8; // 0 < UINT64_MAX
    if (state.gpr[11].low != 0ULL) return 9; // UINT64_MAX < UINT64_MAX is false
    if (state.gpr[16].low != 1ULL) return 10; // 0x8000... < UINT64_MAX
    if (state.gpr[17].low != 0ULL) return 11; // UINT64_MAX < 0
    if (state.gpr[18].low != 1ULL) return 12; // INT64_MIN < -1
    if (state.gpr[19].low != 0ULL) return 13; // INT64_MAX < -32768 is false
    if (state.gpr[20].low != 0ULL) return 14; // UINT64_MAX < 0xFFFFFFFFFFFF8000 is false

    // NOR is a 64-bit operation; GPR upper 64-bit lanes remain untouched.
    if (state.gpr[12].low != 0xC0C0C0C0C0C0C0C0ULL) return 15;
    if (state.gpr[12].high != high_lanes[6]) return 16;

    // An SLT targeting GPR[0] cannot change the hard-wired zero register.
    if (state.gpr[0].low != 0 || state.gpr[0].high != 0) return 17;

    for (std::size_t index = 0; index < high_lanes.size(); ++index) {
        if (state.gpr[index + 6U].high != high_lanes[index]) return 18;
    }
    if (state.gpr[1].high != 0x1111222233334444ULL
        || state.gpr[2].high != 0x2222333344445555ULL
        || state.gpr[3].high != 0x3333444455556666ULL
        || state.gpr[4].high != 0x4444555566667777ULL
        || state.gpr[5].high != 0x5555666677778888ULL) return 19;
    if (state.gpr[15].low != 0xDEADC0DEDEADC0DEULL) return 20;

    return 0;
}
