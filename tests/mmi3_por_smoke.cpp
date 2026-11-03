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

// Every decimal digit below has a distinct bit pattern, so a lane that is
// copied instead of OR-ed, or a half that is left untouched, cannot pass.
constexpr Gpr128 kLhs{
    0x1111'2222'3333'4444ULL,
    0xAAAA'BBBB'CCCC'DDDDULL,
};
constexpr Gpr128 kRhs{
    0x5555'6666'7777'8888ULL,
    0xEEEE'FFFF'0000'9999ULL,
};

// POR is the plain 128-bit bitwise OR of both operands.
constexpr Gpr128 kExpected{
    0x5555'6666'7777'CCCCULL,
    0xEEEE'FFFF'CCCC'DDDDULL,
};

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

// por r5, r5, r6 with r5 = kLhs and r6 = kAliasRhs.
constexpr Gpr128 kAliasRsExpected{
    0x1111'2222'3333'4445ULL,
    0xAAAA'BBBB'CCCC'DDDFULL,
};
// por r7, r8, r7 with r8 = kAliasLhs and r7 = kRhs.
constexpr Gpr128 kAliasRtExpected{
    0xD555'6666'7777'8888ULL,
    0xEFEE'FFFF'0000'9999ULL,
};
// Aliasing the destination with both sources reduces to the self-OR, which
// still has to commit the complete 128-bit operand value.
constexpr Gpr128 kSelfExpected = kLhs;

// Only one operand lane is set in each half, so a helper that OR-ed the two
// low halves into both halves, or dropped a half, produces a different value.
constexpr Gpr128 kSplitLhs{0x0000'0000'FFFF'FFFFULL, 0x0ULL};
constexpr Gpr128 kSplitRhs{0x0ULL, 0xFFFF'FFFF'0000'0000ULL};
constexpr Gpr128 kSplitExpected{0x0000'0000'FFFF'FFFFULL, 0xFFFF'FFFF'0000'0000ULL};

static_assert(reps2::runtime::parallel_or(kLhs, kRhs).low == kExpected.low);
static_assert(reps2::runtime::parallel_or(kLhs, kRhs).high == kExpected.high);
static_assert(reps2::runtime::parallel_or(kLhs, kAliasRhs).low == kAliasRsExpected.low);
static_assert(reps2::runtime::parallel_or(kLhs, kAliasRhs).high == kAliasRsExpected.high);
static_assert(reps2::runtime::parallel_or(kAliasLhs, kRhs).low == kAliasRtExpected.low);
static_assert(reps2::runtime::parallel_or(kAliasLhs, kRhs).high == kAliasRtExpected.high);
static_assert(reps2::runtime::parallel_or(kSplitLhs, kSplitRhs).low == kSplitExpected.low);
static_assert(reps2::runtime::parallel_or(kSplitLhs, kSplitRhs).high == kSplitExpected.high);
// POR is commutative, unlike PCPYLD: the operand order only matters for the
// register fields the decoder reports.
static_assert(reps2::runtime::parallel_or(kLhs, kRhs).low ==
              reps2::runtime::parallel_or(kRhs, kLhs).low);
static_assert(reps2::runtime::parallel_or(kLhs, kRhs).high ==
              reps2::runtime::parallel_or(kRhs, kLhs).high);
static_assert(reps2::runtime::parallel_or(kLhs, kLhs).low == kSelfExpected.low);
static_assert(reps2::runtime::parallel_or(kLhs, kLhs).high == kSelfExpected.high);

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

    // POR r3, r1, r2.
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
