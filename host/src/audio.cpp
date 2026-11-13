// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/host/audio.hpp"

namespace reps2::host {

bool NullAudioSink::open(AudioFormat format) noexcept {
    if (format.sample_rate == 0 || format.channels == 0) return false;
    format_ = format;
    opened_ = true;
    return true;
}

void NullAudioSink::write(std::span<const float> interleaved_samples) noexcept {
    if (!opened_) return;
    samples_ += interleaved_samples.size();
}

} // namespace reps2::host
