//! C++20 lowering for the verified CFG-aware control-flow IR.
//!
//! This backend emits a bounded dispatcher. Static targets inside the
//! translated graph continue natively; unresolved/dynamic targets are written
//! to EeState::pc and returned to the caller as external control flow.

use crate::control_ir::{ControlProgram, ControlTerminator, ControlError};

/// Emits a deterministic C++20 translation unit with execute_cfg().
pub fn emit_cpp(program: &ControlProgram) -> Result<String, ControlError> {
    program.verify()?;

    let mut output = String::new();
    output.push_str(
        "#include <bit>\n\
         #include <cstdint>\n\
         #include \"reps2/runtime/ee_state.hpp\"\n\n\
         namespace reps2::generated {\n\
         [[nodiscard]] reps2::runtime::Status execute_cfg(\n\
             reps2::runtime::EeState& state,\n\
             std::uint64_t instruction_budget\n\
         ) noexcept {\n\
             using reps2::runtime::Status;\n\
             using reps2::runtime::is_word_value;\n\
             using reps2::runtime::arithmetic_shift_right_word;\n\
             using reps2::runtime::sign_extend_immediate;\n\
             using reps2::runtime::sign_extend_word;\n\
             using reps2::runtime::sign_extend_word_signed;\n\
             using reps2::runtime::load_u8;\n\
             using reps2::runtime::load_u16;\n\
             using reps2::runtime::load_u32;\n\
             using reps2::runtime::load_u64;\n\
             using reps2::runtime::load_u128;\n\
             using reps2::runtime::store_u8;\n\
             using reps2::runtime::store_u16;\n\
             using reps2::runtime::store_u32;\n\
             using reps2::runtime::store_u64;\n\
             using reps2::runtime::store_u128;\n\
             using reps2::runtime::load_u32_left;\n\
             using reps2::runtime::load_u32_right;\n\
             using reps2::runtime::store_u32_left;\n\
             using reps2::runtime::store_u32_right;\n\
             using reps2::runtime::load_u64_left;\n\
             using reps2::runtime::load_u64_right;\n\
             using reps2::runtime::store_u64_left;\n\
             using reps2::runtime::store_u64_right;\n\
             std::uint64_t remaining = instruction_budget;\n\
             state.gpr[0] = {};\n\
             for (;;) {\n\
                 switch (state.pc) {\n"
    );

    for block in program.blocks.values() {
        let required = block.instructions.len() + block.terminator.instruction_count();
        output.push_str(&format!(
            "                 case 0x{:08X}U: {{\n\
             \x20        if (remaining < {}ULL) return Status::instruction_budget_exhausted;\n\
             \x20        remaining -= {}ULL;\n",
            block.start_pc, required, required
        ));

        for (instruction_index, instruction) in block.instructions.iter().enumerate() {
            output.push_str(&format!("                     state.pc = 0x{:08X}U;\n", instruction.guest_pc));
            crate::codegen::emit_instruction(&mut output, instruction_index, *instruction);
        }

        emit_terminator(&mut output, &block.terminator, block.instructions.len());
        output.push_str("                 }\n");
    }

    output.push_str(
        "                 default:\n\
             \x20            return Status::external_control_flow;\n\
             \x20    }\n\
             }\n\
         }\n\
         } // namespace reps2::generated\n"
    );
    Ok(output)
}

/// Emits the CFG dispatcher plus the ELF entry-point/RAM-image initializer.
/// The adjacent .ram.bin file is supplied by the host as a 32 MiB byte span.
pub fn emit_cpp_with_entry(
    program: &ControlProgram,
    guest_entry_pc: u32,
) -> Result<String, ControlError> {
    if !program.blocks.contains_key(&guest_entry_pc)
        || !program.entry_points.contains(&guest_entry_pc)
    {
        return Err(ControlError::InvalidEntryPoint(guest_entry_pc));
    }

    let mut output = emit_cpp(program)?;
    output.push_str(&format!(
        "\nnamespace reps2::generated {{\n\\
         [[nodiscard]] reps2::runtime::Status initialize_ee_state(\n\\
             reps2::runtime::EeState& state,\n\\
             std::span<const std::uint8_t> main_ram_image\n\\
         ) noexcept {{\n\\
             return reps2::runtime::initialize_ee_state(state, main_ram_image, 0x{guest_entry_pc:08X}U);\n\\
         }}\n\\
         }} // namespace reps2::generated\n"
    ));
    Ok(output)
}

fn emit_terminator(output: &mut String, terminator: &ControlTerminator, index: usize) {
    match terminator {
        ControlTerminator::Fallthrough { next_pc } => {
            output.push_str(&format!(
                "                     state.pc = 0x{next_pc:08X}U;\n\
                 \x20                    continue;\n"
            ));
        }
        ControlTerminator::ConditionalBranch {
            guest_pc,
            condition,
            target_pc,
            fallthrough_pc,
            link_register,
            delay_slot,
        } => {
            output.push_str(&format!(
                "                     state.pc = 0x{guest_pc:08X}U;\n\
                 \x20                    const bool branch_taken_{guest_pc:08X} = {};\n",
                condition_expression(*condition)
            ));
            emit_link_write(output, *link_register, guest_pc.wrapping_add(8));
            emit_delay_slot(output, index, delay_slot);
            output.push_str(&format!(
                "                     state.pc = branch_taken_{guest_pc:08X} ? 0x{target_pc:08X}U : 0x{fallthrough_pc:08X}U;\n\
                 \x20                    continue;\n"
            ));
        }
        ControlTerminator::DirectJump {
            guest_pc,
            target_pc,
            link_register,
            return_pc,
            delay_slot,
        } => {
            output.push_str(&format!("                     state.pc = 0x{guest_pc:08X}U;\n"));
            emit_link_write(output, *link_register, return_pc.unwrap_or(guest_pc.wrapping_add(8)));
            emit_delay_slot(output, index, delay_slot);
            output.push_str(&format!(
                "                     state.pc = 0x{target_pc:08X}U;\n\
                 \x20                    continue;\n"
            ));
        }
        ControlTerminator::IndirectJump {
            guest_pc,
            source_register,
            link_register,
            return_pc,
            delay_slot,
        } => {
            // Capture the dynamic target before a potentially aliased link write.
            output.push_str(&format!(
                "                     state.pc = 0x{guest_pc:08X}U;\n\
                 \x20                    const std::uint32_t dynamic_target_{guest_pc:08X} = static_cast<std::uint32_t>(state.gpr[{source_register}].low);\n"
            ));
            emit_link_write(output, *link_register, return_pc.unwrap_or(guest_pc.wrapping_add(8)));
            emit_delay_slot(output, index, delay_slot);
            output.push_str(&format!(
                "                     state.pc = dynamic_target_{guest_pc:08X};\n\
                 \x20                    continue;\n"
            ));
        }
        ControlTerminator::Return {
            guest_pc,
            source_register,
            delay_slot,
        } => {
            output.push_str(&format!(
                "                     state.pc = 0x{guest_pc:08X}U;\n\
                 \x20                    const std::uint32_t return_target_{guest_pc:08X} = static_cast<std::uint32_t>(state.gpr[{source_register}].low);\n"
            ));
            emit_delay_slot(output, index, delay_slot);
            output.push_str(&format!(
                "                     state.pc = return_target_{guest_pc:08X};\n\
                 \x20                    continue;\n"
            ));
        }
        ControlTerminator::Unsupported { guest_pc, .. } => {
            output.push_str(&format!(
                "                     state.pc = 0x{guest_pc:08X}U;\n\
                 \x20                    return Status::unsupported_instruction;\n"
            ));
        }
        ControlTerminator::SegmentEnd { next_pc } => {
            output.push_str(&format!(
                "                     state.pc = 0x{next_pc:08X}U;\n\
                 \x20                    return Status::external_control_flow;\n"
            ));
        }
    }
}

fn emit_link_write(output: &mut String, register: Option<u8>, return_pc: u32) {
    if let Some(register) = register
        && register != 0
    {
        output.push_str(&format!(
            "                     state.gpr[{register}].low = static_cast<std::uint64_t>(0x{return_pc:08X}U);\n"
        ));
    }
}

fn emit_delay_slot(
    output: &mut String,
    index: usize,
    delay_slot: &crate::ir::Instruction,
) {
    output.push_str(&format!(
        "                     state.pc = 0x{:08X}U;\n",
        delay_slot.guest_pc
    ));
    crate::codegen::emit_instruction(output, index, *delay_slot);
}

fn condition_expression(condition: crate::analysis::BranchCondition) -> String {
    use crate::analysis::BranchCondition;

    match condition {
        BranchCondition::Equal { lhs, rhs } => {
            format!("state.gpr[{lhs}].low == state.gpr[{rhs}].low")
        }
        BranchCondition::NotEqual { lhs, rhs } => {
            format!("state.gpr[{lhs}].low != state.gpr[{rhs}].low")
        }
        BranchCondition::LessThanOrEqualZero { register } => {
            format!("std::bit_cast<std::int64_t>(state.gpr[{register}].low) <= 0")
        }
        BranchCondition::GreaterThanZero { register } => {
            format!("std::bit_cast<std::int64_t>(state.gpr[{register}].low) > 0")
        }
        BranchCondition::LessThanZero { register } => {
            format!("std::bit_cast<std::int64_t>(state.gpr[{register}].low) < 0")
        }
        BranchCondition::GreaterThanOrEqualZero { register } => {
            format!("std::bit_cast<std::int64_t>(state.gpr[{register}].low) >= 0")
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analysis::{self, AnalysisLimits, EntryPoint};
    use crate::elf::LoadedSegment;

    fn segment(base: u32, words: &[u32]) -> LoadedSegment {
        let mut bytes = Vec::with_capacity(words.len() * 4);
        for word in words {
            bytes.extend_from_slice(&word.to_le_bytes());
        }
        let file_size = bytes.len();
        LoadedSegment { virtual_address: base, flags: 1, file_size, bytes }
    }

    fn entry(address: u32) -> EntryPoint {
        EntryPoint { address, labels: vec!["codegen-test".to_owned()], is_elf_entry: true }
    }

    #[test]
    fn emits_branch_test_before_link_and_delay_slot_before_dispatch() {
        let words = [
            (0x01 << 26) | (8 << 21) | (0x10 << 16) | 3, // bltzal t0, 0x1010
            0x2409_0007, // delay slot
            0x03E0_0008,
            0x0000_0000, // delay slot
            0x240A_0001,
            0x03E0_0008,
            0x0000_0000,
        ];
        let segments = [segment(0x1000, &words)];
        let roots = [0x1000];
        let graph = analysis::discover_cfg(&segments, vec![entry(0x1000)], &roots, AnalysisLimits::default())
            .expect("synthetic branch CFG");
        let program = crate::control_ir::ControlProgram::lift(&graph).expect("control IR");
        let cpp = emit_cpp(&program).expect("C++ output");

        let condition = cpp.find("const bool branch_taken_00001000").expect("branch condition");
        let link = cpp.find("state.gpr[31].low").expect("link write");
        let delay = cpp.find("state.pc = 0x00001004U").expect("delay slot");
        let dispatch = cpp.find("state.pc = branch_taken_00001000").expect("branch dispatch");
        assert!(condition < link && link < delay && delay < dispatch);
        assert!(cpp.contains("0x00001008U"));
        assert!(cpp.contains("0x0000100CU"));
    }

    #[test]
    fn adds_runtime_initializer_for_the_selected_elf_entry() {
        let words = [
            0x2408_0005, // addiu t0, zero, 5
            0x03E0_0008, // jr ra
            0x0000_0000, // delay slot
        ];
        let segments = [segment(0x1000, &words)];
        let roots = [0x1000];
        let graph = analysis::discover_cfg(
            &segments,
            vec![entry(0x1000)],
            &roots,
            AnalysisLimits::default(),
        ).expect("synthetic CFG");
        let program = crate::control_ir::ControlProgram::lift(&graph).expect("control IR");
        let cpp = emit_cpp_with_entry(&program, 0x1000).expect("initialized C++ module");

        assert!(cpp.contains("initialize_ee_state"));
        assert!(cpp.contains("main_ram_image"));
        assert!(cpp.contains("0x00001000U"));
        assert!(emit_cpp_with_entry(&program, 0x2000).is_err());
    }

    #[test]
    fn captures_jalr_target_before_aliased_link_write() {
        let words = [
            (8 << 21) | (8 << 11) | 0x09, // jalr t0, t0
            0x2409_0007,
        ];
        let segments = [segment(0x2000, &words)];
        let roots = [0x2000];
        let graph = analysis::discover_cfg(&segments, vec![entry(0x2000)], &roots, AnalysisLimits::default())
            .expect("synthetic jalr CFG");
        let program = crate::control_ir::ControlProgram::lift(&graph).expect("control IR");
        let cpp = emit_cpp(&program).expect("C++ output");

        let capture = cpp.find("dynamic_target_00002000").expect("dynamic target capture");
        let link = cpp.find("state.gpr[8].low = static_cast").expect("aliased link write");
        let delay = cpp.find("state.pc = 0x00002004U").expect("delay slot");
        assert!(capture < link && link < delay);
    }
}
