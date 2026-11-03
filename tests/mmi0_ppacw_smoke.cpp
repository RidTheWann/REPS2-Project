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

// Every word of both operands is distinct, so a result that takes the wrong
// word, the wrong doubleword or the wrong source cannot match.
constexpr Gpr128 kLhs{
    0x1111'2222'3333'4444ULL,
    0xAAAA'BBBB'CCCC'DDDDULL,
};
constexpr Gpr128 kRhs{
    0x5555'6666'7777'8888ULL,
    0xEEEE'FFFF'0000'9999ULL,
};

// PPACW result words are [rt0, rt2, rs0, rs2]: each result doubleword packs the
// low word of both doublewords of one source.
constexpr Gpr128 kExpected{
    0x0000'9999'7777'8888ULL,
    0xCCCC'DDDD'3333'4444ULL,
};

// Same low doublewords with the high halves cleared. PPACW must produce a
// different value, because unlike PEXTLW it reads the low word of each source's
// upper doubleword.
constexpr Gpr128 kNoHighLhs{kLhs.low, 0};
constexpr Gpr128 kNoHighRhs{kRhs.low, 0};
constexpr Gpr128 kExpectedWithoutHigh{
    0x0000'0000'7777'8888ULL,
    0x0000'0000'3333'4444ULL,
};

// The aliasing cases use source registers whose four words all differ, so an
// instruction word that names the wrong source register yields a different
// result instead of matching by accident.
constexpr Gpr128 kAliasRhs{
    0x5555'5555'0000'0001ULL,
    0x7777'7777'0000'0002ULL,
};
constexpr Gpr128 kAliasLhs{
    0x1111'1111'0000'0004ULL,
    0x3333'3333'0000'0008ULL,
};
constexpr Gpr128 kSelf{
    0x0F1E'2D3C'4B5A'6978ULL,
    0x8796'A5B4'C3D2'E1F0ULL,
};

// ppacw r5, r5, r6 with r5 = kLhs and r6 = kAliasRhs.
constexpr Gpr128 kAliasRsExpected{
    0x0000'0002'0000'0001ULL,
    0xCCCC'DDDD'3333'4444ULL,
};
// ppacw r7, r8, r7 with r8 = kAliasLhs and r7 = kRhs.
constexpr Gpr128 kAliasRtExpected{
    0x0000'9999'7777'8888ULL,
    0x0000'0008'0000'0004ULL,
};
// ppacw r10, r10, r10: both result doublewords pack the same register.
constexpr Gpr128 kSelfExpected{
    0xC3D2'E1F0'4B5A'6978ULL,
    0xC3D2'E1F0'4B5A'6978ULL,
};

static_assert(reps2::runtime::parallel_pack_word(kLhs, kRhs).low == kExpected.low);
static_assert(reps2::runtime::parallel_pack_word(kLhs, kRhs).high == kExpected.high);
static_assert(reps2::runtime::parallel_pack_word(kNoHighLhs, kNoHighRhs).low ==
              kExpectedWithoutHigh.low);
static_assert(reps2::runtime::parallel_pack_word(kNoHighLhs, kNoHighRhs).high ==
              kExpectedWithoutHigh.high);
// Clearing the upper doublewords changes the result, so PPACW really reads them.
static_assert(reps2::runtime::parallel_pack_word(kNoHighLhs, kNoHighRhs).low != kExpected.low);
static_assert(reps2::runtime::parallel_pack_word(kNoHighLhs, kNoHighRhs).high !=
              kExpected.high);
static_assert(reps2::runtime::parallel_pack_word(kLhs, kAliasRhs).low == kAliasRsExpected.low);
static_assert(reps2::runtime::parallel_pack_word(kLhs, kAliasRhs).high == kAliasRsExpected.high);
static_assert(reps2::runtime::parallel_pack_word(kAliasLhs, kRhs).low == kAliasRtExpected.low);
static_assert(reps2::runtime::parallel_pack_word(kAliasLhs, kRhs).high == kAliasRtExpected.high);
static_assert(reps2::runtime::parallel_pack_word(kSelf, kSelf).low == kSelfExpected.low);
static_assert(reps2::runtime::parallel_pack_word(kSelf, kSelf).high == kSelfExpected.high);
// PPACW is not commutative: the first operand supplies the result's high half.
static_assert(reps2::runtime::parallel_pack_word(kRhs, kLhs).low != kExpected.low);
static_assert(reps2::runtime::parallel_pack_word(kRhs, kLhs).high != kExpected.high);
// PPACW and PEXTLW select different lanes, so one must not be lowered as the
// other.
static_assert(reps2::runtime::parallel_pack_word(kLhs, kRhs).high !=
              reps2::runtime::parallel_extend_low_word(kLhs, kRhs).high);
// PPACW ignores the second and fourth word of each source: feeding operands
// whose ignored lanes are poisoned must not change the result.
constexpr Gpr128 kOddPoisonLhs{
    0xFFFF'FFFF'3333'4444ULL,
    0xFFFF'FFFF'CCCC'DDDDULL,
};
constexpr Gpr128 kOddPoisonRhs{
    0xFFFF'FFFF'7777'8888ULL,
    0xFFFF'FFFF'0000'9999ULL,
};
static_assert(reps2::runtime::parallel_pack_word(kOddPoisonLhs, kOddPoisonRhs).low ==
              kExpected.low);
static_assert(reps2::runtime::parallel_pack_word(kOddPoisonLhs, kOddPoisonRhs).high ==
              kExpected.high);

constexpr Gpr128 kSentinel{0x1234'5678'9ABC'DEF0ULL, 0x0BAD'C0DE'F00D'FACEULL};

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
    state.gpr[10] = kSelf;
    state.gpr[20] = kSentinel;

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // ppacw r3, r1, r2.
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

    // The sources are not disturbed by an aliasing destination.
    if (state.gpr[1].low != kLhs.low || state.gpr[1].high != kLhs.high) return 10;
    if (state.gpr[2].low != kRhs.low || state.gpr[2].high != kRhs.high) return 11;

    // The generated guard suppresses writes to the architecturally-zero GPR.
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 12;

    // A register that is neither a source nor a destination keeps both lanes.
    if (state.gpr[20].low != kSentinel.low) return 13;
    if (state.gpr[20].high != kSentinel.high) return 14;

    return 0;
}
