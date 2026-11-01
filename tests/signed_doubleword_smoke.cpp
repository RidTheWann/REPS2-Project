// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

[[nodiscard]] reps2::runtime::EeState make_state() {
    reps2::runtime::EeState state{};
    state.gpr[0].low = 0xDEADBEEFDEADBEEFULL;
    state.gpr[0].high = 0xBAD0BAD0BAD0BAD0ULL;

    state.gpr[8].low = 10;
    state.gpr[9].low = 20;
    state.gpr[10].low = 0xAAAAAAAAAAAAAAAAULL;
    state.gpr[10].high = 0x1010101010101010ULL;

    state.gpr[12].low = 20;
    state.gpr[13].low = 10;
    state.gpr[11].low = 0xBBBBBBBBBBBBBBBBULL;
    state.gpr[11].high = 0x1111111111111111ULL;

    state.gpr[15].low = 10;
    state.gpr[14].low = 0xCCCCCCCCCCCCCCCCULL;
    state.gpr[14].high = 0x1212121212121212ULL;

    state.gpr[16].low = 1;
    state.gpr[17].low = 2;

    state.gpr[18].low = 3;
    state.gpr[19].low = 4;
    state.gpr[18].high = 0x1818181818181818ULL;
    return state;
}

} // namespace

int main() {
    using reps2::runtime::Status;
    const auto has_overflow_frame = [](const reps2::runtime::EeState& state, std::uint32_t fault_pc) {
        return state.pc == reps2::runtime::kEeCommonExceptionVector
            && state.cop0[reps2::runtime::kCop0Epc] == fault_pc
            && (state.cop0[reps2::runtime::kCop0Cause]
                & reps2::runtime::kCop0CauseExcCodeMask) == (12U << 2U)
            && (state.cop0[reps2::runtime::kCop0Cause]
                & reps2::runtime::kCop0CauseBranchDelayMask) == 0U
            && (state.cop0[reps2::runtime::kCop0Status]
                & reps2::runtime::kCop0StatusExlMask) != 0U;
    };

    // Normal signed arithmetic: 10 + 20 = 30, 20 - 10 = 10,
    // and 10 + sign_extend(0xFFFE) = 8.
    auto state = make_state();
    if (reps2::generated::execute(state) != Status::ok) return 1;
    if (state.gpr[10].low != 30ULL) return 2;
    if (state.gpr[11].low != 10ULL) return 3;
    if (state.gpr[14].low != 8ULL) return 4;
    if (state.gpr[18].low != 7ULL) return 5; // DADD rd==rs is alias-safe.
    if (state.gpr[0].low != 0 || state.gpr[0].high != 0) return 6;
    if (state.gpr[10].high != 0x1010101010101010ULL
        || state.gpr[11].high != 0x1111111111111111ULL
        || state.gpr[14].high != 0x1212121212121212ULL
        || state.gpr[18].high != 0x1818181818181818ULL) return 7;

    // DADD overflow: INT64_MAX + 1. The faulting destination is not committed.
    auto add_overflow = make_state();
    add_overflow.gpr[8].low = 0x7FFFFFFFFFFFFFFFULL;
    add_overflow.gpr[9].low = 1ULL;
    add_overflow.gpr[10].low = 0xAAAAAAAAAAAAAAAAULL;
    if (reps2::generated::execute(add_overflow) != Status::exception_delivered\n        || !has_overflow_frame(add_overflow, 0xB000U)) return 8;
    if (add_overflow.gpr[10].low != 0xAAAAAAAAAAAAAAAAULL) return 9;

    // DSUB overflow: INT64_MIN - 1. The preceding DADD commits; DSUB does not.
    auto sub_overflow = make_state();
    sub_overflow.gpr[12].low = 0x8000000000000000ULL;
    sub_overflow.gpr[13].low = 1ULL;
    sub_overflow.gpr[11].low = 0xBBBBBBBBBBBBBBBBULL;
    if (reps2::generated::execute(sub_overflow) != Status::exception_delivered\n        || !has_overflow_frame(sub_overflow, 0xB004U)) return 10;
    if (sub_overflow.gpr[10].low != 30ULL) return 11;
    if (sub_overflow.gpr[11].low != 0xBBBBBBBBBBBBBBBBULL) return 12;

    // DADDI overflow: INT64_MIN + sign_extend(-2).
    auto addi_overflow = make_state();
    addi_overflow.gpr[15].low = 0x8000000000000000ULL;
    addi_overflow.gpr[14].low = 0xCCCCCCCCCCCCCCCCULL;
    if (reps2::generated::execute(addi_overflow) != Status::exception_delivered\n        || !has_overflow_frame(addi_overflow, 0xB008U)) return 13;
    if (addi_overflow.gpr[10].low != 30ULL || addi_overflow.gpr[11].low != 10ULL) return 14;
    if (addi_overflow.gpr[14].low != 0xCCCCCCCCCCCCCCCCULL) return 15;

    // Overflow still traps when rd=GPR[0], even though its result is discarded.
    auto zero_destination_overflow = make_state();
    zero_destination_overflow.gpr[16].low = 0x7FFFFFFFFFFFFFFFULL;
    zero_destination_overflow.gpr[17].low = 1ULL;
    if (reps2::generated::execute(zero_destination_overflow) != Status::exception_delivered\n        || !has_overflow_frame(zero_destination_overflow, 0xB00CU)) return 16;
    if (zero_destination_overflow.gpr[0].low != 0
        || zero_destination_overflow.gpr[0].high != 0) return 17;
    if (zero_destination_overflow.gpr[10].low != 30ULL
        || zero_destination_overflow.gpr[11].low != 10ULL
        || zero_destination_overflow.gpr[14].low != 8ULL) return 18;

    return 0;
}
