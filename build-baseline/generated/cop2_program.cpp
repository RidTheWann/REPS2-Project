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
    const std::uint32_t effective_address_0000 = static_cast<std::uint32_t>(state.gpr[4].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0010U));
    const auto vu0_store_value_0000 = reps2::runtime::vu0_read_vf(state.vu0, 0);
    const auto memory_status_0000 = store_u128(state.memory, effective_address_0000, vu0_store_value_0000);
    if (memory_status_0000 != reps2::runtime::Status::ok) return memory_status_0000;
    state.pc = 0x0000E004U;
    // guest PC 0x0000E004
    const std::uint32_t effective_address_0001 = static_cast<std::uint32_t>(state.gpr[4].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0020U));
    reps2::runtime::Gpr128 vu0_load_value_0001{};
    const auto memory_status_0001 = load_u128(state.memory, effective_address_0001, vu0_load_value_0001);
    if (memory_status_0001 != reps2::runtime::Status::ok) return memory_status_0001;
    reps2::runtime::vu0_write_vf(state.vu0, 5, vu0_load_value_0001);
    state.pc = 0x0000E008U;
    // guest PC 0x0000E008
    const std::uint32_t effective_address_0002 = static_cast<std::uint32_t>(state.gpr[4].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0030U));
    const auto vu0_store_value_0002 = reps2::runtime::vu0_read_vf(state.vu0, 5);
    const auto memory_status_0002 = store_u128(state.memory, effective_address_0002, vu0_store_value_0002);
    if (memory_status_0002 != reps2::runtime::Status::ok) return memory_status_0002;
    state.pc = 0x0000E00CU;
    // guest PC 0x0000E00C
    const std::uint32_t effective_address_0003 = static_cast<std::uint32_t>(state.gpr[4].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0040U));
    reps2::runtime::Gpr128 vu0_load_value_0003{};
    const auto memory_status_0003 = load_u128(state.memory, effective_address_0003, vu0_load_value_0003);
    if (memory_status_0003 != reps2::runtime::Status::ok) return memory_status_0003;
    reps2::runtime::vu0_write_vf(state.vu0, 0, vu0_load_value_0003);
    state.pc = 0x0000E010U;
    // guest PC 0x0000E010
    const std::uint32_t effective_address_0004 = static_cast<std::uint32_t>(state.gpr[4].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0050U));
    const auto vu0_store_value_0004 = reps2::runtime::vu0_read_vf(state.vu0, 0);
    const auto memory_status_0004 = store_u128(state.memory, effective_address_0004, vu0_store_value_0004);
    if (memory_status_0004 != reps2::runtime::Status::ok) return memory_status_0004;
    state.pc = 0x0000E014U;
    // guest PC 0x0000E014
    vu0_write_control(state.vu0, 28, static_cast<std::uint32_t>(state.gpr[3].low));
    state.pc = 0x0000E018U;
    // guest PC 0x0000E018
    if (8 != 0) state.gpr[8].low = sign_extend_word(vu0_read_control(state.vu0, 28));
    state.pc = 0x0000E01CU;
    // guest PC 0x0000E01C
    if (9 != 0) state.gpr[9].low = sign_extend_word(vu0_read_control(state.vu0, 29));
    state.pc = 0x0000E020U;
    // guest PC 0x0000E020
    // nop
    state.pc = 0x0000E024U;
    // guest PC 0x0000E024
    reps2::runtime::vu0_write_vf(state.vu0, 12, state.gpr[2]);
    state.pc = 0x0000E028U;
    // guest PC 0x0000E028
    const std::uint32_t effective_address_0010 = static_cast<std::uint32_t>(state.gpr[4].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0060U));
    const auto vu0_store_value_0010 = reps2::runtime::vu0_read_vf(state.vu0, 12);
    const auto memory_status_0010 = store_u128(state.memory, effective_address_0010, vu0_store_value_0010);
    if (memory_status_0010 != reps2::runtime::Status::ok) return memory_status_0010;
    state.pc = 0x0000E02CU;
    // guest PC 0x0000E02C
    if (3 != 0) state.gpr[3] = reps2::runtime::vu0_read_vf(state.vu0, 12);
    state.pc = 0x0000E030U;
    // guest PC 0x0000E030
    if (2 != 0) state.gpr[2] = reps2::runtime::vu0_read_vf(state.vu0, 0);
    state.pc = 0x0000E034U;
    // guest PC 0x0000E034
    if (0 != 0) state.gpr[0] = reps2::runtime::vu0_read_vf(state.vu0, 12);
    state.pc = 0x0000E038U;
    // guest PC 0x0000E038
    reps2::runtime::vu0_write_vf(state.vu0, 12, state.gpr[0]);
    state.pc = 0x0000E03CU;
    // guest PC 0x0000E03C
    const std::uint32_t effective_address_0015 = static_cast<std::uint32_t>(state.gpr[4].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0080U));
    const auto vu0_store_value_0015 = reps2::runtime::vu0_read_vf(state.vu0, 0);
    const auto memory_status_0015 = store_u128(state.memory, effective_address_0015, vu0_store_value_0015);
    if (memory_status_0015 != reps2::runtime::Status::ok) return memory_status_0015;
    state.pc = 0x0000E040U;
    // guest PC 0x0000E040
    reps2::runtime::vu0_write_vf(state.vu0, 4, state.gpr[2]);
    state.pc = 0x0000E044U;
    // guest PC 0x0000E044
    const std::uint32_t effective_address_0017 = static_cast<std::uint32_t>(state.gpr[4].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x0090U));
    const auto vu0_store_value_0017 = reps2::runtime::vu0_read_vf(state.vu0, 4);
    const auto memory_status_0017 = store_u128(state.memory, effective_address_0017, vu0_store_value_0017);
    if (memory_status_0017 != reps2::runtime::Status::ok) return memory_status_0017;
    state.pc = 0x0000E048U;
    // guest PC 0x0000E048
    reps2::runtime::vu0_write_vi(state.vu0, 7, static_cast<std::uint16_t>(reps2::runtime::vu0_read_vi(state.vu0, 0) + static_cast<std::int16_t>(15)));
    state.pc = 0x0000E04CU;
    // guest PC 0x0000E04C
    { const std::uint16_t vi_2_addr = reps2::runtime::vu0_read_vi(state.vu0, 2); reps2::runtime::vu0_write_vumem_qword(state.vu0, vi_2_addr, reps2::runtime::vu0_read_vf(state.vu0, 5)); reps2::runtime::vu0_write_vi(state.vu0, 2, static_cast<std::uint16_t>(vi_2_addr + 1U)); }
    state.pc = 0x0000E050U;
    // guest PC 0x0000E050
    { const std::uint16_t vi_3_addr = reps2::runtime::vu0_read_vi(state.vu0, 3); reps2::runtime::vu0_write_vf(state.vu0, 6, reps2::runtime::vu0_read_vumem_qword(state.vu0, vi_3_addr)); reps2::runtime::vu0_write_vi(state.vu0, 3, static_cast<std::uint16_t>(vi_3_addr + 1U)); }
    state.pc = 0x0000E054U;
    // guest PC 0x0000E054
    const std::uint32_t effective_address_0021 = static_cast<std::uint32_t>(state.gpr[4].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x00A0U));
    const auto vu0_store_value_0021 = reps2::runtime::vu0_read_vf(state.vu0, 6);
    const auto memory_status_0021 = store_u128(state.memory, effective_address_0021, vu0_store_value_0021);
    if (memory_status_0021 != reps2::runtime::Status::ok) return memory_status_0021;
    state.pc = 0x0000E058U;
    // guest PC 0x0000E058
    reps2::runtime::vu0_write_vi(state.vu0, 8, static_cast<std::uint16_t>(reps2::runtime::vu0_read_vi(state.vu0, 7) + reps2::runtime::vu0_read_vi(state.vu0, 0)));
    state.pc = 0x0000E05CU;
    // guest PC 0x0000E05C
    reps2::runtime::vu0_write_vi(state.vu0, 5, static_cast<std::uint16_t>(reps2::runtime::vu0_read_vi(state.vu0, 0) + reps2::runtime::vu0_read_vi(state.vu0, 0)));
    state.pc = 0x0000E060U;
    // guest PC 0x0000E060
    reps2::runtime::vu0_write_vi(state.vu0, 9, static_cast<std::uint16_t>(reps2::runtime::vu0_read_vi(state.vu0, 8) & reps2::runtime::vu0_read_vi(state.vu0, 7)));
    state.pc = 0x0000E064U;
    // guest PC 0x0000E064
    reps2::runtime::vu0_write_vi(state.vu0, 10, static_cast<std::uint16_t>(reps2::runtime::vu0_read_vi(state.vu0, 5) | reps2::runtime::vu0_read_vi(state.vu0, 8)));
    state.pc = 0x0000E068U;
    // guest PC 0x0000E068
    // nop
    return Status::ok;
}
} // namespace reps2::generated
