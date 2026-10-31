//! Typed control-flow IR built from the reachability-guided CFG.
//!
//! This layer composes scalar operations into verified blocks and preserves
//! architectural control transfers and delay slots as typed terminators.

use std::collections::BTreeMap;
use std::fmt;

use crate::analysis::{BranchCondition, ControlFlowGraph, Terminator};
use crate::ir::{self, Instruction as ScalarInstruction, LiftError, VerifyError as ScalarVerifyError};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ControlBlock {
    pub start_pc: u32,
    pub instructions: Vec<ScalarInstruction>,
    pub terminator: ControlTerminator,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ControlTerminator {
    Fallthrough { next_pc: u32 },
    ConditionalBranch {
        guest_pc: u32,
        condition: BranchCondition,
        target_pc: u32,
        fallthrough_pc: u32,
        link_register: Option<u8>,
        delay_slot: ScalarInstruction,
    },
    DirectJump {
        guest_pc: u32,
        target_pc: u32,
        link_register: Option<u8>,
        return_pc: Option<u32>,
        delay_slot: ScalarInstruction,
    },
    IndirectJump {
        guest_pc: u32,
        source_register: u8,
        link_register: Option<u8>,
        return_pc: Option<u32>,
        delay_slot: ScalarInstruction,
    },
    Return {
        guest_pc: u32,
        source_register: u8,
        delay_slot: ScalarInstruction,
    },
    Unsupported { guest_pc: u32, mnemonic: String },
    SegmentEnd { next_pc: u32 },
}

impl ControlTerminator {
    fn guest_pc(&self) -> Option<u32> {
        match self {
            Self::ConditionalBranch { guest_pc, .. }
            | Self::DirectJump { guest_pc, .. }
            | Self::IndirectJump { guest_pc, .. }
            | Self::Return { guest_pc, .. }
            | Self::Unsupported { guest_pc, .. } => Some(*guest_pc),
            Self::Fallthrough { .. } | Self::SegmentEnd { .. } => None,
        }
    }

    fn delay_slot(&self) -> Option<&ScalarInstruction> {
        match self {
            Self::ConditionalBranch { delay_slot, .. }
            | Self::DirectJump { delay_slot, .. }
            | Self::IndirectJump { delay_slot, .. }
            | Self::Return { delay_slot, .. } => Some(delay_slot),
            Self::Fallthrough { .. } | Self::Unsupported { .. } | Self::SegmentEnd { .. } => None,
        }
    }

    #[must_use]
    pub fn instruction_count(&self) -> usize {
        match self {
            Self::ConditionalBranch { .. }
            | Self::DirectJump { .. }
            | Self::IndirectJump { .. }
            | Self::Return { .. } => 2,
            Self::Unsupported { .. } => 1,
            Self::Fallthrough { .. } | Self::SegmentEnd { .. } => 0,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ControlProgram {
    pub entry_points: Vec<u32>,
    pub blocks: BTreeMap<u32, ControlBlock>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ControlError {
    EmptyProgram,
    NoEntryPoints,
    InvalidEntryPoint(u32),
    BlockKeyMismatch { key: u32, start_pc: u32 },
    MisalignedPc(u32),
    GuestPcOverflow(u32),
    NonSequentialBody { expected: u32, actual: u32 },
    TerminatorPcMismatch { expected: u32, actual: u32 },
    DelaySlotPcMismatch { expected: u32, actual: u32 },
    InvalidRegister(u8),
    ScalarLift(LiftError),
    ScalarVerify(ScalarVerifyError),
}

impl fmt::Display for ControlError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::EmptyProgram => formatter.write_str("control-flow program has no blocks"),
            Self::NoEntryPoints => formatter.write_str("control-flow program has no entry points"),
            Self::InvalidEntryPoint(pc) => write!(formatter, "entry point 0x{pc:08X} has no translated block"),
            Self::BlockKeyMismatch { key, start_pc } => write!(
                formatter,
                "control-flow block key 0x{key:08X} does not match its start PC 0x{start_pc:08X}"
            ),
            Self::MisalignedPc(pc) => write!(formatter, "unaligned control-flow PC 0x{pc:08X}"),
            Self::GuestPcOverflow(pc) => write!(formatter, "guest PC overflows after 0x{pc:08X}"),
            Self::NonSequentialBody { expected, actual } => write!(
                formatter,
                "control-flow block expected guest instruction at 0x{expected:08X}, found 0x{actual:08X}"
            ),
            Self::TerminatorPcMismatch { expected, actual } => write!(
                formatter,
                "control-flow terminator expected at 0x{expected:08X}, found 0x{actual:08X}"
            ),
            Self::DelaySlotPcMismatch { expected, actual } => write!(
                formatter,
                "delay slot expected at 0x{expected:08X}, found 0x{actual:08X}"
            ),
            Self::InvalidRegister(register) => write!(formatter, "invalid control-flow GPR index {register}"),
            Self::ScalarLift(error) => write!(formatter, "scalar IR lift failed: {error}"),
            Self::ScalarVerify(error) => write!(formatter, "scalar IR verification failed: {error}"),
        }
    }
}

impl std::error::Error for ControlError {}

impl ControlProgram {
    /// Builds typed control-flow IR from a discovered CFG.
    ///
    /// Instructions outside the scalar lowering subset become explicit
    /// unsupported terminators; they are never silently replaced with NOPs.
    pub fn lift(graph: &ControlFlowGraph) -> Result<Self, ControlError> {
        let mut blocks = BTreeMap::new();

        for (start_pc, block) in &graph.blocks {
            let mut instructions = Vec::with_capacity(block.instructions.len());
            let mut unsupported = None;

            for decoded in &block.instructions {
                match ir::lift(std::slice::from_ref(decoded)) {
                    Ok(program) => instructions.extend_from_slice(program.instructions()),
                    Err(LiftError::UnsupportedOpcode { pc, mnemonic }) => {
                        unsupported = Some((pc, mnemonic.to_owned()));
                        break;
                    }
                    Err(error @ LiftError::Verification(_)) => {
                        return Err(ControlError::ScalarLift(error));
                    }
                }
            }

            let terminator = if let Some((guest_pc, mnemonic)) = unsupported {
                ControlTerminator::Unsupported { guest_pc, mnemonic }
            } else {
                lift_terminator(&block.terminator)?
            };

            blocks.insert(*start_pc, ControlBlock {
                start_pc: *start_pc,
                instructions,
                terminator,
            });
        }

        let program = Self {
            entry_points: graph.entry_points.iter().map(|entry| entry.address).collect(),
            blocks,
        };
        program.verify()?;
        Ok(program)
    }

    pub fn verify(&self) -> Result<(), ControlError> {
        if self.blocks.is_empty() {
            return Err(ControlError::EmptyProgram);
        }
        if self.entry_points.is_empty() {
            return Err(ControlError::NoEntryPoints);
        }

        for entry in &self.entry_points {
            if !self.blocks.contains_key(entry) {
                return Err(ControlError::InvalidEntryPoint(*entry));
            }
        }

        for (key, block) in &self.blocks {
            if key != &block.start_pc {
                return Err(ControlError::BlockKeyMismatch {
                    key: *key,
                    start_pc: block.start_pc,
                });
            }
            if block.start_pc & 3 != 0 {
                return Err(ControlError::MisalignedPc(block.start_pc));
            }

            ir::Program::new(block.instructions.clone())
                .map_err(ControlError::ScalarVerify)?;

            let mut expected_pc = block.start_pc;
            for instruction in &block.instructions {
                if instruction.guest_pc != expected_pc {
                    return Err(ControlError::NonSequentialBody {
                        expected: expected_pc,
                        actual: instruction.guest_pc,
                    });
                }
                expected_pc = expected_pc.checked_add(4)
                    .ok_or(ControlError::GuestPcOverflow(expected_pc))?;
            }

            if let Some(terminator_pc) = block.terminator.guest_pc() {
                if terminator_pc != expected_pc {
                    return Err(ControlError::TerminatorPcMismatch {
                        expected: expected_pc,
                        actual: terminator_pc,
                    });
                }
            } else {
                let next_pc = match &block.terminator {
                    ControlTerminator::Fallthrough { next_pc }
                    | ControlTerminator::SegmentEnd { next_pc } => *next_pc,
                    _ => expected_pc,
                };
                if next_pc != expected_pc {
                    return Err(ControlError::TerminatorPcMismatch {
                        expected: expected_pc,
                        actual: next_pc,
                    });
                }
            }

            if let Some(delay_slot) = block.terminator.delay_slot() {
                ir::Program::new(vec![*delay_slot])
                    .map_err(ControlError::ScalarVerify)?;
                let control_pc = block.terminator.guest_pc()
                    .ok_or(ControlError::TerminatorPcMismatch {
                        expected: expected_pc,
                        actual: delay_slot.guest_pc,
                    })?;
                let expected_delay_pc = control_pc.checked_add(4)
                    .ok_or(ControlError::GuestPcOverflow(control_pc))?;
                if delay_slot.guest_pc != expected_delay_pc {
                    return Err(ControlError::DelaySlotPcMismatch {
                        expected: expected_delay_pc,
                        actual: delay_slot.guest_pc,
                    });
                }
            }

            verify_terminator_registers(&block.terminator)?;
            verify_terminator_targets(&block.terminator)?;
        }

        Ok(())
    }

    #[must_use]
    pub fn instruction_count(&self) -> usize {
        self.blocks.values()
            .map(|block| block.instructions.len() + block.terminator.instruction_count())
            .sum()
    }

    #[must_use]
    pub fn unsupported_count(&self) -> usize {
        self.blocks.values()
            .filter(|block| matches!(&block.terminator, ControlTerminator::Unsupported { .. }))
            .count()
    }
}

fn lift_terminator(terminator: &Terminator) -> Result<ControlTerminator, ControlError> {
    let lift_delay_slot = |decoded: &crate::mips::Instruction| -> Result<ScalarInstruction, ControlError> {
        let program = ir::lift(std::slice::from_ref(decoded))
            .map_err(ControlError::ScalarLift)?;
        program.instructions().first().copied()
            .ok_or(ControlError::TerminatorPcMismatch {
                expected: decoded.pc,
                actual: decoded.pc.wrapping_add(4),
            })
    };

    match terminator {
        Terminator::Fallthrough { next_pc } => Ok(ControlTerminator::Fallthrough { next_pc: *next_pc }),
        Terminator::ConditionalBranch {
            instruction, condition, target_pc, fallthrough_pc, delay_slot, link_register,
        } => Ok(ControlTerminator::ConditionalBranch {
            guest_pc: instruction.pc,
            condition: *condition,
            target_pc: *target_pc,
            fallthrough_pc: *fallthrough_pc,
            link_register: *link_register,
            delay_slot: lift_delay_slot(delay_slot)?,
        }),
        Terminator::DirectJump {
            instruction, target_pc, delay_slot, link_register, return_pc,
        } => Ok(ControlTerminator::DirectJump {
            guest_pc: instruction.pc,
            target_pc: *target_pc,
            link_register: *link_register,
            return_pc: *return_pc,
            delay_slot: lift_delay_slot(delay_slot)?,
        }),
        Terminator::IndirectJump {
            instruction, source_register, delay_slot, link_register, return_pc,
        } => Ok(ControlTerminator::IndirectJump {
            guest_pc: instruction.pc,
            source_register: *source_register,
            link_register: *link_register,
            return_pc: *return_pc,
            delay_slot: lift_delay_slot(delay_slot)?,
        }),
        Terminator::Return { instruction, source_register, delay_slot } => {
            Ok(ControlTerminator::Return {
                guest_pc: instruction.pc,
                source_register: *source_register,
                delay_slot: lift_delay_slot(delay_slot)?,
            })
        }
        Terminator::Unsupported { instruction } => Ok(ControlTerminator::Unsupported {
            guest_pc: instruction.pc,
            mnemonic: instruction.opcode.mnemonic().to_owned(),
        }),
        Terminator::SegmentEnd { next_pc } => Ok(ControlTerminator::SegmentEnd { next_pc: *next_pc }),
    }
}

fn verify_terminator_targets(terminator: &ControlTerminator) -> Result<(), ControlError> {
    let validate = |address: u32| {
        if address & 3 == 0 {
            Ok(())
        } else {
            Err(ControlError::MisalignedPc(address))
        }
    };

    match terminator {
        ControlTerminator::Fallthrough { next_pc }
        | ControlTerminator::SegmentEnd { next_pc } => validate(*next_pc)?,
        ControlTerminator::ConditionalBranch {
            guest_pc, target_pc, fallthrough_pc, ..
        } => {
            validate(*target_pc)?;
            validate(*fallthrough_pc)?;
            let expected_fallthrough = guest_pc.checked_add(8)
                .ok_or(ControlError::GuestPcOverflow(*guest_pc))?;
            if *fallthrough_pc != expected_fallthrough {
                return Err(ControlError::TerminatorPcMismatch {
                    expected: expected_fallthrough,
                    actual: *fallthrough_pc,
                });
            }
        }
        ControlTerminator::DirectJump {
            guest_pc, target_pc, link_register, return_pc, ..
        } => {
            validate(*target_pc)?;
            if let Some(return_pc) = return_pc { validate(*return_pc)?; }
            verify_link_continuation(*guest_pc, *link_register, *return_pc)?;
        }
        ControlTerminator::IndirectJump {
            guest_pc, link_register, return_pc, ..
        } => {
            if let Some(return_pc) = return_pc { validate(*return_pc)?; }
            verify_link_continuation(*guest_pc, *link_register, *return_pc)?;
        }
        ControlTerminator::Return { .. } | ControlTerminator::Unsupported { .. } => {}
    }
    Ok(())
}

fn verify_link_continuation(
    guest_pc: u32,
    link_register: Option<u8>,
    return_pc: Option<u32>,
) -> Result<(), ControlError> {
    match (link_register, return_pc) {
        (Some(_), Some(return_pc)) => {
            let expected = guest_pc.checked_add(8)
                .ok_or(ControlError::GuestPcOverflow(guest_pc))?;
            if return_pc != expected {
                return Err(ControlError::TerminatorPcMismatch {
                    expected,
                    actual: return_pc,
                });
            }
        }
        (None, None) => {}
        _ => {
            return Err(ControlError::TerminatorPcMismatch {
                expected: guest_pc.checked_add(8)
                    .ok_or(ControlError::GuestPcOverflow(guest_pc))?,
                actual: return_pc.unwrap_or(guest_pc),
            });
        }
    }
    Ok(())
}

fn verify_terminator_registers(terminator: &ControlTerminator) -> Result<(), ControlError> {
    let validate = |register: u8| {
        if register < 32 { Ok(()) } else { Err(ControlError::InvalidRegister(register)) }
    };

    match terminator {
        ControlTerminator::ConditionalBranch { condition, link_register, .. } => {
            match condition {
                BranchCondition::Equal { lhs, rhs } | BranchCondition::NotEqual { lhs, rhs } => {
                    validate(*lhs)?;
                    validate(*rhs)?;
                }
                BranchCondition::LessThanOrEqualZero { register }
                | BranchCondition::GreaterThanZero { register }
                | BranchCondition::LessThanZero { register }
                | BranchCondition::GreaterThanOrEqualZero { register } => validate(*register)?,
            }
            if let Some(register) = link_register { validate(*register)?; }
        }
        ControlTerminator::DirectJump { link_register, .. } => {
            if let Some(register) = link_register { validate(*register)?; }
        }
        ControlTerminator::IndirectJump { source_register, link_register, .. } => {
            validate(*source_register)?;
            if let Some(register) = link_register { validate(*register)?; }
        }
        ControlTerminator::Return { source_register, .. } => validate(*source_register)?,
        ControlTerminator::Fallthrough { .. }
        | ControlTerminator::Unsupported { .. }
        | ControlTerminator::SegmentEnd { .. } => {}
    }
    Ok(())
}

pub fn from_cfg(graph: &ControlFlowGraph) -> Result<ControlProgram, ControlError> {
    ControlProgram::lift(graph)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analysis::{self, AnalysisLimits, EntryPoint};
    use crate::elf::LoadedSegment;

    const PF_X: u32 = 1;

    fn segment(base: u32, words: &[u32]) -> LoadedSegment {
        let mut bytes = Vec::with_capacity(words.len() * 4);
        for word in words { bytes.extend_from_slice(&word.to_le_bytes()); }
        let file_size = bytes.len();
        LoadedSegment { virtual_address: base, flags: PF_X, file_size, bytes }
    }

    fn entry(address: u32) -> EntryPoint {
        EntryPoint { address, labels: vec!["control-ir-test".to_owned()], is_elf_entry: true }
    }

    #[test]
    fn lifts_branch_and_delay_slot_to_distinct_typed_nodes() {
        let words = [
            0x1100_0002, // beq t0, zero, 0x100C
            0x2409_0007, // delay slot
            0x240A_0005,
            0x03E0_0008,
            0x0000_0000,
        ];
        let segments = [segment(0x1000, &words)];
        let roots = [0x1000];
        let graph = analysis::discover_cfg(&segments, vec![entry(0x1000)], &roots, AnalysisLimits::default())
            .expect("synthetic CFG");
        let program = ControlProgram::lift(&graph).expect("typed CFG IR");

        match &program.blocks[&0x1000].terminator {
            ControlTerminator::ConditionalBranch {
                guest_pc, target_pc, fallthrough_pc, delay_slot, ..
            } => {
                assert_eq!(*guest_pc, 0x1000);
                assert_eq!(*target_pc, 0x100C);
                assert_eq!(*fallthrough_pc, 0x1008);
                assert_eq!(delay_slot.guest_pc, 0x1004);
                assert_eq!(delay_slot.operation, ir::Operation::AddImmediateUnsignedWord {
                    dst: ir::Gpr::new(9).unwrap(),
                    src: ir::Gpr::new(0).unwrap(),
                    immediate: 7,
                });
            }
            other => panic!("expected conditional branch terminator, got {other:?}"),
        }
    }

    #[test]
    fn unsupported_scalar_operation_becomes_an_explicit_stop() {
        let words = [
            0x0000_001A, // div: decoded but not yet lifted into executable scalar IR
            0x03E0_0008,
            0x0000_0000,
        ];
        let segments = [segment(0x2000, &words)];
        let roots = [0x2000];
        let graph = analysis::discover_cfg(&segments, vec![entry(0x2000)], &roots, AnalysisLimits::default())
            .expect("synthetic CFG");
        let program = ControlProgram::lift(&graph).expect("unsupported remains explicit");
        assert!(matches!(
            &program.blocks[&0x2000].terminator,
            ControlTerminator::Unsupported { guest_pc: 0x2000, mnemonic } if mnemonic == "div"
        ));
        assert_eq!(program.unsupported_count(), 1);
    }

    #[test]
    fn hilo_and_multiply_instructions_lift_inside_cfg_blocks() {
        let words = [
            0x0020_0011, // mthi r1
            0x0000_1010, // mfhi r2
            0x0022_1818, // mult r3, r1, r2
            0x03E0_0008, // jr ra
            0x0000_0000, // delay slot
        ];
        let segments = [segment(0x3000, &words)];
        let roots = [0x3000];
        let graph = analysis::discover_cfg(&segments, vec![entry(0x3000)], &roots, AnalysisLimits::default())
            .expect("synthetic HI/LO CFG");
        let program = ControlProgram::lift(&graph).expect("HI/LO scalar operations lift in CFG");

        let block = &program.blocks[&0x3000];
        assert_eq!(block.instructions.len(), 3);
        assert!(matches!(block.instructions[0].operation, ir::Operation::MoveToHi { src } if src.index() == 1));
        assert!(matches!(block.instructions[1].operation, ir::Operation::MoveFromHi { dst } if dst.index() == 2));
        assert!(matches!(block.instructions[2].operation,
            ir::Operation::MultiplyWordSigned { dst, lhs, rhs }
                if dst.index() == 3 && lhs.index() == 1 && rhs.index() == 2));
        assert!(matches!(block.terminator, ControlTerminator::Return { .. }));
        assert_eq!(program.unsupported_count(), 0);
    }
}
