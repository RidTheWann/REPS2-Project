// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <array>
#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Gpr128;
using reps2::runtime::Status;

// Every decimal digit below has a distinct bit pattern, so a lane that is
// OR-ed instead of NOR-ed, or a half that is left untouched, cannot pass.
constexpr Gpr128 kLhs{
    0x1111'2222'3333'4444ULL,
    0xAAAA'BBBB'CCCC'DDDDULL,
};
constexpr Gpr128 kRhs{
    0x5555'6666'7777'8888ULL,
    0xEEEE'FFFF'0000'9999ULL,
};

// PNOR is the plain 128-bit bitwise NOR (NOT-OR) of both operands.
constexpr Gpr128 kExpected{
    0xAAAA'9999'8888'3333ULL,
    0x1111'0000'3333'2222ULL,
};

// The aliasing cases deliberately use source registers whose values differ
// from r1/r2, so an instruction word that names the wrong source register
// produces a different result instead of accidentally matching.
constexpr Gpr128 kAliasRhs{
    0x00FF'00FF'00FF'00FFULL,
    0xFF00'FF00'FF00'FF00ULL,
};
constexpr Gpr128 kAliasLhs{
    0x0F0F'0F0F'0F0F'0F0FULL,
    0xF0F0'F0F0'F0F0'F0F0ULL,
};

// pnor r5, r5, r6 with r5 = kLhs and r6 = kAliasRhs.
constexpr Gpr128 kAliasRsExpected{
    0xEE00'DD00'CC00'BB00ULL,
    0x0055'0044'0033'0022ULL,
};
// pnor r7, r8, r7 with r8 = kAliasLhs and r7 = kRhs.
constexpr Gpr128 kAliasRtExpected{
    0xA0A0'9090'8080'7070ULL,
    0x0101'0000'0F0F'0606ULL,
};
// Aliasing the destination with both sources reduces to the self-NOR, whose
// complete 128-bit result is the bitwise NOT of the operand.
constexpr Gpr128 kSelfExpected{
    0xEEEE'DDDD'CCCC'BBBBULL,
    0x5555'4444'3333'2222ULL,
};

// Each half is all ones, so a helper that OR-ed the two low halves into both
// halves, or dropped a half, produces a nonzero result where zero is expected.
constexpr Gpr128 kSplitLhs{0xFFFF'FFFF'FFFF'FFFFULL, 0x0ULL};
constexpr Gpr128 kSplitRhs{0xFFFF'FFFF'0000'0000ULL, 0xFFFF'FFFF'FFFF'FFFFULL};
constexpr Gpr128 kSplitExpected{0x0ULL, 0x0ULL};

static_assert(reps2::runtime::parallel_nor(kLhs, kRhs).low == kExpected.low);
static_assert(reps2::runtime::parallel_nor(kLhs, kRhs).high == kExpected.high);
static_assert(reps2::runtime::parallel_nor(kLhs, kAliasRhs).low == kAliasRsExpected.low);
static_assert(reps2::runtime::parallel_nor(kLhs, kAliasRhs).high == kAliasRsExpected.high);
static_assert(reps2::runtime::parallel_nor(kAliasLhs, kRhs).low == kAliasRtExpected.low);
static_assert(reps2::runtime::parallel_nor(kAliasLhs, kRhs).high == kAliasRtExpected.high);
static_assert(reps2::runtime::parallel_nor(kSplitLhs, kSplitRhs).low == kSplitExpected.low);
static_assert(reps2::runtime::parallel_nor(kSplitLhs, kSplitRhs).high == kSplitExpected.high);
// PNOR is commutative, unlike PCPYLD: the operand order only matters for the
// register fields the decoder reports.
static_assert(reps2::runtime::parallel_nor(kLhs, kRhs).low ==
              reps2::runtime::parallel_nor(kRhs, kLhs).low);
static_assert(reps2::runtime::parallel_nor(kLhs, kRhs).high ==
              reps2::runtime::parallel_nor(kRhs, kLhs).high);
static_assert(reps2::runtime::parallel_nor(kLhs, kLhs).low == kSelfExpected.low);
static_assert(reps2::runtime::parallel_nor(kLhs, kLhs).high == kSelfExpected.high);

constexpr Gpr128 kSentinel{0x0F1E'2D3C'4B5A'6978ULL, 0x8796'A5B4'C3D2'E1F0ULL};

} // namespace

int main() {
    EeState state{};
    state.gpr[1] = kLhs;
    state.gpr[2] = kRhs;
    state.gpr[5] = kLhs;
    state.gpr[6] = kAliasRhs;
    state.gpr[8] = kAliasLhs;
    state.gpr[7] = kRhs;
    state.gpr[10] = kLhs;
    state.gpr[20] = kSentinel;

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // PNOR r3, r1, r2.
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
