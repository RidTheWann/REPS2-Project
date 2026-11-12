// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

namespace reps2::runtime {

inline constexpr std::uint32_t kHostMediaSectorSize{2048U};
inline constexpr std::uint64_t kHostMediaSectorAddressLimit{0x1'0000'0000ULL};

// The host media boundary: the read-only sector source a disc device serves I/O
// from.
//
// The runtime never opens files. The host application — the generated probe, the
// developer CLI, or the M7 library — installs a source here, and the runtime only
// ever asks it for whole 2048-byte user sectors. The host owns `context` and must
// keep it valid while the EeState can issue reads; null is valid only if the
// callback does not require context. When no source is installed,
// every disc operation fails closed: a device model must never invent disc
// contents to keep a trace moving.
//
// `read_sectors` receives exactly `count * kHostMediaSectorSize` writable
// bytes. It must copy user data from `lbn` or write nothing and return false.
// The host adapter checks its image's actual sector count before copying.
struct HostMediaSource {
    void* context{};
    bool (*read_sectors)(
        void* context,
        std::uint32_t lbn,
        std::uint32_t count,
        std::span<std::uint8_t> dest) noexcept = nullptr;
    // Whether a source is installed at all. A model that needs disc data must
    // check this and fail closed rather than assume media is present.
    [[nodiscard]] bool present() const noexcept {
        return read_sectors != nullptr;
    }

    // Read `count` sectors into `dest`, which must hold
    // `count * kHostMediaSectorSize` bytes. Empty reads are successful no-ops;
    // non-empty reads reject an absent source, null destination, or LBN overflow.
    // The host adapter validates its own image length.
    [[nodiscard]] bool read(
        std::uint32_t lbn,
        std::uint32_t count,
        std::span<std::uint8_t> dest
    ) const noexcept {
        if (count == 0U) {
            return true;
        }
        if (!present()) {
            return false;
        }
        const auto required_size =
            static_cast<std::uint64_t>(count) * kHostMediaSectorSize;
        const auto end_lbn = static_cast<std::uint64_t>(lbn) + count;
        if (static_cast<std::uint64_t>(dest.size()) < required_size
            || end_lbn > kHostMediaSectorAddressLimit) {
            return false;
        }
        return read_sectors(
            context,
            lbn,
            count,
            dest.first(static_cast<std::size_t>(required_size)));
    }
};

} // namespace reps2::runtime
