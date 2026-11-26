// SPDX-License-Identifier: GPL-3.0-or-later
//
// Intra-batch VBLANK synchronization, generated-code half (ADR 0220).
//
// The synthetic CFG fixture exposes one entry for this slice at guest PC 0x7600:
//
//   0x7600  lui   t0, 0x1000; ori t0, t0, 0xF000   t0 = I_STAT (0x1000F000)
//   0x7608  ori   t1, zero, 4                      t1 = 1 << kEeIntcSourceVblankStart
//   0x760C  or    s0, a0, zero                     s0 = requested frame count ($a0)
//   0x7610  or    s1, zero, zero                   s1 = observed VBLANK_STARTs
//   0x7614  sw    t1, 0(t0)                        W1C-clear bit 2
//   0x7618  lw    t2, 0(t0); andi t2,t2,4; beq t2,zero,0x7618
//   0x7628  addiu s1, s1, 1
//   0x762C  bne   s1, s0, 0x7614                   next frame
//   0x7634  jr    ra
//
// This drives the AOT-compiled blocks, so the check under test is the emitted
// dispatcher hook: without the intra-batch `sync_ee_timers` the guest can never
// observe more than one VBLANK_START inside a single `execute_cfg` batch, and a
// tight poll loop exhausts the batch budget instead.
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

constexpr std::uint32_t kProgramPc = 0x0000'7600U;
constexpr std::uint32_t kUnreachableReturnPc = 0xDEAD'0000U;
constexpr std::uint32_t kRequestedFrames = 8U;
// Eight NTSC frames plus headroom, well inside one batch.
constexpr std::uint64_t kSingleBatchBudget = 45'000'000ULL;

struct Fixture {
    std::vector<std::uint8_t> ram;
    EeState state;

    explicit Fixture(bool seed_intc = true) : ram(0x0020'0000U, std::uint8_t{0}) {
        state.memory.main_ram = ram;
        if (seed_intc) {
            state.memory.intc.seed_idle_boot_state();
        }
        state.pc = kProgramPc;
        state.gpr[4].low = kRequestedFrames;          // $a0: frames to wait for
        state.gpr[31].low = kUnreachableReturnPc;     // $ra: terminal sentinel
    }
};

// The whole poll loop runs inside ONE batch: the guest must observe all
// kRequestedFrames VBLANK_START edges without the host stepping between them.
int test_multi_vblank_in_one_batch() {
    Fixture fixture;
    EeState& state = fixture.state;

    const Status status = reps2::generated::execute_cfg(state, kSingleBatchBudget);
    if (status != Status::external_control_flow) return 1;
    if (state.pc != kUnreachableReturnPc) return 2;
    // $s1 counts the VBLANK_START edges the guest actually observed.
    if (static_cast<std::uint32_t>(state.gpr[17].low) != kRequestedFrames) return 3;
    if (state.memory.vblank_starts < kRequestedFrames) return 4;
    if (state.memory.vblank_irq_refusals != 0U) return 5;
    if (state.instructions_executed > kSingleBatchBudget) return 6;
    return 0;
}

// The same run repeated is byte-identical: the schedule derives only from guest
// instruction counts, never from host time or address space.
int test_deterministic_replay() {
    Fixture a;
    Fixture b;
    (void)reps2::generated::execute_cfg(a.state, kSingleBatchBudget);
    (void)reps2::generated::execute_cfg(b.state, kSingleBatchBudget);
    if (a.state.gpr[17].low != b.state.gpr[17].low) return 1;
    if (a.state.instructions_executed != b.state.instructions_executed) return 2;
    if (a.state.memory.vblank_starts != b.state.memory.vblank_starts) return 3;
    if (a.state.memory.intc.i_stat != b.state.memory.intc.i_stat) return 4;
    return 0;
}

// `sync_ee_timers` must be idempotent: a second call with no instruction
// progress neither re-advances the cycle source nor re-raises a source.
int test_sync_is_idempotent() {
    Fixture fixture;
    EeState& state = fixture.state;
    state.instructions_executed = 5'000'000ULL;
    reps2::runtime::sync_ee_timers(state);
    const std::uint64_t cycles = state.memory.guest_bus_cycles;
    const std::uint32_t stat = state.memory.intc.i_stat;
    const std::uint64_t starts = state.memory.vblank_starts;
    reps2::runtime::sync_ee_timers(state);
    reps2::runtime::sync_ee_timers(state);
    if (state.memory.guest_bus_cycles != cycles) return 1;
    if (state.memory.intc.i_stat != stat) return 2;
    if (state.memory.vblank_starts != starts) return 3;
    // A watermark never rewinds.
    state.instructions_executed = 1'000'000ULL;
    reps2::runtime::sync_ee_timers(state);
    if (state.memory.guest_bus_cycles != cycles) return 4;
    return 0;
}

// Fail closed: with an unseeded INTC even the guest's own write-one-to-clear
// store to I_STAT is refused (`unsupported_memory_access`), so nothing is
// fabricated and no VBLANK edge is manufactured.
int test_unseeded_intc_fails_closed() {
    Fixture fixture(/*seed_intc=*/false);
    EeState& state = fixture.state;
    constexpr std::uint64_t kBudget = 2'000'000ULL;
    const Status status = reps2::generated::execute_cfg(state, kBudget);
    if (status != reps2::runtime::Status::unsupported_memory_access) return 1;
    if (state.memory.vblank_starts != 0U) return 2;
    if (state.gpr[17].low != 0U) return 3;
    return 0;
}

// Batch-size invariance (ADR 0220): running the same polling guest across the
// same total budget (45,000,000 instructions) as 1 batch, 16 batches, and 450
// small batches must observe the same VBLANK_START count (+/-1), identical
// frame-loop completions, and identical stop status/PC.
int test_batch_size_invariance() {
    auto run_with_batches = [](std::uint64_t batch_budget, std::uint64_t max_batches) {
        Fixture f;
        Status last_status = Status::ok;
        for (std::uint64_t i = 0; i < max_batches; ++i) {
            last_status = reps2::generated::execute_cfg(f.state, batch_budget);
            reps2::runtime::sync_ee_timers(f.state);
            if (last_status != Status::instruction_budget_exhausted) {
                break;
            }
        }
        struct Summary {
            Status status;
            std::uint32_t pc;
            std::uint64_t frames_observed;
            std::uint64_t vblank_starts;
        };
        return Summary{last_status, f.state.pc, f.state.gpr[17].low, f.state.memory.vblank_starts};
    };

    const auto one = run_with_batches(45'000'000ULL, 1ULL);
    const auto sixteen = run_with_batches(2'812'500ULL, 16ULL);
    const auto many = run_with_batches(100'000ULL, 450ULL);

    if (one.status != Status::external_control_flow || one.pc != kUnreachableReturnPc) return 1;
    if (sixteen.status != one.status || sixteen.pc != one.pc) return 2;
    if (many.status != one.status || many.pc != one.pc) return 3;
    if (one.frames_observed != kRequestedFrames) return 4;
    if (sixteen.frames_observed != one.frames_observed || many.frames_observed != one.frames_observed) return 5;

    auto diff = [](std::uint64_t a, std::uint64_t b) { return a > b ? a - b : b - a; };
    if (diff(one.vblank_starts, sixteen.vblank_starts) > 1U) return 6;
    if (diff(one.vblank_starts, many.vblank_starts) > 1U) return 7;
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
        {test_multi_vblank_in_one_batch, "intra-batch VBLANK poll", 10},
        {test_deterministic_replay, "deterministic replay", 20},
        {test_sync_is_idempotent, "sync idempotence", 30},
        {test_unseeded_intc_fails_closed, "unseeded INTC fail-closed", 40},
        {test_batch_size_invariance, "batch-size invariance", 50},
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
