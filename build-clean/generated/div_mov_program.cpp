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
    // nop
    state.pc = 0x00100004U;
    // guest PC 0x00100004
    // nop
    state.pc = 0x00100008U;
    // guest PC 0x00100008
    // nop
    state.pc = 0x0010000CU;
    // guest PC 0x0010000C
    // nop
    state.pc = 0x00100010U;
    // guest PC 0x00100010
    {
        const std::int32_t div_lhs_0004 = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[1].low));
        const std::int32_t div_rhs_0004 = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[2].low));
        if (div_rhs_0004 == 0) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0004 >= 0 ? -1 : 1));
            state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0004));
        } else if (div_lhs_0004 == std::numeric_limits<std::int32_t>::min() && div_rhs_0004 == -1) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::min()));
            state.hi = sign_extend_word(0U);
        } else {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0004 / div_rhs_0004));
            state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0004 % div_rhs_0004));
        }
    }
    state.pc = 0x00100014U;
    // guest PC 0x00100014
    if (20 != 0) state.gpr[20].low = state.hi;
    state.pc = 0x00100018U;
    // guest PC 0x00100018
    if (21 != 0) state.gpr[21].low = state.lo;
    state.pc = 0x0010001CU;
    // guest PC 0x0010001C
    {
        const std::int32_t div_lhs_0007 = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[3].low));
        const std::int32_t div_rhs_0007 = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[0].low));
        if (div_rhs_0007 == 0) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0007 >= 0 ? -1 : 1));
            state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0007));
        } else if (div_lhs_0007 == std::numeric_limits<std::int32_t>::min() && div_rhs_0007 == -1) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::min()));
            state.hi = sign_extend_word(0U);
        } else {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0007 / div_rhs_0007));
            state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0007 % div_rhs_0007));
        }
    }
    state.pc = 0x00100020U;
    // guest PC 0x00100020
    if (22 != 0) state.gpr[22].low = state.hi;
    state.pc = 0x00100024U;
    // guest PC 0x00100024
    if (23 != 0) state.gpr[23].low = state.lo;
    state.pc = 0x00100028U;
    // guest PC 0x00100028
    {
        const std::int32_t div_lhs_0010 = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[4].low));
        const std::int32_t div_rhs_0010 = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[0].low));
        if (div_rhs_0010 == 0) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0010 >= 0 ? -1 : 1));
            state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0010));
        } else if (div_lhs_0010 == std::numeric_limits<std::int32_t>::min() && div_rhs_0010 == -1) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::min()));
            state.hi = sign_extend_word(0U);
        } else {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0010 / div_rhs_0010));
            state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0010 % div_rhs_0010));
        }
    }
    state.pc = 0x0010002CU;
    // guest PC 0x0010002C
    if (24 != 0) state.gpr[24].low = state.hi;
    state.pc = 0x00100030U;
    // guest PC 0x00100030
    if (25 != 0) state.gpr[25].low = state.lo;
    state.pc = 0x00100034U;
    // guest PC 0x00100034
    {
        const std::int32_t div_lhs_0013 = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[5].low));
        const std::int32_t div_rhs_0013 = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[6].low));
        if (div_rhs_0013 == 0) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0013 >= 0 ? -1 : 1));
            state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0013));
        } else if (div_lhs_0013 == std::numeric_limits<std::int32_t>::min() && div_rhs_0013 == -1) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::min()));
            state.hi = sign_extend_word(0U);
        } else {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0013 / div_rhs_0013));
            state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_0013 % div_rhs_0013));
        }
    }
    state.pc = 0x00100038U;
    // guest PC 0x00100038
    if (26 != 0) state.gpr[26].low = state.hi;
    state.pc = 0x0010003CU;
    // guest PC 0x0010003C
    if (27 != 0) state.gpr[27].low = state.lo;
    state.pc = 0x00100040U;
    // guest PC 0x00100040
    {
        const std::uint32_t div_lhs_0016 = static_cast<std::uint32_t>(state.gpr[7].low);
        const std::uint32_t div_rhs_0016 = static_cast<std::uint32_t>(state.gpr[8].low);
        if (div_rhs_0016 == 0U) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(UINT32_MAX));
            state.hi = sign_extend_word(div_lhs_0016);
        } else {
            state.lo = sign_extend_word(div_lhs_0016 / div_rhs_0016);
            state.hi = sign_extend_word(div_lhs_0016 % div_rhs_0016);
        }
    }
    state.pc = 0x00100044U;
    // guest PC 0x00100044
    if (28 != 0) state.gpr[28].low = state.hi;
    state.pc = 0x00100048U;
    // guest PC 0x00100048
    if (29 != 0) state.gpr[29].low = state.lo;
    state.pc = 0x0010004CU;
    // guest PC 0x0010004C
    {
        const std::uint32_t div_lhs_0019 = static_cast<std::uint32_t>(state.gpr[7].low);
        const std::uint32_t div_rhs_0019 = static_cast<std::uint32_t>(state.gpr[0].low);
        if (div_rhs_0019 == 0U) {
            state.lo = sign_extend_word(static_cast<std::uint32_t>(UINT32_MAX));
            state.hi = sign_extend_word(div_lhs_0019);
        } else {
            state.lo = sign_extend_word(div_lhs_0019 / div_rhs_0019);
            state.hi = sign_extend_word(div_lhs_0019 % div_rhs_0019);
        }
    }
    state.pc = 0x00100050U;
    // guest PC 0x00100050
    if (30 != 0) state.gpr[30].low = state.hi;
    state.pc = 0x00100054U;
    // guest PC 0x00100054
    if (31 != 0) state.gpr[31].low = state.lo;
    state.pc = 0x00100058U;
    // guest PC 0x00100058
    if (10 != 0 && state.gpr[12].low == 0U) state.gpr[10].low = state.gpr[11].low;
    state.pc = 0x0010005CU;
    // guest PC 0x0010005C
    if (13 != 0 && state.gpr[2].low == 0U) state.gpr[13].low = state.gpr[11].low;
    state.pc = 0x00100060U;
    // guest PC 0x00100060
    if (14 != 0 && state.gpr[2].low != 0U) state.gpr[14].low = state.gpr[11].low;
    state.pc = 0x00100064U;
    // guest PC 0x00100064
    if (15 != 0 && state.gpr[12].low != 0U) state.gpr[15].low = state.gpr[11].low;
    state.pc = 0x00100068U;
    // guest PC 0x00100068
    if (0 != 0 && state.gpr[12].low == 0U) state.gpr[0].low = state.gpr[11].low;
    state.pc = 0x0010006CU;
    // guest PC 0x0010006C
    if (0 != 0 && state.gpr[2].low != 0U) state.gpr[0].low = state.gpr[11].low;
    return Status::ok;
}
} // namespace reps2::generated
