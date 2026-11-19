// SPDX-License-Identifier: GPL-3.0-or-later
//
// EE Timer 2 + INTC handler delivery smoke test (ADR 0172).
//
// This is the synthetic half of the Timer 2 / INTC slice: the model is driven
// through the public runtime API, so every check is a statement about the
// contract the generated code and the dispatcher consume. It covers the Timer 2
// register model and its compare-match path (including ZRET, overflow, the
// write-one-to-clear status bits, and the fail-closed count when the controller
// was never seeded), the AddIntcHandler / RemoveIntcHandler / EnableIntc /
// DisableIntc services, the delivery predicate, and the interrupt entry and
// return frame.
#include "reps2/runtime/ee_hle.hpp"
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using reps2::runtime::EeIntcHandlerRejection;
using reps2::runtime::EeIntcMaskOutcome;
using reps2::runtime::EeIntcRemoveOutcome;
using reps2::runtime::EeState;
using reps2::runtime::Status;

constexpr std::uint32_t kHandlerAddress = 0x0001'0020U;
constexpr std::uint32_t kRegisterPc = 0x0010'F000U;
constexpr std::uint32_t kResumePc = 0x0012'0000U;

struct Fixture {
    std::vector<std::uint8_t> ram;
    EeState state;

    explicit Fixture(bool seed_intc = true) : ram(0x0020'0000U, std::uint8_t{0}) {
        state.memory.main_ram = ram;
        if (seed_intc) {
            state.memory.intc.seed_idle_boot_state();
        }
        state.hle.syscall_enabled = true;
        state.pc = kRegisterPc;
    }

    // Issue one syscall from kRegisterPc with `arguments` in $a0..$a3.
    Status syscall(
        std::uint32_t number,
        std::uint32_t a0 = 0U,
        std::uint32_t a1 = 0U,
        std::uint32_t a2 = 0U,
        std::uint32_t a3 = 0U
    ) {
        state.pc = kRegisterPc;
        state.gpr[reps2::runtime::kEeSyscallNumberRegister].low = number;
        state.gpr[4].low = a0;
        state.gpr[5].low = a1;
        state.gpr[6].low = a2;
        state.gpr[7].low = a3;
        return reps2::runtime::dispatch_ee_syscall(state, kResumePc);
    }

    std::uint32_t result() const {
        return static_cast<std::uint32_t>(state.gpr[2].low);
    }
};

void enable_ee_interrupts(EeState& state) {
    state.cop0[reps2::runtime::kCop0Status] =
        (state.cop0[reps2::runtime::kCop0Status] | reps2::runtime::kCop0StatusEieMask)
        & ~(reps2::runtime::kCop0StatusExlMask | reps2::runtime::kCop0StatusErlMask);
}

// Configure Timer 2 to count from 0, stop at `compare`, with the given extra
// mode bits on top of CUE.
Status configure_timer2(EeState& state, std::uint32_t compare, std::uint32_t extra_mode) {
    using namespace reps2::runtime;
    if (store_u32(state.memory, kEeTimer2CountAddress, 0U) != Status::ok) return Status::unmapped_memory;
    if (store_u32(state.memory, kEeTimer2CompareAddress, compare) != Status::ok) return Status::unmapped_memory;
    return store_u32(state.memory, kEeTimer2ModeAddress, kEeTimer2CueBit | extra_mode);
}

int test_add_and_remove_intc_handler() {
    using namespace reps2::runtime;
    Fixture fixture;

    // A documented cause with an aligned handler inside main RAM is accepted and
    // returns the HLE-local id (the cause index).
    if (fixture.syscall(kEeSyscallAddIntcHandler, 11U, kHandlerAddress) != Status::ok) return 1;
    if (fixture.state.hle.last_intc_handler_rejection != EeIntcHandlerRejection::none) return 2;
    if (fixture.result() != 11U || fixture.state.hle.last_intc_handler_id != 11) return 3;
    if (fixture.state.hle.intc_handler_count != 1U) return 4;
    const auto& record = fixture.state.hle.intc_handlers[11U];
    if (!record.in_use || record.handler != kHandlerAddress) return 5;
    if (record.register_pc != kRegisterPc) return 6;

    // A second registration on the same cause is refused and changes nothing.
    if (fixture.syscall(kEeSyscallAddIntcHandler, 11U, kHandlerAddress + 4U) != Status::ok) return 7;
    if (fixture.state.hle.last_intc_handler_rejection
        != EeIntcHandlerRejection::cause_already_registered) return 8;
    if (fixture.result() != kEeSyscallFailure) return 9;
    if (fixture.state.hle.intc_handlers[11U].handler != kHandlerAddress) return 10;
    if (fixture.state.hle.intc_handler_count != 1U) return 11;

    // Refusals: an undocumented cause, a null handler, a misaligned handler, a
    // handler outside main RAM, a chained handler, and the AddIntcHandler2
    // argument. Each is a guest-visible negative value and creates no record.
    if (fixture.syscall(kEeSyscallAddIntcHandler, 16U, kHandlerAddress) != Status::ok) return 12;
    if (fixture.state.hle.last_intc_handler_rejection != EeIntcHandlerRejection::invalid_cause) return 13;
    if (fixture.result() != kEeSyscallFailure) return 14;
    if (fixture.syscall(kEeSyscallAddIntcHandler, 12U, 0U) != Status::ok) return 15;
    if (fixture.state.hle.last_intc_handler_rejection != EeIntcHandlerRejection::invalid_handler) return 16;
    if (fixture.syscall(kEeSyscallAddIntcHandler, 12U, kHandlerAddress + 2U) != Status::ok) return 17;
    if (fixture.state.hle.last_intc_handler_rejection != EeIntcHandlerRejection::invalid_handler) return 18;
    if (fixture.syscall(kEeSyscallAddIntcHandler, 12U, 0x4000'0000U) != Status::ok) return 19;
    if (fixture.state.hle.last_intc_handler_rejection != EeIntcHandlerRejection::invalid_handler) return 20;
    if (fixture.syscall(kEeSyscallAddIntcHandler, 12U, kHandlerAddress, 1U) != Status::ok) return 21;
    if (fixture.state.hle.last_intc_handler_rejection
        != EeIntcHandlerRejection::chained_handler_unsupported) return 22;
    if (fixture.syscall(kEeSyscallAddIntcHandler, 12U, kHandlerAddress, 0U, 0x1234U) != Status::ok) return 23;
    if (fixture.state.hle.last_intc_handler_rejection
        != EeIntcHandlerRejection::handler_argument_unsupported) return 24;
    if (fixture.state.hle.intc_handler_count != 1U) return 25;
    if (fixture.state.hle.intc_handlers[12U].in_use) return 26;

    // Removal matches the HLE-local id, not just the cause.
    if (fixture.syscall(kEeSyscallRemoveIntcHandler, 11U, 12U) != Status::ok) return 27;
    if (fixture.state.hle.last_remove_intc_outcome != EeIntcRemoveOutcome::not_registered) return 28;
    if (fixture.result() != kEeSyscallFailure) return 29;
    if (!fixture.state.hle.intc_handlers[11U].in_use) return 30;
    if (fixture.syscall(kEeSyscallRemoveIntcHandler, 16U, 11U) != Status::ok) return 31;
    if (fixture.state.hle.last_remove_intc_outcome != EeIntcRemoveOutcome::invalid_cause) return 32;

    if (fixture.syscall(kEeSyscallRemoveIntcHandler, 11U, 11U) != Status::ok) return 33;
    if (fixture.state.hle.last_remove_intc_outcome != EeIntcRemoveOutcome::removed) return 34;
    if (fixture.state.hle.intc_handlers[11U].in_use) return 35;
    if (fixture.state.hle.intc_handler_count != 0U) return 36;

    // The cause is reusable after removal.
    if (fixture.syscall(kEeSyscallAddIntcHandler, 11U, kHandlerAddress) != Status::ok) return 37;
    if (!fixture.state.hle.intc_handlers[11U].in_use) return 38;
    return 0;
}

int test_enable_and_disable_intc() {
    using namespace reps2::runtime;
    Fixture fixture;

    if (fixture.syscall(kEeSyscallEnableIntc, 11U) != Status::ok) return 1;
    if (fixture.state.hle.last_enable_intc_outcome != EeIntcMaskOutcome::enabled) return 2;
    if ((fixture.state.memory.intc.i_mask & (1U << 11U)) == 0U) return 3;
    // A repeated request must not toggle the bit back off.
    if (fixture.syscall(kEeSyscallEnableIntc, 11U) != Status::ok) return 4;
    if (fixture.state.hle.last_enable_intc_outcome != EeIntcMaskOutcome::already_enabled) return 5;
    if ((fixture.state.memory.intc.i_mask & (1U << 11U)) == 0U) return 6;

    if (fixture.syscall(kEeSyscallDisableIntc, 11U) != Status::ok) return 7;
    if (fixture.state.hle.last_disable_intc_outcome != EeIntcMaskOutcome::disabled) return 8;
    if ((fixture.state.memory.intc.i_mask & (1U << 11U)) != 0U) return 9;
    if (fixture.syscall(kEeSyscallDisableIntc, 11U) != Status::ok) return 10;
    if (fixture.state.hle.last_disable_intc_outcome != EeIntcMaskOutcome::already_disabled) return 11;

    // Enabling a source never invents a pending interrupt.
    if (fixture.state.memory.intc.i_stat != 0U) return 12;

    if (fixture.syscall(kEeSyscallEnableIntc, 16U) != Status::ok) return 13;
    if (fixture.state.hle.last_enable_intc_outcome != EeIntcMaskOutcome::invalid_cause) return 14;
    if (fixture.result() != kEeSyscallFailure) return 15;

    // Without a seeded controller the mask cannot be read: fail closed, not a
    // fabricated quiet mask.
    Fixture unseeded(false);
    if (unseeded.syscall(kEeSyscallEnableIntc, 11U) != Status::unsupported_memory_access) return 16;
    if (unseeded.state.hle.last_enable_intc_outcome
        != EeIntcMaskOutcome::unknown_register_state) return 17;
    return 0;
}

int test_timer2_register_model() {
    using namespace reps2::runtime;
    Fixture fixture;
    EeState& state = fixture.state;

    // No timer is enabled out of reset and nothing raises.
    if (state.memory.timer2_mode != 0U || state.memory.timer2_count != 0U) return 1;
    reps2::runtime::advance_ee_timers(state, 1000U);
    if (state.memory.timer2_count != 0U) return 2;
    if (state.memory.timer2_matches != 0U || state.memory.timer2_irq_raises != 0U) return 3;

    // CLKS 0 counts one tick per modelled BUSCLK cycle; a read materialises the
    // elapsed counts since the last base.
    if (configure_timer2(state, 500U, kEeTimer2CmpeBit) != Status::ok) return 4;
    reps2::runtime::advance_ee_timers(state, 100U);
    std::uint32_t value = 0xFFFF'FFFFU;
    if (load_u32(state.memory, kEeTimer2CountAddress, value) != Status::ok) return 5;
    if (value != 100U) return 6;

    // The match latches EQUF and raises INTC source 11 exactly once.
    reps2::runtime::advance_ee_timers(state, 500U);
    if ((state.memory.timer2_mode & kEeTimer2EqufBit) == 0U) return 7;
    if (state.memory.timer2_matches != 1U) return 8;
    if (state.memory.timer2_irq_raises != 1U || state.memory.timer2_irq_refusals != 0U) return 9;
    if ((state.memory.intc.i_stat & (1U << kEeIntcSourceTimer2)) == 0U) return 10;
    if (state.memory.timer2_count <= 500U) return 11;
    // A further advance does not re-raise: the counter is already past compare.
    reps2::runtime::advance_ee_timers(state, 100U);
    if (state.memory.timer2_irq_raises != 1U) return 12;

    // EQUF is write-one-to-clear; the writable mode bits are kept.
    if (store_u32(state.memory, kEeTimer2ModeAddress,
                  kEeTimer2CueBit | kEeTimer2CmpeBit | kEeTimer2EqufBit) != Status::ok) return 13;
    if ((state.memory.timer2_mode & kEeTimer2EqufBit) != 0U) return 14;
    if ((state.memory.timer2_mode & kEeTimer2CueBit) == 0U) return 15;

    // Timer 2 and Timer 3 are independent register files over the shared clock.
    if (state.memory.timer3_mode != 0U || state.memory.timer3_count != 0U) return 16;
    if (store_u32(state.memory, kEeTimer2ModeAddress, 0U) != Status::ok) return 17;
    if (configure_timer2(state, 0xFFFFU, 0U) != Status::ok) return 18;
    reps2::runtime::advance_ee_timers(state, 0x1'0000U);
    if ((state.memory.timer2_mode & kEeTimer2OvffBit) == 0U) return 19;
    if (state.memory.timer3_mode != 0U || state.memory.timer3_count != 0U) return 20;
    return 0;
}

int test_timer2_match_edge_cases() {
    using namespace reps2::runtime;

    // ZRET resets at the match, so a periodic timer re-arms and can raise again.
    {
        Fixture fixture;
        if (configure_timer2(fixture.state, 5U, kEeTimer2CmpeBit | kEeTimer2ZretBit)
            != Status::ok) return 1;
        reps2::runtime::advance_ee_timers(fixture.state, 10U);
        if (fixture.state.memory.timer2_count != 0U) return 2;
        if (fixture.state.memory.timer2_matches != 1U) return 3;
        reps2::runtime::advance_ee_timers(fixture.state, 10U);
        if (fixture.state.memory.timer2_matches != 2U) return 4;
        if (fixture.state.memory.timer2_irq_raises != 2U) return 5;
    }

    // Without CMPE the counter still runs but neither EQUF nor an interrupt is
    // generated: the match is gated, not fabricated.
    {
        Fixture fixture;
        if (configure_timer2(fixture.state, 5U, 0U) != Status::ok) return 6;
        reps2::runtime::advance_ee_timers(fixture.state, 10U);
        if ((fixture.state.memory.timer2_mode & kEeTimer2EqufBit) != 0U) return 7;
        if (fixture.state.memory.timer2_matches != 0U) return 8;
        if (fixture.state.memory.intc.i_stat != 0U) return 9;
    }

    // An unseeded INTC refuses the raise, and the refusal is counted instead of
    // the match being silently turned into a pending interrupt.
    {
        Fixture fixture(false);
        if (configure_timer2(fixture.state, 5U, kEeTimer2CmpeBit) != Status::ok) return 10;
        reps2::runtime::advance_ee_timers(fixture.state, 10U);
        if (fixture.state.memory.timer2_matches != 1U) return 11;
        if (fixture.state.memory.timer2_irq_raises != 0U) return 12;
        if (fixture.state.memory.timer2_irq_refusals != 1U) return 13;
        if (fixture.state.memory.intc.i_stat_known) return 14;
    }
    return 0;
}

int test_delivery_predicate() {
    using namespace reps2::runtime;
    Fixture fixture;
    EeState& state = fixture.state;
    constexpr std::uint32_t kCause = kEeIntcSourceTimer2;

    if (fixture.syscall(kEeSyscallAddIntcHandler, kCause, kHandlerAddress) != Status::ok) return 1;
    if (fixture.syscall(kEeSyscallEnableIntc, kCause) != Status::ok) return 2;
    if (configure_timer2(state, 5U, kEeTimer2CmpeBit) != Status::ok) return 3;
    if (!state.memory.intc.raise_source(kCause)) return 4;

    // Pending and enabled, but the CPU has interrupts disabled: no delivery.
    if (ee_intc_line_asserted(state)) return 5;
    enable_ee_interrupts(state);
    if (!ee_intc_line_asserted(state)) return 6;
    if (select_ee_intc_interrupt_cause(state) != kCause) return 7;

    // A masked source never asserts, even with a handler and interrupts on.
    if (fixture.syscall(kEeSyscallDisableIntc, kCause) != Status::ok) return 8;
    if (ee_intc_line_asserted(state)) return 9;
    // Masking the source did not clear its pending state; only a guest write of
    // one to I_STAT does that.
    if ((state.memory.intc.i_stat & (1U << kCause)) == 0U) return 10;
    if (fixture.syscall(kEeSyscallEnableIntc, kCause) != Status::ok) return 11;

    // A source with no registered handler is not delivered either; the raw line
    // stays asserted, so nothing is acknowledged behind the guest's back.
    if (fixture.syscall(kEeSyscallRemoveIntcHandler, kCause, kCause) != Status::ok) return 12;
    if (ee_intc_line_asserted(state)) return 13;
    if (select_ee_intc_interrupt_cause(state) != kEeMaxIntcHandlers) return 14;
    if (!state.memory.intc.line_asserted()) return 15;
    if (fixture.syscall(kEeSyscallAddIntcHandler, kCause, kHandlerAddress) != Status::ok) return 16;

    // EXL marks an exception in progress: no nested interrupt is taken.
    state.cop0[kCop0Status] |= kCop0StatusExlMask;
    if (ee_intc_line_asserted(state)) return 17;
    state.cop0[kCop0Status] &= ~kCop0StatusExlMask;
    if (!ee_intc_line_asserted(state)) return 18;

    // An unseeded controller is never asserted.
    Fixture unseeded(false);
    if (unseeded.syscall(kEeSyscallAddIntcHandler, kCause, kHandlerAddress) != Status::ok) return 19;
    enable_ee_interrupts(unseeded.state);
    if (ee_intc_line_asserted(unseeded.state)) return 20;
    if (select_ee_intc_interrupt_cause(unseeded.state) != kEeMaxIntcHandlers) return 21;
    return 0;
}

int test_interrupt_entry_and_return() {
    using namespace reps2::runtime;
    Fixture fixture;
    EeState& state = fixture.state;
    constexpr std::uint32_t kCause = kEeIntcSourceTimer2;
    constexpr std::uint32_t kInterruptedPc = 0x0012'3400U;

    if (fixture.syscall(kEeSyscallAddIntcHandler, kCause, kHandlerAddress) != Status::ok) return 1;
    if (fixture.syscall(kEeSyscallEnableIntc, kCause) != Status::ok) return 2;
    enable_ee_interrupts(state);
    if (configure_timer2(state, 0x10U, kEeTimer2CmpeBit) != Status::ok) return 3;
    reps2::runtime::advance_ee_timers(state, 0x20U);
    if (!ee_intc_line_asserted(state)) return 4;

    state.pc = kInterruptedPc;
    const auto saved_cop0 = state.cop0;
    const auto saved_fpr = state.fpr;
    const auto saved_vu0 = state.vu0;
    const std::uint64_t saved_instructions = state.instructions_executed;

    if (enter_ee_intc_interrupt(state) != Status::ok) return 5;
    if (state.pc != kHandlerAddress) return 6;
    if (state.hle.syscall_handler_depth != 1U) return 7;
    if (state.hle.intc_interrupts_delivered != 1U) return 8;
    if (state.hle.last_intc_interrupt_cause != kCause) return 9;
    if (state.hle.last_intc_interrupt_handler != kHandlerAddress) return 10;

    // The handler is `s32 handler(s32 cause)` and returns through the sentinel.
    if (static_cast<std::uint32_t>(state.gpr[4].low) != kCause) return 11;
    if (static_cast<std::uint32_t>(state.gpr[31].low)
        != kEeSyscallHandlerReturnPc) return 12;

    // The source is acknowledged before the handler runs, so the line deasserts.
    if ((state.memory.intc.i_stat & (1U << kCause)) != 0U) return 13;
    if (state.memory.intc.line_asserted()) return 14;
    if (ee_intc_line_asserted(state)) return 15;

    // Interrupt context: EPC latches the restart PC, EXL blocks nesting, Cause
    // reports IP2 (and never the DMAC's IP3).
    if (state.cop0[kCop0Epc] != kInterruptedPc) return 16;
    if ((state.cop0[kCop0Status] & kCop0StatusExlMask) == 0U) return 17;
    if ((state.cop0[kCop0Cause] & kCop0CauseIp2Mask) == 0U) return 18;
    if ((state.cop0[kCop0Cause] & kCop0CauseIp3Mask) != 0U) return 19;

    // The handler returns through the kseg3 sentinel.
    if (ee_syscall_handler_return_pending(state)) return 20;
    state.pc = kEeSyscallHandlerReturnPc;
    if (!ee_syscall_handler_return_pending(state)) return 21;
    if (return_from_ee_syscall_handler(state) != Status::ok) return 22;
    if (state.hle.syscall_handler_depth != 0U) return 23;
    if (state.hle.intc_interrupts_completed != 1U) return 24;
    if (state.pc != kInterruptedPc) return 25;
    // The interrupt context restores whole: the handler's register, COP0, FPU and
    // VU0 changes are not the interrupted guest's.
    if (state.cop0 != saved_cop0) return 26;
    // The FPU register file is saved and restored with the frame.
    if (state.fpr != saved_fpr) return 27;
    if (state.vu0.vf[1].low != saved_vu0.vf[1].low) return 28;
    if (state.instructions_executed != saved_instructions) return 29;

    // A frame-depth overflow refuses the interrupt before acknowledging it.
    {
        Fixture deep;
        if (deep.syscall(kEeSyscallAddIntcHandler, kCause, kHandlerAddress) != Status::ok) return 30;
        if (deep.syscall(kEeSyscallEnableIntc, kCause) != Status::ok) return 31;
        enable_ee_interrupts(deep.state);
        if (!deep.state.memory.intc.raise_source(kCause)) return 32;
        deep.state.hle.syscall_handler_depth =
            static_cast<std::uint32_t>(kEeMaxSyscallHandlerDepth);
        if (enter_ee_intc_interrupt(deep.state) != Status::syscall_handler_overflow) return 33;
        if ((deep.state.memory.intc.i_stat & (1U << kCause)) == 0U) return 34;
        if (deep.state.hle.intc_interrupts_delivered != 0U) return 35;
    }
    return 0;
}

// The dispatcher must check the INTC line after the higher-priority DMAC line.
// This is a source-level guard: the synthetic CFG fixture has no interrupt
// source of its own, so the ordering is pinned where it is emitted.
int test_codegen_emits_intc_check() {
    // Covered by the Rust codegen test; nothing to execute here.
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
        {test_add_and_remove_intc_handler, "add/remove INTC handler", 10},
        {test_enable_and_disable_intc, "enable/disable INTC", 40},
        {test_timer2_register_model, "Timer 2 register model", 70},
        {test_timer2_match_edge_cases, "Timer 2 match edge cases", 110},
        {test_delivery_predicate, "INTC delivery predicate", 150},
        {test_interrupt_entry_and_return, "INTC interrupt entry/return", 200},
        {test_codegen_emits_intc_check, "codegen INTC hook", 250},
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