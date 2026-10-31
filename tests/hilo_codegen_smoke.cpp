// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

int main() {
    using reps2::runtime::EeState;
    using reps2::runtime::Status;

    EeState state{};
    state.gpr[1].low = 0xFFFFFFFFFFFFFFF0ULL;
    state.gpr[2].low = 0x0000000000000030ULL;
    state.gpr[3].low = 0xFFFFFFFFFFFFFFFFULL;
    state.gpr[20].high = 0xA1A2A3A4A5A6A7A8ULL;
    state.gpr[21].high = 0xB1B2B3B4B5B6B7B8ULL;
    state.gpr[22].high = 0xC1C2C3C4C5C6C7C8ULL;
    state.gpr[23].high = 0xD1D2D3D4D5D6D7D8ULL;
    state.gpr[24].high = 0xE1E2E3E4E5E6E7E8ULL;
    state.gpr[25].high = 0xF1F2F3F4F5F6F7F8ULL;

    if (reps2::generated::execute(state) != Status::ok) return 1;
    if (state.gpr[20].low != 0xFFFFFFFFFFFFFFF0ULL) return 2;
    if (state.gpr[21].low != 0x0000000000000030ULL) return 3;
    if (state.gpr[20].high != 0xA1A2A3A4A5A6A7A8ULL) return 4;
    if (state.gpr[21].high != 0xB1B2B3B4B5B6B7B8ULL) return 5;
    // MULT -16 * 48 = -768, with sign-extended 32-bit HI/LO result lanes.
    if (state.gpr[22].low != 0xFFFFFFFFFFFFFD00ULL) return 6;
    if (state.gpr[24].low != 0xFFFFFFFFFFFFFFFFULL) return 7;
    if (state.gpr[25].low != 0xFFFFFFFFFFFFFD00ULL) return 8;
    if (state.gpr[22].high != 0xC1C2C3C4C5C6C7C8ULL) return 9;
    if (state.gpr[24].high != 0xE1E2E3E4E5E6E7E8ULL) return 10;
    if (state.gpr[25].high != 0xF1F2F3F4F5F6F7F8ULL) return 11;
    // MULTU 0xFFFFFFFF * 48 = 0x0000002FFFFFFFD0.
    if (state.gpr[23].low != 0xFFFFFFFFFFFFFFD0ULL) return 12;
    if (state.gpr[23].high != 0xD1D2D3D4D5D6D7D8ULL) return 13;
    if (state.hi != 0x000000000000002FULL) return 14;
    if (state.lo != 0xFFFFFFFFFFFFFFD0ULL) return 15;
    return 0;
}
