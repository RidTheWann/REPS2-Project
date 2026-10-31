//! Deterministic C++20 source generation for the verified scalar IR subset.

use crate::ir::{Gpr, Instruction, Operation, Program, VerifyError};

/// Generates one C++ translation unit exporting
/// reps2::generated::execute(EeState&).
pub fn emit_cpp(program: &Program) -> Result<String, VerifyError> {
    program.verify()?;

    let mut output = String::new();
    output.push_str(
        "#include <cstdint>\n\
         #include \"reps2/runtime/ee_state.hpp\"\n\n\
         namespace reps2::generated {\n\
         [[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept {\n\
             using reps2::runtime::Status;\n\
             using reps2::runtime::is_word_value;\n\
             using reps2::runtime::arithmetic_shift_right_word;\n\
             using reps2::runtime::sign_extend_immediate;\n\
             using reps2::runtime::sign_extend_word;\n\
             using reps2::runtime::sign_extend_word_signed;\n\
             state.gpr[0] = {};\n"
    );

    for (index, instruction) in program.instructions().iter().enumerate() {
        emit_instruction(&mut output, index, *instruction);
    }

    output.push_str("    return Status::ok;\n}\n} // namespace reps2::generated\n");
    Ok(output)
}

pub(crate) fn emit_instruction(output: &mut String, index: usize, instruction: Instruction) {
    let pc = instruction.guest_pc;
    let prefix = format!("    // guest PC 0x{pc:08X}\n");
    output.push_str(&prefix);

    match instruction.operation {
        Operation::Nop => output.push_str("    // nop\n"),
        Operation::AddWord { dst, lhs, rhs } => emit_word_add(
            output, index, dst, lhs, rhs, true
        ),
        Operation::AddImmediateWord { dst, src, immediate } => {
            emit_word_immediate_add(output, index, dst, src, immediate, true);
        }
        Operation::AddUnsignedWord { dst, lhs, rhs } => emit_word_add(
            output, index, dst, lhs, rhs, false
        ),
        Operation::AddImmediateUnsignedWord { dst, src, immediate } => {
            emit_word_immediate_add(output, index, dst, src, immediate, false);
        }
        Operation::SubWord { dst, lhs, rhs } => emit_word_sub(
            output, index, dst, lhs, rhs, true
        ),
        Operation::SubUnsignedWord { dst, lhs, rhs } => emit_word_sub(
            output, index, dst, lhs, rhs, false
        ),
        Operation::And64 { dst, lhs, rhs } => emit_binary64(output, dst, lhs, rhs, "&"),
        Operation::Or64 { dst, lhs, rhs } => emit_binary64(output, dst, lhs, rhs, "|"),
        Operation::Xor64 { dst, lhs, rhs } => emit_binary64(output, dst, lhs, rhs, "^"),
        Operation::AndImmediate64 { dst, src, immediate } => {
            emit_immediate64(output, dst, src, immediate, "&");
        }
        Operation::OrImmediate64 { dst, src, immediate } => {
            emit_immediate64(output, dst, src, immediate, "|");
        }
        Operation::XorImmediate64 { dst, src, immediate } => {
            emit_immediate64(output, dst, src, immediate, "^");
        }
        Operation::LoadUpperWord { dst, immediate } => {
            output.push_str(&format!(
                "    if ({} != 0) state.gpr[{}].low = sign_extend_word(static_cast<std::uint32_t>(0x{:04X}U) << 16U);\n",
                dst.index(), dst.index(), immediate
            ));
        }
        Operation::ShiftLeftWord { dst, src, amount } => {
            emit_shift(output, dst, src, amount, "<<", false);
        }
        Operation::ShiftRightLogicalWord { dst, src, amount } => {
            emit_shift(output, dst, src, amount, ">>", false);
        }
        Operation::ShiftRightArithmeticWord { dst, src, amount } => {
            emit_shift(output, dst, src, amount, "arithmetic", true);
        }
    }
}

fn emit_word_add(
    output: &mut String,
    index: usize,
    dst: Gpr,
    lhs: Gpr,
    rhs: Gpr,
    trap_overflow: bool,
) {
    let a = lhs.index();
    let b = rhs.index();
    let d = dst.index();
    let tag = format!("{index:04}");
    output.push_str(&format!(
        "    if (!is_word_value(state.gpr[{a}].low) || !is_word_value(state.gpr[{b}].low)) return Status::undefined_word_operand;\n"
    ));
    if trap_overflow {
        output.push_str(&format!(
            "    const std::int64_t lhs_{tag} = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[{a}].low));\n\
             \x20   const std::int64_t rhs_{tag} = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[{b}].low));\n\
             \x20   const std::int64_t result_{tag} = lhs_{tag} + rhs_{tag};\n\
             \x20   if (result_{tag} < -2147483648LL || result_{tag} > 2147483647LL) return Status::integer_overflow;\n\
             \x20   if ({d} != 0) state.gpr[{d}].low = sign_extend_word(static_cast<std::uint32_t>(result_{tag}));\n"
        ));
    } else {
        output.push_str(&format!(
            "    const std::uint32_t lhs_{tag} = static_cast<std::uint32_t>(state.gpr[{a}].low);\n\
             \x20   const std::uint32_t rhs_{tag} = static_cast<std::uint32_t>(state.gpr[{b}].low);\n\
             \x20   const std::uint32_t result_{tag} = lhs_{tag} + rhs_{tag};\n\
             \x20   if ({d} != 0) state.gpr[{d}].low = sign_extend_word(result_{tag});\n"
        ));
    }
}

fn emit_word_immediate_add(
    output: &mut String,
    index: usize,
    dst: Gpr,
    src: Gpr,
    immediate: u16,
    trap_overflow: bool,
) {
    let s = src.index();
    let d = dst.index();
    let tag = format!("{index:04}");
    if trap_overflow {
        output.push_str(&format!(
            "    if (!is_word_value(state.gpr[{s}].low)) return Status::undefined_word_operand;\n\
             \x20   const std::int64_t lhs_{tag} = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[{s}].low));\n\
             \x20   const std::int64_t rhs_{tag} = sign_extend_word_signed(sign_extend_immediate(0x{:04X}U));\n\
             \x20   const std::int64_t result_{tag} = lhs_{tag} + rhs_{tag};\n\
             \x20   if (result_{tag} < -2147483648LL || result_{tag} > 2147483647LL) return Status::integer_overflow;\n\
             \x20   if ({d} != 0) state.gpr[{d}].low = sign_extend_word(static_cast<std::uint32_t>(result_{tag}));\n",
            immediate
        ));
    } else {
        output.push_str(&format!(
            "    if (!is_word_value(state.gpr[{s}].low)) return Status::undefined_word_operand;\n\
             \x20   const std::uint32_t lhs_{tag} = static_cast<std::uint32_t>(state.gpr[{s}].low);\n\
             \x20   const std::uint32_t imm_{tag} = (0x{:04X}U & 0x8000U) != 0 ? (0x{:04X}U | 0xFFFF0000U) : 0x{:04X}U;\n\
             \x20   const std::uint32_t result_{tag} = lhs_{tag} + imm_{tag};\n\
             \x20   if ({d} != 0) state.gpr[{d}].low = sign_extend_word(result_{tag});\n",
            immediate, immediate, immediate
        ));
    }
}

fn emit_word_sub(
    output: &mut String,
    index: usize,
    dst: Gpr,
    lhs: Gpr,
    rhs: Gpr,
    trap_overflow: bool,
) {
    let a = lhs.index();
    let b = rhs.index();
    let d = dst.index();
    let tag = format!("{index:04}");
    output.push_str(&format!(
        "    if (!is_word_value(state.gpr[{a}].low) || !is_word_value(state.gpr[{b}].low)) return Status::undefined_word_operand;\n"
    ));
    if trap_overflow {
        output.push_str(&format!(
            "    const std::int64_t lhs_{tag} = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[{a}].low));\n\
             \x20   const std::int64_t rhs_{tag} = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[{b}].low));\n\
             \x20   const std::int64_t result_{tag} = lhs_{tag} - rhs_{tag};\n\
             \x20   if (result_{tag} < -2147483648LL || result_{tag} > 2147483647LL) return Status::integer_overflow;\n\
             \x20   if ({d} != 0) state.gpr[{d}].low = sign_extend_word(static_cast<std::uint32_t>(result_{tag}));\n"
        ));
    } else {
        output.push_str(&format!(
            "    const std::uint32_t lhs_{tag} = static_cast<std::uint32_t>(state.gpr[{a}].low);\n\
             \x20   const std::uint32_t rhs_{tag} = static_cast<std::uint32_t>(state.gpr[{b}].low);\n\
             \x20   const std::uint32_t result_{tag} = lhs_{tag} - rhs_{tag};\n\
             \x20   if ({d} != 0) state.gpr[{d}].low = sign_extend_word(result_{tag});\n"
        ));
    }
}

fn emit_binary64(output: &mut String, dst: Gpr, lhs: Gpr, rhs: Gpr, operator: &str) {
    let d = dst.index();
    let a = lhs.index();
    let b = rhs.index();
    output.push_str(&format!(
        "    if ({d} != 0) state.gpr[{d}].low = state.gpr[{a}].low {operator} state.gpr[{b}].low;\n"
    ));
}

fn emit_immediate64(output: &mut String, dst: Gpr, src: Gpr, immediate: u16, operator: &str) {
    let d = dst.index();
    let s = src.index();
    output.push_str(&format!(
        "    if ({d} != 0) state.gpr[{d}].low = state.gpr[{s}].low {operator} 0x{immediate:04X}ULL;\n"
    ));
}

fn emit_shift(output: &mut String, dst: Gpr, src: Gpr, amount: u8, operator: &str, arithmetic: bool) {
    let d = dst.index();
    let s = src.index();
    if arithmetic {
        output.push_str(&format!(
            "    if (!is_word_value(state.gpr[{s}].low)) return Status::undefined_word_operand;\n\
             \x20   if ({d} != 0) state.gpr[{d}].low = sign_extend_word(arithmetic_shift_right_word(static_cast<std::uint32_t>(state.gpr[{s}].low), {amount}U));\n"
        ));
    } else {
        output.push_str(&format!(
            "    if (!is_word_value(state.gpr[{s}].low)) return Status::undefined_word_operand;\n\
             \x20   if ({d} != 0) state.gpr[{d}].low = sign_extend_word(static_cast<std::uint32_t>(state.gpr[{s}].low) {operator} {amount}U);\n"
        ));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir;

    #[test]
    fn emits_deterministic_source_and_preserves_gpr_high_halves() {
        let decoded = [
            crate::mips::decode((0x09 << 26) | (8 << 16) | 5, 0x1000),
            crate::mips::decode((0x09 << 26) | (9 << 16) | 7, 0x1004),
            crate::mips::decode((8 << 21) | (9 << 16) | (10 << 11) | 0x21, 0x1008),
        ];
        let program = ir::lift(&decoded).expect("lift subset");
        let first = emit_cpp(&program).expect("emit C++");
        let second = emit_cpp(&program).expect("emit C++ again");
        assert_eq!(first, second);
        assert!(first.contains("is_word_value"));
        assert!(first.contains("state.gpr[10].low"));
        assert!(first.contains("sign_extend_word"));
    }
}
