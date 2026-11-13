// SPDX-License-Identifier: GPL-3.0-or-later
// PC host backend contracts. These interfaces isolate PS2 runtime behavior
// from concrete PC graphics, audio, input and display APIs.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reps2::host {

enum class GraphicsApi : std::uint8_t {
    Software = 0,
    DirectX12 = 1,
    Vulkan = 2,
};

[[nodiscard]] constexpr const char* graphics_api_name(GraphicsApi api) noexcept {
    switch (api) {
    case GraphicsApi::Software: return "software";
    case GraphicsApi::DirectX12: return "directx12";
    case GraphicsApi::Vulkan: return "vulkan";
    }
    return "unknown";
}

/// Availability is probed, never assumed: a missing driver reports false.
struct GraphicsApiAvailability {
    GraphicsApi api = GraphicsApi::Software;
    bool runtime_present = false;
    std::string reason;
};

[[nodiscard]] GraphicsApiAvailability probe_graphics_api(GraphicsApi api) noexcept;

/// Deterministic headless selection: the first available API in preference
/// order, always falling back to the software reference path.
[[nodiscard]] GraphicsApi select_graphics_api() noexcept;

struct FrameDesc {
    std::uint32_t width = 640;
    std::uint32_t height = 448;
};

class GraphicsBackend {
public:
    virtual ~GraphicsBackend() = default;

    [[nodiscard]] virtual GraphicsApi api() const noexcept = 0;
    [[nodiscard]] virtual const FrameDesc& frame_desc() const noexcept = 0;

    virtual bool begin_frame() noexcept = 0;
    virtual void end_frame() noexcept = 0;
    [[nodiscard]] virtual std::uint64_t frames_presented() const noexcept = 0;
};

/// Reference backend: rasterizes frames into a host RGBA8 buffer. It exists so
/// the runtime can be validated headlessly before a GPU backend is bound.
class SoftwareGraphicsBackend final : public GraphicsBackend {
public:
    explicit SoftwareGraphicsBackend(FrameDesc desc);

    [[nodiscard]] GraphicsApi api() const noexcept override { return GraphicsApi::Software; }
    [[nodiscard]] const FrameDesc& frame_desc() const noexcept override { return desc_; }

    bool begin_frame() noexcept override;
    void end_frame() noexcept override;
    [[nodiscard]] std::uint64_t frames_presented() const noexcept override { return frames_; }

    void clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) noexcept;
    [[nodiscard]] std::uint32_t pixel(std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] const std::uint32_t* pixels() const noexcept { return pixels_.data(); }

private:
    FrameDesc desc_{};
    std::vector<std::uint32_t> pixels_;
    std::uint64_t frames_ = 0;
    bool in_frame_ = false;
};

/// Creates a backend for the requested API. Returns nullptr when that API has
/// no implementation bound yet; callers use select_graphics_api() to fall back.
[[nodiscard]] std::unique_ptr<GraphicsBackend> create_graphics_backend(
    GraphicsApi api, FrameDesc desc);

} // namespace reps2::host
