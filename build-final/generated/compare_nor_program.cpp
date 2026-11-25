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
    state.pc = 0x0000A000U;
    // guest PC 0x0000A000
    if (12 != 0) state.gpr[12].low = ~(state.gpr[4].low | state.gpr[5].low);
    state.pc = 0x0000A004U;
    // guest PC 0x0000A004
    if (6 != 0) state.gpr[6].low = (std::bit_cast<std::int64_t>(state.gpr[1].low) < std::bit_cast<std::int64_t>(state.gpr[2].low)) ? 1ULL : 0ULL;
    state.pc = 0x0000A008U;
    // guest PC 0x0000A008
    if (7 != 0) state.gpr[7].low = (state.gpr[1].low < state.gpr[2].low) ? 1ULL : 0ULL;
    state.pc = 0x0000A00CU;
    // guest PC 0x0000A00C
    if (8 != 0) state.gpr[8].low = (std::bit_cast<std::int64_t>(state.gpr[1].low) < static_cast<std::int64_t>(1LL)) ? 1ULL : 0ULL;
    state.pc = 0x0000A010U;
    // guest PC 0x0000A010
    if (9 != 0) state.gpr[9].low = (std::bit_cast<std::int64_t>(state.gpr[2].low) < static_cast<std::int64_t>(-1LL)) ? 1ULL : 0ULL;
    state.pc = 0x0000A014U;
    // guest PC 0x0000A014
    if (10 != 0) state.gpr[10].low = (state.gpr[0].low < static_cast<std::uint64_t>(-1LL)) ? 1ULL : 0ULL;
    state.pc = 0x0000A018U;
    // guest PC 0x0000A018
    if (11 != 0) state.gpr[11].low = (state.gpr[3].low < static_cast<std::uint64_t>(-1LL)) ? 1ULL : 0ULL;
    state.pc = 0x0000A01CU;
    // guest PC 0x0000A01C
    if (13 != 0) state.gpr[13].low = (std::bit_cast<std::int64_t>(state.gpr[2].low) < std::bit_cast<std::int64_t>(state.gpr[1].low)) ? 1ULL : 0ULL;
    state.pc = 0x0000A020U;
    // guest PC 0x0000A020
    if (14 != 0) state.gpr[14].low = (state.gpr[2].low < state.gpr[1].low) ? 1ULL : 0ULL;
    state.pc = 0x0000A024U;
    // guest PC 0x0000A024
    if (0 != 0) state.gpr[0].low = (std::bit_cast<std::int64_t>(state.gpr[1].low) < std::bit_cast<std::int64_t>(state.gpr[2].low)) ? 1ULL : 0ULL;
    state.pc = 0x0000A028U;
    // guest PC 0x0000A028
    if (16 != 0) state.gpr[16].low = (state.gpr[1].low < static_cast<std::uint64_t>(-1LL)) ? 1ULL : 0ULL;
    state.pc = 0x0000A02CU;
    // guest PC 0x0000A02C
    if (17 != 0) state.gpr[17].low = (state.gpr[3].low < static_cast<std::uint64_t>(0LL)) ? 1ULL : 0ULL;
    state.pc = 0x0000A030U;
    // guest PC 0x0000A030
    if (18 != 0) state.gpr[18].low = (std::bit_cast<std::int64_t>(state.gpr[1].low) < static_cast<std::int64_t>(-1LL)) ? 1ULL : 0ULL;
    state.pc = 0x0000A034U;
    // guest PC 0x0000A034
    if (19 != 0) state.gpr[19].low = (std::bit_cast<std::int64_t>(state.gpr[2].low) < static_cast<std::int64_t>(-32768LL)) ? 1ULL : 0ULL;
    state.pc = 0x0000A038U;
    // guest PC 0x0000A038
    if (20 != 0) state.gpr[20].low = (state.gpr[3].low < static_cast<std::uint64_t>(-32768LL)) ? 1ULL : 0ULL;
    return Status::ok;
}
} // namespace reps2::generated
