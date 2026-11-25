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
    state.pc = 0x00009000U;
    // guest PC 0x00009000
    state.hi = state.gpr[1].low;
    state.pc = 0x00009004U;
    // guest PC 0x00009004
    if (20 != 0) state.gpr[20].low = state.hi;
    state.pc = 0x00009008U;
    // guest PC 0x00009008
    state.lo = state.gpr[2].low;
    state.pc = 0x0000900CU;
    // guest PC 0x0000900C
    if (21 != 0) state.gpr[21].low = state.lo;
    state.pc = 0x00009010U;
    // guest PC 0x00009010
    const std::int64_t multiply_lhs_0004 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[1].low));
    const std::int64_t multiply_rhs_0004 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[2].low));
    const std::int64_t multiply_product_0004 = multiply_lhs_0004 * multiply_rhs_0004;
    const std::uint64_t multiply_product_bits_0004 = static_cast<std::uint64_t>(multiply_product_0004);
    const std::uint32_t multiply_low_0004 = static_cast<std::uint32_t>(multiply_product_bits_0004);
    const std::uint32_t multiply_high_0004 = static_cast<std::uint32_t>(multiply_product_bits_0004 >> 32U);
    state.lo = sign_extend_word(multiply_low_0004);
    state.hi = sign_extend_word(multiply_high_0004);
    if (22 != 0) state.gpr[22].low = state.lo;
    state.pc = 0x00009014U;
    // guest PC 0x00009014
    if (24 != 0) state.gpr[24].low = state.hi;
    state.pc = 0x00009018U;
    // guest PC 0x00009018
    if (25 != 0) state.gpr[25].low = state.lo;
    state.pc = 0x0000901CU;
    // guest PC 0x0000901C
    const std::int64_t multiply_lhs_0007 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[1].low));
    const std::int64_t multiply_rhs_0007 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[2].low));
    const std::int64_t multiply_product_0007 = multiply_lhs_0007 * multiply_rhs_0007;
    const std::uint64_t multiply_product_bits_0007 = static_cast<std::uint64_t>(multiply_product_0007);
    const std::uint32_t multiply_low_0007 = static_cast<std::uint32_t>(multiply_product_bits_0007);
    const std::uint32_t multiply_high_0007 = static_cast<std::uint32_t>(multiply_product_bits_0007 >> 32U);
    state.lo = sign_extend_word(multiply_low_0007);
    state.hi = sign_extend_word(multiply_high_0007);
    if (0 != 0) state.gpr[0].low = state.lo;
    state.pc = 0x00009020U;
    // guest PC 0x00009020
    if (26 != 0) state.gpr[26].low = state.hi;
    state.pc = 0x00009024U;
    // guest PC 0x00009024
    if (27 != 0) state.gpr[27].low = state.lo;
    state.pc = 0x00009028U;
    // guest PC 0x00009028
    const std::uint64_t multiply_lhs_0010 = static_cast<std::uint32_t>(state.gpr[3].low);
    const std::uint64_t multiply_rhs_0010 = static_cast<std::uint32_t>(state.gpr[2].low);
    const std::uint64_t multiply_product_bits_0010 = multiply_lhs_0010 * multiply_rhs_0010;
    const std::uint32_t multiply_low_0010 = static_cast<std::uint32_t>(multiply_product_bits_0010);
    const std::uint32_t multiply_high_0010 = static_cast<std::uint32_t>(multiply_product_bits_0010 >> 32U);
    state.lo = sign_extend_word(multiply_low_0010);
    state.hi = sign_extend_word(multiply_high_0010);
    if (23 != 0) state.gpr[23].low = state.lo;
    state.pc = 0x0000902CU;
    // guest PC 0x0000902C
    const std::int64_t multiply_lhs_0011 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[4].low));
    const std::int64_t multiply_rhs_0011 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[5].low));
    const std::int64_t multiply_product_0011 = multiply_lhs_0011 * multiply_rhs_0011;
    const std::uint64_t multiply_product_bits_0011 = static_cast<std::uint64_t>(multiply_product_0011);
    const std::uint32_t multiply_low_0011 = static_cast<std::uint32_t>(multiply_product_bits_0011);
    const std::uint32_t multiply_high_0011 = static_cast<std::uint32_t>(multiply_product_bits_0011 >> 32U);
    state.lo = sign_extend_word(multiply_low_0011);
    state.hi = sign_extend_word(multiply_high_0011);
    if (6 != 0) state.gpr[6].low = state.lo;
    state.pc = 0x00009030U;
    // guest PC 0x00009030
    if (13 != 0) state.gpr[13].low = state.hi;
    state.pc = 0x00009034U;
    // guest PC 0x00009034
    if (14 != 0) state.gpr[14].low = state.lo;
    state.pc = 0x00009038U;
    // guest PC 0x00009038
    const std::int64_t multiply_lhs_0014 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[8].low));
    const std::int64_t multiply_rhs_0014 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[9].low));
    const std::int64_t multiply_product_0014 = multiply_lhs_0014 * multiply_rhs_0014;
    const std::uint64_t multiply_product_bits_0014 = static_cast<std::uint64_t>(multiply_product_0014);
    const std::uint32_t multiply_low_0014 = static_cast<std::uint32_t>(multiply_product_bits_0014);
    const std::uint32_t multiply_high_0014 = static_cast<std::uint32_t>(multiply_product_bits_0014 >> 32U);
    state.lo = sign_extend_word(multiply_low_0014);
    state.hi = sign_extend_word(multiply_high_0014);
    if (10 != 0) state.gpr[10].low = state.lo;
    state.pc = 0x0000903CU;
    // guest PC 0x0000903C
    if (15 != 0) state.gpr[15].low = state.hi;
    state.pc = 0x00009040U;
    // guest PC 0x00009040
    if (16 != 0) state.gpr[16].low = state.lo;
    state.pc = 0x00009044U;
    // guest PC 0x00009044
    const std::int64_t multiply_lhs_0017 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[11].low));
    const std::int64_t multiply_rhs_0017 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[12].low));
    const std::int64_t multiply_product_0017 = multiply_lhs_0017 * multiply_rhs_0017;
    const std::uint64_t multiply_product_bits_0017 = static_cast<std::uint64_t>(multiply_product_0017);
    const std::uint32_t multiply_low_0017 = static_cast<std::uint32_t>(multiply_product_bits_0017);
    const std::uint32_t multiply_high_0017 = static_cast<std::uint32_t>(multiply_product_bits_0017 >> 32U);
    state.lo = sign_extend_word(multiply_low_0017);
    state.hi = sign_extend_word(multiply_high_0017);
    if (17 != 0) state.gpr[17].low = state.lo;
    state.pc = 0x00009048U;
    // guest PC 0x00009048
    if (18 != 0) state.gpr[18].low = state.hi;
    state.pc = 0x0000904CU;
    // guest PC 0x0000904C
    if (19 != 0) state.gpr[19].low = state.lo;
    state.pc = 0x00009050U;
    // guest PC 0x00009050
    const std::uint64_t multiply_lhs_0020 = static_cast<std::uint32_t>(state.gpr[3].low);
    const std::uint64_t multiply_rhs_0020 = static_cast<std::uint32_t>(state.gpr[2].low);
    const std::uint64_t multiply_product_bits_0020 = multiply_lhs_0020 * multiply_rhs_0020;
    const std::uint32_t multiply_low_0020 = static_cast<std::uint32_t>(multiply_product_bits_0020);
    const std::uint32_t multiply_high_0020 = static_cast<std::uint32_t>(multiply_product_bits_0020 >> 32U);
    state.lo = sign_extend_word(multiply_low_0020);
    state.hi = sign_extend_word(multiply_high_0020);
    if (3 != 0) state.gpr[3].low = state.lo;
    state.pc = 0x00009054U;
    // guest PC 0x00009054
    const std::int64_t multiply_lhs_0021 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[1].low));
    const std::int64_t multiply_rhs_0021 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[2].low));
    const std::int64_t multiply_product_0021 = multiply_lhs_0021 * multiply_rhs_0021;
    const std::uint64_t multiply_product_bits_0021 = static_cast<std::uint64_t>(multiply_product_0021);
    const std::uint32_t multiply_low_0021 = static_cast<std::uint32_t>(multiply_product_bits_0021);
    const std::uint32_t multiply_high_0021 = static_cast<std::uint32_t>(multiply_product_bits_0021 >> 32U);
    state.lo = sign_extend_word(multiply_low_0021);
    state.hi = sign_extend_word(multiply_high_0021);
    if (1 != 0) state.gpr[1].low = state.lo;
    state.pc = 0x00009058U;
    // guest PC 0x00009058
    state.hi1 = state.gpr[28].low;
    state.pc = 0x0000905CU;
    // guest PC 0x0000905C
    state.lo1 = state.gpr[29].low;
    state.pc = 0x00009060U;
    // guest PC 0x00009060
    if (30 != 0) state.gpr[30].low = state.hi1;
    state.pc = 0x00009064U;
    // guest PC 0x00009064
    if (31 != 0) state.gpr[31].low = state.lo1;
    state.pc = 0x00009068U;
    // guest PC 0x00009068
    const std::int64_t maddacc_lhs_0026 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[1].low));
    const std::int64_t maddacc_rhs_0026 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[2].low));
    const std::uint64_t maddacc_product_0026 = static_cast<std::uint64_t>(maddacc_lhs_0026 * maddacc_rhs_0026);
    const std::uint64_t maddacc_pair_0026 = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.hi)) << 32U) | static_cast<std::uint32_t>(state.lo);
    const std::uint64_t maddacc_sum_0026 = maddacc_pair_0026 + maddacc_product_0026;
    state.lo = sign_extend_word(static_cast<std::uint32_t>(maddacc_sum_0026));
    state.hi = sign_extend_word(static_cast<std::uint32_t>(maddacc_sum_0026 >> 32U));
    state.pc = 0x0000906CU;
    // guest PC 0x0000906C
    const std::uint64_t maddacc_lhs_0027 = static_cast<std::uint32_t>(state.gpr[3].low);
    const std::uint64_t maddacc_rhs_0027 = static_cast<std::uint32_t>(state.gpr[2].low);
    const std::uint64_t maddacc_product_0027 = maddacc_lhs_0027 * maddacc_rhs_0027;
    const std::uint64_t maddacc_pair_0027 = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.hi)) << 32U) | static_cast<std::uint32_t>(state.lo);
    const std::uint64_t maddacc_sum_0027 = maddacc_pair_0027 + maddacc_product_0027;
    state.lo = sign_extend_word(static_cast<std::uint32_t>(maddacc_sum_0027));
    state.hi = sign_extend_word(static_cast<std::uint32_t>(maddacc_sum_0027 >> 32U));
    return Status::ok;
}
} // namespace reps2::generated
