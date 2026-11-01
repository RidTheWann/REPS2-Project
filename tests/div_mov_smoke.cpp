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
    state.gpr[0].low = 0xDEADBEEF;
    state.gpr[0].high = 0xBAD0BAD0BAD0BAD0ULL;

    state.gpr[1].low = 100;
    state.gpr[1].high = 0x0101010101010101ULL;

    state.gpr[2].low = 3;
    state.gpr[2].high = 0x0202020202020202ULL;

    state.gpr[3].low = 42;
    state.gpr[3].high = 0x0303030303030303ULL;

    state.gpr[4].low = 0xFFFFFFFFFFFFFFD6ULL; // -42 in sign-extended low word
    state.gpr[4].high = 0x0404040404040404ULL;

    state.gpr[5].low = 0xFFFFFFFF80000000ULL; // INT32_MIN
    state.gpr[5].high = 0x0505050505050505ULL;

    state.gpr[6].low = 0xFFFFFFFFFFFFFFFFULL; // -1
    state.gpr[6].high = 0x0606060606060606ULL;

    state.gpr[7].low = 100;
    state.gpr[7].high = 0x0707070707070707ULL;

    state.gpr[8].low = 3;
    state.gpr[8].high = 0x0808080808080808ULL;

    state.gpr[10].low = 0xAAAAAAAAAAAAAAAAULL;
    state.gpr[10].high = 0x1010101010101010ULL;

    state.gpr[11].low = 0x1234567890ABCDEFULL;
    state.gpr[11].high = 0x1111111111111111ULL;

    state.gpr[12].low = 0ULL;
    state.gpr[12].high = 0x1212121212121212ULL;

    state.gpr[13].low = 0xBBBBBBBBBBBBBBBBULL;
    state.gpr[13].high = 0x1313131313131313ULL;

    state.gpr[14].low = 0xCCCCCCCCCCCCCCCCULL;
    state.gpr[14].high = 0x1414141414141414ULL;

    state.gpr[15].low = 0xDDDDDDDDDDDDDDDDULL;
    state.gpr[15].high = 0x1515151515151515ULL;

    // Output registers
    for (int r = 20; r <= 31; ++r) {
        state.gpr[r].low = 0;
        state.gpr[r].high = 0x88000000ULL | static_cast<std::uint64_t>(r);
    }

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // GPR[0] remains hard-wired to zero
    if (state.gpr[0].low != 0 || state.gpr[0].high != 0) return 2;

    // Normal signed DIV 100 / 3 -> HI = 1, LO = 33
    if (state.gpr[20].low != 1) return 3;
    if (state.gpr[21].low != 33) return 4;

    // Signed DIV / 0 (positive dividend): 42 / 0 -> HI = 42, LO = -1
    if (state.gpr[22].low != 42) return 5;
    if (state.gpr[23].low != 0xFFFFFFFFFFFFFFFFULL) return 6;

    // Signed DIV / 0 (negative dividend): -42 / 0 -> HI = -42, LO = 1
    if (state.gpr[24].low != 0xFFFFFFFFFFFFFFD6ULL) return 7;
    if (state.gpr[25].low != 1) return 8;

    // Signed DIV overflow: INT32_MIN / -1 -> HI = 0, LO = INT32_MIN
    if (state.gpr[26].low != 0) return 9;
    if (state.gpr[27].low != 0xFFFFFFFF80000000ULL) return 10;

    // Normal unsigned DIVU 100 / 3 -> HI = 1, LO = 33
    if (state.gpr[28].low != 1) return 11;
    if (state.gpr[29].low != 33) return 12;

    // Unsigned DIVU / 0: 100 / 0 -> HI = 100, LO = UINT32_MAX sign-extended (all ones)
    if (state.gpr[30].low != 100) return 13;
    if (state.gpr[31].low != 0xFFFFFFFFFFFFFFFFULL) return 14;

    // MOVZ: r10 = r11 when r12 == 0 (taken)
    if (state.gpr[10].low != 0x1234567890ABCDEFULL) return 15;

    // MOVZ: r13 = r11 when r2 == 0 (not taken, r2 == 3)
    if (state.gpr[13].low != 0xBBBBBBBBBBBBBBBBULL) return 16;

    // MOVN: r14 = r11 when r2 != 0 (taken, r2 == 3)
    if (state.gpr[14].low != 0x1234567890ABCDEFULL) return 17;

    // MOVN: r15 = r11 when r12 != 0 (not taken, r12 == 0)
    if (state.gpr[15].low != 0xDDDDDDDDDDDDDDDDULL) return 18;

    // Verify upper lanes preserved
    if (state.gpr[10].high != 0x1010101010101010ULL) return 19;
    if (state.gpr[13].high != 0x1313131313131313ULL) return 20;
    if (state.gpr[14].high != 0x1414141414141414ULL) return 21;
    if (state.gpr[15].high != 0x1515151515151515ULL) return 22;

    for (int r = 20; r <= 31; ++r) {
        if (state.gpr[r].high != (0x88000000ULL | static_cast<std::uint64_t>(r))) {
            return 23;
        }
    }

    return 0;
}
