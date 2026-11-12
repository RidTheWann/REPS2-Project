// SPDX-License-Identifier: GPL-3.0-or-later
#include "rg_reps2_engine/module_loader.hpp"

#include <memory>
#include <new>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__ANDROID__) || defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#else
#error "RG REPS2 ENGINE dynamic modules are not implemented for this platform"
#endif

namespace rg::reps2::engine {

namespace detail {

// The native handle is intentionally never passed to FreeLibrary/dlclose.
// Dynamic unloading can invalidate callbacks held by other subsystems and has
// additional TLS/symbol-lifetime hazards on ELF systems. A future unload design
// must first add quiescence and module-owned thread/TLS lifecycle contracts.
class DynamicLibrary final {
public:
    explicit DynamicLibrary(void* handle) noexcept : handle_(handle) {}
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    [[nodiscard]] void* handle() const noexcept { return handle_; }

private:
    void* handle_{};
};

} // namespace detail

ModuleLoadResult NativeModuleLoader::load(
    const Engine& engine,
    const std::filesystem::path& path,
    PreparedNativeModule& prepared
) noexcept {
    if (path.empty()) {
        return {ModuleLoadError::empty_path, EngineError::none, 0U};
    }

    std::error_code filesystem_error{};
    const auto absolute_path = std::filesystem::canonical(path, filesystem_error);
    if (filesystem_error) {
        return {ModuleLoadError::path_resolution_failed, EngineError::none,
                static_cast<std::uint32_t>(filesystem_error.value())};
    }
    if (!std::filesystem::is_regular_file(absolute_path, filesystem_error)
        || filesystem_error) {
        return {ModuleLoadError::not_a_regular_file, EngineError::none,
                static_cast<std::uint32_t>(filesystem_error.value())};
    }

    void* native_handle = nullptr;
#if defined(_WIN32)
    native_handle = static_cast<void*>(LoadLibraryExW(
        absolute_path.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
    ));
    if (native_handle == nullptr) {
        return {ModuleLoadError::library_load_failed, EngineError::none,
                static_cast<std::uint32_t>(GetLastError())};
    }
    const auto symbol = GetProcAddress(
        static_cast<HMODULE>(native_handle),
        kNativeModuleDescriptorSymbol
    );
    if (symbol == nullptr) {
        return {ModuleLoadError::descriptor_symbol_not_found, EngineError::none,
                static_cast<std::uint32_t>(GetLastError())};
    }
    const auto entry = reinterpret_cast<NativeModuleDescriptorEntry>(symbol);
#elif defined(__ANDROID__) || defined(__linux__) || defined(__APPLE__)
    native_handle = dlopen(absolute_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (native_handle == nullptr) {
        return {ModuleLoadError::library_load_failed, EngineError::none, 0U};
    }
    (void)dlerror();
    void* symbol = dlsym(native_handle, kNativeModuleDescriptorSymbol);
    const char* symbol_error = dlerror();
    if (symbol_error != nullptr || symbol == nullptr) {
        return {ModuleLoadError::descriptor_symbol_not_found, EngineError::none, 0U};
    }
    const auto entry = reinterpret_cast<NativeModuleDescriptorEntry>(symbol);
#else
    (void)engine;
    (void)prepared;
    return {ModuleLoadError::unsupported_platform, EngineError::none, 0U};
#endif

    std::shared_ptr<detail::DynamicLibrary> library{};
    try {
        library = std::make_shared<detail::DynamicLibrary>(native_handle);
    } catch (const std::bad_alloc&) {
        // The OS module remains pinned intentionally; see DynamicLibrary.
        return {ModuleLoadError::out_of_memory, EngineError::none, 0U};
    }

    const NativeModuleDescriptor* descriptor = entry();
    if (descriptor == nullptr) {
        return {ModuleLoadError::null_descriptor, EngineError::none, 0U};
    }

    PreparedNativeModule candidate{};
    const EngineError admission = engine.prepare(*descriptor, candidate);
    if (admission != EngineError::none) {
        const auto load_error = admission == EngineError::invalid_module_descriptor
            ? ModuleLoadError::invalid_descriptor
            : ModuleLoadError::engine_rejected_module;
        return {load_error, admission, 0U};
    }

    candidate.library_ = std::move(library);
    candidate.dynamically_loaded_ = true;
    prepared = std::move(candidate);
    return {};
}

} // namespace rg::reps2::engine
