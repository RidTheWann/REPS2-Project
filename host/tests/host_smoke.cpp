// SPDX-License-Identifier: GPL-3.0-or-later
// Headless checks for the PC host backend contracts. No GPU, audio device or
// interactive window is required; the Win32 path is only checked when one can
// be created without showing it.

#include "reps2/host/audio.hpp"
#include "reps2/host/display.hpp"
#include "reps2/host/graphics.hpp"
#include "reps2/host/input.hpp"
#include "reps2/host/platform.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int check_software_graphics() {
    using namespace reps2::host;
    const GraphicsApiAvailability software = probe_graphics_api(GraphicsApi::Software);
    if (!software.runtime_present) return 1;

    const GraphicsApi selected = select_graphics_api();
    if (!probe_graphics_api(selected).runtime_present) return 2;

    auto backend = create_graphics_backend(GraphicsApi::Software, FrameDesc{8, 4});
    if (backend == nullptr) return 3;
    if (backend->frame_desc().width != 8 || backend->frame_desc().height != 4) return 4;
    if (backend->begin_frame() == false) return 5;
    if (backend->begin_frame() == true) return 6; // nested begin rejected
    backend->end_frame();
    if (backend->frames_presented() != 1) return 7;

    auto* software_backend = static_cast<SoftwareGraphicsBackend*>(backend.get());
    software_backend->clear(0x10, 0x20, 0x30, 0xFF);
    const std::uint32_t expected = 0xFF102030U;
    if (software_backend->pixel(0, 0) != expected) return 8;
    if (software_backend->pixel(7, 3) != expected) return 9;
    if (software_backend->pixel(8, 0) != 0) return 10;

    // GPU backends are only constructed once bound; the factory must not fake one.
    if (create_graphics_backend(GraphicsApi::DirectX12, FrameDesc{1, 1}) != nullptr) return 11;
    if (create_graphics_backend(GraphicsApi::Vulkan, FrameDesc{1, 1}) != nullptr) return 12;
    return 0;
}

int check_input_queue() {
    using namespace reps2::host;
    InputQueue queue(2);
    queue.push({InputEventType::KeyDown, KeyCode::Cross, 0});
    queue.push({InputEventType::Character, KeyCode::Unknown, U'x'});
    queue.push({InputEventType::KeyUp, KeyCode::Cross, 0}); // evicts the oldest

    if (queue.size() != 2) return 20;
    InputEvent event{};
    if (!queue.pop(event) || event.type != InputEventType::Character || event.character != U'x') return 21;
    if (!queue.pop(event) || event.type != InputEventType::KeyUp || event.key != KeyCode::Cross) return 22;
    if (queue.pop(event)) return 23;
    return 0;
}

int check_audio_sink() {
    using namespace reps2::host;
    NullAudioSink sink;
    if (sink.open(AudioFormat{0, 2})) return 30; // rejected invalid rate
    if (sink.open(AudioFormat{48'000, 2}) == false) return 31;
    const std::vector<float> samples{0.0f, 0.5f, -0.5f, 1.0f};
    sink.write(samples);
    if (sink.samples_written() != samples.size()) return 32;
    sink.close();
    sink.write(samples);
    if (sink.samples_written() != samples.size()) return 33;
    return 0;
}

int check_display_mapping() {
    using namespace reps2::host;
    const auto ntsc = map_ps2_mode(Ps2VideoMode::Ntsc640x448);
    if (ntsc.width != 1280 || ntsc.height != 896 || ntsc.refresh_hz != 60 || ntsc.scale != 2) return 40;
    const auto pal = map_ps2_mode(Ps2VideoMode::Pal640x512);
    if (pal.width != 1280 || pal.height != 1024 || pal.refresh_hz != 50 || pal.scale != 2) return 41;
    return 0;
}

int check_host_window() {
    using namespace reps2::host;
    auto window = create_host_window();
    if (window == nullptr) return 50;
    if (!window->create(HostWindowDesc{"REPS2 host smoke", 640, 360})) return 51;
    if (window->should_close()) return 52;
    if (!window->pump_once()) return 53;
    window->destroy();
    return 0;
}

} // namespace

int main() {
    for (const auto& [name, result] : std::initializer_list<std::pair<const char*, int>>{
             {"software graphics", check_software_graphics()},
             {"input queue", check_input_queue()},
             {"audio sink", check_audio_sink()},
             {"display mapping", check_display_mapping()},
             {"host window", check_host_window()}}) {
        if (result != 0) {
            std::fprintf(stderr, "host smoke: %s failed (%d)\n", name, result);
            return result;
        }
    }
    std::printf("host smoke: all checks passed\n");
    return 0;
}
