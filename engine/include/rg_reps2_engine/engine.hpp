// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

#include "rg_reps2_engine/module.hpp"
#include "reps2/runtime/ee_state.hpp"

namespace rg::reps2::engine {

enum class EngineError : std::uint8_t {
    none,
    unsupported_host_target,
    invalid_module_name,
    invalid_module_descriptor,
    module_descriptor_abi_mismatch,
    engine_abi_mismatch,
    runtime_abi_mismatch,
    toolchain_abi_mismatch,
    native_target_mismatch,
    missing_initializer,
    missing_entry_point,
    invalid_prepared_module,
    invalid_instruction_budget,
    invalid_module_status
};

struct ExecutionResult {
    EngineError error{EngineError::none};
    ::reps2::runtime::Status guest_status{::reps2::runtime::Status::ok};
    bool invoked{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == EngineError::none && invoked;
    }
};

// Stateless module admission and coarse-grained native dispatch. Session
// ownership, RAM, and lifecycle are provided by EngineSession.
class Engine final {
public:
    constexpr explicit Engine(NativeTarget host_target = current_native_target()) noexcept
        : host_target_(host_target) {}

    [[nodiscard]] constexpr NativeTarget host_target() const noexcept {
        return host_target_;
    }

    [[nodiscard]] constexpr EngineError validate(
        const PreparedNativeModule& module
    ) const noexcept {
        if (!supported_native_target(host_target_)) {
            return EngineError::unsupported_host_target;
        }
        if (!module.valid()) {
            return EngineError::invalid_prepared_module;
        }
        if (module.engine_abi_version_ != kEngineAbiVersion) {
            return EngineError::engine_abi_mismatch;
        }
        if (module.runtime_abi_version_ != REPS2_RUNTIME_ABI_VERSION) {
            return EngineError::runtime_abi_mismatch;
        }
        if (module.toolchain_abi_fingerprint_ != kToolchainAbiFingerprint) {
            return EngineError::toolchain_abi_mismatch;
        }
        if (module.target_ != host_target_) {
            return EngineError::native_target_mismatch;
        }
        return EngineError::none;
    }

    // Admission is transactional: prepared is unchanged on every failure.
    [[nodiscard]] EngineError prepare(
        const NativeModuleDescriptor& descriptor,
        PreparedNativeModule& prepared
    ) const noexcept {
        if (!supported_native_target(host_target_)) {
            return EngineError::unsupported_host_target;
        }
        if (descriptor.struct_size != sizeof(NativeModuleDescriptor)
            || descriptor.reserved_flags != 0U) {
            return EngineError::invalid_module_descriptor;
        }
        for (const auto value : descriptor.reserved) {
            if (value != 0U) return EngineError::invalid_module_descriptor;
        }
        if (descriptor.descriptor_abi_version != kNativeModuleDescriptorAbiVersion) {
            return EngineError::module_descriptor_abi_mismatch;
        }
        if (descriptor.name == nullptr || descriptor.name_size == 0U
            || descriptor.name_size > kNativeModuleNameMaxSize) {
            return EngineError::invalid_module_name;
        }
        for (std::uint32_t index = 0; index < descriptor.name_size; ++index) {
            const char character = descriptor.name[index];
            const bool alphanumeric = (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9');
            if (!alphanumeric && character != '_' && character != '-' && character != '.') {
                return EngineError::invalid_module_name;
            }
        }
        if (descriptor.engine_abi_version != kEngineAbiVersion) {
            return EngineError::engine_abi_mismatch;
        }
        if (descriptor.runtime_abi_version != REPS2_RUNTIME_ABI_VERSION) {
            return EngineError::runtime_abi_mismatch;
        }
        if (descriptor.toolchain_abi_fingerprint != kToolchainAbiFingerprint) {
            return EngineError::toolchain_abi_mismatch;
        }
        const auto architecture = static_cast<Architecture>(descriptor.target_architecture);
        const auto operating_system =
            static_cast<OperatingSystem>(descriptor.target_operating_system);
        const bool valid_architecture = architecture == Architecture::x86_64
            || architecture == Architecture::arm64;
        const bool valid_operating_system = operating_system == OperatingSystem::windows
            || operating_system == OperatingSystem::android
            || operating_system == OperatingSystem::linux
            || operating_system == OperatingSystem::macos;
        if (!valid_architecture || !valid_operating_system) {
            return EngineError::invalid_module_descriptor;
        }
        const NativeTarget target{architecture, operating_system};
        if (target != host_target_) {
            return EngineError::native_target_mismatch;
        }
        if (descriptor.initialize == nullptr) {
            return EngineError::missing_initializer;
        }
        if (descriptor.execute == nullptr) {
            return EngineError::missing_entry_point;
        }

        PreparedNativeModule candidate{};
        std::copy_n(descriptor.name, descriptor.name_size, candidate.name_.begin());
        candidate.name_[descriptor.name_size] = '\0';
        candidate.name_size_ = descriptor.name_size;
        candidate.engine_abi_version_ = descriptor.engine_abi_version;
        candidate.runtime_abi_version_ = descriptor.runtime_abi_version;
        candidate.toolchain_abi_fingerprint_ = descriptor.toolchain_abi_fingerprint;
        candidate.target_ = target;
        candidate.initialize_ = descriptor.initialize;
        candidate.execute_ = descriptor.execute;
        prepared = candidate;
        return EngineError::none;
    }

    // This call crosses the module ABI once per coarse execution budget. Guest
    // block/instruction dispatch remains inside generated native code.
    [[nodiscard]] ExecutionResult execute(
        const PreparedNativeModule& module,
        ::reps2::runtime::EeState& state,
        std::uint64_t instruction_budget
    ) const noexcept {
        const EngineError validation = validate(module);
        if (validation != EngineError::none) {
            return {validation, ::reps2::runtime::Status::ok, false};
        }
        if (instruction_budget == 0U) {
            return {EngineError::invalid_instruction_budget,
                    ::reps2::runtime::Status::ok, false};
        }
        const std::uint32_t raw_status =
            module.execute_(&state, instruction_budget);
        if (raw_status > static_cast<std::uint32_t>(
                ::reps2::runtime::Status::semaphore_wait_blocked)) {
            return {EngineError::invalid_module_status,
                    ::reps2::runtime::Status::ok, true};
        }
        return {EngineError::none,
                static_cast<::reps2::runtime::Status>(raw_status), true};
    }

private:
    NativeTarget host_target_;
};

} // namespace rg::reps2::engine
