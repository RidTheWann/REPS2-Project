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

// Bytes are numbered from the low end of each 64-bit lane. PEXTLB emits
// [rt0, rs0, rt1, rs1, ..., rt7, rs7] and ignores both source high lanes.
constexpr Gpr128 kLhs{
    0x1111'2222'3333'4444ULL,
    0xAAAA'BBBB'CCCC'DDDDULL,
};
constexpr Gpr128 kRhs{
    0x5555'6666'7777'8888ULL,
    0xEEEE'FFFF'0000'9999ULL,
};
constexpr Gpr128 kExpected{
    0x3377'3377'4488'4488ULL,
    0x1155'1155'2266'2266ULL,
};
constexpr Gpr128 kLowOnlyLhs{kLhs.low, 0};
constexpr Gpr128 kLowOnlyRhs{kRhs.low, 0};

// Alias cases use distinct source values, preventing wrong register wiring from
// passing accidentally.
constexpr Gpr128 kAliasRhs{0x0000'0000'0000'0102ULL, 0x0000'0000'0000'0304ULL};
constexpr Gpr128 kAliasLhs{0x0807'0605'0403'0201ULL, 0x100F'0E0D'0C0B'0A09ULL};
constexpr Gpr128 kAliasRsExpected{0x3300'3300'4401'4402ULL, 0x1100'1100'2200'2200ULL};
constexpr Gpr128 kAliasRtExpected{0x0477'0377'0288'0188ULL, 0x0855'0755'0666'0566ULL};
constexpr Gpr128 kSelfExpected{0x3333'3333'4444'4444ULL, 0x1111'1111'2222'2222ULL};

static_assert(reps2::runtime::parallel_extend_low_byte(kLhs, kRhs).low == kExpected.low);
static_assert(reps2::runtime::parallel_extend_low_byte(kLhs, kRhs).high == kExpected.high);
static_assert(reps2::runtime::parallel_extend_low_byte(kLowOnlyLhs, kLowOnlyRhs).low ==
              kExpected.low);
static_assert(reps2::runtime::parallel_extend_low_byte(kLowOnlyLhs, kLowOnlyRhs).high ==
              kExpected.high);
static_assert(reps2::runtime::parallel_extend_low_byte(kLhs, kAliasRhs).low ==
              kAliasRsExpected.low);
static_assert(reps2::runtime::parallel_extend_low_byte(kLhs, kAliasRhs).high ==
              kAliasRsExpected.high);
static_assert(reps2::runtime::parallel_extend_low_byte(kAliasLhs, kRhs).low ==
              kAliasRtExpected.low);
static_assert(reps2::runtime::parallel_extend_low_byte(kAliasLhs, kRhs).high ==
              kAliasRtExpected.high);
static_assert(reps2::runtime::parallel_extend_low_byte(kLhs, kLhs).low == kSelfExpected.low);
static_assert(reps2::runtime::parallel_extend_low_byte(kLhs, kLhs).high == kSelfExpected.high);
static_assert(reps2::runtime::parallel_extend_low_byte(kRhs, kLhs).low != kExpected.low);

constexpr Gpr128 kSentinel{0x0F1E'2D3C'4B5A'6978ULL, 0x8796'A5B4'C3D2'E1F0ULL};

} // namespace

int main() {
    EeState state{};
    state.gpr[0] = Gpr128{0xDEAD'BEEF'CAFE'BABEULL, 0xBAD0'BAD0'BAD0'BAD0ULL};
    state.gpr[1] = kLhs;
    state.gpr[2] = kRhs;
    state.gpr[5] = kLhs;
    state.gpr[6] = kAliasRhs;
    state.gpr[8] = kAliasLhs;
    state.gpr[7] = kRhs;
    state.gpr[10] = kLhs;
    state.gpr[20] = kSentinel;

    if (reps2::generated::execute(state) != Status::ok) return 1;
    if (state.gpr[3].low != kExpected.low) return 2;
    if (state.gpr[3].high != kExpected.high) return 3;
    if (state.gpr[5].low != kAliasRsExpected.low) return 4;
    if (state.gpr[5].high != kAliasRsExpected.high) return 5;
    if (state.gpr[7].low != kAliasRtExpected.low) return 6;
    if (state.gpr[7].high != kAliasRtExpected.high) return 7;
    if (state.gpr[10].low != kSelfExpected.low) return 8;
    if (state.gpr[10].high != kSelfExpected.high) return 9;
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 10;
    if (state.gpr[20].low != kSentinel.low || state.gpr[20].high != kSentinel.high) return 11;
    return 0;
}
