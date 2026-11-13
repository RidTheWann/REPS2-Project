// SPDX-License-Identifier: GPL-3.0-or-later
// Platform windowing contract. Runtime subsystems consume IHostWindow; only
// the host layer knows about Win32 or any other platform API.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace reps2::host {

struct HostWindowDesc {
    std::string title = "REPS2";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
};

class IHostWindow {
public:
    virtual ~IHostWindow() = default;

    [[nodiscard]] virtual bool create(const HostWindowDesc& desc) noexcept = 0;
    virtual void show() noexcept = 0;
    virtual void hide() noexcept = 0;
    /// Processes all pending platform messages; returns false after the user
    /// closed the window.
    [[nodiscard]] virtual bool pump_once() noexcept = 0;
    [[nodiscard]] virtual bool should_close() const noexcept = 0;
    [[nodiscard]] virtual void* native_handle() const noexcept = 0;
    virtual void destroy() noexcept = 0;
};

/// Creates the platform window implementation for the current build target.
[[nodiscard]] std::unique_ptr<IHostWindow> create_host_window();

} // namespace reps2::host
