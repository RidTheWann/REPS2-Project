// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>

#include "rg_reps2_engine/target.hpp"
#include "rg_reps2_engine/toolchain_abi.hpp"
#include "rg_reps2_engine/version.hpp"
#include "reps2/runtime/abi_version.hpp"

namespace rg::reps2::engine {

inline constexpr std::uint32_t kNativeModuleDescriptorAbiVersion{1U};
inline constexpr std::uint32_t kNativeModuleNameMaxSize{96U};
inline constexpr char kNativeModuleDescriptorSymbol[] =
    "rg_reps2_get_native_module_descriptor_v1";

struct NativeModuleDescriptor;

// Keep callback function types at C language linkage. The opaque EeState pointer
// is interpreted only by a module built against the matching runtime ABI.
extern "C" {
using NativeModuleInitialize = std::uint32_t (*)(
    void* guest_state,
    const std::uint8_t* ram_image,
    std::uint64_t ram_image_size
) noexcept;
using NativeModuleExecute = std::uint32_t (*)(
    void* guest_state,
    std::uint64_t instruction_budget
) noexcept;
using NativeModuleDescriptorEntry = const NativeModuleDescriptor* (*)() noexcept;
}

// POD descriptor returned by the exported C symbol. Do not add STL objects,
// references, exceptions, virtual functions, or ownership-bearing C++ types.
struct NativeModuleDescriptor final {
    std::uint32_t struct_size{};
    std::uint32_t descriptor_abi_version{kNativeModuleDescriptorAbiVersion};
    const char* name{};
    std::uint32_t name_size{};
    std::uint32_t engine_abi_version{kEngineAbiVersion};
    std::uint32_t runtime_abi_version{REPS2_RUNTIME_ABI_VERSION};
    std::uint32_t reserved_flags{};
    std::uint64_t toolchain_abi_fingerprint{kToolchainAbiFingerprint};
    std::uint8_t target_architecture{
        static_cast<std::uint8_t>(current_native_target().architecture)};
    std::uint8_t target_operating_system{
        static_cast<std::uint8_t>(current_native_target().operating_system)};
    std::uint8_t reserved[6]{};
    NativeModuleInitialize initialize{};
    NativeModuleExecute execute{};
};

static_assert(std::is_standard_layout_v<NativeModuleDescriptor>);
static_assert(std::is_trivially_copyable_v<NativeModuleDescriptor>);

[[nodiscard]] constexpr NativeModuleDescriptor make_native_module_descriptor(
    const char* name,
    std::uint32_t name_size,
    NativeModuleInitialize initialize,
    NativeModuleExecute execute,
    NativeTarget target = current_native_target()
) noexcept {
    NativeModuleDescriptor descriptor{};
    descriptor.struct_size = static_cast<std::uint32_t>(sizeof(NativeModuleDescriptor));
    descriptor.name = name;
    descriptor.name_size = name_size;
    descriptor.target_architecture = static_cast<std::uint8_t>(target.architecture);
    descriptor.target_operating_system = static_cast<std::uint8_t>(target.operating_system);
    descriptor.initialize = initialize;
    descriptor.execute = execute;
    return descriptor;
}

namespace detail {
class DynamicLibrary;
}

class Engine;
class NativeModuleLoader;
class EngineSession;

#if defined(_WIN32)
#define RG_REPS2_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define RG_REPS2_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Host-owned prepared handle. Dynamic-library ownership is retained by this
// value and copied into EngineSession; static modules remain caller-owned.
class PreparedNativeModule final {
public:
    PreparedNativeModule() = default;

    [[nodiscard]] bool valid() const noexcept {
        return name_size_ != 0U && name_size_ <= kNativeModuleNameMaxSize
            && initialize_ != nullptr && execute_ != nullptr;
    }
    [[nodiscard]] std::string_view name() const noexcept {
        return std::string_view{name_.data(), name_size_};
    }
    [[nodiscard]] NativeTarget target() const noexcept { return target_; }
    [[nodiscard]] std::uint64_t toolchain_abi_fingerprint() const noexcept {
        return toolchain_abi_fingerprint_;
    }
    [[nodiscard]] bool dynamically_loaded() const noexcept { return dynamically_loaded_; }

private:
    std::array<char, kNativeModuleNameMaxSize + 1U> name_{};
    std::uint32_t name_size_{};
    std::uint32_t engine_abi_version_{};
    std::uint32_t runtime_abi_version_{};
    std::uint64_t toolchain_abi_fingerprint_{};
    NativeTarget target_{};
    NativeModuleInitialize initialize_{};
    NativeModuleExecute execute_{};
    std::shared_ptr<const detail::DynamicLibrary> library_{};
    bool dynamically_loaded_{};

    friend class Engine;
    friend class NativeModuleLoader;
    friend class EngineSession;
};

} // namespace rg::reps2::engine
