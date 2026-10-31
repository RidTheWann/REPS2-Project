// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/elf.hpp"
#include "reps2/mips.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::uintmax_t maximum_input_size = 512U * 1024U * 1024U;

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error("Cannot inspect input file: " + error.message());
    }
    if (size > maximum_input_size) {
        throw std::runtime_error("Input file exceeds the 512 MiB safety limit");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot open input file: " + path.string());
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream || static_cast<std::size_t>(stream.gcount()) != bytes.size()) {
            throw std::runtime_error("Failed to read the complete input file");
        }
    }
    return bytes;
}

[[nodiscard]] std::uint32_t parse_hex_u32(std::string_view text) {
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2U);
    }
    if (text.empty()) {
        throw std::runtime_error("Expected a hexadecimal 32-bit value");
    }

    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::runtime_error("Invalid hexadecimal 32-bit value");
    }
    return value;
}

void print_usage() {
    std::cout << "REPS2 - PlayStation 2 static recompilation research tool\n\n"
              << "Usage:\n"
              << "  reps2 version\n"
              << "  reps2 elf-info <elf-file>\n"
              << "  reps2 decode <instruction-word-hex> [pc-hex]\n\n"
              << "The current build is an experimental foundation, not a complete game recompiler.\n";
}

[[nodiscard]] int elf_info(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    const auto image = reps2::elf::parse(std::span<const std::byte>(bytes));

    std::cout << "ELF32 little-endian image\n"
              << "  Type: " << image.type << (image.is_executable() ? " (executable)" : "") << '\n'
              << "  Machine: " << image.machine << (image.is_mips() ? " (MIPS)" : " (not MIPS)") << '\n'
              << "  Entry point: 0x" << std::hex << std::uppercase << std::setw(8)
              << std::setfill('0') << image.entry_point << std::dec << '\n'
              << "  Program headers: " << image.program_headers.size() << '\n'
              << "  Section headers: " << image.section_headers.size() << '\n'
              << "  PT_LOAD segments: " << image.loadable_segment_count() << '\n';

    if (!image.is_mips()) {
        std::cerr << "Warning: ELF machine is not EM_MIPS; this file is not a supported PS2 EE target.\n";
    }
    return 0;
}

[[nodiscard]] int decode_word(std::string_view word_text, std::string_view pc_text) {
    const auto word = parse_hex_u32(word_text);
    const auto pc = pc_text.empty() ? 0U : parse_hex_u32(pc_text);
    const auto decoded = reps2::mips::decode(word, pc);

    std::cout << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
              << word << std::dec << "  " << decoded.mnemonic()
              << "  rs=" << static_cast<unsigned int>(decoded.rs)
              << " rt=" << static_cast<unsigned int>(decoded.rt)
              << " rd=" << static_cast<unsigned int>(decoded.rd);
    if (decoded.has_delay_slot) {
        std::cout << "  [delay-slot]";
    }
    if (decoded.opcode == reps2::mips::Opcode::beq
        || decoded.opcode == reps2::mips::Opcode::bne
        || decoded.opcode == reps2::mips::Opcode::blez
        || decoded.opcode == reps2::mips::Opcode::bgtz
        || decoded.opcode == reps2::mips::Opcode::j
        || decoded.opcode == reps2::mips::Opcode::jal) {
        std::cout << "  target=0x" << std::hex << std::uppercase << decoded.jump_target << std::dec;
    }
    if (decoded.opcode == reps2::mips::Opcode::addi
        || decoded.opcode == reps2::mips::Opcode::addiu
        || decoded.opcode == reps2::mips::Opcode::slti
        || decoded.opcode == reps2::mips::Opcode::sltiu
        || decoded.opcode == reps2::mips::Opcode::lb
        || decoded.opcode == reps2::mips::Opcode::lh
        || decoded.opcode == reps2::mips::Opcode::lw
        || decoded.opcode == reps2::mips::Opcode::lbu
        || decoded.opcode == reps2::mips::Opcode::lhu
        || decoded.opcode == reps2::mips::Opcode::sb
        || decoded.opcode == reps2::mips::Opcode::sh
        || decoded.opcode == reps2::mips::Opcode::sw) {
        std::cout << "  imm=" << decoded.signed_immediate();
    }
    std::cout << '\n';

    if (!decoded.is_supported()) {
        std::cerr << "Instruction is outside the currently implemented decoder subset.\n";
        return 3;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "version") {
            std::cout << "REPS2 0.1.0 (experimental foundation)\n";
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "elf-info") {
            return elf_info(std::filesystem::path(argv[2]));
        }
        if ((argc == 3 || argc == 4) && std::string_view(argv[1]) == "decode") {
            return decode_word(argv[2], argc == 4 ? std::string_view(argv[3]) : std::string_view{});
        }
        print_usage();
        return argc == 1 ? 0 : 2;
    } catch (const reps2::elf::ParseError& error) {
        std::cerr << "ELF parse error: " << error.what() << '\n';
        return 4;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
