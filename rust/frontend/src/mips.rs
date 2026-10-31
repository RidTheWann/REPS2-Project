//! Decoder for the explicitly implemented base-MIPS integer subset.

/// Decoded operation. Unsupported encodings remain Unknown and must never
/// be treated as no-ops by later compiler phases.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Opcode {
    Nop,
    Sll, Srl, Sra,
    Jr, Jalr, Syscall, Break,
    Mfhi, Mflo, Mult, Multu, Div, Divu,
    Add, Addu, Sub, Subu, And, Or, Xor, Nor, Slt, Sltu,
    J, Jal, Beq, Bne, Blez, Bgtz,
    Addi, Addiu, Slti, Sltiu, Andi, Ori, Xori, Lui,
    Lb, Lh, Lw, Lbu, Lhu, Sb, Sh, Sw,
    Unknown,
}

impl Opcode {
    /// Returns a stable mnemonic for diagnostics.
    #[must_use]
    pub const fn mnemonic(self) -> &'static str {
        match self {
            Self::Nop => "nop",
            Self::Sll => "sll",
            Self::Srl => "srl",
            Self::Sra => "sra",
            Self::Jr => "jr",
            Self::Jalr => "jalr",
            Self::Syscall => "syscall",
            Self::Break => "break",
            Self::Mfhi => "mfhi",
            Self::Mflo => "mflo",
            Self::Mult => "mult",
            Self::Multu => "multu",
            Self::Div => "div",
            Self::Divu => "divu",
            Self::Add => "add",
            Self::Addu => "addu",
            Self::Sub => "sub",
            Self::Subu => "subu",
            Self::And => "and",
            Self::Or => "or",
            Self::Xor => "xor",
            Self::Nor => "nor",
            Self::Slt => "slt",
            Self::Sltu => "sltu",
            Self::J => "j",
            Self::Jal => "jal",
            Self::Beq => "beq",
            Self::Bne => "bne",
            Self::Blez => "blez",
            Self::Bgtz => "bgtz",
            Self::Addi => "addi",
            Self::Addiu => "addiu",
            Self::Slti => "slti",
            Self::Sltiu => "sltiu",
            Self::Andi => "andi",
            Self::Ori => "ori",
            Self::Xori => "xori",
            Self::Lui => "lui",
            Self::Lb => "lb",
            Self::Lh => "lh",
            Self::Lw => "lw",
            Self::Lbu => "lbu",
            Self::Lhu => "lhu",
            Self::Sb => "sb",
            Self::Sh => "sh",
            Self::Sw => "sw",
            Self::Unknown => "unknown",
        }
    }
}

/// One decoded guest instruction. This is decode metadata, not executed
/// architectural semantics.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Instruction {
    pub raw: u32,
    pub pc: u32,
    pub opcode: Opcode,
    pub rs: u8,
    pub rt: u8,
    pub rd: u8,
    pub shift_amount: u8,
    pub immediate: u16,
    pub target: u32,
    pub has_delay_slot: bool,
}

impl Instruction {
    /// Returns the immediate interpreted as signed 16-bit.
    #[must_use]
    pub const fn signed_immediate(self) -> i32 {
        self.immediate as i16 as i32
    }

    /// Returns whether the encoding belongs to the supported subset.
    #[must_use]
    pub const fn is_supported(self) -> bool {
        !matches!(self.opcode, Opcode::Unknown)
    }
}

/// Decodes one word from the explicitly supported base-MIPS integer subset.
#[must_use]
pub fn decode(word: u32, pc: u32) -> Instruction {
    let rs = ((word >> 21) & 0x1F) as u8;
    let rt = ((word >> 16) & 0x1F) as u8;
    let rd = ((word >> 11) & 0x1F) as u8;
    let shift_amount = ((word >> 6) & 0x1F) as u8;
    let immediate = (word & 0xFFFF) as u16;
    let primary = word >> 26;

    let mut instruction = Instruction {
        raw: word,
        pc,
        opcode: Opcode::Unknown,
        rs,
        rt,
        rd,
        shift_amount,
        immediate,
        target: 0,
        has_delay_slot: false,
    };

    if word == 0 {
        instruction.opcode = Opcode::Nop;
        return instruction;
    }

    if primary == 0 {
        instruction.opcode = match word & 0x3F {
            0x00 if rs == 0 => Opcode::Sll,
            0x02 if rs == 0 => Opcode::Srl,
            0x03 if rs == 0 => Opcode::Sra,
            0x08 if rt == 0 && rd == 0 && shift_amount == 0 => Opcode::Jr,
            0x09 if rt == 0 && shift_amount == 0 => Opcode::Jalr,
            0x0C => Opcode::Syscall,
            0x0D => Opcode::Break,
            0x10 if rs == 0 && rt == 0 && shift_amount == 0 => Opcode::Mfhi,
            0x12 if rs == 0 && rt == 0 && shift_amount == 0 => Opcode::Mflo,
            0x18 if rd == 0 && shift_amount == 0 => Opcode::Mult,
            0x19 if rd == 0 && shift_amount == 0 => Opcode::Multu,
            0x1A if rd == 0 && shift_amount == 0 => Opcode::Div,
            0x1B if rd == 0 && shift_amount == 0 => Opcode::Divu,
            0x20 if shift_amount == 0 => Opcode::Add,
            0x21 if shift_amount == 0 => Opcode::Addu,
            0x22 if shift_amount == 0 => Opcode::Sub,
            0x23 if shift_amount == 0 => Opcode::Subu,
            0x24 if shift_amount == 0 => Opcode::And,
            0x25 if shift_amount == 0 => Opcode::Or,
            0x26 if shift_amount == 0 => Opcode::Xor,
            0x27 if shift_amount == 0 => Opcode::Nor,
            0x2A if shift_amount == 0 => Opcode::Slt,
            0x2B if shift_amount == 0 => Opcode::Sltu,
            _ => Opcode::Unknown,
        };
    } else {
        instruction.opcode = match primary {
            0x02 => {
                instruction.target = (pc.wrapping_add(4) & 0xF000_0000) | ((word & 0x03FF_FFFF) << 2);
                instruction.has_delay_slot = true;
                Opcode::J
            }
            0x03 => {
                instruction.target = (pc.wrapping_add(4) & 0xF000_0000) | ((word & 0x03FF_FFFF) << 2);
                instruction.has_delay_slot = true;
                Opcode::Jal
            }
            0x04..=0x07 => {
                let offset = ((immediate as i16 as i32).wrapping_mul(4)) as u32;
                instruction.target = pc.wrapping_add(4).wrapping_add(offset);
                instruction.has_delay_slot = true;
                match primary {
                    0x04 => Opcode::Beq,
                    0x05 => Opcode::Bne,
                    0x06 if rt == 0 => Opcode::Blez,
                    0x07 if rt == 0 => Opcode::Bgtz,
                    _ => Opcode::Unknown,
                }
            }
            0x08 => Opcode::Addi,
            0x09 => Opcode::Addiu,
            0x0A => Opcode::Slti,
            0x0B => Opcode::Sltiu,
            0x0C => Opcode::Andi,
            0x0D => Opcode::Ori,
            0x0E => Opcode::Xori,
            0x0F if rs == 0 => Opcode::Lui,
            0x20 => Opcode::Lb,
            0x21 => Opcode::Lh,
            0x23 => Opcode::Lw,
            0x24 => Opcode::Lbu,
            0x25 => Opcode::Lhu,
            0x28 => Opcode::Sb,
            0x29 => Opcode::Sh,
            0x2B => Opcode::Sw,
            _ => Opcode::Unknown,
        };
    }

    if !matches!(instruction.opcode,
        Opcode::J | Opcode::Jal | Opcode::Beq | Opcode::Bne |
        Opcode::Blez | Opcode::Bgtz | Opcode::Jr | Opcode::Jalr) {
        instruction.has_delay_slot = false;
    }
    instruction
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_nop_addiu_and_signed_immediate() {
        assert_eq!(decode(0, 0x1000).opcode, Opcode::Nop);
        let word = (0x09 << 26) | (4 << 21) | (5 << 16) | 0xFFFC;
        let decoded = decode(word, 0x1000);
        assert_eq!(decoded.opcode, Opcode::Addiu);
        assert_eq!(decoded.rs, 4);
        assert_eq!(decoded.rt, 5);
        assert_eq!(decoded.signed_immediate(), -4);
    }

    #[test]
    fn decodes_branch_delay_slot_and_target() {
        let word = (0x04 << 26) | (2 << 21) | (3 << 16) | 4;
        let decoded = decode(word, 0x1000);
        assert_eq!(decoded.opcode, Opcode::Beq);
        assert!(decoded.has_delay_slot);
        assert_eq!(decoded.target, 0x1014);
    }

    #[test]
    fn rejects_invalid_reserved_fields_and_unknown_ops() {
        let bad_jr = (4 << 21) | (1 << 16) | 0x08;
        assert!(!decode(bad_jr, 0).is_supported());
        assert!(!decode(0xFFFF_FFFF, 0).is_supported());
    }

    #[test]
    fn jump_region_uses_pc_plus_four() {
        let decoded = decode((0x03 << 26) | 1, 0x8000_0000);
        assert_eq!(decoded.opcode, Opcode::Jal);
        assert_eq!(decoded.target, 0x8000_0004);
    }
}
