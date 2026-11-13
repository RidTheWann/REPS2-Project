// SPDX-License-Identifier: GPL-3.0-or-later
// PS2 video mode to host presentation mapping.

#pragma once

#include <cstdint>

namespace reps2::host {

/// Guest-visible PS2 video modes REPS2 knows how to present.
enum class Ps2VideoMode : std::uint8_t {
    Ntsc640x448 = 0,
    Pal640x512 = 1,
    Ntsc640x480 = 2,
    Pal720x576 = 3,
};

struct HostDisplayMode {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t refresh_hz = 0;
    /// Integer presentation scale relative to the guest framebuffer.
    std::uint32_t scale = 1;
};

/// Deterministic presentation mapping: the guest framebuffer stays unmodified
/// and the host scales it by an integer factor for a crisp picture.
[[nodiscard]] HostDisplayMode map_ps2_mode(Ps2VideoMode mode) noexcept;

} // namespace reps2::host
