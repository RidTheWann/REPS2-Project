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
    state.pc = 0x0000F000U;
    // guest PC 0x0000F000
    if (2 != 0) state.gpr[2].low = sign_extend_word(static_cast<std::uint32_t>(0x1000U) << 16U);
    state.pc = 0x0000F004U;
    // guest PC 0x0000F004
    if (2 != 0) state.gpr[2].low = state.gpr[2].low | 0xF000ULL;
    state.pc = 0x0000F008U;
    // guest PC 0x0000F008
    const std::uint32_t effective_address_0002 = static_cast<std::uint32_t>(state.gpr[2].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0000U));
    std::uint32_t memory_value_0002{};
    const auto memory_status_0002 = load_u32(state.memory, effective_address_0002, memory_value_0002);
    if (memory_status_0002 == Status::address_error) return reps2::runtime::raise_ee_address_error(state, reps2::runtime::AddressErrorCode::Load, effective_address_0002, false);
    if (memory_status_0002 != Status::ok) return memory_status_0002;
    state.gpr[3].low = sign_extend_word(memory_value_0002);
    state.pc = 0x0000F00CU;
    // guest PC 0x0000F00C
    const std::uint32_t effective_address_0003 = static_cast<std::uint32_t>(state.gpr[2].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0000U));
    const auto memory_status_0003 = store_u32(state.memory, effective_address_0003, static_cast<std::uint32_t>(state.gpr[3].low));
    if (memory_status_0003 == Status::address_error) return reps2::runtime::raise_ee_address_error(state, reps2::runtime::AddressErrorCode::Store, effective_address_0003, false);
    if (memory_status_0003 != Status::ok) return memory_status_0003;
    state.pc = 0x0000F010U;
    // guest PC 0x0000F010
    if (2 != 0) state.gpr[2].low = state.gpr[2].low | 0x0010ULL;
    state.pc = 0x0000F014U;
    // guest PC 0x0000F014
    const std::uint32_t effective_address_0005 = static_cast<std::uint32_t>(state.gpr[2].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0000U));
    std::uint32_t memory_value_0005{};
    const auto memory_status_0005 = load_u32(state.memory, effective_address_0005, memory_value_0005);
    if (memory_status_0005 == Status::address_error) return reps2::runtime::raise_ee_address_error(state, reps2::runtime::AddressErrorCode::Load, effective_address_0005, false);
    if (memory_status_0005 != Status::ok) return memory_status_0005;
    state.gpr[3].low = sign_extend_word(memory_value_0005);
    state.pc = 0x0000F018U;
    // guest PC 0x0000F018
    const std::uint32_t effective_address_0006 = static_cast<std::uint32_t>(state.gpr[2].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0000U));
    const auto memory_status_0006 = store_u32(state.memory, effective_address_0006, static_cast<std::uint32_t>(state.gpr[3].low));
    if (memory_status_0006 == Status::address_error) return reps2::runtime::raise_ee_address_error(state, reps2::runtime::AddressErrorCode::Store, effective_address_0006, false);
    if (memory_status_0006 != Status::ok) return memory_status_0006;
    return Status::ok;
}
} // namespace reps2::generated
