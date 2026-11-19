// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native module fixture for the run-report census section (ADR 0170). Its
// execute entry dispatches three EE syscall boundaries from two guest PCs: two
// serviced calls at one site and one unknown number at another. The runner test
// then checks the JSON census against those known boundaries.
#include "rg_reps2_engine/module.hpp"
#include "reps2/runtime/ee_hle.hpp"
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>

namespace {
constexpr char kModuleName[] = "census-report-fixture";
constexpr std::uint32_t kServicedSite = 0x0010'0C00U;
constexpr std::uint32_t kRefusedSite = 0x0010'0C10U;

std::uint32_t first_unserved_number() noexcept {
    for (std::uint32_t number = 0U;
         number < reps2::runtime::EeHleState::kSyscallCensusNumbers; ++number) {
        if (reps2::runtime::find_ee_syscall_service(number) == nullptr) {
            return number;
        }
    }
    return 0U;
}

reps2::runtime::Status dispatch_at(
    reps2::runtime::EeState& state,
    std::uint32_t pc,
    std::uint32_t code
) noexcept {
    state.pc = pc;
    state.gpr[reps2::runtime::kEeSyscallNumberRegister].low = code;
    return reps2::runtime::dispatch_ee_syscall(state, pc + 4U);
}
} // namespace

extern "C" std::uint32_t rg_census_initialize(
    void* opaque_state,
    const std::uint8_t* ram_image,
    std::uint64_t ram_image_size
) noexcept {
    if (opaque_state == nullptr || ram_image == nullptr
        || ram_image_size != reps2::runtime::kEeMainRamSize) {
        return static_cast<std::uint32_t>(reps2::runtime::Status::invalid_memory_image);
    }
    auto& state = *static_cast<reps2::runtime::EeState*>(opaque_state);
    state.pc = kServicedSite;
    state.instructions_executed = 0U;
    return static_cast<std::uint32_t>(reps2::runtime::Status::ok);
}

extern "C" std::uint32_t rg_census_execute(
    void* opaque_state,
    std::uint64_t budget
) noexcept {
    if (opaque_state == nullptr || budget < 3U) {
        return static_cast<std::uint32_t>(
            reps2::runtime::Status::unsupported_memory_access);
    }
    auto& state = *static_cast<reps2::runtime::EeState*>(opaque_state);
    (void)dispatch_at(state, kServicedSite, reps2::runtime::kEeSyscallGetThreadId);
    (void)dispatch_at(state, kServicedSite, reps2::runtime::kEeSyscallGetThreadId);
    (void)dispatch_at(state, kRefusedSite, first_unserved_number());
    state.instructions_executed += 3U;
    return static_cast<std::uint32_t>(reps2::runtime::Status::external_control_flow);
}

namespace {
const rg::reps2::engine::NativeModuleDescriptor kDescriptor =
    rg::reps2::engine::make_native_module_descriptor(
        kModuleName,
        static_cast<std::uint32_t>(sizeof(kModuleName) - 1U),
        &rg_census_initialize,
        &rg_census_execute
    );
} // namespace

RG_REPS2_MODULE_EXPORT const rg::reps2::engine::NativeModuleDescriptor*
rg_reps2_get_native_module_descriptor_v1() noexcept {
    return &kDescriptor;
}