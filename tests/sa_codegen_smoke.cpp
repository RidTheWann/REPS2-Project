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

constexpr std::uint64_t kMtsaSource = 0xDEAD'BEEF'CAFE'0001ULL;

// QFSRV operands are chosen so every expected lane is hand-computable:
// GPR[7] (funnel low 128) = {1, 2} and GPR[5] (funnel high 128) = {3, 4}.
constexpr Gpr128 kFunnelLower{0x0000'0000'0000'0001ULL, 0x0000'0000'0000'0002ULL};
constexpr Gpr128 kFunnelUpper{0x0000'0000'0000'0003ULL, 0x0000'0000'0000'0004ULL};

void preset(EeState& state) {
    // A non-zero GPR[0] proves the generated prologue and MFSA both keep the
    // hard-wired zero register unwritable.
    state.gpr[0] = Gpr128{0xDEAD'BEEFULL, 0xBAD0BAD0'BAD0BAD0ULL};
    state.gpr[1] = Gpr128{kMtsaSource, 0x1111'2222'3333'4444ULL};
    state.gpr[2] = Gpr128{0ULL, 0x2222'3333'4444'5555ULL};
    // MTSAB input: (0x4 ^ 0xF) * 8 = 11 * 8 = 88 bits.
    state.gpr[3] = Gpr128{0x0000'0000'0000'0014ULL, 0x3333'4444'5555'6666ULL};
    state.gpr[5] = kFunnelUpper;
    // A dirty destination proves QFSRV writes both 64-bit lanes.
    state.gpr[6] = Gpr128{0xFFFF'FFFF'FFFF'FFFFULL, 0xFFFF'FFFF'FFFF'FFFFULL};
    state.gpr[7] = kFunnelLower;
    // MTSAH input: (0x3 ^ 0x7) * 16 = 4 * 16 = 64 bits.
    state.gpr[8] = Gpr128{0x0000'0000'0000'0023ULL, 0x8888'9999'AAAA'BBBBULL};
    state.gpr[9] = Gpr128{0ULL, 0x9999'AAAA'BBBB'CCCCULL};
    state.gpr[10] = Gpr128{0xFFFF'FFFF'FFFF'FFFFULL, 0xFFFF'FFFF'FFFF'FFFFULL};
    // MTSAB input where the nibble equals the immediate: (5 ^ 5) * 8 = 0.
    state.gpr[11] = Gpr128{0x0000'0000'0000'0005ULL, 0xCCCC'DDDD'EEEE'FFFFULL};
    state.gpr[12] = Gpr128{0xFFFF'FFFF'FFFF'FFFFULL, 0xFFFF'FFFF'FFFF'FFFFULL};
}

} // namespace

int main() {
    EeState state{};
    preset(state);

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // MTSA/MFSA move the complete 64-bit low lane, and MFSA leaves GPR[127:64].
    if (state.gpr[2].low != kMtsaSource) return 2;
    if (state.gpr[2].high != 0x2222'3333'4444'5555ULL) return 3;

    // QFSRV with SA = 88 shifts across the 128-bit funnel boundary.
    if (state.gpr[6].low != 0x0000'0300'0000'0000ULL) return 4;
    if (state.gpr[6].high != 0x0000'0400'0000'0000ULL) return 5;

    // QFSRV reads SA without consuming it, so MFSA still observes the MTSAB value.
    if (state.gpr[9].low != 88ULL) return 6;
    if (state.gpr[9].high != 0x9999'AAAA'BBBB'CCCCULL) return 7;

    // QFSRV with SA = 64 selects whole 64-bit words from the funnel.
    if (state.gpr[10].low != kFunnelLower.high) return 8;
    if (state.gpr[10].high != kFunnelUpper.low) return 9;

    // QFSRV with SA = 0 copies the lower quadword operand unchanged.
    if (state.gpr[12].low != kFunnelLower.low) return 10;
    if (state.gpr[12].high != kFunnelLower.high) return 11;

    // MTSA and MTSAB leave SA as a bit count, not a byte or halfword index.
    if (state.sa != 0ULL) return 12;
    if (state.gpr[1].low != kMtsaSource) return 13;

    // MFSA targeting GPR[0] must not create a writable register.
    if (state.gpr[0].low != 0ULL || state.gpr[0].high != 0ULL) return 14;

    return 0;
}
