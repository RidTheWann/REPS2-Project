#include <bit>
#include <cstdint>
#include <limits>
#include "reps2/runtime/ee_state.hpp"

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept {
using reps2::runtime::Status;
using reps2::runtime::is_word_value;
using reps2::runtime::arithmetic_shift_right_word;
using reps2::runtime::arithmetic_shift_right_doubleword;
using reps2::runtime::sign_extend_immediate;
using reps2::runtime::sign_extend_word;
using reps2::runtime::sign_extend_word_signed;
using reps2::runtime::parallel_add_u32;             using reps2::runtime::parallel_add_word;
using reps2::runtime::parallel_add_u8;
using reps2::runtime::parallel_compare_equal_word;
using reps2::runtime::parallel_sub_word;
using reps2::runtime::parallel_compare_greater_word;
using reps2::runtime::parallel_add_halfword;
using reps2::runtime::parallel_sub_halfword;
using reps2::runtime::parallel_sub_byte;
using reps2::runtime::parallel_copy_low_doubleword;
using reps2::runtime::parallel_and;
using reps2::runtime::parallel_xor;
using reps2::runtime::parallel_or;
using reps2::runtime::parallel_nor;
using reps2::runtime::parallel_extend_low_word;
using reps2::runtime::parallel_extend_low_byte;
using reps2::runtime::parallel_pack_word;
using reps2::runtime::parallel_copy_upper_doubleword;
using reps2::runtime::parallel_exchange_even_word;
using reps2::runtime::parallel_rotate_3_words;
using reps2::runtime::parallel_copy_halfword;
using reps2::runtime::parallel_extend_5;
using reps2::runtime::parallel_pack_5;

using reps2::runtime::funnel_shift_right_vec;
using reps2::runtime::store_fpu_control;
using reps2::runtime::apply_fpu_flags;
using reps2::runtime::apply_fpu_condition;
using reps2::runtime::fpu_compare;
using reps2::runtime::FpuCompare;
using reps2::runtime::fpu_add;
using reps2::runtime::fpu_sub;
using reps2::runtime::fpu_mul;
using reps2::runtime::fpu_div;
using reps2::runtime::fpu_sqrt;
using reps2::runtime::fpu_rsqrt;
using reps2::runtime::fpu_abs;
using reps2::runtime::fpu_neg;
using reps2::runtime::fpu_mov;
using reps2::runtime::fpu_max;
using reps2::runtime::fpu_min;
using reps2::runtime::fpu_cvt_w_s;
using reps2::runtime::fpu_cvt_s_w;
using reps2::runtime::fpu_madd;
using reps2::runtime::fpu_msub;
using reps2::runtime::load_u8;
using reps2::runtime::load_u16;
using reps2::runtime::load_u32;
using reps2::runtime::load_u64;
using reps2::runtime::load_u128;
using reps2::runtime::store_u8;
using reps2::runtime::store_u16;
using reps2::runtime::store_u32;
using reps2::runtime::store_u64;
using reps2::runtime::store_u128;
using reps2::runtime::load_u32_left;
using reps2::runtime::load_u32_right;
using reps2::runtime::store_u32_left;
using reps2::runtime::store_u32_right;
using reps2::runtime::load_u64_left;
using reps2::runtime::load_u64_right;
using reps2::runtime::store_u64_left;
using reps2::runtime::store_u64_right;
using reps2::runtime::read_cop0;
using reps2::runtime::write_cop0;
using reps2::runtime::vu0_read_control;
using reps2::runtime::vu0_write_control;
using reps2::runtime::vu0_rotate_right_word;
using reps2::runtime::vu0_sub_lanes;
using reps2::runtime::vu0_add_lanes;
using reps2::runtime::vu0_broadcast_max;
using reps2::runtime::vu0_broadcast_min;
using reps2::runtime::vu0_opmula;
using reps2::runtime::vu0_opmsub;
using reps2::runtime::vu0_acc_broadcast_mul;
using reps2::runtime::vu0_move_masked;
using reps2::runtime::vu0_broadcast_add;
using reps2::runtime::vu0_broadcast_madd;
using reps2::runtime::vu0_read_vf;
using reps2::runtime::vu0_write_vf;
using reps2::runtime::enable_interrupts;
using reps2::runtime::disable_interrupts;
state.gpr[0] = {};
    state.pc = 0x00010000U;
    // guest PC 0x00010000
    state.fpr[1] = static_cast<std::uint32_t>(state.gpr[1].low);
    state.pc = 0x00010004U;
    // guest PC 0x00010004
    state.fpr[2] = static_cast<std::uint32_t>(state.gpr[2].low);
    state.pc = 0x00010008U;
    // guest PC 0x00010008
    state.fpr[3] = static_cast<std::uint32_t>(state.gpr[3].low);
    state.pc = 0x0001000CU;
    // guest PC 0x0001000C
    state.fpr[4] = static_cast<std::uint32_t>(state.gpr[4].low);
    state.pc = 0x00010010U;
    // guest PC 0x00010010
    const auto fpu_result_0004 = fpu_add(state.fpr[1], state.fpr[2]);
    state.fpr[10] = fpu_result_0004.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0004);
    state.pc = 0x00010014U;
    // guest PC 0x00010014
    const auto fpu_result_0005 = fpu_sub(state.fpr[1], state.fpr[2]);
    state.fpr[11] = fpu_result_0005.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0005);
    state.pc = 0x00010018U;
    // guest PC 0x00010018
    const auto fpu_result_0006 = fpu_mul(state.fpr[1], state.fpr[2]);
    state.fpr[12] = fpu_result_0006.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0006);
    state.pc = 0x0001001CU;
    // guest PC 0x0001001C
    const auto fpu_result_0007 = fpu_div(state.fpr[1], state.fpr[2]);
    state.fpr[13] = fpu_result_0007.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0007);
    state.pc = 0x00010020U;
    // guest PC 0x00010020
    const auto fpu_result_0008 = fpu_sqrt(state.fpr[4]);
    state.fpr[14] = fpu_result_0008.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0008);
    state.pc = 0x00010024U;
    // guest PC 0x00010024
    const auto fpu_result_0009 = fpu_abs(state.fpr[11]);
    state.fpr[15] = fpu_result_0009.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0009);
    state.pc = 0x00010028U;
    // guest PC 0x00010028
    const auto fpu_result_0010 = fpu_mov(state.fpr[11]);
    state.fpr[16] = fpu_result_0010.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0010);
    state.pc = 0x0001002CU;
    // guest PC 0x0001002C
    const auto fpu_result_0011 = fpu_neg(state.fpr[11]);
    state.fpr[17] = fpu_result_0011.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0011);
    state.pc = 0x00010030U;
    // guest PC 0x00010030
    const auto fpu_result_0012 = fpu_rsqrt(state.fpr[1], state.fpr[4]);
    state.fpr[18] = fpu_result_0012.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0012);
    state.pc = 0x00010034U;
    // guest PC 0x00010034
    const auto fpu_result_0013 = fpu_add(state.fpr[1], state.fpr[2]);
    state.acc = fpu_result_0013.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0013);
    state.pc = 0x00010038U;
    // guest PC 0x00010038
    const auto fpu_result_0014 = fpu_madd(state.acc, state.fpr[1], state.fpr[2]);
    state.fpr[19] = fpu_result_0014.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0014);
    state.pc = 0x0001003CU;
    // guest PC 0x0001003C
    const auto fpu_result_0015 = fpu_sub(state.fpr[1], state.fpr[2]);
    state.acc = fpu_result_0015.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0015);
    state.pc = 0x00010040U;
    // guest PC 0x00010040
    const auto fpu_result_0016 = fpu_msub(state.acc, state.fpr[1], state.fpr[2]);
    state.fpr[20] = fpu_result_0016.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0016);
    state.pc = 0x00010044U;
    // guest PC 0x00010044
    const auto fpu_result_0017 = fpu_mul(state.fpr[1], state.fpr[2]);
    state.acc = fpu_result_0017.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0017);
    state.pc = 0x00010048U;
    // guest PC 0x00010048
    const auto fpu_result_0018 = fpu_madd(state.acc, state.fpr[1], state.fpr[2]);
    state.acc = fpu_result_0018.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0018);
    state.pc = 0x0001004CU;
    // guest PC 0x0001004C
    const auto fpu_result_0019 = fpu_msub(state.acc, state.fpr[1], state.fpr[2]);
    state.acc = fpu_result_0019.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0019);
    state.pc = 0x00010050U;
    // guest PC 0x00010050
    const auto fpu_result_0020 = fpu_max(state.fpr[11], state.fpr[3]);
    state.fpr[21] = fpu_result_0020.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0020);
    state.pc = 0x00010054U;
    // guest PC 0x00010054
    const auto fpu_result_0021 = fpu_min(state.fpr[11], state.fpr[3]);
    state.fpr[22] = fpu_result_0021.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0021);
    state.pc = 0x00010058U;
    // guest PC 0x00010058
    const auto fpu_result_0022 = fpu_cvt_w_s(state.fpr[11]);
    state.fpr[23] = fpu_result_0022.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0022);
    state.pc = 0x0001005CU;
    // guest PC 0x0001005C
    const auto fpu_result_0023 = fpu_cvt_s_w(state.fpr[23]);
    state.fpr[24] = fpu_result_0023.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0023);
    state.pc = 0x00010060U;
    // guest PC 0x00010060
    state.fpr[0] = static_cast<std::uint32_t>(state.gpr[0].low);
    state.pc = 0x00010064U;
    // guest PC 0x00010064
    const auto fpu_result_0025 = fpu_div(state.fpr[1], state.fpr[0]);
    state.fpr[25] = fpu_result_0025.bits;
    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_0025);
    state.pc = 0x00010068U;
    // guest PC 0x00010068
    if (5 != 0) state.gpr[5].low = sign_extend_word(state.fcsr);
    return Status::ok;
}
} // namespace reps2::generated
