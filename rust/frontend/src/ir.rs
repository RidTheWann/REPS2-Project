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

/// A validated FPU register index. FPR[0] holds a value, so unlike `Gpr` this
/// type has no zero-register special case.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Fpr(u8);

impl Fpr {
    /// Returns the architectural register number.
    #[must_use]
    pub const fn index(self) -> u8 {
        self.0
    }
}

/// The only FPU control registers the EE defines. FCR1..FCR30 are reserved and
/// accessing them is not representable in this IR.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FpuControl {
    /// Read-only implementation register.
    Fir,
    /// The single condition, flag and rounding-field register.
    Fcsr,
}

/// Operations whose guest-visible behavior is explicitly represented by this
/// initial IR. Scalar word operations write a sign-extended 32-bit result into
/// the low 64 bits of a 128-bit EE GPR. Parallel word operations update all
/// four 32-bit lanes, and logical operations update only GPR[63:0].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Operation {
    Nop,
    MoveFromHi { dst: Gpr },
    MoveFromLo { dst: Gpr },
    MoveToHi { src: Gpr },
    MoveToLo { src: Gpr },
    MoveFromHi1 { dst: Gpr },
    MoveFromLo1 { dst: Gpr },
    MoveToHi1 { src: Gpr },
    MoveToLo1 { src: Gpr },
    MoveFromSa { dst: Gpr },
    MoveToSa { src: Gpr },
    MoveToSaByte { src: Gpr, immediate: u16 },
    MoveToSaHalfword { src: Gpr, immediate: u16 },
    FunnelShiftRightVec { dst: Gpr, lhs: Gpr, rhs: Gpr },
    MoveFromFpuRegister { dst: Gpr, src: Fpr },
    MoveToFpuRegister { dst: Fpr, src: Gpr },
    MoveFromFpuControl { dst: Gpr, register: FpuControl },
    MoveToFpuControl { src: Gpr, register: FpuControl },
    LoadFpuWord { dst: Fpr, base: Gpr, offset: u16 },
    StoreFpuWord { src: Fpr, base: Gpr, offset: u16 },
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
    ParallelAddUnsignedWord { dst: Gpr, lhs: Gpr, rhs: Gpr },
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

fn fpr(index: u8) -> Fpr {
    // Decoder register fields are five bits wide by construction.
    Fpr(index & 0x1F)
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
            Opcode::Mfhi1 => Operation::MoveFromHi1 {
                dst: gpr(instruction.rd),
            },
            Opcode::Mflo1 => Operation::MoveFromLo1 {
                dst: gpr(instruction.rd),
            },
            Opcode::Mthi1 => Operation::MoveToHi1 {
                src: gpr(instruction.rs),
            },
            Opcode::Mtlo1 => Operation::MoveToLo1 {
                src: gpr(instruction.rs),
            },
            Opcode::Mfsa => Operation::MoveFromSa {
                dst: gpr(instruction.rd),
            },
            Opcode::Mtsa => Operation::MoveToSa {
                src: gpr(instruction.rs),
            },
            Opcode::Mtsab => Operation::MoveToSaByte {
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
            },
            Opcode::Mtsah => Operation::MoveToSaHalfword {
                src: gpr(instruction.rs),
                immediate: instruction.immediate,
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
            Opcode::ParallelAddUnsignedWord => Operation::ParallelAddUnsignedWord {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::FunnelShiftRightVec => Operation::FunnelShiftRightVec {
                dst: gpr(instruction.rd),
                lhs: gpr(instruction.rs),
                rhs: gpr(instruction.rt),
            },
            Opcode::MoveFromFpuRegister => Operation::MoveFromFpuRegister {
                dst: gpr(instruction.rt),
                src: fpr(instruction.rd),
            },
            Opcode::MoveToFpuRegister => Operation::MoveToFpuRegister {
                dst: fpr(instruction.rd),
                src: gpr(instruction.rt),
            },
            // Only FIR (FCR0) and FCSR (FCR31) are representable; the decoder
            // already rejects other control numbers, and a reserved number must
            // not be silently folded onto FCSR.
            Opcode::MoveFromFpuControl => match instruction.rd {
                0 => Operation::MoveFromFpuControl {
                    dst: gpr(instruction.rt),
                    register: FpuControl::Fir,
                },
                31 => Operation::MoveFromFpuControl {
                    dst: gpr(instruction.rt),
                    register: FpuControl::Fcsr,
                },
                _ => {
                    return Err(LiftError::UnsupportedOpcode {
                        pc: instruction.pc,
                        mnemonic: instruction.opcode.mnemonic(),
                    });
                }
            },
            Opcode::MoveToFpuControl => match instruction.rd {
                31 => Operation::MoveToFpuControl {
                    src: gpr(instruction.rt),
                    register: FpuControl::Fcsr,
                },
                _ => {
                    return Err(LiftError::UnsupportedOpcode {
                        pc: instruction.pc,
                        mnemonic: instruction.opcode.mnemonic(),
                    });
                }
            },
            Opcode::LoadFpuWord => Operation::LoadFpuWord {
                dst: fpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
            },
            Opcode::StoreFpuWord => Operation::StoreFpuWord {
                src: fpr(instruction.rt),
                base: gpr(instruction.rs),
                offset: instruction.immediate,
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
    fn lifts_r5900_mmi_pipeline_one_hilo_moves() {
        let words = [
            (0x1C << 26) | (4 << 21) | 0x11,
            (0x1C << 26) | (5 << 21) | 0x13,
            (0x1C << 26) | (6 << 11) | 0x10,
            (0x1C << 26) | (7 << 11) | 0x12,
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| decoded(*word, 0xD000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = lift(&decoded_stream).expect("MMI pipeline-1 HI/LO moves lift");
        assert!(
            matches!(program.instructions()[0].operation, Operation::MoveToHi1 { src } if src.index() == 4)
        );
        assert!(
            matches!(program.instructions()[1].operation, Operation::MoveToLo1 { src } if src.index() == 5)
        );
        assert!(
            matches!(program.instructions()[2].operation, Operation::MoveFromHi1 { dst } if dst.index() == 6)
        );
        assert!(
            matches!(program.instructions()[3].operation, Operation::MoveFromLo1 { dst } if dst.index() == 7)
        );
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
    fn lifts_r5900_parallel_add_unsigned_word() {
        let words = [
            (0x1C << 26) | (1 << 21) | (2 << 16) | (3 << 11) | (0x10 << 6) | 0x28,
            (0x1C << 26) | (1 << 21) | (2 << 16) | (0x10 << 6) | 0x28,
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| decoded(*word, 0xC000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = lift(&decoded_stream).expect("PADDUW lifts into typed IR");

        assert!(matches!(program.instructions()[0].operation,
            Operation::ParallelAddUnsignedWord { dst, lhs, rhs }
                if dst.index() == 3 && lhs.index() == 1 && rhs.index() == 2));
        assert!(matches!(program.instructions()[1].operation,
            Operation::ParallelAddUnsignedWord { dst, .. } if dst.is_zero()));
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

    #[test]
    fn lifts_r5900_sa_instructions() {
        let stream = [
            mips::decode(0x0080_0029, 0x1000), // mtsa $4
            mips::decode((0x01 << 26) | (4 << 21) | (0x18 << 16) | 0x000F, 0x1004), // mtsab $4, 0xF
            mips::decode((0x01 << 26) | (4 << 21) | (0x19 << 16) | 0x0007, 0x1008), // mtsah $4, 0x7
            mips::decode(0x0000_2828, 0x100C), // mfsa $5
        ];
        let program = lift(&stream).expect("lift SA");
        assert_eq!(program.instructions().len(), 4);
        assert!(matches!(
            program.instructions()[0].operation,
            Operation::MoveToSa { src: Gpr(4) }
        ));
        assert!(matches!(
            program.instructions()[1].operation,
            Operation::MoveToSaByte {
                src: Gpr(4),
                immediate: 0xF
            }
        ));
        assert!(matches!(
            program.instructions()[2].operation,
            Operation::MoveToSaHalfword {
                src: Gpr(4),
                immediate: 0x7
            }
        ));
        assert!(matches!(
            program.instructions()[3].operation,
            Operation::MoveFromSa { dst: Gpr(5) }
        ));
    }

    #[test]
    fn lifts_mmi1_qfsrv_into_funnel_shift() {
        let stream = [mips::decode(0x7085_36E8, 0x1000)];
        let program = lift(&stream).expect("lift QFSRV");
        assert!(matches!(
            program.instructions()[0].operation,
            Operation::FunnelShiftRightVec {
                dst: Gpr(6),
                lhs: Gpr(4),
                rhs: Gpr(5)
            }
        ));
        program.verify().expect("QFSRV verifies");
    }

    #[test]
    fn lifts_cop1_transfers_onto_fpr_and_fcsr() {
        let stream = [
            mips::decode(0x4402_2800, 0x1000), // mfc1 $2, $f5
            mips::decode(0x4487_6000, 0x1004), // mtc1 $7, $f12
            mips::decode(0x4443_0000, 0x1008), // cfc1 $3, FIR
            mips::decode(0x4443_F800, 0x100C), // cfc1 $3, FCSR
            mips::decode(0x44C4_F800, 0x1010), // ctc1 $4, FCSR
            mips::decode(0xC506_0010, 0x1014), // lwc1 $f6, 0x10($8)
            mips::decode(0xE506_FFFC, 0x1018), // swc1 $f6, -4($8)
        ];
        let program = lift(&stream).expect("lift COP1 transfers");
        assert!(matches!(
            program.instructions()[0].operation,
            Operation::MoveFromFpuRegister {
                dst: Gpr(2),
                src: Fpr(5)
            }
        ));
        assert!(matches!(
            program.instructions()[1].operation,
            Operation::MoveToFpuRegister {
                dst: Fpr(12),
                src: Gpr(7)
            }
        ));
        assert!(matches!(
            program.instructions()[2].operation,
            Operation::MoveFromFpuControl {
                dst: Gpr(3),
                register: FpuControl::Fir
            }
        ));
        assert!(matches!(
            program.instructions()[3].operation,
            Operation::MoveFromFpuControl {
                dst: Gpr(3),
                register: FpuControl::Fcsr
            }
        ));
        assert!(matches!(
            program.instructions()[4].operation,
            Operation::MoveToFpuControl {
                src: Gpr(4),
                register: FpuControl::Fcsr
            }
        ));
        assert!(matches!(
            program.instructions()[5].operation,
            Operation::LoadFpuWord {
                dst: Fpr(6),
                base: Gpr(8),
                offset: 0x0010
            }
        ));
        assert!(matches!(
            program.instructions()[6].operation,
            Operation::StoreFpuWord {
                src: Fpr(6),
                base: Gpr(8),
                offset: 0xFFFC
            }
        ));
        program.verify().expect("COP1 transfers verify");

        // FPR[0] is a real register, so it must survive lifting rather than
        // being folded away the way GPR[0] is.
        let fpr_zero = mips::decode(0x4480_0000, 0x101C); // mtc1 $zero, $f0
        let program = lift(&[fpr_zero]).expect("lift FPR[0] write");
        assert!(matches!(
            program.instructions()[0].operation,
            Operation::MoveToFpuRegister {
                dst: Fpr(0),
                src: Gpr(0)
            }
        ));
    }

    #[test]
    fn refuses_cop1_control_numbers_the_ee_does_not_define() {
        // The decoder already rejects these, so the only way an undefined
        // control number reaches lifting is a future decoder widening. Folding
        // it onto FCSR would silently execute a different instruction.
        let mut undefined_read = mips::decode(0x4443_0000, 0x1100);
        undefined_read.rd = 5;
        let error = lift(&[undefined_read]).expect_err("CFC1 of FCR5 is not FCSR");
        assert_eq!(
            error,
            LiftError::UnsupportedOpcode {
                pc: 0x1100,
                mnemonic: "cfc1"
            }
        );

        let mut undefined_write = mips::decode(0x44C4_F800, 0x1104);
        undefined_write.rd = 0;
        let error = lift(&[undefined_write]).expect_err("CTC1 of FIR is not FCSR");
        assert_eq!(
            error,
            LiftError::UnsupportedOpcode {
                pc: 0x1104,
                mnemonic: "ctc1"
            }
        );
    }
}
