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

constexpr Gpr128 kSrc{
    0x1111'2222'3333'4444ULL,
    0xAAAA'BBBB'CCCC'DDDDULL,
};

// PROT3W result words are [rt1, rt2, rt0, rt3].
constexpr Gpr128 kExpected{
    0xCCCC'DDDD'1111'2222ULL,
    0xAAAA'BBBB'3333'4444ULL,
};

constexpr Gpr128 kSentinel{0x1234'5678'9ABC'DEF0ULL, 0x0BAD'C0DE'F00D'FACEULL};

static_assert(reps2::runtime::parallel_rotate_3_words(kSrc).low == kExpected.low);
static_assert(reps2::runtime::parallel_rotate_3_words(kSrc).high == kExpected.high);

} // namespace

int main() {
    EeState state{};
    state.gpr[0] = Gpr128{0xDEAD'BEEF'CAFE'BABEULL, 0xBAD0'BAD0'BAD0'BAD0ULL};
    state.gpr[2] = kSrc;
    state.gpr[3] = kSentinel;
    state.gpr[20] = kSentinel;
    if (reps2::generated::execute(state) != Status::ok) return 1;
    if (state.gpr[3].low != kExpected.low) return 2;
    if (state.gpr[3].high != kExpected.high) return 3;
    if (state.gpr[2].low != kSrc.low || state.gpr[2].high != kSrc.high) return 4;
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 5;
    if (state.gpr[20].low != kSentinel.low || state.gpr[20].high != kSentinel.high) return 6;
    return 0;
}
