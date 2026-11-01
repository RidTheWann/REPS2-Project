//! Decoder for the explicitly implemented base-MIPS integer subset.

/// Decoded operation. Unsupported encodings remain Unknown and must never
/// be treated as no-ops by later compiler phases.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Opcode {
    Nop,
    Sll,
    Srl,
    Sra,
    Jr,
    Jalr,
    Syscall,
    Break,
    Mfhi,
    Mthi,
    Mflo,
    Mtlo,
    Mfhi1,
    Mthi1,
    Mflo1,
    Mtlo1,
    Mfsa,
    Mtsa,
    Mtsab,
    Mtsah,
    Mult,
    Multu,
    Div,
    Divu,
    Add,
    Addu,
    Sub,
    Subu,
    And,
    Or,
    Xor,
    Nor,
    Slt,
    Sltu,
    Dadd,
    Daddu,
    Dsub,
    Dsubu,
    Daddi,
    Daddiu,
    Dsll,
    Dsrl,
    Dsra,
    Dsll32,
    Dsrl32,
    Dsra32,
    Sllv,
    Srlv,
    Srav,
    Dsllv,
    Dsrlv,
    Dsrav,
    J,
    Jal,
    Beq,
    Bne,
    Blez,
    Bgtz,
    Bltz,
    Bgez,
    Bltzal,
    Bgezal,
    Beql,
    Bnel,
    Blezl,
    Bgtzl,
    Bltzl,
    Bgezl,
    Bltzall,
    Bgezall,
    Addi,
    Addiu,
    Slti,
    Sltiu,
    Andi,
    Ori,
    Xori,
    Lui,
    Lb,
    Lh,
    Lw,
    Lbu,
    Lhu,
    Lwu,
    Sb,
    Sh,
    Sw,
    Lwl,
    Lwr,
    Swl,
    Swr,
    Ldl,
    Ldr,
    Sdl,
    Sdr,
    Ld,
    Sd,
    Lq,
    Sq,
    ParallelAddUnsignedWord,
    FunnelShiftRightVec,
    MoveFromFpuRegister,
    MoveToFpuRegister,
    MoveFromFpuControl,
    MoveToFpuControl,
    LoadFpuWord,
    StoreFpuWord,
    // COP1 arithmetic. The S-class names the field in bits 15..11 fs and the
    // one in bits 20..16 ft, which is the same placement MFC1 uses for the FPR
    // number; Sony's SQRT.S takes its operand in ft and reserves fs, while
    // ABS.S, MOV.S, NEG.S and CVT.W.S take it in fs and reserve ft. Retail
    // code agrees with that reading without exception, and it is what makes the
    // reserved-zero fields in out/census-v4-cop1-fields.json line up.
    FpuAddSingle,
    FpuSubtractSingle,
    FpuMultiplySingle,
    FpuDivideSingle,
    FpuSquareRootSingle,
    FpuAbsoluteSingle,
    FpuMoveSingle,
    FpuNegateSingle,
    FpuReciprocalSquareRootSingle,
    FpuAddAccumulateSingle,
    FpuSubtractAccumulateSingle,
    FpuMultiplyAccumulateSingle,
    FpuMultiplyAddSingle,
    FpuMultiplySubtractSingle,
    FpuMultiplyAddAccumulateSingle,
    FpuMultiplySubtractAccumulateSingle,
    FpuMaximumSingle,
    FpuMinimumSingle,
    FpuConvertSingleToWord,
    FpuConvertWordToSingle,
    Unknown,
}

/// Broad instruction family used for deterministic coverage accounting.
///
/// The classification is based only on encoding fields. It does not imply that
/// an instruction is decoded, supported, or reachable.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum EncodingFamily {
    Special,
    RegImm,
    Jump,
    Branch,
    Immediate,
    Cop0,
    Cop1,
    Cop2,
    Mmi0,
    Mmi1,
    Mmi2,
    Mmi3,
    Mmi,
    LoadStore,
    Other,
}

impl EncodingFamily {
    /// Returns a stable lower-case label for diagnostic output.
    #[must_use]
    pub const fn label(self) -> &'static str {
        match self {
            Self::Special => "special",
            Self::RegImm => "regimm",
            Self::Jump => "jump",
            Self::Branch => "branch",
            Self::Immediate => "immediate",
            Self::Cop0 => "cop0",
            Self::Cop1 => "cop1",
            Self::Cop2 => "cop2",
            Self::Mmi0 => "mmi0",
            Self::Mmi1 => "mmi1",
            Self::Mmi2 => "mmi2",
            Self::Mmi3 => "mmi3",
            Self::Mmi => "mmi",
            Self::LoadStore => "load-store",
            Self::Other => "other",
        }
    }
}

/// Classifies a raw word into a broad encoding family for coverage summaries.
///
/// MMI groups follow the R5900 encoding: primary opcode `0x1C` with selectors
/// `0x08`, `0x09`, `0x28`, and `0x29` selecting MMI0, MMI2, MMI1, and MMI3.
#[must_use]
pub const fn encoding_family(word: u32) -> EncodingFamily {
    let primary = (word >> 26) as u8;
    match primary {
        0x00 => EncodingFamily::Special,
        0x01 => EncodingFamily::RegImm,
        0x02 | 0x03 => EncodingFamily::Jump,
        0x04..=0x07 | 0x14..=0x17 => EncodingFamily::Branch,
        0x08..=0x0F | 0x18 | 0x19 => EncodingFamily::Immediate,
        0x10 => EncodingFamily::Cop0,
        0x11 => EncodingFamily::Cop1,
        0x12 => EncodingFamily::Cop2,
        0x1C => match (word & 0x3F) as u8 {
            0x08 => EncodingFamily::Mmi0,
            0x09 => EncodingFamily::Mmi2,
            0x28 => EncodingFamily::Mmi1,
            0x29 => EncodingFamily::Mmi3,
            _ => EncodingFamily::Mmi,
        },
        0x1A | 0x1B | 0x1E | 0x1F | 0x20..=0x2E | 0x31 | 0x36 | 0x37 | 0x39 | 0x3E | 0x3F => {
            EncodingFamily::LoadStore
        }
        _ => EncodingFamily::Other,
    }
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
            Self::Mthi => "mthi",
            Self::Mflo => "mflo",
            Self::Mtlo => "mtlo",
            Self::Mfhi1 => "mfhi1",
            Self::Mthi1 => "mthi1",
            Self::Mflo1 => "mflo1",
            Self::Mtlo1 => "mtlo1",
            Self::Mfsa => "mfsa",
            Self::Mtsa => "mtsa",
            Self::Mtsab => "mtsab",
            Self::Mtsah => "mtsah",
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
            Self::Dadd => "dadd",
            Self::Daddu => "daddu",
            Self::Dsub => "dsub",
            Self::Dsubu => "dsubu",
            Self::Daddi => "daddi",
            Self::Daddiu => "daddiu",
            Self::Dsll => "dsll",
            Self::Dsrl => "dsrl",
            Self::Dsra => "dsra",
            Self::Dsll32 => "dsll32",
            Self::Dsrl32 => "dsrl32",
            Self::Dsra32 => "dsra32",
            Self::Sllv => "sllv",
            Self::Srlv => "srlv",
            Self::Srav => "srav",
            Self::Dsllv => "dsllv",
            Self::Dsrlv => "dsrlv",
            Self::Dsrav => "dsrav",
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
            Self::Beql => "beql",
            Self::Bnel => "bnel",
            Self::Blezl => "blezl",
            Self::Bgtzl => "bgtzl",
            Self::Bltzl => "bltzl",
            Self::Bgezl => "bgezl",
            Self::Bltzall => "bltzall",
            Self::Bgezall => "bgezall",
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
            Self::ParallelAddUnsignedWord => "padduw",
            Self::FunnelShiftRightVec => "qfsrv",
            Self::MoveFromFpuRegister => "mfc1",
            Self::MoveToFpuRegister => "mtc1",
            Self::MoveFromFpuControl => "cfc1",
            Self::MoveToFpuControl => "ctc1",
            Self::LoadFpuWord => "lwc1",
            Self::StoreFpuWord => "swc1",
            Self::FpuAddSingle => "add.s",
            Self::FpuSubtractSingle => "sub.s",
            Self::FpuMultiplySingle => "mul.s",
            Self::FpuDivideSingle => "div.s",
            Self::FpuSquareRootSingle => "sqrt.s",
            Self::FpuAbsoluteSingle => "abs.s",
            Self::FpuMoveSingle => "mov.s",
            Self::FpuNegateSingle => "neg.s",
            Self::FpuReciprocalSquareRootSingle => "rsqrt.s",
            Self::FpuAddAccumulateSingle => "adda.s",
            Self::FpuSubtractAccumulateSingle => "suba.s",
            Self::FpuMultiplyAccumulateSingle => "mula.s",
            Self::FpuMultiplyAddSingle => "madd.s",
            Self::FpuMultiplySubtractSingle => "msub.s",
            Self::FpuMultiplyAddAccumulateSingle => "madda.s",
            Self::FpuMultiplySubtractAccumulateSingle => "msuba.s",
            Self::FpuMaximumSingle => "max.s",
            Self::FpuMinimumSingle => "min.s",
            Self::FpuConvertSingleToWord => "cvt.w.s",
            Self::FpuConvertWordToSingle => "cvt.s.w",
            Self::Unknown => "unknown",
        }
    }
}

/// Guest-visible memory effects summarized for static analysis.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MemoryEffect {
    None,
    Read,
    Write,
    /// The instruction encoding is not understood; memory effects are unknown.
    Unknown,
}

/// Control-flow behavior relevant to CFG and data-flow consumers.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ControlFlowEffect {
    None,
    ConditionalDirect,
    ConditionalCall,
    DirectJump,
    DirectCall,
    IndirectJump,
    IndirectCall,
    /// A synchronous trap instruction (for example SYSCALL or BREAK).
    Trap,
    /// The instruction encoding is not understood.
    Unknown,
}

/// Conservative architectural effects for one decoded instruction.
///
/// Register masks use bit N for GPR N. For known instructions, GPR[0] is
/// omitted because it is hard-wired to zero. Unknown instructions conservatively
/// report every writable GPR as potentially read or written.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct InstructionEffects {
    pub gpr_read_mask: u32,
    pub gpr_write_mask: u32,
    /// FPU register dependencies. Unlike GPR[0], FPR[0] is a real register, so
    /// these masks include bit 0.
    pub fpr_read_mask: u32,
    pub fpr_write_mask: u32,
    pub reads_hi: bool,
    pub reads_lo: bool,
    pub writes_hi: bool,
    pub writes_lo: bool,
    pub reads_hi1: bool,
    pub reads_lo1: bool,
    pub writes_hi1: bool,
    pub writes_lo1: bool,
    pub reads_sa: bool,
    pub writes_sa: bool,
    /// CFC1 reads and CTC1 writes the single implemented FPU control register.
    pub reads_fcr: bool,
    pub writes_fcr: bool,
    /// The COP1 accumulator. Section 8.2 gives ACC no register number, so only
    /// the .A forms and the multiply-accumulate family touch it: ADDA, SUBA,
    /// MULA, MADDA and MSUBA write it, and MADD, MSUB, MADDA and MSUBA read it.
    pub reads_acc: bool,
    pub writes_acc: bool,
    pub memory: MemoryEffect,
    pub control_flow: ControlFlowEffect,
    /// True when instruction execution can synchronously raise an exception.
    /// Instruction-fetch faults and asynchronous interrupts are not included.
    pub may_raise_exception: bool,
    /// True when this is an unsupported/unknown instruction encoding.
    pub unknown: bool,
}

impl InstructionEffects {
    fn none() -> Self {
        Self {
            gpr_read_mask: 0,
            gpr_write_mask: 0,
            fpr_read_mask: 0,
            fpr_write_mask: 0,
            reads_hi: false,
            reads_lo: false,
            writes_hi: false,
            writes_lo: false,
            reads_hi1: false,
            reads_lo1: false,
            writes_hi1: false,
            writes_lo1: false,
            reads_sa: false,
            writes_sa: false,
            reads_fcr: false,
            writes_fcr: false,
            reads_acc: false,
            writes_acc: false,
            memory: MemoryEffect::None,
            control_flow: ControlFlowEffect::None,
            may_raise_exception: false,
            unknown: false,
        }
    }
}

fn gpr_bit(index: u8) -> u32 {
    if index == 0 || index >= 32 {
        0
    } else {
        1_u32 << index
    }
}

/// FPR[0] holds a value, so bit 0 of the mask is meaningful here.
fn fpr_bit(index: u8) -> u32 {
    if index < 32 { 1_u32 << index } else { 0 }
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
            Opcode::J
                | Opcode::Jal
                | Opcode::Beq
                | Opcode::Bne
                | Opcode::Blez
                | Opcode::Bgtz
                | Opcode::Bltz
                | Opcode::Bgez
                | Opcode::Bltzal
                | Opcode::Bgezal
                | Opcode::Beql
                | Opcode::Bnel
                | Opcode::Blezl
                | Opcode::Bgtzl
                | Opcode::Bltzl
                | Opcode::Bgezl
                | Opcode::Bltzall
                | Opcode::Bgezall
                | Opcode::Jr
                | Opcode::Jalr
        )
    }

    /// Returns whether the opcode is a conditional direct branch.
    #[must_use]
    pub const fn is_conditional_branch(self) -> bool {
        matches!(
            self.opcode,
            Opcode::Beq
                | Opcode::Bne
                | Opcode::Blez
                | Opcode::Bgtz
                | Opcode::Bltz
                | Opcode::Bgez
                | Opcode::Bltzal
                | Opcode::Bgezal
                | Opcode::Beql
                | Opcode::Bnel
                | Opcode::Blezl
                | Opcode::Bgtzl
                | Opcode::Bltzl
                | Opcode::Bgezl
                | Opcode::Bltzall
                | Opcode::Bgezall
        )
    }

    /// Returns whether the branch annuls its delay slot when the branch is not
    /// taken. The R5900 branch-likely family only executes the immediately
    /// following instruction on the taken path.
    #[must_use]
    pub const fn is_branch_likely(self) -> bool {
        matches!(
            self.opcode,
            Opcode::Beql
                | Opcode::Bnel
                | Opcode::Blezl
                | Opcode::Bgtzl
                | Opcode::Bltzl
                | Opcode::Bgezl
                | Opcode::Bltzall
                | Opcode::Bgezall
        )
    }

    /// Summarizes the architectural data, memory, control-flow, and exception
    /// effects of this instruction for static analysis.
    ///
    /// The summary covers only opcodes recognized by this decoder. Unknown
    /// encodings are intentionally conservative and must not be optimized as
    /// though they had no side effects.
    #[must_use]
    pub fn effects(self) -> InstructionEffects {
        let mut effects = InstructionEffects::none();
        let rs = gpr_bit(self.rs);
        let rt = gpr_bit(self.rt);
        let rd = gpr_bit(self.rd);

        match self.opcode {
            Opcode::Nop => {}

            Opcode::Sll | Opcode::Srl | Opcode::Sra => {
                effects.gpr_read_mask = rt;
                effects.gpr_write_mask = rd;
            }

            Opcode::Sllv
            | Opcode::Srlv
            | Opcode::Srav
            | Opcode::Dsllv
            | Opcode::Dsrlv
            | Opcode::Dsrav
            | Opcode::Add
            | Opcode::Addu
            | Opcode::Sub
            | Opcode::Subu
            | Opcode::Dadd
            | Opcode::Dsub
            | Opcode::And
            | Opcode::Or
            | Opcode::Xor
            | Opcode::Nor
            | Opcode::Slt
            | Opcode::Sltu
            | Opcode::Daddu
            | Opcode::Dsubu => {
                effects.gpr_read_mask = rs | rt;
                effects.gpr_write_mask = rd;
                effects.may_raise_exception = matches!(
                    self.opcode,
                    Opcode::Add | Opcode::Sub | Opcode::Dadd | Opcode::Dsub
                );
            }

            Opcode::Dsll
            | Opcode::Dsrl
            | Opcode::Dsra
            | Opcode::Dsll32
            | Opcode::Dsrl32
            | Opcode::Dsra32 => {
                effects.gpr_read_mask = rt;
                effects.gpr_write_mask = rd;
            }

            Opcode::Mfhi => {
                effects.reads_hi = true;
                effects.gpr_write_mask = rd;
            }
            Opcode::Mthi => {
                effects.gpr_read_mask = rs;
                effects.writes_hi = true;
            }
            Opcode::Mflo => {
                effects.reads_lo = true;
                effects.gpr_write_mask = rd;
            }
            Opcode::Mtlo => {
                effects.gpr_read_mask = rs;
                effects.writes_lo = true;
            }
            Opcode::Mfhi1 => {
                effects.reads_hi1 = true;
                effects.gpr_write_mask = rd;
            }
            Opcode::Mthi1 => {
                effects.gpr_read_mask = rs;
                effects.writes_hi1 = true;
            }
            Opcode::Mflo1 => {
                effects.reads_lo1 = true;
                effects.gpr_write_mask = rd;
            }
            Opcode::Mtlo1 => {
                effects.gpr_read_mask = rs;
                effects.writes_lo1 = true;
            }
            Opcode::Mfsa => {
                effects.reads_sa = true;
                effects.gpr_write_mask = rd;
            }
            Opcode::Mtsa => {
                effects.gpr_read_mask = rs;
                effects.writes_sa = true;
            }
            Opcode::Mtsab => {
                effects.gpr_read_mask = rs;
                effects.writes_sa = true;
            }
            Opcode::Mtsah => {
                effects.gpr_read_mask = rs;
                effects.writes_sa = true;
            }
            Opcode::Mult | Opcode::Multu => {
                effects.gpr_read_mask = rs | rt;
                effects.gpr_write_mask = rd;
                effects.writes_hi = true;
                effects.writes_lo = true;
            }
            Opcode::Div | Opcode::Divu => {
                effects.gpr_read_mask = rs | rt;
                effects.writes_hi = true;
                effects.writes_lo = true;
            }

            Opcode::J => {
                effects.control_flow = ControlFlowEffect::DirectJump;
            }
            Opcode::Jal => {
                effects.gpr_write_mask = gpr_bit(31);
                effects.control_flow = ControlFlowEffect::DirectCall;
            }
            Opcode::Beq | Opcode::Bne | Opcode::Beql | Opcode::Bnel => {
                effects.gpr_read_mask = rs | rt;
                effects.control_flow = ControlFlowEffect::ConditionalDirect;
            }
            Opcode::Blez
            | Opcode::Bgtz
            | Opcode::Bltz
            | Opcode::Bgez
            | Opcode::Blezl
            | Opcode::Bgtzl
            | Opcode::Bltzl
            | Opcode::Bgezl => {
                effects.gpr_read_mask = rs;
                effects.control_flow = ControlFlowEffect::ConditionalDirect;
            }
            Opcode::Bltzal | Opcode::Bgezal | Opcode::Bltzall | Opcode::Bgezall => {
                effects.gpr_read_mask = rs;
                // The link register is written unconditionally by the branch,
                // including branch-likely variants; annulment applies only to
                // the delay-slot instruction on the not-taken path.
                effects.gpr_write_mask = gpr_bit(31);
                effects.control_flow = ControlFlowEffect::ConditionalCall;
            }
            Opcode::Jr => {
                effects.gpr_read_mask = rs;
                effects.control_flow = ControlFlowEffect::IndirectJump;
            }
            Opcode::Jalr => {
                effects.gpr_read_mask = rs;
                effects.gpr_write_mask = rd;
                effects.control_flow = if self.rd == 0 {
                    ControlFlowEffect::IndirectJump
                } else {
                    ControlFlowEffect::IndirectCall
                };
            }

            Opcode::Addi
            | Opcode::Addiu
            | Opcode::Daddi
            | Opcode::Daddiu
            | Opcode::Slti
            | Opcode::Sltiu
            | Opcode::Andi
            | Opcode::Ori
            | Opcode::Xori => {
                effects.gpr_read_mask = rs;
                effects.gpr_write_mask = rt;
                effects.may_raise_exception = matches!(self.opcode, Opcode::Addi | Opcode::Daddi);
            }
            Opcode::Lui => {
                effects.gpr_write_mask = rt;
            }
            Opcode::ParallelAddUnsignedWord => {
                effects.gpr_read_mask = rs | rt;
                effects.gpr_write_mask = rd;
            }
            Opcode::FunnelShiftRightVec => {
                effects.reads_sa = true;
                effects.gpr_read_mask = rs | rt;
                effects.gpr_write_mask = rd;
            }

            // COP1 transfers use rt as the GPR and rd as the FPU register or
            // control-register number. They cannot raise a floating-point
            // exception because the EE FPU has none (section 8.8: processing
            // continues by setting a flag); a CpU-Unusable exception still
            // exists but needs the unmodeled COP0 Status.CU1 gate. The
            // load/store forms can raise the same address error as the other
            // memory operations.
            Opcode::MoveFromFpuRegister => {
                effects.fpr_read_mask = fpr_bit(self.rd);
                effects.gpr_write_mask = rt;
            }
            Opcode::MoveToFpuRegister => {
                effects.gpr_read_mask = rt;
                effects.fpr_write_mask = fpr_bit(self.rd);
            }
            Opcode::MoveFromFpuControl => {
                // FIR (FCR0) is a read-only constant and touches no state.
                effects.reads_fcr = self.rd == 31;
                effects.gpr_write_mask = rt;
            }
            Opcode::MoveToFpuControl => {
                effects.writes_fcr = true;
                effects.gpr_read_mask = rt;
            }
            Opcode::LoadFpuWord => {
                effects.gpr_read_mask = rs;
                effects.fpr_write_mask = fpr_bit(self.rt);
                effects.memory = MemoryEffect::Read;
                effects.may_raise_exception = true;
            }
            Opcode::StoreFpuWord => {
                effects.gpr_read_mask = rs;
                effects.fpr_read_mask = fpr_bit(self.rt);
                effects.memory = MemoryEffect::Write;
                effects.may_raise_exception = true;
            }

            // COP1 arithmetic never raises: section 8.8 says processing
            // continues after setting a cause flag. Every form updates the
            // sticky summary in FCR31, and the accumulate forms touch ACC,
            // which has no COP1 register number and is reachable only here.
            Opcode::FpuAddSingle
            | Opcode::FpuSubtractSingle
            | Opcode::FpuMultiplySingle
            | Opcode::FpuDivideSingle
            | Opcode::FpuMaximumSingle
            | Opcode::FpuMinimumSingle => {
                effects.fpr_read_mask = fpr_bit(self.rd) | fpr_bit(self.rt);
                effects.fpr_write_mask = fpr_bit(self.shift_amount);
                effects.writes_fcr = true;
            }
            Opcode::FpuSquareRootSingle => {
                effects.fpr_read_mask = fpr_bit(self.rt);
                effects.fpr_write_mask = fpr_bit(self.shift_amount);
                effects.writes_fcr = true;
            }
            Opcode::FpuReciprocalSquareRootSingle => {
                effects.fpr_read_mask = fpr_bit(self.rd) | fpr_bit(self.rt);
                effects.fpr_write_mask = fpr_bit(self.shift_amount);
                effects.writes_fcr = true;
            }
            Opcode::FpuAbsoluteSingle
            | Opcode::FpuMoveSingle
            | Opcode::FpuNegateSingle
            | Opcode::FpuConvertSingleToWord
            | Opcode::FpuConvertWordToSingle => {
                effects.fpr_read_mask = fpr_bit(self.rd);
                effects.fpr_write_mask = fpr_bit(self.shift_amount);
                effects.writes_fcr = true;
            }
            Opcode::FpuAddAccumulateSingle
            | Opcode::FpuSubtractAccumulateSingle
            | Opcode::FpuMultiplyAccumulateSingle => {
                effects.fpr_read_mask = fpr_bit(self.rd) | fpr_bit(self.rt);
                effects.writes_acc = true;
                effects.writes_fcr = true;
            }
            Opcode::FpuMultiplyAddAccumulateSingle
            | Opcode::FpuMultiplySubtractAccumulateSingle => {
                effects.fpr_read_mask = fpr_bit(self.rd) | fpr_bit(self.rt);
                effects.reads_acc = true;
                effects.writes_acc = true;
                effects.writes_fcr = true;
            }
            Opcode::FpuMultiplyAddSingle | Opcode::FpuMultiplySubtractSingle => {
                effects.fpr_read_mask = fpr_bit(self.rd) | fpr_bit(self.rt);
                effects.reads_acc = true;
                effects.fpr_write_mask = fpr_bit(self.shift_amount);
                effects.writes_fcr = true;
            }

            Opcode::Lb
            | Opcode::Lh
            | Opcode::Lw
            | Opcode::Lbu
            | Opcode::Lhu
            | Opcode::Lwu
            | Opcode::Ld
            | Opcode::Lq => {
                effects.gpr_read_mask = rs;
                effects.gpr_write_mask = rt;
                effects.memory = MemoryEffect::Read;
                effects.may_raise_exception = true;
            }
            Opcode::Lwl | Opcode::Lwr | Opcode::Ldl | Opcode::Ldr => {
                // Merge loads consume the old destination value as well as
                // the base address and then replace the selected byte lanes.
                effects.gpr_read_mask = rs | rt;
                effects.gpr_write_mask = rt;
                effects.memory = MemoryEffect::Read;
                effects.may_raise_exception = true;
            }
            Opcode::Sb
            | Opcode::Sh
            | Opcode::Sw
            | Opcode::Sd
            | Opcode::Sq
            | Opcode::Swl
            | Opcode::Swr
            | Opcode::Sdl
            | Opcode::Sdr => {
                effects.gpr_read_mask = rs | rt;
                effects.memory = MemoryEffect::Write;
                effects.may_raise_exception = true;
            }

            Opcode::Syscall | Opcode::Break => {
                effects.control_flow = ControlFlowEffect::Trap;
                effects.may_raise_exception = true;
            }

            Opcode::Unknown => {
                // Do not let an unrecognized EE, COP, FPU, MMI, or malformed
                // encoding look like a side-effect-free instruction.
                effects.gpr_read_mask = !1_u32;
                effects.gpr_write_mask = !1_u32;
                effects.reads_hi = true;
                effects.reads_lo = true;
                effects.writes_hi = true;
                effects.writes_lo = true;
                effects.reads_hi1 = true;
                effects.reads_lo1 = true;
                effects.writes_hi1 = true;
                effects.writes_lo1 = true;
                effects.reads_sa = true;
                effects.writes_sa = true;
                effects.memory = MemoryEffect::Unknown;
                effects.control_flow = ControlFlowEffect::Unknown;
                effects.may_raise_exception = true;
                effects.unknown = true;
            }
        }

        effects
    }
}

/// Computes a region-relative `J`/`JAL` target.
///
/// The host program counter is 32 bits on the R5900, but the region carried
/// into the target is the **high 4 bits** of the delay-slot address
/// (`PC + 4`), not the high 3 bits of classic MIPS32. In particular a KSEG0 PC
/// such as `0x90000000` produces a KSEG1 target instead of a sign-extended
/// `0x80000000`-region one. The carry out of the low 28 bits is kept, so the
/// last words of the address space select region `0xF` rather than wrapping to
/// region `0x0`.
#[must_use]
fn jump_target(pc: u32, word: u32) -> u32 {
    let delay_pc = u64::from(pc) + 4;
    // Bits 31..28, including the carry out of bit 31 as bit 32.
    let region = (delay_pc >> 28) as u32;
    (region << 28) | ((word & 0x03FF_FFFF) << 2)
}

/// Decodes the COP1 transfer, S-class and W-class formats.
///
/// The `fmt` field (bits 25..21) selects the transfer sub-operation for the
/// classic forms and the operand format for arithmetic. The S-class names the
/// FPR in bits 15..11 `fs` and the one in bits 20..16 `ft`, the same placement
/// MFC1 already uses, and `fd` is bits 10..6. The manual masks make the unused
/// field reserved-zero; retail code leaves those fields at zero in every
/// sampled word, so a non-zero reserved field is not decoded as the
/// instruction it would otherwise name.
fn decode_cop1(word: u32) -> Opcode {
    let format = ((word >> 21) & 0x1F) as u8;
    let ft = ((word >> 16) & 0x1F) as u8;
    let fs = ((word >> 11) & 0x1F) as u8;
    let fd = ((word >> 6) & 0x1F) as u8;
    let funct = (word & 0x3F) as u8;

    if matches!(format, 0 | 2 | 4 | 6) {
        // Every transfer form reserves bits 10..0 (mask 0xFFFFF800).
        if (word & 0x7FF) != 0 {
            return Opcode::Unknown;
        }
        return match format {
            0 => Opcode::MoveFromFpuRegister,
            2 if fs == 0 || fs == 31 => Opcode::MoveFromFpuControl,
            4 => Opcode::MoveToFpuRegister,
            6 if fs == 31 => Opcode::MoveToFpuControl,
            _ => Opcode::Unknown,
        };
    }

    match format {
        0x10 => match funct {
            0x00 => Opcode::FpuAddSingle,
            0x01 => Opcode::FpuSubtractSingle,
            0x02 => Opcode::FpuMultiplySingle,
            0x03 => Opcode::FpuDivideSingle,
            0x04 if fs == 0 => Opcode::FpuSquareRootSingle,
            0x05 if ft == 0 => Opcode::FpuAbsoluteSingle,
            0x06 if ft == 0 => Opcode::FpuMoveSingle,
            0x07 if ft == 0 => Opcode::FpuNegateSingle,
            0x16 => Opcode::FpuReciprocalSquareRootSingle,
            0x18 if fd == 0 => Opcode::FpuAddAccumulateSingle,
            0x19 if fd == 0 => Opcode::FpuSubtractAccumulateSingle,
            0x1A if fd == 0 => Opcode::FpuMultiplyAccumulateSingle,
            0x1C => Opcode::FpuMultiplyAddSingle,
            0x1D => Opcode::FpuMultiplySubtractSingle,
            0x1E if fd == 0 => Opcode::FpuMultiplyAddAccumulateSingle,
            0x1F if fd == 0 => Opcode::FpuMultiplySubtractAccumulateSingle,
            0x24 if ft == 0 => Opcode::FpuConvertSingleToWord,
            0x28 => Opcode::FpuMaximumSingle,
            0x29 => Opcode::FpuMinimumSingle,
            _ => Opcode::Unknown,
        },
        0x14 if funct == 0x20 && ft == 0 => Opcode::FpuConvertWordToSingle,
        _ => Opcode::Unknown,
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
            0x11 if rt == 0 && rd == 0 && shift_amount == 0 => Opcode::Mthi,
            0x12 if rs == 0 && rt == 0 && shift_amount == 0 => Opcode::Mflo,
            0x13 if rt == 0 && rd == 0 && shift_amount == 0 => Opcode::Mtlo,
            // EE-specific SA moves reuse the R-type shift field: MFSA takes its
            // destination from rd (bits 15..11) while MTSA reads rs.
            0x28 if rs == 0 && rt == 0 && shift_amount == 0 => Opcode::Mfsa,
            0x29 if rt == 0 && rd == 0 && shift_amount == 0 => Opcode::Mtsa,
            // R5900 MULT/MULTU use rd as an optional third destination.
            // MULT/MULTU share the MIPS32 rd field as an R5900 destination for
            // the low 64 bits of the product; DIV/DIVU keep rd reserved-zero
            // exactly as MIPS32 requires (there is no R5900 division alias).
            0x18 if shift_amount == 0 => Opcode::Mult,
            0x19 if shift_amount == 0 => Opcode::Multu,
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
            0x2C if shift_amount == 0 => Opcode::Dadd,
            0x2D if shift_amount == 0 => Opcode::Daddu,
            0x2E if shift_amount == 0 => Opcode::Dsub,
            0x2F if shift_amount == 0 => Opcode::Dsubu,
            0x38 if rs == 0 => Opcode::Dsll,
            0x3A if rs == 0 => Opcode::Dsrl,
            0x3B if rs == 0 => Opcode::Dsra,
            0x3C if rs == 0 => Opcode::Dsll32,
            0x3E if rs == 0 => Opcode::Dsrl32,
            0x3F if rs == 0 => Opcode::Dsra32,
            0x04 if shift_amount == 0 => Opcode::Sllv,
            0x06 if shift_amount == 0 => Opcode::Srlv,
            0x07 if shift_amount == 0 => Opcode::Srav,
            0x14 if shift_amount == 0 => Opcode::Dsllv,
            0x16 if shift_amount == 0 => Opcode::Dsrlv,
            0x17 if shift_amount == 0 => Opcode::Dsrav,
            _ => Opcode::Unknown,
        };
    } else {
        instruction.opcode = match primary {
            0x01 => {
                let opcode = match rt {
                    0x00 => Opcode::Bltz,
                    0x01 => Opcode::Bgez,
                    0x02 => Opcode::Bltzl,
                    0x03 => Opcode::Bgezl,
                    0x10 => Opcode::Bltzal,
                    0x11 => Opcode::Bgezal,
                    0x12 => Opcode::Bltzall,
                    0x13 => Opcode::Bgezall,
                    0x18 => Opcode::Mtsab,
                    0x19 => Opcode::Mtsah,
                    _ => Opcode::Unknown,
                };
                if matches!(
                    opcode,
                    Opcode::Bltz
                        | Opcode::Bgez
                        | Opcode::Bltzl
                        | Opcode::Bgezl
                        | Opcode::Bltzal
                        | Opcode::Bgezal
                        | Opcode::Bltzall
                        | Opcode::Bgezall
                ) {
                    instruction.target = pc
                        .wrapping_add(4)
                        .wrapping_add(((immediate as i16 as i32).wrapping_mul(4)) as u32);
                    instruction.has_delay_slot = true;
                }
                opcode
            }
            0x02 => {
                instruction.target = jump_target(pc, word);
                instruction.has_delay_slot = true;
                Opcode::J
            }
            0x03 => {
                instruction.target = jump_target(pc, word);
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
            0x14..=0x17 => {
                let offset = ((immediate as i16 as i32).wrapping_mul(4)) as u32;
                instruction.target = pc.wrapping_add(4).wrapping_add(offset);
                instruction.has_delay_slot = true;
                match primary {
                    0x14 => Opcode::Beql,
                    0x15 => Opcode::Bnel,
                    0x16 if rt == 0 => Opcode::Blezl,
                    0x17 if rt == 0 => Opcode::Bgtzl,
                    _ => Opcode::Unknown,
                }
            }
            0x08 => Opcode::Addi,
            0x09 => Opcode::Addiu,
            0x18 => Opcode::Daddi,
            0x19 => Opcode::Daddiu,
            0x0A => Opcode::Slti,
            0x0B => Opcode::Sltiu,
            0x0C => Opcode::Andi,
            0x0D => Opcode::Ori,
            0x0E => Opcode::Xori,
            0x0F if rs == 0 => Opcode::Lui,
            0x1A => Opcode::Ldl,
            0x1B => Opcode::Ldr,
            0x1C => match (word & 0x3F) as u8 {
                0x28 if ((word >> 6) & 0x1F) == 0x10 => Opcode::ParallelAddUnsignedWord,
                0x28 if ((word >> 6) & 0x1F) == 0x1B => Opcode::FunnelShiftRightVec,
                0x10 if rs == 0 && rt == 0 && ((word >> 6) & 0x1F) == 0 => Opcode::Mfhi1,
                0x11 if rt == 0 && rd == 0 && ((word >> 6) & 0x1F) == 0 => Opcode::Mthi1,
                0x12 if rs == 0 && rt == 0 && ((word >> 6) & 0x1F) == 0 => Opcode::Mflo1,
                0x13 if rt == 0 && rd == 0 && ((word >> 6) & 0x1F) == 0 => Opcode::Mtlo1,
                _ => Opcode::Unknown,
            },
            // Bit 25 is not a COP1/COP2 flag: COP2 (VU0) has its own primary
            // opcode 0x12, so 0x11 is unambiguously the FPU. Bits 25..21 are the
            // format selector, which is why the register transfers and the
            // arithmetic share one arm.
            0x11 => decode_cop1(word),
            0x20 => Opcode::Lb,
            0x21 => Opcode::Lh,
            0x22 => Opcode::Lwl,
            0x23 => Opcode::Lw,
            0x24 => Opcode::Lbu,
            0x25 => Opcode::Lhu,
            0x27 => Opcode::Lwu,
            0x31 => Opcode::LoadFpuWord,
            0x39 => Opcode::StoreFpuWord,
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

    if !instruction.is_control_transfer() {
        instruction.has_delay_slot = false;
    }
    instruction
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Builds a COP1 word from the hardware field layout: format in 25..21,
    /// ft in 20..16, fs in 15..11, fd in 10..6.
    fn encode_cop1(format: u32, ft: u32, fs: u32, fd: u32, funct: u32) -> u32 {
        (0x11 << 26) | (format << 21) | (ft << 16) | (fs << 11) | (fd << 6) | funct
    }

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
        assert_eq!(
            decode((0x20 << 26) | (4 << 21) | (5 << 16) | 0xFFFC, 0).signed_immediate(),
            -4
        );
        assert_eq!(
            decode((0x37 << 26) | (4 << 21) | (5 << 16) | 0xFFF8, 0).signed_immediate(),
            -8
        );
    }

    #[test]
    fn rejects_invalid_reserved_fields_and_unknown_ops() {
        let bad_jr = (4 << 21) | (1 << 16) | 0x08;
        assert!(!decode(bad_jr, 0).is_supported());
        assert_eq!(decode(0xFFFF_FFFF, 0).opcode, Opcode::Sd);
        assert!(!decode(0xEC00_0000, 0).is_supported());
    }

    #[test]
    fn jump_region_uses_the_high_four_bits_of_pc_plus_four() {
        // PC 0x80000000 + 4 = 0x80000004 selects region 0x8.
        let decoded = decode((0x03 << 26) | 1, 0x8000_0000);
        assert_eq!(decoded.opcode, Opcode::Jal);
        assert_eq!(decoded.target, 0x8000_0004);

        // The last word of region 0x7: PC + 4 crosses into region 0x8, which a
        // signed-arithmetic implementation of PC+4 would get wrong.
        assert_eq!(decode(0x0800_0003, 0x7FFF_FFFC).target, 0x8000_000C);
        assert_eq!(decode(0x0C00_0002, 0x7FFF_FFFC).target, 0x8000_0008);

        // KSEG0 and KSEG1 regions are selected by the same four-bit rule; the
        // region is never copied from the sign extension of the target field.
        assert_eq!(decode(0x0C00_0002, 0x8000_0000).target, 0x8000_0008);
        assert_eq!(decode(0x0800_0002, 0xA000_0000).target, 0xA000_0008);
        assert_eq!(decode(0x0800_0002, 0x9000_0000).target, 0x9000_0008);

        // A PC + 4 whose bit 31 carries out keeps that carry inside the host
        // value instead of panicking; the resulting 33-bit region is then
        // truncated to the architectural 32-bit PC (region 0x0 here), which is
        // the address the R5900 itself would form.
        assert_eq!(decode(0x0800_0002, 0xFFFF_FFFC).target, 0x0000_0008);
    }

    #[test]
    fn decodes_r5900_nontrapping_doubleword_integer_operations() {
        let cases = [
            (((8 << 21) | (9 << 16) | (10 << 11) | 0x2D), Opcode::Daddu),
            (((8 << 21) | (9 << 16) | (10 << 11) | 0x2F), Opcode::Dsubu),
            (
                ((0x19 << 26) | (8 << 21) | (10 << 16) | 0xFFF0),
                Opcode::Daddiu,
            ),
            (((9 << 16) | (10 << 11) | (7 << 6) | 0x38), Opcode::Dsll),
            (((9 << 16) | (10 << 11) | (7 << 6) | 0x3A), Opcode::Dsrl),
            (((9 << 16) | (10 << 11) | (7 << 6) | 0x3B), Opcode::Dsra),
            (((9 << 16) | (10 << 11) | (7 << 6) | 0x3C), Opcode::Dsll32),
            (((9 << 16) | (10 << 11) | (7 << 6) | 0x3E), Opcode::Dsrl32),
            (((9 << 16) | (10 << 11) | (7 << 6) | 0x3F), Opcode::Dsra32),
        ];
        for (word, expected) in cases {
            let decoded = decode(word, 0x4000);
            assert_eq!(decoded.opcode, expected, "word 0x{word:08X}");
            assert!(!decoded.has_delay_slot);
        }
        assert_eq!(
            decode((0x19 << 26) | (8 << 21) | (9 << 16) | 0xFFF0, 0).signed_immediate(),
            -16
        );
    }

    #[test]
    fn decodes_register_variable_word_and_doubleword_shifts() {
        let cases = [
            ((4 << 21) | (5 << 16) | (6 << 11) | 0x04, Opcode::Sllv),
            ((4 << 21) | (5 << 16) | (7 << 11) | 0x06, Opcode::Srlv),
            ((4 << 21) | (5 << 16) | (8 << 11) | 0x07, Opcode::Srav),
            ((4 << 21) | (5 << 16) | (30 << 11) | 0x14, Opcode::Dsllv),
            ((4 << 21) | (5 << 16) | (31 << 11) | 0x16, Opcode::Dsrlv),
            ((4 << 21) | (5 << 16) | (22 << 11) | 0x17, Opcode::Dsrav),
        ];
        for (word, expected) in cases {
            assert_eq!(decode(word, 0x6000).opcode, expected, "word 0x{word:08X}");
        }
    }

    #[test]
    fn decodes_r5900_mmi_parallel_add_unsigned_word_and_rejects_other_groups() {
        let word = (0x1C << 26) | (1 << 21) | (2 << 16) | (3 << 11) | (0x10 << 6) | 0x28;
        let decoded = decode(word, 0x7000);
        assert_eq!(decoded.opcode, Opcode::ParallelAddUnsignedWord);
        assert_eq!((decoded.rs, decoded.rt, decoded.rd), (1, 2, 3));
        assert_eq!(
            encoding_family(word),
            EncodingFamily::Mmi1,
            "PADDUW is an MMI1-group encoding"
        );
        assert!(!decoded.has_delay_slot);
        assert!(!decoded.is_control_transfer());

        let effects = decoded.effects();
        assert_eq!(effects.gpr_read_mask, (1 << 1) | (1 << 2));
        assert_eq!(effects.gpr_write_mask, 1 << 3);
        assert!(!effects.unknown);

        // The same function bits in another MMI group, and a different MMI1
        // selector, must not be mistaken for PADDUW.
        let mmi0 = (word & !0x3F) | 0x08;
        assert_eq!(decode(mmi0, 0).opcode, Opcode::Unknown);
        let other_mmi1 = (word & !(0x1F << 6)) | (0x11 << 6);
        assert_eq!(decode(other_mmi1, 0).opcode, Opcode::Unknown);
    }

    #[test]
    fn decodes_r5900_mmi_pipeline_one_hilo_moves_and_reserved_fields() {
        let mthi1 = (0x1C << 26) | (4 << 21) | 0x11;
        let mtlo1 = (0x1C << 26) | (5 << 21) | 0x13;
        let mfhi1 = (0x1C << 26) | (6 << 11) | 0x10;
        let mflo1 = (0x1C << 26) | (7 << 11) | 0x12;
        assert_eq!(decode(mthi1, 0xF000).opcode, Opcode::Mthi1);
        assert_eq!(decode(mtlo1, 0xF004).opcode, Opcode::Mtlo1);
        assert_eq!(decode(mfhi1, 0xF008).opcode, Opcode::Mfhi1);
        assert_eq!(decode(mflo1, 0xF00C).opcode, Opcode::Mflo1);

        // Reserved fields and non-zero MMI selector bits remain unsupported.
        assert!(!decode(mthi1 | (1 << 16), 0).is_supported());
        assert!(!decode(mthi1 | (1 << 11), 0).is_supported());
        assert!(!decode(mfhi1 | (1 << 21), 0).is_supported());
        assert!(!decode(mfhi1 | (1 << 16), 0).is_supported());
        assert!(!decode(mthi1 | (1 << 6), 0).is_supported());
    }

    #[test]
    fn effects_track_mmi_pipeline_one_hilo_dependencies() {
        let mthi1 = decode((0x1C << 26) | (4 << 21) | 0x11, 0).effects();
        assert_eq!(mthi1.gpr_read_mask, 1 << 4);
        assert_eq!(mthi1.gpr_write_mask, 0);
        assert!(mthi1.writes_hi1);
        assert!(!mthi1.writes_lo1);
        assert!(!mthi1.writes_hi && !mthi1.writes_lo);

        let mtlo1 = decode((0x1C << 26) | (5 << 21) | 0x13, 0).effects();
        assert_eq!(mtlo1.gpr_read_mask, 1 << 5);
        assert!(mtlo1.writes_lo1);
        assert!(!mtlo1.writes_hi1);

        let mfhi1 = decode((0x1C << 26) | (6 << 11) | 0x10, 0).effects();
        assert!(mfhi1.reads_hi1);
        assert_eq!(mfhi1.gpr_write_mask, 1 << 6);

        let mflo1 = decode((0x1C << 26) | (7 << 11) | 0x12, 0).effects();
        assert!(mflo1.reads_lo1);
        assert_eq!(mflo1.gpr_write_mask, 1 << 7);
    }

    #[test]
    fn rejects_nonzero_reserved_shift_amount_for_variable_shifts() {
        for function in [0x04, 0x06, 0x07, 0x14, 0x16, 0x17] {
            let invalid = (4 << 21) | (5 << 16) | (6 << 11) | (1 << 6) | function;
            assert!(
                !decode(invalid, 0).is_supported(),
                "function 0x{function:02X}"
            );
        }
    }

    #[test]
    fn rejects_reserved_source_fields_for_doubleword_immediate_shifts() {
        for function in [0x38, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F] {
            let invalid = (1 << 21) | (9 << 16) | (10 << 11) | function;
            assert!(
                !decode(invalid, 0).is_supported(),
                "function 0x{function:02X}"
            );
        }
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

    #[test]
    fn decodes_branch_likely_family_and_annulment_metadata() {
        let cases = [
            ((0x14 << 26) | (8 << 21) | (9 << 16) | 2, Opcode::Beql),
            ((0x15 << 26) | (8 << 21) | (9 << 16) | 2, Opcode::Bnel),
            ((0x16 << 26) | (8 << 21) | 2, Opcode::Blezl),
            ((0x17 << 26) | (8 << 21) | 2, Opcode::Bgtzl),
            ((0x01 << 26) | (8 << 21) | (0x02 << 16) | 2, Opcode::Bltzl),
            ((0x01 << 26) | (8 << 21) | (0x03 << 16) | 2, Opcode::Bgezl),
            ((0x01 << 26) | (8 << 21) | (0x12 << 16) | 2, Opcode::Bltzall),
            ((0x01 << 26) | (8 << 21) | (0x13 << 16) | 2, Opcode::Bgezall),
        ];
        for (word, expected) in cases {
            let decoded = decode(word, 0x1000);
            assert_eq!(decoded.opcode, expected, "word 0x{word:08X}");
            assert!(decoded.is_supported());
            assert!(decoded.has_delay_slot);
            assert!(decoded.is_control_transfer());
            assert!(decoded.is_conditional_branch());
            assert!(decoded.is_branch_likely(), "word 0x{word:08X}");
            assert_eq!(decoded.target, 0x100C);
        }

        let branch = decode((0x04 << 26) | (8 << 21) | (9 << 16) | 2, 0x1000);
        assert!(!branch.is_branch_likely());
        assert!(!decode((0x16 << 26) | (8 << 21) | (1 << 16), 0x1000).is_supported());
        assert!(!decode((0x17 << 26) | (8 << 21) | (1 << 16), 0x1000).is_supported());
    }

    #[test]
    fn classifies_encoding_families_for_coverage() {
        assert_eq!(encoding_family(0x0000_000C), EncodingFamily::Special);
        assert_eq!(encoding_family(0x0419_0000), EncodingFamily::RegImm);
        assert_eq!(encoding_family(0x0800_0000), EncodingFamily::Jump);
        assert_eq!(encoding_family(0x1420_FFFA), EncodingFamily::Branch);
        assert_eq!(encoding_family(0x2408_0005), EncodingFamily::Immediate);
        assert_eq!(encoding_family(0x4080_0000), EncodingFamily::Cop0);
        assert_eq!(encoding_family(0x4480_0000), EncodingFamily::Cop1);
        assert_eq!(encoding_family(0x4840_0000), EncodingFamily::Cop2);
        assert_eq!(encoding_family(0x7000_0008), EncodingFamily::Mmi0);
        assert_eq!(encoding_family(0x7000_0009), EncodingFamily::Mmi2);
        assert_eq!(encoding_family(0x7000_0C28), EncodingFamily::Mmi1);
        assert_eq!(encoding_family(0x7000_0029), EncodingFamily::Mmi3);
        assert_eq!(encoding_family(0x7000_0410), EncodingFamily::Mmi);
        assert_eq!(encoding_family(0x8C44_0000), EncodingFamily::LoadStore);
        assert_eq!(encoding_family(0x4C00_0000), EncodingFamily::Other);

        assert_eq!(EncodingFamily::Mmi1.label(), "mmi1");
        assert_eq!(EncodingFamily::LoadStore.label(), "load-store");
    }

    #[test]
    fn effects_classify_branch_likely_and_write_link_unconditionally() {
        let beql = decode((0x14 << 26) | (4 << 21) | (5 << 16) | 1, 0x5000).effects();
        assert_eq!(beql.gpr_read_mask, (1 << 4) | (1 << 5));
        assert_eq!(beql.gpr_write_mask, 0);
        assert_eq!(beql.control_flow, ControlFlowEffect::ConditionalDirect);

        let bltzall = decode((0x01 << 26) | (4 << 21) | (0x12 << 16) | 1, 0x5004).effects();
        assert_eq!(bltzall.gpr_read_mask, 1 << 4);
        assert_eq!(bltzall.gpr_write_mask, 1 << 31);
        assert_eq!(bltzall.control_flow, ControlFlowEffect::ConditionalCall);

        let bgezall = decode((0x01 << 26) | (9 << 21) | (0x13 << 16) | 1, 0x5008).effects();
        assert_eq!(bgezall.gpr_read_mask, 1 << 9);
        assert_eq!(bgezall.gpr_write_mask, 1 << 31);
        assert_eq!(bgezall.control_flow, ControlFlowEffect::ConditionalCall);
    }

    #[test]
    fn effects_report_scalar_register_dependencies_and_hardwired_zero() {
        let addu = decode((4 << 21) | (5 << 16) | (6 << 11) | 0x21, 0x1000);
        let effects = addu.effects();
        assert_eq!(effects.gpr_read_mask, (1 << 4) | (1 << 5));
        assert_eq!(effects.gpr_write_mask, 1 << 6);
        assert_eq!(effects.memory, MemoryEffect::None);
        assert_eq!(effects.control_flow, ControlFlowEffect::None);
        assert!(!effects.may_raise_exception);

        let addiu_from_zero = decode((0x09 << 26) | (7 << 16) | 12, 0x1004);
        let effects = addiu_from_zero.effects();
        assert_eq!(effects.gpr_read_mask, 0);
        assert_eq!(effects.gpr_write_mask, 1 << 7);
    }

    #[test]
    fn effects_track_hilo_producers_and_consumers() {
        let mult = decode((4 << 21) | (5 << 16) | 0x18, 0x2000).effects();
        assert_eq!(mult.gpr_read_mask, (1 << 4) | (1 << 5));
        assert!(mult.writes_hi);
        assert!(mult.writes_lo);
        assert!(!mult.reads_hi);
        assert!(!mult.reads_lo);

        let mfhi = decode((6 << 11) | 0x10, 0x2004).effects();
        assert!(mfhi.reads_hi);
        assert!(!mfhi.reads_lo);
        assert_eq!(mfhi.gpr_write_mask, 1 << 6);
    }

    #[test]
    fn effects_distinguish_partial_load_merge_and_store_dependencies() {
        let lwl = decode((0x22 << 26) | (4 << 21) | (5 << 16) | 1, 0x3000).effects();
        assert_eq!(lwl.gpr_read_mask, (1 << 4) | (1 << 5));
        assert_eq!(lwl.gpr_write_mask, 1 << 5);
        assert_eq!(lwl.memory, MemoryEffect::Read);
        assert!(lwl.may_raise_exception);

        let swl = decode((0x2A << 26) | (4 << 21) | (5 << 16) | 1, 0x3004).effects();
        assert_eq!(swl.gpr_read_mask, (1 << 4) | (1 << 5));
        assert_eq!(swl.gpr_write_mask, 0);
        assert_eq!(swl.memory, MemoryEffect::Write);
        assert!(swl.may_raise_exception);
    }

    #[test]
    fn effects_include_link_register_and_control_flow_classification() {
        let bgezal = decode((0x01 << 26) | (4 << 21) | (0x11 << 16) | 1, 0x4000);
        let effects = bgezal.effects();
        assert_eq!(effects.gpr_read_mask, 1 << 4);
        assert_eq!(effects.gpr_write_mask, 1 << 31);
        assert_eq!(effects.control_flow, ControlFlowEffect::ConditionalCall);

        let jal = decode((0x03 << 26) | 0x20, 0x4004).effects();
        assert_eq!(jal.gpr_write_mask, 1 << 31);
        assert_eq!(jal.control_flow, ControlFlowEffect::DirectCall);

        let jalr = decode((4 << 21) | (7 << 11) | 0x09, 0x4008).effects();
        assert_eq!(jalr.gpr_read_mask, 1 << 4);
        assert_eq!(jalr.gpr_write_mask, 1 << 7);
        assert_eq!(jalr.control_flow, ControlFlowEffect::IndirectCall);

        let jalr_without_link = decode((4 << 21) | 0x09, 0x400C).effects();
        assert_eq!(jalr_without_link.gpr_read_mask, 1 << 4);
        assert_eq!(jalr_without_link.gpr_write_mask, 0);
        assert_eq!(
            jalr_without_link.control_flow,
            ControlFlowEffect::IndirectJump
        );
    }

    #[test]
    fn effects_mark_traps_overflow_and_unknown_encodings_conservatively() {
        let add = decode((4 << 21) | (5 << 16) | (6 << 11) | 0x20, 0x5000).effects();
        assert!(add.may_raise_exception);

        let syscall = decode(0x0000_000C, 0x5004).effects();
        assert_eq!(syscall.control_flow, ControlFlowEffect::Trap);
        assert!(syscall.may_raise_exception);

        let unknown = decode(0xEC00_0000, 0x5008).effects();
        assert!(unknown.unknown);
        assert_eq!(unknown.gpr_read_mask, 0xFFFF_FFFE);
        assert_eq!(unknown.gpr_write_mask, 0xFFFF_FFFE);
        assert_eq!(unknown.memory, MemoryEffect::Unknown);
        assert_eq!(unknown.control_flow, ControlFlowEffect::Unknown);
        assert!(unknown.reads_hi && unknown.reads_lo);
        assert!(unknown.writes_hi && unknown.writes_lo);
        assert!(unknown.may_raise_exception);
    }

    #[test]
    fn decodes_r5900_three_operand_multiply_and_hilo_moves() {
        let mult = decode((8 << 21) | (9 << 16) | (10 << 11) | 0x18, 0x6000);
        assert_eq!(mult.opcode, Opcode::Mult);
        assert_eq!((mult.rs, mult.rt, mult.rd), (8, 9, 10));
        let multu = decode((8 << 21) | (9 << 16) | (11 << 11) | 0x19, 0x6004);
        assert_eq!(multu.opcode, Opcode::Multu);
        assert_eq!((multu.rs, multu.rt, multu.rd), (8, 9, 11));
        let mult_to_zero = decode((8 << 21) | (9 << 16) | 0x18, 0x6008);
        assert_eq!(mult_to_zero.opcode, Opcode::Mult);
        assert_eq!(mult_to_zero.rd, 0);
        assert_eq!(decode((4 << 21) | 0x11, 0x600C).opcode, Opcode::Mthi);
        assert_eq!(decode((5 << 21) | 0x13, 0x6010).opcode, Opcode::Mtlo);
        assert_eq!(decode((20 << 11) | 0x10, 0x6014).opcode, Opcode::Mfhi);
        assert_eq!(decode((21 << 11) | 0x12, 0x6018).opcode, Opcode::Mflo);
        // DIV/DIVU keep their reserved rd field; only MULT/MULTU gained rd.
        assert_eq!(
            decode((8 << 21) | (9 << 16) | (10 << 11) | 0x1A, 0x601C).opcode,
            Opcode::Unknown
        );
        assert_eq!(
            decode((8 << 21) | (9 << 16) | 0x1B, 0x6020).opcode,
            Opcode::Divu
        );
        assert_eq!(
            decode((8 << 21) | (9 << 16) | (10 << 11) | 0x18 | (1 << 6), 0x6024).opcode,
            Opcode::Unknown
        );
    }

    #[test]
    fn effects_model_r5900_hilo_moves_and_three_operand_multiply() {
        let mthi = decode((4 << 21) | 0x11, 0x6100).effects();
        assert_eq!(mthi.gpr_read_mask, 1 << 4);
        assert_eq!(mthi.gpr_write_mask, 0);
        assert!(mthi.writes_hi);
        assert!(!mthi.writes_lo);
        let mtlo = decode((5 << 21) | 0x13, 0x6104).effects();
        assert_eq!(mtlo.gpr_read_mask, 1 << 5);
        assert!(mtlo.writes_lo);
        assert!(!mtlo.writes_hi);
        let mfhi = decode((12 << 11) | 0x10, 0x6108).effects();
        assert!(mfhi.reads_hi);
        assert_eq!(mfhi.gpr_write_mask, 1 << 12);
        let mflo = decode((13 << 11) | 0x12, 0x610C).effects();
        assert!(mflo.reads_lo);
        assert_eq!(mflo.gpr_write_mask, 1 << 13);
        let mult = decode((4 << 21) | (5 << 16) | (6 << 11) | 0x18, 0x6110).effects();
        assert_eq!(mult.gpr_read_mask, (1 << 4) | (1 << 5));
        assert_eq!(mult.gpr_write_mask, 1 << 6);
        assert!(mult.writes_hi && mult.writes_lo);
        let mult_to_zero = decode((4 << 21) | (5 << 16) | 0x19, 0x6114).effects();
        assert_eq!(mult_to_zero.gpr_write_mask, 0);
        assert!(mult_to_zero.writes_hi && mult_to_zero.writes_lo);
        let div = decode((4 << 21) | (5 << 16) | 0x1A, 0x6118).effects();
        assert_eq!(div.gpr_write_mask, 0);
        assert!(div.writes_hi && div.writes_lo);
    }

    #[test]
    fn decodes_r5900_compare_and_nor_operations() {
        let register_cases = [
            (((4 << 21) | (5 << 16) | (6 << 11) | 0x27), Opcode::Nor),
            (((4 << 21) | (5 << 16) | (7 << 11) | 0x2A), Opcode::Slt),
            (((4 << 21) | (5 << 16) | (8 << 11) | 0x2B), Opcode::Sltu),
        ];
        for (word, expected) in register_cases {
            let decoded = decode(word, 0x7000);
            assert_eq!(decoded.opcode, expected, "word 0x{word:08X}");
            assert_eq!((decoded.rs, decoded.rt), (4, 5));
            assert_eq!(decoded.shift_amount, 0);
            assert!(!decoded.has_delay_slot);
        }

        let immediate_cases = [
            (
                ((0x0A << 26) | (9 << 21) | (10 << 16) | 0x8001),
                Opcode::Slti,
            ),
            (
                ((0x0B << 26) | (11 << 21) | (12 << 16) | 0xFFFF),
                Opcode::Sltiu,
            ),
        ];
        for (word, expected) in immediate_cases {
            let decoded = decode(word, 0x7100);
            assert_eq!(decoded.opcode, expected, "word 0x{word:08X}");
            assert_eq!(
                decoded.signed_immediate(),
                if expected == Opcode::Slti { -32767 } else { -1 }
            );
        }
    }

    #[test]
    fn effects_track_compare_and_nor_dependencies_without_exceptions() {
        let nor = decode((4 << 21) | (5 << 16) | (6 << 11) | 0x27, 0x7200).effects();
        assert_eq!(nor.gpr_read_mask, (1 << 4) | (1 << 5));
        assert_eq!(nor.gpr_write_mask, 1 << 6);
        assert_eq!(nor.memory, MemoryEffect::None);
        assert!(!nor.may_raise_exception);

        let slt = decode((4 << 21) | (5 << 16) | (7 << 11) | 0x2A, 0x7204).effects();
        assert_eq!(slt.gpr_read_mask, (1 << 4) | (1 << 5));
        assert_eq!(slt.gpr_write_mask, 1 << 7);
        assert!(!slt.may_raise_exception);

        let sltiu = decode((0x0B << 26) | (8 << 21) | (9 << 16) | 0xFFFF, 0x7208).effects();
        assert_eq!(sltiu.gpr_read_mask, 1 << 8);
        assert_eq!(sltiu.gpr_write_mask, 1 << 9);
        assert!(!sltiu.may_raise_exception);
    }

    #[test]
    fn decodes_signed_doubleword_arithmetic_and_reserved_fields() {
        let cases = [
            (((8 << 21) | (9 << 16) | (10 << 11) | 0x2C), Opcode::Dadd),
            (((12 << 21) | (13 << 16) | (11 << 11) | 0x2E), Opcode::Dsub),
            (
                ((0x18 << 26) | (15 << 21) | (14 << 16) | 0xFFFE),
                Opcode::Daddi,
            ),
        ];
        for (word, expected) in cases {
            let decoded = decode(word, 0xB000);
            assert_eq!(decoded.opcode, expected, "word 0x{word:08X}");
            assert!(!decoded.has_delay_slot);
        }
        let dadd = decode((8 << 21) | (9 << 16) | (10 << 11) | 0x2C, 0xB000);
        assert_eq!((dadd.rs, dadd.rt, dadd.rd), (8, 9, 10));
        let daddi = decode((0x18 << 26) | (15 << 21) | (14 << 16) | 0xFFFE, 0xB004);
        assert_eq!((daddi.rs, daddi.rt), (15, 14));
        assert_eq!(daddi.signed_immediate(), -2);
        let malformed = (8 << 21) | (9 << 16) | (10 << 11) | (1 << 6) | 0x2C;
        assert_eq!(decode(malformed, 0xB008).opcode, Opcode::Unknown);
    }

    #[test]
    fn signed_doubleword_arithmetic_effects_mark_overflow_and_zero_destination() {
        let dadd = decode((8 << 21) | (9 << 16) | (10 << 11) | 0x2C, 0xB100).effects();
        assert_eq!(dadd.gpr_read_mask, (1 << 8) | (1 << 9));
        assert_eq!(dadd.gpr_write_mask, 1 << 10);
        assert!(dadd.may_raise_exception);
        let dsub = decode((12 << 21) | (13 << 16) | (11 << 11) | 0x2E, 0xB104).effects();
        assert_eq!(dsub.gpr_read_mask, (1 << 12) | (1 << 13));
        assert_eq!(dsub.gpr_write_mask, 1 << 11);
        assert!(dsub.may_raise_exception);
        let daddi = decode((0x18 << 26) | (15 << 21) | (14 << 16) | 0xFFFE, 0xB108).effects();
        assert_eq!(daddi.gpr_read_mask, 1 << 15);
        assert_eq!(daddi.gpr_write_mask, 1 << 14);
        assert!(daddi.may_raise_exception);
        let dadd_to_zero = decode((8 << 21) | (9 << 16) | 0x2C, 0xB10C).effects();
        assert_eq!(dadd_to_zero.gpr_write_mask, 0);
        assert!(dadd_to_zero.may_raise_exception);
    }

    #[test]
    fn decodes_r5900_sa_instructions_and_reserved_fields() {
        let mfsa = 0x0000_2828; // mfsa $5 (rd=5, funct=0x28)
        let mtsa = 0x0080_0029; // mtsa $4 (rs=4, funct=0x29)
        let mtsab = (0x01 << 26) | (4 << 21) | (0x18 << 16) | 0x0007; // mtsab $4, 7
        let mtsah = (0x01 << 26) | (4 << 21) | (0x19 << 16) | 0x0003; // mtsah $4, 3

        let dec_mfsa = decode(mfsa, 0x1000);
        assert_eq!(dec_mfsa.opcode, Opcode::Mfsa);
        assert_eq!(dec_mfsa.rd, 5);
        assert!(!dec_mfsa.has_delay_slot);

        let dec_mtsa = decode(mtsa, 0x1004);
        assert_eq!(dec_mtsa.opcode, Opcode::Mtsa);
        assert_eq!(dec_mtsa.rs, 4);
        assert!(!dec_mtsa.has_delay_slot);

        let dec_mtsab = decode(mtsab, 0x1008);
        assert_eq!(dec_mtsab.opcode, Opcode::Mtsab);
        assert_eq!(dec_mtsab.rs, 4);
        assert_eq!(dec_mtsab.immediate, 7);
        assert!(!dec_mtsab.has_delay_slot);

        let dec_mtsah = decode(mtsah, 0x100C);
        assert_eq!(dec_mtsah.opcode, Opcode::Mtsah);
        assert_eq!(dec_mtsah.rs, 4);
        assert_eq!(dec_mtsah.immediate, 3);
        assert!(!dec_mtsah.has_delay_slot);

        // Reserved fields for MFSA (rs, rt, shamt must be zero)
        assert!(!decode(mfsa | (1 << 21), 0).is_supported());
        assert!(!decode(mfsa | (1 << 16), 0).is_supported());
        assert!(!decode(mfsa | (1 << 6), 0).is_supported());

        // Reserved fields for MTSA (rt, rd, shamt must be zero)
        assert!(!decode(mtsa | (1 << 16), 0).is_supported());
        assert!(!decode(mtsa | (1 << 11), 0).is_supported());
        assert!(!decode(mtsa | (1 << 6), 0).is_supported());
    }

    #[test]
    fn effects_track_sa_register_dependencies() {
        let mfsa = decode(0x0000_2828, 0).effects();
        assert!(mfsa.reads_sa);
        assert!(!mfsa.writes_sa);
        assert_eq!(mfsa.gpr_write_mask, 1 << 5);
        assert_eq!(mfsa.gpr_read_mask, 0);

        let mtsa = decode(0x0080_0029, 0).effects();
        assert!(!mtsa.reads_sa);
        assert!(mtsa.writes_sa);
        assert_eq!(mtsa.gpr_read_mask, 1 << 4);
        assert_eq!(mtsa.gpr_write_mask, 0);

        let mtsab = decode((0x01 << 26) | (4 << 21) | (0x18 << 16) | 7, 0).effects();
        assert!(!mtsab.reads_sa);
        assert!(mtsab.writes_sa);
        assert_eq!(mtsab.gpr_read_mask, 1 << 4);

        let mtsah = decode((0x01 << 26) | (4 << 21) | (0x19 << 16) | 3, 0).effects();
        assert!(!mtsah.reads_sa);
        assert!(mtsah.writes_sa);
        assert_eq!(mtsah.gpr_read_mask, 1 << 4);
    }

    #[test]
    fn decodes_mmi1_qfsrv_and_tracks_sa_dependency() {
        // qfsrv rd=$6, rs=$4, rt=$5: MMI1 selector bits 10..6 = 0x1B, funct 0x28.
        let word = 0x7085_36E8;
        let decoded = decode(word, 0x2000);
        assert_eq!(decoded.opcode, Opcode::FunnelShiftRightVec);
        assert_eq!(decoded.rs, 4);
        assert_eq!(decoded.rt, 5);
        assert_eq!(decoded.rd, 6);
        assert!(!decoded.has_delay_slot);

        let effects = decoded.effects();
        assert!(effects.reads_sa);
        assert!(!effects.writes_sa);
        assert_eq!(effects.gpr_read_mask, (1 << 4) | (1 << 5));
        assert_eq!(effects.gpr_write_mask, 1 << 6);

        // MMI group selectors keep the shared funct 0x28 space unambiguous:
        // selector 0x10 is PADDUW, selector 0x1B is QFSRV.
        assert_eq!(
            decode(word & !(0x1F << 6) | (0x10 << 6), 0).opcode,
            Opcode::ParallelAddUnsignedWord
        );
    }

    #[test]
    fn decodes_cop1_register_and_control_transfers() {
        // GNU binutils masks: mfc1 0x44000000/0xffe007ff, mtc1 0x44800000/
        // 0xffe007ff, cfc1 0x44400000, ctc1 0x44c00000. The selector lives in
        // bits 25..21, so bit 25 is part of the format field, not a COP1/COP2
        // flag; COP2 (VU0) has its own primary opcode 0x12.
        let cases = [
            (
                0x4402_2800_u32,
                Opcode::MoveFromFpuRegister,
                0u8,
                2u8,
                5u8,
                "mfc1",
            ),
            (0x4480_0000, Opcode::MoveToFpuRegister, 4, 0, 0, "mtc1"),
            (0x4443_F800, Opcode::MoveFromFpuControl, 2, 3, 31, "cfc1"),
            (0x44C4_F800, Opcode::MoveToFpuControl, 6, 4, 31, "ctc1"),
        ];
        for (word, expected, fmt, gpr, fpu, mnemonic) in cases {
            let decoded = decode(word, 0x1000);
            assert_eq!(decoded.opcode, expected, "word 0x{word:08X}");
            assert_eq!(decoded.rs, fmt, "word 0x{word:08X} format field");
            assert_eq!(decoded.rt, gpr, "word 0x{word:08X} GPR field");
            assert_eq!(decoded.rd, fpu, "word 0x{word:08X} FPR/FCR field");
            assert_eq!(decoded.opcode.mnemonic(), mnemonic);
            assert!(!decoded.has_delay_slot);
            assert!(!decoded.is_control_transfer());
        }

        // The EE FPU is single precision only: control numbers other than FIR
        // (FCR0) and FCSR (FCR31) are not implemented encodings.
        assert_eq!(decode(0x4443_2800, 0).opcode, Opcode::Unknown);
        // CTC1 of FIR would silently discard the write, so it stays unsupported.
        assert_eq!(decode(0x44C4_0000, 0).opcode, Opcode::Unknown);
        // Bits 10..0 are reserved zero for all four transfer formats.
        for reserved in [1_u32, 1 << 6, 1 << 10] {
            assert_eq!(decode(0x4402_2800 | reserved, 0).opcode, Opcode::Unknown);
            assert_eq!(decode(0x4480_0000 | reserved, 0).opcode, Opcode::Unknown);
        }
        // Formats the EE does not define: CFC1/CTC1 of an undefined control
        // number and the untouched COP1 register-transfer slots.
        assert_eq!(
            decode((0x11 << 26) | (1 << 21) | (2 << 16), 0).opcode,
            Opcode::Unknown
        );
        assert_eq!(
            decode((0x11 << 26) | (3 << 21) | (2 << 16), 0).opcode,
            Opcode::Unknown
        );
        // Setting bit 25 moves mfc1's format selector from 0 to 16, which is
        // S-class arithmetic, so the bit is part of the selector and not a
        // COP1/COP2 flag. The remaining fields then read as ADD.S $f0, $f5, $f2.
        let arithmetic = decode(0x4402_2800 | (1 << 25), 0);
        assert_eq!(arithmetic.opcode, Opcode::FpuAddSingle);
        assert_eq!(
            (arithmetic.rd, arithmetic.rt, arithmetic.shift_amount),
            (5, 2, 0)
        );
        // COP2 (VU0) is a separate primary opcode and stays unsupported here.
        assert_eq!(
            decode((0x12 << 26) | (2 << 16) | (5 << 11), 0).opcode,
            Opcode::Unknown
        );
    }

    #[test]
    fn decodes_cop1_single_precision_arithmetic() {
        // Distinct operand numbers in every word make a swapped field change
        // the decoded values, not just the mnemonic.
        let cases = [
            (0x00, Opcode::FpuAddSingle, "add.s"),
            (0x01, Opcode::FpuSubtractSingle, "sub.s"),
            (0x02, Opcode::FpuMultiplySingle, "mul.s"),
            (0x03, Opcode::FpuDivideSingle, "div.s"),
            (0x16, Opcode::FpuReciprocalSquareRootSingle, "rsqrt.s"),
            (0x1C, Opcode::FpuMultiplyAddSingle, "madd.s"),
            (0x1D, Opcode::FpuMultiplySubtractSingle, "msub.s"),
            (0x28, Opcode::FpuMaximumSingle, "max.s"),
            (0x29, Opcode::FpuMinimumSingle, "min.s"),
        ];
        for (funct, opcode, mnemonic) in cases {
            let decoded = decode(encode_cop1(0x10, 2, 1, 3, funct), 0x2000);
            assert_eq!(decoded.opcode, opcode, "funct 0x{funct:02X}");
            assert_eq!(decoded.opcode.mnemonic(), mnemonic);
            assert_eq!((decoded.rt, decoded.rd, decoded.shift_amount), (2, 1, 3));
            assert!(!decoded.has_delay_slot);
            assert!(!decoded.is_control_transfer());
        }

        // SQRT.S takes its operand from ft and reserves fs.
        let sqrt = decode(encode_cop1(0x10, 2, 0, 3, 0x04), 0);
        assert_eq!(sqrt.opcode, Opcode::FpuSquareRootSingle);
        assert_eq!((sqrt.rt, sqrt.rd, sqrt.shift_amount), (2, 0, 3));

        // The unary forms take fs and reserve ft.
        for (funct, opcode, mnemonic) in [
            (0x05, Opcode::FpuAbsoluteSingle, "abs.s"),
            (0x06, Opcode::FpuMoveSingle, "mov.s"),
            (0x07, Opcode::FpuNegateSingle, "neg.s"),
            (0x24, Opcode::FpuConvertSingleToWord, "cvt.w.s"),
        ] {
            let decoded = decode(encode_cop1(0x10, 0, 1, 3, funct), 0);
            assert_eq!(decoded.opcode, opcode, "funct 0x{funct:02X}");
            assert_eq!(decoded.opcode.mnemonic(), mnemonic);
            assert_eq!((decoded.rd, decoded.shift_amount), (1, 3));
        }

        // The accumulate forms have no destination register: fd is reserved.
        for (funct, opcode, mnemonic) in [
            (0x18, Opcode::FpuAddAccumulateSingle, "adda.s"),
            (0x19, Opcode::FpuSubtractAccumulateSingle, "suba.s"),
            (0x1A, Opcode::FpuMultiplyAccumulateSingle, "mula.s"),
            (0x1E, Opcode::FpuMultiplyAddAccumulateSingle, "madda.s"),
            (0x1F, Opcode::FpuMultiplySubtractAccumulateSingle, "msuba.s"),
        ] {
            let decoded = decode(encode_cop1(0x10, 2, 1, 0, funct), 0);
            assert_eq!(decoded.opcode, opcode, "funct 0x{funct:02X}");
            assert_eq!(decoded.opcode.mnemonic(), mnemonic);
            assert_eq!((decoded.rt, decoded.rd, decoded.shift_amount), (2, 1, 0));
        }

        // CVT.S.W is the only W-class (format 0x14) instruction.
        let cvt = decode(encode_cop1(0x14, 0, 1, 3, 0x20), 0);
        assert_eq!(cvt.opcode, Opcode::FpuConvertWordToSingle);
        assert_eq!((cvt.rt, cvt.rd, cvt.shift_amount), (0, 1, 3));
    }

    #[test]
    fn rejects_cop1_arithmetic_with_a_nonzero_reserved_field() {
        // SQRT.S reserves fs; ABS/MOV/NEG/CVT.W.S reserve ft; the accumulate
        // forms reserve fd; CVT.S.W reserves ft.
        assert_eq!(
            decode(encode_cop1(0x10, 2, 1, 3, 0x04), 0).opcode,
            Opcode::Unknown
        );
        for funct in [0x05, 0x06, 0x07, 0x24] {
            assert_eq!(
                decode(encode_cop1(0x10, 2, 1, 3, funct), 0).opcode,
                Opcode::Unknown,
                "funct 0x{funct:02X} must reserve ft"
            );
        }
        for funct in [0x18, 0x19, 0x1A, 0x1E, 0x1F] {
            assert_eq!(
                decode(encode_cop1(0x10, 2, 1, 3, funct), 0).opcode,
                Opcode::Unknown,
                "funct 0x{funct:02X} must reserve fd"
            );
        }
        assert_eq!(
            decode(encode_cop1(0x14, 2, 1, 3, 0x20), 0).opcode,
            Opcode::Unknown
        );
        // CVT.S.W uses funct 0x20; the S-class value is not a W-class encoding.
        assert_eq!(
            decode(encode_cop1(0x14, 0, 1, 3, 0x24), 0).opcode,
            Opcode::Unknown
        );
        // Undefined S-class functions and unimplemented COP1 formats.
        for funct in [0x08, 0x20, 0x2A] {
            assert_eq!(
                decode(encode_cop1(0x10, 2, 1, 3, funct), 0).opcode,
                Opcode::Unknown,
                "funct 0x{funct:02X}"
            );
        }
        for format in [0x01, 0x08, 0x12, 0x13, 0x15] {
            assert_eq!(
                decode(encode_cop1(format, 2, 1, 3, 0x00), 0).opcode,
                Opcode::Unknown,
                "format 0x{format:02X}"
            );
        }
    }

    #[test]
    fn effects_track_cop1_arithmetic_dependencies() {
        let add = decode(encode_cop1(0x10, 2, 1, 3, 0x00), 0).effects();
        assert_eq!(add.fpr_read_mask, (1 << 1) | (1 << 2));
        assert_eq!(add.fpr_write_mask, 1 << 3);
        assert!(add.writes_fcr);
        assert!(!add.reads_fcr);
        assert!(!add.reads_acc);
        assert!(!add.writes_acc);
        assert_eq!(add.memory, MemoryEffect::None);
        assert!(!add.may_raise_exception);

        // A reserved-zero field must not become a dependency on FPR[0].
        let sqrt = decode(encode_cop1(0x10, 2, 0, 3, 0x04), 0).effects();
        assert_eq!(sqrt.fpr_read_mask, 1 << 2);
        assert_eq!(sqrt.fpr_write_mask, 1 << 3);
        let abs = decode(encode_cop1(0x10, 0, 1, 3, 0x05), 0).effects();
        assert_eq!(abs.fpr_read_mask, 1 << 1);
        assert_eq!(abs.fpr_write_mask, 1 << 3);

        let madd = decode(encode_cop1(0x10, 2, 1, 3, 0x1C), 0).effects();
        assert!(madd.reads_acc);
        assert!(!madd.writes_acc);
        assert_eq!(madd.fpr_read_mask, (1 << 1) | (1 << 2));
        assert_eq!(madd.fpr_write_mask, 1 << 3);
        assert!(madd.writes_fcr);

        let adda = decode(encode_cop1(0x10, 2, 1, 0, 0x18), 0).effects();
        assert!(!adda.reads_acc);
        assert!(adda.writes_acc);
        assert_eq!(adda.fpr_write_mask, 0);
        assert_eq!(adda.fpr_read_mask, (1 << 1) | (1 << 2));

        let madda = decode(encode_cop1(0x10, 2, 1, 0, 0x1E), 0).effects();
        assert!(madda.reads_acc);
        assert!(madda.writes_acc);
        assert_eq!(madda.fpr_write_mask, 0);
    }

    #[test]
    fn decodes_lwc1_and_swc1_word_memory_forms() {
        // LWC1 is primary 0x31 and SWC1 is primary 0x39. 0x35 is LDC1, which
        // the single-precision-only EE does not implement; SWC2 is 0x3A.
        let load = decode(0xC506_0010, 0x1000);
        assert_eq!(load.opcode, Opcode::LoadFpuWord);
        assert_eq!((load.rs, load.rt, load.rd), (8, 6, 0));
        assert_eq!(load.signed_immediate(), 16);
        let store = decode(0xE506_FFFC, 0x1000);
        assert_eq!(store.opcode, Opcode::StoreFpuWord);
        assert_eq!((store.rs, store.rt), (8, 6));
        assert_eq!(store.signed_immediate(), -4);
        assert_eq!(decode(0xD506_0010, 0).opcode, Opcode::Unknown);
    }

    #[test]
    fn effects_track_cop1_fpr_and_fcr_dependencies() {
        // FPR[0] is a real register, unlike GPR[0], so bit 0 must be set.
        let mfc1 = decode(0x4402_2800, 0).effects();
        assert_eq!(mfc1.fpr_read_mask, 1 << 5);
        assert_eq!(mfc1.fpr_write_mask, 0);
        assert_eq!(mfc1.gpr_write_mask, 1 << 2);
        assert_eq!(mfc1.gpr_read_mask, 0);
        assert_eq!(mfc1.memory, MemoryEffect::None);
        assert!(!mfc1.may_raise_exception);

        let mtc1 = decode(0x4480_0000, 0).effects();
        assert_eq!(mtc1.fpr_write_mask, 1);
        assert_eq!(mtc1.gpr_read_mask, 0);
        assert!(!mtc1.may_raise_exception);

        let cfc1_fir = decode(0x4443_0000, 0).effects();
        assert!(!cfc1_fir.reads_fcr);
        assert!(!cfc1_fir.writes_fcr);
        assert_eq!(cfc1_fir.gpr_write_mask, 1 << 3);
        let cfc1_fcsr = decode(0x4443_F800, 0).effects();
        assert!(cfc1_fcsr.reads_fcr);
        assert!(!cfc1_fcsr.writes_fcr);

        let ctc1 = decode(0x44C4_F800, 0).effects();
        assert!(ctc1.writes_fcr);
        assert!(!ctc1.reads_fcr);
        assert_eq!(ctc1.gpr_read_mask, 1 << 4);

        let load = decode(0xC506_0010, 0).effects();
        assert_eq!(load.gpr_read_mask, 1 << 8);
        assert_eq!(load.fpr_write_mask, 1 << 6);
        assert_eq!(load.memory, MemoryEffect::Read);
        assert!(load.may_raise_exception);

        let store = decode(0xE506_FFFC, 0).effects();
        assert_eq!(store.gpr_read_mask, 1 << 8);
        assert_eq!(store.fpr_read_mask, 1 << 6);
        assert_eq!(store.fpr_write_mask, 0);
        assert_eq!(store.memory, MemoryEffect::Write);
        assert!(store.may_raise_exception);
    }
}
