// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/host/display.hpp"

namespace reps2::host {

HostDisplayMode map_ps2_mode(Ps2VideoMode mode) noexcept {
    switch (mode) {
    case Ps2VideoMode::Ntsc640x448:
        return {1280, 896, 60, 2};
    case Ps2VideoMode::Pal640x512:
        return {1280, 1024, 50, 2};
    case Ps2VideoMode::Ntsc640x480:
        return {1280, 960, 60, 2};
    case Ps2VideoMode::Pal720x576:
        return {1440, 1152, 50, 2};
    }
    return {1280, 896, 60, 2};
}

} // namespace reps2::host
