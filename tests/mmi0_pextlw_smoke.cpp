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

// The four lower words are all distinct, so a result that reads the wrong
// source word, or copies instead of interleaving, cannot match.
constexpr Gpr128 kLhs{
    0x1111'2222'3333'4444ULL,
    0xAAAA'BBBB'CCCC'DDDDULL,
};
constexpr Gpr128 kRhs{
    0x5555'6666'7777'8888ULL,
    0xEEEE'FFFF'0000'9999ULL,
};

// PEXTLW result words are [rt0, rs0, rt1, rs1]; both high doublewords are
// ignored.
constexpr Gpr128 kExpected{
    0x3333'4444'7777'8888ULL,
    0x1111'2222'5555'6666ULL,
};

// Same low doublewords with the high halves cleared: PEXTLW must produce the
// same value, proving it never reads the upper 64 bits of either source.
constexpr Gpr128 kLowOnlyLhs{kLhs.low, 0};
constexpr Gpr128 kLowOnlyRhs{kRhs.low, 0};

// The aliasing cases deliberately use source registers whose values differ
// from r1/r2, so an instruction word that names the wrong source register
// produces a different result instead of accidentally matching.
constexpr Gpr128 kAliasRhs{
    0x0000'0000'0000'0001ULL,
    0x0000'0000'0000'0002ULL,
};
constexpr Gpr128 kAliasLhs{
    0x8000'0000'0000'0000ULL,
    0x0100'0000'0000'0000ULL,
};

// pextlw r5, r5, r6 with r5 = kLhs and r6 = kAliasRhs.
constexpr Gpr128 kAliasRsExpected{
    0x3333'4444'0000'0001ULL,
    0x1111'2222'0000'0000ULL,
};
// pextlw r7, r8, r7 with r8 = kAliasLhs and r7 = kRhs.
constexpr Gpr128 kAliasRtExpected{
    0x0000'0000'7777'8888ULL,
    0x8000'0000'5555'6666ULL,
};
// Aliasing the destination with both sources duplicates each word.
constexpr Gpr128 kSelfExpected{
    0x3333'4444'3333'4444ULL,
    0x1111'2222'1111'2222ULL,
};

static_assert(reps2::runtime::parallel_extend_low_word(kLhs, kRhs).low == kExpected.low);
static_assert(reps2::runtime::parallel_extend_low_word(kLhs, kRhs).high == kExpected.high);
static_assert(reps2::runtime::parallel_extend_low_word(kLowOnlyLhs, kLowOnlyRhs).low ==
              kExpected.low);
static_assert(reps2::runtime::parallel_extend_low_word(kLowOnlyLhs, kLowOnlyRhs).high ==
              kExpected.high);
static_assert(reps2::runtime::parallel_extend_low_word(kLhs, kAliasRhs).low ==
              kAliasRsExpected.low);
static_assert(reps2::runtime::parallel_extend_low_word(kLhs, kAliasRhs).high ==
              kAliasRsExpected.high);
static_assert(reps2::runtime::parallel_extend_low_word(kAliasLhs, kRhs).low ==
              kAliasRtExpected.low);
static_assert(reps2::runtime::parallel_extend_low_word(kAliasLhs, kRhs).high ==
              kAliasRtExpected.high);
static_assert(reps2::runtime::parallel_extend_low_word(kLhs, kLhs).low == kSelfExpected.low);
static_assert(reps2::runtime::parallel_extend_low_word(kLhs, kLhs).high == kSelfExpected.high);
// PEXTLW is not commutative: swapping the operands swaps the interleaved words.
static_assert(reps2::runtime::parallel_extend_low_word(kRhs, kLhs).low != kExpected.low);

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

    // pextlw r3, r1, r2.
    if (state.gpr[3].low != kExpected.low) return 2;
    if (state.gpr[3].high != kExpected.high) return 3;

    // Destination aliases the first source.
    if (state.gpr[5].low != kAliasRsExpected.low) return 4;
    if (state.gpr[5].high != kAliasRsExpected.high) return 5;

    // Destination aliases the second source.
    if (state.gpr[7].low != kAliasRtExpected.low) return 6;
    if (state.gpr[7].high != kAliasRtExpected.high) return 7;

    // Destination aliases both sources.
    if (state.gpr[10].low != kSelfExpected.low) return 8;
    if (state.gpr[10].high != kSelfExpected.high) return 9;

    // The generated guard suppresses writes to the architecturally-zero GPR.
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 10;

    // A register that is neither a source nor a destination keeps both lanes.
    if (state.gpr[20].low != kSentinel.low) return 11;
    if (state.gpr[20].high != kSentinel.high) return 12;

    return 0;
}
