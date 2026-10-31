// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/mips.hpp"

namespace reps2::mips {
namespace {

[[nodiscard]] constexpr std::uint8_t field(std::uint32_t value, unsigned int shift,
                                            std::uint32_t mask) noexcept {
    return static_cast<std::uint8_t>((value >> shift) & mask);
}

[[nodiscard]] constexpr std::uint32_t branch_target(std::uint32_t pc,
                                                     std::uint16_t immediate) noexcept {
    const auto signed_offset = static_cast<std::int32_t>(static_cast<std::int16_t>(immediate));
    const auto byte_offset = static_cast<std::uint32_t>(signed_offset * 4);
    return pc + 4U + byte_offset;
}

} // namespace

std::int32_t Instruction::signed_immediate() const noexcept {
    return static_cast<std::int32_t>(static_cast<std::int16_t>(immediate));
}

bool Instruction::is_supported() const noexcept {
    return opcode != Opcode::unknown;
}

std::string_view Instruction::mnemonic() const noexcept {
    switch (opcode) {
    case Opcode::nop: return "nop";
    case Opcode::sll: return "sll";
    case Opcode::srl: return "srl";
    case Opcode::sra: return "sra";
    case Opcode::jr: return "jr";
    case Opcode::jalr: return "jalr";
    case Opcode::syscall: return "syscall";
    case Opcode::breakpoint: return "break";
    case Opcode::mfhi: return "mfhi";
    case Opcode::mflo: return "mflo";
    case Opcode::mult: return "mult";
    case Opcode::multu: return "multu";
    case Opcode::div: return "div";
    case Opcode::divu: return "divu";
    case Opcode::add: return "add";
    case Opcode::addu: return "addu";
    case Opcode::sub: return "sub";
    case Opcode::subu: return "subu";
    case Opcode::bit_and: return "and";
    case Opcode::bit_or: return "or";
    case Opcode::bit_xor: return "xor";
    case Opcode::bit_nor: return "nor";
    case Opcode::slt: return "slt";
    case Opcode::sltu: return "sltu";
    case Opcode::j: return "j";
    case Opcode::jal: return "jal";
    case Opcode::beq: return "beq";
    case Opcode::bne: return "bne";
    case Opcode::blez: return "blez";
    case Opcode::bgtz: return "bgtz";
    case Opcode::addi: return "addi";
    case Opcode::addiu: return "addiu";
    case Opcode::slti: return "slti";
    case Opcode::sltiu: return "sltiu";
    case Opcode::andi: return "andi";
    case Opcode::ori: return "ori";
    case Opcode::xori: return "xori";
    case Opcode::lui: return "lui";
    case Opcode::lb: return "lb";
    case Opcode::lh: return "lh";
    case Opcode::lw: return "lw";
    case Opcode::lbu: return "lbu";
    case Opcode::lhu: return "lhu";
    case Opcode::sb: return "sb";
    case Opcode::sh: return "sh";
    case Opcode::sw: return "sw";
    case Opcode::unknown: return "unknown";
    }
    return "unknown";
}

Instruction decode(std::uint32_t word, std::uint32_t pc) noexcept {
    Instruction instruction;
    instruction.raw = word;
    instruction.pc = pc;
    instruction.rs = field(word, 21U, 0x1FU);
    instruction.rt = field(word, 16U, 0x1FU);
    instruction.rd = field(word, 11U, 0x1FU);
    instruction.shift_amount = field(word, 6U, 0x1FU);
    instruction.immediate = static_cast<std::uint16_t>(word & 0xFFFFU);

    if (word == 0U) {
        instruction.opcode = Opcode::nop;
        return instruction;
    }

    const auto primary = field(word, 26U, 0x3FU);
    if (primary == 0U) {
        switch (field(word, 0U, 0x3FU)) {
        case 0x00U: instruction.opcode = Opcode::sll; break;
        case 0x02U: instruction.opcode = Opcode::srl; break;
        case 0x03U: instruction.opcode = Opcode::sra; break;
        case 0x08U: instruction.opcode = Opcode::jr; instruction.has_delay_slot = true; break;
        case 0x09U: instruction.opcode = Opcode::jalr; instruction.has_delay_slot = true; break;
        case 0x0CU: instruction.opcode = Opcode::syscall; break;
        case 0x0DU: instruction.opcode = Opcode::breakpoint; break;
        case 0x10U: instruction.opcode = Opcode::mfhi; break;
        case 0x12U: instruction.opcode = Opcode::mflo; break;
        case 0x18U: instruction.opcode = Opcode::mult; break;
        case 0x19U: instruction.opcode = Opcode::multu; break;
        case 0x1AU: instruction.opcode = Opcode::div; break;
        case 0x1BU: instruction.opcode = Opcode::divu; break;
        case 0x20U: instruction.opcode = Opcode::add; break;
        case 0x21U: instruction.opcode = Opcode::addu; break;
        case 0x22U: instruction.opcode = Opcode::sub; break;
        case 0x23U: instruction.opcode = Opcode::subu; break;
        case 0x24U: instruction.opcode = Opcode::bit_and; break;
        case 0x25U: instruction.opcode = Opcode::bit_or; break;
        case 0x26U: instruction.opcode = Opcode::bit_xor; break;
        case 0x27U: instruction.opcode = Opcode::bit_nor; break;
        case 0x2AU: instruction.opcode = Opcode::slt; break;
        case 0x2BU: instruction.opcode = Opcode::sltu; break;
        default: instruction.opcode = Opcode::unknown; break;
        }
        return instruction;
    }

    switch (primary) {
    case 0x02U:
        instruction.opcode = Opcode::j;
        instruction.has_delay_slot = true;
        instruction.jump_target = ((pc + 4U) & 0xF0000000U) | ((word & 0x03FFFFFFU) << 2U);
        break;
    case 0x03U:
        instruction.opcode = Opcode::jal;
        instruction.has_delay_slot = true;
        instruction.jump_target = ((pc + 4U) & 0xF0000000U) | ((word & 0x03FFFFFFU) << 2U);
        break;
    case 0x04U: instruction.opcode = Opcode::beq; instruction.has_delay_slot = true; instruction.jump_target = branch_target(pc, instruction.immediate); break;
    case 0x05U: instruction.opcode = Opcode::bne; instruction.has_delay_slot = true; instruction.jump_target = branch_target(pc, instruction.immediate); break;
    case 0x06U: instruction.opcode = Opcode::blez; instruction.has_delay_slot = true; instruction.jump_target = branch_target(pc, instruction.immediate); break;
    case 0x07U: instruction.opcode = Opcode::bgtz; instruction.has_delay_slot = true; instruction.jump_target = branch_target(pc, instruction.immediate); break;
    case 0x08U: instruction.opcode = Opcode::addi; break;
    case 0x09U: instruction.opcode = Opcode::addiu; break;
    case 0x0AU: instruction.opcode = Opcode::slti; break;
    case 0x0BU: instruction.opcode = Opcode::sltiu; break;
    case 0x0CU: instruction.opcode = Opcode::andi; break;
    case 0x0DU: instruction.opcode = Opcode::ori; break;
    case 0x0EU: instruction.opcode = Opcode::xori; break;
    case 0x0FU: instruction.opcode = Opcode::lui; break;
    case 0x20U: instruction.opcode = Opcode::lb; break;
    case 0x21U: instruction.opcode = Opcode::lh; break;
    case 0x23U: instruction.opcode = Opcode::lw; break;
    case 0x24U: instruction.opcode = Opcode::lbu; break;
    case 0x25U: instruction.opcode = Opcode::lhu; break;
    case 0x28U: instruction.opcode = Opcode::sb; break;
    case 0x29U: instruction.opcode = Opcode::sh; break;
    case 0x2BU: instruction.opcode = Opcode::sw; break;
    default: instruction.opcode = Opcode::unknown; break;
    }
    return instruction;
}

} // namespace reps2::mips
