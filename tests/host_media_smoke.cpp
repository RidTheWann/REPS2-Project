// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host media boundary smoke test: the runtime never opens files, so a disc
// device can only serve I/O when the host installed a read-only sector source.
// These cases pin the boundary rules: absence fails closed, a valid range is
// forwarded verbatim, an empty read forwards nothing, and an overflowing or
// out-of-range request is refused before the host source is touched.
#include "reps2/runtime/ee_state.hpp"
#include "reps2/runtime/host_media.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

using reps2::runtime::HostMediaSource;
using reps2::runtime::kHostMediaSectorAddressLimit;

constexpr std::uint32_t kSectorSize = 2048U;
constexpr std::uint32_t kTestSectors = 8U;

struct FakeMedia {
    std::array<std::uint8_t, kSectorSize * kTestSectors> bytes{};
    std::uint32_t calls{};
    std::uint32_t last_lbn{};
    std::uint32_t last_count{};
    std::size_t last_dest_size{};
    bool fail{};
};

bool read_sectors(
    void* context,
    std::uint32_t lbn,
    std::uint32_t count,
    std::span<std::uint8_t> dest
) noexcept {
    auto* media = static_cast<FakeMedia*>(context);
    ++media->calls;
    media->last_lbn = lbn;
    media->last_count = count;
    media->last_dest_size = dest.size();
    if (media->fail) {
        return false;
    }
    if (dest.size() < count * kSectorSize) {
        return false;
    }
    // A host source refuses anything outside its own image.
    if (static_cast<std::uint64_t>(lbn) + count > kTestSectors) {
        return false;
    }
    for (std::uint32_t i = 0U; i < count * kSectorSize; ++i) {
        dest[static_cast<std::size_t>(i)] =
            media->bytes[(static_cast<std::size_t>(lbn) * kSectorSize) + i];
    }
    return true;
}

void seed(FakeMedia& media) {
    for (std::size_t i = 0U; i < media.bytes.size(); ++i) {
        media.bytes[i] = static_cast<std::uint8_t>(i * 7U);
    }
}

int test_absent_source_fails_closed() {
    const HostMediaSource source{};
    if (source.present()) return 1;
    std::array<std::uint8_t, kSectorSize> dest{};
    dest[0] = 0xABU;
    if (source.read(0U, 1U, dest)) return 2;
    if (dest[0] != 0xABU) return 3; // untouched
    if (!source.read(0U, 0U, {})) return 4; // an empty no-op needs no media
    return 0;
}

int test_ee_state_defaults_to_absent_media() {
    reps2::runtime::EeState state{};
    if (state.media.present()) return 5;
    return 0;
}

int test_short_destination_is_refused_without_callback() {
    FakeMedia media{};
    HostMediaSource source{};
    source.context = &media;
    source.read_sectors = &read_sectors;
    std::array<std::uint8_t, kSectorSize - 1U> dest{};
    if (source.read(0U, 1U, dest)) return 6;
    if (media.calls != 0U) return 7;
    return 0;
}

int test_valid_range_is_forwarded() {
    FakeMedia media{};
    seed(media);
    HostMediaSource source{};
    source.context = &media;
    source.read_sectors = &read_sectors;
    if (!source.present()) return 10;

    std::array<std::uint8_t, kSectorSize * 3U> dest{};
    if (!source.read(3U, 2U, dest)) return 11;
    if (media.calls != 1U || media.last_lbn != 3U || media.last_count != 2U) return 12;
    if (media.last_dest_size != kSectorSize * 2U) return 13;
    for (std::size_t i = 0U; i < kSectorSize * 2U; ++i) {
        if (dest[i] != media.bytes[(3U * kSectorSize) + i]) return 14;
    }
    if (dest.back() != 0U) return 15;

    // A source that reports failure writes nothing and reports failure.
    media.fail = true;
    std::array<std::uint8_t, kSectorSize> one{};
    one[0] = 0x5AU;
    if (source.read(1U, 1U, one)) return 14;
    if (one[0] != 0x5AU) return 15;
    return 0;
}

int test_empty_read_forwards_nothing() {
    FakeMedia media{};
    HostMediaSource source{};
    source.context = &media;
    source.read_sectors = &read_sectors;

    std::array<std::uint8_t, kSectorSize> dest{};
    if (!source.read(4U, 0U, dest)) return 20;
    if (media.calls != 0U) return 21;
    return 0;
}

int test_out_of_range_and_overflow_are_refused() {
    FakeMedia media{};
    HostMediaSource source{};
    source.context = &media;
    source.read_sectors = &read_sectors;

    std::array<std::uint8_t, kSectorSize> dest{};
    // A range beyond the 32-bit sector-address space and a null destination
    // are refused before the host source is called.
    if (source.read(0xFFFF'FFFFU, 2U, dest)) return 30;
    if (source.read(0U, 1U, {})) return 31;
    if (media.calls != 0U) return 32;
    // The last representable sector reaches the host adapter, which refuses it
    // because it lies beyond this synthetic image.
    if (source.read(0xFFFF'FFFFU, 1U, dest)) return 33;
    if (media.calls != 1U || media.last_lbn != 0xFFFF'FFFFU
        || media.last_count != 1U) {
        return 35;
    }
    return 0;
}

} // namespace

int main() {
    if (const auto result = test_absent_source_fails_closed(); result != 0) {
        std::fprintf(stderr, "host media absent-source test failed at check %d\n", result);
        return 10 + result;
    }
    if (const auto result = test_ee_state_defaults_to_absent_media(); result != 0) {
        std::fprintf(stderr, "host media default-state test failed at check %d\n", result);
        return 15 + result;
    }
    if (const auto result = test_short_destination_is_refused_without_callback(); result != 0) {
        std::fprintf(stderr, "host media short-destination test failed at check %d\n", result);
        return 18 + result;
    }
    if (const auto result = test_valid_range_is_forwarded(); result != 0) {
        std::fprintf(stderr, "host media forward test failed at check %d\n", result);
        return 20 + result;
    }
    if (const auto result = test_empty_read_forwards_nothing(); result != 0) {
        std::fprintf(stderr, "host media empty-read test failed at check %d\n", result);
        return 30 + result;
    }
    if (const auto result = test_out_of_range_and_overflow_are_refused(); result != 0) {
        std::fprintf(stderr, "host media range test failed at check %d\n", result);
        return 40 + result;
    }
    std::printf("host media boundary smoke passed\n");
    return 0;
}
