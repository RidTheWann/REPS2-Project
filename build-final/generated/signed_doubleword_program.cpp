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
    state.pc = 0x0000B000U;
    // guest PC 0x0000B000
    const std::uint64_t signed_add_lhs_0000 = state.gpr[8].low;
    const std::uint64_t signed_add_rhs_0000 = state.gpr[9].low;
    const std::uint64_t signed_add_result_0000 = signed_add_lhs_0000 + signed_add_rhs_0000;
    if (((~(signed_add_lhs_0000 ^ signed_add_rhs_0000) & (signed_add_lhs_0000 ^ signed_add_result_0000)) & 0x8000000000000000ULL) != 0ULL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, false);
    if (10 != 0) state.gpr[10].low = signed_add_result_0000;
    state.pc = 0x0000B004U;
    // guest PC 0x0000B004
    const std::uint64_t signed_sub_lhs_0001 = state.gpr[12].low;
    const std::uint64_t signed_sub_rhs_0001 = state.gpr[13].low;
    const std::uint64_t signed_sub_result_0001 = signed_sub_lhs_0001 - signed_sub_rhs_0001;
    if ((((signed_sub_lhs_0001 ^ signed_sub_rhs_0001) & (signed_sub_lhs_0001 ^ signed_sub_result_0001)) & 0x8000000000000000ULL) != 0ULL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, false);
    if (11 != 0) state.gpr[11].low = signed_sub_result_0001;
    state.pc = 0x0000B008U;
    // guest PC 0x0000B008
    const std::uint64_t signed_addi_lhs_0002 = state.gpr[15].low;
    const std::uint64_t signed_addi_rhs_0002 = static_cast<std::uint64_t>(-2LL);
    const std::uint64_t signed_addi_result_0002 = signed_addi_lhs_0002 + signed_addi_rhs_0002;
    if (((~(signed_addi_lhs_0002 ^ signed_addi_rhs_0002) & (signed_addi_lhs_0002 ^ signed_addi_result_0002)) & 0x8000000000000000ULL) != 0ULL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, false);
    if (14 != 0) state.gpr[14].low = signed_addi_result_0002;
    state.pc = 0x0000B00CU;
    // guest PC 0x0000B00C
    const std::uint64_t signed_add_lhs_0003 = state.gpr[16].low;
    const std::uint64_t signed_add_rhs_0003 = state.gpr[17].low;
    const std::uint64_t signed_add_result_0003 = signed_add_lhs_0003 + signed_add_rhs_0003;
    if (((~(signed_add_lhs_0003 ^ signed_add_rhs_0003) & (signed_add_lhs_0003 ^ signed_add_result_0003)) & 0x8000000000000000ULL) != 0ULL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, false);
    if (0 != 0) state.gpr[0].low = signed_add_result_0003;
    state.pc = 0x0000B010U;
    // guest PC 0x0000B010
    const std::uint64_t signed_add_lhs_0004 = state.gpr[18].low;
    const std::uint64_t signed_add_rhs_0004 = state.gpr[19].low;
    const std::uint64_t signed_add_result_0004 = signed_add_lhs_0004 + signed_add_rhs_0004;
    if (((~(signed_add_lhs_0004 ^ signed_add_rhs_0004) & (signed_add_lhs_0004 ^ signed_add_result_0004)) & 0x8000000000000000ULL) != 0ULL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, false);
    if (18 != 0) state.gpr[18].low = signed_add_result_0004;
    return Status::ok;
}
} // namespace reps2::generated
