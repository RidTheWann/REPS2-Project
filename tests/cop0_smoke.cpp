// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

int main() {
    using reps2::runtime::EeState;
    using reps2::runtime::Status;
    using reps2::runtime::kEeProcessorId;
    using reps2::runtime::kCop0StatusEieMask;
    using reps2::runtime::kCop0PrId;
    using reps2::runtime::kCop0Status;

    EeState state{};

    // Initial register states with distinct high halves to verify lane preservation
    for (int r = 0; r < 32; ++r) {
        state.gpr[r].low = 0;
        state.gpr[r].high = 0xDEAD0000ULL | static_cast<std::uint64_t>(r);
    }

    state.cop0[kCop0PrId] = kEeProcessorId;
    state.cop0[kCop0Status] = 0;

    // GPR[2]: attempted overwrite of PRId (read-only)
    state.gpr[2].low = 0x12345678ULL;

    // GPR[4]: value to write into Count (COP0 reg 9)
    state.gpr[4].low = 0x00ABCDEFULL;

    // GPR[8]: value to write into Status (COP0 reg 12)
    state.gpr[8].low = 0x55AA55AAULL;

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // GPR[0] remains hard-wired to zero
    if (state.gpr[0].low != 0 || state.gpr[0].high != 0) return 2;

    // PRId read (mfc0 $1, $15)
    if (state.gpr[1].low != kEeProcessorId) return 3;

    // PRId after attempted mtc0 $2, $15 (read-only, remains kEeProcessorId)
    if (state.gpr[3].low != kEeProcessorId) return 4;
    if (state.cop0[kCop0PrId] != kEeProcessorId) return 5;

    // Count written and read back (mfc0 $5, $9)
    if (state.gpr[5].low != 0x00ABCDEFULL) return 6;
    if (state.cop0[9] != 0x00ABCDEF) return 7;

    // EI set Status.EIE (bit 16)
    if ((state.gpr[6].low & kCop0StatusEieMask) == 0) return 8;

    // DI cleared Status.EIE (bit 16)
    if ((state.gpr[7].low & kCop0StatusEieMask) != 0) return 9;

    // Status written by mtc0 $8, $12 and read back by mfc0 $9, $12
    if (state.gpr[9].low != 0x55AA55AAULL) return 10;
    if (state.cop0[kCop0Status] != 0x55AA55AA) return 11;

    // Verify upper lanes preserved for all written GPRs
    for (int r = 1; r <= 9; ++r) {
        if (state.gpr[r].high != (0xDEAD0000ULL | static_cast<std::uint64_t>(r))) {
            return 20 + r;
        }
    }

    return 0;
}
