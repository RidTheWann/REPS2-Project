//! Typed, host-independent IR for a deliberately bounded scalar EE subset.

use std::fmt;

use crate::mips::{self, Opcode};

/// A validated architectural GPR index.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Gpr(u8);

impl Gpr {
    /// Constructs a GPR index, rejecting values outside the 32-register file.
    pub fn new(index: u8) -> Result<Self, VerifyError> {
        if index < 32 {
            Ok(Self(index))
        } else {
            Err(VerifyError::InvalidRegister(index))
        }
    }

    /// Returns the architectural register number.
    #[must_use]
    pub const fn index(self) -> u8 {
        self.0
    }

    /// Returns true for the architectural hard-wired zero register.
    #[must_use]
    pub const fn is_zero(self) -> bool {
        self.0 == 0
    }
}

/// Operations whose guest-visible behavior is explicitly represented by this
/// initial IR. Word operations write a sign-extended 32-bit result into the
/// low 64 bits of a 128-bit EE GPR. Logical operations update only GPR[63:0].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Operation {
    Nop,
    MoveFromHi { dst: Gpr },
    MoveFromLo { dst: Gpr },
    MoveToHi { src: Gpr },
    MoveToLo { src: Gpr },
    MultiplyWordSigned { dst: Gpr, lhs: Gpr, rhs: Gpr },
    MultiplyWordUnsigned { dst: Gpr, lhs: Gpr, rhs: Gpr },
    Nor64 { dst: Gpr, lhs: Gpr, rhs: Gpr },
    SetLessThanSigned64 { dst: Gpr, lhs: Gpr, rhs: Gpr },
    SetLessThanUnsigned64 { dst: Gpr, lhs: Gpr, rhs: Gpr },
    SetLessThanImmediateSigned64 { dst: Gpr, src: Gpr, immediate: u16 },
    SetLessThanImmediateUnsigned64 { dst: Gpr, src: Gpr, immediate: u16 },
    AddWord { dst: Gpr, lhs: Gpr, rhs: Gpr },
    AddImmediateWord { dst: Gpr, src: Gpr, immediate: u16 },
    AddUnsignedWord { dst: Gpr, lhs: Gpr, rhs: Gpr },
    AddImmediateUnsignedWord { dst: Gpr, src: Gpr, immediate: u16 },
    SubWord { dst: Gpr, lhs: Gpr, rhs: Gpr },
    SubUnsignedWord { dst: Gpr, lhs: Gpr, rhs: Gpr },
    AddDoublewordSigned { dst: Gpr, lhs: Gpr, rhs: Gpr },
    SubDoublewordSigned { dst: Gpr, lhs: Gpr, rhs: Gpr },
    AddImmediateDoublewordSigned { dst: Gpr, src: Gpr, immediate: u16 },
    AddDoublewordUnsigned { dst: Gpr, lhs: Gpr, rhs: Gpr },
    SubDoublewordUnsigned { dst: Gpr, lhs: Gpr, rhs: Gpr },
    AddImmediateDoublewordUnsigned { dst: Gpr, src: Gpr, immediate: u16 },
    ShiftLeftDoubleword { dst: Gpr, src: Gpr, amount: u8 },
    ShiftRightLogicalDoubleword { dst: Gpr, src: Gpr, amount: u8 },
    ShiftRightArithmeticDoubleword { dst: Gpr, src: Gpr, amount: u8 },
    ShiftLeftWordVariable { dst: Gpr, src: Gpr, amount: Gpr },
    ShiftRightLogicalWordVariable { dst: Gpr, src: Gpr, amount: Gpr },
    ShiftRightArithmeticWordVariable { dst: Gpr, src: Gpr, amount: Gpr },
    ShiftLeftDoublewordVariable { dst: Gpr, src: Gpr, amount: Gpr },
    ShiftRightLogicalDoublewordVariable { dst: Gpr, src: Gpr, amount: Gpr },
    ShiftRightArithmeticDoublewordVariable { dst: Gpr, src: Gpr, amount: Gpr },
    And64 { dst: Gpr, lhs: Gpr, rhs: Gpr },
    Or64 { dst: Gpr, lhs: Gpr, rhs: Gpr },
    Xor64 { dst: Gpr, lhs: Gpr, rhs: Gpr },
    AndImmediate64 { dst: Gpr, src: Gpr, immediate: u16 },
    OrImmediate64 { dst: Gpr, src: Gpr, immediate: u16 },
    XorImmediate64 { dst: Gpr, src: Gpr, immediate: u16 },
    LoadUpperWord { dst: Gpr, immediate: u16 },
    ShiftLeftWord { dst: Gpr, src: Gpr, amount: u8 },
    ShiftRightLogicalWord { dst: Gpr, src: Gpr, amount: u8 },
    ShiftRightArithmeticWord { dst: Gpr, src: Gpr, amount: u8 },
    LoadByteSigned { dst: Gpr, base: Gpr, offset: u16 },
    LoadByteUnsigned { dst: Gpr, base: Gpr, offset: u16 },
    LoadHalfSigned { dst: Gpr, base: Gpr, offset: u16 },
    LoadHalfUnsigned { dst: Gpr, base: Gpr, offset: u16 },
    LoadWordSigned { dst: Gpr, base: Gpr, offset: u16 },
    LoadWordUnsigned { dst: Gpr, base: Gpr, offset: u16 },
    StoreByte { src: Gpr, base: Gpr, offset: u16 },
    StoreHalf { src: Gpr, base: Gpr, offset: u16 },
    StoreWord { src: Gpr, base: Gpr, offset: u16 },
    LoadWordLeft { dst: Gpr, base: Gpr, offset: u16 },
    LoadWordRight { dst: Gpr, base: Gpr, offset: u16 },
    StoreWordLeft { src: Gpr, base: Gpr, offset: u16 },
    StoreWordRight { src: Gpr, base: Gpr, offset: u16 },
    LoadDoublewordLeft { dst: Gpr, base: Gpr, offset: u16 },
    LoadDoublewordRight { dst: Gpr, base: Gpr, offset: u16 },
    StoreDoublewordLeft { src: Gpr, base: Gpr, offset: u16 },
    StoreDoublewordRight { src: Gpr, base: Gpr, offset: u16 },
    LoadDoubleword { dst: Gpr, base: Gpr, offset: u16 },
    StoreDoubleword { src: Gpr, base: Gpr, offset: u16 },
    LoadQuadword { dst: Gpr, base: Gpr, offset: u16 },
    StoreQuadword { src: Gpr, base: Gpr, offset: u16 },
}

/// One IR operation with its original guest instruction address.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Instruction {
    pub guest_pc: u32,
    pub operation: Operation,
}

/// A straight-line basic block. Control-flow and delay slots are not silently
/// flattened into this type; they remain outside this initial IR subset.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Program {
    instructions: Vec<Instruction>,
}

impl Program {
    /// Creates a verified straight-line program.
    pub fn new(instructions: Vec<Instruction>) -> Result<Self, VerifyError> {
        let program = Self { instructions };
        program.verify()?;
        Ok(program)
    }

    /// Returns the immutable IR instruction sequence.
    #[must_use]
    pub fn instructions(&self) -> &[Instruction] {
        &self.instructions
    }

    /// Verifies source-PC alignment, sequential layout, and instruction
    /// operand invariants before code generation.
    pub fn verify(&self) -> Result<(), VerifyError> {
        for (index, instruction) in self.instructions.iter().enumerate() {
            if instruction.guest_pc & 3 != 0 {
                return Err(VerifyError::MisalignedGuestPc(instruction.guest_pc));
            }
            if index > 0 {
                let previous = self.instructions[index - 1].guest_pc;
                let expected = previous
                    .checked_add(4)
                    .ok_or(VerifyError::GuestPcOverflow(previous))?;
                if instruction.guest_pc != expected {
                    return Err(VerifyError::NonSequentialGuestPc {
                        previous,
                        current: instruction.guest_pc,
                    });
                }
            }
            verify_operation(instruction.operation)?;
        }
        Ok(())
    }
}

/// Instruction lifting failure. Unsupported operations are not converted to
/// no-ops, host calls, or guessed behavior.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum LiftError {
    UnsupportedOpcode { pc: u32, mnemonic: &'static str },
    Verification(VerifyError),
}

impl fmt::Display for LiftError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnsupportedOpcode { pc, mnemonic } => {
                write!(f, "unsupported opcode {mnemonic} at guest PC 0x{pc:08X}")
            }
            Self::Verification(error) => write!(f, "IR verification failed: {error}"),
        }
    }
}

impl std::error::Error for LiftError {}

/// IR verification failure.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum VerifyError {
    InvalidRegister(u8),
    MisalignedGuestPc(u32),
    GuestPcOverflow(u32),
    NonSequentialGuestPc { previous: u32, current: u32 },
    InvalidShiftAmount(u8),
}

impl fmt::Display for VerifyError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidRegister(register) => write!(f, "invalid GPR index {register}"),
            Self::MisalignedGuestPc(pc) => write!(f, "unaligned guest PC 0x{pc:08X}"),
            Self::GuestPcOverflow(pc) => write!(f, "guest PC overflows after 0x{pc:08X}"),
            Self::NonSequentialGuestPc { previous, current } => write!(
                f,
                "linear IR is not sequential: 0x{previous:08X} is followed by 0x{current:08X}"
            ),
            Self::InvalidShiftAmount(amount) => write!(f, "invalid word shift amount {amount}"),
        }
    }
}

impl std::error::Error for VerifyError {}

fn verify_operation(operation: Operation) -> Result<(), VerifyError> {
    let shift = match operation {
        Operation::ShiftLeftWord { amount, .. }
        | Operation::ShiftRightLogicalWord { amount, .. }
        | Operation::ShiftRightArithmeticWord { amount, .. } => Some((amount, 32)),
        Operation::ShiftLeftDoubleword { amount, .. }
        | Operation::ShiftRightLogicalDoubleword { amount, .. }
        | Operation::ShiftRightArithmeticDoubleword { amount, .. } => Some((amount, 64)),
        _ => None,
    };
    if let Some((amount, limit)) = shift
        && amount >= limit
    {
        return Err(VerifyError::InvalidShiftAmount(amount));
    }
    Ok(())
}

fn gpr(index: u8) -> Gpr {
    // Decoder register fields are five bits wide by construction.
    Gpr(index & 0x1F)
}

/// Lifts a sequential decoded instruction stream into the supported IR.
/// Branches, memory operations, exceptions and other unsupported semantics
/// are returned as an explicit error.
pub fn lift(decoded: &[mips::Instruction]) -> Result<Program, LiftError> {
    let mut instructions = Vec::with_capacity(decoded.len());
    for instruction in decoded {
        let operation = match instruction.opcode {
            Opcode::Nop => Operation::Nop,
            Opcode::Mfhi => Operation::MoveFromHi {
                dst: gpr(instruction.rd),
            },
            Opcode::Mflo => Operation::MoveFromLo {
                dst: gpr(instruction.rd),
            },
            Opcode::Mthi => Operation::MoveToHi {
                src: gpr(instruction.rs),
            },
            Opcode::Mtlo => Operation::MoveToLo {
                src: gpr(instruction.rs),
            },
            Opcode::Mult => Operation::MultiplyWordSigned {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Multu => Operation::MultiplyWordUnsigned {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Nor => Operation::Nor64 {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Slt => Operation::SetLessThanSigned64 {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Sltu => Operation::SetLessThanUnsigned64 {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Slti => Operation::SetLessThanImmediateSigned64 {
                dst: gpr(instruction.rt),
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Sltiu => Operation::SetLessThanImmediateUnsigned64 {
                dst: gpr(instruction.rt),
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Add => Operation::AddWord {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Addu => Operation::AddUnsignedWord {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Sub => Operation::SubWord {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Subu => Operation::SubUnsignedWord {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Dadd => Operation::AddDoublewordSigned {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Dsub => Operation::SubDoublewordSigned {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Daddi => Operation::AddImmediateDoublewordSigned {
                dst: gpr(instruction.rt),
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Daddu => Operation::AddDoublewordUnsigned {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Dsubu => Operation::SubDoublewordUnsigned {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Daddiu => Operation::AddImmediateDoublewordUnsigned {
                dst: gpr(instruction.rt),
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Dsll => Operation::ShiftLeftDoubleword {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: instruction.shift_amount,
            },
            Opcode::Dsrl => Operation::ShiftRightLogicalDoubleword {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: instruction.shift_amount,
            },
            Opcode::Dsra => Operation::ShiftRightArithmeticDoubleword {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: instruction.shift_amount,
            },
            Opcode::Dsll32 => Operation::ShiftLeftDoubleword {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: instruction.shift_amount + 32,
            },
            Opcode::Dsrl32 => Operation::ShiftRightLogicalDoubleword {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: instruction.shift_amount + 32,
            },
            Opcode::Dsra32 => Operation::ShiftRightArithmeticDoubleword {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: instruction.shift_amount + 32,
            },
            Opcode::Sllv => Operation::ShiftLeftWordVariable {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: gpr(instruction.rs),
            },
            Opcode::Srlv => Operation::ShiftRightLogicalWordVariable {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: gpr(instruction.rs),
            },
            Opcode::Srav => Operation::ShiftRightArithmeticWordVariable {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: gpr(instruction.rs),
            },
            Opcode::Dsllv => Operation::ShiftLeftDoublewordVariable {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: gpr(instruction.rs),
            },
            Opcode::Dsrlv => Operation::ShiftRightLogicalDoublewordVariable {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: gpr(instruction.rs),
            },
            Opcode::Dsrav => Operation::ShiftRightArithmeticDoublewordVariable {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: gpr(instruction.rs),
            },
            Opcode::And => Operation::And64 {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Or => Operation::Or64 {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Xor => Operation::Xor64 {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::Addi => Operation::AddImmediateWord {
                dst: gpr(instruction.rt),
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Addiu => Operation::AddImmediateUnsignedWord {
                dst: gpr(instruction.rt),
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Andi => Operation::AndImmediate64 {
                dst: gpr(instruction.rt),
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Ori => Operation::OrImmediate64 {
                dst: gpr(instruction.rt),
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Xori => Operation::XorImmediate64 {
                dst: gpr(instruction.rt),
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Lui => Operation::LoadUpperWord {
                dst: gpr(instruction.rt),
                immediate: instruction.immediate,
            },
            Opcode::Sll => Operation::ShiftLeftWord {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: instruction.shift_amount,
            },
            Opcode::Srl => Operation::ShiftRightLogicalWord {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: instruction.shift_amount,
            },
            Opcode::Sra => Operation::ShiftRightArithmeticWord {
                dst: gpr(instruction.rd),
                src: gpr(instruction.rt),
                amount: instruction.shift_amount,
            },
            Opcode::Lb => Operation::LoadByteSigned {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Lbu => Operation::LoadByteUnsigned {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Lh => Operation::LoadHalfSigned {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Lhu => Operation::LoadHalfUnsigned {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Lw => Operation::LoadWordSigned {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Lwu => Operation::LoadWordUnsigned {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Sb => Operation::StoreByte {
                src: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Sh => Operation::StoreHalf {
                src: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Sw => Operation::StoreWord {
                src: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Lwl => Operation::LoadWordLeft {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Lwr => Operation::LoadWordRight {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Swl => Operation::StoreWordLeft {
                src: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Swr => Operation::StoreWordRight {
                src: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Ldl => Operation::LoadDoublewordLeft {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Ldr => Operation::LoadDoublewordRight {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Sdl => Operation::StoreDoublewordLeft {
                src: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Sdr => Operation::StoreDoublewordRight {
                src: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Ld => Operation::LoadDoubleword {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Sd => Operation::StoreDoubleword {
                src: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Lq => Operation::LoadQuadword {
                dst: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::Sq => Operation::StoreQuadword {
                src: gpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            other => {
                return Err(LiftError::UnsupportedOpcode {
                    pc: instruction.pc,
                    mnemonic: other.mnemonic(),
                });
            }
        };
        instructions.push(Instruction {
            guest_pc: instruction.pc,
            operation,
        });
    }

    Program::new(instructions).map_err(LiftError::Verification)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn decoded(word: u32, pc: u32) -> mips::Instruction {
        mips::decode(word, pc)
    }

    #[test]
    fn lifts_a_supported_straight_line_subset() {
        let stream = [
            decoded((0x09 << 26) | (8 << 16) | 5, 0x1000),
            decoded((0x0F << 26) | (9 << 16) | 0x8000, 0x1004),
            decoded((0x0D << 26) | (10 << 16) | 0x1234, 0x1008),
        ];
        let program = lift(&stream).expect("supported operations lift");
        assert_eq!(program.instructions().len(), 3);
        assert_eq!(program.instructions()[0].guest_pc, 0x1000);
    }

    #[test]
    fn refuses_control_flow_in_a_straight_line_block() {
        let branch = decoded((0x04 << 26) | 4, 0x2000);
        let error = lift(&[branch]).expect_err("branch needs a CFG/delay-slot representation");
        assert_eq!(
            error,
            LiftError::UnsupportedOpcode {
                pc: 0x2000,
                mnemonic: "beq"
            }
        );
    }

    #[test]
    fn lifts_load_store_family_with_signed_offsets() {
        let words = [
            (0x20 << 26) | (4 << 21) | (5 << 16) | 0xFFFC,
            (0x24 << 26) | (4 << 21) | (6 << 16) | 1,
            (0x21 << 26) | (4 << 21) | (7 << 16) | 2,
            (0x25 << 26) | (4 << 21) | (8 << 16) | 4,
            (0x23 << 26) | (4 << 21) | (9 << 16) | 8,
            (0x27 << 26) | (4 << 21) | (10 << 16) | 12,
            (0x28 << 26) | (4 << 21) | (5 << 16) | 1,
            (0x29 << 26) | (4 << 21) | (6 << 16) | 2,
            (0x2B << 26) | (4 << 21) | (7 << 16) | 4,
            (0x1E << 26) | (4 << 21) | (11 << 16) | 8,
            (0x1F << 26) | (4 << 21) | (12 << 16) | 12,
        ];
        let stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| decoded(*word, 0x2000 + (index as u32 * 4)))
            .collect::<Vec<_>>();
        let program = lift(&stream).expect("load/store scalar IR");
        assert!(matches!(
            program.instructions()[0].operation,
            Operation::LoadByteSigned { dst, base, offset: 0xFFFC }
                if dst.index() == 5 && base.index() == 4
        ));
        assert!(matches!(
            program.instructions()[4].operation,
            Operation::LoadWordSigned { dst, base, offset: 8 }
                if dst.index() == 9 && base.index() == 4
        ));
        assert!(matches!(
            program.instructions()[5].operation,
            Operation::LoadWordUnsigned { dst, base, offset: 12 }
                if dst.index() == 10 && base.index() == 4
        ));
        assert!(matches!(
            program.instructions()[8].operation,
            Operation::StoreWord { src, base, offset: 4 }
                if src.index() == 7 && base.index() == 4
        ));
        assert!(matches!(
            program.instructions()[9].operation,
            Operation::LoadQuadword { dst, base, offset: 8 }
                if dst.index() == 11 && base.index() == 4
        ));
        assert!(matches!(
            program.instructions()[10].operation,
            Operation::StoreQuadword { src, base, offset: 12 }
                if src.index() == 12 && base.index() == 4
        ));
    }

    #[test]
    fn lifts_r5900_doubleword_integer_operations() {
        let words = [
            (8 << 21) | (9 << 16) | (10 << 11) | 0x2D,
            (8 << 21) | (9 << 16) | (11 << 11) | 0x2F,
            (0x19 << 26) | (8 << 21) | (12 << 16) | 0xFFF0,
            (9 << 16) | (13 << 11) | (7 << 6) | 0x38,
            (9 << 16) | (14 << 11) | (7 << 6) | 0x3E,
            (9 << 16) | (15 << 11) | (7 << 6) | 0x3F,
        ];
        let stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| decoded(*word, 0x3000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = lift(&stream).expect("R5900 doubleword operations lift");
        assert!(matches!(
            program.instructions()[0].operation,
            Operation::AddDoublewordUnsigned { dst, lhs, rhs }
                if dst.index() == 10 && lhs.index() == 8 && rhs.index() == 9
        ));
        assert!(matches!(
            program.instructions()[1].operation,
            Operation::SubDoublewordUnsigned { dst, .. } if dst.index() == 11
        ));
        assert!(matches!(
            program.instructions()[2].operation,
            Operation::AddImmediateDoublewordUnsigned { dst, src, immediate: 0xFFF0 }
                if dst.index() == 12 && src.index() == 8
        ));
        assert!(matches!(
            program.instructions()[3].operation,
            Operation::ShiftLeftDoubleword { amount: 7, .. }
        ));
        assert!(matches!(
            program.instructions()[4].operation,
            Operation::ShiftRightLogicalDoubleword { amount: 39, .. }
        ));
        assert!(matches!(
            program.instructions()[5].operation,
            Operation::ShiftRightArithmeticDoubleword { amount: 39, .. }
        ));
    }

    #[test]
    fn lifts_register_variable_word_and_doubleword_shifts() {
        let words = [
            (4 << 21) | (5 << 16) | (6 << 11) | 0x04,
            (4 << 21) | (5 << 16) | (7 << 11) | 0x06,
            (4 << 21) | (5 << 16) | (8 << 11) | 0x07,
            (4 << 21) | (5 << 16) | (30 << 11) | 0x14,
            (4 << 21) | (5 << 16) | (31 << 11) | 0x16,
            (4 << 21) | (5 << 16) | (22 << 11) | 0x17,
        ];
        let decoded = words
            .iter()
            .enumerate()
            .map(|(index, word)| decoded(*word, 0x7000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = lift(&decoded).expect("variable shifts lift");
        assert!(matches!(
            program.instructions()[0].operation,
            Operation::ShiftLeftWordVariable { dst, src, amount }
                if dst.index() == 6 && src.index() == 5 && amount.index() == 4
        ));
        assert!(matches!(
            program.instructions()[1].operation,
            Operation::ShiftRightLogicalWordVariable { dst, src, amount }
                if dst.index() == 7 && src.index() == 5 && amount.index() == 4
        ));
        assert!(matches!(
            program.instructions()[2].operation,
            Operation::ShiftRightArithmeticWordVariable { dst, src, amount }
                if dst.index() == 8 && src.index() == 5 && amount.index() == 4
        ));
        assert!(matches!(
            program.instructions()[3].operation,
            Operation::ShiftLeftDoublewordVariable { dst, src, amount }
                if dst.index() == 30 && src.index() == 5 && amount.index() == 4
        ));
        assert!(matches!(
            program.instructions()[4].operation,
            Operation::ShiftRightLogicalDoublewordVariable { dst, src, amount }
                if dst.index() == 31 && src.index() == 5 && amount.index() == 4
        ));
        assert!(matches!(
            program.instructions()[5].operation,
            Operation::ShiftRightArithmeticDoublewordVariable { dst, src, amount }
                if dst.index() == 22 && src.index() == 5 && amount.index() == 4
        ));
    }

    #[test]
    fn verifier_rejects_doubleword_shift_amount_outside_sixty_four_bit_range() {
        let invalid = Instruction {
            guest_pc: 0x1000,
            operation: Operation::ShiftLeftDoubleword {
                dst: gpr(2),
                src: gpr(3),
                amount: 64,
            },
        };
        assert_eq!(
            Program::new(vec![invalid]),
            Err(VerifyError::InvalidShiftAmount(64))
        );
    }

    #[test]
    fn verifier_rejects_non_sequential_or_misaligned_guest_pc() {
        let first = Instruction {
            guest_pc: 0x1000,
            operation: Operation::Nop,
        };
        let second = Instruction {
            guest_pc: 0x1008,
            operation: Operation::Nop,
        };
        assert!(matches!(
            Program::new(vec![first, second]),
            Err(VerifyError::NonSequentialGuestPc { .. })
        ));
    }

    #[test]
    fn lifts_r5900_hilo_moves_and_three_operand_multiply() {
        let words = [
            (4 << 21) | 0x11,
            (12 << 11) | 0x10,
            (5 << 21) | 0x13,
            (13 << 11) | 0x12,
            (4 << 21) | (5 << 16) | (6 << 11) | 0x18,
            (4 << 21) | (5 << 16) | (7 << 11) | 0x19,
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| decoded(*word, 0x8000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = lift(&decoded_stream).expect("R5900 HI/LO and multiply IR");
        assert!(
            matches!(program.instructions()[0].operation, Operation::MoveToHi { src } if src.index() == 4)
        );
        assert!(
            matches!(program.instructions()[1].operation, Operation::MoveFromHi { dst } if dst.index() == 12)
        );
        assert!(
            matches!(program.instructions()[2].operation, Operation::MoveToLo { src } if src.index() == 5)
        );
        assert!(
            matches!(program.instructions()[3].operation, Operation::MoveFromLo { dst } if dst.index() == 13)
        );
        assert!(matches!(program.instructions()[4].operation,
            Operation::MultiplyWordSigned { dst, lhs, rhs } if dst.index() == 6 && lhs.index() == 4 && rhs.index() == 5));
        assert!(matches!(program.instructions()[5].operation,
            Operation::MultiplyWordUnsigned { dst, lhs, rhs } if dst.index() == 7 && lhs.index() == 4 && rhs.index() == 5));
    }

    #[test]
    fn lifts_r5900_signed_unsigned_comparisons_and_nor() {
        let words = [
            (4 << 21) | (5 << 16) | (6 << 11) | 0x27, // nor r6, r4, r5
            (4 << 21) | (5 << 16) | (7 << 11) | 0x2A, // slt r7, r4, r5
            (4 << 21) | (5 << 16) | (8 << 11) | 0x2B, // sltu r8, r4, r5
            (0x0A << 26) | (9 << 21) | (10 << 16) | 0x8001, // slti r10, r9, -32767
            (0x0B << 26) | (11 << 21) | (12 << 16) | 0xFFFF, // sltiu r12, r11, -1
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| decoded(*word, 0xA000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = lift(&decoded_stream).expect("R5900 comparisons and NOR lift");

        assert!(matches!(program.instructions()[0].operation,
            Operation::Nor64 { dst, lhs, rhs }
                if dst.index() == 6 && lhs.index() == 4 && rhs.index() == 5));
        assert!(matches!(program.instructions()[1].operation,
            Operation::SetLessThanSigned64 { dst, lhs, rhs }
                if dst.index() == 7 && lhs.index() == 4 && rhs.index() == 5));
        assert!(matches!(program.instructions()[2].operation,
            Operation::SetLessThanUnsigned64 { dst, lhs, rhs }
                if dst.index() == 8 && lhs.index() == 4 && rhs.index() == 5));
        assert!(matches!(program.instructions()[3].operation,
            Operation::SetLessThanImmediateSigned64 { dst, src, immediate: 0x8001 }
                if dst.index() == 10 && src.index() == 9));
        assert!(matches!(program.instructions()[4].operation,
            Operation::SetLessThanImmediateUnsigned64 { dst, src, immediate: 0xFFFF }
                if dst.index() == 12 && src.index() == 11));
    }

    #[test]
    fn lifts_trapping_signed_doubleword_arithmetic_separately_from_unsigned_ops() {
        let words = [
            (8 << 21) | (9 << 16) | (10 << 11) | 0x2C,
            (12 << 21) | (13 << 16) | (11 << 11) | 0x2E,
            (0x18 << 26) | (15 << 21) | (14 << 16) | 0xFFFE,
            (8 << 21) | (9 << 16) | (16 << 11) | 0x2D,
            (8 << 21) | (9 << 16) | (17 << 11) | 0x2F,
            (0x19 << 26) | (15 << 21) | (18 << 16) | 0xFFFE,
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| decoded(*word, 0xB000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = lift(&decoded_stream).expect("signed and unsigned doubleword IR");

        assert!(matches!(program.instructions()[0].operation,
            Operation::AddDoublewordSigned { dst, lhs, rhs }
                if dst.index() == 10 && lhs.index() == 8 && rhs.index() == 9));
        assert!(matches!(program.instructions()[1].operation,
            Operation::SubDoublewordSigned { dst, lhs, rhs }
                if dst.index() == 11 && lhs.index() == 12 && rhs.index() == 13));
        assert!(matches!(program.instructions()[2].operation,
            Operation::AddImmediateDoublewordSigned { dst, src, immediate: 0xFFFE }
                if dst.index() == 14 && src.index() == 15));
        assert!(matches!(program.instructions()[3].operation,
            Operation::AddDoublewordUnsigned { dst, .. } if dst.index() == 16));
        assert!(matches!(program.instructions()[4].operation,
            Operation::SubDoublewordUnsigned { dst, .. } if dst.index() == 17));
        assert!(matches!(program.instructions()[5].operation,
            Operation::AddImmediateDoublewordUnsigned { dst, .. } if dst.index() == 18));
    }
}
