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
    J, Jal, Beq, Bne, Blez, Bgtz, Bltz, Bgez, Bltzal, Bgezal,
    Addi, Addiu, Slti, Sltiu, Andi, Ori, Xori, Lui,
    Lb, Lh, Lw, Lbu, Lhu, Lwu, Sb, Sh, Sw,
    Lwl, Lwr, Swl, Swr, Ldl, Ldr, Sdl, Sdr,
    Ld, Sd, Lq, Sq,
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
            Self::Bltz => "bltz",
            Self::Bgez => "bgez",
            Self::Bltzal => "bltzal",
            Self::Bgezal => "bgezal",
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
            Self::Lwu => "lwu",
            Self::Sb => "sb",
            Self::Sh => "sh",
            Self::Sw => "sw",
            Self::Lwl => "lwl",
            Self::Lwr => "lwr",
            Self::Swl => "swl",
            Self::Swr => "swr",
            Self::Ldl => "ldl",
            Self::Ldr => "ldr",
            Self::Sdl => "sdl",
            Self::Sdr => "sdr",
            Self::Ld => "ld",
            Self::Sd => "sd",
            Self::Lq => "lq",
            Self::Sq => "sq",
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

    /// Returns whether the opcode transfers control and owns a delay slot.
    #[must_use]
    pub const fn is_control_transfer(self) -> bool {
        matches!(
            self.opcode,
            Opcode::J | Opcode::Jal | Opcode::Beq | Opcode::Bne |
            Opcode::Blez | Opcode::Bgtz | Opcode::Bltz | Opcode::Bgez |
            Opcode::Bltzal | Opcode::Bgezal | Opcode::Jr | Opcode::Jalr
        )
    }

    /// Returns whether the opcode is a conditional direct branch.
    #[must_use]
    pub const fn is_conditional_branch(self) -> bool {
        matches!(
            self.opcode,
            Opcode::Beq | Opcode::Bne | Opcode::Blez | Opcode::Bgtz |
            Opcode::Bltz | Opcode::Bgez | Opcode::Bltzal | Opcode::Bgezal
        )
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
            0x01 => {
                let opcode = match rt {
                    0x00 => Opcode::Bltz,
                    0x01 => Opcode::Bgez,
                    0x10 => Opcode::Bltzal,
                    0x11 => Opcode::Bgezal,
                    _ => Opcode::Unknown,
                };
                if opcode != Opcode::Unknown {
                    instruction.target = pc.wrapping_add(4)
                        .wrapping_add(((immediate as i16 as i32).wrapping_mul(4)) as u32);
                    instruction.has_delay_slot = true;
                }
                opcode
            }
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
            0x1A => Opcode::Ldl,
            0x1B => Opcode::Ldr,
            0x20 => Opcode::Lb,
            0x21 => Opcode::Lh,
            0x22 => Opcode::Lwl,
            0x23 => Opcode::Lw,
            0x24 => Opcode::Lbu,
            0x25 => Opcode::Lhu,
            0x27 => Opcode::Lwu,
            0x28 => Opcode::Sb,
            0x29 => Opcode::Sh,
            0x2A => Opcode::Swl,
            0x2B => Opcode::Sw,
            0x2C => Opcode::Sdl,
            0x2D => Opcode::Sdr,
            0x2E => Opcode::Swr,
            0x26 => Opcode::Lwr,
            0x37 => Opcode::Ld,
            0x3F => Opcode::Sd,
            0x1E => Opcode::Lq,
            0x1F => Opcode::Sq,
            _ => Opcode::Unknown,
        };
    }

    if !matches!(instruction.opcode,
        Opcode::J | Opcode::Jal | Opcode::Beq | Opcode::Bne |
        Opcode::Blez | Opcode::Bgtz | Opcode::Bltz | Opcode::Bgez |
        Opcode::Bltzal | Opcode::Bgezal | Opcode::Jr | Opcode::Jalr) {
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
    fn decodes_load_store_width_and_signedness_variants() {
        let cases = [
            ((0x20 << 26) | (4 << 21) | (5 << 16) | 0xFFFC, Opcode::Lb),
            ((0x21 << 26) | (4 << 21) | (5 << 16) | 2, Opcode::Lh),
            ((0x23 << 26) | (4 << 21) | (5 << 16) | 4, Opcode::Lw),
            ((0x24 << 26) | (4 << 21) | (5 << 16) | 6, Opcode::Lbu),
            ((0x25 << 26) | (4 << 21) | (5 << 16) | 8, Opcode::Lhu),
            ((0x27 << 26) | (4 << 21) | (5 << 16) | 12, Opcode::Lwu),
            ((0x28 << 26) | (4 << 21) | (5 << 16) | 1, Opcode::Sb),
            ((0x29 << 26) | (4 << 21) | (5 << 16) | 2, Opcode::Sh),
            ((0x2B << 26) | (4 << 21) | (5 << 16) | 4, Opcode::Sw),
            ((0x22 << 26) | (4 << 21) | (5 << 16) | 0xFFFD, Opcode::Lwl),
            ((0x26 << 26) | (4 << 21) | (5 << 16) | 0xFFFE, Opcode::Lwr),
            ((0x2A << 26) | (4 << 21) | (5 << 16) | 1, Opcode::Swl),
            ((0x2E << 26) | (4 << 21) | (5 << 16) | 2, Opcode::Swr),
            ((0x1A << 26) | (4 << 21) | (5 << 16) | 0xFFFC, Opcode::Ldl),
            ((0x1B << 26) | (4 << 21) | (5 << 16) | 0xFFFD, Opcode::Ldr),
            ((0x2C << 26) | (4 << 21) | (5 << 16) | 3, Opcode::Sdl),
            ((0x2D << 26) | (4 << 21) | (5 << 16) | 4, Opcode::Sdr),
            ((0x37 << 26) | (4 << 21) | (5 << 16) | 0xFFF8, Opcode::Ld),
            ((0x3F << 26) | (4 << 21) | (5 << 16) | 0xFFF8, Opcode::Sd),
            ((0x1E << 26) | (4 << 21) | (5 << 16) | 8, Opcode::Lq),
            ((0x1F << 26) | (4 << 21) | (5 << 16) | 12, Opcode::Sq),
        ];
        for (word, expected) in cases {
            let decoded = decode(word, 0x1000);
            assert_eq!(decoded.opcode, expected);
            assert_eq!(decoded.rs, 4);
            assert_eq!(decoded.rt, 5);
        }
        assert_eq!(decode((0x20 << 26) | (4 << 21) | (5 << 16) | 0xFFFC, 0).signed_immediate(), -4);
        assert_eq!(decode((0x37 << 26) | (4 << 21) | (5 << 16) | 0xFFF8, 0).signed_immediate(), -8);
    }

    #[test]
    fn rejects_invalid_reserved_fields_and_unknown_ops() {
        let bad_jr = (4 << 21) | (1 << 16) | 0x08;
        assert!(!decode(bad_jr, 0).is_supported());
        assert_eq!(decode(0xFFFF_FFFF, 0).opcode, Opcode::Sd);
        assert!(!decode(0xEC00_0000, 0).is_supported());
    }

    #[test]
    fn jump_region_uses_pc_plus_four() {
        let decoded = decode((0x03 << 26) | 1, 0x8000_0000);
        assert_eq!(decoded.opcode, Opcode::Jal);
        assert_eq!(decoded.target, 0x8000_0004);
    }

    #[test]
    fn decodes_regimm_signed_branches_and_link_variant() {
        let bltz = (0x01 << 26) | (8 << 21) | 2;
        let decoded_bltz = decode(bltz, 0x1000);
        assert_eq!(decoded_bltz.opcode, Opcode::Bltz);
        assert!(decoded_bltz.has_delay_slot);
        assert_eq!(decoded_bltz.target, 0x100C);

        let bgezal = (0x01 << 26) | (9 << 21) | (0x11 << 16) | 0xFFFE;
        let decoded_bgezal = decode(bgezal, 0x2000);
        assert_eq!(decoded_bgezal.opcode, Opcode::Bgezal);
        assert!(decoded_bgezal.has_delay_slot);
        assert_eq!(decoded_bgezal.target, 0x1FFC);
        assert!(decoded_bgezal.is_control_transfer());
    }
}
