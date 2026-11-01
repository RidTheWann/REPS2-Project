//! Reachability-guided executable analysis and delay-slot-aware control-flow graphs.

use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::fmt;

use crate::elf::{self, Image, LoadedSegment};
use crate::mips::{self, Instruction as DecodedInstruction, Opcode};

const STT_FUNC: u8 = 2;

/// Bounded resources for analysis of untrusted or unusually large executables.
#[derive(Clone, Copy, Debug)]
pub struct AnalysisLimits {
    pub max_loaded_memory: usize,
    pub max_instructions: usize,
    pub max_blocks: usize,
    /// Maximum number of independent virtual views of main RAM that may be
    /// materialized at once. The EE exposes four aliases of the same RAM, so
    /// four is the architectural maximum.
    pub max_aliased_roots: u8,
    /// Maximum number of heuristic function-root candidates that static
    /// discovery may collect before it stops and reports truncation. Retail
    /// executables are stripped, so this bounds the work spent on a hostile or
    /// unusually large image.
    pub max_function_roots: usize,
}

impl Default for AnalysisLimits {
    fn default() -> Self {
        Self {
            max_loaded_memory: 256 * 1024 * 1024,
            max_instructions: 2_000_000,
            max_blocks: 250_000,
            max_aliased_roots: 4,
            max_function_roots: 500_000,
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

/// Delay-slot execution policy for a conditional branch.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DelaySlotPolicy {
    /// The delay-slot instruction executes on both the taken and fallthrough
    /// paths.
    ExecuteAlways,
    /// The delay-slot instruction executes only when the branch is taken.
    /// This is the R5900 branch-likely annulment rule.
    AnnulWhenNotTaken,
}

/// Conditions used by supported direct conditional branches.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BranchCondition {
    Equal {
        lhs: u8,
        rhs: u8,
    },
    NotEqual {
        lhs: u8,
        rhs: u8,
    },
    LessThanOrEqualZero {
        register: u8,
    },
    GreaterThanZero {
        register: u8,
    },
    LessThanZero {
        register: u8,
    },
    GreaterThanOrEqualZero {
        register: u8,
    },
    /// The FCR31 condition bit (bit 23) written by the preceding COP1 compare.
    /// BC1F branches when it is clear and BC1T when it is set.
    FpuCondition {
        when_set: bool,
    },
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
        delay_slot_policy: DelaySlotPolicy,
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
            Self::DirectJump {
                link_register: Some(_),
                ..
            } => "direct call + delay slot",
            Self::DirectJump {
                link_register: None,
                ..
            } => "direct jump + delay slot",
            Self::IndirectJump {
                link_register: Some(_),
                ..
            } => "indirect call + delay slot",
            Self::IndirectJump {
                link_register: None,
                ..
            } => "indirect jump + delay slot",
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
            + if self.terminator.control_instruction().is_some() {
                1
            } else {
                0
            }
            + if self.terminator.delay_slot().is_some() {
                1
            } else {
                0
            }
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
    InvalidLoadedSegment {
        virtual_address: u32,
        file_size: usize,
        memory_size: usize,
    },
    MissingDelaySlot {
        control_pc: u32,
        delay_pc: u32,
    },
    UnsupportedDelaySlot {
        control_pc: u32,
        delay_pc: u32,
    },
    ControlTransferInDelaySlot {
        control_pc: u32,
        delay_pc: u32,
    },
    TargetEntersDelaySlot {
        target_pc: u32,
        owning_control_pc: u32,
    },
    GuestPcOverflow(u32),
    InstructionLimitExceeded {
        limit: usize,
    },
    BlockLimitExceeded {
        limit: usize,
    },
    AliasedViewLimitExceeded {
        limit: u8,
    },
    InternalBlockOwnershipError(u32),
}

impl fmt::Display for AnalysisError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Elf(message) => write!(formatter, "ELF loading failed: {message}"),
            Self::NoExecutableEntryPoints => {
                formatter.write_str("no executable entry point or function symbol was found")
            }
            Self::EntryPointNotExecutable(pc) => write!(
                formatter,
                "entry point 0x{pc:08X} is not in an executable segment or any of its EE aliases"
            ),
            Self::AliasedViewLimitExceeded { limit } => write!(
                formatter,
                "analysis requires more than {limit} aliased views of main RAM"
            ),
            Self::InvalidLoadedSegment {
                virtual_address,
                file_size,
                memory_size,
            } => write!(
                formatter,
                "loaded segment at 0x{virtual_address:08X} has file size {file_size} larger than its backing memory buffer {memory_size}"
            ),
            Self::MissingDelaySlot {
                control_pc,
                delay_pc,
            } => write!(
                formatter,
                "control instruction at 0x{control_pc:08X} has no executable delay slot at 0x{delay_pc:08X}"
            ),
            Self::UnsupportedDelaySlot {
                control_pc,
                delay_pc,
            } => write!(
                formatter,
                "control instruction at 0x{control_pc:08X} has an unsupported delay-slot instruction at 0x{delay_pc:08X}"
            ),
            Self::ControlTransferInDelaySlot {
                control_pc,
                delay_pc,
            } => write!(
                formatter,
                "control transfer at 0x{delay_pc:08X} appears in the delay slot of 0x{control_pc:08X}"
            ),
            Self::TargetEntersDelaySlot {
                target_pc,
                owning_control_pc,
            } => write!(
                formatter,
                "control-flow target 0x{target_pc:08X} enters the delay slot of 0x{owning_control_pc:08X}"
            ),
            Self::GuestPcOverflow(pc) => write!(formatter, "guest PC overflows after 0x{pc:08X}"),
            Self::InstructionLimitExceeded { limit } => {
                write!(formatter, "analysis exceeded the {limit} instruction limit")
            }
            Self::BlockLimitExceeded { limit } => {
                write!(formatter, "analysis exceeded the {limit} basic-block limit")
            }
            Self::InternalBlockOwnershipError(pc) => write!(
                formatter,
                "cannot split or resolve instruction ownership at 0x{pc:08X}"
            ),
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

/// Returns whether a guest PC is backed by executable file bytes in the
/// segment's declared alias region.
#[must_use]
pub fn contains_executable_address(segments: &[LoadedSegment], address: u32) -> bool {
    crate::address::file_backed_executable_segment(segments, address).is_some()
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

/// Heuristic function roots recovered from executable bytes.
///
/// Retail PS2 boot executables are commonly stripped of every `STT_FUNC`
/// symbol, so the ELF entry alone bounds static analysis to the entry's first
/// few blocks. This pass recovers additional entry candidates from two
/// encoding-level signals. Both are candidates rather than reachability proofs:
/// embedded data can decode as a `JAL` or as a stack prologue, and every
/// candidate is validated by the CFG traversal before any block is emitted.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct FunctionDiscovery {
    /// Addresses targeted by a `JAL` whose word is file-backed executable code.
    pub call_targets: BTreeSet<u32>,
    /// Addresses whose first word is `addiu $sp, $sp, -N`, the PS2/EE ABI
    /// callee prologue.
    pub prologue_candidates: BTreeSet<u32>,
    /// Number of aligned words examined.
    pub scanned_words: u64,
    /// Whether the candidate limit stopped the scan before the end of `.text`.
    pub truncated: bool,
}

impl FunctionDiscovery {
    /// The union of both evidence sets in deterministic address order.
    #[must_use]
    pub fn candidate_roots(&self) -> BTreeSet<u32> {
        self.call_targets
            .union(&self.prologue_candidates)
            .copied()
            .collect()
    }

    /// Number of distinct candidate addresses.
    #[must_use]
    pub fn candidate_count(&self) -> usize {
        self.call_targets.union(&self.prologue_candidates).count()
    }
}

/// Scans file-backed executable bytes for function entry candidates.
///
/// The scan reads each executable `PT_LOAD` segment in its declared alias view,
/// so the four EE aliases of one segment are not revisited. A `JAL` target is
/// admitted only when the target is itself backed by file bytes in an
/// executable segment; a prologue is admitted only from an executable,
/// file-backed word.
#[must_use]
pub fn discover_function_roots(
    segments: &[LoadedSegment],
    limits: AnalysisLimits,
) -> FunctionDiscovery {
    let mut discovery = FunctionDiscovery::default();
    for segment in segments {
        if segment.flags & elf::PF_X == 0 {
            continue;
        }
        let file_size = segment.file_size.min(segment.bytes.len());
        if file_size < 4 {
            continue;
        }
        let base = segment.virtual_address;
        let mut offset = ((4 - (base % 4)) % 4) as usize;
        while offset + 4 <= file_size {
            if discovery.call_targets.len() + discovery.prologue_candidates.len()
                >= limits.max_function_roots
            {
                discovery.truncated = true;
                return discovery;
            }
            let word = u32::from_le_bytes(
                segment.bytes[offset..offset + 4]
                    .try_into()
                    .expect("four-byte discovery window"),
            );
            let Some(pc) = base.checked_add(offset as u32) else {
                break;
            };
            discovery.scanned_words += 1;
            let instruction = mips::decode(word, pc);
            if instruction.opcode == Opcode::Jal
                && crate::address::file_backed_executable_segment(segments, instruction.target)
                    .is_some()
            {
                discovery.call_targets.insert(instruction.target);
            }
            if instruction.opcode == Opcode::Addiu
                && instruction.rs == 29
                && instruction.rt == 29
                && instruction.signed_immediate() < 0
            {
                discovery.prologue_candidates.insert(pc);
            }
            offset += 4;
        }
    }
    discovery
}

/// Materialized image segments plus the authoritative analysis roots.
struct PreparedImage {
    segments: Vec<LoadedSegment>,
    entry_points: Vec<EntryPoint>,
    roots: Vec<u32>,
}

/// Materializes PT_LOAD segments and collects the authoritative entry points
/// (the ELF entry and any `STT_FUNC` symbols that map to executable bytes).
fn prepare(
    image: &Image,
    file_bytes: &[u8],
    limits: AnalysisLimits,
) -> Result<PreparedImage, AnalysisError> {
    let segments = elf::load_segments(file_bytes, image, limits.max_loaded_memory)
        .map_err(|error| AnalysisError::Elf(error.to_string()))?;
    let entry_points = collect_entry_points(image, &segments);
    let roots = entry_points
        .iter()
        .map(|entry| entry.address)
        .collect::<Vec<_>>();
    Ok(PreparedImage {
        segments,
        entry_points,
        roots,
    })
}

/// Loads PT_LOAD segments, discovers executable entry points, and builds a
/// delay-slot-aware CFG from the ELF entry and function symbols.
pub fn discover_elf(
    image: &Image,
    file_bytes: &[u8],
    limits: AnalysisLimits,
) -> Result<ControlFlowGraph, AnalysisError> {
    let PreparedImage {
        segments,
        entry_points,
        roots,
    } = prepare(image, file_bytes, limits)?;
    discover_cfg(&segments, entry_points, &roots, limits)
}

/// The result of an analysis that augments authoritative roots with
/// heuristically discovered function roots.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DiscoveryAnalysis {
    pub graph: ControlFlowGraph,
    pub discovery: FunctionDiscovery,
    /// Entry points that produced a block, including accepted discoveries.
    pub accepted_roots: usize,
    /// Discovered candidates the traversal rejected (delay-slot interior or
    /// otherwise undecodable) and which are therefore not represented.
    pub skipped_roots: Vec<u32>,
    /// Heuristic-only block starts that could not be decoded and were skipped,
    /// including any that were reached from an accepted discovered candidate
    /// rather than being candidates themselves.
    pub skipped_blocks: Vec<u32>,
}

/// Builds a CFG from the ELF entry and function symbols plus static function
/// discovery over file-backed executable bytes.
///
/// This is the path a stripped retail executable needs: it never trusts a
/// discovered candidate as a reachability claim, and a candidate that cannot be
/// decoded as a block is dropped and reported rather than aborting the whole
/// analysis.
pub fn discover_elf_with_discovery(
    image: &Image,
    file_bytes: &[u8],
    limits: AnalysisLimits,
) -> Result<DiscoveryAnalysis, AnalysisError> {
    let PreparedImage {
        segments,
        mut entry_points,
        roots: authoritative,
    } = prepare(image, file_bytes, limits)?;
    let discovery = discover_function_roots(&segments, limits);

    let authoritative_set: BTreeSet<u32> = authoritative.iter().copied().collect();
    let mut discovered_roots = Vec::new();
    for candidate in discovery.candidate_roots() {
        if !authoritative_set.contains(&candidate) {
            discovered_roots.push(candidate);
        }
    }

    let mut roots = authoritative;
    roots.extend(discovered_roots.iter().copied());
    let heuristic: BTreeSet<u32> = discovered_roots.iter().copied().collect();
    for root in &discovered_roots {
        entry_points.push(EntryPoint {
            address: *root,
            labels: vec!["discovered".to_owned()],
            is_elf_entry: false,
        });
    }

    let built = build_cfg(&segments, entry_points, &roots, &heuristic, limits)?;
    let mut graph = built.graph;
    // A candidate the traversal could not turn into a block must not remain an
    // entry point, or the control-IR verifier would reject the program.
    graph
        .entry_points
        .retain(|entry| graph.blocks.contains_key(&entry.address));
    let accepted_roots = graph.entry_points.len();
    Ok(DiscoveryAnalysis {
        graph,
        discovery,
        accepted_roots,
        skipped_roots: built.rejected_roots,
        skipped_blocks: built.skipped_blocks,
    })
}

fn enqueue_root(
    queue: &mut VecDeque<(u32, bool)>,
    queued_authoritative: &mut BTreeSet<u32>,
    queued_lenient: &mut BTreeSet<u32>,
    address: u32,
    lenient: bool,
) {
    let inserted = if lenient {
        queued_lenient.insert(address)
    } else {
        queued_authoritative.insert(address)
    };
    if inserted {
        queue.push_back((address, lenient));
    }
}

/// A CFG plus the heuristic-only blocks the traversal could not decode.
struct BuiltCfg {
    graph: ControlFlowGraph,
    rejected_roots: Vec<u32>,
    skipped_blocks: Vec<u32>,
}

/// Builds a CFG from one or more executable roots in already materialized
/// PT_LOAD segments. Only reachable words are decoded. Authoritative roots must
/// decode; heuristic roots that fail are reported through `BuiltCfg` instead of
/// aborting the analysis.
pub fn discover_cfg(
    segments: &[LoadedSegment],
    entry_points: Vec<EntryPoint>,
    roots: &[u32],
    limits: AnalysisLimits,
) -> Result<ControlFlowGraph, AnalysisError> {
    let built = build_cfg(segments, entry_points, roots, &BTreeSet::new(), limits)?;
    Ok(built.graph)
}

fn build_cfg(
    segments: &[LoadedSegment],
    entry_points: Vec<EntryPoint>,
    roots: &[u32],
    heuristic_roots: &BTreeSet<u32>,
    limits: AnalysisLimits,
) -> Result<BuiltCfg, AnalysisError> {
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
        let Some(memory_end) =
            u64::from(segment.virtual_address).checked_add(segment.bytes.len() as u64)
        else {
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
        if root & 3 != 0 {
            return Err(AnalysisError::EntryPointNotExecutable(*root));
        }
        unique_roots.insert(*root);
    }

    // Roots that differ only by alias view decode the same bytes. Admit the
    // first view of each physical root so alias duplicates cannot bypass the
    // block budget or replay identical work.
    let mut admitted_roots = BTreeSet::new();
    let mut root_views = BTreeSet::new();
    let mut aliased_root_views = BTreeSet::new();
    for root in unique_roots {
        let Some(physical) = crate::address::physical_main_ram_address(root) else {
            return Err(AnalysisError::EntryPointNotExecutable(root));
        };
        if crate::address::executable_file_bytes(segments, physical, 4).is_none() {
            return Err(AnalysisError::EntryPointNotExecutable(root));
        }
        // A root is decoded in its own region, so a KSEG0 entry is never
        // silently retargeted to a differently declared view. Roots that reach
        // their bytes only through another view are counted separately.
        let own_region = crate::address::file_backed_executable_segment(segments, root).is_some();
        let region = crate::address::AliasRegion::of(root);
        if own_region {
            if root_views.insert((region, physical)) {
                admitted_roots.insert(root);
            }
        } else if aliased_root_views.insert((region, physical)) {
            admitted_roots.insert(root);
        }
    }
    if aliased_root_views.len() > usize::from(limits.max_aliased_roots) {
        return Err(AnalysisError::AliasedViewLimitExceeded {
            limit: limits.max_aliased_roots,
        });
    }

    // Each queue entry records whether it is only heuristically reachable.
    // Authoritative reachability is never dropped: a heuristic-only block that
    // fails to decode is skipped and reported, and it is revisited with hard
    // error semantics if authoritative reachability later arrives there.
    let mut queue: VecDeque<(u32, bool)> = VecDeque::new();
    let mut queued_authoritative = BTreeSet::new();
    let mut queued_lenient = BTreeSet::new();

    let mut leaders = admitted_roots.clone();
    let authoritative_roots: BTreeSet<u32> = admitted_roots
        .iter()
        .filter(|root| !heuristic_roots.contains(root))
        .copied()
        .collect();
    let mut authoritative_leaders = authoritative_roots.clone();
    for root in &admitted_roots {
        enqueue_root(
            &mut queue,
            &mut queued_authoritative,
            &mut queued_lenient,
            *root,
            !authoritative_roots.contains(root),
        );
    }

    let mut blocks = BTreeMap::new();
    let mut owners: BTreeMap<u32, InstructionOwner> = BTreeMap::new();
    let mut external_targets = BTreeSet::new();
    let mut unresolved_indirect_jumps = BTreeSet::new();
    let mut rejected_roots = Vec::new();
    let mut skipped_blocks = Vec::new();
    let mut decoded_instruction_count = 0usize;

    let is_executable_view = |addr: u32| -> bool {
        contains_executable_address(segments, addr)
            || (crate::address::AliasRegion::of(addr).is_some_and(|r| {
                aliased_root_views
                    .iter()
                    .any(|(view_r, _)| *view_r == Some(r))
            }) && crate::address::physical_main_ram_address(addr)
                .is_some_and(|p| crate::address::executable_file_bytes(segments, p, 4).is_some()))
    };

    while let Some((start_pc, lenient)) = queue.pop_front() {
        if let Some(block) = blocks.get(&start_pc) {
            // Authoritative reachability arriving at an already decoded
            // heuristic block upgrades its successors: any of them that were
            // skipped as heuristic-only must now be retried with hard errors.
            if !lenient {
                for target in block_targets(block) {
                    if !is_executable_view(target) {
                        external_targets.insert(target);
                        continue;
                    }
                    leaders.insert(target);
                    authoritative_leaders.insert(target);
                    enqueue_root(
                        &mut queue,
                        &mut queued_authoritative,
                        &mut queued_lenient,
                        target,
                        false,
                    );
                }
            }
            continue;
        }

        if let Some(owner) = owners.get(&start_pc).copied() {
            if owner.location == OwnerLocation::DelaySlot {
                if lenient {
                    skipped_blocks.push(start_pc);
                    if heuristic_roots.contains(&start_pc) {
                        rejected_roots.push(start_pc);
                    }
                    continue;
                }
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
            if let Err(error) = split_block_at(start_pc, owner, &mut blocks, &mut owners) {
                if lenient {
                    skipped_blocks.push(start_pc);
                    if heuristic_roots.contains(&start_pc) {
                        rejected_roots.push(start_pc);
                    }
                    continue;
                }
                return Err(error);
            }
            if blocks.len() > limits.max_blocks {
                return Err(AnalysisError::BlockLimitExceeded {
                    limit: limits.max_blocks,
                });
            }
            continue;
        }

        if !is_executable_view(start_pc) {
            external_targets.insert(start_pc);
            continue;
        }
        if blocks.len() >= limits.max_blocks {
            return Err(AnalysisError::BlockLimitExceeded {
                limit: limits.max_blocks,
            });
        }

        let block = match build_block(
            segments,
            start_pc,
            &leaders,
            &authoritative_leaders,
            &owners,
            &blocks,
            &mut decoded_instruction_count,
            limits,
        ) {
            Ok(block) => block,
            Err(_) if lenient => {
                skipped_blocks.push(start_pc);
                if heuristic_roots.contains(&start_pc) {
                    rejected_roots.push(start_pc);
                }
                continue;
            }
            Err(error) => return Err(error),
        };

        register_block_owners(&block, &mut owners)?;
        let next_targets = block_targets(&block);
        if let Terminator::IndirectJump { instruction, .. } = &block.terminator {
            unresolved_indirect_jumps.insert(instruction.pc);
        }
        blocks.insert(start_pc, block);

        for target in next_targets {
            if !is_executable_view(target) {
                external_targets.insert(target);
                continue;
            }
            // A target inside an already decoded block is a split request even
            // when it arrives through another alias view, so it is admitted.
            // Requests are queued once per address; an alias cycle therefore
            // costs at most one extra queue entry per view, and each view that
            // is decoded is claimed in `decoded_alias_views`.
            leaders.insert(target);
            if !lenient {
                authoritative_leaders.insert(target);
            }
            enqueue_root(
                &mut queue,
                &mut queued_authoritative,
                &mut queued_lenient,
                target,
                lenient,
            );
        }
    }

    Ok(BuiltCfg {
        graph: ControlFlowGraph {
            entry_points,
            blocks,
            unresolved_indirect_jumps: unresolved_indirect_jumps.into_iter().collect(),
            external_targets: external_targets.into_iter().collect(),
            decoded_instruction_count,
        },
        rejected_roots,
        skipped_blocks,
    })
}

/// Decodes exactly one basic block beginning at `start_pc`.
///
/// The terminator and its delay slot stay explicit; the returned block's body
/// excludes both. Errors propagate to the caller so authoritative roots fail
/// hard while heuristic roots are tolerated.
#[allow(clippy::too_many_arguments)]
fn build_block(
    segments: &[LoadedSegment],
    start_pc: u32,
    leaders: &BTreeSet<u32>,
    authoritative_leaders: &BTreeSet<u32>,
    owners: &BTreeMap<u32, InstructionOwner>,
    blocks: &BTreeMap<u32, BasicBlock>,
    decoded_instruction_count: &mut usize,
    limits: AnalysisLimits,
) -> Result<BasicBlock, AnalysisError> {
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

        increment_instruction_count(decoded_instruction_count, limits.max_instructions)?;

        let instruction = mips::decode(word, pc);
        if !instruction.is_supported()
            || matches!(
                instruction.opcode,
                Opcode::Syscall | Opcode::Break | Opcode::Eret
            )
        {
            break Terminator::Unsupported { instruction };
        }

        if instruction.is_conditional_branch() {
            let delay_slot =
                fetch_delay_slot(segments, instruction, authoritative_leaders, owners)?;
            increment_instruction_count(decoded_instruction_count, limits.max_instructions)?;
            let condition = branch_condition(instruction)
                .ok_or(AnalysisError::InternalBlockOwnershipError(instruction.pc))?;
            let fallthrough_pc = instruction
                .pc
                .checked_add(8)
                .ok_or(AnalysisError::GuestPcOverflow(instruction.pc))?;
            break Terminator::ConditionalBranch {
                instruction,
                condition,
                target_pc: instruction.target,
                fallthrough_pc,
                delay_slot,
                delay_slot_policy: if instruction.is_branch_likely() {
                    DelaySlotPolicy::AnnulWhenNotTaken
                } else {
                    DelaySlotPolicy::ExecuteAlways
                },
                link_register: matches!(
                    instruction.opcode,
                    Opcode::Bltzal | Opcode::Bgezal | Opcode::Bltzall | Opcode::Bgezall
                )
                .then_some(31),
            };
        }

        match instruction.opcode {
            Opcode::J | Opcode::Jal => {
                let delay_slot =
                    fetch_delay_slot(segments, instruction, authoritative_leaders, owners)?;
                increment_instruction_count(decoded_instruction_count, limits.max_instructions)?;
                let return_pc = if instruction.opcode == Opcode::Jal {
                    Some(
                        instruction
                            .pc
                            .checked_add(8)
                            .ok_or(AnalysisError::GuestPcOverflow(instruction.pc))?,
                    )
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
                let delay_slot =
                    fetch_delay_slot(segments, instruction, authoritative_leaders, owners)?;
                increment_instruction_count(decoded_instruction_count, limits.max_instructions)?;
                break Terminator::Return {
                    instruction,
                    source_register: instruction.rs,
                    delay_slot,
                };
            }
            Opcode::Jr | Opcode::Jalr => {
                let delay_slot =
                    fetch_delay_slot(segments, instruction, authoritative_leaders, owners)?;
                increment_instruction_count(decoded_instruction_count, limits.max_instructions)?;
                let link_register = if instruction.opcode == Opcode::Jalr && instruction.rd != 0 {
                    Some(instruction.rd)
                } else {
                    None
                };
                let return_pc = if link_register.is_some() {
                    Some(
                        instruction
                            .pc
                            .checked_add(8)
                            .ok_or(AnalysisError::GuestPcOverflow(instruction.pc))?,
                    )
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
        pc = pc
            .checked_add(4)
            .ok_or(AnalysisError::GuestPcOverflow(pc))?;
    };

    Ok(BasicBlock {
        start_pc,
        instructions,
        terminator,
    })
}

fn read_instruction_word(segments: &[LoadedSegment], pc: u32) -> Option<u32> {
    if pc & 3 != 0 {
        return None;
    }
    let payload = crate::address::region_payload(pc)?;
    let bytes = crate::address::executable_file_bytes(segments, payload, 4)?;
    Some(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
}

fn increment_instruction_count(count: &mut usize, limit: usize) -> Result<(), AnalysisError> {
    *count = count
        .checked_add(1)
        .ok_or(AnalysisError::InstructionLimitExceeded { limit })?;
    if *count > limit {
        return Err(AnalysisError::InstructionLimitExceeded { limit });
    }
    Ok(())
}

fn fetch_delay_slot(
    segments: &[LoadedSegment],
    control: DecodedInstruction,
    authoritative_leaders: &BTreeSet<u32>,
    owners: &BTreeMap<u32, InstructionOwner>,
) -> Result<DecodedInstruction, AnalysisError> {
    let delay_pc = control
        .pc
        .checked_add(4)
        .ok_or(AnalysisError::GuestPcOverflow(control.pc))?;
    // A heuristic-only leader that happens to land in this delay slot is not an
    // authoritative boundary claim. Ignoring it lets the authoritative control
    // instruction decode; the candidate is rejected as a delay-slot interior
    // when its own queue entry is visited.
    if authoritative_leaders.contains(&delay_pc) || owners.contains_key(&delay_pc) {
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
    // A trap in a delay slot has no scalar-IR representation. Rejecting it here
    // turns the owning block into an explicit unsupported stop instead of
    // allowing a graph that cannot be lowered to control IR.
    if !delay_slot.is_supported()
        || matches!(
            delay_slot.opcode,
            Opcode::Syscall | Opcode::Break | Opcode::Eret
        )
    {
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
        Opcode::Beq | Opcode::Beql => Some(BranchCondition::Equal {
            lhs: instruction.rs,
            rhs: instruction.rt,
        }),
        Opcode::Bne | Opcode::Bnel => Some(BranchCondition::NotEqual {
            lhs: instruction.rs,
            rhs: instruction.rt,
        }),
        Opcode::Blez | Opcode::Blezl => Some(BranchCondition::LessThanOrEqualZero {
            register: instruction.rs,
        }),
        Opcode::Bgtz | Opcode::Bgtzl => Some(BranchCondition::GreaterThanZero {
            register: instruction.rs,
        }),
        Opcode::Bltz | Opcode::Bltzal | Opcode::Bltzl | Opcode::Bltzall => {
            Some(BranchCondition::LessThanZero {
                register: instruction.rs,
            })
        }
        Opcode::Bgez | Opcode::Bgezal | Opcode::Bgezl | Opcode::Bgezall => {
            Some(BranchCondition::GreaterThanOrEqualZero {
                register: instruction.rs,
            })
        }
        Opcode::Bc1f | Opcode::Bc1fl => Some(BranchCondition::FpuCondition { when_set: false }),
        Opcode::Bc1t | Opcode::Bc1tl => Some(BranchCondition::FpuCondition { when_set: true }),
        _ => None,
    }
}

fn block_targets(block: &BasicBlock) -> Vec<u32> {
    block
        .terminator
        .outgoing_edges(block.start_pc)
        .into_iter()
        .map(|edge| edge.to_pc)
        .collect()
}

fn register_block_owners(
    block: &BasicBlock,
    owners: &mut BTreeMap<u32, InstructionOwner>,
) -> Result<(), AnalysisError> {
    for (index, instruction) in block.instructions.iter().enumerate() {
        if owners
            .insert(
                instruction.pc,
                InstructionOwner {
                    block_start: block.start_pc,
                    location: OwnerLocation::Body(index),
                },
            )
            .is_some()
        {
            return Err(AnalysisError::InternalBlockOwnershipError(instruction.pc));
        }
    }

    if let Some(instruction) = block.terminator.control_instruction()
        && owners
            .insert(
                instruction.pc,
                InstructionOwner {
                    block_start: block.start_pc,
                    location: OwnerLocation::Control,
                },
            )
            .is_some()
    {
        return Err(AnalysisError::InternalBlockOwnershipError(instruction.pc));
    }

    if let Some(delay_slot) = block.terminator.delay_slot()
        && owners
            .insert(
                delay_slot.pc,
                InstructionOwner {
                    block_start: block.start_pc,
                    location: OwnerLocation::DelaySlot,
                },
            )
            .is_some()
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

    let mut prefix = blocks
        .remove(&owner.block_start)
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
            (OwnerLocation::Body(index), Some(split_index)) => {
                OwnerLocation::Body(index - split_index)
            }
            (other, _) => other,
        };
        owners.insert(
            pc,
            InstructionOwner {
                block_start: target_pc,
                location,
            },
        );
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
    image
        .symbols
        .iter()
        .filter(|symbol| symbol.kind == STT_FUNC && symbol.section_index != 0 && symbol.value != 0)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::elf::PF_X;

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
        let graph = discover_cfg(
            &segments,
            vec![entry(0x1000)],
            &roots,
            AnalysisLimits::default(),
        )
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
        assert!(edges.iter().any(|edge| edge.from_block == 0x1000
            && edge.to_pc == 0x1010
            && edge.kind == EdgeKind::Taken));
        assert!(edges.iter().any(|edge| edge.from_block == 0x1000
            && edge.to_pc == 0x100C
            && edge.kind == EdgeKind::Fallthrough));
        assert!(matches!(
            &graph.blocks[&0x1010].terminator,
            Terminator::Return { .. }
        ));
    }

    #[test]
    fn records_branch_likely_annulment_and_link_semantics() {
        let words = [
            (0x14 << 26) | (8 << 21) | 2, // beql t0, zero, 0x600C
            0x2409_0007,                  // delay slot: t1 = 7
            0x240A_0005,                  // not-taken path: t2 = 5
            0x03E0_0008,                  // jr ra
            0x0000_0000,                  // return delay slot
        ];
        let segments = [segment(0x6000, &words)];
        let roots = [0x6000];
        let graph = discover_cfg(
            &segments,
            vec![entry(0x6000)],
            &roots,
            AnalysisLimits::default(),
        )
        .expect("valid branch-likely CFG");

        match &graph.blocks[&0x6000].terminator {
            Terminator::ConditionalBranch {
                target_pc,
                fallthrough_pc,
                delay_slot,
                delay_slot_policy,
                link_register,
                ..
            } => {
                assert_eq!(*target_pc, 0x600C);
                assert_eq!(*fallthrough_pc, 0x6008);
                assert_eq!(delay_slot.pc, 0x6004);
                assert_eq!(*delay_slot_policy, DelaySlotPolicy::AnnulWhenNotTaken);
                assert_eq!(*link_register, None);
            }
            other => panic!("expected branch-likely terminator, got {other:?}"),
        }
        assert_eq!(graph.blocks[&0x6008].instructions.len(), 1);
        assert_eq!(graph.blocks[&0x6008].instructions[0].opcode, Opcode::Addiu);
        assert!(matches!(
            graph.blocks[&0x6008].terminator,
            Terminator::Fallthrough { next_pc: 0x600C }
        ));

        let link_words = [
            (0x01 << 26) | (8 << 21) | (0x12 << 16) | 2, // bltzall t0, zero, 0x610C
            0x2409_0007,                                 // delay slot: t1 = 7
            0x240A_0005,                                 // not-taken path: t2 = 5
            0x03E0_0008,                                 // jr ra
            0x0000_0000,                                 // return delay slot
        ];
        let link_segments = [segment(0x6100, &link_words)];
        let link_graph = discover_cfg(
            &link_segments,
            vec![entry(0x6100)],
            &[0x6100],
            AnalysisLimits::default(),
        )
        .expect("valid link-likely CFG");
        match &link_graph.blocks[&0x6100].terminator {
            Terminator::ConditionalBranch {
                delay_slot_policy,
                link_register,
                ..
            } => {
                assert_eq!(*delay_slot_policy, DelaySlotPolicy::AnnulWhenNotTaken);
                assert_eq!(*link_register, Some(31));
            }
            other => panic!("expected link-likely terminator, got {other:?}"),
        }
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
        let graph = discover_cfg(
            &segments,
            vec![entry(0x2000)],
            &roots,
            AnalysisLimits::default(),
        )
        .expect("backward branch graph");
        assert_eq!(graph.blocks.len(), 3);
        assert_eq!(graph.blocks[&0x2000].instructions.len(), 1);
        assert!(matches!(
            &graph.blocks[&0x2000].terminator,
            Terminator::Fallthrough { next_pc: 0x2004 }
        ));
        assert_eq!(graph.blocks[&0x2004].instructions.len(), 1);
        assert!(matches!(
            &graph.blocks[&0x2004].terminator,
            Terminator::ConditionalBranch {
                target_pc: 0x2004,
                ..
            }
        ));
    }

    #[test]
    fn rejects_a_branch_target_that_enters_a_delay_slot() {
        let words = [0x1100_0000, 0x0000_0000, 0x03E0_0008, 0x0000_0000];
        let segments = [segment(0x3000, &words)];
        let roots = [0x3000];
        let error = discover_cfg(
            &segments,
            vec![entry(0x3000)],
            &roots,
            AnalysisLimits::default(),
        )
        .expect_err("branch into delay slot is not a valid block boundary");
        assert!(matches!(
            error,
            AnalysisError::TargetEntersDelaySlot {
                target_pc: 0x3004,
                ..
            }
        ));
    }

    #[test]
    fn rejects_inconsistent_external_segment_metadata() {
        let mut segment = segment(0x3900, &[0x2408_0005]);
        segment.file_size = segment.bytes.len() + 1;
        let segments = [segment];
        let roots = [0x3900];
        let error = discover_cfg(
            &segments,
            vec![entry(0x3900)],
            &roots,
            AnalysisLimits::default(),
        )
        .expect_err("file-backed size cannot exceed the owned buffer");
        assert!(matches!(error, AnalysisError::InvalidLoadedSegment { .. }));
    }

    #[test]
    fn does_not_decode_zero_filled_bss_as_executable_instructions() {
        let mut segment = segment(0x3800, &[0x2408_0005, 0]);
        segment.file_size = 4;
        let segments = [segment];
        let roots = [0x3800];
        let graph = discover_cfg(
            &segments,
            vec![entry(0x3800)],
            &roots,
            AnalysisLimits::default(),
        )
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
        let words = [0x0800_0802, 0x0800_0803, 0x03E0_0008, 0x0000_0000];
        let segments = [segment(0x2000, &words)];
        let roots = [0x2000];
        let error = discover_cfg(
            &segments,
            vec![entry(0x2000)],
            &roots,
            AnalysisLimits::default(),
        )
        .expect_err("control transfer in a delay slot is unsupported");
        assert!(matches!(
            error,
            AnalysisError::ControlTransferInDelaySlot { .. }
        ));
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
        let graph = discover_cfg(
            &segments,
            vec![entry(0x4000)],
            &roots,
            AnalysisLimits::default(),
        )
        .expect("indirect call graph");
        assert_eq!(graph.unresolved_indirect_jumps, vec![0x4000]);
        assert!(graph.edges().iter().any(|edge| {
            edge.from_block == 0x4000
                && edge.to_pc == 0x4008
                && edge.kind == EdgeKind::ReturnContinuation
        }));
    }

    #[test]
    fn follows_kseg0_calls_made_through_jalr() {
        // The dominant real-world shape: one PT_LOAD described in KSEG0. The
        // R5900 `J` region comes from the delay-slot address, so a boot
        // executable that calls within cached memory uses `JALR` (or a
        // register-loaded target); `JAL` from KSEG0 would select KSEG1.
        //   lui  t0, 0x8010      # t0 = 0x80100000
        //   addiu t0, t0, 0x10   # t0 = 0x80100010
        //   jalr t0              # call 0x80100010, ra = 0x8010000C
        //   nop                  # delay slot
        //   addiu t1, zero, 1    # callee body
        //   jr ra                # return
        //   nop                  # return delay slot: 0x8010001C
        let words = [
            0x3C08_8010, // lui  $8, 0x8010
            0x2508_0010, // addiu $8, $8, 0x10
            0x0100_F809, // jalr $8
            0x0000_0000, // delay slot
            0x2409_0001, // callee body
            0x03E0_0008, // jr $ra
            0x0000_0000, // return delay slot
        ];
        let segments = [segment(0x8010_0000, &words)];
        let entry_pc = 0x8010_0000;
        let graph = discover_cfg(
            &segments,
            vec![entry(entry_pc)],
            &[entry_pc],
            AnalysisLimits::default(),
        )
        .expect("KSEG0 control flow");

        // The call block and the callee block are reachable statically. A
        // register-indirect `jr` return has no static destination, so no
        // continuation block is invented for it.
        assert_eq!(graph.blocks.len(), 2);
        // Only the `jalr` target is unresolved; the callee itself was reached
        // statically because the same image contains it.
        assert_eq!(graph.unresolved_indirect_jumps, vec![0x8010_0008]);
        assert!(graph.external_targets.is_empty());
        assert!(matches!(
            graph.blocks[&0x8010_0000].terminator,
            Terminator::IndirectJump {
                source_register: 8,
                ..
            }
        ));
        let callee = discover_cfg(
            &segments,
            vec![entry(0x8010_0010)],
            &[0x8010_0010],
            AnalysisLimits::default(),
        )
        .expect("KSEG0 callee");
        assert_eq!(callee.blocks[&0x8010_0010].instructions.len(), 1);
        assert_eq!(
            callee.blocks[&0x8010_0010].instructions[0].opcode,
            Opcode::Addiu
        );
        assert!(matches!(
            callee.blocks[&0x8010_0010].terminator,
            Terminator::Return { .. }
        ));

        // A fetch through a differently declared view is still refused: the
        // KSEG0 image never materialized 0x00100010, even though the payload
        // aliases it, because the runtime address translation must be decided
        // by the image, not guessed by the analyzer.
        assert!(!contains_executable_address(&segments, 0x0010_0010));
        assert!(contains_executable_address(&segments, 0x8010_0010));
    }

    #[test]
    fn alias_roots_are_limited_and_contribute_distinct_blocks() {
        // The KSEG0 root aliases bytes of a direct-physical segment. It is
        // accepted as a distinct entry, gets its own block, and does not
        // corrupt block ownership of the direct view.
        let words = [
            0x2401_0001, // addiu $1, $0, 1
            0x03E0_0008, // jr $ra
            0x0000_0000, // return delay slot
        ];
        let segments = [segment(0x0010_0000, &words)];
        let limits = AnalysisLimits {
            max_aliased_roots: 1,
            ..Default::default()
        };
        let graph = discover_cfg(
            &segments,
            vec![entry(0x0010_0000), entry(0x8010_0000)],
            &[0x0010_0000, 0x8010_0000],
            limits,
        )
        .expect("aliased roots");
        assert!(graph.blocks.contains_key(&0x0010_0000));
        assert!(graph.blocks.contains_key(&0x8010_0000));
        assert_eq!(graph.blocks.len(), 2);
        assert_eq!(graph.decoded_instruction_count, 6);

        let zero_budget = AnalysisLimits {
            max_aliased_roots: 0,
            ..Default::default()
        };
        let error = discover_cfg(
            &segments,
            vec![entry(0x8010_0000)],
            &[0x8010_0000],
            zero_budget,
        )
        .expect_err("aliased root without budget");
        assert_eq!(error, AnalysisError::AliasedViewLimitExceeded { limit: 0 });
    }

    #[test]
    fn rejects_a_trap_instruction_in_a_delay_slot() {
        let words = [
            0x1100_0001, // beq $t0, $zero, 0x3008
            0x0000_000D, // delay slot: break, which scalar IR cannot represent
            0x03E0_0008, // jr $ra
            0x0000_0000, // return delay slot
        ];
        let segments = [segment(0x3000, &words)];
        let error = discover_cfg(
            &segments,
            vec![entry(0x3000)],
            &[0x3000],
            AnalysisLimits::default(),
        )
        .expect_err("trap delay slot is an explicit unsupported stop");
        assert!(matches!(
            error,
            AnalysisError::UnsupportedDelaySlot {
                control_pc: 0x3000,
                delay_pc: 0x3004,
            }
        ));
    }

    #[test]
    fn discovers_jal_targets_and_stack_prologues() {
        let words = [
            0x27BD_FFE0, // 0x1000: addiu $sp, $sp, -32
            0x0C00_0404, // 0x1004: jal 0x1010
            0x0000_0000, // 0x1008: delay slot
            0x03E0_0008, // 0x100C: jr $ra
            0x27BD_FFF0, // 0x1010: addiu $sp, $sp, -16
            0x0C00_2400, // 0x1014: jal 0x9000, outside executable file bytes
        ];
        let segments = [segment(0x1000, &words)];
        let discovery = discover_function_roots(&segments, AnalysisLimits::default());

        assert_eq!(discovery.scanned_words, 6);
        assert!(!discovery.truncated);
        assert_eq!(
            discovery.call_targets,
            BTreeSet::from([0x1010]),
            "only JAL targets backed by executable file bytes are admitted"
        );
        assert_eq!(
            discovery.prologue_candidates,
            BTreeSet::from([0x1000, 0x1010])
        );
        assert_eq!(
            discovery.candidate_roots(),
            BTreeSet::from([0x1000, 0x1010])
        );
        assert_eq!(discovery.candidate_count(), 2);
    }

    #[test]
    fn tolerates_a_heuristic_root_inside_a_delay_slot() {
        // 0x3004 is the delay slot of the branch at 0x3000 but is also queued
        // as a discovered candidate. The candidate must not stop the
        // authoritative block from decoding; it is rejected when visited.
        let words = [
            0x1100_0001, // 0x3000: beq $t0, $zero, 0x3008
            0x0000_0000, // 0x3004: delay slot (also a heuristic candidate)
            0x03E0_0008, // 0x3008: jr $ra
            0x0000_0000, // 0x300C: return delay slot
        ];
        let segments = [segment(0x3000, &words)];
        let heuristic = BTreeSet::from([0x3004]);
        let built = build_cfg(
            &segments,
            vec![entry(0x3000), entry(0x3004)],
            &[0x3000, 0x3004],
            &heuristic,
            AnalysisLimits::default(),
        )
        .expect("authoritative control flow still decodes");

        assert_eq!(built.rejected_roots, vec![0x3004]);
        assert_eq!(built.skipped_blocks, vec![0x3004]);
        assert!(built.graph.blocks.contains_key(&0x3000));
        assert!(built.graph.blocks.contains_key(&0x3008));
        assert!(!built.graph.blocks.contains_key(&0x3004));
        assert_eq!(built.graph.blocks.len(), 2);
        assert!(matches!(
            &built.graph.blocks[&0x3000].terminator,
            Terminator::ConditionalBranch { delay_slot, .. } if delay_slot.pc == 0x3004
        ));
    }

    #[test]
    fn stops_discovery_scan_at_the_candidate_limit() {
        let words = [
            0x27BD_FFE0, // 0x1000: addiu $sp, $sp, -32
            0x27BD_FFF0, // 0x1004: addiu $sp, $sp, -16
            0x03E0_0008, // 0x1008: jr $ra
            0x0000_0000, // 0x100C: delay slot
        ];
        let segments = [segment(0x1000, &words)];
        let limits = AnalysisLimits {
            max_function_roots: 1,
            ..Default::default()
        };
        let discovery = discover_function_roots(&segments, limits);

        assert!(discovery.truncated);
        assert_eq!(discovery.scanned_words, 1);
        assert_eq!(discovery.prologue_candidates, BTreeSet::from([0x1000]));
        assert_eq!(discovery.candidate_roots(), BTreeSet::from([0x1000]));
    }

    #[test]
    fn discovers_roots_in_a_stripped_executable_image() {
        // A stripped PT_LOAD has no STT_FUNC symbols, so the ELF entry alone
        // only yields the `jr $ra` at 0x100000. Discovery recovers the stack
        // prologues at 0x100010 and 0x100030 plus the `jal` target, which makes
        // the rest of the callee bodies statically reachable.
        let mut words = vec![0u32; 15]; // 0x100000..0x10003B
        words[0] = 0x03E0_0008; // 0x100000: jr $ra
        words[4] = 0x27BD_FFE0; // 0x100010: addiu $sp, $sp, -32
        words[5] = 0x0C04_000C; // 0x100014: jal 0x100030
        words[7] = 0x03E0_0008; // 0x10001C: jr $ra
        words[12] = 0x27BD_FFF0; // 0x100030: addiu $sp, $sp, -16
        words[13] = 0x03E0_0008; // 0x100034: jr $ra

        let mut file_bytes = Vec::with_capacity(words.len() * 4);
        for word in &words {
            file_bytes.extend_from_slice(&word.to_le_bytes());
        }
        let file_size = u32::try_from(file_bytes.len()).expect("synthetic image size");

        let image = Image {
            file_type: elf::ET_EXEC,
            machine: elf::EM_MIPS,
            entry_point: 0x0010_0000,
            flags: 0,
            section_name_table_index: 0,
            program_headers: vec![elf::ProgramHeader {
                kind: elf::PT_LOAD,
                offset: 0,
                virtual_address: 0x0010_0000,
                physical_address: 0x0010_0000,
                file_size,
                memory_size: file_size,
                flags: elf::PF_R | PF_X,
                alignment: 4,
            }],
            section_headers: Vec::new(),
            symbols: Vec::new(),
            relocations: Vec::new(),
        };

        let analysis = discover_elf_with_discovery(&image, &file_bytes, AnalysisLimits::default())
            .expect("stripped synthetic ELF");

        assert_eq!(
            analysis.discovery.candidate_roots(),
            BTreeSet::from([0x0010_0010, 0x0010_0030])
        );
        assert!(analysis.skipped_roots.is_empty());
        assert!(analysis.skipped_blocks.is_empty());
        assert_eq!(analysis.accepted_roots, 3);
        assert_eq!(
            analysis
                .graph
                .entry_points
                .iter()
                .map(|entry| entry.address)
                .collect::<Vec<_>>(),
            vec![0x0010_0000, 0x0010_0010, 0x0010_0030]
        );
        let discovered = analysis
            .graph
            .entry_points
            .iter()
            .find(|entry| entry.address == 0x0010_0010)
            .expect("discovered prologue is an entry point");
        assert_eq!(discovered.labels, vec!["discovered".to_owned()]);
        assert!(!discovered.is_elf_entry);
        assert_eq!(
            analysis.graph.blocks.keys().copied().collect::<Vec<_>>(),
            vec![0x0010_0000, 0x0010_0010, 0x0010_001C, 0x0010_0030]
        );
        assert_eq!(analysis.graph.decoded_instruction_count, 10);
    }

    #[test]
    fn reports_configured_block_and_instruction_limits() {
        let words = [
            0x1100_0002, // beq $t0, $zero, 0x400C
            0x0000_0000, // delay slot
            0x0000_0000, // fallthrough block
            0x03E0_0008, // jr $ra
            0x0000_0000, // return delay slot
        ];
        let segments = [segment(0x4000, &words)];
        let entries = vec![entry(0x4000)];
        let roots = [0x4000];

        let limits = AnalysisLimits {
            max_blocks: 1,
            ..AnalysisLimits::default()
        };
        let error = discover_cfg(&segments, entries.clone(), &roots, limits)
            .expect_err("block limit is enforced");
        assert!(matches!(
            error,
            AnalysisError::BlockLimitExceeded { limit: 1 }
        ));

        let limits = AnalysisLimits {
            max_instructions: 1,
            ..AnalysisLimits::default()
        };
        let error =
            discover_cfg(&segments, entries, &roots, limits).expect_err("instruction limit");
        assert!(matches!(
            error,
            AnalysisError::InstructionLimitExceeded { limit: 1 }
        ));
    }
}
