// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the Rust-generated CFG dispatcher through the RG REPS2 ENGINE
// session API. This tests ownership, initializer handoff, bounded execution,
// resumable budget exhaustion, and explicit stop-state reporting.
#include "rg_reps2_engine/rg_reps2_engine.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status initialize_ee_state(
    reps2::runtime::EeState&,
    std::span<const std::uint8_t>
) noexcept;
[[nodiscard]] reps2::runtime::Status execute_cfg(
    reps2::runtime::EeState&,
    std::uint64_t
) noexcept;
}

namespace {

// A two-sector synthetic disc. The callback is host-owned and the runtime only
// forwards whole-sector reads into a destination span.
struct MediaFixture {
    std::array<std::uint8_t, 4096> bytes{};
    std::uint32_t calls{};
    std::uint32_t last_lbn{};
};

bool media_read(
    void* context,
    std::uint32_t lbn,
    std::uint32_t count,
    std::span<std::uint8_t> dest
) noexcept {
    auto* media = static_cast<MediaFixture*>(context);
    ++media->calls;
    media->last_lbn = lbn;
    const auto required = static_cast<std::size_t>(count) * 2048U;
    const auto start = static_cast<std::size_t>(lbn) * 2048U;
    if (dest.size() < required || start + required > media->bytes.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < required; ++index) {
        dest[index] = media->bytes[start + index];
    }
    return true;
}

} // namespace

// A minimal module whose initializer observes the host media source before any
// guest instruction runs. It records what it saw in $a1 (present) and $a2 (read
// succeeded), and copies sector 1 into RAM at 0x4000.
extern "C" std::uint32_t rg_test_initialize_media_module(
    void* opaque_state,
    const std::uint8_t* ram_image,
    std::uint64_t ram_image_size
) noexcept {
    if (opaque_state == nullptr || ram_image == nullptr
        || ram_image_size != reps2::runtime::kEeMainRamSize) {
        return static_cast<std::uint32_t>(reps2::runtime::Status::invalid_memory_image);
    }
    auto& state = *static_cast<reps2::runtime::EeState*>(opaque_state);
    state.pc = 0x2000U;
    state.gpr[5].low = state.media.present() ? 1U : 0U;
    const bool read_ok = state.media.read(
        1U, 1U, state.memory.main_ram.subspan(0x4000U, 2048U));
    state.gpr[6].low = read_ok ? 1U : 0U;
    return static_cast<std::uint32_t>(reps2::runtime::Status::ok);
}

extern "C" std::uint32_t rg_test_execute_media_module(
    void*,
    std::uint64_t
) noexcept {
    return static_cast<std::uint32_t>(reps2::runtime::Status::ok);
}

extern "C" std::uint32_t rg_test_initialize_generated_module(
    void* opaque_state,
    const std::uint8_t* ram_image,
    std::uint64_t ram_image_size
) noexcept {
    if (opaque_state == nullptr || ram_image == nullptr
        || ram_image_size != reps2::runtime::kEeMainRamSize) {
        return static_cast<std::uint32_t>(reps2::runtime::Status::invalid_memory_image);
    }
    return static_cast<std::uint32_t>(reps2::generated::initialize_ee_state(
        *static_cast<reps2::runtime::EeState*>(opaque_state),
        std::span<const std::uint8_t>(
            ram_image, static_cast<std::size_t>(ram_image_size))));
}

extern "C" std::uint32_t rg_test_execute_generated_module(
    void* opaque_state,
    std::uint64_t budget
) noexcept {
    if (opaque_state == nullptr) {
        return static_cast<std::uint32_t>(
            reps2::runtime::Status::unsupported_memory_access);
    }
    return static_cast<std::uint32_t>(reps2::generated::execute_cfg(
        *static_cast<reps2::runtime::EeState*>(opaque_state), budget));
}

namespace {

using rg::reps2::engine::Engine;
using rg::reps2::engine::EngineError;
using rg::reps2::engine::EngineSession;
using rg::reps2::engine::NativeModuleDescriptor;
using rg::reps2::engine::PreparedNativeModule;
using rg::reps2::engine::RunDisposition;
using rg::reps2::engine::SessionConfig;
using rg::reps2::engine::SessionError;
using rg::reps2::engine::SessionPhase;
using reps2::runtime::Status;

int fail(int code, const char* message) {
    std::fprintf(stderr, "RG REPS2 ENGINE session smoke failed (%d): %s\n", code, message);
    return code;
}

int test_module_admission(const Engine& engine, PreparedNativeModule& prepared) {
    constexpr char kModuleName[] = "synthetic-r5900-cfg-module";
    NativeModuleDescriptor descriptor = rg::reps2::engine::make_native_module_descriptor(
        kModuleName,
        static_cast<std::uint32_t>(sizeof(kModuleName) - 1U),
        &rg_test_initialize_generated_module,
        &rg_test_execute_generated_module
    );

    if (engine.prepare(descriptor, prepared) != EngineError::none) {
        return fail(1, "valid generated CFG module was rejected");
    }
    if (!prepared.valid() || prepared.name() != kModuleName) {
        return fail(2, "prepared module identity is invalid");
    }

    auto invalid = descriptor;
    invalid.engine_abi_version += 1U;
    if (engine.prepare(invalid, prepared) != EngineError::engine_abi_mismatch) {
        return fail(3, "engine ABI mismatch was admitted");
    }
    invalid = descriptor;
    invalid.runtime_abi_version += 1U;
    if (engine.prepare(invalid, prepared) != EngineError::runtime_abi_mismatch) {
        return fail(4, "runtime ABI mismatch was admitted");
    }
    invalid = descriptor;
    invalid.toolchain_abi_fingerprint += 1U;
    if (engine.prepare(invalid, prepared) != EngineError::toolchain_abi_mismatch) {
        return fail(5, "toolchain ABI mismatch was admitted");
    }
    invalid = descriptor;
    invalid.initialize = nullptr;
    if (engine.prepare(invalid, prepared) != EngineError::missing_initializer) {
        return fail(6, "module without an initializer was admitted");
    }
    invalid = descriptor;
    invalid.execute = nullptr;
    if (engine.prepare(invalid, prepared) != EngineError::missing_entry_point) {
        return fail(7, "module without an execution entry was admitted");
    }
    invalid = descriptor;
    invalid.struct_size = 0U;
    if (engine.prepare(invalid, prepared) != EngineError::invalid_module_descriptor) {
        return fail(8, "malformed descriptor size was admitted");
    }
    invalid = descriptor;
    invalid.descriptor_abi_version += 1U;
    if (engine.prepare(invalid, prepared) != EngineError::module_descriptor_abi_mismatch) {
        return fail(9, "unsupported descriptor ABI was admitted");
    }
    if (engine.prepare(descriptor, prepared) != EngineError::none || !prepared.valid()) {
        return fail(10, "valid module could not be re-prepared after rejection");
    }
    return 0;
}

int test_session_lifecycle_and_budgeted_execution(
    const Engine& engine,
    const PreparedNativeModule& module
) {
    std::vector<std::uint8_t> initial_ram(reps2::runtime::kEeMainRamSize, 0U);
    initial_ram[0x20U] = 0xA5U;

    SessionConfig config{};
    config.instruction_budget = 1U;
    config.initial_main_ram = std::span<const std::uint8_t>(initial_ram);
    config.enable_ee_syscall_hle = true;

    EngineSession session;
    const auto started = session.start(engine, module, config);
    if (!started.ok()) return fail(20, "valid session failed to initialize");
    if (session.phase() != SessionPhase::ready || !session.active()) {
        return fail(21, "session did not enter ready phase");
    }
    if (session.module_name() != module.name()) return fail(22, "session module identity mismatch");
    if (session.main_ram().size() != reps2::runtime::kEeMainRamSize) {
        return fail(23, "session did not allocate 32 MiB of EE main RAM");
    }
    if (session.main_ram()[0x20U] != 0xA5U) {
        return fail(24, "initial RAM image was not copied into session-owned memory");
    }
    if (session.state().memory.main_ram.data() != session.main_ram().data()
        || session.state().memory.main_ram.size() != session.main_ram().size()) {
        return fail(25, "EeState memory span does not refer to session-owned RAM");
    }
    if (session.state().pc != 0x1000U) return fail(26, "generated initializer did not set the entry PC");
    if (!session.state().hle.syscall_enabled) return fail(27, "session HLE option was not applied");
    if (!session.state().memory.dmac.d_stat_known) {
        return fail(27, "session HLE option did not establish the modelled boot device state");
    }

    session.state().gpr[8].low = 1U;
    session.state().gpr[31].low = 0x1020U;
    const auto paused = session.run();
    if (!paused.ok() || paused.guest_status != Status::instruction_budget_exhausted
        || paused.disposition != RunDisposition::budget_exhausted
        || session.phase() != SessionPhase::paused) {
        return fail(28, "small instruction budget did not pause the guest");
    }
    if (paused.pc_before != 0x1000U || paused.pc_after != 0x1000U) {
        return fail(29, "budget exhaustion changed the guest PC");
    }

    const auto resumed = session.run(64U);
    if (!resumed.ok() || resumed.guest_status != Status::external_control_flow
        || resumed.disposition != RunDisposition::stopped
        || session.phase() != SessionPhase::stopped) {
        return fail(30, "resumed CFG execution did not report external control flow");
    }
    if (resumed.pc_after != 0x1020U || session.state().gpr[9].low != 7U
        || session.state().gpr[10].low != 5U) {
        return fail(31, "resumed generated guest code produced the wrong state");
    }
    if (resumed.instructions_after <= paused.instructions_after
        || !resumed.instruction_counter_monotonic) {
        return fail(32, "session instruction accounting did not advance monotonically");
    }
    if (session.run(64U).error != SessionError::invalid_phase) {
        return fail(33, "terminal session was allowed to execute again");
    }

    session.close();
    if (session.phase() != SessionPhase::empty || session.active()
        || !session.main_ram().empty()) {
        return fail(34, "close did not release the session and owned RAM");
    }
    return 0;
}

int test_invalid_session_inputs(const Engine& engine, const PreparedNativeModule& module) {
    EngineSession session;
    std::vector<std::uint8_t> short_image(32U, 0U);
    SessionConfig config{};
    config.initial_main_ram = std::span<const std::uint8_t>(short_image);
    const auto result = session.start(engine, module, config);
    if (result.error != SessionError::invalid_ram_image_size
        || session.phase() != SessionPhase::empty) {
        return fail(40, "malformed RAM image was not rejected transactionally");
    }

    config.initial_main_ram = {};
    config.instruction_budget = 0U;
    const auto budget_result = session.start(engine, module, config);
    if (budget_result.error != SessionError::invalid_instruction_budget
        || session.phase() != SessionPhase::empty) {
        return fail(41, "zero default instruction budget was not rejected");
    }

    config.instruction_budget = 64U;
    PreparedNativeModule invalid_module{};
    const auto module_result = session.start(engine, invalid_module, config);
    if (module_result.error != SessionError::incompatible_engine
        || module_result.engine_error != EngineError::invalid_prepared_module
        || session.phase() != SessionPhase::empty) {
        return fail(42, "invalid prepared module did not fail closed");
    }

    config.enable_ee_syscall_hle = false;
    EngineSession plain_session;
    const auto plain = plain_session.start(engine, module, config);
    if (!plain.ok()) return fail(43, "session without HLE failed to initialize");
    if (plain_session.state().hle.syscall_enabled) {
        return fail(44, "HLE dispatch was enabled without the session option");
    }
    if (plain_session.state().memory.dmac.d_stat_known) {
        return fail(45, "modelled boot device state was seeded without the HLE option");
    }
    return 0;
}

int test_media_source_is_installed_before_initialization(const Engine& engine) {
    MediaFixture media{};
    for (std::size_t index = 0U; index < media.bytes.size(); ++index) {
        media.bytes[index] = static_cast<std::uint8_t>(index * 3U);
    }

    constexpr char kModuleName[] = "synthetic-media-initializer";
    NativeModuleDescriptor descriptor = rg::reps2::engine::make_native_module_descriptor(
        kModuleName,
        static_cast<std::uint32_t>(sizeof(kModuleName) - 1U),
        &rg_test_initialize_media_module,
        &rg_test_execute_media_module
    );
    PreparedNativeModule module{};
    if (engine.prepare(descriptor, module) != EngineError::none) {
        return fail(50, "media-observing module was rejected");
    }

    SessionConfig config{};
    config.instruction_budget = 8U;
    config.media = ::reps2::runtime::HostMediaSource{&media, &media_read};

    EngineSession session;
    const auto started = session.start(engine, module, config);
    if (!started.ok()) return fail(51, "session with a host media source failed to start");
    if (session.state().gpr[5].low != 1U) {
        return fail(52, "the module initializer observed absent media");
    }
    if (session.state().gpr[6].low != 1U) {
        return fail(53, "the module initializer could not read a sector");
    }
    if (media.calls != 1U || media.last_lbn != 1U) {
        return fail(54, "the host source was not asked for sector 1 once");
    }
    if (session.main_ram()[0x4000U + 3U] != media.bytes[2048U + 3U]) {
        return fail(55, "sector bytes did not reach session-owned RAM");
    }
    if (!session.state().media.present()) {
        return fail(56, "the session lost the media source after initialization");
    }

    const auto ran = session.run();
    if (!ran.ok() || ran.disposition != RunDisposition::completed) {
        return fail(57, "media module did not complete");
    }
    return 0;
}

} // namespace

int main() {
    const Engine engine{};
    PreparedNativeModule module{};
    if (const int result = test_module_admission(engine, module); result != 0) return result;
    if (const int result = test_session_lifecycle_and_budgeted_execution(engine, module);
        result != 0) return result;
    if (const int result = test_invalid_session_inputs(engine, module); result != 0) return result;
    if (const int result = test_media_source_is_installed_before_initialization(engine);
        result != 0) return result;

    std::printf("RG REPS2 ENGINE session, RAM ownership, POD module ABI, and resumable execution passed\n");
    return 0;
}
