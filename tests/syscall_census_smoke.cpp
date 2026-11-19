// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bounded EE syscall census and trace-ring smoke test (ADR 0170).
//
// The census is the diagnostic that names the dominant service and its hottest
// call sites for a bounded probe. These checks pin the accounting invariants,
// per-service and per-site attribution (including the negated "i" form), the
// overflow bucket for numbers outside the table, and the trace ring's capacity,
// ordering and disable path.
#include "reps2/runtime/ee_hle.hpp"
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>
#include <cstdio>

namespace {

using reps2::runtime::EeHleState;
using reps2::runtime::EeState;
using reps2::runtime::Status;

// A serviced syscall with no memory arguments and no thread dependency.
constexpr std::uint32_t kServicedA = reps2::runtime::kEeSyscallGetThreadId;
// A second serviced syscall with a different number.
constexpr std::uint32_t kServicedB = reps2::runtime::kEeSyscallFlushCache;

Status dispatch_at(EeState& state, std::uint32_t pc, std::uint32_t code) {
    state.pc = pc;
    state.gpr[reps2::runtime::kEeSyscallNumberRegister].low = code;
    return reps2::runtime::dispatch_ee_syscall(state, pc + 4U);
}

std::uint32_t first_unserved_number() {
    for (std::uint32_t number = 0U; number < EeHleState::kSyscallCensusNumbers; ++number) {
        if (reps2::runtime::find_ee_syscall_service(number) == nullptr) {
            return number;
        }
    }
    return 0xFFFF'FFFFU;
}

int test_service_and_site_attribution() {
    EeState state{};
    state.hle.syscall_enabled = true;
    const std::uint32_t unserved = first_unserved_number();
    if (unserved == 0xFFFF'FFFFU) return 1;

    // Two calls from one site, one call from a second site, then one refused.
    if (dispatch_at(state, 0x0010'0000U, kServicedA) != Status::ok) return 2;
    if (dispatch_at(state, 0x0010'0000U, kServicedA) != Status::ok) return 3;
    if (dispatch_at(state, 0x0010'0020U, kServicedB) != Status::ok) return 4;
    if (dispatch_at(state, 0x0010'0040U, unserved) != Status::unhandled_syscall) return 5;

    const auto& a = state.hle.syscall_census[kServicedA];
    if (a.seen != 2U || a.handled != 2U || a.refused != 0U) return 6;
    const auto& b = state.hle.syscall_census[kServicedB];
    if (b.seen != 1U || b.handled != 1U || b.refused != 0U) return 7;
    const auto& refused = state.hle.syscall_census[unserved];
    if (refused.seen != 1U || refused.handled != 0U || refused.refused != 1U) return 8;

    // The census aggregates the same boundaries the run report counts.
    if (state.hle.syscall_count != 4U || state.hle.syscall_handled_count != 3U) return 9;

    // seen == handled + refused for every occupied slot.
    for (std::size_t number = 0U; number < state.hle.syscall_census.size(); ++number) {
        const auto& entry = state.hle.syscall_census[number];
        if (entry.seen != entry.handled + entry.refused) return 10;
    }

    bool found_a = false;
    bool found_b = false;
    for (const auto& site : state.hle.syscall_hot_sites) {
        if (site.count == 0U) continue;
        if (site.pc == 0x0010'0000U && site.number == kServicedA) {
            found_a = site.count == 2U;
        }
        if (site.pc == 0x0010'0020U && site.number == kServicedB) {
            found_b = site.count == 1U;
        }
    }
    if (!found_a || !found_b) return 11;

    // The last boundary is the refused one, so report correlation can name it.
    if (state.hle.last_syscall_pc != 0x0010'0040U
        || state.hle.last_syscall_number != unserved) return 12;
    return 0;
}

int test_sites_sharing_a_service() {
    EeState state{};
    state.hle.syscall_enabled = true;

    // Three calls from one site and one from another: both stay resident with
    // exact counts because the table has room.
    for (int index = 0; index < 3; ++index) {
        if (dispatch_at(state, 0x0010'1000U, kServicedA) != Status::ok) return 20;
    }
    if (dispatch_at(state, 0x0010'2000U, kServicedA) != Status::ok) return 21;

    if (state.hle.syscall_census[kServicedA].seen != 4U) return 22;

    bool saw_first = false;
    bool saw_second = false;
    for (const auto& site : state.hle.syscall_hot_sites) {
        if (site.pc == 0x0010'1000U && site.number == kServicedA && site.count == 3U) {
            saw_first = true;
        }
        if (site.pc == 0x0010'2000U && site.number == kServicedA && site.count == 1U) {
            saw_second = true;
        }
    }
    if (!saw_first || !saw_second) return 23;
    return 0;
}

int test_negated_i_form_attribution() {
    EeState state{};
    state.hle.syscall_enabled = true;

    // The "i" wrappers pass -number; dispatch uses its magnitude, so the census
    // must fold both forms into the same service entry.
    if (dispatch_at(state, 0x0010'0000U, 0U - kServicedA) != Status::ok) return 30;
    if (dispatch_at(state, 0x0010'0004U, kServicedA) != Status::ok) return 31;

    const auto& entry = state.hle.syscall_census[kServicedA];
    if (entry.seen != 2U || entry.handled != 2U || entry.refused != 0U) return 32;
    if (state.hle.last_syscall_number != kServicedA) return 33;
    return 0;
}

int test_overflow_bucket() {
    EeState state{};
    state.hle.syscall_enabled = true;
    constexpr std::uint32_t kOutsideTable = EeHleState::kSyscallCensusNumbers;
    if (reps2::runtime::find_ee_syscall_service(kOutsideTable) != nullptr) return 40;

    if (dispatch_at(state, 0x0010'0000U, kOutsideTable) != Status::unhandled_syscall) return 41;

    const auto& overflow = state.hle.syscall_census_overflow;
    if (overflow.seen != 1U || overflow.handled != 0U || overflow.refused != 1U) return 42;
    if (state.hle.syscall_census[kOutsideTable % EeHleState::kSyscallCensusNumbers].seen != 0U) {
        return 43;
    }
    return 0;
}

int test_trace_ring_capacity_and_order() {
    EeState state{};
    state.hle.syscall_enabled = true;
    state.hle.syscall_trace_capacity = 4U;

    for (std::uint32_t index = 0U; index < 6U; ++index) {
        const std::uint32_t pc = 0x0010'0100U + (index * 4U);
        const std::uint32_t code = index == 5U ? 0x0000'00FFU : kServicedA;
        if (index < 5U && dispatch_at(state, pc, code) != Status::ok) return 50;
        if (index == 5U && dispatch_at(state, pc, code) != Status::unhandled_syscall) return 51;
    }

    if (state.hle.syscall_trace_count != 4U) return 52;
    if (state.hle.syscall_trace_dropped != 2U) return 53;
    if (state.hle.syscall_count != 6U) return 54;

    // The ring keeps the newest four boundaries in chronological order.
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        const auto& event = state.hle.syscall_trace_event(index);
        const std::uint32_t expected_pc = 0x0010'0108U + (index * 4U);
        if (event.pc != expected_pc) return 55;
        if (event.number != (index == 3U ? 0x0000'00FFU : kServicedA)) return 56;
        if (event.refused != (index == 3U)) return 57;
        if (event.path != (index == 3U ? reps2::runtime::EeSyscallPath::unhandled
                                       : reps2::runtime::EeSyscallPath::host_service)) return 58;
    }
    return 0;
}

int test_disabled_trace_retains_nothing() {
    EeState state{};
    state.hle.syscall_enabled = true;
    state.hle.syscall_trace_capacity = 0U;

    if (dispatch_at(state, 0x0010'0000U, kServicedA) != Status::ok) return 60;
    if (state.hle.syscall_trace_count != 0U) return 61;
    if (state.hle.syscall_trace_dropped != 0U) return 62;
    // The census itself is always maintained when HLE dispatch is enabled.
    if (state.hle.syscall_census[kServicedA].seen != 1U) return 63;
    return 0;
}

} // namespace

int main() {
    if (const auto result = test_service_and_site_attribution(); result != 0) {
        std::fprintf(stderr, "service/site attribution test failed at check %d\n", result);
        return 10 + result;
    }
    if (const auto result = test_sites_sharing_a_service(); result != 0) {
        std::fprintf(stderr, "shared-service site test failed at check %d\n", result);
        return 40 + result;
    }
    if (const auto result = test_negated_i_form_attribution(); result != 0) {
        std::fprintf(stderr, "i-form attribution test failed at check %d\n", result);
        return 70 + result;
    }
    if (const auto result = test_overflow_bucket(); result != 0) {
        std::fprintf(stderr, "overflow-bucket test failed at check %d\n", result);
        return 90 + result;
    }
    if (const auto result = test_trace_ring_capacity_and_order(); result != 0) {
        std::fprintf(stderr, "trace-ring test failed at check %d\n", result);
        return 110 + result;
    }
    if (const auto result = test_disabled_trace_retains_nothing(); result != 0) {
        std::fprintf(stderr, "disabled-trace test failed at check %d\n", result);
        return 130 + result;
    }
    return 0;
}