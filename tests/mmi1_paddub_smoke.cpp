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

constexpr Gpr128 kLhs{
    0x1010'1010'1010'1010ULL,
    0x1010'1010'1010'1010ULL,
};
constexpr Gpr128 kRhs{
    0x2020'2020'2020'2020ULL,
    0x2020'2020'2020'2020ULL,
};
constexpr Gpr128 kExpected{
    0x3030'3030'3030'3030ULL,
    0x3030'3030'3030'3030ULL,
};

constexpr Gpr128 kSentinel{0x1234'5678'9ABC'DEF0ULL, 0x0BAD'C0DE'F00D'FACEULL};

static_assert(reps2::runtime::parallel_add_u8(kLhs, kRhs).low == kExpected.low);
static_assert(reps2::runtime::parallel_add_u8(kLhs, kRhs).high == kExpected.high);

} // namespace

int main() {
    EeState state{};
    state.gpr[0] = Gpr128{0xDEAD'BEEF'CAFE'BABEULL, 0xBAD0'BAD0'BAD0'BAD0ULL};
    state.gpr[1] = kLhs;
    state.gpr[2] = kRhs;
    state.gpr[3] = kSentinel;
    state.gpr[20] = kSentinel;
    if (reps2::generated::execute(state) != Status::ok) return 1;
    if (state.gpr[3].low != kExpected.low) return 2;
    if (state.gpr[3].high != kExpected.high) return 3;
    if (state.gpr[1].low != kLhs.low || state.gpr[1].high != kLhs.high) return 4;
    if (state.gpr[2].low != kRhs.low || state.gpr[2].high != kRhs.high) return 5;
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 6;
    if (state.gpr[20].low != kSentinel.low || state.gpr[20].high != kSentinel.high) return 7;
    return 0;
}
