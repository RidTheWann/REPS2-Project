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
        Operation::LoadByteSigned { dst, base, offset } => {
            emit_memory_load(output, index, dst, base, offset, 1, true);
        }
        Operation::LoadByteUnsigned { dst, base, offset } => {
            emit_memory_load(output, index, dst, base, offset, 1, false);
        }
        Operation::LoadHalfSigned { dst, base, offset } => {
            emit_memory_load(output, index, dst, base, offset, 2, true);
        }
        Operation::LoadHalfUnsigned { dst, base, offset } => {
            emit_memory_load(output, index, dst, base, offset, 2, false);
        }
        Operation::LoadWordSigned { dst, base, offset } => {
            emit_memory_load(output, index, dst, base, offset, 4, true);
        }
        Operation::LoadWordUnsigned { dst, base, offset } => {
            emit_memory_load(output, index, dst, base, offset, 4, false);
        }
        Operation::StoreByte { src, base, offset } => {
            emit_memory_store(output, index, src, base, offset, 1);
        }
        Operation::StoreHalf { src, base, offset } => {
            emit_memory_store(output, index, src, base, offset, 2);
        }
        Operation::StoreWord { src, base, offset } => {
            emit_memory_store(output, index, src, base, offset, 4);
        }
        Operation::LoadDoubleword { dst, base, offset } => {
            emit_memory_load(output, index, dst, base, offset, 8, false);
        }
        Operation::StoreDoubleword { src, base, offset } => {
            emit_memory_store(output, index, src, base, offset, 8);
        }
        Operation::LoadQuadword { dst, base, offset } => {
            emit_quadword_load(output, index, dst, base, offset);
        }
        Operation::StoreQuadword { src, base, offset } => {
            emit_quadword_store(output, index, src, base, offset);
        }
    }
}

fn emit_effective_address(output: &mut String, tag: &str, base: Gpr, offset: u16) {
    output.push_str(&format!(
        "    const std::uint32_t effective_address_{tag} = static_cast<std::uint32_t>(state.gpr[{}].low) + static_cast<std::uint32_t>(sign_extend_immediate(0x{offset:04X}U));\n",
        base.index()
    ));
}

fn emit_memory_load(
    output: &mut String,
    index: usize,
    dst: Gpr,
    base: Gpr,
    offset: u16,
    width: u8,
    signed: bool,
) {
    let tag = format!("{index:04}");
    let value_type = match width {
        1 => "std::uint8_t",
        2 => "std::uint16_t",
        4 => "std::uint32_t",
        8 => "std::uint64_t",
        _ => unreachable!("memory IR only permits byte, halfword, word and doubleword"),
    };
    let helper = match width {
        1 => "load_u8",
        2 => "load_u16",
        4 => "load_u32",
        8 => "load_u64",
        _ => unreachable!("memory IR only permits byte, halfword, word and doubleword"),
    };
    emit_effective_address(output, &tag, base, offset);
    output.push_str(&format!(
        "    {value_type} memory_value_{tag}{{}};\n    const auto memory_status_{tag} = {helper}(state.memory, effective_address_{tag}, memory_value_{tag});\n    if (memory_status_{tag} != Status::ok) return memory_status_{tag};\n"
    ));

    let value_expression = match (width, signed) {
        (1, true) => format!(
            "static_cast<std::uint64_t>(memory_value_{tag}) | ((memory_value_{tag} & 0x80U) != 0 ? 0xFFFFFFFFFFFFFF00ULL : 0ULL)"
        ),
        (1, false) | (2, false) | (4, false) => {
            format!("static_cast<std::uint64_t>(memory_value_{tag})")
        }
        (2, true) => format!(
            "static_cast<std::uint64_t>(memory_value_{tag}) | ((memory_value_{tag} & 0x8000U) != 0 ? 0xFFFFFFFFFFFF0000ULL : 0ULL)"
        ),
        (4, true) => format!("sign_extend_word(memory_value_{tag})"),
        (8, _) => format!("memory_value_{tag}"),
        _ => unreachable!("memory IR only permits supported load variants"),
    };
    if !dst.is_zero() {
        output.push_str(&format!(
            "    state.gpr[{}].low = {value_expression};\n",
            dst.index()
        ));
    }
}

fn emit_memory_store(
    output: &mut String,
    index: usize,
    src: Gpr,
    base: Gpr,
    offset: u16,
    width: u8,
) {
    let tag = format!("{index:04}");
    let (helper, value_type) = match width {
        1 => ("store_u8", "std::uint8_t"),
        2 => ("store_u16", "std::uint16_t"),
        4 => ("store_u32", "std::uint32_t"),
        8 => ("store_u64", "std::uint64_t"),
        _ => unreachable!("memory IR only permits byte, halfword, word and doubleword"),
    };
    emit_effective_address(output, &tag, base, offset);
    output.push_str(&format!(
        "    const auto memory_status_{tag} = {helper}(state.memory, effective_address_{tag}, static_cast<{value_type}>(state.gpr[{}].low));\n    if (memory_status_{tag} != Status::ok) return memory_status_{tag};\n",
        src.index()
    ));
}

fn emit_quadword_load(output: &mut String, index: usize, dst: Gpr, base: Gpr, offset: u16) {
    let tag = format!("{index:04}");
    emit_effective_address(output, &tag, base, offset);
    output.push_str(&format!(
        "    reps2::runtime::Gpr128 quadword_value_{tag}{{}};\n    const auto memory_status_{tag} = load_u128(state.memory, effective_address_{tag}, quadword_value_{tag});\n    if (memory_status_{tag} != Status::ok) return memory_status_{tag};\n"
    ));
    if !dst.is_zero() {
        output.push_str(&format!("    state.gpr[{}] = quadword_value_{tag};\n", dst.index()));
    }
}

fn emit_quadword_store(output: &mut String, index: usize, src: Gpr, base: Gpr, offset: u16) {
    let tag = format!("{index:04}");
    emit_effective_address(output, &tag, base, offset);
    output.push_str(&format!(
        "    const auto memory_status_{tag} = store_u128(state.memory, effective_address_{tag}, state.gpr[{}]);\n    if (memory_status_{tag} != Status::ok) return memory_status_{tag};\n",
        src.index()
    ));
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
    fn emits_checked_little_endian_memory_accesses_and_sign_extension() {
        let words = [
            (0x20 << 26) | (8 << 21) | (9 << 16) | 0xFFFC, // lb t1, -4(t0)
            (0x25 << 26) | (8 << 21) | (10 << 16) | 2, // lhu t2, 2(t0)
            (0x21 << 26) | (8 << 21) | (11 << 16) | 4, // lh t3, 4(t0)
            (0x23 << 26) | (8 << 21) | (12 << 16) | 8, // lw t4, 8(t0)
            (0x2B << 26) | (8 << 21) | (13 << 16) | 12, // sw t5, 12(t0)
            (0x1E << 26) | (8 << 21) | (14 << 16) | 4, // lq t6, 4(t0)
            (0x1F << 26) | (8 << 21) | (15 << 16) | 8, // sq t7, 8(t0)
            (0x37 << 26) | (8 << 21) | (16 << 16) | 4, // ld s0, 4(t0)
            (0x3F << 26) | (8 << 21) | (16 << 16) | 0x34, // sd s0, 0x34(t0)
        ];
        let decoded = words.iter().enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0x2000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded).expect("memory IR");
        let cpp = emit_cpp(&program).expect("memory C++");
        assert!(cpp.contains("load_u8(state.memory"));
        assert!(cpp.contains("load_u16(state.memory"));
        assert!(cpp.contains("load_u32(state.memory"));
        assert!(cpp.contains("store_u32(state.memory"));
        assert!(cpp.contains("load_u128(state.memory"));
        assert!(cpp.contains("store_u128(state.memory"));
        assert!(cpp.contains("load_u64(state.memory"));
        assert!(cpp.contains("store_u64(state.memory"));
        assert!(cpp.contains("state.gpr[14] = quadword_value_0005"));
        assert!(cpp.contains("state.gpr[16].low = memory_value_0007"));
        assert!(cpp.contains("0xFFFFFFFFFFFFFF00ULL"));
        assert!(cpp.contains("0xFFFFFFFFFFFF0000ULL"));
        assert!(cpp.contains("sign_extend_word(memory_value_0003)"));
        assert!(!cpp.contains("Status::address_error"));
        assert!(cpp.contains("memory_status_0000 != Status::ok"));
    }

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
