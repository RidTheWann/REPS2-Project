// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic EE VBLANK timing tests (ADR 0218).
#include "reps2/runtime/ee_hle.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
using namespace reps2::runtime;
constexpr std::uint32_t kStat = 0x1000'F000U;
constexpr std::uint32_t kMask = 0x1000'F010U;
constexpr std::uint32_t kStartBit = 1U << kEeIntcSourceVblankStart;
constexpr std::uint32_t kEndBit = 1U << kEeIntcSourceVblankEnd;
constexpr std::uint64_t kStart = kEeVblankCyclesPerFrame - kEeVblankDurationCycles;

struct Fixture {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x0020'0000U, std::uint8_t{0});
    EeState state{};
    Fixture() { state.memory.main_ram = ram; state.memory.intc.seed_idle_boot_state(); }
    std::uint32_t stat() const { std::uint32_t v{}; (void)load_u32(state.memory,kStat,v); return v; }
    std::uint32_t mask() const { std::uint32_t v{}; (void)load_u32(state.memory,kMask,v); return v; }
};

int test_before_period() {
    Fixture f;
    advance_ee_timers(f.state, kStart - 1U);
    return ((f.stat() & kStartBit) == 0U && f.state.memory.vblank_starts == 0U) ? 0 : 1;
}

int test_start_and_end_boundaries() {
    Fixture f;
    advance_ee_timers(f.state, kStart);
    if ((f.stat() & kStartBit) == 0U || f.state.memory.vblank_starts != 1U || !f.state.memory.vblank_in_blank) return 1;
    if ((f.stat() & kEndBit) != 0U) return 2;
    advance_ee_timers(f.state, kEeVblankDurationCycles);
    return ((f.stat() & kEndBit) != 0U && f.state.memory.vblank_ends == 1U && !f.state.memory.vblank_in_blank) ? 0 : 3;
}

int test_multi_frame_jump() {
    Fixture f;
    advance_ee_timers(f.state, 3U*kEeVblankCyclesPerFrame + kStart + 10U);
    return (f.state.memory.vblank_starts == 4U && f.state.memory.vblank_ends == 3U
        && (f.stat() & (kStartBit|kEndBit)) == (kStartBit|kEndBit)) ? 0 : 1;
}

int test_clear_w1c() {
    Fixture f; advance_ee_timers(f.state,kStart);
    if (store_u32(f.state.memory,kStat,kStartBit)!=Status::ok) return 1;
    return (f.stat() & kStartBit)==0U ? 0 : 2;
}

int test_mask_does_not_hide_pending() {
    Fixture f; advance_ee_timers(f.state,kStart);
    if (f.mask()!=0U || (f.stat()&kStartBit)==0U) return 1;
    if (store_u32(f.state.memory,kMask,kStartBit)!=Status::ok) return 2;
    return (f.mask()==kStartBit && (f.stat()&kStartBit)!=0U) ? 0 : 3;
}

int test_deterministic_replay() {
    Fixture a,b;
    constexpr std::uint64_t steps[]{1000U,kStart,kEeVblankDurationCycles,2U*kEeVblankCyclesPerFrame};
    for (const auto n:steps) { advance_ee_timers(a.state,n); advance_ee_timers(b.state,n); }
    return (a.stat()==b.stat() && a.state.memory.vblank_starts==b.state.memory.vblank_starts
        && a.state.memory.vblank_ends==b.state.memory.vblank_ends
        && a.state.memory.vblank_in_blank==b.state.memory.vblank_in_blank) ? 0 : 1;
}

int test_unseeded_intc_fails_closed() {
    Fixture f; f.state.memory.intc.i_stat_known=false;
    advance_ee_timers(f.state,kStart);
    return (f.state.memory.vblank_starts==0U && f.state.memory.vblank_irq_refusals==1U) ? 0 : 1;
}

// Mid-batch timer arming under deadline-based sync (ADR 0220):
// When `guest_bus_cycles` has not yet caught up to `instructions_executed`,
// enabling Timer 2 via MMIO must not attribute pre-arm cycles to Timer 2.
int test_timer2_mid_batch_arm_rebase() {
    Fixture f;
    f.state.instructions_executed = 500'000ULL;
    // Arm Timer 2 with compare = 1000 ticks, CMPE enabled; cycles 0..500,000
    // elapsed BEFORE arming.
    if (store_u32(f.state.memory, kEeTimer2CountAddress, 0U) != Status::ok) return 1;
    if (store_u32(f.state.memory, kEeTimer2CompareAddress, 1000U) != Status::ok) return 2;
    if (store_u32(f.state.memory, kEeTimer2ModeAddress,
                  kEeTimer2CueBit | kEeTimer2CmpeBit) != Status::ok) return 3;
    sync_ee_timers(f.state);
    if (f.state.memory.timer2_matches != 0U) return 4;
    if (f.state.memory.timer2_cycles_at_base != 500'000ULL) return 5;
    // Advance past compare = 1000 cycles after arming: now the match fires once.
    f.state.instructions_executed = 501'001ULL;
    sync_ee_timers(f.state);
    return (f.state.memory.timer2_matches == 1U && f.state.memory.timer2_irq_raises == 1U) ? 0 : 6;
}

} // namespace

int main() {
    struct Test { const char* name; int (*run)(); };
    constexpr Test tests[]{{"before_period",test_before_period},{"start_end",test_start_and_end_boundaries},
        {"multi_frame_jump",test_multi_frame_jump},{"clear_w1c",test_clear_w1c},
        {"mask_polling",test_mask_does_not_hide_pending},{"deterministic_replay",test_deterministic_replay},
        {"unseeded_fails_closed",test_unseeded_intc_fails_closed},
        {"timer2_mid_batch_arm_rebase",test_timer2_mid_batch_arm_rebase}};
    for (const auto& t:tests) { const int rc=t.run(); if(rc!=0){std::fprintf(stderr,"%s:%d\n",t.name,rc);return rc;} }
    return 0;
}