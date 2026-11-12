// SPDX-License-Identifier: GPL-3.0-or-later
// A tiny ABI fixture that is loaded as a real DLL/SO by the engine loader test.
#include "rg_reps2_engine/module.hpp"
#include "reps2/runtime/ee_state.hpp"

#include <algorithm>
#include <cstdint>

namespace {
constexpr char kModuleName[] = "dynamic-module-fixture";
} // namespace

extern "C" std::uint32_t rg_fixture_initialize(
    void* opaque_state,
    const std::uint8_t* ram_image,
    std::uint64_t ram_image_size
) noexcept {
    if (opaque_state == nullptr || ram_image == nullptr
        || ram_image_size != reps2::runtime::kEeMainRamSize) {
        return static_cast<std::uint32_t>(reps2::runtime::Status::invalid_memory_image);
    }
    auto& state = *static_cast<reps2::runtime::EeState*>(opaque_state);
    state.pc = 0x00100000U;
    state.instructions_executed = 0U;
    return static_cast<std::uint32_t>(reps2::runtime::Status::ok);
}

extern "C" std::uint32_t rg_fixture_execute(
    void* opaque_state,
    std::uint64_t budget
) noexcept {
    if (opaque_state == nullptr || budget == 0U) {
        return static_cast<std::uint32_t>(
            reps2::runtime::Status::unsupported_memory_access);
    }
    auto& state = *static_cast<reps2::runtime::EeState*>(opaque_state);
    const auto charged = std::min<std::uint64_t>(budget, 3U);
    state.instructions_executed += charged;
    state.pc += static_cast<std::uint32_t>(charged * 4U);
    return static_cast<std::uint32_t>(reps2::runtime::Status::external_control_flow);
}

namespace {
const rg::reps2::engine::NativeModuleDescriptor kDescriptor =
    rg::reps2::engine::make_native_module_descriptor(
        kModuleName,
        static_cast<std::uint32_t>(sizeof(kModuleName) - 1U),
        &rg_fixture_initialize,
        &rg_fixture_execute
    );
} // namespace

RG_REPS2_MODULE_EXPORT const rg::reps2::engine::NativeModuleDescriptor*
rg_reps2_get_native_module_descriptor_v1() noexcept {
    return &kDescriptor;
}
