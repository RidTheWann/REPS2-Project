// SPDX-License-Identifier: GPL-3.0-or-later
//
// EE syscall HLE smoke test.
//
// The synthetic CFG fixture exposes three HLE entries:
//   0xE000  syscall; addiu v0, zero, 1; jr ra
//   0xE100  syscall; addiu v0, zero, 1        (service number has no handler)
//   0xE200  beq t0, zero, 0xE214; syscall (delay slot); ... ; either path jr ra
//
// The EE kernel service number is passed in $v1 (GPR[3]), so these tests set it
// directly instead of relying on the instruction's 20-bit code field.
#include "reps2/runtime/ee_hle.hpp"

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
constexpr std::uint32_t kFlushCache = reps2::runtime::kEeSyscallFlushCache;

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
