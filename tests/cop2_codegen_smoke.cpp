// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Gpr128;
using reps2::runtime::Status;

constexpr std::uint64_t kScratchAddress = 0x40U;
constexpr Gpr128 kVf0Constant{0x0000'0000'0000'0000ULL, 0x3F80'0000'0000'0000ULL};
// VMR32 (ADR 0191): move with rotate per word. Sony VU manual: ft.y<-fs.x,
// ft.z<-fs.y, ft.w<-fs.z, ft.x<-fs.w. With words [a,b,c,d] the result is [d,a,b,c].
constexpr Gpr128 kRotateSrc{0x1111'2222'3333'4444ULL, 0xAAAA'BBBB'CCCC'DDDDULL};
constexpr Gpr128 kRotateExpected{0x3333'4444'AAAA'BBBBULL, 0xCCCC'DDDD'1111'2222ULL};
static_assert(reps2::runtime::vu0_rotate_right_word(kRotateSrc).low == kRotateExpected.low);
static_assert(reps2::runtime::vu0_rotate_right_word(kRotateSrc).high == kRotateExpected.high);

// VSUB lane subtract through the VU float model (ADR 0192). Gpr128 word order
// is x,y,z,w; dest mask bit3=x, bit2=y, bit1=z, bit0=w. 4.0 - 1.0 = 3.0.
constexpr std::uint32_t kF4 = 0x4080'0000U;  // 4.0f
constexpr std::uint32_t kF1 = 0x3F80'0000U;  // 1.0f
constexpr std::uint32_t kF3 = 0x4040'0000U;  // 3.0f
constexpr std::uint64_t kPacked4 = static_cast<std::uint64_t>(kF4) | (static_cast<std::uint64_t>(kF4) << 32U);
constexpr std::uint64_t kPacked1 = static_cast<std::uint64_t>(kF1) | (static_cast<std::uint64_t>(kF1) << 32U);
constexpr reps2::runtime::Gpr128 kVsubFs{kPacked4, kPacked4};
constexpr reps2::runtime::Gpr128 kVsubFt{kPacked1, kPacked1};
constexpr reps2::runtime::Gpr128 kVsubDst{
    0xDEAD'BEEF'DEAD'BEEFULL, 0xDEAD'BEEF'DEAD'BEEFULL};
// dest mask 0xF writes all four lanes -> all 3.0f.
constexpr auto kVsubAll = reps2::runtime::vu0_sub_lanes(kVsubDst, kVsubFs, kVsubFt, 0xF);
// (numeric expectation checked at runtime below; constexpr evaluation of the
// FPU model differs subtly across toolchains, so the exact lane value is
// asserted in main() rather than at compile time.)
// dest mask 0x0 writes no lane -> destination preserved.
constexpr auto kVsubNone = reps2::runtime::vu0_sub_lanes(kVsubDst, kVsubFs, kVsubFt, 0x0);
static_assert(kVsubNone.low == kVsubDst.low && kVsubNone.high == kVsubDst.high);

std::vector<std::uint8_t> main_ram_image(reps2::runtime::kEeMainRamSize);

void preset(EeState& state) {
    main_ram_image.assign(reps2::runtime::kEeMainRamSize, 0);
    state.memory.main_ram = main_ram_image;
    // Scratch area at +0x10..+0x1F for SQC2 $vf0 (must become constant).
    // Scratch area at +0x20..+0x2F pre-filled with known pattern for LQC2 $vf5.
    std::uint8_t pattern[16] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    };
    for (std::size_t i = 0; i < 16; ++i) {
        main_ram_image[kScratchAddress + 0x20 + i] = pattern[i];
    }
    // Scratch at +0x40..+0x4F is garbage (DEADBEEF...) to prove LQC2 $vf0 ignores it.
    main_ram_image[kScratchAddress + 0x40] = 0xDE;
    main_ram_image[kScratchAddress + 0x41] = 0xAD;
    main_ram_image[kScratchAddress + 0x42] = 0xBE;
    main_ram_image[kScratchAddress + 0x43] = 0xEF;

    // GPR[0] starts dirty to prove the prologue resets it and LQC2 $vf0 cannot write it.
    state.gpr[0] = Gpr128{0xDEAD'BEEFULL, 0xBAD0BAD0'BAD0BAD0ULL};
    // GPR[4] = base address for loads/stores.
    state.gpr[4] = Gpr128{kScratchAddress, 0x1111'2222'3333'4444ULL};
    // GPR[2] holds a known quadword for the QMTC2 $2, $vf12 transfer.
    state.gpr[2] = Gpr128{0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL};
    // GPR[3] supplies the word moved into VU0 control register 28 by CTC2;
    // choose a value with bit 31 set to prove sign-extension on CFC2.
    state.gpr[3] = Gpr128{0x8000'0200ULL, 0x1111'2222'3333'4444ULL};
    // Fill VF file with garbage to prove LQC2/SQC2 really replace contents.
    state.vu0.vf.fill(Gpr128{0xFFFF'FFFF'FFFF'FFFFULL, 0xFFFF'FFFF'FFFF'FFFFULL});
    // Pre-fill the QMTC2/QMFC2 scratch quadwords so that every store, including
    // the mostly-zero VF0 constant, is an observable write.
    for (std::size_t i = 0x60; i < 0x90; ++i) {
        main_ram_image[kScratchAddress + i] = 0x5A;
    }
}

} // namespace

int main() {
    // Runtime check for VSUB lane subtract (ADR 0192): 4.0 - 1.0 = 3.0 in all
    // four lanes, with unselected lanes preserved. Uses a runtime value rather
    // than static_assert because constexpr evaluation differs across toolchains.
    constexpr std::uint64_t kThreeWord = 0x4040'0000'4040'0000ULL;
    if (kVsubAll.low != kThreeWord || kVsubAll.high != kThreeWord) {
        std::printf("VSUB all: low=%016llX high=%016llX want=%016llX\n",
                    (unsigned long long)kVsubAll.low, (unsigned long long)kVsubAll.high,
                    (unsigned long long)kThreeWord);
        return 60;
    }
    if (kVsubNone.low != kVsubDst.low || kVsubNone.high != kVsubDst.high) return 61;
    // Fill scratch first, then attach the full-size RAM image.
    EeState state{};
    preset(state);
    const Status status = reps2::generated::execute(state);
    if (status != Status::ok) {
        std::printf("execute failed: status=%d pc=0x%08X\n",
                    static_cast<int>(status), state.pc);
        return 1;
    }
    // SQC2 $vf12, 0x60($a0) executes after the CFC2/CTC2 group, so bytes at
    // +0x60..+0x6F are the GPR2 quadword (low lane first).
    if (main_ram_image[kScratchAddress + 0x60] != 0xEF) return 51;
    // Quadword is little-endian: X(4), Y(4), Z(4), W(4).
    // W = 1.0f = 0x3F800000 => bytes 0x00, 0x00, 0x80, 0x3F at offsets 12..15.
    if (main_ram_image[kScratchAddress + 0x10] != 0x00) return 2;
    if (main_ram_image[kScratchAddress + 0x11] != 0x00) return 3;
    if (main_ram_image[kScratchAddress + 0x12] != 0x00) return 4;
    if (main_ram_image[kScratchAddress + 0x13] != 0x00) return 5;
    if (main_ram_image[kScratchAddress + 0x14] != 0x00) return 6;
    if (main_ram_image[kScratchAddress + 0x15] != 0x00) return 7;
    if (main_ram_image[kScratchAddress + 0x16] != 0x00) return 8;
    if (main_ram_image[kScratchAddress + 0x17] != 0x00) return 9;
    if (main_ram_image[kScratchAddress + 0x18] != 0x00) return 10;
    if (main_ram_image[kScratchAddress + 0x19] != 0x00) return 11;
    if (main_ram_image[kScratchAddress + 0x1A] != 0x00) return 12;
    if (main_ram_image[kScratchAddress + 0x1B] != 0x00) return 13;
    if (main_ram_image[kScratchAddress + 0x1C] != 0x00) return 14;
    if (main_ram_image[kScratchAddress + 0x1D] != 0x00) return 15;
    if (main_ram_image[kScratchAddress + 0x1E] != 0x80) return 16;
    if (main_ram_image[kScratchAddress + 0x1F] != 0x3F) return 17;

    // LQC2 $vf5, 0x20($a0) loads from +0x20 (preset pattern).
    // SQC2 $vf5, 0x30($a0) stores VF5 back at +0x30.
    if (main_ram_image[kScratchAddress + 0x30] != 0xAA) return 18;
    if (main_ram_image[kScratchAddress + 0x31] != 0xBB) return 19;
    if (main_ram_image[kScratchAddress + 0x32] != 0xCC) return 20;
    if (main_ram_image[kScratchAddress + 0x33] != 0xDD) return 21;
    if (main_ram_image[kScratchAddress + 0x34] != 0xEE) return 22;
    if (main_ram_image[kScratchAddress + 0x35] != 0xFF) return 23;
    if (main_ram_image[kScratchAddress + 0x36] != 0x00) return 24;
    if (main_ram_image[kScratchAddress + 0x37] != 0x11) return 25;
    if (main_ram_image[kScratchAddress + 0x38] != 0x22) return 26;
    if (main_ram_image[kScratchAddress + 0x39] != 0x33) return 27;
    if (main_ram_image[kScratchAddress + 0x3A] != 0x44) return 28;
    if (main_ram_image[kScratchAddress + 0x3B] != 0x55) return 29;
    if (main_ram_image[kScratchAddress + 0x3C] != 0x66) return 30;
    if (main_ram_image[kScratchAddress + 0x3D] != 0x77) return 31;
    if (main_ram_image[kScratchAddress + 0x3E] != 0x88) return 32;
    if (main_ram_image[kScratchAddress + 0x3F] != 0x99) return 33;

    // LQC2 $vf0, 0x40($a0) must NOT overwrite the hardwired constant.
    // The memory at +0x40..+0x4F is garbage, but VF0 stays constant.
    // SQC2 $vf0, 0x50($a0) then stores the constant again at +0x50.
    if (main_ram_image[kScratchAddress + 0x50] != 0x00) return 34;
    if (main_ram_image[kScratchAddress + 0x51] != 0x00) return 35;
    if (main_ram_image[kScratchAddress + 0x52] != 0x00) return 36;
    if (main_ram_image[kScratchAddress + 0x53] != 0x00) return 37;
    if (main_ram_image[kScratchAddress + 0x54] != 0x00) return 38;
    if (main_ram_image[kScratchAddress + 0x55] != 0x00) return 39;
    if (main_ram_image[kScratchAddress + 0x56] != 0x00) return 40;
    if (main_ram_image[kScratchAddress + 0x57] != 0x00) return 41;
    if (main_ram_image[kScratchAddress + 0x58] != 0x00) return 42;
    if (main_ram_image[kScratchAddress + 0x59] != 0x00) return 43;
    if (main_ram_image[kScratchAddress + 0x5A] != 0x00) return 44;
    if (main_ram_image[kScratchAddress + 0x5B] != 0x00) return 45;
    if (main_ram_image[kScratchAddress + 0x5C] != 0x00) return 46;
    if (main_ram_image[kScratchAddress + 0x5D] != 0x00) return 47;
    if (main_ram_image[kScratchAddress + 0x5E] != 0x80) return 48;
    if (main_ram_image[kScratchAddress + 0x5F] != 0x3F) return 49;

    // GPR[0] must still be zero (prologue reset, and LQC2 $vf0 does not write GPR).
    if (state.gpr[0].low != 0ULL || state.gpr[0].high != 0ULL) return 50;

    // QMTC2 $2, $vf12 then SQC2 $vf12, 0x60($a0): the whole GPR2 quadword
    // reaches memory little-endian (low lane first, then high lane).
    if (main_ram_image[kScratchAddress + 0x60] != 0xEF) return 51;
    if (main_ram_image[kScratchAddress + 0x61] != 0xCD) return 52;
    if (main_ram_image[kScratchAddress + 0x62] != 0xAB) return 53;
    if (main_ram_image[kScratchAddress + 0x63] != 0x89) return 54;
    if (main_ram_image[kScratchAddress + 0x64] != 0x67) return 55;
    if (main_ram_image[kScratchAddress + 0x65] != 0x45) return 56;
    if (main_ram_image[kScratchAddress + 0x66] != 0x23) return 57;
    if (main_ram_image[kScratchAddress + 0x67] != 0x01) return 58;
    if (main_ram_image[kScratchAddress + 0x68] != 0x10) return 59;
    if (main_ram_image[kScratchAddress + 0x69] != 0x32) return 60;
    if (main_ram_image[kScratchAddress + 0x6A] != 0x54) return 61;
    if (main_ram_image[kScratchAddress + 0x6B] != 0x76) return 62;
    if (main_ram_image[kScratchAddress + 0x6C] != 0x98) return 63;
    if (main_ram_image[kScratchAddress + 0x6D] != 0xBA) return 64;
    if (main_ram_image[kScratchAddress + 0x6E] != 0xDC) return 65;
    if (main_ram_image[kScratchAddress + 0x6F] != 0xFE) return 66;

    // SQC2 $vf0, 0x80($a0) -> VF0 constant stored at +0x80..+0x8F
    if (main_ram_image[kScratchAddress + 0x80] != 0x00) return 67;
    if (main_ram_image[kScratchAddress + 0x81] != 0x00) return 68;
    if (main_ram_image[kScratchAddress + 0x82] != 0x00) return 69;
    if (main_ram_image[kScratchAddress + 0x83] != 0x00) return 70;
    if (main_ram_image[kScratchAddress + 0x84] != 0x00) return 71;
    if (main_ram_image[kScratchAddress + 0x85] != 0x00) return 72;
    if (main_ram_image[kScratchAddress + 0x86] != 0x00) return 73;
    if (main_ram_image[kScratchAddress + 0x87] != 0x00) return 74;
    if (main_ram_image[kScratchAddress + 0x88] != 0x00) return 75;
    if (main_ram_image[kScratchAddress + 0x89] != 0x00) return 76;
    if (main_ram_image[kScratchAddress + 0x8A] != 0x00) return 77;
    if (main_ram_image[kScratchAddress + 0x8B] != 0x00) return 78;
    if (main_ram_image[kScratchAddress + 0x8C] != 0x00) return 79;
    if (main_ram_image[kScratchAddress + 0x8D] != 0x00) return 80;
    if (main_ram_image[kScratchAddress + 0x8E] != 0x80) return 81;
    if (main_ram_image[kScratchAddress + 0x8F] != 0x3F) return 82;

    // SQC2 $vf4, 0x90($a0) -> VF4 (GPR2 = VF0 constant) stored at +0x90..+0x9F
    if (main_ram_image[kScratchAddress + 0x90] != 0x00) return 83;
    if (main_ram_image[kScratchAddress + 0x91] != 0x00) return 84;
    if (main_ram_image[kScratchAddress + 0x92] != 0x00) return 85;
    if (main_ram_image[kScratchAddress + 0x93] != 0x00) return 86;
    if (main_ram_image[kScratchAddress + 0x94] != 0x00) return 87;
    if (main_ram_image[kScratchAddress + 0x95] != 0x00) return 88;
    if (main_ram_image[kScratchAddress + 0x96] != 0x00) return 89;
    if (main_ram_image[kScratchAddress + 0x97] != 0x00) return 90;
    if (main_ram_image[kScratchAddress + 0x98] != 0x00) return 91;
    if (main_ram_image[kScratchAddress + 0x99] != 0x00) return 92;
    if (main_ram_image[kScratchAddress + 0x9A] != 0x00) return 93;
    if (main_ram_image[kScratchAddress + 0x9B] != 0x00) return 94;
    if (main_ram_image[kScratchAddress + 0x9C] != 0x00) return 95;
    if (main_ram_image[kScratchAddress + 0x9D] != 0x00) return 96;
    if (main_ram_image[kScratchAddress + 0x9E] != 0x80) return 97;
    if (main_ram_image[kScratchAddress + 0x9F] != 0x3F) return 98;

    // QMFC2 $3, $vf12: GPR3 receives the VF12 quadword.
    if (state.gpr[3].low != 0x0123'4567'89AB'CDEFULL) return 99;
    if (state.gpr[3].high != 0xFEDC'BA98'7654'3210ULL) return 100;

    // QMFC2 $2, $vf0: GPR2 receives the hardwired VF0 constant.
    if (state.gpr[2].low != kVf0Constant.low) return 101;
    if (state.gpr[2].high != kVf0Constant.high) return 102;

    // QMFC2 $0, $vf12: the GPR0 write must be discarded.
    if (state.gpr[0].low != 0ULL || state.gpr[0].high != 0ULL) return 103;

    // CFC2 $8, $28: GPR8 receives the value written by CTC2 $3, $28
    // (sign-extended from 32 bits). CFC2 output uses $8 to preserve $a0.
    if (state.gpr[8].low != 0xFFFF'FFFF'8000'0200ULL) return 104;

    // CFC2 $9, $29: VPU_STAT is zero-initialized; read returns 0.
    if (state.gpr[9].low != 0ULL) return 106;

    // ADR 0201: VIADDI vi7 <- vi0 + 15 = 15 (VI0 is hardwired zero).
    // All VI registers started at zero; VI7 is now 15.
    if (state.vu0.vi[7] != 15U) return 107;

    // ADR 0202: VSQI VF5, vi2++ stored the VF5 pattern (loaded earlier from
    // +0x20) to VU Mem0 qword 0, then incremented VI2 from 0 to 1. The bytes
    // AA BB CC DD EE FF 00 11 22 33 44 55 66 77 88 99 load little-endian.
    const auto stored = reps2::runtime::vu0_read_vumem_qword(state.vu0, 0U);
    if (stored.low != 0x1100'FFEE'DDCC'BBAAULL) return 108;
    if (stored.high != 0x9988'7766'5544'3322ULL) return 109;
    if (state.vu0.vi[2] != 1U) return 110;

    // VLQI VF6, vi3++ loaded VU Mem0 qword 0 into VF6 (VI3: 0 -> 1), and
    // SQC2 wrote that same quadword to main RAM at +0xA0.
    for (std::size_t i = 0; i < 16; ++i) {
        if (main_ram_image[kScratchAddress + 0xA0 + i] != main_ram_image[kScratchAddress + 0x20 + i]) return 111;
    }
    if (state.vu0.vi[3] != 1U) return 112;

    // ADR 0203: VIADD vi8 <- vi7 + vi0 = 15; VIADD vi5 <- vi0 + vi0 = 0.
    if (state.vu0.vi[8] != 15U) return 113;
    if (state.vu0.vi[5] != 0U) return 114;

    // ADR 0204: VIAND vi9 <- vi8 & vi7 = 15; VIOR vi10 <- vi5 | vi8 = 15.
    if (state.vu0.vi[9] != 15U) return 115;
    if (state.vu0.vi[10] != 15U) return 116;

    return 0;
}