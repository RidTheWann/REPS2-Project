// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace reps2::mips {

enum class Opcode {
    nop,
    sll, srl, sra,
    jr, jalr, syscall, breakpoint,
    mfhi, mflo, mult, multu, div, divu,
    add, addu, sub, subu, bit_and, bit_or, bit_xor, bit_nor, slt, sltu,
    j, jal, beq, bne, blez, bgtz,
    addi, addiu, slti, sltiu, andi, ori, xori, lui,
    lb, lh, lw, lbu, lhu, sb, sh, sw,
    unknown
};

struct Instruction {
    std::uint32_t raw{};
    std::uint32_t pc{};
    Opcode opcode{Opcode::unknown};
    std::uint8_t rs{};
    std::uint8_t rt{};
    std::uint8_t rd{};
    std::uint8_t shift_amount{};
    std::uint16_t immediate{};
    std::uint32_t jump_target{};
    bool has_delay_slot{};

    [[nodiscard]] std::int32_t signed_immediate() const noexcept;
    [[nodiscard]] bool is_supported() const noexcept;
    [[nodiscard]] std::string_view mnemonic() const noexcept;
};

// Decodes the documented base-integer subset used by the initial REPS2
// foundation. R5900 MMI, COP0/1/2 and VU-specific encodings are intentionally
// reported as unknown until their semantics and tests are implemented.
[[nodiscard]] Instruction decode(std::uint32_t word, std::uint32_t pc = 0U) noexcept;

} // namespace reps2::mips
