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

constexpr Gpr128 kPextInput{
    0x0000'FFFF'0000'801FULL,
    0x0000'7C00'1234'5678ULL,
};
constexpr Gpr128 kPextExpected{
    0x80F8'F8F8'8000'00F8ULL,
    0x00F8'0000'00A8'98C0ULL,
};
constexpr Gpr128 kPpacInput{
    0x80FF'FFFF'1234'5678ULL,
    0xFF00'0000'00F8'F8F8ULL,
};
constexpr Gpr128 kPpacExpected{
    0x0000'FFFF'0000'194FULL,
    0x0000'8000'0000'7FFFULL,
};
constexpr Gpr128 kRoundTripExpected{
    0x0000'FFFF'0000'801FULL,
    0x0000'7C00'0000'5678ULL,
};
constexpr Gpr128 kSentinel{
    0x0F1E'2D3C'4B5A'6978ULL,
    0x8796'A5B4'C3D2'E1F0ULL,
};

static_assert(reps2::runtime::parallel_extend_5(kPextInput).low == kPextExpected.low);
static_assert(reps2::runtime::parallel_extend_5(kPextInput).high == kPextExpected.high);
static_assert(reps2::runtime::parallel_pack_5(kPpacInput).low == kPpacExpected.low);
static_assert(reps2::runtime::parallel_pack_5(kPpacInput).high == kPpacExpected.high);
static_assert(
    reps2::runtime::parallel_pack_5(reps2::runtime::parallel_extend_5(kPextInput)).low
        == kRoundTripExpected.low);
static_assert(
    reps2::runtime::parallel_pack_5(reps2::runtime::parallel_extend_5(kPextInput)).high
        == kRoundTripExpected.high);

} // namespace

int main() {
    EeState state{};
    state.gpr[0] = kSentinel;
    state.gpr[1] = kPpacInput;
    state.gpr[2] = kPextInput;
    state.gpr[5] = kPextInput;
    state.gpr[6] = kPpacInput;
    state.gpr[20] = kSentinel;

    if (reps2::generated::execute(state) != Status::ok) return 1;
    if (state.gpr[3].low != kPextExpected.low) return 2;
    if (state.gpr[3].high != kPextExpected.high) return 3;
    if (state.gpr[7].low != kRoundTripExpected.low) return 4;
    if (state.gpr[7].high != kRoundTripExpected.high) return 5;
    if (state.gpr[4].low != kPpacExpected.low) return 6;
    if (state.gpr[4].high != kPpacExpected.high) return 7;
    if (state.gpr[5].low != kPextExpected.low) return 8;
    if (state.gpr[5].high != kPextExpected.high) return 9;
    if (state.gpr[6].low != kPpacExpected.low) return 10;
    if (state.gpr[6].high != kPpacExpected.high) return 11;
    if (state.gpr[0].low != kSentinel.low || state.gpr[0].high != kSentinel.high) return 12;
    if (state.gpr[20].low != kSentinel.low || state.gpr[20].high != kSentinel.high) return 13;
    return 0;
}
