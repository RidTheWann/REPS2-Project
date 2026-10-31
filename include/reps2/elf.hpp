// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace reps2::elf {

inline constexpr std::uint16_t machine_mips = 8;
inline constexpr std::uint16_t type_executable = 2;
inline constexpr std::uint32_t segment_load = 1;
inline constexpr std::uint32_t section_type_no_bits = 8;

class ParseError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ProgramHeader {
    std::uint32_t type{};
    std::uint32_t offset{};
    std::uint32_t virtual_address{};
    std::uint32_t physical_address{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t flags{};
    std::uint32_t alignment{};
};

struct SectionHeader {
    std::uint32_t name_offset{};
    std::uint32_t type{};
    std::uint32_t flags{};
    std::uint32_t virtual_address{};
    std::uint32_t offset{};
    std::uint32_t size{};
    std::uint32_t link{};
    std::uint32_t info{};
    std::uint32_t alignment{};
    std::uint32_t entry_size{};
};

struct Image {
    std::uint16_t type{};
    std::uint16_t machine{};
    std::uint32_t entry_point{};
    std::uint32_t flags{};
    std::vector<ProgramHeader> program_headers;
    std::vector<SectionHeader> section_headers;

    [[nodiscard]] bool is_mips() const noexcept;
    [[nodiscard]] bool is_executable() const noexcept;
    [[nodiscard]] std::size_t loadable_segment_count() const noexcept;
};

// Parses a bounded ELF32 little-endian image. It validates all table and
// file-backed segment ranges before exposing parsed metadata.
[[nodiscard]] Image parse(std::span<const std::byte> bytes);

} // namespace reps2::elf
