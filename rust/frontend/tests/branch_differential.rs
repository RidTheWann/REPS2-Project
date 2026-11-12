// SPDX-License-Identifier: GPL-3.0-or-later
//! Differential test of R5900 branch/control-transfer semantics against the
//! independent Python oracle (tools/branch_oracle.py).
//!
//! The test vectors in `branch_vectors.inc` are produced by the oracle, which
//! evaluates every branch encoding against the documented EE architectural
//! rules. This binary reaches the same outcomes through the decoder + CFG
//! analysis, so agreement is evidence about both implementations rather than
//! about one of them twice.

use reps2_frontend::analysis::{self, AnalysisLimits, EntryPoint, Terminator};
use reps2_frontend::elf::LoadedSegment;
use reps2_frontend::mips::Opcode;

#[derive(Clone, Copy, Debug)]
#[allow(dead_code)]
struct BranchVector {
    word: u32,
    kind: BranchKind,
    pc: u32,
    gpr_rs: u64,
    gpr_rt: u64,
    rs_index: u8,
    fpu_cond: bool,
    taken: bool,
    next_pc: u32,
    link_reg: i32,
    link_value: u32,
    delay_slot_executes: bool,
    gpr0_suppressed: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[allow(clippy::upper_case_acronyms)]
enum BranchKind {
    BEQ,
    BNE,
    BLEZ,
    BGTZ,
    BLTZ,
    BGEZ,
    BEQL,
    BNEL,
    BLEZL,
    BGTZL,
    BLTZL,
    BGEZL,
    BLTZAL,
    BGEZAL,
    BLTZALL,
    BGEZALL,
    J,
    JAL,
    JR,
    JALR,
    BC1F,
    BC1FL,
    BC1T,
    BC1TL,
}

impl BranchKind {
    #[allow(dead_code)]
    fn to_opcode(self) -> Opcode {
        match self {
            Self::BEQ => Opcode::Beq,
            Self::BNE => Opcode::Bne,
            Self::BLEZ => Opcode::Blez,
            Self::BGTZ => Opcode::Bgtz,
            Self::BLTZ => Opcode::Bltz,
            Self::BGEZ => Opcode::Bgez,
            Self::BEQL => Opcode::Beql,
            Self::BNEL => Opcode::Bnel,
            Self::BLEZL => Opcode::Blezl,
            Self::BGTZL => Opcode::Bgtzl,
            Self::BLTZL => Opcode::Bltzl,
            Self::BGEZL => Opcode::Bgezl,
            Self::BLTZAL => Opcode::Bltzal,
            Self::BGEZAL => Opcode::Bgezal,
            Self::BLTZALL => Opcode::Bltzall,
            Self::BGEZALL => Opcode::Bgezall,
            Self::J => Opcode::J,
            Self::JAL => Opcode::Jal,
            Self::JR => Opcode::Jr,
            Self::JALR => Opcode::Jalr,
            Self::BC1F => Opcode::Bc1f,
            Self::BC1FL => Opcode::Bc1fl,
            Self::BC1T => Opcode::Bc1t,
            Self::BC1TL => Opcode::Bc1tl,
        }
    }
}

// Include the generated vectors
include!("generated/branch_vectors.inc");

fn expected_condition(vec: &BranchVector) -> analysis::BranchCondition {
    match vec.kind {
        BranchKind::BEQ | BranchKind::BEQL => analysis::BranchCondition::Equal {
            lhs: vec.rs_index,
            rhs: 9,
        },
        BranchKind::BNE | BranchKind::BNEL => analysis::BranchCondition::NotEqual {
            lhs: vec.rs_index,
            rhs: 9,
        },
        BranchKind::BLEZ | BranchKind::BLEZL => analysis::BranchCondition::LessThanOrEqualZero {
            register: vec.rs_index,
        },
        BranchKind::BGTZ | BranchKind::BGTZL => analysis::BranchCondition::GreaterThanZero {
            register: vec.rs_index,
        },
        BranchKind::BLTZ | BranchKind::BLTZL | BranchKind::BLTZAL | BranchKind::BLTZALL => {
            analysis::BranchCondition::LessThanZero {
                register: vec.rs_index,
            }
        }
        BranchKind::BGEZ | BranchKind::BGEZL | BranchKind::BGEZAL | BranchKind::BGEZALL => {
            analysis::BranchCondition::GreaterThanOrEqualZero {
                register: vec.rs_index,
            }
        }
        BranchKind::BC1F | BranchKind::BC1FL => {
            analysis::BranchCondition::FpuCondition { when_set: false }
        }
        BranchKind::BC1T | BranchKind::BC1TL => {
            analysis::BranchCondition::FpuCondition { when_set: true }
        }
        _ => unreachable!("conditional-branch vector expected"),
    }
}

fn expected_link(vec: &BranchVector) -> Option<u8> {
    if vec.link_reg < 0 || vec.gpr0_suppressed {
        None
    } else {
        Some(vec.link_reg as u8)
    }
}

#[test]
fn branch_differential_decoder_and_cfg() {
    let mut passed = 0;
    let mut failed = 0;
    let mut skipped = 0;

    for (idx, vec) in BRANCH_VECTORS.iter().enumerate() {
        // 0xFFFF1000 is in the EE "Other" region (not Kuseg/PhysicalAlias/
        // Kseg0/Kseg1) and its payload exceeds 32 MiB main RAM, so it cannot
        // alias an executable segment; discover_cfg correctly rejects it.
        let valid_base_pcs = [0x00001000u32, 0x80001000, 0xA0001000];
        if !valid_base_pcs.contains(&vec.pc) {
            skipped += 1;
            continue;
        }

        // Build minimal segment covering exactly what we need. Indirect
        // targets stay unresolved by the analysis, so they do not need to be
        // covered; only statically followed edges must decode. A direct
        // target outside 32 MiB main RAM is recorded but not followed.
        let branch_pc = vec.pc;
        let delay_pc = branch_pc.wrapping_add(4);
        let fallthrough_pc = branch_pc.wrapping_add(8);
        let follows_target = match vec.kind {
            BranchKind::J | BranchKind::JAL => (vec.next_pc & 0x1FFF_FFFF) < 0x0200_0000,
            BranchKind::JR | BranchKind::JALR => false,
            _ => vec.taken,
        };
        let target_pc = if follows_target {
            vec.next_pc
        } else {
            fallthrough_pc
        };

        let min_addr = [branch_pc, delay_pc, fallthrough_pc, target_pc]
            .iter()
            .min()
            .copied()
            .unwrap_or(branch_pc);
        let max_addr = [branch_pc, delay_pc, fallthrough_pc, target_pc]
            .iter()
            .max()
            .copied()
            .unwrap_or(branch_pc);

        let segment_start = (min_addr & !0xFFF).max(0x1000);
        let segment_end = (max_addr + 16) | 0xFFF;
        let word_count = ((segment_end - segment_start) / 4) as usize;

        let mut words = vec![0u32; word_count];

        let write_word = |words: &mut [u32], pc: u32, word: u32| {
            if pc >= segment_start && pc < segment_end {
                let idx = ((pc - segment_start) / 4) as usize;
                if idx < words.len() {
                    words[idx] = word;
                }
            }
        };

        // The branch word is written last: a taken target can coincide with
        // the branch PC itself (self-loop offsets), and the control
        // instruction must win over any marker placed at the same address.
        write_word(&mut words, delay_pc, 0x2409_0007); // addiu t1, zero, 7
        write_word(&mut words, fallthrough_pc, 0x240A_0005); // addiu t2, zero, 5
        if follows_target {
            // A statically followed target is also decoded as its own entry
            // root; keep it a non-control word so a nearby branch's delay slot
            // or fallthrough landing on it still decodes ordinary code.
            write_word(&mut words, target_pc, 0x240B_0003); // addiu t3, zero, 3
            write_word(&mut words, target_pc.wrapping_add(4), 0x0000_0000); // nop
        }
        write_word(&mut words, branch_pc, vec.word);

        let mut bytes = Vec::with_capacity(words.len() * 4);
        for word in &words {
            bytes.extend_from_slice(&word.to_le_bytes());
        }

        let segment = LoadedSegment {
            virtual_address: segment_start,
            flags: 1, // PF_X
            file_size: bytes.len(),
            bytes: bytes.clone(),
        };

        let segments = [segment];

        // Entry points: PC (branch), fallthrough, and target
        let mut entries = vec![EntryPoint {
            address: vec.pc,
            labels: vec!["branch_entry".to_owned()],
            is_elf_entry: true,
        }];

        if fallthrough_pc != vec.pc {
            entries.push(EntryPoint {
                address: fallthrough_pc,
                labels: vec!["fallthrough".to_owned()],
                is_elf_entry: false,
            });
        }
        if follows_target && vec.next_pc != vec.pc && vec.next_pc != fallthrough_pc {
            entries.push(EntryPoint {
                address: vec.next_pc,
                labels: vec!["target".to_owned()],
                is_elf_entry: false,
            });
        }

        let roots: Vec<u32> = entries.iter().map(|e| e.address).collect();

        let graph =
            match analysis::discover_cfg(&segments, entries, &roots, AnalysisLimits::default()) {
                Ok(g) => g,
                Err(e) => {
                    eprintln!("Vector {}: CFG discovery failed: {}", idx, e);
                    failed += 1;
                    continue;
                }
            };

        let block = match graph.blocks.get(&vec.pc) {
            Some(b) => b,
            None => {
                eprintln!("Vector {}: No block at PC 0x{:08X}", idx, vec.pc);
                failed += 1;
                continue;
            }
        };

        match &block.terminator {
            Terminator::ConditionalBranch {
                target_pc,
                fallthrough_pc,
                condition,
                delay_slot,
                delay_slot_policy,
                link_register,
                ..
            } => {
                if *target_pc != vec.next_pc && vec.taken {
                    eprintln!(
                        "Vector {}: target_pc mismatch: expected 0x{:08X}, got 0x{:08X}",
                        idx, vec.next_pc, target_pc
                    );
                    failed += 1;
                    continue;
                }
                if *fallthrough_pc != vec.pc.wrapping_add(8) {
                    eprintln!("Vector {}: fallthrough_pc mismatch", idx);
                    failed += 1;
                    continue;
                }
                if *condition != expected_condition(vec) {
                    eprintln!(
                        "Vector {}: condition mismatch: expected {:?}, got {:?}",
                        idx,
                        expected_condition(vec),
                        condition
                    );
                    failed += 1;
                    continue;
                }
                if delay_slot.pc != vec.pc.wrapping_add(4) {
                    eprintln!(
                        "Vector {}: delay slot PC mismatch: expected 0x{:08X}, got 0x{:08X}",
                        idx,
                        vec.pc.wrapping_add(4),
                        delay_slot.pc
                    );
                    failed += 1;
                    continue;
                }

                let expected_annul = matches!(
                    vec.kind,
                    BranchKind::BEQL
                        | BranchKind::BNEL
                        | BranchKind::BLEZL
                        | BranchKind::BGTZL
                        | BranchKind::BLTZL
                        | BranchKind::BGEZL
                        | BranchKind::BLTZALL
                        | BranchKind::BGEZALL
                        | BranchKind::BC1FL
                        | BranchKind::BC1TL
                );
                if expected_annul
                    != matches!(
                        *delay_slot_policy,
                        analysis::DelaySlotPolicy::AnnulWhenNotTaken
                    )
                {
                    eprintln!("Vector {}: delay_slot_policy mismatch", idx);
                    failed += 1;
                    continue;
                }

                if vec.taken {
                    if !vec.delay_slot_executes {
                        eprintln!(
                            "Vector {}: expected delay slot to execute on taken path",
                            idx
                        );
                        failed += 1;
                        continue;
                    }
                } else {
                    if expected_annul {
                        if vec.delay_slot_executes {
                            eprintln!(
                                "Vector {}: expected delay slot to be annulled on not-taken branch-likely",
                                idx
                            );
                            failed += 1;
                            continue;
                        }
                    } else {
                        if !vec.delay_slot_executes {
                            eprintln!(
                                "Vector {}: expected delay slot to execute on not-taken regular branch",
                                idx
                            );
                            failed += 1;
                            continue;
                        }
                    }
                }

                if expected_link(vec) != *link_register {
                    eprintln!(
                        "Vector {}: link_register mismatch: expected {:?}, got {:?}",
                        idx,
                        expected_link(vec),
                        link_register
                    );
                    failed += 1;
                    continue;
                }
            }
            Terminator::DirectJump {
                target_pc,
                delay_slot,
                link_register,
                return_pc,
                ..
            } => {
                if *target_pc != vec.next_pc {
                    eprintln!(
                        "Vector {}: DirectJump target_pc mismatch: expected 0x{:08X}, got 0x{:08X}",
                        idx, vec.next_pc, target_pc
                    );
                    failed += 1;
                    continue;
                }
                if expected_link(vec) != *link_register {
                    eprintln!("Vector {}: DirectJump link_register mismatch", idx);
                    failed += 1;
                    continue;
                }
                let expected_return_pc = if matches!(vec.kind, BranchKind::JAL) {
                    Some(vec.pc.wrapping_add(8))
                } else {
                    None
                };
                if *return_pc != expected_return_pc {
                    eprintln!(
                        "Vector {}: DirectJump return_pc mismatch: expected {:?}, got {:?}",
                        idx, expected_return_pc, return_pc
                    );
                    failed += 1;
                    continue;
                }
                if delay_slot.pc != vec.pc.wrapping_add(4) {
                    eprintln!("Vector {}: DirectJump delay slot PC mismatch", idx);
                    failed += 1;
                    continue;
                }
            }
            Terminator::IndirectJump {
                source_register,
                delay_slot,
                link_register,
                ..
            } => {
                // JR $ra (rs=31) must be classified as Return, not IndirectJump.
                if matches!(vec.kind, BranchKind::JR) && vec.rs_index == 31 {
                    eprintln!(
                        "Vector {}: JR $ra produced IndirectJump instead of Return",
                        idx
                    );
                    failed += 1;
                    continue;
                }
                if *source_register != vec.rs_index {
                    eprintln!(
                        "Vector {}: IndirectJump source_register mismatch: expected {}, got {}",
                        idx, vec.rs_index, source_register
                    );
                    failed += 1;
                    continue;
                }
                if expected_link(vec) != *link_register {
                    eprintln!(
                        "Vector {}: IndirectJump link_register mismatch: expected {:?}, got {:?}",
                        idx,
                        expected_link(vec),
                        link_register
                    );
                    failed += 1;
                    continue;
                }
                if delay_slot.pc != vec.pc.wrapping_add(4) {
                    eprintln!("Vector {}: IndirectJump delay slot PC mismatch", idx);
                    failed += 1;
                    continue;
                }
            }
            Terminator::Return {
                source_register,
                delay_slot,
                ..
            } => {
                if !matches!(vec.kind, BranchKind::JR) || vec.rs_index != 31 {
                    eprintln!(
                        "Vector {}: unexpected Return terminator for kind {:?} rs={}",
                        idx, vec.kind, vec.rs_index
                    );
                    failed += 1;
                    continue;
                }
                if *source_register != 31 {
                    eprintln!("Vector {}: Return source_register mismatch", idx);
                    failed += 1;
                    continue;
                }
                if delay_slot.pc != vec.pc.wrapping_add(4) {
                    eprintln!("Vector {}: Return delay slot PC mismatch", idx);
                    failed += 1;
                    continue;
                }
            }
            other => {
                eprintln!("Vector {}: Unexpected terminator type: {:?}", idx, other);
                failed += 1;
                continue;
            }
        }

        passed += 1;
    }

    println!(
        "Branch differential test: {} passed, {} failed, {} skipped (PC outside mappable main RAM)",
        passed, failed, skipped
    );
    assert_eq!(failed, 0, "Some vectors failed differential verification");
}
