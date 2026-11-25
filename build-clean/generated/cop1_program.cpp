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
    state.pc = 0x0000E000U;
    // guest PC 0x0000E000
    state.fpr[6] = static_cast<std::uint32_t>(state.gpr[1].low);
    state.pc = 0x0000E004U;
    // guest PC 0x0000E004
    if (3 != 0) state.gpr[3].low = sign_extend_word(state.fpr[6]);
    state.pc = 0x0000E008U;
    // guest PC 0x0000E008
    if (0 != 0) state.gpr[0].low = sign_extend_word(state.fpr[6]);
    state.pc = 0x0000E00CU;
    // guest PC 0x0000E00C
    const std::uint32_t effective_address_0003 = static_cast<std::uint32_t>(state.gpr[8].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0040U));
    const auto memory_status_0003 = store_u32(state.memory, effective_address_0003, state.fpr[6]);
    if (memory_status_0003 == Status::address_error) return reps2::runtime::raise_ee_address_error(state, reps2::runtime::AddressErrorCode::Store, effective_address_0003, false);
    if (memory_status_0003 != Status::ok) return memory_status_0003;
    state.pc = 0x0000E010U;
    // guest PC 0x0000E010
    const std::uint32_t effective_address_0004 = static_cast<std::uint32_t>(state.gpr[8].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0040U));
    std::uint32_t memory_value_0004{};
    const auto memory_status_0004 = load_u32(state.memory, effective_address_0004, memory_value_0004);
    if (memory_status_0004 == Status::address_error) return reps2::runtime::raise_ee_address_error(state, reps2::runtime::AddressErrorCode::Load, effective_address_0004, false);
    if (memory_status_0004 != Status::ok) return memory_status_0004;
    state.fpr[0] = memory_value_0004;
    state.pc = 0x0000E014U;
    // guest PC 0x0000E014
    if (4 != 0) state.gpr[4].low = sign_extend_word(state.fpr[0]);
    state.pc = 0x0000E018U;
    // guest PC 0x0000E018
    state.fpr[0] = static_cast<std::uint32_t>(state.gpr[2].low);
    state.pc = 0x0000E01CU;
    // guest PC 0x0000E01C
    if (10 != 0) state.gpr[10].low = sign_extend_word(state.fpr[0]);
    state.pc = 0x0000E020U;
    // guest PC 0x0000E020
    state.fpr[0] = static_cast<std::uint32_t>(state.gpr[0].low);
    state.pc = 0x0000E024U;
    // guest PC 0x0000E024
    if (11 != 0) state.gpr[11].low = sign_extend_word(state.fpr[0]);
    state.pc = 0x0000E028U;
    // guest PC 0x0000E028
    const std::uint32_t effective_address_0010 = static_cast<std::uint32_t>(state.gpr[9].low) + static_cast<std::uint32_t>(sign_extend_immediate(0xFFFCU));
    std::uint32_t memory_value_0010{};
    const auto memory_status_0010 = load_u32(state.memory, effective_address_0010, memory_value_0010);
    if (memory_status_0010 == Status::address_error) return reps2::runtime::raise_ee_address_error(state, reps2::runtime::AddressErrorCode::Load, effective_address_0010, false);
    if (memory_status_0010 != Status::ok) return memory_status_0010;
    state.fpr[7] = memory_value_0010;
    state.pc = 0x0000E02CU;
    // guest PC 0x0000E02C
    if (7 != 0) state.gpr[7].low = sign_extend_word(state.fpr[7]);
    state.pc = 0x0000E030U;
    // guest PC 0x0000E030
    if (5 != 0) state.gpr[5].low = sign_extend_word(reps2::runtime::kFpuImplementationRegister);
    state.pc = 0x0000E034U;
    // guest PC 0x0000E034
    store_fpu_control(state.fcsr, static_cast<std::uint32_t>(state.gpr[2].low));
    state.pc = 0x0000E038U;
    // guest PC 0x0000E038
    if (6 != 0) state.gpr[6].low = sign_extend_word(state.fcsr);
    return Status::ok;
}
} // namespace reps2::generated
