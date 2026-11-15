// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Gpr128;
using reps2::runtime::Status;

constexpr Gpr128 kSentinel{0x1234'5678'9ABC'DEF0ULL, 0x0BAD'C0DE'F00D'FACEULL};

} // namespace

int main() {
    EeState state{};
    state.gpr[0] = kSentinel;
    state.gpr[3] = Gpr128{3, 0};
    state.gpr[4] = Gpr128{0x0000'0000'8000'0000ULL, 0};
    state.gpr[5] = Gpr128{2, 0};
    state.gpr[6] = kSentinel;
    state.gpr[7] = Gpr128{0xFFFF'FFFF'FFFF'FFF9ULL, 0}; // -7
    state.gpr[8] = Gpr128{2, 0};
    state.gpr[9] = Gpr128{7, 0};
    state.gpr[10] = Gpr128{2, 0};
    state.gpr[11] = kSentinel;
    state.gpr[12] = kSentinel;
    state.gpr[13] = kSentinel;
    state.gpr[14] = kSentinel;
    state.gpr[19] = Gpr128{0xFFFF'FFFF'FFFF'FFFEULL, 0}; // -2
    state.gpr[20] = kSentinel;
    state.lo1 = kSentinel.low;
    state.hi1 = kSentinel.high;

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // mult1 $19, $3 -> rd = rt = 3. (-2 * 3) = -6 = 0xFFFF'FFFF'FFFF'FFFA.
    if (state.gpr[3].low != 0xFFFF'FFFF'FFFF'FFFAULL) return 2;
    // multu1 $4, $5 -> $6. (0x8000_0000 * 2) = 0x1_0000_0000 -> rd.low = 0.
    if (state.gpr[6].low != 0) return 3;
    // div1 -7 / 2 -> mflo1 = -3, mfhi1 = -1.
    if (state.gpr[11].low != 0xFFFF'FFFF'FFFF'FFFDULL) return 4;
    if (state.gpr[12].low != 0xFFFF'FFFF'FFFF'FFFFULL) return 5;
    // divu1 7 / 2 -> mflo1 = 3, mfhi1 = 1.
    if (state.gpr[13].low != 3) return 6;
    if (state.gpr[14].low != 1) return 7;
    // multu1 with rd == 0 must not write GPR[0]; its HI1:LO1 write stands.
    if (state.gpr[0].low != 0 || state.gpr[0].high != 0) return 8;
    // 0x8000_0000 * 2 = 0x1_0000_0000: LO1 = 0, HI1 = 1.
    if (state.lo1 != 0) return 9;
    if (state.hi1 != 1) return 10;
    // Untouched bystander.
    if (state.gpr[20].low != kSentinel.low || state.gpr[20].high != kSentinel.high) return 11;
    return 0;
}