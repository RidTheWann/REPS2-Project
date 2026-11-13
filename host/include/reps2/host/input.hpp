// SPDX-License-Identifier: GPL-3.0-or-later
// Keyboard and pointer input contracts for the PC host backend.

#pragma once

#include <cstdint>
#include <deque>

namespace reps2::host {

enum class InputEventType : std::uint8_t { KeyDown, KeyUp, Character };

/// Portable key identifiers. A platform backend maps its native codes onto
/// these values so runtime-side code never sees a Win32 virtual key.
enum class KeyCode : std::uint16_t {
    Unknown = 0,
    Up, Down, Left, Right,
    Triangle, Circle, Cross, Square,
    Start, Select,
    L1, R1, L2, R2, L3, R3,
    Escape, F1,
};

struct InputEvent {
    InputEventType type = InputEventType::KeyDown;
    KeyCode key = KeyCode::Unknown;
    char32_t character = 0;
};

/// Bounded FIFO. A full queue drops the oldest event instead of growing
/// without limit, which keeps a stalled consumer from exhausting memory.
class InputQueue {
public:
    explicit InputQueue(std::size_t capacity = 64) : capacity_(capacity == 0 ? 1 : capacity) {}

    void push(const InputEvent& event) noexcept;
    [[nodiscard]] bool pop(InputEvent& out) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }
    [[nodiscard]] bool empty() const noexcept { return events_.empty(); }
    void clear() noexcept { events_.clear(); }

private:
    std::size_t capacity_;
    std::deque<InputEvent> events_;
};

} // namespace reps2::host
