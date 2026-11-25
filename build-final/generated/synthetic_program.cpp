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
    state.pc = 0x00100000U;
    // guest PC 0x00100000
    if (!is_word_value(state.gpr[0].low)) return Status::undefined_word_operand;
    const std::uint32_t lhs_0000 = static_cast<std::uint32_t>(state.gpr[0].low);
    const std::uint32_t imm_0000 = (0x0005U & 0x8000U) != 0 ? (0x0005U | 0xFFFF0000U) : 0x0005U;
    const std::uint32_t result_0000 = lhs_0000 + imm_0000;
    if (8 != 0) state.gpr[8].low = sign_extend_word(result_0000);
    state.pc = 0x00100004U;
    // guest PC 0x00100004
    if (!is_word_value(state.gpr[0].low)) return Status::undefined_word_operand;
    const std::uint32_t lhs_0001 = static_cast<std::uint32_t>(state.gpr[0].low);
    const std::uint32_t imm_0001 = (0x0007U & 0x8000U) != 0 ? (0x0007U | 0xFFFF0000U) : 0x0007U;
    const std::uint32_t result_0001 = lhs_0001 + imm_0001;
    if (9 != 0) state.gpr[9].low = sign_extend_word(result_0001);
    state.pc = 0x00100008U;
    // guest PC 0x00100008
    if (!is_word_value(state.gpr[8].low) || !is_word_value(state.gpr[9].low)) return Status::undefined_word_operand;
    const std::uint32_t lhs_0002 = static_cast<std::uint32_t>(state.gpr[8].low);
    const std::uint32_t rhs_0002 = static_cast<std::uint32_t>(state.gpr[9].low);
    const std::uint32_t result_0002 = lhs_0002 + rhs_0002;
    if (10 != 0) state.gpr[10].low = sign_extend_word(result_0002);
    state.pc = 0x0010000CU;
    // guest PC 0x0010000C
    if (11 != 0) state.gpr[11].low = sign_extend_word(static_cast<std::uint32_t>(0x8000U) << 16U);
    state.pc = 0x00100010U;
    // guest PC 0x00100010
    if (12 != 0) state.gpr[12].low = state.gpr[0].low | 0x1234ULL;
    state.pc = 0x00100014U;
    // guest PC 0x00100014
    if (13 != 0) state.gpr[13].low = state.gpr[12].low ^ 0xFFFFULL;
    state.pc = 0x00100018U;
    // guest PC 0x00100018
    if (14 != 0) state.gpr[14].low = state.gpr[13].low & 0x00FFULL;
    state.pc = 0x0010001CU;
    // guest PC 0x0010001C
    if (15 != 0) state.gpr[15].low = sign_extend_word(static_cast<std::uint32_t>(state.gpr[8].low) << 2U);
    state.pc = 0x00100020U;
    // guest PC 0x00100020
    if (16 != 0) state.gpr[16].low = sign_extend_word(static_cast<std::uint32_t>(state.gpr[15].low) >> 1U);
    state.pc = 0x00100024U;
    // guest PC 0x00100024
    if (17 != 0) state.gpr[17].low = sign_extend_word(arithmetic_shift_right_word(static_cast<std::uint32_t>(state.gpr[11].low), 31U));
    state.pc = 0x00100028U;
    // guest PC 0x00100028
    if (!is_word_value(state.gpr[20].low)) return Status::undefined_word_operand;
    const std::uint32_t lhs_0010 = static_cast<std::uint32_t>(state.gpr[20].low);
    const std::uint32_t imm_0010 = (0x0001U & 0x8000U) != 0 ? (0x0001U | 0xFFFF0000U) : 0x0001U;
    const std::uint32_t result_0010 = lhs_0010 + imm_0010;
    if (18 != 0) state.gpr[18].low = sign_extend_word(result_0010);
    state.pc = 0x0010002CU;
    // guest PC 0x0010002C
    if (!is_word_value(state.gpr[21].low)) return Status::undefined_word_operand;
    const std::int64_t lhs_0011 = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[21].low));
    const std::int64_t rhs_0011 = sign_extend_word_signed(sign_extend_immediate(0x0001U));
    const std::int64_t result_0011 = lhs_0011 + rhs_0011;
    if (result_0011 < -2147483648LL || result_0011 > 2147483647LL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, false);
    if (22 != 0) state.gpr[22].low = sign_extend_word(static_cast<std::uint32_t>(result_0011));
    state.pc = 0x00100030U;
    // guest PC 0x00100030
    if (23 != 0) state.gpr[23].low = state.gpr[1].low + state.gpr[2].low;
    state.pc = 0x00100034U;
    // guest PC 0x00100034
    if (24 != 0) state.gpr[24].low = state.gpr[1].low - state.gpr[2].low;
    state.pc = 0x00100038U;
    // guest PC 0x00100038
    if (25 != 0) state.gpr[25].low = state.gpr[1].low + static_cast<std::uint64_t>(-16LL);
    state.pc = 0x0010003CU;
    // guest PC 0x0010003C
    if (26 != 0) state.gpr[26].low = state.gpr[2].low << 4U;
    state.pc = 0x00100040U;
    // guest PC 0x00100040
    if (27 != 0) state.gpr[27].low = state.gpr[1].low >> 32U;
    state.pc = 0x00100044U;
    // guest PC 0x00100044
    if (28 != 0) state.gpr[28].low = arithmetic_shift_right_doubleword(state.gpr[1].low, 32U);
    state.pc = 0x00100048U;
    // guest PC 0x00100048
    if (29 != 0) state.gpr[29].low = state.gpr[3].low + static_cast<std::uint64_t>(1LL);
    state.pc = 0x0010004CU;
    // guest PC 0x0010004C
    if (!is_word_value(state.gpr[5].low)) return Status::undefined_word_operand;
    const std::uint32_t shift_amount_0019 = static_cast<std::uint32_t>(state.gpr[4].low) & 0x1FU;
    if (6 != 0) state.gpr[6].low = sign_extend_word(static_cast<std::uint32_t>(state.gpr[5].low) << shift_amount_0019);
    state.pc = 0x00100050U;
    // guest PC 0x00100050
    if (!is_word_value(state.gpr[5].low)) return Status::undefined_word_operand;
    const std::uint32_t shift_amount_0020 = static_cast<std::uint32_t>(state.gpr[4].low) & 0x1FU;
    if (7 != 0) state.gpr[7].low = sign_extend_word(static_cast<std::uint32_t>(state.gpr[5].low) >> shift_amount_0020);
    state.pc = 0x00100054U;
    // guest PC 0x00100054
    if (!is_word_value(state.gpr[5].low)) return Status::undefined_word_operand;
    const std::uint32_t shift_amount_0021 = static_cast<std::uint32_t>(state.gpr[4].low) & 0x1FU;
    if (22 != 0) state.gpr[22].low = sign_extend_word(arithmetic_shift_right_word(static_cast<std::uint32_t>(state.gpr[5].low), static_cast<std::uint8_t>(shift_amount_0021)));
    state.pc = 0x00100058U;
    // guest PC 0x00100058
    const std::uint32_t shift_amount_0022 = static_cast<std::uint32_t>(state.gpr[4].low) & 0x3FU;
    if (30 != 0) state.gpr[30].low = state.gpr[5].low << shift_amount_0022;
    state.pc = 0x0010005CU;
    // guest PC 0x0010005C
    const std::uint32_t shift_amount_0023 = static_cast<std::uint32_t>(state.gpr[4].low) & 0x3FU;
    if (31 != 0) state.gpr[31].low = state.gpr[5].low >> shift_amount_0023;
    state.pc = 0x00100060U;
    // guest PC 0x00100060
    const std::uint32_t shift_amount_0024 = static_cast<std::uint32_t>(state.gpr[4].low) & 0x3FU;
    if (19 != 0) state.gpr[19].low = arithmetic_shift_right_doubleword(state.gpr[5].low, static_cast<std::uint8_t>(shift_amount_0024));
    state.pc = 0x00100064U;
    // guest PC 0x00100064
    if (3 != 0) state.gpr[3] = parallel_add_u32(state.gpr[1], state.gpr[2]);
    state.pc = 0x00100068U;
    // guest PC 0x00100068
    if (0 != 0) state.gpr[0] = parallel_add_u32(state.gpr[1], state.gpr[2]);
    return Status::ok;
}
} // namespace reps2::generated
