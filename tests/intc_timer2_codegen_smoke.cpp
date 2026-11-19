// SPDX-License-Identifier: GPL-3.0-or-later
//
// EE Timer 2 + INTC delivery, generated-code half (ADR 0172).
//
// The synthetic CFG fixture exposes two entries for this slice:
//   0x6600  AddIntcHandler(11, 0x6700, 0, 0); _EnableIntc(11); addiu; jr ra
//   0x6700  sw $a0, 0(0x00100000); jr ra
//
// This drives the AOT-compiled blocks, so the check under test is the emitted
// dispatcher hook and the interrupt frame, not a hand-written stand-in: the
// timer model raises INTC source 11, the dispatcher acknowledges I_STAT and
// enters the registered handler as guest code with the cause in $a0, the
// handler's store lands in guest memory, and the interrupted context resumes
// with its own registers intact.
#include "reps2/runtime/ee_hle.hpp"
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute_cfg(
    reps2::runtime::EeState& state,
    std::uint64_t instruction_budget
) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Status;

constexpr std::uint32_t kProgramPc = 0x0000'6600U;
constexpr std::uint32_t kResumePc = 0x0000'6620U;
constexpr std::uint32_t kHandlerPc = 0x0000'6700U;
constexpr std::uint32_t kScratchAddress = 0x0010'0000U;
constexpr std::uint32_t kUnreachableReturnPc = 0xDEAD'0000U;
constexpr std::uint32_t kCause = reps2::runtime::kEeIntcSourceTimer2;

struct Fixture {
    std::vector<std::uint8_t> ram;
    EeState state;

    Fixture() : ram(0x0020'0000U, std::uint8_t{0}) {
        state.memory.main_ram = ram;
        state.memory.intc.seed_idle_boot_state();
        state.hle.syscall_enabled = true;
        state.cop0[reps2::runtime::kCop0Status] |= reps2::runtime::kCop0StatusEieMask;
        state.cop0[reps2::runtime::kCop0Status] &=
            ~(reps2::runtime::kCop0StatusExlMask | reps2::runtime::kCop0StatusErlMask);
        state.pc = kProgramPc;
        state.gpr[31].low = kUnreachableReturnPc;
    }
};

int test_generated_intc_delivery() {
    using namespace reps2::runtime;
    Fixture fixture;
    EeState& state = fixture.state;

    // Step 1: run the guest setup. The two syscalls register the handler and
    // enable the source; the block sequence then exits at `jr ra`.
    const Status setup = reps2::generated::execute_cfg(state, 64U);
    if (setup != Status::external_control_flow) return 1;
    if (state.pc != kUnreachableReturnPc) return 2;
    if (state.hle.add_intc_handler_calls != 1U) return 3;
    if (state.hle.last_intc_handler_rejection != EeIntcHandlerRejection::none) return 4;
    if (state.hle.last_intc_handler_id != static_cast<std::int32_t>(kCause)) return 5;
    if (state.hle.intc_handlers[kCause].handler != kHandlerPc) return 6;
    if (state.hle.enable_intc_calls != 1U) return 7;
    if ((state.memory.intc.i_mask & (1U << kCause)) == 0U) return 8;

    // Step 2: the Timer 2 model raises the source. Nothing in the guest did
    // this, so the raise is device state, not a fabricated syscall result.
    if (store_u32(state.memory, kEeTimer2CountAddress, 0U) != Status::ok) return 9;
    if (store_u32(state.memory, kEeTimer2CompareAddress, 4U) != Status::ok) return 10;
    if (store_u32(state.memory,
                  kEeTimer2ModeAddress,
                  kEeTimer2CueBit | kEeTimer2CmpeBit) != Status::ok) return 11;
    advance_ee_timers(state, 8U);
    if (state.memory.timer2_matches != 1U) return 12;
    if (state.memory.timer2_irq_raises != 1U) return 13;
    if ((state.memory.intc.i_stat & (1U << kCause)) == 0U) return 14;
    if (!ee_intc_line_asserted(state)) return 15;

    // Step 3: the next dispatcher entry takes the interrupt. The handler runs at
    // 0x6700 with $a0 = cause and stores it; the frame then restores the
    // interrupted context and resumes at the saved PC.
    const std::uint32_t interrupted_v0 = 0x1234'5678U;
    state.gpr[2].low = interrupted_v0;
    state.pc = kResumePc;
    const Status run = reps2::generated::execute_cfg(state, 64U);
    if (run != Status::external_control_flow) return 16;
    if (state.hle.intc_interrupts_delivered != 1U) return 17;
    if (state.hle.intc_interrupts_completed != 1U) return 18;
    if (state.hle.last_intc_interrupt_cause != kCause) return 19;
    if (state.hle.last_intc_interrupt_handler != kHandlerPc) return 20;

    std::uint32_t scratch = 0U;
    if (load_u32(state.memory, kScratchAddress, scratch) != Status::ok) return 21;
    if (scratch != kCause) return 22;

    // The source was acknowledged before the handler ran, so the line dropped
    // and no second interrupt was delivered.
    if ((state.memory.intc.i_stat & (1U << kCause)) != 0U) return 23;
    if (state.memory.intc.line_asserted()) return 24;
    if (state.hle.syscall_handler_depth != 0U) return 25;

    // The interrupted guest's register file is its own, not the handler's.
    if (static_cast<std::uint32_t>(state.gpr[2].low) != interrupted_v0) return 26;
    if (state.pc != kUnreachableReturnPc) return 27;
    return 0;
}

// A pending source with no registered handler must not be entered, and must not
// be acknowledged behind the guest's back. This is the fail-closed half of the
// dispatcher hook.
int test_generated_no_handler_leaves_line_set() {
    using namespace reps2::runtime;
    Fixture fixture;
    EeState& state = fixture.state;

    if (!state.memory.intc.raise_source(kCause)) return 1;
    if (!state.memory.intc.write_mask(1U << kCause)) return 2;
    if (!state.memory.intc.line_asserted()) return 3;
    if (ee_intc_line_asserted(state)) return 4;

    // The dispatcher still runs the guest code it was asked for.
    state.pc = kResumePc;
    const Status run = reps2::generated::execute_cfg(state, 64U);
    if (run != Status::external_control_flow) return 5;
    if (state.hle.intc_interrupts_delivered != 0U) return 6;
    if ((state.memory.intc.i_stat & (1U << kCause)) == 0U) return 7;
    if (state.pc != kUnreachableReturnPc) return 8;

    // Enabling interrupts without a seeded controller asserts nothing either.
    Fixture unseeded;
    unseeded.state.memory.intc = {};
    unseeded.state.pc = kResumePc;
    if (ee_intc_line_asserted(unseeded.state)) return 9;
    if (reps2::generated::execute_cfg(unseeded.state, 64U) != Status::external_control_flow) {
        return 10;
    }
    if (unseeded.state.hle.intc_interrupts_delivered != 0U) return 11;
    return 0;
}

} // namespace

int main() {
    struct Test {
        int (*function)();
        const char* name;
        int base;
    };
    const Test tests[] = {
        {test_generated_intc_delivery, "generated INTC delivery", 10},
        {test_generated_no_handler_leaves_line_set, "generated INTC fail-closed", 60},
    };
    for (const Test& test : tests) {
        const int result = test.function();
        if (result != 0) {
            std::fprintf(stderr, "%s test failed at check %d\n", test.name, result);
            return test.base + result;
        }
    }
    return 0;
}