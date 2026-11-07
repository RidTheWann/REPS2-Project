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

// Independent bit-routing oracle from the manual's concatenation diagram,
// not the runtime mask/shift expressions. Check every input bit in every word,
// including ignored upper halfwords and truncated channel/alpha bits.
bool bit_routes() {
    constexpr std::array<unsigned, 16> positions{
        3, 4, 5, 6, 7, 11, 12, 13, 14, 15, 19, 20, 21, 22, 23, 31};
    for (unsigned lane = 0; lane < 4; ++lane) {
        for (unsigned bit = 0; bit < 32; ++bit) {
            Gpr128 input{}, extended{}, packed{};
            auto& in = lane < 2 ? input.low : input.high;
            auto& ext = lane < 2 ? extended.low : extended.high;
            auto& pack = lane < 2 ? packed.low : packed.high;
            const unsigned offset = (lane % 2) * 32;
            in = std::uint64_t{1} << (offset + bit);
            if (bit < 16) ext = std::uint64_t{1} << (offset + positions[bit]);
            for (unsigned i = 0; i < positions.size(); ++i) {
                if (positions[i] == bit) pack = std::uint64_t{1} << (offset + i);
            }
            const auto actual_ext = reps2::runtime::parallel_extend_5(input);
            const auto actual_pack = reps2::runtime::parallel_pack_5(input);
            if (actual_ext.low != extended.low || actual_ext.high != extended.high
                || actual_pack.low != packed.low || actual_pack.high != packed.high) return false;
        }
    }
    return true;
}

} // namespace

int main() {
    if (!bit_routes()) return 14;
    EeState state{};
    state.gpr[0] = kSentinel;
    state.gpr[1] = kPpacInput;
    state.gpr[2] = kPextInput;
    state.gpr[5] = kPextInput;
    state.gpr[6] = kPpacInput;
    state.gpr[20] = kSentinel;
    state.gpr[8] = kSentinel;
    state.gpr[9] = kSentinel;
    state.hi = 0x01234567;
    state.lo = 0x89ABCDEF;
    state.hi1 = 0x76543210;
    state.lo1 = 0xFEDCBA98;
    state.sa = 123;

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
    // The generated entry normalizes GPR0 even if the host supplied garbage.
    if (state.gpr[0].low != 0 || state.gpr[0].high != 0) return 12;
    if (state.gpr[20].low != kSentinel.low || state.gpr[20].high != kSentinel.high) return 13;
    if (state.gpr[8].low != 0 || state.gpr[8].high != 0
        || state.gpr[9].low != 0 || state.gpr[9].high != 0) return 15;
    if (state.hi != 0x01234567 || state.lo != 0x89ABCDEF
        || state.hi1 != 0x76543210 || state.lo1 != 0xFEDCBA98 || state.sa != 123) return 16;
    if (state.gpr[1].low != kPpacInput.low || state.gpr[1].high != kPpacInput.high
        || state.gpr[2].low != kPextInput.low || state.gpr[2].high != kPextInput.high) return 17;
    return 0;
}
