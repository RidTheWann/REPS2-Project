// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/host/graphics.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace reps2::host {

GraphicsApiAvailability probe_graphics_api(GraphicsApi api) noexcept {
    switch (api) {
    case GraphicsApi::Software:
        return {api, true, "reference rasterizer is always available"};
    case GraphicsApi::DirectX12:
#if defined(_WIN32)
        if (::GetModuleHandleW(L"d3d12.dll") != nullptr
            || ::LoadLibraryW(L"d3d12.dll") != nullptr) {
            return {api, true, "d3d12.dll present"};
        }
        return {api, false, "d3d12.dll not found"};
#else
        return {api, false, "DirectX 12 is a Windows-only API"};
#endif
    case GraphicsApi::Vulkan:
#if defined(_WIN32)
        if (::GetModuleHandleW(L"vulkan-1.dll") != nullptr
            || ::LoadLibraryW(L"vulkan-1.dll") != nullptr) {
            return {api, true, "vulkan-1.dll present"};
        }
        return {api, false, "vulkan-1.dll not found"};
#else
        return {api, false, "loader present, no backend bound yet"};
#endif
    }
    return {api, false, "unknown API"};
}

GraphicsApi select_graphics_api() noexcept {
    for (const auto api : {GraphicsApi::DirectX12, GraphicsApi::Vulkan, GraphicsApi::Software}) {
        if (probe_graphics_api(api).runtime_present)
            return api;
    }
    return GraphicsApi::Software;
}

SoftwareGraphicsBackend::SoftwareGraphicsBackend(FrameDesc desc) : desc_(desc) {
    if (desc_.width == 0) desc_.width = 1;
    if (desc_.height == 0) desc_.height = 1;
    pixels_.assign(static_cast<std::size_t>(desc_.width) * desc_.height, 0xFF000000U);
}

bool SoftwareGraphicsBackend::begin_frame() noexcept {
    if (in_frame_) return false;
    in_frame_ = true;
    return true;
}

void SoftwareGraphicsBackend::end_frame() noexcept {
    if (!in_frame_) return;
    in_frame_ = false;
    ++frames_;
}

void SoftwareGraphicsBackend::clear(
    std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) noexcept {
    const auto value = (static_cast<std::uint32_t>(a) << 24U)
        | (static_cast<std::uint32_t>(r) << 16U)
        | (static_cast<std::uint32_t>(g) << 8U)
        | static_cast<std::uint32_t>(b);
    for (auto& pixel : pixels_) pixel = value;
}

std::uint32_t SoftwareGraphicsBackend::pixel(std::uint32_t x, std::uint32_t y) const noexcept {
    if (x >= desc_.width || y >= desc_.height) return 0;
    return pixels_[static_cast<std::size_t>(y) * desc_.width + x];
}

std::unique_ptr<GraphicsBackend> create_graphics_backend(GraphicsApi api, FrameDesc desc) {
    if (api == GraphicsApi::Software)
        return std::make_unique<SoftwareGraphicsBackend>(desc);
    // A GPU backend is only returned once it is actually bound. Reporting
    // availability without an implementation would be a false capability.
    return nullptr;
}

} // namespace reps2::host
