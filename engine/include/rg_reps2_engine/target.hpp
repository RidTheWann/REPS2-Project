// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string_view>
namespace rg::reps2::engine {
enum class Architecture : std::uint8_t { unknown, x86_64, arm64 };
enum class OperatingSystem : std::uint8_t { unknown, windows, android, linux, macos };
struct NativeTarget {
    Architecture architecture{Architecture::unknown};
    OperatingSystem operating_system{OperatingSystem::unknown};
    [[nodiscard]] constexpr bool known() const noexcept {
        return architecture != Architecture::unknown && operating_system != OperatingSystem::unknown;
    }
    friend constexpr bool operator==(NativeTarget, NativeTarget) noexcept = default;
};
[[nodiscard]] constexpr std::string_view architecture_name(Architecture architecture) noexcept {
    switch (architecture) {
    case Architecture::x86_64: return "x86_64";
    case Architecture::arm64: return "arm64";
    case Architecture::unknown: return "unknown";
    }
    return "unknown";
}
[[nodiscard]] constexpr std::string_view operating_system_name(OperatingSystem operating_system) noexcept {
    switch (operating_system) {
    case OperatingSystem::windows: return "windows";
    case OperatingSystem::android: return "android";
    case OperatingSystem::linux: return "linux";
    case OperatingSystem::macos: return "macos";
    case OperatingSystem::unknown: return "unknown";
    }
    return "unknown";
}
// Detects the binary currently being compiled; this is not cross-compilation.
[[nodiscard]] constexpr NativeTarget current_native_target() noexcept {
    Architecture architecture = Architecture::unknown;
#if defined(_M_X64) || defined(__x86_64__)
    architecture = Architecture::x86_64;
#elif defined(_M_ARM64) || defined(__aarch64__)
    architecture = Architecture::arm64;
#endif
    OperatingSystem operating_system = OperatingSystem::unknown;
#if defined(__ANDROID__)
    operating_system = OperatingSystem::android;
#elif defined(_WIN32)
    operating_system = OperatingSystem::windows;
#elif defined(__APPLE__)
    operating_system = OperatingSystem::macos;
#elif defined(__linux__)
    operating_system = OperatingSystem::linux;
#endif
    return {architecture, operating_system};
}
[[nodiscard]] constexpr bool supported_native_target(NativeTarget target) noexcept {
    return target.known();
}
} // namespace rg::reps2::engine
