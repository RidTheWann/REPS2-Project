// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/elf.hpp"
#include "reps2/mips.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& name) {
    if (!condition) {
        ++failures;
        std::cerr << "[FAIL] " << name << '\n';
    } else {
        std::cout << "[PASS] " << name << '\n';
    }
}

void put_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::vector<std::byte> make_minimal_elf() {
    std::vector<std::byte> bytes(112U, std::byte{0});
    bytes[0] = std::byte{0x7F};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    put_u16(bytes, 16U, reps2::elf::type_executable);
    put_u16(bytes, 18U, reps2::elf::machine_mips);
    put_u32(bytes, 20U, 1U);
    put_u32(bytes, 24U, 0x00100000U);
    put_u32(bytes, 28U, 52U);
    put_u32(bytes, 32U, 0U);
    put_u32(bytes, 36U, 0U);
    put_u16(bytes, 40U, 52U);
    put_u16(bytes, 42U, 32U);
    put_u16(bytes, 44U, 1U);
    put_u16(bytes, 46U, 40U);
    put_u16(bytes, 48U, 0U);
    put_u16(bytes, 50U, 0U);

    put_u32(bytes, 52U, reps2::elf::segment_load);
    put_u32(bytes, 56U, 96U);
    put_u32(bytes, 60U, 0x00100000U);
    put_u32(bytes, 64U, 0x00100000U);
    put_u32(bytes, 68U, 16U);
    put_u32(bytes, 72U, 16U);
    put_u32(bytes, 76U, 5U);
    put_u32(bytes, 80U, 0x1000U);
    return bytes;
}

template <typename Function>
void expect_parse_error(Function&& function, const std::string& name) {
    try {
        function();
        expect(false, name);
    } catch (const reps2::elf::ParseError&) {
        expect(true, name);
    } catch (...) {
        expect(false, name + " (unexpected exception type)");
    }
}

void test_elf_parser() {
    auto bytes = make_minimal_elf();
    const auto image = reps2::elf::parse(std::span<const std::byte>(bytes));
    expect(image.is_mips(), "ELF identifies EM_MIPS");
    expect(image.is_executable(), "ELF identifies ET_EXEC");
    expect(image.entry_point == 0x00100000U, "ELF entry point parsed");
    expect(image.loadable_segment_count() == 1U, "ELF PT_LOAD segment parsed");

    auto bad_magic = bytes;
    bad_magic[0] = std::byte{0};
    expect_parse_error([&] { (void)reps2::elf::parse(bad_magic); }, "Reject invalid ELF magic");

    auto bad_endian = bytes;
    bad_endian[5] = std::byte{2};
    expect_parse_error([&] { (void)reps2::elf::parse(bad_endian); }, "Reject big-endian ELF");

    auto truncated_table = bytes;
    put_u32(truncated_table, 28U, 0xFFFFFFF0U);
    expect_parse_error([&] { (void)reps2::elf::parse(truncated_table); }, "Reject out-of-range program-header table");

    auto header_overlap = bytes;
    put_u32(header_overlap, 28U, 16U);
    expect_parse_error([&] { (void)reps2::elf::parse(header_overlap); }, "Reject program-header table overlapping ELF header");

    auto invalid_segment = bytes;
    put_u32(invalid_segment, 68U, 20U);
    put_u32(invalid_segment, 72U, 16U);
    expect_parse_error([&] { (void)reps2::elf::parse(invalid_segment); }, "Reject segment file size greater than memory size");

    auto note_segment = bytes;
    put_u32(note_segment, 52U, 4U); // PT_NOTE may have file data and no memory image.
    put_u32(note_segment, 72U, 0U);
    const auto note_image = reps2::elf::parse(note_segment);
    expect(note_image.loadable_segment_count() == 0U, "Do not count PT_NOTE as PT_LOAD");

    auto invalid_names = bytes;
    put_u16(invalid_names, 50U, 1U);
    expect_parse_error([&] { (void)reps2::elf::parse(invalid_names); }, "Reject section-name index without section table");
}

void test_mips_decoder() {
    const auto nop = reps2::mips::decode(0U, 0x1000U);
    expect(nop.opcode == reps2::mips::Opcode::nop, "Decode canonical NOP");

    const std::uint32_t addiu = (0x09U << 26U) | (4U << 21U) | (5U << 16U) | 0xFFFCU;
    const auto addiu_decoded = reps2::mips::decode(addiu, 0x1000U);
    expect(addiu_decoded.opcode == reps2::mips::Opcode::addiu, "Decode ADDIU");
    expect(addiu_decoded.signed_immediate() == -4, "Sign-extend ADDIU immediate");
    expect(addiu_decoded.rs == 4U && addiu_decoded.rt == 5U, "Decode ADDIU register fields");

    const std::uint32_t beq = (0x04U << 26U) | (2U << 21U) | (3U << 16U) | 4U;
    const auto beq_decoded = reps2::mips::decode(beq, 0x1000U);
    expect(beq_decoded.opcode == reps2::mips::Opcode::beq, "Decode BEQ");
    expect(beq_decoded.has_delay_slot, "Mark branch delay slot");
    expect(beq_decoded.jump_target == 0x1014U, "Compute BEQ target from PC+4");

    const std::uint32_t jr = (4U << 21U) | 0x08U;
    const auto jr_decoded = reps2::mips::decode(jr, 0x2000U);
    expect(jr_decoded.opcode == reps2::mips::Opcode::jr, "Decode JR");
    expect(jr_decoded.has_delay_slot, "Mark JR delay slot");

    const std::uint32_t jal = (0x03U << 26U) | 1U;
    const auto jal_decoded = reps2::mips::decode(jal, 0x80000000U);
    expect(jal_decoded.opcode == reps2::mips::Opcode::jal, "Decode JAL");
    expect(jal_decoded.jump_target == 0x80000004U, "Compute JAL target using upper PC region");

    const auto unsupported = reps2::mips::decode(0xFFFFFFFFU);
    expect(!unsupported.is_supported(), "Unsupported instruction is explicit");
}

} // namespace

int main() {
    test_elf_parser();
    test_mips_decoder();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All REPS2 tests passed\n";
    return 0;
}
