// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

int main() {
    using reps2::runtime::EeState;
    using reps2::runtime::Status;

    EeState state{};
    state.gpr[19].low = 0xFFFFFFFFFFFFFFF0ULL;
    state.gpr[19].high = 0x1111222233334444ULL;
    state.gpr[20].low = 0x0000000000000030ULL;
    state.gpr[20].high = 0x5555666677778888ULL;
    state.gpr[21].low = 0xFFFFFFFFFFFFFFFFULL;
    state.gpr[23].high = 0xABCDEF0123456789ULL;
    state.gpr[24].high = 0xABCDEF0123456789ULL;
    state.gpr[25].high = 0xABCDEF0123456789ULL;
    state.gpr[26].high = 0xABCDEF0123456789ULL;
    state.gpr[27].high = 0xABCDEF0123456789ULL;
    state.gpr[28].high = 0xABCDEF0123456789ULL;
    state.gpr[29].high = 0xABCDEF0123456789ULL;
    state.gpr[9].high = 0x123456789ABCDEF0ULL;
    state.gpr[0].low = 0xDEADBEEF;
    state.gpr[0].high = 0xBAD0BAD0BAD0BAD0ULL;

    if (reps2::generated::execute(state) != Status::ok) {
        return 1;
    }

    if (state.gpr[0].low != 0 || state.gpr[0].high != 0) {
        return 2;
    }
    if (state.gpr[8].low != 5 || state.gpr[9].low != 7) {
        return 3;
    }
    if (state.gpr[9].high != 0x123456789ABCDEF0ULL) {
        return 4;
    }
    if (state.gpr[10].low != 12) {
        return 5;
    }
    if (state.gpr[11].low != 0xFFFFFFFF80000000ULL) {
        return 6;
    }
    if (state.gpr[12].low != 0x1234ULL) {
        return 7;
    }
    if (state.gpr[13].low != 0xEDCBULL) {
        return 8;
    }
    if (state.gpr[14].low != 0xCBULL) {
        return 9;
    }
    if (state.gpr[15].low != 20 || state.gpr[16].low != 10) {
        return 10;
    }
    if (state.gpr[17].low != 0xFFFFFFFFFFFFFFFFULL) {
        return 11;
    }
    if (state.gpr[18].low != 1) {
        return 12;
    }

    EeState invalid{};
    invalid.gpr[20].low = 0x0000000100000001ULL;
    invalid.gpr[18].low = 0xDEADBEEF;
    const auto invalid_status = reps2::generated::execute(invalid);
    if (invalid_status != Status::undefined_word_operand) {
        return 13;
    }
    if (invalid.gpr[18].low != 0xDEADBEEF) {
        return 14;
    }

    EeState overflow{};
    overflow.gpr[21].low = 0x000000007FFFFFFFULL;
    overflow.gpr[22].low = 0xAABBCCDDULL;
    const auto overflow_status = reps2::generated::execute(overflow);
    if (overflow_status != Status::integer_overflow) {
        return 15;
    }
    if (overflow.gpr[22].low != 0xAABBCCDDULL) {
        return 16;
    }

    if (state.gpr[23].low != 0x20ULL) return 17; // DADDU wraps modulo 2^64.
    if (state.gpr[24].low != 0xFFFFFFFFFFFFFFC0ULL) return 18; // DSUBU.
    if (state.gpr[25].low != 0xFFFFFFFFFFFFFFE0ULL) return 19; // DADDIU sign-extends its immediate.
    if (state.gpr[26].low != 0x300ULL) return 20; // DSLL.
    if (state.gpr[27].low != 0xFFFFFFFFULL) return 21; // DSRL32.
    if (state.gpr[28].low != 0xFFFFFFFFFFFFFFFFULL) return 22; // DSRA32.
    if (state.gpr[29].low != 0ULL) return 23; // DADDIU unsigned wraparound.
    if (state.gpr[23].high != 0xABCDEF0123456789ULL
        || state.gpr[24].high != 0xABCDEF0123456789ULL
        || state.gpr[25].high != 0xABCDEF0123456789ULL
        || state.gpr[26].high != 0xABCDEF0123456789ULL
        || state.gpr[27].high != 0xABCDEF0123456789ULL
        || state.gpr[28].high != 0xABCDEF0123456789ULL
        || state.gpr[29].high != 0xABCDEF0123456789ULL) return 24;
    if (state.gpr[19].high != 0x1111222233334444ULL
        || state.gpr[20].high != 0x5555666677778888ULL) return 25;

    return 0;
}
