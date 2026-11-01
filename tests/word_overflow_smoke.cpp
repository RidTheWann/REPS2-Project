// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

[[nodiscard]] bool has_overflow_frame(
    const reps2::runtime::EeState& state,
    std::uint32_t fault_pc
) noexcept {
    using namespace reps2::runtime;
    return state.pc == kEeCommonExceptionVector
        && state.cop0[kCop0Epc] == fault_pc
        && (state.cop0[kCop0Cause] & kCop0CauseExcCodeMask) == (12U << 2U)
        && (state.cop0[kCop0Cause] & kCop0CauseBranchDelayMask) == 0U
        && (state.cop0[kCop0Status] & kCop0StatusExlMask) != 0U;
}

[[nodiscard]] reps2::runtime::EeState make_state() {
    reps2::runtime::EeState state{};
    state.gpr[10].low = 0xAAAAAAAAAAAAAAAAULL;
    state.gpr[11].low = 0xBBBBBBBBBBBBBBBBULL;
    state.gpr[14].low = 0xCCCCCCCCCCCCCCCCULL;
    return state;
}

} // namespace

int main() {
    using reps2::runtime::Status;

    // Normal execution reaches the end with the expected signed word results.
    auto normal = make_state();
    normal.gpr[8].low = 2U;
    normal.gpr[9].low = 3U;
    normal.gpr[12].low = 9U;
    normal.gpr[13].low = 4U;
    normal.gpr[15].low = 9U;
    if (reps2::generated::execute(normal) != Status::ok) return 1;
    if (normal.gpr[10].low != 5U || normal.gpr[11].low != 5U
        || normal.gpr[14].low != 10U) return 2;
    if (normal.gpr[0].low != 0U || normal.gpr[0].high != 0U) return 3;

    // ADD: INT32_MAX + 1 raises OVERFLOW before writing the destination.
    auto add_overflow = make_state();
    add_overflow.gpr[8].low = 0x7FFFFFFFULL;
    add_overflow.gpr[9].low = 1U;
    if (reps2::generated::execute(add_overflow) != Status::exception_delivered
        || !has_overflow_frame(add_overflow, 0xC000U)) return 4;
    if (add_overflow.gpr[10].low != 0xAAAAAAAAAAAAAAAAULL) return 5;

    // SUB: INT32_MIN - 1 raises OVERFLOW at its own guest PC.
    auto sub_overflow = make_state();
    sub_overflow.gpr[8].low = 0U;
    sub_overflow.gpr[9].low = 0U;
    sub_overflow.gpr[12].low = 0xFFFFFFFF80000000ULL;
    sub_overflow.gpr[13].low = 1U;
    if (reps2::generated::execute(sub_overflow) != Status::exception_delivered
        || !has_overflow_frame(sub_overflow, 0xC004U)) return 6;
    if (sub_overflow.gpr[11].low != 0xBBBBBBBBBBBBBBBBULL) return 7;

    // ADDI: INT32_MAX + sign_extend(1) raises OVERFLOW.
    auto addi_overflow = make_state();
    addi_overflow.gpr[8].low = 0U;
    addi_overflow.gpr[9].low = 0U;
    addi_overflow.gpr[12].low = 1U;
    addi_overflow.gpr[13].low = 1U;
    addi_overflow.gpr[15].low = 0x7FFFFFFFULL;
    if (reps2::generated::execute(addi_overflow) != Status::exception_delivered
        || !has_overflow_frame(addi_overflow, 0xC008U)) return 8;
    if (addi_overflow.gpr[14].low != 0xCCCCCCCCCCCCCCCCULL) return 9;

    // ADD still traps when rd is GPR[0], despite suppressing its write.
    auto zero_destination_overflow = make_state();
    zero_destination_overflow.gpr[8].low = 0U;
    zero_destination_overflow.gpr[9].low = 0U;
    zero_destination_overflow.gpr[12].low = 1U;
    zero_destination_overflow.gpr[13].low = 1U;
    zero_destination_overflow.gpr[15].low = 0U;
    zero_destination_overflow.gpr[16].low = 0x7FFFFFFFULL;
    zero_destination_overflow.gpr[17].low = 1U;
    if (reps2::generated::execute(zero_destination_overflow) != Status::exception_delivered
        || !has_overflow_frame(zero_destination_overflow, 0xC00CU)) return 10;
    if (zero_destination_overflow.gpr[0].low != 0U
        || zero_destination_overflow.gpr[0].high != 0U) return 11;

    return 0;
}
