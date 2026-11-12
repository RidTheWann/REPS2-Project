// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "reps2/runtime/ee_state.hpp"

#define RG_REPS2_STRINGIZE_DETAIL(value) #value
#define RG_REPS2_STRINGIZE(value) RG_REPS2_STRINGIZE_DETAIL(value)

#if defined(_MSC_VER)
#define RG_REPS2_COMPILER_ABI_TAG "msvc-" RG_REPS2_STRINGIZE(_MSC_FULL_VER)
#elif defined(__clang__)
#define RG_REPS2_COMPILER_ABI_TAG "clang-" __clang_version__
#elif defined(__GNUC__)
#define RG_REPS2_COMPILER_ABI_TAG "gcc-" __VERSION__
#else
#define RG_REPS2_COMPILER_ABI_TAG "unknown-compiler"
#endif

#if defined(_MSVC_STL_VERSION)
#define RG_REPS2_STDLIB_ABI_TAG "msvc-stl-" RG_REPS2_STRINGIZE(_MSVC_STL_VERSION)
#elif defined(_LIBCPP_VERSION)
#define RG_REPS2_STDLIB_ABI_TAG "libcxx-" RG_REPS2_STRINGIZE(_LIBCPP_VERSION)
#elif defined(__GLIBCXX__)
#define RG_REPS2_STDLIB_ABI_TAG "libstdcxx-" RG_REPS2_STRINGIZE(__GLIBCXX__)
#else
#define RG_REPS2_STDLIB_ABI_TAG "unknown-stdlib"
#endif

#if defined(_GLIBCXX_USE_CXX11_ABI)
#define RG_REPS2_CXX11_ABI_TAG RG_REPS2_STRINGIZE(_GLIBCXX_USE_CXX11_ABI)
#else
#define RG_REPS2_CXX11_ABI_TAG "na"
#endif

#if defined(_ITERATOR_DEBUG_LEVEL)
#define RG_REPS2_ITERATOR_ABI_TAG RG_REPS2_STRINGIZE(_ITERATOR_DEBUG_LEVEL)
#else
#define RG_REPS2_ITERATOR_ABI_TAG "na"
#endif

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#define RG_REPS2_EXCEPTIONS_ABI_TAG "exceptions-on"
#else
#define RG_REPS2_EXCEPTIONS_ABI_TAG "exceptions-off"
#endif

#if defined(__GXX_RTTI) || defined(_CPPRTTI)
#define RG_REPS2_RTTI_ABI_TAG "rtti-on"
#else
#define RG_REPS2_RTTI_ABI_TAG "rtti-off"
#endif

#if defined(_MSVC_LANG)
#define RG_REPS2_CPP_LANG_VALUE _MSVC_LANG
#else
#define RG_REPS2_CPP_LANG_VALUE __cplusplus
#endif

namespace rg::reps2::engine {

namespace detail {
[[nodiscard]] constexpr std::uint64_t fnv1a_64(std::string_view value) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] constexpr std::uint64_t mix_abi_value(
    std::uint64_t hash,
    std::uint64_t value
) noexcept {
    for (unsigned byte = 0; byte < 8U; ++byte) {
        hash ^= value & 0xFFU;
        hash *= 1099511628211ULL;
        value >>= 8U;
    }
    return hash;
}
} // namespace detail

inline constexpr std::string_view kToolchainAbiTag{
    "compiler=" RG_REPS2_COMPILER_ABI_TAG
    ";stdlib=" RG_REPS2_STDLIB_ABI_TAG
    ";cxx11abi=" RG_REPS2_CXX11_ABI_TAG
    ";iterator=" RG_REPS2_ITERATOR_ABI_TAG
    ";exceptions=" RG_REPS2_EXCEPTIONS_ABI_TAG
    ";rtti=" RG_REPS2_RTTI_ABI_TAG
    ";language=" RG_REPS2_STRINGIZE(RG_REPS2_CPP_LANG_VALUE)
};

// The module callbacks receive EeState as an opaque pointer. These layout
// sentinels make an accidental compiler/STL/runtime mismatch fail admission.
// REPS2_RUNTIME_ABI_VERSION remains the authoritative semantic ABI version.
inline constexpr std::uint64_t kToolchainAbiFingerprint = []() constexpr {
    auto hash = detail::fnv1a_64(kToolchainAbiTag);
    hash = detail::mix_abi_value(hash, sizeof(void*));
    hash = detail::mix_abi_value(hash, sizeof(::reps2::runtime::EeState));
    hash = detail::mix_abi_value(hash, alignof(::reps2::runtime::EeState));
    hash = detail::mix_abi_value(hash, sizeof(::reps2::runtime::EeMemory));
    hash = detail::mix_abi_value(hash, sizeof(::reps2::runtime::Status));
    hash = detail::mix_abi_value(hash, sizeof(std::span<std::uint8_t>));
    return hash;
}();

} // namespace rg::reps2::engine

#undef RG_REPS2_STRINGIZE_DETAIL
#undef RG_REPS2_STRINGIZE
#undef RG_REPS2_COMPILER_ABI_TAG
#undef RG_REPS2_STDLIB_ABI_TAG
#undef RG_REPS2_CXX11_ABI_TAG
#undef RG_REPS2_ITERATOR_ABI_TAG
#undef RG_REPS2_EXCEPTIONS_ABI_TAG
#undef RG_REPS2_RTTI_ABI_TAG
#undef RG_REPS2_CPP_LANG_VALUE
