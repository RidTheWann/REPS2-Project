// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <new>
#include <span>
#include <utility>
#include <vector>

#include "rg_reps2_engine/engine.hpp"
#include "reps2/runtime/ee_hle.hpp"
#include "reps2/runtime/ee_state.hpp"

namespace rg::reps2::engine {

enum class SessionPhase : std::uint8_t {
    empty,
    ready,
    running,
    paused,
    completed,
    stopped
};

enum class SessionError : std::uint8_t {
    none,
    invalid_phase,
    incompatible_engine,
    invalid_instruction_budget,
    invalid_ram_image_size,
    out_of_memory,
    initialization_failed,
    execution_failed
};

struct SessionConfig {
    std::uint64_t instruction_budget{5'000'000U};
    std::span<const std::uint8_t> initial_main_ram{};
    bool enable_ee_syscall_hle{};
    bool enable_instruction_trace{};
    ::reps2::runtime::HostMediaSource media{};
};

struct SessionStartResult {
    SessionError error{SessionError::none};
    EngineError engine_error{EngineError::none};
    ::reps2::runtime::Status initialization_status{::reps2::runtime::Status::ok};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == SessionError::none;
    }
};

enum class RunDisposition : std::uint8_t {
    budget_exhausted,
    completed,
    stopped
};

struct SessionRunResult {
    SessionError error{SessionError::none};
    EngineError engine_error{EngineError::none};
    ::reps2::runtime::Status guest_status{::reps2::runtime::Status::ok};
    RunDisposition disposition{RunDisposition::stopped};
    std::uint64_t budget_supplied{};
    std::uint64_t instructions_before{};
    std::uint64_t instructions_after{};
    std::uint64_t instructions_this_run{};
    std::uint32_t pc_before{};
    std::uint32_t pc_after{};
    bool instruction_counter_monotonic{true};
    bool invoked{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == SessionError::none
            && engine_error == EngineError::none
            && invoked;
    }
};

// One synchronous guest execution context. Each session owns its 32 MiB EE
// main RAM and EeState. It is intentionally non-copyable/non-movable because
// EeState::memory.main_ram is a span into the session's vector.
class EngineSession final {
public:
    EngineSession() = default;
    EngineSession(const EngineSession&) = delete;
    EngineSession& operator=(const EngineSession&) = delete;
    EngineSession(EngineSession&&) = delete;
    EngineSession& operator=(EngineSession&&) = delete;
    ~EngineSession() = default;

    [[nodiscard]] SessionStartResult start(
        const Engine& engine,
        const PreparedNativeModule& module,
        const SessionConfig& config = {}
    ) noexcept {
        if (phase_ != SessionPhase::empty) {
            return {SessionError::invalid_phase, EngineError::none,
                    ::reps2::runtime::Status::ok};
        }
        const EngineError validation = engine.validate(module);
        if (validation != EngineError::none) {
            return {SessionError::incompatible_engine, validation,
                    ::reps2::runtime::Status::ok};
        }
        if (config.instruction_budget == 0U) {
            return {SessionError::invalid_instruction_budget, EngineError::none,
                    ::reps2::runtime::Status::ok};
        }
        if (!config.initial_main_ram.empty()
            && config.initial_main_ram.size() != ::reps2::runtime::kEeMainRamSize) {
            return {SessionError::invalid_ram_image_size, EngineError::none,
                    ::reps2::runtime::Status::ok};
        }

        try {
            main_ram_.assign(::reps2::runtime::kEeMainRamSize, std::uint8_t{0});
        } catch (const std::bad_alloc&) {
            release_resources();
            return {SessionError::out_of_memory, EngineError::none,
                    ::reps2::runtime::Status::ok};
        }

        if (!config.initial_main_ram.empty()) {
            std::copy(config.initial_main_ram.begin(),
                      config.initial_main_ram.end(), main_ram_.begin());
        }

        state_ = {};
        state_.memory.main_ram = std::span<std::uint8_t>(
            main_ram_.data(), main_ram_.size());
        // The media source is host configuration, not HLE observation state, and
        // the generated initializer resets EeHleState without touching it. Attach
        // it before the initializer so boot-time code can read the host's
        // read-only source; otherwise it always observes absent media.
        state_.media = config.media;

        const auto image = std::span<const std::uint8_t>(
            main_ram_.data(), main_ram_.size());
        const std::uint32_t raw_initialization_status = module.initialize_(
            &state_, image.data(), static_cast<std::uint64_t>(image.size()));
        if (raw_initialization_status > static_cast<std::uint32_t>(
                ::reps2::runtime::Status::semaphore_wait_blocked)) {
            release_resources();
            return {SessionError::initialization_failed, EngineError::invalid_module_status,
                    ::reps2::runtime::Status::ok};
        }
        const auto init_status =
            static_cast<::reps2::runtime::Status>(raw_initialization_status);
        if (init_status != ::reps2::runtime::Status::ok) {
            release_resources();
            return {SessionError::initialization_failed, EngineError::none, init_status};
        }

        state_.hle.syscall_enabled = config.enable_ee_syscall_hle;
        if (config.enable_ee_syscall_hle) {
            // The generated initializer resets the device state; establish the
            // same modelled boot environment the --hle-syscalls probe seeds.
            ::reps2::runtime::initialize_hle_boot_environment(state_);
        }
        state_.instruction_trace_enabled = config.enable_instruction_trace;

        engine_ = engine;
        module_ = module;
        instruction_budget_ = config.instruction_budget;
        phase_ = SessionPhase::ready;
        return {SessionError::none, EngineError::none, init_status};
    }

    [[nodiscard]] SessionRunResult run(std::uint64_t budget_override = 0U) noexcept {
        if (phase_ != SessionPhase::ready && phase_ != SessionPhase::paused) {
            SessionRunResult invalid{};
            invalid.error = SessionError::invalid_phase;
            return invalid;
        }
        const std::uint64_t budget =
            budget_override == 0U ? instruction_budget_ : budget_override;
        if (budget == 0U) {
            SessionRunResult invalid{};
            invalid.error = SessionError::invalid_instruction_budget;
            return invalid;
        }

        const std::uint64_t before = state_.instructions_executed;
        const std::uint32_t pc_before = state_.pc;
        phase_ = SessionPhase::running;
        const ExecutionResult execution = engine_.execute(module_, state_, budget);

        SessionRunResult result{};
        result.engine_error = execution.error;
        result.guest_status = execution.guest_status;
        result.budget_supplied = budget;
        result.instructions_before = before;
        result.instructions_after = state_.instructions_executed;
        result.pc_before = pc_before;
        result.pc_after = state_.pc;
        result.invoked = execution.invoked;
        result.instruction_counter_monotonic =
            result.instructions_after >= result.instructions_before;
        result.instructions_this_run = result.instruction_counter_monotonic
            ? result.instructions_after - result.instructions_before
            : 0U;

        if (!execution.ok()) {
            result.error = SessionError::execution_failed;
            result.disposition = RunDisposition::stopped;
            phase_ = SessionPhase::stopped;
            return result;
        }

        if (execution.guest_status == ::reps2::runtime::Status::instruction_budget_exhausted) {
            result.disposition = RunDisposition::budget_exhausted;
            phase_ = SessionPhase::paused;
        } else if (execution.guest_status == ::reps2::runtime::Status::ok) {
            result.disposition = RunDisposition::completed;
            phase_ = SessionPhase::completed;
        } else {
            result.disposition = RunDisposition::stopped;
            phase_ = SessionPhase::stopped;
        }
        return result;
    }

    void close() noexcept {
        release_resources();
        phase_ = SessionPhase::empty;
    }

    [[nodiscard]] SessionPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool active() const noexcept { return phase_ != SessionPhase::empty; }
    [[nodiscard]] std::string_view module_name() const noexcept { return module_.name(); }
    [[nodiscard]] ::reps2::runtime::EeState& state() noexcept { return state_; }
    [[nodiscard]] const ::reps2::runtime::EeState& state() const noexcept { return state_; }
    [[nodiscard]] std::span<std::uint8_t> main_ram() noexcept {
        return std::span<std::uint8_t>(main_ram_.data(), main_ram_.size());
    }
    [[nodiscard]] std::span<const std::uint8_t> main_ram() const noexcept {
        return std::span<const std::uint8_t>(main_ram_.data(), main_ram_.size());
    }

private:
    void release_resources() noexcept {
        state_ = {};
        module_ = {};
        instruction_budget_ = 0U;
        std::vector<std::uint8_t>{}.swap(main_ram_);
    }

    std::vector<std::uint8_t> main_ram_{};
    ::reps2::runtime::EeState state_{};
    Engine engine_{};
    PreparedNativeModule module_{};
    std::uint64_t instruction_budget_{};
    SessionPhase phase_{SessionPhase::empty};
};

} // namespace rg::reps2::engine
