// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/host/input.hpp"

namespace reps2::host {

void InputQueue::push(const InputEvent& event) noexcept {
    if (events_.size() >= capacity_)
        events_.pop_front();
    events_.push_back(event);
}

bool InputQueue::pop(InputEvent& out) noexcept {
    if (events_.empty()) return false;
    out = events_.front();
    events_.pop_front();
    return true;
}

} // namespace reps2::host
