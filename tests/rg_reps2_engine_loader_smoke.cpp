// SPDX-License-Identifier: GPL-3.0-or-later
#include "rg_reps2_engine/rg_reps2_engine.hpp"

#include <cstdio>
#include <filesystem>
#include <string_view>

namespace {

using namespace rg::reps2::engine;
using reps2::runtime::Status;

int fail(int code, const char* message) {
    std::fprintf(stderr, "RG REPS2 ENGINE loader smoke failed (%d): %s\n", code, message);
    return code;
}

} // namespace

int main(int argc, char** argv) {
    const bool generated_module = argc == 3 && std::string_view{argv[2]} == "--generated";
    if (argc != 3) {
        return fail(1, "expected a valid module path and either a missing-symbol fixture or --generated");
    }

    const Engine engine{};
    PreparedNativeModule prepared{};

    const auto loaded = NativeModuleLoader::load(engine, std::filesystem::path(argv[1]), prepared);
    if (!loaded.ok() || !prepared.valid() || !prepared.dynamically_loaded()) {
        return fail(2, "valid dynamic module was not admitted");
    }
    if (prepared.name().empty()) {
        return fail(3, "dynamic module did not provide a valid copied name");
    }
    if (!generated_module && prepared.name() != "dynamic-module-fixture") {
        return fail(4, "dynamic fixture module name was not copied into host-owned storage");
    }

    if (!generated_module) {
        const auto bad_symbol = NativeModuleLoader::load(
            engine, std::filesystem::path(argv[2]), prepared);
        if (bad_symbol.error != ModuleLoadError::descriptor_symbol_not_found
            || !prepared.valid() || prepared.name() != "dynamic-module-fixture") {
            return fail(5, "failed load modified the previously prepared module");
        }

        std::filesystem::path missing_path{argv[1]};
        missing_path += ".missing";
        const auto missing = NativeModuleLoader::load(engine, missing_path, prepared);
        if (missing.error != ModuleLoadError::path_resolution_failed || !prepared.valid()) {
            return fail(6, "nonexistent module path did not fail transactionally");
        }
    }

    EngineSession session{};
    SessionConfig config{};
    config.instruction_budget = generated_module ? 64U : 8U;
    const auto started = session.start(engine, prepared, config);
    if (!started.ok() || session.phase() != SessionPhase::ready) {
        return fail(7, "session could not retain a loaded native module");
    }

    // The session must keep its module entry points alive after the caller
    // drops its own prepared-module handle.
    prepared = {};
    const auto run = session.run();
    if (!run.ok() || !run.instruction_counter_monotonic) {
        return fail(8, "session did not execute the loaded module after handle release");
    }
    if (generated_module) {
        if (run.instructions_this_run == 0U
            && run.guest_status != Status::instruction_budget_exhausted) {
            return fail(9, "generated AOT module returned without executing guest code");
        }
    } else if (run.guest_status != Status::external_control_flow
        || run.pc_before != 0x00100000U || run.pc_after != 0x0010000CU
        || run.instructions_this_run != 3U) {
        return fail(10, "dynamic fixture execution result mismatch");
    }

    session.close();
    if (session.phase() != SessionPhase::empty) {
        return fail(11, "session did not close after dynamic execution");
    }

    std::puts(generated_module
        ? "RG REPS2 ENGINE generated AOT module load and execution passed"
        : "RG REPS2 ENGINE dynamic module loading, ABI admission, and session lifetime passed");
    return 0;
}
