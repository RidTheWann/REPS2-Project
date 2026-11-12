// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace rg::reps2::engine {

inline constexpr std::string_view kEngineName{"RG REPS2 ENGINE"};

struct Version {
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t patch;
};

inline constexpr Version kEngineVersion{0U, 2U, 0U};
// Bumped because the prepared-module and callback ABI changed to the v1 POD
// descriptor and opaque-state C ABI.
inline constexpr std::uint32_t kEngineAbiVersion{2U};

} // namespace rg::reps2::engine
