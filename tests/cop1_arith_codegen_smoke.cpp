// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

using reps2::runtime::EeState;

// The fixture loads these four values into FPR[1..4] with MTC1.
constexpr std::uint32_t kOne = 0x3F80'0000U;
constexpr std::uint32_t kTwo = 0x4000'0000U;
constexpr std::uint32_t kHalf = 0x3F00'0000U;
constexpr std::uint32_t kFour = 0x4080'0000U;

// 1.0 / +0.0 returns +Fmax and names the divide-by-zero cause, whose sticky
// partner sits eleven bits lower; the reset image adds bit 24 and bits 1..0.
constexpr std::uint32_t kFcsrAfterDivideByZero = 0x0101'0021U;

} // namespace

int main() {
    EeState state{};
    state.gpr[1].low = kOne;
    state.gpr[2].low = kTwo;
    state.gpr[3].low = kHalf;
    state.gpr[4].low = kFour;
    // A dirty high lane proves CFC1 only writes the low 64 bits.
    state.gpr[5] = {0ULL, 0x5555'6666'7777'8888ULL};

    if (reps2::generated::execute(state) != reps2::runtime::Status::ok) return 1;

    // MOV/ABS/NEG, ADD/SUB/MUL/DIV and SQRT read the expected operand fields.
    if (state.fpr[10] != 0x4040'0000U) return 2;  // add.s  1 + 2 = 3
    if (state.fpr[11] != 0xBF80'0000U) return 3;  // sub.s  1 - 2 = -1
    if (state.fpr[12] != 0x4000'0000U) return 4;  // mul.s  1 * 2 = 2
    if (state.fpr[13] != 0x3F00'0000U) return 5;  // div.s  1 / 2 = 0.5
    if (state.fpr[14] != 0x4000'0000U) return 6;  // sqrt.s 4 = 2 (operand in ft)
    if (state.fpr[15] != 0x3F80'0000U) return 7;  // abs.s  -1 = 1
    if (state.fpr[16] != 0xBF80'0000U) return 8;  // mov.s  -1
    if (state.fpr[17] != 0x3F80'0000U) return 9;  // neg.s  -1 = 1
    if (state.fpr[18] != 0x3F00'0000U) return 10; // rsqrt.s 1 / sqrt(4) = 0.5

    // ACC routing: ADDA leaves 3, MADD reads it (3 + 2 = 5), SUBA leaves -1,
    // MSUB reads it (-1 - 2 = -3), then MULA/MADDA/MSUBA end at 2.
    if (state.fpr[19] != 0x40A0'0000U) return 11; // madd.s  acc + 1*2 = 5
    if (state.fpr[20] != 0xC040'0000U) return 12; // msub.s  acc - 1*2 = -3
    if (state.acc != 0x4000'0000U) return 13;

    // MAX/MIN and both conversions take their source from fs.
    if (state.fpr[21] != 0x3F00'0000U) return 14; // max.s -1, 0.5 = 0.5
    if (state.fpr[22] != 0xBF80'0000U) return 15; // min.s -1, 0.5 = -1
    if (state.fpr[23] != 0xFFFF'FFFFU) return 16; // cvt.w.s -1 truncates to -1
    if (state.fpr[24] != 0xBF80'0000U) return 17; // cvt.s.w -1 = -1.0

    // The final divide raises D for real, so the CFC1 readback proves the
    // generated code routes every result through apply_fpu_flags.
    if (state.fpr[25] != 0x7FFF'FFFFU) return 18;
    if (state.fcsr != kFcsrAfterDivideByZero) return 19;
    if (state.gpr[5].low != kFcsrAfterDivideByZero) return 20;
    if (state.gpr[5].high != 0x5555'6666'7777'8888ULL) return 21;

    // MTC1 $zero wrote the real register FPR[0].
    if (state.fpr[0] != 0U) return 22;

    return 0;
}
