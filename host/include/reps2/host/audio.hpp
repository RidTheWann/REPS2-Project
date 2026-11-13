// SPDX-License-Identifier: GPL-3.0-or-later
// Audio host contracts. The runtime produces interleaved float samples; a host
// sink owns device timing and buffering.

#pragma once

#include <cstdint>
#include <span>

namespace reps2::host {

struct AudioFormat {
    std::uint32_t sample_rate = 48'000;
    std::uint16_t channels = 2;
};

class AudioSink {
public:
    virtual ~AudioSink() = default;

    [[nodiscard]] virtual AudioFormat format() const noexcept = 0;
    virtual bool open(AudioFormat format) noexcept = 0;
    virtual void close() noexcept = 0;
    virtual void write(std::span<const float> interleaved_samples) noexcept = 0;
    [[nodiscard]] virtual std::uint64_t samples_written() const noexcept = 0;
};

/// Headless sink used by tests and by profiles without an audio device.
class NullAudioSink final : public AudioSink {
public:
    [[nodiscard]] AudioFormat format() const noexcept override { return format_; }
    bool open(AudioFormat format) noexcept override;
    void close() noexcept override { opened_ = false; }
    void write(std::span<const float> interleaved_samples) noexcept override;
    [[nodiscard]] std::uint64_t samples_written() const noexcept override { return samples_; }

    [[nodiscard]] bool is_open() const noexcept { return opened_; }

private:
    AudioFormat format_{};
    std::uint64_t samples_ = 0;
    bool opened_ = false;
};

} // namespace reps2::host
