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

    state.gpr[1].low = 0xFFFFFFFFFFFFFFF0ULL; // low word = -16
    state.gpr[1].high = 0x1111222233334444ULL;
    state.gpr[2].low = 0x0000000000000030ULL; // low word = 48
    state.gpr[3].low = 0xFFFFFFFFFFFFFFFFULL; // unsigned low word = 2^32 - 1
    state.gpr[3].high = 0x3333444455556666ULL;

    // R5900 signed multiply boundary vectors, based on the public QEMU TCG test.
    state.gpr[4].low = 0xFFFFFFFF80000000ULL; // INT32_MIN
    state.gpr[5].low = 0x000000007FFFFFFFULL; // INT32_MAX
    state.gpr[8].low = 0xFFFFFFFF80000000ULL; // INT32_MIN
    state.gpr[9].low = 0xFFFFFFFF80000001ULL; // -INT32_MAX
    state.gpr[11].low = 0xFFFFFFFF80000000ULL; // INT32_MIN
    state.gpr[12].low = 0xFFFFFFFF80000000ULL; // INT32_MIN

    state.gpr[6].high = 0x6666777788889999ULL;
    state.gpr[10].high = 0xAAAA000011112222ULL;
    state.gpr[17].high = 0xBBBB000022223333ULL;

    state.gpr[13].high = 0x1313131313131313ULL;
    state.gpr[14].high = 0x1414141414141414ULL;
    state.gpr[15].high = 0x1515151515151515ULL;
    state.gpr[16].high = 0x1616161616161616ULL;
    state.gpr[18].high = 0x1818181818181818ULL;
    state.gpr[19].high = 0x1919191919191919ULL;

    state.gpr[20].high = 0xA1A2A3A4A5A6A7A8ULL;
    state.gpr[21].high = 0xB1B2B3B4B5B6B7B8ULL;
    state.gpr[22].high = 0xC1C2C3C4C5C6C7C8ULL;
    state.gpr[23].high = 0xD1D2D3D4D5D6D7D8ULL;
    state.gpr[24].high = 0xE1E2E3E4E5E6E7E8ULL;
    state.gpr[25].high = 0xF1F2F3F4F5F6F7F8ULL;
    state.gpr[26].high = 0x2626262626262626ULL;
    state.gpr[27].high = 0x2727272727272727ULL;

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // GPR zero remains hard-wired even when a three-operand MULT targets rd=0.
    if (state.gpr[0].low != 0 || state.gpr[0].high != 0) return 2;

    // MTHI/MTLO source and MFHI/MFLO destination semantics.
    if (state.gpr[20].low != 0xFFFFFFFFFFFFFFF0ULL) return 3;
    if (state.gpr[21].low != 0x0000000000000030ULL) return 4;
    if (state.gpr[20].high != 0xA1A2A3A4A5A6A7A8ULL) return 5;
    if (state.gpr[21].high != 0xB1B2B3B4B5B6B7B8ULL) return 6;

    // MULT -16 * 48 = -768; HI and LO are sign-extended 32-bit lanes.
    if (state.gpr[22].low != 0xFFFFFFFFFFFFFD00ULL) return 7;
    if (state.gpr[24].low != 0xFFFFFFFFFFFFFFFFULL) return 8;
    if (state.gpr[25].low != 0xFFFFFFFFFFFFFD00ULL) return 9;
    if (state.gpr[26].low != 0xFFFFFFFFFFFFFFFFULL) return 10;
    if (state.gpr[27].low != 0xFFFFFFFFFFFFFD00ULL) return 11;
    if (state.gpr[22].high != 0xC1C2C3C4C5C6C7C8ULL
        || state.gpr[24].high != 0xE1E2E3E4E5E6E7E8ULL
        || state.gpr[25].high != 0xF1F2F3F4F5F6F7F8ULL
        || state.gpr[26].high != 0x2626262626262626ULL
        || state.gpr[27].high != 0x2727272727272727ULL) return 12;

    // MULTU 0xFFFFFFFF * 48 = 0x0000002FFFFFFFD0. rd receives LO.
    if (state.gpr[23].low != 0xFFFFFFFFFFFFFFD0ULL) return 13;
    if (state.gpr[23].high != 0xD1D2D3D4D5D6D7D8ULL) return 14;

    // QEMU's R5900 MULT TCG vectors:
    // INT32_MIN * INT32_MAX = -0x3FFFFFFF80000000.
    if (state.gpr[6].low != 0xFFFFFFFF80000000ULL) return 15;
    if (state.gpr[13].low != 0xFFFFFFFFC0000000ULL) return 16;
    if (state.gpr[14].low != 0xFFFFFFFF80000000ULL) return 17;
    // INT32_MIN * -INT32_MAX = 0x3FFFFFFF80000000.
    if (state.gpr[10].low != 0xFFFFFFFF80000000ULL) return 18;
    if (state.gpr[15].low != 0x000000003FFFFFFFULL) return 19;
    if (state.gpr[16].low != 0xFFFFFFFF80000000ULL) return 20;
    // INT32_MIN * INT32_MIN = 0x4000000000000000.
    if (state.gpr[17].low != 0ULL) return 21;
    if (state.gpr[18].low != 0x0000000040000000ULL) return 22;
    if (state.gpr[19].low != 0ULL) return 23;

    // MULTU rd=rs and MULT rd=rs are safe: both source operands are captured first.
    if (state.gpr[3].low != 0xFFFFFFFFFFFFFFD0ULL) return 24;
    if (state.gpr[3].high != 0x3333444455556666ULL) return 25;
    if (state.gpr[1].low != 0xFFFFFFFFFFFFFD00ULL) return 26;
    if (state.gpr[1].high != 0x1111222233334444ULL) return 27;

    if (state.hi != 0xFFFFFFFFFFFFFFFFULL) return 28;
    if (state.lo != 0xFFFFFFFFFFFFFD00ULL) return 29;
    if (state.gpr[6].high != 0x6666777788889999ULL
        || state.gpr[10].high != 0xAAAA000011112222ULL
        || state.gpr[17].high != 0xBBBB000022223333ULL
        || state.gpr[13].high != 0x1313131313131313ULL
        || state.gpr[14].high != 0x1414141414141414ULL
        || state.gpr[15].high != 0x1515151515151515ULL
        || state.gpr[16].high != 0x1616161616161616ULL
        || state.gpr[18].high != 0x1818181818181818ULL
        || state.gpr[19].high != 0x1919191919191919ULL) return 30;

    return 0;
}
