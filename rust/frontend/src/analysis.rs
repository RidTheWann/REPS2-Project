//! Reachability-guided executable analysis and delay-slot-aware control-flow graphs.

use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::fmt;

use crate::elf::{self, Image, LoadedSegment};
use crate::mips::{self, Instruction as DecodedInstruction, Opcode};

const PF_X: u32 = 1;
const STT_FUNC: u8 = 2;

/// Bounded resources for analysis of untrusted or unusually large executables.
#[derive(Clone, Copy, Debug)]
pub struct AnalysisLimits {
    pub max_loaded_memory: usize,
    pub max_instructions: usize,
    pub max_blocks: usize,
}

impl Default for AnalysisLimits {
    fn default() -> Self {
        Self {
            max_loaded_memory: 256 * 1024 * 1024,
            max_instructions: 2_000_000,
            max_blocks: 250_000,
        }
    }
}

/// A discovered executable root: the ELF entry point or a function symbol.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EntryPoint {
    pub address: u32,
    pub labels: Vec<String>,
    pub is_elf_entry: bool,
}

/// Conditions used by supported direct conditional branches.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BranchCondition {
    Equal { lhs: u8, rhs: u8 },
    NotEqual { lhs: u8, rhs: u8 },
    LessThanOrEqualZero { register: u8 },
    GreaterThanZero { register: u8 },
    LessThanZero { register: u8 },
    GreaterThanOrEqualZero { register: u8 },
}

/// The semantic role of an outgoing static edge.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EdgeKind {
    Fallthrough,
    Taken,
    Jump,
    Call,
    ReturnContinuation,
}

/// One statically known CFG edge.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Edge {
    pub from_block: u32,
    pub to_pc: u32,
    pub kind: EdgeKind,
}

/// The block's control operation. The control instruction and delay slot stay
/// separate from the ordinary instruction sequence.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Terminator {
    Fallthrough {
        next_pc: u32,
    },
    ConditionalBranch {
        instruction: DecodedInstruction,
        condition: BranchCondition,
        target_pc: u32,
        fallthrough_pc: u32,
        delay_slot: DecodedInstruction,
        link_register: Option<u8>,
    },
    DirectJump {
        instruction: DecodedInstruction,
        target_pc: u32,
        delay_slot: DecodedInstruction,
        link_register: Option<u8>,
        return_pc: Option<u32>,
    },
    IndirectJump {
        instruction: DecodedInstruction,
        source_register: u8,
        delay_slot: DecodedInstruction,
        link_register: Option<u8>,
        return_pc: Option<u32>,
    },
    Return {
        instruction: DecodedInstruction,
        source_register: u8,
        delay_slot: DecodedInstruction,
    },
    Unsupported {
        instruction: DecodedInstruction,
    },
    SegmentEnd {
        next_pc: u32,
    },
}

impl Terminator {
    fn control_instruction(&self) -> Option<DecodedInstruction> {
        match self {
            Self::ConditionalBranch { instruction, .. }
            | Self::DirectJump { instruction, .. }
            | Self::IndirectJump { instruction, .. }
            | Self::Return { instruction, .. }
            | Self::Unsupported { instruction } => Some(*instruction),
            Self::Fallthrough { .. } | Self::SegmentEnd { .. } => None,
        }
    }

    fn delay_slot(&self) -> Option<DecodedInstruction> {
        match self {
            Self::ConditionalBranch { delay_slot, .. }
            | Self::DirectJump { delay_slot, .. }
            | Self::IndirectJump { delay_slot, .. }
            | Self::Return { delay_slot, .. } => Some(*delay_slot),
            Self::Fallthrough { .. } | Self::Unsupported { .. } | Self::SegmentEnd { .. } => None,
        }
    }

    fn outgoing_edges(&self, from_block: u32) -> Vec<Edge> {
        match self {
            Self::Fallthrough { next_pc } => vec![Edge {
                from_block,
                to_pc: *next_pc,
                kind: EdgeKind::Fallthrough,
            }],
            Self::ConditionalBranch {
                target_pc,
                fallthrough_pc,
                ..
            } => vec![
                Edge {
                    from_block,
                    to_pc: *target_pc,
                    kind: EdgeKind::Taken,
                },
                Edge {
                    from_block,
                    to_pc: *fallthrough_pc,
                    kind: EdgeKind::Fallthrough,
                },
            ],
            Self::DirectJump {
                target_pc,
                link_register,
                return_pc,
                ..
            } => {
                let mut edges = vec![Edge {
                    from_block,
                    to_pc: *target_pc,
                    kind: if link_register.is_some() {
                        EdgeKind::Call
                    } else {
                        EdgeKind::Jump
                    },
                }];
                if let Some(return_pc) = return_pc {
                    edges.push(Edge {
                        from_block,
                        to_pc: *return_pc,
                        kind: EdgeKind::ReturnContinuation,
                    });
                }
                edges
            }
            Self::IndirectJump {
                link_register,
                return_pc,
                ..
            } => match (link_register, return_pc) {
                (Some(_), Some(return_pc)) => vec![Edge {
                    from_block,
                    to_pc: *return_pc,
                    kind: EdgeKind::ReturnContinuation,
                }],
                _ => Vec::new(),
            },
            Self::Return { .. } | Self::Unsupported { .. } | Self::SegmentEnd { .. } => Vec::new(),
        }
    }

    /// Returns a short diagnostic label for CLI output.
    #[must_use]
    pub fn label(&self) -> &'static str {
        match self {
            Self::Fallthrough { .. } => "fallthrough",
            Self::ConditionalBranch { .. } => "conditional branch + delay slot",
            Self::DirectJump { link_register: Some(_), .. } => "direct call + delay slot",
            Self::DirectJump { link_register: None, .. } => "direct jump + delay slot",
            Self::IndirectJump { link_register: Some(_), .. } => "indirect call + delay slot",
            Self::IndirectJump { link_register: None, .. } => "indirect jump + delay slot",
            Self::Return { .. } => "return + delay slot",
            Self::Unsupported { .. } => "unsupported instruction",
            Self::SegmentEnd { .. } => "executable segment end",
        }
    }
}

/// A basic block. Its ordinary instruction list excludes the control
/// instruction and delay slot, which remain explicit in the terminator.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BasicBlock {
    pub start_pc: u32,
    pub instructions: Vec<DecodedInstruction>,
    pub terminator: Terminator,
}

impl BasicBlock {
    /// Number of unique instruction words represented by this block,
    /// including its control instruction and delay slot where present.
    #[must_use]
    pub fn instruction_count(&self) -> usize {
        self.instructions.len()
            + if self.terminator.control_instruction().is_some() { 1 } else { 0 }
            + if self.terminator.delay_slot().is_some() { 1 } else { 0 }
    }
}

/// Reachability-guided static CFG. Indirect jump targets and non-executable
/// direct targets are retained as unresolved evidence rather than guessed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ControlFlowGraph {
    pub entry_points: Vec<EntryPoint>,
    pub blocks: BTreeMap<u32, BasicBlock>,
    pub unresolved_indirect_jumps: Vec<u32>,
    pub external_targets: Vec<u32>,
    pub decoded_instruction_count: usize,
}

impl ControlFlowGraph {
    /// Returns all statically known block edges in deterministic order.
    #[must_use]
    pub fn edges(&self) -> Vec<Edge> {
        let mut edges = Vec::new();
        for block in self.blocks.values() {
            edges.extend(block.terminator.outgoing_edges(block.start_pc));
        }
        edges.sort_by_key(|edge| (edge.from_block, edge.to_pc, edge.kind as u8));
        edges
    }

    /// Counts blocks stopped by unsupported instructions.
    #[must_use]
    pub fn unsupported_block_count(&self) -> usize {
        self.blocks
            .values()
            .filter(|block| matches!(&block.terminator, Terminator::Unsupported { .. }))
            .count()
    }
}

/// Errors which stop analysis rather than allowing guessed control flow.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum AnalysisError {
    Elf(String),
    NoExecutableEntryPoints,
    EntryPointNotExecutable(u32),
    InvalidLoadedSegment { virtual_address: u32, file_size: usize, memory_size: usize },
    MissingDelaySlot { control_pc: u32, delay_pc: u32 },
    UnsupportedDelaySlot { control_pc: u32, delay_pc: u32 },
    ControlTransferInDelaySlot { control_pc: u32, delay_pc: u32 },
    TargetEntersDelaySlot { target_pc: u32, owning_control_pc: u32 },
    GuestPcOverflow(u32),
    InstructionLimitExceeded { limit: usize },
    BlockLimitExceeded { limit: usize },
    InternalBlockOwnershipError(u32),
}

impl fmt::Display for AnalysisError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Elf(message) => write!(formatter, "ELF loading failed: {message}"),
            Self::NoExecutableEntryPoints => formatter.write_str("no executable entry point or function symbol was found"),
            Self::EntryPointNotExecutable(pc) => write!(formatter, "entry point 0x{pc:08X} is not in an executable segment"),
            Self::InvalidLoadedSegment { virtual_address, file_size, memory_size } => write!(
                formatter,
                "loaded segment at 0x{virtual_address:08X} has file size {file_size} larger than its backing memory buffer {memory_size}"
            ),
            Self::MissingDelaySlot { control_pc, delay_pc } => write!(
                formatter,
                "control instruction at 0x{control_pc:08X} has no executable delay slot at 0x{delay_pc:08X}"
            ),
            Self::UnsupportedDelaySlot { control_pc, delay_pc } => write!(
                formatter,
                "control instruction at 0x{control_pc:08X} has an unsupported delay-slot instruction at 0x{delay_pc:08X}"
            ),
            Self::ControlTransferInDelaySlot { control_pc, delay_pc } => write!(
                formatter,
                "control transfer at 0x{delay_pc:08X} appears in the delay slot of 0x{control_pc:08X}"
            ),
            Self::TargetEntersDelaySlot { target_pc, owning_control_pc } => write!(
                formatter,
                "control-flow target 0x{target_pc:08X} enters the delay slot of 0x{owning_control_pc:08X}"
            ),
            Self::GuestPcOverflow(pc) => write!(formatter, "guest PC overflows after 0x{pc:08X}"),
            Self::InstructionLimitExceeded { limit } => write!(formatter, "analysis exceeded the {limit} instruction limit"),
            Self::BlockLimitExceeded { limit } => write!(formatter, "analysis exceeded the {limit} basic-block limit"),
            Self::InternalBlockOwnershipError(pc) => write!(formatter, "cannot split or resolve instruction ownership at 0x{pc:08X}"),
        }
    }
}

impl std::error::Error for AnalysisError {}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum OwnerLocation {
    Body(usize),
    Control,
    DelaySlot,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct InstructionOwner {
    block_start: u32,
    location: OwnerLocation,
}

/// Returns whether a guest PC lies inside an executable PT_LOAD image.
#[must_use]
pub fn contains_executable_address(segments: &[LoadedSegment], address: u32) -> bool {
    let address = u64::from(address);
    segments.iter().any(|segment| {
        if segment.flags & PF_X == 0 {
            return false;
        }
        let start = u64::from(segment.virtual_address);
        let file_size = segment.file_size.min(segment.bytes.len()) as u64;
        let Some(end) = start.checked_add(file_size) else {
            return false;
        };
        address >= start && address < end
    })
}

/// Collects the ELF entry point and named STT_FUNC symbols that lie inside
/// executable PT_LOAD segments. Duplicate addresses are merged.
#[must_use]
pub fn collect_entry_points(image: &Image, segments: &[LoadedSegment]) -> Vec<EntryPoint> {
    let mut entries: BTreeMap<u32, (bool, BTreeSet<String>)> = BTreeMap::new();

    if image.entry_point != 0 && contains_executable_address(segments, image.entry_point) {
        entries
            .entry(image.entry_point)
            .or_insert_with(|| (false, BTreeSet::new()))
            .0 = true;
    }

    for symbol in &image.symbols {
        if symbol.kind != STT_FUNC
            || symbol.value == 0
            || symbol.section_index == 0
            || !contains_executable_address(segments, symbol.value)
        {
            continue;
        }
        let label = if symbol.name.is_empty() {
            format!("function@0x{:08X}", symbol.value)
        } else {
            symbol.name.clone()
        };
        entries
            .entry(symbol.value)
            .or_insert_with(|| (false, BTreeSet::new()))
            .1
            .insert(label);
    }

    entries
        .into_iter()
        .map(|(address, (is_elf_entry, labels))| EntryPoint {
            address,
            labels: labels.into_iter().collect(),
            is_elf_entry,
        })
        .collect()
}

/// Loads PT_LOAD segments, discovers executable entry points, and builds a
/// delay-slot-aware CFG from the ELF entry and function symbols.
pub fn discover_elf(
    image: &Image,
    file_bytes: &[u8],
    limits: AnalysisLimits,
) -> Result<ControlFlowGraph, AnalysisError> {
    let segments = elf::load_segments(file_bytes, image, limits.max_loaded_memory)
        .map_err(|error| AnalysisError::Elf(error.to_string()))?;
    let entry_points = collect_entry_points(image, &segments);
    let roots = entry_points.iter().map(|entry| entry.address).collect::<Vec<_>>();
    discover_cfg(&segments, entry_points, &roots, limits)
}

/// Builds a CFG from one or more executable roots in already materialized
/// PT_LOAD segments. Only reachable words are decoded.
pub fn discover_cfg(
    segments: &[LoadedSegment],
    entry_points: Vec<EntryPoint>,
    roots: &[u32],
    limits: AnalysisLimits,
) -> Result<ControlFlowGraph, AnalysisError> {
    if roots.is_empty() {
        return Err(AnalysisError::NoExecutableEntryPoints);
    }
    for segment in segments {
        if segment.file_size > segment.bytes.len() {
            return Err(AnalysisError::InvalidLoadedSegment {
                virtual_address: segment.virtual_address,
                file_size: segment.file_size,
                memory_size: segment.bytes.len(),
            });
        }
        let Some(memory_end) = u64::from(segment.virtual_address)
            .checked_add(segment.bytes.len() as u64) else {
            return Err(AnalysisError::InvalidLoadedSegment {
                virtual_address: segment.virtual_address,
                file_size: segment.file_size,
                memory_size: segment.bytes.len(),
            });
        };
        if memory_end > u64::from(u32::MAX) + 1 {
            return Err(AnalysisError::InvalidLoadedSegment {
                virtual_address: segment.virtual_address,
                file_size: segment.file_size,
                memory_size: segment.bytes.len(),
            });
        }
    }

    let mut unique_roots = BTreeSet::new();
    for root in roots {
        if root & 3 != 0 || !contains_executable_address(segments, *root) {
            return Err(AnalysisError::EntryPointNotExecutable(*root));
        }
        unique_roots.insert(*root);
    }

    let mut queue = VecDeque::new();
    let mut queued = BTreeSet::new();
    let mut leaders = unique_roots.clone();
    for root in unique_roots {
        queue.push_back(root);
        queued.insert(root);
    }

    let mut blocks = BTreeMap::new();
    let mut owners: BTreeMap<u32, InstructionOwner> = BTreeMap::new();
    let mut external_targets = BTreeSet::new();
    let mut unresolved_indirect_jumps = BTreeSet::new();
    let mut decoded_instruction_count = 0usize;

    while let Some(start_pc) = queue.pop_front() {
        if blocks.contains_key(&start_pc) {
            continue;
        }

        if let Some(owner) = owners.get(&start_pc).copied() {
            if owner.location == OwnerLocation::DelaySlot {
                let owning_control_pc = blocks
                    .get(&owner.block_start)
                    .and_then(|block: &BasicBlock| block.terminator.control_instruction())
                    .map(|instruction| instruction.pc)
                    .unwrap_or(owner.block_start);
                return Err(AnalysisError::TargetEntersDelaySlot {
                    target_pc: start_pc,
                    owning_control_pc,
                });
            }
            split_block_at(start_pc, owner, &mut blocks, &mut owners)?;
            if blocks.len() > limits.max_blocks {
                return Err(AnalysisError::BlockLimitExceeded {
                    limit: limits.max_blocks,
                });
            }
            continue;
        }

        if !contains_executable_address(segments, start_pc) {
            external_targets.insert(start_pc);
            continue;
        }
        if blocks.len() >= limits.max_blocks {
            return Err(AnalysisError::BlockLimitExceeded {
                limit: limits.max_blocks,
            });
        }

        let mut instructions = Vec::new();
        let mut pc = start_pc;
        let terminator = loop {
            if pc != start_pc
                && (leaders.contains(&pc) || blocks.contains_key(&pc) || owners.contains_key(&pc))
            {
                break Terminator::Fallthrough { next_pc: pc };
            }

            let Some(word) = read_instruction_word(segments, pc) else {
                break Terminator::SegmentEnd { next_pc: pc };
            };

            increment_instruction_count(&mut decoded_instruction_count, limits.max_instructions)?;

            let instruction = mips::decode(word, pc);
            if !instruction.is_supported()
                || matches!(instruction.opcode, Opcode::Syscall | Opcode::Break)
            {
                break Terminator::Unsupported { instruction };
            }

            if instruction.is_conditional_branch() {
                let delay_slot = fetch_delay_slot(segments, instruction, &leaders, &owners)?;
                increment_instruction_count(&mut decoded_instruction_count, limits.max_instructions)?;
                let condition = branch_condition(instruction)
                    .ok_or(AnalysisError::InternalBlockOwnershipError(instruction.pc))?;
                let fallthrough_pc = instruction.pc.checked_add(8)
                    .ok_or(AnalysisError::GuestPcOverflow(instruction.pc))?;
                break Terminator::ConditionalBranch {
                    instruction,
                    condition,
                    target_pc: instruction.target,
                    fallthrough_pc,
                    delay_slot,
                    link_register: matches!(instruction.opcode, Opcode::Bltzal | Opcode::Bgezal)
                        .then_some(31),
                };
            }

            match instruction.opcode {
                Opcode::J | Opcode::Jal => {
                    let delay_slot = fetch_delay_slot(segments, instruction, &leaders, &owners)?;
                    increment_instruction_count(&mut decoded_instruction_count, limits.max_instructions)?;
                    let return_pc = if instruction.opcode == Opcode::Jal {
                        Some(instruction.pc.checked_add(8)
                            .ok_or(AnalysisError::GuestPcOverflow(instruction.pc))?)
                    } else {
                        None
                    };
                    break Terminator::DirectJump {
                        instruction,
                        target_pc: instruction.target,
                        delay_slot,
                        link_register: (instruction.opcode == Opcode::Jal).then_some(31),
                        return_pc,
                    };
                }
                Opcode::Jr if instruction.rs == 31 => {
                    let delay_slot = fetch_delay_slot(segments, instruction, &leaders, &owners)?;
                    increment_instruction_count(&mut decoded_instruction_count, limits.max_instructions)?;
                    break Terminator::Return {
                        instruction,
                        source_register: instruction.rs,
                        delay_slot,
                    };
                }
                Opcode::Jr | Opcode::Jalr => {
                    let delay_slot = fetch_delay_slot(segments, instruction, &leaders, &owners)?;
                    increment_instruction_count(&mut decoded_instruction_count, limits.max_instructions)?;
                    let link_register = if instruction.opcode == Opcode::Jalr && instruction.rd != 0 {
                        Some(instruction.rd)
                    } else {
                        None
                    };
                    let return_pc = if link_register.is_some() {
                        Some(instruction.pc.checked_add(8)
                            .ok_or(AnalysisError::GuestPcOverflow(instruction.pc))?)
                    } else {
                        None
                    };
                    break Terminator::IndirectJump {
                        instruction,
                        source_register: instruction.rs,
                        delay_slot,
                        link_register,
                        return_pc,
                    };
                }
                _ => {}
            }

            instructions.push(instruction);
            pc = pc.checked_add(4).ok_or(AnalysisError::GuestPcOverflow(pc))?;
        };

        let block = BasicBlock {
            start_pc,
            instructions,
            terminator,
        };
        register_block_owners(&block, &mut owners)?;
        let next_targets = block_targets(&block);
        if let Terminator::IndirectJump { instruction, .. } = &block.terminator {
            unresolved_indirect_jumps.insert(instruction.pc);
        }
        blocks.insert(start_pc, block);

        for target in next_targets {
            if contains_executable_address(segments, target) {
                leaders.insert(target);
                if queued.insert(target) {
                    queue.push_back(target);
                }
            } else {
                external_targets.insert(target);
            }
        }
    }

    Ok(ControlFlowGraph {
        entry_points,
        blocks,
        unresolved_indirect_jumps: unresolved_indirect_jumps.into_iter().collect(),
        external_targets: external_targets.into_iter().collect(),
        decoded_instruction_count,
    })
}

fn read_instruction_word(segments: &[LoadedSegment], pc: u32) -> Option<u32> {
    if pc & 3 != 0 {
        return None;
    }
    let address = u64::from(pc);
    for segment in segments {
        if segment.flags & PF_X == 0 {
            continue;
        }
        let start = u64::from(segment.virtual_address);
        let file_size = segment.file_size.min(segment.bytes.len()) as u64;
        let end = start.checked_add(file_size)?;
        if address < start || address >= end {
            continue;
        }
        let offset = usize::try_from(address - start).ok()?;
        let end_offset = offset.checked_add(4)?;
        if end_offset <= file_size {
            let bytes = &segment.bytes[offset..end_offset];
            return Some(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]));
        }
        return None;
    }
    None
}

fn increment_instruction_count(count: &mut usize, limit: usize) -> Result<(), AnalysisError> {
    *count = count.checked_add(1)
        .ok_or(AnalysisError::InstructionLimitExceeded { limit })?;
    if *count > limit {
        return Err(AnalysisError::InstructionLimitExceeded { limit });
    }
    Ok(())
}

fn fetch_delay_slot(
    segments: &[LoadedSegment],
    control: DecodedInstruction,
    leaders: &BTreeSet<u32>,
    owners: &BTreeMap<u32, InstructionOwner>,
) -> Result<DecodedInstruction, AnalysisError> {
    let delay_pc = control.pc.checked_add(4)
        .ok_or(AnalysisError::GuestPcOverflow(control.pc))?;
    if leaders.contains(&delay_pc) || owners.contains_key(&delay_pc) {
        return Err(AnalysisError::TargetEntersDelaySlot {
            target_pc: delay_pc,
            owning_control_pc: control.pc,
        });
    }
    let Some(word) = read_instruction_word(segments, delay_pc) else {
        return Err(AnalysisError::MissingDelaySlot {
            control_pc: control.pc,
            delay_pc,
        });
    };
    let delay_slot = mips::decode(word, delay_pc);
    if !delay_slot.is_supported() {
        return Err(AnalysisError::UnsupportedDelaySlot {
            control_pc: control.pc,
            delay_pc,
        });
    }
    if delay_slot.is_control_transfer() {
        return Err(AnalysisError::ControlTransferInDelaySlot {
            control_pc: control.pc,
            delay_pc,
        });
    }
    Ok(delay_slot)
}

fn branch_condition(instruction: DecodedInstruction) -> Option<BranchCondition> {
    match instruction.opcode {
        Opcode::Beq => Some(BranchCondition::Equal {
            lhs: instruction.rs,
            rhs: instruction.rt,
        }),
        Opcode::Bne => Some(BranchCondition::NotEqual {
            lhs: instruction.rs,
            rhs: instruction.rt,
        }),
        Opcode::Blez => Some(BranchCondition::LessThanOrEqualZero {
            register: instruction.rs,
        }),
        Opcode::Bgtz => Some(BranchCondition::GreaterThanZero {
            register: instruction.rs,
        }),
        Opcode::Bltz | Opcode::Bltzal => Some(BranchCondition::LessThanZero {
            register: instruction.rs,
        }),
        Opcode::Bgez | Opcode::Bgezal => Some(BranchCondition::GreaterThanOrEqualZero {
            register: instruction.rs,
        }),
        _ => None,
    }
}

fn block_targets(block: &BasicBlock) -> Vec<u32> {
    block.terminator.outgoing_edges(block.start_pc)
        .into_iter()
        .map(|edge| edge.to_pc)
        .collect()
}

fn register_block_owners(
    block: &BasicBlock,
    owners: &mut BTreeMap<u32, InstructionOwner>,
) -> Result<(), AnalysisError> {
    for (index, instruction) in block.instructions.iter().enumerate() {
        if owners.insert(instruction.pc, InstructionOwner {
            block_start: block.start_pc,
            location: OwnerLocation::Body(index),
        }).is_some() {
            return Err(AnalysisError::InternalBlockOwnershipError(instruction.pc));
        }
    }

    if let Some(instruction) = block.terminator.control_instruction()
        && owners.insert(instruction.pc, InstructionOwner {
            block_start: block.start_pc,
            location: OwnerLocation::Control,
        }).is_some()
    {
        return Err(AnalysisError::InternalBlockOwnershipError(instruction.pc));
    }

    if let Some(delay_slot) = block.terminator.delay_slot()
        && owners.insert(delay_slot.pc, InstructionOwner {
            block_start: block.start_pc,
            location: OwnerLocation::DelaySlot,
        }).is_some()
    {
        return Err(AnalysisError::InternalBlockOwnershipError(delay_slot.pc));
    }
    Ok(())
}

fn split_block_at(
    target_pc: u32,
    owner: InstructionOwner,
    blocks: &mut BTreeMap<u32, BasicBlock>,
    owners: &mut BTreeMap<u32, InstructionOwner>,
) -> Result<(), AnalysisError> {
    if owner.location == OwnerLocation::DelaySlot {
        let owning_control_pc = blocks
            .get(&owner.block_start)
            .and_then(|block| block.terminator.control_instruction())
            .map(|instruction| instruction.pc)
            .unwrap_or(owner.block_start);
        return Err(AnalysisError::TargetEntersDelaySlot {
            target_pc,
            owning_control_pc,
        });
    }
    if target_pc == owner.block_start {
        return Ok(());
    }

    let mut prefix = blocks.remove(&owner.block_start)
        .ok_or(AnalysisError::InternalBlockOwnershipError(target_pc))?;
    let old_start = prefix.start_pc;
    let old_terminator = std::mem::replace(
        &mut prefix.terminator,
        Terminator::Fallthrough { next_pc: target_pc },
    );

    let (tail_instructions, body_split_index) = match owner.location {
        OwnerLocation::Body(index) => {
            if index >= prefix.instructions.len() {
                return Err(AnalysisError::InternalBlockOwnershipError(target_pc));
            }
            (prefix.instructions.split_off(index), Some(index))
        }
        OwnerLocation::Control => (Vec::new(), None),
        OwnerLocation::DelaySlot => {
            return Err(AnalysisError::InternalBlockOwnershipError(target_pc));
        }
    };

    let tail = BasicBlock {
        start_pc: target_pc,
        instructions: tail_instructions,
        terminator: old_terminator,
    };

    let moved_owners = owners
        .range(target_pc..)
        .filter(|(_, current)| current.block_start == old_start)
        .map(|(pc, current)| (*pc, current.location))
        .collect::<Vec<_>>();
    for (pc, location) in moved_owners {
        let location = match (location, body_split_index) {
            (OwnerLocation::Body(index), Some(split_index)) => OwnerLocation::Body(index - split_index),
            (other, _) => other,
        };
        owners.insert(pc, InstructionOwner {
            block_start: target_pc,
            location,
        });
    }

    blocks.insert(old_start, prefix);
    if blocks.insert(target_pc, tail).is_some() {
        return Err(AnalysisError::InternalBlockOwnershipError(target_pc));
    }
    Ok(())
}

/// Returns function symbols without deciding whether their values map to code.
#[must_use]
pub fn executable_function_symbols(image: &Image) -> Vec<&elf::Symbol> {
    image.symbols.iter()
        .filter(|symbol| symbol.kind == STT_FUNC && symbol.section_index != 0 && symbol.value != 0)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn segment(base: u32, words: &[u32]) -> LoadedSegment {
        let mut bytes = Vec::with_capacity(words.len() * 4);
        for word in words {
            bytes.extend_from_slice(&word.to_le_bytes());
        }
        let file_size = bytes.len();
        LoadedSegment {
            virtual_address: base,
            flags: PF_X,
            file_size,
            bytes,
        }
    }

    fn entry(address: u32) -> EntryPoint {
        EntryPoint {
            address,
            labels: vec!["synthetic-entry".to_owned()],
            is_elf_entry: true,
        }
    }

    #[test]
    fn discovers_conditional_branch_and_keeps_delay_slot_explicit() {
        let words = [
            0x2408_0001,
            0x1100_0002,
            0x2409_0007,
            0x0000_0000,
            0x03E0_0008,
            0x0000_0000,
        ];
        let segments = [segment(0x1000, &words)];
        let roots = [0x1000];
        let graph = discover_cfg(&segments, vec![entry(0x1000)], &roots, AnalysisLimits::default())
            .expect("valid synthetic control flow");

        assert_eq!(graph.blocks.len(), 3);
        let entry_block = &graph.blocks[&0x1000];
        assert_eq!(entry_block.instructions.len(), 1);
        match &entry_block.terminator {
            Terminator::ConditionalBranch {
                target_pc,
                fallthrough_pc,
                delay_slot,
                ..
            } => {
                assert_eq!(*target_pc, 0x1010);
                assert_eq!(*fallthrough_pc, 0x100C);
                assert_eq!(delay_slot.pc, 0x1008);
                assert_eq!(delay_slot.opcode, Opcode::Addiu);
            }
            other => panic!("expected conditional branch, got {other:?}"),
        }
        let edges = graph.edges();
        assert!(edges.iter().any(|edge| edge.from_block == 0x1000 && edge.to_pc == 0x1010 && edge.kind == EdgeKind::Taken));
        assert!(edges.iter().any(|edge| edge.from_block == 0x1000 && edge.to_pc == 0x100C && edge.kind == EdgeKind::Fallthrough));
        assert!(matches!(&graph.blocks[&0x1010].terminator, Terminator::Return { .. }));
    }

    #[test]
    fn splits_a_block_when_a_backward_branch_targets_its_interior() {
        let words = [
            0x2401_0001,
            0x2402_0002,
            0x1422_FFFE,
            0x0000_0000,
            0x03E0_0008,
            0x0000_0000,
        ];
        let segments = [segment(0x2000, &words)];
        let roots = [0x2000];
        let graph = discover_cfg(&segments, vec![entry(0x2000)], &roots, AnalysisLimits::default())
            .expect("backward branch graph");
        assert_eq!(graph.blocks.len(), 3);
        assert_eq!(graph.blocks[&0x2000].instructions.len(), 1);
        assert!(matches!(&graph.blocks[&0x2000].terminator, Terminator::Fallthrough { next_pc: 0x2004 }));
        assert_eq!(graph.blocks[&0x2004].instructions.len(), 1);
        assert!(matches!(&graph.blocks[&0x2004].terminator, Terminator::ConditionalBranch { target_pc: 0x2004, .. }));
    }

    #[test]
    fn rejects_a_branch_target_that_enters_a_delay_slot() {
        let words = [
            0x1100_0000,
            0x0000_0000,
            0x03E0_0008,
            0x0000_0000,
        ];
        let segments = [segment(0x3000, &words)];
        let roots = [0x3000];
        let error = discover_cfg(&segments, vec![entry(0x3000)], &roots, AnalysisLimits::default())
            .expect_err("branch into delay slot is not a valid block boundary");
        assert!(matches!(error, AnalysisError::TargetEntersDelaySlot { target_pc: 0x3004, .. }));
    }

    #[test]
    fn rejects_inconsistent_external_segment_metadata() {
        let mut segment = segment(0x3900, &[0x2408_0005]);
        segment.file_size = segment.bytes.len() + 1;
        let segments = [segment];
        let roots = [0x3900];
        let error = discover_cfg(&segments, vec![entry(0x3900)], &roots, AnalysisLimits::default())
            .expect_err("file-backed size cannot exceed the owned buffer");
        assert!(matches!(error, AnalysisError::InvalidLoadedSegment { .. }));
    }

    #[test]
    fn does_not_decode_zero_filled_bss_as_executable_instructions() {
        let mut segment = segment(0x3800, &[0x2408_0005, 0]);
        segment.file_size = 4;
        let segments = [segment];
        let roots = [0x3800];
        let graph = discover_cfg(&segments, vec![entry(0x3800)], &roots, AnalysisLimits::default())
            .expect("file-backed executable prefix");
        assert_eq!(graph.decoded_instruction_count, 1);
        assert_eq!(graph.blocks.len(), 1);
        assert_eq!(graph.blocks[&0x3800].instructions.len(), 1);
        assert!(matches!(
            &graph.blocks[&0x3800].terminator,
            Terminator::SegmentEnd { next_pc: 0x3804 }
        ));
    }

    #[test]
    fn rejects_control_transfer_inside_another_delay_slot() {
        let words = [
            0x0800_0802,
            0x0800_0803,
            0x03E0_0008,
            0x0000_0000,
        ];
        let segments = [segment(0x2000, &words)];
        let roots = [0x2000];
        let error = discover_cfg(&segments, vec![entry(0x2000)], &roots, AnalysisLimits::default())
            .expect_err("control transfer in a delay slot is unsupported");
        assert!(matches!(error, AnalysisError::ControlTransferInDelaySlot { .. }));
    }

    #[test]
    fn records_indirect_jump_and_call_return_continuation() {
        let words = [
            0x03E0_F809,
            0x2401_0001,
            0x2402_0002,
            0x03E0_0008,
            0x0000_0000,
        ];
        let segments = [segment(0x4000, &words)];
        let roots = [0x4000];
        let graph = discover_cfg(&segments, vec![entry(0x4000)], &roots, AnalysisLimits::default())
            .expect("indirect call graph");
        assert_eq!(graph.unresolved_indirect_jumps, vec![0x4000]);
        assert!(graph.edges().iter().any(|edge| {
            edge.from_block == 0x4000
                && edge.to_pc == 0x4008
                && edge.kind == EdgeKind::ReturnContinuation
        }));
    }
}
