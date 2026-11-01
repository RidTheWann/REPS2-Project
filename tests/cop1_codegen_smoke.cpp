// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Gpr128;
using reps2::runtime::Status;

// GPR[1] carries bit 32 so MTC1 must truncate it away, and its low half has
// the sign bit set so MFC1 must sign-extend it back into GPR[63:32].
constexpr std::uint64_t kFprSource = 0x0000'0001'8000'00AAULL;
constexpr std::uint32_t kFprValue = 0x8000'00AAU;
constexpr std::uint64_t kSignExtendedFpr = 0xFFFF'FFFF'8000'00AAULL;
// The CTC1 source sets bit 32, which must not reach the 32-bit store; bits 31
// and 22 and the reserved 13..7 field, which are read-as-zero and must not
// persist; the condition bit, cause I and sticky U, which must; and bits 1..0
// as 0b10, where the EE hard-wires the rounding field to 0b01 on the read back.
constexpr std::uint64_t kFcsrSource = 0x0000'0001'80C2'100AULL;
constexpr std::uint32_t kFcsrValue = 0x0182'0009U;
// CFC1 $31 can no longer show sign extension, because bit 31 is one of the
// reserved-zero bits the store cannot set: the only patterns FCR31 can hold
// have it clear. MFC1 covers the extension of a set sign bit, and this check
// still proves CFC1 leaves GPR[127:64] alone.
constexpr std::uint64_t kFcsrReadBack = 0x0182'0009ULL;
// The same source through MTC1 keeps the reserved bits and bit 1: only CTC1
// applies the mask.
constexpr std::uint64_t kSignExtendedFcsrSource = 0xFFFF'FFFF'80C2'100AULL;
constexpr std::uint32_t kScratchAddress = 0x40U;

std::array<std::uint8_t, 256> main_memory{};

void preset(EeState& state) {
    state.memory.main_ram = std::span<std::uint8_t>(main_memory);
    // A dirty word at the store destination proves SWC1 wrote it, and the two
    // LWC1 forms must read the stored value rather than the preset bytes.
    main_memory[kScratchAddress] = 0xDE;
    main_memory[kScratchAddress + 1U] = 0xAD;
    main_memory[kScratchAddress + 2U] = 0xBE;
    main_memory[kScratchAddress + 3U] = 0xEF;

    // A non-zero GPR[0] proves the prologue resets it and MFC1 $0 cannot
    // re-write it: the block issues MFC1 $0 while FPR[6] holds a non-zero value.
    state.gpr[0] = Gpr128{0xDEAD'BEEFULL, 0xBAD0BAD0'BAD0BAD0ULL};
    state.gpr[1] = Gpr128{kFprSource, 0x1111'2222'3333'4444ULL};
    state.gpr[2] = Gpr128{kFcsrSource, 0x2222'3333'4444'5555ULL};
    state.gpr[3] = Gpr128{0ULL, 0x3333'4444'5555'6666ULL};
    state.gpr[4] = Gpr128{0xFFFF'FFFF'FFFF'FFFFULL, 0x4444'5555'6666'7777ULL};
    state.gpr[5] = Gpr128{0ULL, 0x5555'6666'7777'8888ULL};
    state.gpr[6] = Gpr128{0xFFFF'FFFF'FFFF'FFFFULL, 0x6666'7777'8888'9999ULL};
    state.gpr[7] = Gpr128{0xFFFF'FFFF'FFFF'FFFFULL, 0x7777'8888'9999'AAAAULL};
    state.gpr[8] = Gpr128{0ULL, 0x8888'9999'AAAA'BBBBULL};
    // The negative-offset LWC1 base points one word past the scratch word.
    state.gpr[9] = Gpr128{kScratchAddress + 4U, 0x9999'AAAA'BBBB'CCCCULL};
    state.gpr[10] = Gpr128{0xFFFF'FFFF'FFFF'FFFFULL, 0xAAAA'BBBB'CCCC'DDDDULL};
    state.gpr[11] = Gpr128{0xFFFF'FFFF'FFFF'FFFFULL, 0xDDDD'EEEE'FFFF'0000ULL};
    // A dirty FPR file shows the movers and loads really replaced contents.
    state.fpr.fill(0xFFFF'FFFFU);
}

} // namespace

int main() {
    EeState state{};
    preset(state);

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // MTC1 takes only GPR[31:0]; MFC1 sign-extends into GPR[63:0] and leaves
    // GPR[127:64] untouched.
    if (state.fpr[6] != kFprValue) return 2;
    if (state.gpr[3].low != kSignExtendedFpr) return 3;
    if (state.gpr[3].high != 0x3333'4444'5555'6666ULL) return 4;

    // SWC1 stores the 32-bit FPU register little-endian at the aligned address.
    if (main_memory[kScratchAddress] != 0xAA
        || main_memory[kScratchAddress + 1U] != 0x00
        || main_memory[kScratchAddress + 2U] != 0x00
        || main_memory[kScratchAddress + 3U] != 0x80) {
        return 5;
    }

    // LWC1 and MTC1 must both write FPR[0]: it is an architectural register,
    // unlike GPR[0].
    if (state.gpr[4].low != kSignExtendedFpr) return 6;
    if (state.gpr[10].low != kSignExtendedFcsrSource) return 7;
    if (state.gpr[10].high != 0xAAAA'BBBB'CCCC'DDDDULL) return 8;

    // The word that halted every retail entry scan so far executes for real.
    if (state.fpr[0] != 0U) return 9;
    if (state.gpr[11].low != 0ULL) return 10;
    if (state.gpr[11].high != 0xDDDD'EEEE'FFFF'0000ULL) return 11;

    // A negative signed offset reaches the same stored word.
    if (state.fpr[7] != kFprValue) return 12;
    if (state.gpr[7].low != kSignExtendedFpr) return 13;
    if (state.gpr[7].high != 0x7777'8888'9999'AAAAULL) return 14;

    // FIR is read-only and its documented value is asserted literally: the
    // emitter reads kFpuImplementationRegister, so comparing against that
    // constant would only prove the constant equals itself.
    if (state.gpr[5].low != 0x0000'0000'0000'2E00ULL) return 15;
    if (state.gpr[5].high != 0x5555'6666'7777'8888ULL) return 16;

    // CTC1 truncates to 32 bits, keeps only C and the eight flag bits and
    // cannot program the rounding field.
    if (state.fcsr != kFcsrValue) return 17;
    if (state.gpr[6].low != kFcsrReadBack) return 18;
    if (state.gpr[6].high != 0x6666'7777'8888'9999ULL) return 22;

    // The movers read their GPR sources without writing them back.
    if (state.gpr[1].low != kFprSource) return 19;
    if (state.gpr[2].low != kFcsrSource) return 20;

    // MFC1 $0 ran while FPR[6] held 0x800000AA, so an unguarded write would
    // have stored the sign-extended value here.
    if (state.gpr[0].low != 0ULL || state.gpr[0].high != 0ULL) return 21;

    return 0;
}
