// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>

#include "rg_reps2_engine/engine.hpp"

namespace rg::reps2::engine {

enum class ModuleLoadError : std::uint8_t {
    none,
    empty_path,
    path_resolution_failed,
    not_a_regular_file,
    unsupported_platform,
    library_load_failed,
    descriptor_symbol_not_found,
    null_descriptor,
    invalid_descriptor,
    engine_rejected_module,
    out_of_memory
};

struct ModuleLoadResult {
    ModuleLoadError error{ModuleLoadError::none};
    EngineError engine_error{EngineError::none};
    std::uint32_t platform_error{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == ModuleLoadError::none && engine_error == EngineError::none;
    }
};

// Loads a trusted native module from an explicit file path. Libraries are
// intentionally pinned for process lifetime; hot-unload is not supported.
class NativeModuleLoader final {
public:
    [[nodiscard]] static ModuleLoadResult load(
        const Engine& engine,
        const std::filesystem::path& path,
        PreparedNativeModule& prepared
    ) noexcept;
};

[[nodiscard]] constexpr const char* module_load_error_name(ModuleLoadError error) noexcept {
    switch (error) {
    case ModuleLoadError::none: return "none";
    case ModuleLoadError::empty_path: return "empty_path";
    case ModuleLoadError::path_resolution_failed: return "path_resolution_failed";
    case ModuleLoadError::not_a_regular_file: return "not_a_regular_file";
    case ModuleLoadError::unsupported_platform: return "unsupported_platform";
    case ModuleLoadError::library_load_failed: return "library_load_failed";
    case ModuleLoadError::descriptor_symbol_not_found: return "descriptor_symbol_not_found";
    case ModuleLoadError::null_descriptor: return "null_descriptor";
    case ModuleLoadError::invalid_descriptor: return "invalid_descriptor";
    case ModuleLoadError::engine_rejected_module: return "engine_rejected_module";
    case ModuleLoadError::out_of_memory: return "out_of_memory";
    }
    return "unknown";
}

} // namespace rg::reps2::engine
