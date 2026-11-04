// SPDX-License-Identifier: GPL-3.0-or-later
//
// EE syscall HLE smoke test.
//
// The synthetic CFG fixture exposes four HLE entries:
//   0xE000  syscall; addiu v0, zero, 1; jr ra
//   0xE100  syscall; addiu v0, zero, 1        (service number has no handler)
//   0xE200  beq t0, zero, 0xE214; syscall (delay slot); ... ; either path jr ra
//   0xE400  syscall; jr ra                    ($v0 keeps the service result)
//
// The EE kernel service number is passed in $v1 (GPR[3]), so these tests set it
// directly instead of relying on the instruction's 20-bit code field.
#include "reps2/runtime/ee_hle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string_view>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute_cfg(
    reps2::runtime::EeState& state,
    std::uint64_t instruction_budget
) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Status;

constexpr std::uint32_t kUnreachableReturnPc = 0xDEAD0000U;
constexpr std::uint32_t kSetupThread = reps2::runtime::kEeSyscallSetupThread;
constexpr std::uint32_t kSetupHeap = reps2::runtime::kEeSyscallSetupHeap;
constexpr std::uint32_t kCreateSema = reps2::runtime::kEeSyscallCreateSema;
constexpr std::uint32_t kSetSyscall = reps2::runtime::kEeSyscallSetSyscall;
constexpr std::uint32_t kFlushCache = reps2::runtime::kEeSyscallFlushCache;

// The CreateSema tests need guest-readable parameters, but a full 32 MiB image
// per case is unnecessary: main RAM is a span, so a small aligned window proves
// the same address-resolution and bounds behavior.
using RamWindow = std::array<std::uint8_t, 0x1000>;
constexpr std::uint32_t kSemaParameters = 0x0000'0100U;

void store_word(RamWindow& ram, std::uint32_t address, std::uint32_t value) {
    const auto offset = static_cast<std::size_t>(address);
    ram[offset] = static_cast<std::uint8_t>(value);
    ram[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    ram[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    ram[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t load_word(const RamWindow& ram, std::uint32_t address) {
    const auto offset = static_cast<std::size_t>(address);
    return static_cast<std::uint32_t>(ram[offset])
        | (static_cast<std::uint32_t>(ram[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(ram[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(ram[offset + 3U]) << 24U);
}

// Writes the six documented parameter words: count, max_count, init_count,
// wait_threads, attr, option.
void store_sema_parameters(
    RamWindow& ram,
    std::uint32_t address,
    std::int32_t count,
    std::int32_t max_count,
    std::int32_t init_count,
    std::int32_t wait_threads,
    std::uint32_t attr,
    std::uint32_t option
) {
    const auto words = {count, max_count, init_count, wait_threads};
    std::uint32_t index = 0;
    for (const std::int32_t word : words) {
        store_word(ram, address + index * 4U, static_cast<std::uint32_t>(word));
        ++index;
    }
    store_word(ram, address + 16U, attr);
    store_word(ram, address + 20U, option);
}

// A serviced call resumes at the instruction after the trap and the following
// register write proves the guest continued past the SYSCALL.
int test_disabled_layer_keeps_the_architectural_exception() {
    EeState state{};
    state.pc = 0xE000U;
    state.gpr[3].low = kSetupThread;
    const Status status = reps2::generated::execute_cfg(state, 8U);
    if (status != Status::exception_delivered) {
        return 10;
    }
    if (state.pc != reps2::runtime::kEeCommonExceptionVector) {
        return 11;
    }
    if ((state.cop0[reps2::runtime::kCop0Cause] & reps2::runtime::kCop0CauseExcCodeMask)
        != (8U << 2U)) {
        return 12;
    }
    if (state.cop0[reps2::runtime::kCop0Epc] != 0xE000U) {
        return 13;
    }
    if ((state.cop0[reps2::runtime::kCop0Cause]
         & reps2::runtime::kCop0CauseBranchDelayMask)
        != 0U) {
        return 14;
    }
    if ((state.cop0[reps2::runtime::kCop0Status] & reps2::runtime::kCop0StatusExlMask) == 0U) {
        return 15;
    }
    // The host service layer was never consulted.
    if (state.hle.syscall_enabled || state.hle.syscall_count != 0U
        || state.hle.syscall_handled_count != 0U) {
        return 16;
    }
    return 0;
}

int test_setup_thread_with_the_stack_sentinel() {
    EeState state{};
    state.pc = 0xE000U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupThread;
    state.gpr[4].low = 0x80100000U; // $a0: gp
    state.gpr[5].low = 0xFFFFFFFFU; // $a1: no caller stack
    state.gpr[6].low = 0x4000U;     // $a2: stack size
    state.gpr[7].low = 0x00200000U; // $a3: argv block
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 20;
    }
    // The trailing `addiu v0, zero, 1` ran, so the call resumed after the trap.
    if (state.gpr[2].low != 1U) {
        return 21;
    }
    if (state.hle.syscall_count != 1U || state.hle.syscall_handled_count != 1U) {
        return 22;
    }
    if (state.hle.last_syscall_code != kSetupThread
        || state.hle.last_syscall_number != kSetupThread) {
        return 23;
    }
    if (!state.hle.setup_thread_called) {
        return 24;
    }
    // 32 MiB main RAM minus the 4 KiB top guard, then the 0x2A0 reserved bytes.
    const std::uint32_t expected_top =
        reps2::runtime::kEeMainRamSize - reps2::runtime::kEeSetupThreadTopGuard;
    const std::uint32_t expected_sp =
        expected_top - reps2::runtime::kEeSetupThreadStackReserve;
    if (state.hle.setup_thread_stack_top != expected_top
        || state.hle.setup_thread_stack_pointer != expected_sp) {
        return 25;
    }
    if (state.hle.setup_thread_stack_size != 0x4000U
        || state.hle.setup_thread_gp != 0x80100000U
        || state.hle.setup_thread_args != 0x00200000U) {
        return 26;
    }
    // No COP0 exception context was created by the serviced call.
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 27;
    }
    return 0;
}

int test_setup_thread_with_an_explicit_stack() {
    EeState state{};
    state.pc = 0xE000U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupThread;
    state.gpr[5].low = 0x00100000U; // $a1: caller stack base
    state.gpr[6].low = 0x00008000U; // $a2: stack size
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 30;
    }
    if (state.hle.setup_thread_stack_top != 0x00108000U) {
        return 31;
    }
    if (state.hle.setup_thread_stack_pointer
        != 0x00108000U - reps2::runtime::kEeSetupThreadStackReserve) {
        return 32;
    }
    if (state.hle.setup_thread_stack_size != 0x00008000U) {
        return 33;
    }
    return 0;
}

int test_negated_service_number_dispatches_on_its_magnitude() {
    EeState state{};
    state.pc = 0xE000U;
    state.hle.syscall_enabled = true;
    // The ps2sdk "i" wrappers pass the negated service number.
    state.gpr[3].low = static_cast<std::uint64_t>(0U - kSetupThread);
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 40;
    }
    if (state.hle.last_syscall_code != 0U - kSetupThread
        || state.hle.last_syscall_number != kSetupThread) {
        return 41;
    }
    if (!state.hle.setup_thread_called || state.hle.syscall_handled_count != 1U) {
        return 42;
    }
    return 0;
}

int test_unknown_service_stops_without_faking_success() {
    EeState state{};
    state.pc = 0xE100U;
    state.hle.syscall_enabled = true;
    state.gpr[2].low = 0x1234U;  // sentinel result the guest must keep
    state.gpr[3].low = 0x00000099U; // no host handler

    const Status status = reps2::generated::execute_cfg(state, 8U);
    if (status != Status::unhandled_syscall) {
        return 50;
    }
    // The stop is a host boundary at the SYSCALL, not a guest exception.
    if (state.pc != 0xE100U) {
        return 51;
    }
    if (state.gpr[2].low != 0x1234U) {
        return 52;
    }
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U
        || (state.cop0[reps2::runtime::kCop0Status] & reps2::runtime::kCop0StatusExlMask) != 0U) {
        return 53;
    }
    if (state.hle.syscall_count != 1U || state.hle.syscall_handled_count != 0U) {
        return 54;
    }
    if (state.hle.last_syscall_number != 0x99U || state.hle.setup_thread_called) {
        return 55;
    }
    if (reps2::runtime::ee_syscall_service_name(0x99U) != nullptr) {
        return 56;
    }
    return 0;
}

int test_flush_cache_is_an_honest_no_op() {
    EeState state{};
    state.pc = 0xE000U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kFlushCache;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 60;
    }
    if (state.hle.syscall_handled_count != 1U || state.hle.last_syscall_number != kFlushCache) {
        return 61;
    }
    // The resume marker still ran and no thread state was invented.
    if (state.gpr[2].low != 1U || state.hle.setup_thread_called) {
        return 62;
    }
    return 0;
}

int test_delay_slot_syscall_resumes_at_the_taken_target() {
    EeState state{};
    state.pc = 0xE200U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupThread;
    state.gpr[8].low = 0U; // t0 == 0 takes the branch
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 70;
    }
    // 9 comes from the taken target; 7 would mean the fallthrough was resumed.
    if (state.gpr[2].low != 9U) {
        return 71;
    }
    if (state.hle.syscall_handled_count != 1U) {
        return 72;
    }
    if (state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 73;
    }
    return 0;
}

int test_delay_slot_syscall_resumes_at_the_fallthrough() {
    EeState state{};
    state.pc = 0xE200U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupThread;
    state.gpr[8].low = 1U; // t0 != 0: not taken, the slot still executes
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 80;
    }
    if (state.gpr[2].low != 7U) {
        return 81;
    }
    if (state.hle.syscall_handled_count != 1U) {
        return 82;
    }
    return 0;
}

int test_setup_heap_with_explicit_range() {
    EeState state{};
    state.pc = 0xE300U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupHeap;
    state.gpr[4].low = 0x00100000U; // $a0: heap start
    state.gpr[5].low = 0x00008000U; // $a1: heap size
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 95;
    }
    if (!state.hle.setup_heap_called
        || state.hle.setup_heap_start != 0x00100000U
        || state.hle.setup_heap_size != 0x00008000U
        || state.hle.setup_heap_end != 0x00108000U) {
        return 96;
    }
    // The fixture's first post-trap instruction overwrites $v0 with its resume
    // marker; the host observation preserves the service result independently.
    if (state.gpr[2].low != 1U) {
        return 97;
    }
    return 0;
}

int test_setup_heap_size_sentinel_uses_setup_thread_stack() {
    EeState state{};
    state.pc = 0xE300U;
    state.hle.syscall_enabled = true;
    state.hle.setup_thread_called = true;
    state.hle.setup_thread_stack_pointer = 0x01FF0000U;
    state.gpr[3].low = kSetupHeap;
    state.gpr[4].low = 0x008A0400U; // retail heap start
    state.gpr[5].low = 0xFFFFFFFFU; // ps2sdk/retail observed size sentinel
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 98;
    }
    if (state.hle.setup_heap_end != 0x01FF0000U || state.gpr[2].low != 1U) {
        return 99;
    }
    return 0;
}

int test_create_sema_returns_a_unique_id() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE400U;
    state.hle.syscall_enabled = true;
    // count/max_count/init_count/wait_threads: only the middle two are inputs.
    store_sema_parameters(ram, kSemaParameters, 0, 4, 2, 0, 0U, 0U);
    state.gpr[3].low = kCreateSema;
    state.gpr[4].low = kSemaParameters;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 110;
    }
    // The fixture does not touch $v0, so this is the service's own result.
    if (state.gpr[2].low != 0U) {
        return 111;
    }
    if (state.hle.last_sema_id != 0 || state.hle.semaphore_count != 1U) {
        return 112;
    }
    if (state.hle.last_sema_init_count != 2 || state.hle.last_sema_max_count != 4) {
        return 113;
    }
    const auto& sema = state.hle.semaphores[0];
    if (!sema.in_use || sema.count != 2 || sema.max_count != 4
        || sema.attr != 0U || sema.option != 0U) {
        return 114;
    }
    if (sema.create_pc != 0xE400U || state.hle.last_sema_rejection
        != reps2::runtime::EeSemaRejection::none) {
        return 115;
    }
    // Only $v0 changed: the service number register and the argument registers
    // are not invented side effects, and no COP0 context was created.
    if (state.gpr[3].low != kCreateSema || state.gpr[4].low != kSemaParameters
        || state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 116;
    }
    // The guest parameter block is an input, not a status mirror.
    if (load_word(ram, kSemaParameters) != 0U
        || load_word(ram, kSemaParameters + 4U) != 4U
        || load_word(ram, kSemaParameters + 8U) != 2U) {
        return 117;
    }
    // A second call must not reuse the first id.
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.gpr[2].low != 1U || state.hle.semaphore_count != 2U) {
        return 118;
    }
    return 0;
}

int test_create_sema_consumes_only_the_documented_result() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE400U;
    state.hle.syscall_enabled = true;
    // The ps2sdk "i" wrappers pass the negated service number; CreateSema has no
    // such wrapper, but dispatch normalizes by magnitude regardless.
    store_sema_parameters(ram, kSemaParameters, 0, 1, 1, 0, 0U, 0U);
    state.gpr[3].low = static_cast<std::uint64_t>(0U - kCreateSema);
    state.gpr[4].low = kSemaParameters;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 120;
    }
    if (state.hle.last_syscall_number != kCreateSema || state.hle.last_sema_id != 0) {
        return 121;
    }
    if (state.gpr[2].low != 0U) {
        return 122;
    }
    return 0;
}

int test_create_sema_rejects_inconsistent_counts() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;

    // init_count above max_count can never be represented by the object.
    store_sema_parameters(ram, kSemaParameters, 0, 2, 5, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 130;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.last_sema_rejection != reps2::runtime::EeSemaRejection::init_exceeds_max) {
        return 131;
    }
    if (state.hle.semaphore_count != 0U || state.hle.last_sema_id != -1) {
        return 132;
    }

    // A negative count is not a count.
    store_sema_parameters(ram, kSemaParameters, 0, -1, 0, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.hle.last_sema_rejection != reps2::runtime::EeSemaRejection::negative_counts) {
        return 133;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL || state.hle.semaphore_count != 0U) {
        return 134;
    }

    // An equal pair is accepted: the bound is inclusive.
    store_sema_parameters(ram, kSemaParameters, 0, 3, 3, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok || state.gpr[2].low != 0U
        || state.hle.semaphores[0].count != 3 || state.hle.semaphores[0].max_count != 3) {
        return 135;
    }
    return 0;
}

int test_create_sema_rejects_unreadable_parameters() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    store_sema_parameters(ram, kSemaParameters, 0, 1, 1, 0, 0U, 0U);

    // A misaligned pointer is refused rather than read partially.
    state.gpr[4].low = kSemaParameters + 2U;
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.hle.last_sema_rejection
            != reps2::runtime::EeSemaRejection::unreadable_parameters) {
        return 140;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL || state.hle.semaphore_count != 0U) {
        return 141;
    }

    // A block that runs past the end of main RAM is refused, not truncated.
    state.gpr[4].low = static_cast<std::uint32_t>(ram.size()) - 8U;
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.hle.last_sema_rejection
            != reps2::runtime::EeSemaRejection::unreadable_parameters) {
        return 142;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL || state.hle.semaphore_count != 0U) {
        return 143;
    }

    // An address outside any RAM alias is refused too.
    state.gpr[4].low = 0xFFF0'0000U;
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.hle.semaphore_count != 0U || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) {
        return 144;
    }
    return 0;
}

int test_create_sema_table_is_bounded() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    store_sema_parameters(ram, kSemaParameters, 0, 1, 0, 0, 0U, 0U);
    state.gpr[4].low = kSemaParameters;

    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(reps2::runtime::kEeMaxSemaphores);
         ++index) {
        if (reps2::runtime::hle_create_sema(state) != Status::ok) {
            return 150;
        }
        if (state.gpr[2].low != static_cast<std::uint64_t>(index)) {
            return 151;
        }
    }
    // The documented kernel limit is a hard bound, and the refusal is the
    // documented -1 rather than a wrapped-around id.
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 152;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.last_sema_rejection != reps2::runtime::EeSemaRejection::table_full) {
        return 153;
    }
    if (state.hle.semaphore_count != reps2::runtime::kEeMaxSemaphores
        || state.hle.last_sema_id != -1) {
        return 154;
    }
    return 0;
}

int test_set_syscall_registers_guest_handler() {
    EeState state{};
    state.pc = 0xE500U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetSyscall;
    state.gpr[4].low = 0x83U;
    state.gpr[5].low = 0x0025EF38U;
    state.gpr[31].low = kUnreachableReturnPc;
    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) return 160;
    const auto& registration = state.hle.syscall_registrations[0x83U];
    if (!registration.in_use || registration.handler_address != 0x0025EF38U) return 161;
    if (state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::none) return 162;
    if (state.gpr[2].low != 0U || state.gpr[3].low != kSetSyscall) return 163;
    return 0;
}

int test_set_syscall_rejects_invalid_registration() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.gpr[4].low = 256U;
    state.gpr[5].low = 0x1000U;
    if (reps2::runtime::hle_set_syscall(state) != Status::ok
        || state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::invalid_slot) return 170;
    state.gpr[4].low = 1U;
    state.gpr[5].low = 0x1002U;
    if (reps2::runtime::hle_set_syscall(state) != Status::ok
        || state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::invalid_handler) return 171;
    state.gpr[5].low = 0xFFF00000U;
    if (reps2::runtime::hle_set_syscall(state) != Status::ok
        || state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::invalid_handler) return 172;
    return 0;
}

int test_delay_slot_syscall_without_the_layer_raises_the_exception() {
    EeState state{};
    state.pc = 0xE200U;
    state.gpr[3].low = kSetupThread;
    state.gpr[8].low = 0U;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::exception_delivered) {
        return 90;
    }
    if (state.pc != reps2::runtime::kEeCommonExceptionVector) {
        return 91;
    }
    // EPC names the branch and Cause.BD marks the delay slot.
    if (state.cop0[reps2::runtime::kCop0Epc] != 0xE200U) {
        return 92;
    }
    if ((state.cop0[reps2::runtime::kCop0Cause]
         & reps2::runtime::kCop0CauseBranchDelayMask)
        == 0U) {
        return 93;
    }
    if ((state.cop0[reps2::runtime::kCop0Cause] & reps2::runtime::kCop0CauseExcCodeMask)
        != (8U << 2U)) {
        return 94;
    }
    return 0;
}

int test_service_table_reports_names() {
    if (reps2::runtime::ee_syscall_service_name(kSetupThread) == nullptr) {
        return 100;
    }
    if (reps2::runtime::find_ee_syscall_service(kFlushCache) == nullptr) {
        return 101;
    }
    if (reps2::runtime::find_ee_syscall_service(kSetupHeap) == nullptr) {
        return 102;
    }
    if (reps2::runtime::find_ee_syscall_service(kCreateSema) == nullptr) {
        return 104;
    }
    const char* const sema_name = reps2::runtime::ee_syscall_service_name(kCreateSema);
    if (sema_name == nullptr || std::string_view(sema_name) != "CreateSema") {
        return 105;
    }
    const char* const name = reps2::runtime::status_name(Status::unhandled_syscall);
    if (name == nullptr || std::string_view(name) != "unhandled_syscall") {
        return 103;
    }
    return 0;
}

} // namespace

int main() {
    const struct {
        const char* what;
        int (*run)();
    } cases[] = {
        {"disabled layer keeps the architectural exception",
         &test_disabled_layer_keeps_the_architectural_exception},
        {"setup thread with the stack sentinel", &test_setup_thread_with_the_stack_sentinel},
        {"setup thread with an explicit stack", &test_setup_thread_with_an_explicit_stack},
        {"negated service number", &test_negated_service_number_dispatches_on_its_magnitude},
        {"unknown service", &test_unknown_service_stops_without_faking_success},
        {"flush cache", &test_flush_cache_is_an_honest_no_op},
        {"delay slot taken target", &test_delay_slot_syscall_resumes_at_the_taken_target},
        {"delay slot fallthrough", &test_delay_slot_syscall_resumes_at_the_fallthrough},
        {"delay slot without the layer",
         &test_delay_slot_syscall_without_the_layer_raises_the_exception},
        {"setup heap explicit range", &test_setup_heap_with_explicit_range},
        {"setup heap size sentinel", &test_setup_heap_size_sentinel_uses_setup_thread_stack},
        {"create sema unique id", &test_create_sema_returns_a_unique_id},
        {"create sema negated service number",
         &test_create_sema_consumes_only_the_documented_result},
        {"create sema inconsistent counts", &test_create_sema_rejects_inconsistent_counts},
        {"create sema unreadable parameters", &test_create_sema_rejects_unreadable_parameters},
        {"create sema table bound", &test_create_sema_table_is_bounded},
        {"set syscall registers handler", &test_set_syscall_registers_guest_handler},
        {"set syscall rejects invalid registration", &test_set_syscall_rejects_invalid_registration},
        {"service table names", &test_service_table_reports_names},
    };

    for (const auto& test : cases) {
        const int result = test.run();
        if (result != 0) {
            std::fprintf(stderr, "hle syscall smoke failed: %s (assertion %d)\n", test.what, result);
            return result;
        }
    }
    std::printf("EE syscall HLE smoke passed: %zu cases\n", std::size(cases));
    return 0;
}
