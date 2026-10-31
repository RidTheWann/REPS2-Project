// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute_cfg(
    reps2::runtime::EeState& state,
    std::uint64_t instruction_budget
) noexcept;
}

int main() {
    using reps2::runtime::EeState;
    using reps2::runtime::Status;

    // Conditional branch not taken: the delay slot and fallthrough block run,
    // then a direct jump's delay slot runs before the return.
    EeState not_taken{};
    not_taken.pc = 0x1000;
    not_taken.gpr[8].low = 1;
    not_taken.gpr[31].low = 0x1020;
    const auto not_taken_status = reps2::generated::execute_cfg(not_taken, 64);
    if (not_taken_status != Status::external_control_flow || not_taken.pc != 0x1020) {
        return 1;
    }
    if (not_taken.gpr[9].low != 7 || not_taken.gpr[10].low != 5) {
        return 2;
    }
    if (not_taken.gpr[11].low != 9 || not_taken.gpr[12].low != 0) {
        return 3;
    }
    if (not_taken.gpr[13].low != 13) {
        return 4;
    }

    // Conditional branch taken: delay slot still runs, the fallthrough block
    // is skipped, and the target block reaches the same return.
    EeState taken{};
    taken.pc = 0x1000;
    taken.gpr[8].low = 0;
    taken.gpr[31].low = 0x1020;
    const auto taken_status = reps2::generated::execute_cfg(taken, 64);
    if (taken_status != Status::external_control_flow || taken.pc != 0x1020) {
        return 5;
    }
    if (taken.gpr[9].low != 7 || taken.gpr[10].low != 0) {
        return 6;
    }
    if (taken.gpr[11].low != 0 || taken.gpr[12].low != 11) {
        return 7;
    }
    if (taken.gpr[13].low != 13) {
        return 8;
    }

    // A block is admitted atomically by the instruction budget. The branch
    // plus its delay slot requires two guest instructions.
    EeState limited{};
    limited.pc = 0x1000;
    limited.gpr[8].low = 0;
    const auto budget_status = reps2::generated::execute_cfg(limited, 1);
    if (budget_status != Status::instruction_budget_exhausted || limited.pc != 0x1000) {
        return 9;
    }
    if (limited.gpr[9].low != 0) {
        return 10;
    }

    // JAL writes PC+8, executes its delay slot, enters the callee, and JR RA
    // executes its own delay slot before returning to the continuation block.
    EeState call{};
    call.pc = 0x2000;
    const auto call_status = reps2::generated::execute_cfg(call, 64);
    if (call_status != Status::unsupported_instruction || call.pc != 0x2008) {
        return 11;
    }
    if (call.gpr[31].low != 0x2008) {
        return 12;
    }
    if (call.gpr[14].low != 17 || call.gpr[15].low != 19) {
        return 13;
    }

    // BLTZAL writes the link even when its predicate is false.
    EeState link_not_taken{};
    link_not_taken.pc = 0x3000;
    link_not_taken.gpr[8].low = 1;
    const auto link_not_taken_status = reps2::generated::execute_cfg(link_not_taken, 64);
    if (link_not_taken_status != Status::unsupported_instruction || link_not_taken.pc != 0x3008) {
        return 14;
    }
    if (link_not_taken.gpr[31].low != 0x3008 || link_not_taken.gpr[9].low != 23) {
        return 15;
    }

    // The same branch-and-link writes PC+8 when taken.
    EeState link_taken{};
    link_taken.pc = 0x3000;
    link_taken.gpr[8].low = 0xFFFFFFFFFFFFFFFFULL;
    const auto link_taken_status = reps2::generated::execute_cfg(link_taken, 64);
    if (link_taken_status != Status::unsupported_instruction || link_taken.pc != 0x300C) {
        return 16;
    }
    if (link_taken.gpr[31].low != 0x3008 || link_taken.gpr[9].low != 23) {
        return 17;
    }

    // JALR with rd == rs must jump to the captured old source value even
    // though that GPR is overwritten with the link address.
    EeState jalr_alias{};
    jalr_alias.pc = 0x4000;
    jalr_alias.gpr[8].low = 0x5000;
    const auto jalr_status = reps2::generated::execute_cfg(jalr_alias, 64);
    if (jalr_status != Status::external_control_flow || jalr_alias.pc != 0x5000) {
        return 18;
    }
    if (jalr_alias.gpr[8].low != 0x4008 || jalr_alias.gpr[9].low != 31) {
        return 19;
    }

    return 0;
}
