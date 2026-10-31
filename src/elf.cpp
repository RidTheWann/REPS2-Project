// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/elf.hpp"

#include <limits>
#include <string>

namespace reps2::elf {
namespace {

[[nodiscard]] std::uint8_t read_u8(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset >= bytes.size()) {
        throw ParseError("ELF field extends beyond the input buffer");
    }
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2U) {
        throw ParseError("ELF 16-bit field extends beyond the input buffer");
    }
    const auto low = static_cast<std::uint16_t>(read_u8(bytes, offset));
    const auto high = static_cast<std::uint16_t>(read_u8(bytes, offset + 1U));
    return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        throw ParseError("ELF 32-bit field extends beyond the input buffer");
    }
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(read_u8(bytes, offset + index))
                 << static_cast<unsigned int>(index * 8U);
    }
    return value;
}

[[nodiscard]] bool range_fits(std::uint64_t offset, std::uint64_t size,
                              std::size_t total_size) noexcept {
    const auto total = static_cast<std::uint64_t>(total_size);
    return offset <= total && size <= (total - offset);
}

void validate_table(std::uint32_t offset, std::uint16_t entry_size,
                    std::uint16_t entry_count, std::size_t minimum_entry_size,
                    std::size_t total_size, const char* label) {
    if (entry_count == 0U) {
        return;
    }
    if (static_cast<std::size_t>(entry_size) < minimum_entry_size) {
        throw ParseError(std::string("ELF ") + label + " entry size is too small");
    }
    const auto table_size = static_cast<std::uint64_t>(entry_size)
                            * static_cast<std::uint64_t>(entry_count);
    if (!range_fits(offset, table_size, total_size)) {
        throw ParseError(std::string("ELF ") + label + " table extends beyond the input buffer");
    }
}

} // namespace

bool Image::is_mips() const noexcept {
    return machine == machine_mips;
}

bool Image::is_executable() const noexcept {
    return type == type_executable;
}

std::size_t Image::loadable_segment_count() const noexcept {
    std::size_t count = 0;
    for (const auto& header : program_headers) {
        if (header.type == segment_load) {
            ++count;
        }
    }
    return count;
}

Image parse(std::span<const std::byte> bytes) {
    constexpr std::size_t elf_header_size = 52U;
    constexpr std::size_t program_header_size = 32U;
    constexpr std::size_t section_header_size = 40U;

    if (bytes.size() < elf_header_size) {
        throw ParseError("Input is too small to contain an ELF32 header");
    }

    constexpr std::uint8_t elf_magic[] = {0x7FU, 0x45U, 0x4CU, 0x46U};
    for (std::size_t index = 0; index < 4U; ++index) {
        if (read_u8(bytes, index) != elf_magic[index]) {
            throw ParseError("Input does not have the ELF magic signature");
        }
    }

    if (read_u8(bytes, 4U) != 1U) {
        throw ParseError("Only ELF32 files are supported by this parser");
    }
    if (read_u8(bytes, 5U) != 1U) {
        throw ParseError("Only little-endian ELF files are supported by this parser");
    }
    if (read_u8(bytes, 6U) != 1U) {
        throw ParseError("Unsupported ELF identification version");
    }

    Image image;
    image.type = read_u16(bytes, 16U);
    image.machine = read_u16(bytes, 18U);
    if (read_u32(bytes, 20U) != 1U) {
        throw ParseError("Unsupported ELF header version");
    }
    image.entry_point = read_u32(bytes, 24U);
    const auto program_offset = read_u32(bytes, 28U);
    const auto section_offset = read_u32(bytes, 32U);
    image.flags = read_u32(bytes, 36U);
    const auto header_size = read_u16(bytes, 40U);
    const auto program_entry_size = read_u16(bytes, 42U);
    const auto program_count = read_u16(bytes, 44U);
    const auto section_entry_size = read_u16(bytes, 46U);
    const auto section_count = read_u16(bytes, 48U);
    const auto section_name_index = read_u16(bytes, 50U);

    if (header_size < elf_header_size || static_cast<std::size_t>(header_size) > bytes.size()) {
        throw ParseError("Invalid ELF header size");
    }
    if (program_count == std::numeric_limits<std::uint16_t>::max()
        || section_name_index == std::numeric_limits<std::uint16_t>::max()
        || (section_count == 0U && section_offset != 0U)) {
        throw ParseError("Extended ELF numbering is not supported yet");
    }
    if (section_count == 0U && section_name_index != 0U) {
        throw ParseError("ELF has a section-name string-table index but no section table");
    }
    if (section_count != 0U && section_name_index != 0U
        && section_name_index >= section_count) {
        throw ParseError("ELF section-name string-table index is out of range");
    }
    if (program_count != 0U && program_offset < header_size) {
        throw ParseError("ELF program-header table overlaps the ELF header");
    }
    if (section_count != 0U && section_offset < header_size) {
        throw ParseError("ELF section-header table overlaps the ELF header");
    }

    validate_table(program_offset, program_entry_size, program_count,
                   program_header_size, bytes.size(), "program-header");
    validate_table(section_offset, section_entry_size, section_count,
                   section_header_size, bytes.size(), "section-header");

    if (program_count != 0U && section_count != 0U) {
        const auto program_table_end = static_cast<std::uint64_t>(program_offset)
                                       + static_cast<std::uint64_t>(program_entry_size)
                                             * static_cast<std::uint64_t>(program_count);
        const auto section_table_end = static_cast<std::uint64_t>(section_offset)
                                       + static_cast<std::uint64_t>(section_entry_size)
                                             * static_cast<std::uint64_t>(section_count);
        const bool tables_overlap = static_cast<std::uint64_t>(program_offset) < section_table_end
                                    && static_cast<std::uint64_t>(section_offset) < program_table_end;
        if (tables_overlap) {
            throw ParseError("ELF program-header and section-header tables overlap");
        }
    }

    image.program_headers.reserve(program_count);
    for (std::uint16_t index = 0; index < program_count; ++index) {
        const auto base = static_cast<std::size_t>(program_offset)
                          + static_cast<std::size_t>(index)
                                * static_cast<std::size_t>(program_entry_size);
        ProgramHeader header;
        header.type = read_u32(bytes, base);
        header.offset = read_u32(bytes, base + 4U);
        header.virtual_address = read_u32(bytes, base + 8U);
        header.physical_address = read_u32(bytes, base + 12U);
        header.file_size = read_u32(bytes, base + 16U);
        header.memory_size = read_u32(bytes, base + 20U);
        header.flags = read_u32(bytes, base + 24U);
        header.alignment = read_u32(bytes, base + 28U);

        if (header.type == segment_load && header.file_size > header.memory_size) {
            throw ParseError("ELF PT_LOAD file size exceeds its memory size");
        }
        if (!range_fits(header.offset, header.file_size, bytes.size())) {
            throw ParseError("ELF segment data extends beyond the input buffer");
        }
        image.program_headers.push_back(header);
    }

    image.section_headers.reserve(section_count);
    for (std::uint16_t index = 0; index < section_count; ++index) {
        const auto base = static_cast<std::size_t>(section_offset)
                          + static_cast<std::size_t>(index)
                                * static_cast<std::size_t>(section_entry_size);
        SectionHeader header;
        header.name_offset = read_u32(bytes, base);
        header.type = read_u32(bytes, base + 4U);
        header.flags = read_u32(bytes, base + 8U);
        header.virtual_address = read_u32(bytes, base + 12U);
        header.offset = read_u32(bytes, base + 16U);
        header.size = read_u32(bytes, base + 20U);
        header.link = read_u32(bytes, base + 24U);
        header.info = read_u32(bytes, base + 28U);
        header.alignment = read_u32(bytes, base + 32U);
        header.entry_size = read_u32(bytes, base + 36U);

        if (header.type != section_type_no_bits
            && !range_fits(header.offset, header.size, bytes.size())) {
            throw ParseError("ELF section data extends beyond the input buffer");
        }
        image.section_headers.push_back(header);
    }

    return image;
}

} // namespace reps2::elf
