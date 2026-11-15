// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bounded read-only ISO 9660 path resolution for the CDVD HLE.
//
// This turns a guest-supplied disc path (as `sceCdSearchFile` receives it) into
// the fields the CDVDFSV SearchFile reply carries: the file's extent LSN, its
// byte size, its 8.3 name and its recording date. It reads only through the
// host media boundary (`HostMediaSource`, ADR 0106); the runtime never opens a
// file. Every step is bounded and fails closed: an unreadable sector, a
// malformed descriptor or a record that would leave the declared extents stops
// the walk with `found == false` instead of inventing an entry.
//
// Field offsets follow the ISO 9660 volume-descriptor and directory-record
// layouts as used by the REPS2 frontend's independent reader
// (`rust/frontend/src/iso9660.rs`) and cross-checked against PS2SDK
// `iop/cdvd/cdvdman/src/cdvdman.c` (`CdSearchFileInner`). ADR 0141.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "reps2/runtime/host_media.hpp"

namespace reps2::runtime {

inline constexpr std::uint32_t kEeIsoSectorSize = 2048U;
inline constexpr std::uint32_t kEeIsoPrimaryVolumeDescriptorLba = 16U;
// Bound on how many sectors one directory extent is walked: enough for a
// realistic in-game directory, small enough that a corrupt extent cannot turn
// the walk into an unbounded read.
inline constexpr std::uint32_t kEeIsoMaxDirectorySectors = 64U;
// Bound on path components (ISO 9660 permits 8 levels); deeper input is refused.
inline constexpr std::uint32_t kEeIsoMaxPathComponents = 8U;
inline constexpr std::size_t kEeIsoMaxNameLength = 16U;

// One resolved entry, already in the layout the reply uses.
struct EeIsoFileEntry {
    bool found{};
    std::uint32_t lsn{};              ///< extent location + extended attribute
    std::uint32_t size{};             ///< data length in bytes
    char name[kEeIsoMaxNameLength]{}; ///< final path component, NUL-terminated
    std::uint8_t date[8]{};           ///< cdvdman order: flags, sec, min, hour, day, mon, year
};

[[nodiscard]] inline std::uint16_t iso_read_u16le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

[[nodiscard]] inline std::uint32_t iso_read_u32le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0])
        | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16)
        | (static_cast<std::uint32_t>(p[3]) << 24);
}

// An ISO 9660 directory record. Only the fields the walk needs are kept; a
// record whose length byte is zero is padding to the next sector boundary.
struct EeIsoDirectoryRecord {
    std::uint32_t lsn{};
    std::uint32_t size{};
    bool is_directory{};
    bool multi_extent{};
    std::string_view name{};
    std::uint8_t recording_date[7]{};
};

// Parse one directory record from `sector` at `offset`. Returns false when the
// record is truncated or its name would leave the sector.
[[nodiscard]] inline bool iso_parse_directory_record(
    const std::uint8_t* sector,
    std::size_t offset,
    EeIsoDirectoryRecord& out
) noexcept {
    const std::uint8_t record_length = sector[offset];
    if (record_length == 0U) {
        return false; // no record here: caller advances to the next sector
    }
    if (record_length < 34U || offset + record_length > kEeIsoSectorSize) {
        return false;
    }
    const std::uint8_t* record = sector + offset;
    const std::uint32_t extent = iso_read_u32le(record + 2);
    const std::uint32_t extended_attributes = record[1];
    const std::uint8_t name_length = record[32];
    if (name_length == 0U || name_length > 32U || 33U + name_length > record_length) {
        return false;
    }
    out.lsn = extent + extended_attributes; // extent + extended-attribute length
    out.size = iso_read_u32le(record + 10);
    out.is_directory = (record[25] & 0x02U) != 0U;
    out.multi_extent = (record[25] & 0x80U) != 0U;
    out.name = std::string_view(
        reinterpret_cast<const char*>(record + 33), name_length);
    for (std::size_t index = 0U; index < 7U; ++index) {
        out.recording_date[index] = record[18U + index];
    }
    return true;
}

// Compare a guest-supplied path component with a directory-record name.
//
// The PS2SDK reference (`cdvdman_cmpname`) compares the first 12 bytes
// case-sensitively, including the ISO ";1" version suffix. REPS2 compares the
// decoded names case-insensitively with a trailing ";1" stripped from both, so
// a caller that omits the version suffix still resolves; this tolerance is a
// deliberate, documented deviation (ADR 0141).
[[nodiscard]] inline bool iso_component_matches(
    std::string_view wanted,
    std::string_view record_name
) noexcept {
    const auto normalize = [](std::string_view value, char* out, std::size_t capacity) {
        std::size_t written = 0U;
        for (const char raw : value) {
            if (raw == ';') {
                break; // drop the version suffix and anything after it
            }
            if (raw == ' ') {
                break; // trailing spaces are padding
            }
            if (written + 1U >= capacity) {
                break;
            }
            out[written++] = (raw >= 'a' && raw <= 'z')
                ? static_cast<char>(raw - 'a' + 'A')
                : raw;
        }
        out[written] = '\0';
        return written;
    };
    char left[16]{};
    char right[16]{};
    const std::size_t left_length = normalize(wanted, left, sizeof(left));
    const std::size_t right_length = normalize(record_name, right, sizeof(right));
    if (left_length == 0U || left_length != right_length) {
        return false;
    }
    for (std::size_t index = 0U; index < left_length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

// Read the primary volume descriptor and return its root directory record.
[[nodiscard]] inline bool iso_read_root_record(
    HostMediaSource& media,
    std::uint8_t* sector_buffer,
    EeIsoDirectoryRecord& root
) noexcept {
    if (!media.read(
            kEeIsoPrimaryVolumeDescriptorLba, 1U,
            std::span<std::uint8_t>(sector_buffer, kEeIsoSectorSize))) {
        return false;
    }
    if (sector_buffer[0] != 1U // primary volume descriptor
        || sector_buffer[1] != 'C' || sector_buffer[2] != 'D'
        || sector_buffer[3] != '0' || sector_buffer[4] != '0'
        || sector_buffer[5] != '1' || sector_buffer[6] != 1U) {
        return false;
    }
    const std::uint8_t root_length = sector_buffer[156];
    if (root_length < 34U) {
        return false;
    }
    // The root record is parsed from the descriptor's own bytes; parse it in
    // place by copying the 156..156+length window into a scratch sector view.
    std::uint8_t record_buffer[kEeIsoSectorSize]{};
    for (std::uint32_t index = 0U; index < root_length; ++index) {
        record_buffer[index] = sector_buffer[156U + index];
    }
    record_buffer[0] = root_length;
    return iso_parse_directory_record(record_buffer, 0U, root);
}

// Resolve `path` (one of "\DIR\FILE.EXT;1", "DIR/FILE.EXT", optional
// "cdrom0:" prefix) to an entry. Bounded by component count and directory
// sectors; returns false when the walk cannot establish an entry.
[[nodiscard]] inline bool iso_resolve_path(
    HostMediaSource& media,
    std::string_view path,
    EeIsoFileEntry& out
) noexcept {
    out = EeIsoFileEntry{};
    if (!media.present() || path.empty()) {
        return false;
    }
    // Drop an optional device prefix and any leading separators.
    if (path.size() >= 6U && path.substr(0U, 6U) == "cdrom0") {
        path.remove_prefix(6U);
        if (!path.empty() && path.front() == ':') {
            path.remove_prefix(1U);
        }
    }
    while (!path.empty() && (path.front() == '\\' || path.front() == '/')) {
        path.remove_prefix(1U);
    }

    std::uint8_t sector_buffer[kEeIsoSectorSize]{};
    EeIsoDirectoryRecord current{};
    if (!iso_read_root_record(media, sector_buffer, current)) {
        return false;
    }

    std::uint32_t components = 0U;
    while (!path.empty()) {
        if (++components > kEeIsoMaxPathComponents) {
            return false;
        }
        const std::size_t separator = path.find_first_of("\\/");
        const std::string_view component = separator == std::string_view::npos
            ? path
            : path.substr(0U, separator);
        path = separator == std::string_view::npos ? std::string_view{}
                                                   : path.substr(separator + 1U);
        if (component.empty()) {
            continue; // repeated separators
        }
        if (current.multi_extent) {
            return false; // multi-extent directories are not modelled
        }
        if (current.size == 0U || current.lsn == 0U) {
            return false;
        }

        const std::uint32_t sectors =
            (current.size + kEeIsoSectorSize - 1U) / kEeIsoSectorSize;
        if (sectors == 0U || sectors > kEeIsoMaxDirectorySectors) {
            return false;
        }

        bool matched = false;
        EeIsoDirectoryRecord match{};
        for (std::uint32_t index = 0U; index < sectors && !matched; ++index) {
            if (!media.read(
                    current.lsn + index, 1U,
                    std::span<std::uint8_t>(sector_buffer, kEeIsoSectorSize))) {
                return false;
            }
            std::size_t offset = 0U;
            while (offset + 1U <= kEeIsoSectorSize) {
                const std::uint8_t record_length = sector_buffer[offset];
                if (record_length == 0U) {
                    break; // padding: the rest of this sector is empty
                }
                EeIsoDirectoryRecord candidate{};
                if (!iso_parse_directory_record(sector_buffer, offset, candidate)) {
                    return false; // malformed record: fail closed, do not guess
                }
                // The "." and ".." self/parent records are not path components.
                if (candidate.name.size() == 1U
                    && (candidate.name.front() == '\0' || candidate.name.front() == '\1')) {
                    offset += record_length;
                    continue;
                }
                if (iso_component_matches(component, candidate.name)) {
                    matched = true;
                    match = candidate;
                    break;
                }
                offset += record_length;
            }
        }
        if (!matched) {
            return false;
        }
        if (match.multi_extent) {
            return false; // multi-extent entries are not modelled: fail closed
        }
        current = match;
    }

    out.found = true;
    out.lsn = current.lsn;
    out.size = current.size;
    std::size_t name_length = 0U;
    for (const char raw : current.name) {
        if (raw == ';') {
            break;
        }
        if (raw == ' ') {
            break;
        }
        if (name_length + 1U >= kEeIsoMaxNameLength) {
            break;
        }
        out.name[name_length++] = raw;
    }
    out.name[name_length] = '\0';
    // The reply's date field follows the PS2SDK `sceCdlFILE` layout
    // (`date[0]=flags, [1]=sec, [2]=min, [3]=hour, [4]=day, [5]=mon, [6..7]=year`
    // as a 16-bit LE word) filled by the cdvdman reference
    // (`iop/cdvd/cdvdman/src/cdvdman.c` lines 2947-2954) from the ISO record's
    // `[year, mon, day, hour, min, sec, gmt]` bytes, with `file_year = year + 1900`.
    // `date[0]` is left zero: the reference's flag byte is not established by the
    // fetched lines, so no value is invented for it.
    const std::uint32_t file_year =
        static_cast<std::uint32_t>(current.recording_date[0]) + 1900U;
    out.date[0] = 0U;
    out.date[1] = current.recording_date[5];
    out.date[2] = current.recording_date[4];
    out.date[3] = current.recording_date[3];
    out.date[4] = current.recording_date[2];
    out.date[5] = current.recording_date[1];
    out.date[6] = static_cast<std::uint8_t>(file_year & 0xFFU);
    out.date[7] = static_cast<std::uint8_t>((file_year >> 8U) & 0xFFU);
    return true;
}

} // namespace reps2::runtime
