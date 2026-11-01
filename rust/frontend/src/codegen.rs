// SPDX-License-Identifier: GPL-3.0-or-later
//! Deterministic C++20 source generation for the verified scalar IR subset.

use crate::ir::{
    Fpr, FpuComparison, FpuControl, Gpr, Instruction, Operation, Program, VerifyError,
};

/// Generates one C++ translation unit exporting
/// reps2::generated::execute(EeState&).
pub fn emit_cpp(program: &Program) -> Result<String, VerifyError> {
    program.verify()?;

    let mut output = String::new();
    output.push_str(
        "#include <bit>\n\
         #include <cstdint>\n\
         #include <limits>\n\
         #include \"reps2/runtime/ee_state.hpp\"\n\n\
         namespace reps2::generated {\n\
         [[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept {\n\
             using reps2::runtime::Status;\n\
             using reps2::runtime::is_word_value;\n\
             using reps2::runtime::arithmetic_shift_right_word;\n\
             using reps2::runtime::arithmetic_shift_right_doubleword;\n\
             using reps2::runtime::sign_extend_immediate;\n\
             using reps2::runtime::sign_extend_word;\n\
             using reps2::runtime::sign_extend_word_signed;\n\
             using reps2::runtime::parallel_add_u32;\n\
             using reps2::runtime::funnel_shift_right_vec;\n\
             using reps2::runtime::store_fpu_control;\n\
             using reps2::runtime::apply_fpu_flags;\n\
             using reps2::runtime::apply_fpu_condition;\n\
             using reps2::runtime::fpu_compare;\n\
             using reps2::runtime::FpuCompare;\n\
             using reps2::runtime::fpu_add;\n\
             using reps2::runtime::fpu_sub;\n\
             using reps2::runtime::fpu_mul;\n\
             using reps2::runtime::fpu_div;\n\
             using reps2::runtime::fpu_sqrt;\n\
             using reps2::runtime::fpu_rsqrt;\n\
             using reps2::runtime::fpu_abs;\n\
             using reps2::runtime::fpu_neg;\n\
             using reps2::runtime::fpu_mov;\n\
             using reps2::runtime::fpu_max;\n\
             using reps2::runtime::fpu_min;\n\
             using reps2::runtime::fpu_cvt_w_s;\n\
             using reps2::runtime::fpu_cvt_s_w;\n\
             using reps2::runtime::fpu_madd;\n\
             using reps2::runtime::fpu_msub;\n\
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
             using reps2::runtime::read_cop0;\n\
             using reps2::runtime::write_cop0;\n\
             using reps2::runtime::enable_interrupts;\n\
             using reps2::runtime::disable_interrupts;\n\
             state.gpr[0] = {};\n",
    );

    for (index, instruction) in program.instructions().iter().enumerate() {
        output.push_str(&format!(
            "    state.pc = 0x{:08X}U;\n",
            instruction.guest_pc
        ));
        emit_instruction(&mut output, index, *instruction, false);
    }

    output.push_str("    return Status::ok;\n}\n} // namespace reps2::generated\n");
    Ok(output)
}

pub(crate) fn emit_instruction(
    output: &mut String,
    index: usize,
    instruction: Instruction,
    in_branch_delay: bool,
) {
    let pc = instruction.guest_pc;
    let prefix = format!("    // guest PC 0x{pc:08X}\n");
    output.push_str(&prefix);

    match instruction.operation {
        Operation::Nop => output.push_str("    // nop\n"),
        Operation::MoveFromHi { dst } => emit_move_from_hilo(output, dst, "hi"),
        Operation::MoveFromLo { dst } => emit_move_from_hilo(output, dst, "lo"),
        Operation::MoveToHi { src } => emit_move_to_hilo(output, src, "hi"),
        Operation::MoveToLo { src } => emit_move_to_hilo(output, src, "lo"),
        Operation::MoveFromHi1 { dst } => emit_move_from_hilo(output, dst, "hi1"),
        Operation::MoveFromLo1 { dst } => emit_move_from_hilo(output, dst, "lo1"),
        Operation::MoveToHi1 { src } => emit_move_to_hilo(output, src, "hi1"),
        Operation::MoveToLo1 { src } => emit_move_to_hilo(output, src, "lo1"),
        Operation::MoveFromSa { dst } => {
            let d = dst.index();
            output.push_str(&format!(
                "    if ({d} != 0) state.gpr[{d}].low = state.sa;\n"
            ));
        }
        Operation::MoveToSa { src } => {
            let s = src.index();
            output.push_str(&format!("    state.sa = state.gpr[{s}].low;\n"));
        }
        Operation::MoveToSaByte { src, immediate } => {
            let s = src.index();
            let imm = immediate & 0x0F;
            output.push_str(&format!(
                "    state.sa = ((state.gpr[{s}].low & 0x0FU) ^ {imm:#04X}U) * 8U;\n"
            ));
        }
        Operation::MoveToSaHalfword { src, immediate } => {
            let s = src.index();
            let imm = immediate & 0x07;
            output.push_str(&format!(
                "    state.sa = ((state.gpr[{s}].low & 0x07U) ^ {imm:#04X}U) * 16U;\n"
            ));
        }
        Operation::FunnelShiftRightVec { dst, lhs, rhs } => {
            let d = dst.index();
            let a = lhs.index();
            let b = rhs.index();
            output.push_str(&format!(
                "    if ({d} != 0) state.gpr[{d}] = funnel_shift_right_vec(state.gpr[{a}], state.gpr[{b}], state.sa);\n"
            ));
        }
        Operation::MoveFromFpuRegister { dst, src } => {
            let d = dst.index();
            // MFC1 sign-extends the 32-bit FPU result into GPR[63:0] and leaves
            // the upper 64-bit lane untouched.
            output.push_str(&format!(
                "    if ({d} != 0) state.gpr[{d}].low = sign_extend_word(state.fpr[{}]);\n",
                src.index()
            ));
        }
        Operation::MoveToFpuRegister { dst, src } => {
            let (d, s) = (dst.index(), src.index());
            output.push_str(&format!(
                "    state.fpr[{d}] = static_cast<std::uint32_t>(state.gpr[{s}].low);\n"
            ));
        }
        Operation::MoveFromFpuControl { dst, register } => {
            let d = dst.index();
            let source = match register {
                FpuControl::Fir => "reps2::runtime::kFpuImplementationRegister".to_owned(),
                FpuControl::Fcsr => "state.fcsr".to_owned(),
            };
            output.push_str(&format!(
                "    if ({d} != 0) state.gpr[{d}].low = sign_extend_word({source});\n"
            ));
        }
        Operation::MoveToFpuControl { src, register } => {
            let s = src.index();
            // Only FCSR is writable. This emitter is infallible, so the
            // guarantee lives in lifting: ir.rs maps CTC1 solely to
            // FpuControl::Fcsr and rejects every other control number, which
            // its unit test pins. The assertion guards against a later lift
            // widening the mapping.
            debug_assert!(matches!(register, FpuControl::Fcsr));
            output.push_str(&format!(
                "    store_fpu_control(state.fcsr, static_cast<std::uint32_t>(state.gpr[{s}].low));\n"
            ));
        }
        Operation::LoadFpuWord { dst, base, offset } => {
            let tag = format!("{index:04}");
            emit_effective_address(output, &tag, base, offset);
            output.push_str(&format!(
                "    std::uint32_t memory_value_{tag}{{}};\n    const auto memory_status_{tag} = load_u32(state.memory, effective_address_{tag}, memory_value_{tag});\n"
            ));
            emit_memory_status_check(output, &tag, false, in_branch_delay);
            output.push_str(&format!(
                "    state.fpr[{}] = memory_value_{tag};\n",
                dst.index()
            ));
        }
        Operation::StoreFpuWord { src, base, offset } => {
            let tag = format!("{index:04}");
            emit_effective_address(output, &tag, base, offset);
            output.push_str(&format!(
                "    const auto memory_status_{tag} = store_u32(state.memory, effective_address_{tag}, state.fpr[{}]);\n",
                src.index()
            ));
            emit_memory_status_check(output, &tag, true, in_branch_delay);
        }
        Operation::MoveFromCop0 { dst, cop0_reg } => {
            let d = dst.index();
            output.push_str(&format!(
                "    if ({d} != 0) state.gpr[{d}].low = sign_extend_word(read_cop0(state, {cop0_reg}));\n"
            ));
        }
        Operation::MoveToCop0 { src, cop0_reg } => {
            let s = src.index();
            output.push_str(&format!(
                "    write_cop0(state, {cop0_reg}, static_cast<std::uint32_t>(state.gpr[{s}].low));\n"
            ));
        }
        Operation::EnableInterrupts => {
            output.push_str("    enable_interrupts(state);\n");
        }
        Operation::DisableInterrupts => {
            output.push_str("    disable_interrupts(state);\n");
        }
        Operation::FpuAddSingle { dst, lhs, rhs } => {
            emit_fpu_binary(output, index, dst, lhs, rhs, "fpu_add");
        }
        Operation::FpuSubtractSingle { dst, lhs, rhs } => {
            emit_fpu_binary(output, index, dst, lhs, rhs, "fpu_sub");
        }
        Operation::FpuMultiplySingle { dst, lhs, rhs } => {
            emit_fpu_binary(output, index, dst, lhs, rhs, "fpu_mul");
        }
        Operation::FpuDivideSingle { dst, lhs, rhs } => {
            emit_fpu_binary(output, index, dst, lhs, rhs, "fpu_div");
        }
        Operation::FpuReciprocalSquareRootSingle { dst, lhs, rhs } => {
            emit_fpu_binary(output, index, dst, lhs, rhs, "fpu_rsqrt");
        }
        Operation::FpuMaximumSingle { dst, lhs, rhs } => {
            emit_fpu_binary(output, index, dst, lhs, rhs, "fpu_max");
        }
        Operation::FpuMinimumSingle { dst, lhs, rhs } => {
            emit_fpu_binary(output, index, dst, lhs, rhs, "fpu_min");
        }
        Operation::FpuSquareRootSingle { dst, src } => {
            emit_fpu_unary(output, index, dst, src, "fpu_sqrt");
        }
        Operation::FpuAbsoluteSingle { dst, src } => {
            emit_fpu_unary(output, index, dst, src, "fpu_abs");
        }
        Operation::FpuMoveSingle { dst, src } => {
            emit_fpu_unary(output, index, dst, src, "fpu_mov");
        }
        Operation::FpuNegateSingle { dst, src } => {
            emit_fpu_unary(output, index, dst, src, "fpu_neg");
        }
        Operation::FpuConvertSingleToWord { dst, src } => {
            emit_fpu_unary(output, index, dst, src, "fpu_cvt_w_s");
        }
        Operation::FpuConvertWordToSingle { dst, src } => {
            emit_fpu_unary(output, index, dst, src, "fpu_cvt_s_w");
        }
        Operation::FpuCompareSingle {
            lhs,
            rhs,
            comparison,
        } => {
            let comparison = match comparison {
                FpuComparison::False => "FpuCompare::False",
                FpuComparison::Equal => "FpuCompare::Equal",
                FpuComparison::LessThan => "FpuCompare::LessThan",
                FpuComparison::LessThanOrEqual => "FpuCompare::LessThanOrEqual",
            };
            output.push_str(&format!(
                "    state.fcsr = apply_fpu_condition(state.fcsr, fpu_compare({comparison}, state.fpr[{}], state.fpr[{}]));\n",
                lhs.index(),
                rhs.index()
            ));
        }
        Operation::FpuAddAccumulateSingle { lhs, rhs } => {
            emit_fpu_binary_to_acc(output, index, lhs, rhs, "fpu_add");
        }
        Operation::FpuSubtractAccumulateSingle { lhs, rhs } => {
            emit_fpu_binary_to_acc(output, index, lhs, rhs, "fpu_sub");
        }
        Operation::FpuMultiplyAccumulateSingle { lhs, rhs } => {
            emit_fpu_binary_to_acc(output, index, lhs, rhs, "fpu_mul");
        }
        Operation::FpuMultiplyAddSingle { dst, lhs, rhs } => {
            emit_fpu_madd(output, index, Some(dst), lhs, rhs, "fpu_madd");
        }
        Operation::FpuMultiplySubtractSingle { dst, lhs, rhs } => {
            emit_fpu_madd(output, index, Some(dst), lhs, rhs, "fpu_msub");
        }
        Operation::FpuMultiplyAddAccumulateSingle { lhs, rhs } => {
            emit_fpu_madd(output, index, None, lhs, rhs, "fpu_madd");
        }
        Operation::FpuMultiplySubtractAccumulateSingle { lhs, rhs } => {
            emit_fpu_madd(output, index, None, lhs, rhs, "fpu_msub");
        }
        Operation::MultiplyWordSigned { dst, lhs, rhs } => {
            emit_multiply_word(output, index, dst, lhs, rhs, true)
        }
        Operation::MultiplyWordUnsigned { dst, lhs, rhs } => {
            emit_multiply_word(output, index, dst, lhs, rhs, false)
        }
        Operation::DivideWordSigned { lhs, rhs } => emit_divide_word(output, index, lhs, rhs, true),
        Operation::DivideWordUnsigned { lhs, rhs } => {
            emit_divide_word(output, index, lhs, rhs, false)
        }
        Operation::MoveConditionalNotZero { dst, src, test } => {
            emit_move_conditional(output, dst, src, test, false)
        }
        Operation::MoveConditionalZero { dst, src, test } => {
            emit_move_conditional(output, dst, src, test, true)
        }
        Operation::Nor64 { dst, lhs, rhs } => emit_binary64_not_or(output, dst, lhs, rhs),
        Operation::SetLessThanSigned64 { dst, lhs, rhs } => {
            emit_set_less_than(output, dst, lhs, rhs, true);
        }
        Operation::SetLessThanUnsigned64 { dst, lhs, rhs } => {
            emit_set_less_than(output, dst, lhs, rhs, false);
        }
        Operation::SetLessThanImmediateSigned64 {
            dst,
            src,
            immediate,
        } => {
            emit_set_less_than_immediate(output, dst, src, immediate, true);
        }
        Operation::SetLessThanImmediateUnsigned64 {
            dst,
            src,
            immediate,
        } => {
            emit_set_less_than_immediate(output, dst, src, immediate, false);
        }
        Operation::AddWord { dst, lhs, rhs } => {
            emit_word_add(output, index, dst, lhs, rhs, true, in_branch_delay)
        }
        Operation::AddImmediateWord {
            dst,
            src,
            immediate,
        } => {
            emit_word_immediate_add(output, index, dst, src, immediate, true, in_branch_delay);
        }
        Operation::AddUnsignedWord { dst, lhs, rhs } => {
            emit_word_add(output, index, dst, lhs, rhs, false, in_branch_delay)
        }
        Operation::AddImmediateUnsignedWord {
            dst,
            src,
            immediate,
        } => {
            emit_word_immediate_add(output, index, dst, src, immediate, false, in_branch_delay);
        }
        Operation::SubWord { dst, lhs, rhs } => {
            emit_word_sub(output, index, dst, lhs, rhs, true, in_branch_delay)
        }
        Operation::SubUnsignedWord { dst, lhs, rhs } => {
            emit_word_sub(output, index, dst, lhs, rhs, false, in_branch_delay)
        }
        Operation::ParallelAddUnsignedWord { dst, lhs, rhs } => {
            emit_parallel_add_u32(output, dst, lhs, rhs);
        }
        Operation::AddDoublewordSigned { dst, lhs, rhs } => {
            emit_doubleword_signed_add(output, index, dst, lhs, rhs, in_branch_delay);
        }
        Operation::SubDoublewordSigned { dst, lhs, rhs } => {
            emit_doubleword_signed_sub(output, index, dst, lhs, rhs, in_branch_delay);
        }
        Operation::AddImmediateDoublewordSigned {
            dst,
            src,
            immediate,
        } => {
            emit_doubleword_signed_immediate_add(
                output,
                index,
                dst,
                src,
                immediate,
                in_branch_delay,
            );
        }
        Operation::AddDoublewordUnsigned { dst, lhs, rhs } => {
            emit_binary64(output, dst, lhs, rhs, "+")
        }
        Operation::SubDoublewordUnsigned { dst, lhs, rhs } => {
            emit_binary64(output, dst, lhs, rhs, "-")
        }
        Operation::AddImmediateDoublewordUnsigned {
            dst,
            src,
            immediate,
        } => {
            emit_doubleword_immediate_add(output, dst, src, immediate);
        }
        Operation::ShiftLeftDoubleword { dst, src, amount } => {
            emit_doubleword_shift(output, dst, src, amount, "<<", false);
        }
        Operation::ShiftRightLogicalDoubleword { dst, src, amount } => {
            emit_doubleword_shift(output, dst, src, amount, ">>", false);
        }
        Operation::ShiftRightArithmeticDoubleword { dst, src, amount } => {
            emit_doubleword_shift(output, dst, src, amount, "arithmetic", true);
        }
        Operation::ShiftLeftWordVariable { dst, src, amount } => {
            emit_variable_word_shift(output, index, dst, src, amount, "<<", false);
        }
        Operation::ShiftRightLogicalWordVariable { dst, src, amount } => {
            emit_variable_word_shift(output, index, dst, src, amount, ">>", false);
        }
        Operation::ShiftRightArithmeticWordVariable { dst, src, amount } => {
            emit_variable_word_shift(output, index, dst, src, amount, "arithmetic", true);
        }
        Operation::ShiftLeftDoublewordVariable { dst, src, amount } => {
            emit_variable_doubleword_shift(output, index, dst, src, amount, "<<", false);
        }
        Operation::ShiftRightLogicalDoublewordVariable { dst, src, amount } => {
            emit_variable_doubleword_shift(output, index, dst, src, amount, ">>", false);
        }
        Operation::ShiftRightArithmeticDoublewordVariable { dst, src, amount } => {
            emit_variable_doubleword_shift(output, index, dst, src, amount, "arithmetic", true);
        }
        Operation::And64 { dst, lhs, rhs } => emit_binary64(output, dst, lhs, rhs, "&"),
        Operation::Or64 { dst, lhs, rhs } => emit_binary64(output, dst, lhs, rhs, "|"),
        Operation::Xor64 { dst, lhs, rhs } => emit_binary64(output, dst, lhs, rhs, "^"),
        Operation::AndImmediate64 {
            dst,
            src,
            immediate,
        } => {
            emit_immediate64(output, dst, src, immediate, "&");
        }
        Operation::OrImmediate64 {
            dst,
            src,
            immediate,
        } => {
            emit_immediate64(output, dst, src, immediate, "|");
        }
        Operation::XorImmediate64 {
            dst,
            src,
            immediate,
        } => {
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
            emit_memory_load(
                output,
                index,
                dst,
                base,
                offset,
                MemoryLoadSpec {
                    width: 1,
                    signed: true,
                },
                in_branch_delay,
            );
        }
        Operation::LoadByteUnsigned { dst, base, offset } => {
            emit_memory_load(
                output,
                index,
                dst,
                base,
                offset,
                MemoryLoadSpec {
                    width: 1,
                    signed: false,
                },
                in_branch_delay,
            );
        }
        Operation::LoadHalfSigned { dst, base, offset } => {
            emit_memory_load(
                output,
                index,
                dst,
                base,
                offset,
                MemoryLoadSpec {
                    width: 2,
                    signed: true,
                },
                in_branch_delay,
            );
        }
        Operation::LoadHalfUnsigned { dst, base, offset } => {
            emit_memory_load(
                output,
                index,
                dst,
                base,
                offset,
                MemoryLoadSpec {
                    width: 2,
                    signed: false,
                },
                in_branch_delay,
            );
        }
        Operation::LoadWordSigned { dst, base, offset } => {
            emit_memory_load(
                output,
                index,
                dst,
                base,
                offset,
                MemoryLoadSpec {
                    width: 4,
                    signed: true,
                },
                in_branch_delay,
            );
        }
        Operation::LoadWordUnsigned { dst, base, offset } => {
            emit_memory_load(
                output,
                index,
                dst,
                base,
                offset,
                MemoryLoadSpec {
                    width: 4,
                    signed: false,
                },
                in_branch_delay,
            );
        }
        Operation::StoreByte { src, base, offset } => {
            emit_memory_store(output, index, src, base, offset, 1, in_branch_delay);
        }
        Operation::StoreHalf { src, base, offset } => {
            emit_memory_store(output, index, src, base, offset, 2, in_branch_delay);
        }
        Operation::StoreWord { src, base, offset } => {
            emit_memory_store(output, index, src, base, offset, 4, in_branch_delay);
        }
        Operation::LoadWordLeft { dst, base, offset } => {
            emit_partial_memory_load(output, index, dst, base, offset, "load_u32_left");
        }
        Operation::LoadWordRight { dst, base, offset } => {
            emit_partial_memory_load(output, index, dst, base, offset, "load_u32_right");
        }
        Operation::StoreWordLeft { src, base, offset } => {
            emit_partial_memory_store(
                output,
                index,
                src,
                base,
                offset,
                "store_u32_left",
                "std::uint32_t",
            );
        }
        Operation::StoreWordRight { src, base, offset } => {
            emit_partial_memory_store(
                output,
                index,
                src,
                base,
                offset,
                "store_u32_right",
                "std::uint32_t",
            );
        }
        Operation::LoadDoublewordLeft { dst, base, offset } => {
            emit_partial_memory_load(output, index, dst, base, offset, "load_u64_left");
        }
        Operation::LoadDoublewordRight { dst, base, offset } => {
            emit_partial_memory_load(output, index, dst, base, offset, "load_u64_right");
        }
        Operation::StoreDoublewordLeft { src, base, offset } => {
            emit_partial_memory_store(
                output,
                index,
                src,
                base,
                offset,
                "store_u64_left",
                "std::uint64_t",
            );
        }
        Operation::StoreDoublewordRight { src, base, offset } => {
            emit_partial_memory_store(
                output,
                index,
                src,
                base,
                offset,
                "store_u64_right",
                "std::uint64_t",
            );
        }
        Operation::LoadDoubleword { dst, base, offset } => {
            emit_memory_load(
                output,
                index,
                dst,
                base,
                offset,
                MemoryLoadSpec {
                    width: 8,
                    signed: false,
                },
                in_branch_delay,
            );
        }
        Operation::StoreDoubleword { src, base, offset } => {
            emit_memory_store(output, index, src, base, offset, 8, in_branch_delay);
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

fn emit_fpu_operation(output: &mut String, index: usize, destination: &str, expression: &str) {
    let tag = format!("{index:04}");
    output.push_str(&format!(
        "    const auto fpu_result_{tag} = {expression};\n    {destination} = fpu_result_{tag}.bits;\n    state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_{tag});\n"
    ));
}

fn emit_fpu_binary(output: &mut String, index: usize, dst: Fpr, lhs: Fpr, rhs: Fpr, helper: &str) {
    emit_fpu_operation(
        output,
        index,
        &format!("state.fpr[{}]", dst.index()),
        &format!(
            "{helper}(state.fpr[{}], state.fpr[{}])",
            lhs.index(),
            rhs.index()
        ),
    );
}

fn emit_fpu_unary(output: &mut String, index: usize, dst: Fpr, src: Fpr, helper: &str) {
    emit_fpu_operation(
        output,
        index,
        &format!("state.fpr[{}]", dst.index()),
        &format!("{helper}(state.fpr[{}])", src.index()),
    );
}

fn emit_fpu_binary_to_acc(output: &mut String, index: usize, lhs: Fpr, rhs: Fpr, helper: &str) {
    emit_fpu_operation(
        output,
        index,
        "state.acc",
        &format!(
            "{helper}(state.fpr[{}], state.fpr[{}])",
            lhs.index(),
            rhs.index()
        ),
    );
}

fn emit_fpu_madd(
    output: &mut String,
    index: usize,
    dst: Option<Fpr>,
    lhs: Fpr,
    rhs: Fpr,
    helper: &str,
) {
    let destination = match dst {
        Some(dst) => format!("state.fpr[{}]", dst.index()),
        None => "state.acc".to_owned(),
    };
    emit_fpu_operation(
        output,
        index,
        &destination,
        &format!(
            "{helper}(state.acc, state.fpr[{}], state.fpr[{}])",
            lhs.index(),
            rhs.index()
        ),
    );
}

#[derive(Clone, Copy)]
struct MemoryLoadSpec {
    width: u8,
    signed: bool,
}

fn emit_memory_load(
    output: &mut String,
    index: usize,
    dst: Gpr,
    base: Gpr,
    offset: u16,
    access: MemoryLoadSpec,
    in_branch_delay: bool,
) {
    let MemoryLoadSpec { width, signed } = access;
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
        "    {value_type} memory_value_{tag}{{}};\n    const auto memory_status_{tag} = {helper}(state.memory, effective_address_{tag}, memory_value_{tag});\n"
    ));
    emit_memory_status_check(output, &tag, false, in_branch_delay);

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
    in_branch_delay: bool,
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
        "    const auto memory_status_{tag} = {helper}(state.memory, effective_address_{tag}, static_cast<{value_type}>(state.gpr[{}].low));\n",
        src.index()
    ));
    emit_memory_status_check(output, &tag, true, in_branch_delay);
}

fn emit_memory_status_check(output: &mut String, tag: &str, is_store: bool, in_branch_delay: bool) {
    let code = if is_store { "Store" } else { "Load" };
    output.push_str(&format!(
        "    if (memory_status_{tag} == Status::address_error) return reps2::runtime::raise_ee_address_error(state, reps2::runtime::AddressErrorCode::{code}, effective_address_{tag}, {in_branch_delay});\n    if (memory_status_{tag} != Status::ok) return memory_status_{tag};\n"
    ));
}

fn emit_partial_memory_load(
    output: &mut String,
    index: usize,
    dst: Gpr,
    base: Gpr,
    offset: u16,
    helper: &str,
) {
    let tag = format!("{index:04}");
    emit_effective_address(output, &tag, base, offset);
    output.push_str(&format!(
        "    std::uint64_t merged_value_{tag}{{}};\n    const auto memory_status_{tag} = {helper}(state.memory, effective_address_{tag}, state.gpr[{}].low, merged_value_{tag});\n    if (memory_status_{tag} != Status::ok) return memory_status_{tag};\n",
        dst.index()
    ));
    if !dst.is_zero() {
        output.push_str(&format!(
            "    state.gpr[{}].low = merged_value_{tag};\n",
            dst.index()
        ));
    }
}

fn emit_partial_memory_store(
    output: &mut String,
    index: usize,
    src: Gpr,
    base: Gpr,
    offset: u16,
    helper: &str,
    value_type: &str,
) {
    let tag = format!("{index:04}");
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
        output.push_str(&format!(
            "    state.gpr[{}] = quadword_value_{tag};\n",
            dst.index()
        ));
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
    in_branch_delay: bool,
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
             \x20   if (result_{tag} < -2147483648LL || result_{tag} > 2147483647LL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, {in_branch_delay});\n\
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
    in_branch_delay: bool,
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
             \x20   if (result_{tag} < -2147483648LL || result_{tag} > 2147483647LL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, {in_branch_delay});\n\
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
    in_branch_delay: bool,
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
             \x20   if (result_{tag} < -2147483648LL || result_{tag} > 2147483647LL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, {in_branch_delay});\n\
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

fn emit_parallel_add_u32(output: &mut String, dst: Gpr, lhs: Gpr, rhs: Gpr) {
    let d = dst.index();
    let a = lhs.index();
    let b = rhs.index();
    output.push_str(&format!(
        "    if ({d} != 0) state.gpr[{d}] = parallel_add_u32(state.gpr[{a}], state.gpr[{b}]);\n"
    ));
}

fn emit_move_from_hilo(output: &mut String, dst: Gpr, special: &str) {
    let d = dst.index();
    output.push_str(&format!(
        "    if ({d} != 0) state.gpr[{d}].low = state.{special};\n"
    ));
}

fn emit_move_to_hilo(output: &mut String, src: Gpr, special: &str) {
    let s = src.index();
    output.push_str(&format!("    state.{special} = state.gpr[{s}].low;\n"));
}

fn emit_multiply_word(
    output: &mut String,
    index: usize,
    dst: Gpr,
    lhs: Gpr,
    rhs: Gpr,
    signed: bool,
) {
    let tag = format!("{index:04}");
    let d = dst.index();
    let a = lhs.index();
    let b = rhs.index();

    if signed {
        output.push_str(&format!(
            "    const std::int64_t multiply_lhs_{tag} = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[{a}].low));\n\
             \x20   const std::int64_t multiply_rhs_{tag} = sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[{b}].low));\n\
             \x20   const std::int64_t multiply_product_{tag} = multiply_lhs_{tag} * multiply_rhs_{tag};\n\
             \x20   const std::uint64_t multiply_product_bits_{tag} = static_cast<std::uint64_t>(multiply_product_{tag});\n\
             \x20   const std::uint32_t multiply_low_{tag} = static_cast<std::uint32_t>(multiply_product_bits_{tag});\n\
             \x20   const std::uint32_t multiply_high_{tag} = static_cast<std::uint32_t>(multiply_product_bits_{tag} >> 32U);\n"
        ));
    } else {
        output.push_str(&format!(
            "    const std::uint64_t multiply_lhs_{tag} = static_cast<std::uint32_t>(state.gpr[{a}].low);\n\
             \x20   const std::uint64_t multiply_rhs_{tag} = static_cast<std::uint32_t>(state.gpr[{b}].low);\n\
             \x20   const std::uint64_t multiply_product_bits_{tag} = multiply_lhs_{tag} * multiply_rhs_{tag};\n\
             \x20   const std::uint32_t multiply_low_{tag} = static_cast<std::uint32_t>(multiply_product_bits_{tag});\n\
             \x20   const std::uint32_t multiply_high_{tag} = static_cast<std::uint32_t>(multiply_product_bits_{tag} >> 32U);\n"
        ));
    }

    output.push_str(&format!(
        "    state.lo = sign_extend_word(multiply_low_{tag});\n\
         \x20   state.hi = sign_extend_word(multiply_high_{tag});\n\
         \x20   if ({d} != 0) state.gpr[{d}].low = state.lo;\n"
    ));
}

/// Emits C++ for R5900 DIV/DIVU.
///
/// The R5900 defines the results for every input pair:
/// - Divisor zero: quotient = ±1 (sign-dependent for DIV, all-ones for DIVU),
///   remainder = dividend.
/// - INT32_MIN / -1 (signed only): quotient = INT32_MIN, remainder = 0.
///
/// The EE does *not* leave the results undefined for these edge cases; PCSX2
/// implements the same table. The generated code must handle them without
/// invoking C++ signed-overflow or division-by-zero UB.
fn emit_divide_word(output: &mut String, index: usize, lhs: Gpr, rhs: Gpr, signed: bool) {
    let tag = format!("{index:04}");
    let a = lhs.index();
    let b = rhs.index();

    if signed {
        // Signed division: extract low 32-bit words, sign-extend to int32_t.
        output.push_str(&format!(
            "    {{\n\
             \x20       const std::int32_t div_lhs_{tag} = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[{a}].low));\n\
             \x20       const std::int32_t div_rhs_{tag} = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.gpr[{b}].low));\n\
             \x20       if (div_rhs_{tag} == 0) {{\n\
             \x20           state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_{tag} >= 0 ? -1 : 1));\n\
             \x20           state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_{tag}));\n\
             \x20       }} else if (div_lhs_{tag} == std::numeric_limits<std::int32_t>::min() && div_rhs_{tag} == -1) {{\n\
             \x20           state.lo = sign_extend_word(static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::min()));\n\
             \x20           state.hi = sign_extend_word(0U);\n\
             \x20       }} else {{\n\
             \x20           state.lo = sign_extend_word(static_cast<std::uint32_t>(div_lhs_{tag} / div_rhs_{tag}));\n\
             \x20           state.hi = sign_extend_word(static_cast<std::uint32_t>(div_lhs_{tag} % div_rhs_{tag}));\n\
             \x20       }}\n\
             \x20   }}\n"
        ));
    } else {
        // Unsigned division: extract low 32-bit words.
        output.push_str(&format!(
            "    {{\n\
             \x20       const std::uint32_t div_lhs_{tag} = static_cast<std::uint32_t>(state.gpr[{a}].low);\n\
             \x20       const std::uint32_t div_rhs_{tag} = static_cast<std::uint32_t>(state.gpr[{b}].low);\n\
             \x20       if (div_rhs_{tag} == 0U) {{\n\
             \x20           state.lo = sign_extend_word(static_cast<std::uint32_t>(UINT32_MAX));\n\
             \x20           state.hi = sign_extend_word(div_lhs_{tag});\n\
             \x20       }} else {{\n\
             \x20           state.lo = sign_extend_word(div_lhs_{tag} / div_rhs_{tag});\n\
             \x20           state.hi = sign_extend_word(div_lhs_{tag} % div_rhs_{tag});\n\
             \x20       }}\n\
             \x20   }}\n"
        ));
    }
}

/// Emits C++ for MOVN/MOVZ conditional register moves.
///
/// MOVN: GPR[rd] = GPR[rs] if GPR[rt] != 0 (when_zero = false)
/// MOVZ: GPR[rd] = GPR[rs] if GPR[rt] == 0 (when_zero = true)
///
/// The move copies GPR[63:0] only, preserving GPR[127:64].
fn emit_move_conditional(output: &mut String, dst: Gpr, src: Gpr, test: Gpr, when_zero: bool) {
    let d = dst.index();
    let s = src.index();
    let t = test.index();
    let cmp = if when_zero { "==" } else { "!=" };
    output.push_str(&format!(
        "    if ({d} != 0 && state.gpr[{t}].low {cmp} 0U) state.gpr[{d}].low = state.gpr[{s}].low;\n"
    ));
}

fn emit_binary64_not_or(output: &mut String, dst: Gpr, lhs: Gpr, rhs: Gpr) {
    let d = dst.index();
    let a = lhs.index();
    let b = rhs.index();
    output.push_str(&format!(
        "    if ({d} != 0) state.gpr[{d}].low = ~(state.gpr[{a}].low | state.gpr[{b}].low);\n"
    ));
}

fn emit_set_less_than(output: &mut String, dst: Gpr, lhs: Gpr, rhs: Gpr, signed: bool) {
    let d = dst.index();
    let a = lhs.index();
    let b = rhs.index();
    let expression = if signed {
        format!(
            "std::bit_cast<std::int64_t>(state.gpr[{a}].low) < std::bit_cast<std::int64_t>(state.gpr[{b}].low)"
        )
    } else {
        format!("state.gpr[{a}].low < state.gpr[{b}].low")
    };
    output.push_str(&format!(
        "    if ({d} != 0) state.gpr[{d}].low = ({expression}) ? 1ULL : 0ULL;\n"
    ));
}

fn emit_set_less_than_immediate(
    output: &mut String,
    dst: Gpr,
    src: Gpr,
    immediate: u16,
    signed: bool,
) {
    let d = dst.index();
    let s = src.index();
    // Both SLTI and SLTIU sign-extend the 16-bit immediate. SLTIU then
    // interprets that sign-extended bit pattern as an unsigned 64-bit value.
    let signed_immediate = immediate as i16 as i64;
    let rhs = if signed {
        format!("static_cast<std::int64_t>({signed_immediate}LL)")
    } else {
        format!("static_cast<std::uint64_t>({signed_immediate}LL)")
    };
    let lhs = if signed {
        format!("std::bit_cast<std::int64_t>(state.gpr[{s}].low)")
    } else {
        format!("state.gpr[{s}].low")
    };
    output.push_str(&format!(
        "    if ({d} != 0) state.gpr[{d}].low = ({lhs} < {rhs}) ? 1ULL : 0ULL;\n"
    ));
}

fn emit_doubleword_signed_add(
    output: &mut String,
    index: usize,
    dst: Gpr,
    lhs: Gpr,
    rhs: Gpr,
    in_branch_delay: bool,
) {
    let tag = format!("{index:04}");
    let d = dst.index();
    let a = lhs.index();
    let b = rhs.index();
    output.push_str(&format!(
        "    const std::uint64_t signed_add_lhs_{tag} = state.gpr[{a}].low;\n\
         \x20   const std::uint64_t signed_add_rhs_{tag} = state.gpr[{b}].low;\n\
         \x20   const std::uint64_t signed_add_result_{tag} = signed_add_lhs_{tag} + signed_add_rhs_{tag};\n\
         \x20   if (((~(signed_add_lhs_{tag} ^ signed_add_rhs_{tag}) & (signed_add_lhs_{tag} ^ signed_add_result_{tag})) & 0x8000000000000000ULL) != 0ULL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, {in_branch_delay});\n\
         \x20   if ({d} != 0) state.gpr[{d}].low = signed_add_result_{tag};\n"
    ));
}

fn emit_doubleword_signed_sub(
    output: &mut String,
    index: usize,
    dst: Gpr,
    lhs: Gpr,
    rhs: Gpr,
    in_branch_delay: bool,
) {
    let tag = format!("{index:04}");
    let d = dst.index();
    let a = lhs.index();
    let b = rhs.index();
    output.push_str(&format!(
        "    const std::uint64_t signed_sub_lhs_{tag} = state.gpr[{a}].low;\n\
         \x20   const std::uint64_t signed_sub_rhs_{tag} = state.gpr[{b}].low;\n\
         \x20   const std::uint64_t signed_sub_result_{tag} = signed_sub_lhs_{tag} - signed_sub_rhs_{tag};\n\
         \x20   if ((((signed_sub_lhs_{tag} ^ signed_sub_rhs_{tag}) & (signed_sub_lhs_{tag} ^ signed_sub_result_{tag})) & 0x8000000000000000ULL) != 0ULL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, {in_branch_delay});\n\
         \x20   if ({d} != 0) state.gpr[{d}].low = signed_sub_result_{tag};\n"
    ));
}

fn emit_doubleword_signed_immediate_add(
    output: &mut String,
    index: usize,
    dst: Gpr,
    src: Gpr,
    immediate: u16,
    in_branch_delay: bool,
) {
    let tag = format!("{index:04}");
    let d = dst.index();
    let s = src.index();
    let signed_immediate = immediate as i16 as i64;
    output.push_str(&format!(
        "    const std::uint64_t signed_addi_lhs_{tag} = state.gpr[{s}].low;\n\
         \x20   const std::uint64_t signed_addi_rhs_{tag} = static_cast<std::uint64_t>({signed_immediate}LL);\n\
         \x20   const std::uint64_t signed_addi_result_{tag} = signed_addi_lhs_{tag} + signed_addi_rhs_{tag};\n\
         \x20   if (((~(signed_addi_lhs_{tag} ^ signed_addi_rhs_{tag}) & (signed_addi_lhs_{tag} ^ signed_addi_result_{tag})) & 0x8000000000000000ULL) != 0ULL) return reps2::runtime::raise_ee_exception(state, reps2::runtime::ExceptionCode::Overflow, {in_branch_delay});\n\
         \x20   if ({d} != 0) state.gpr[{d}].low = signed_addi_result_{tag};\n"
    ));
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

fn emit_doubleword_immediate_add(output: &mut String, dst: Gpr, src: Gpr, immediate: u16) {
    let d = dst.index();
    let s = src.index();
    let signed_immediate = immediate as i16 as i64;
    output.push_str(&format!(
        "    if ({d} != 0) state.gpr[{d}].low = state.gpr[{s}].low + static_cast<std::uint64_t>({signed_immediate}LL);\n"
    ));
}

fn emit_doubleword_shift(
    output: &mut String,
    dst: Gpr,
    src: Gpr,
    amount: u8,
    operator: &str,
    arithmetic: bool,
) {
    let d = dst.index();
    let s = src.index();
    if arithmetic {
        output.push_str(&format!(
            "    if ({d} != 0) state.gpr[{d}].low = arithmetic_shift_right_doubleword(state.gpr[{s}].low, {amount}U);\n"
        ));
    } else {
        output.push_str(&format!(
            "    if ({d} != 0) state.gpr[{d}].low = state.gpr[{s}].low {operator} {amount}U;\n"
        ));
    }
}

fn emit_variable_word_shift(
    output: &mut String,
    index: usize,
    dst: Gpr,
    src: Gpr,
    amount: Gpr,
    operator: &str,
    arithmetic: bool,
) {
    let tag = format!("{index:04}");
    let d = dst.index();
    let s = src.index();
    let a = amount.index();
    output.push_str(&format!(
        "    if (!is_word_value(state.gpr[{s}].low)) return Status::undefined_word_operand;\n    const std::uint32_t shift_amount_{tag} = static_cast<std::uint32_t>(state.gpr[{a}].low) & 0x1FU;\n"
    ));
    if arithmetic {
        output.push_str(&format!(
            "    if ({d} != 0) state.gpr[{d}].low = sign_extend_word(arithmetic_shift_right_word(static_cast<std::uint32_t>(state.gpr[{s}].low), static_cast<std::uint8_t>(shift_amount_{tag})));\n"
        ));
    } else {
        output.push_str(&format!(
            "    if ({d} != 0) state.gpr[{d}].low = sign_extend_word(static_cast<std::uint32_t>(state.gpr[{s}].low) {operator} shift_amount_{tag});\n"
        ));
    }
}

fn emit_variable_doubleword_shift(
    output: &mut String,
    index: usize,
    dst: Gpr,
    src: Gpr,
    amount: Gpr,
    operator: &str,
    arithmetic: bool,
) {
    let tag = format!("{index:04}");
    let d = dst.index();
    let s = src.index();
    let a = amount.index();
    output.push_str(&format!(
        "    const std::uint32_t shift_amount_{tag} = static_cast<std::uint32_t>(state.gpr[{a}].low) & 0x3FU;\n"
    ));
    if arithmetic {
        output.push_str(&format!(
            "    if ({d} != 0) state.gpr[{d}].low = arithmetic_shift_right_doubleword(state.gpr[{s}].low, static_cast<std::uint8_t>(shift_amount_{tag}));\n"
        ));
    } else {
        output.push_str(&format!(
            "    if ({d} != 0) state.gpr[{d}].low = state.gpr[{s}].low {operator} shift_amount_{tag};\n"
        ));
    }
}

fn emit_shift(
    output: &mut String,
    dst: Gpr,
    src: Gpr,
    amount: u8,
    operator: &str,
    arithmetic: bool,
) {
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
            (0x25 << 26) | (8 << 21) | (10 << 16) | 2,     // lhu t2, 2(t0)
            (0x21 << 26) | (8 << 21) | (11 << 16) | 4,     // lh t3, 4(t0)
            (0x23 << 26) | (8 << 21) | (12 << 16) | 8,     // lw t4, 8(t0)
            (0x2B << 26) | (8 << 21) | (13 << 16) | 12,    // sw t5, 12(t0)
            (0x1E << 26) | (8 << 21) | (14 << 16) | 4,     // lq t6, 4(t0)
            (0x1F << 26) | (8 << 21) | (15 << 16) | 8,     // sq t7, 8(t0)
            (0x37 << 26) | (8 << 21) | (16 << 16) | 4,     // ld s0, 4(t0)
            (0x3F << 26) | (8 << 21) | (16 << 16) | 0x34,  // sd s0, 0x34(t0)
            (0x22 << 26) | (8 << 21) | (17 << 16) | 1,     // lwl s1, 1(t0)
            (0x26 << 26) | (8 << 21) | (18 << 16) | 2,     // lwr s2, 2(t0)
            (0x2A << 26) | (8 << 21) | (19 << 16) | 3,     // swl s3, 3(t0)
            (0x2E << 26) | (8 << 21) | (20 << 16) | 1,     // swr s4, 1(t0)
            (0x1A << 26) | (8 << 21) | (21 << 16) | 3,     // ldl s5, 3(t0)
            (0x1B << 26) | (8 << 21) | (22 << 16) | 5,     // ldr s6, 5(t0)
            (0x2C << 26) | (8 << 21) | (23 << 16) | 6,     // sdl s7, 6(t0)
            (0x2D << 26) | (8 << 21) | (24 << 16) | 7,     // sdr t8, 7(t0)
        ];
        let decoded = words
            .iter()
            .enumerate()
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
        assert!(cpp.contains("load_u32_left(state.memory"));
        assert!(cpp.contains("load_u32_right(state.memory"));
        assert!(cpp.contains("store_u32_left(state.memory"));
        assert!(cpp.contains("store_u32_right(state.memory"));
        assert!(cpp.contains("load_u64_left(state.memory"));
        assert!(cpp.contains("load_u64_right(state.memory"));
        assert!(cpp.contains("store_u64_left(state.memory"));
        assert!(cpp.contains("store_u64_right(state.memory"));
        assert!(cpp.contains("state.gpr[14] = quadword_value_0005"));
        assert!(cpp.contains("state.gpr[16].low = memory_value_0007"));
        assert!(cpp.contains("0xFFFFFFFFFFFFFF00ULL"));
        assert!(cpp.contains("0xFFFFFFFFFFFF0000ULL"));
        assert!(cpp.contains("sign_extend_word(memory_value_0003)"));
        assert!(cpp.contains("AddressErrorCode::Load"));
        assert!(cpp.contains("AddressErrorCode::Store"));
        assert!(cpp.contains("raise_ee_address_error"));
        assert!(cpp.contains("memory_status_0000 != Status::ok"));
    }

    #[test]
    fn emits_doubleword_arithmetic_and_shifts_without_touching_upper_gpr_lane() {
        let words = [
            (8 << 21) | (9 << 16) | (10 << 11) | 0x2D,
            (8 << 21) | (9 << 16) | (11 << 11) | 0x2F,
            (0x19 << 26) | (8 << 21) | (12 << 16) | 0xFFF0,
            (9 << 16) | (13 << 11) | (7 << 6) | 0x38,
            (9 << 16) | (14 << 11) | (7 << 6) | 0x3E,
            (9 << 16) | (15 << 11) | (7 << 6) | 0x3F,
        ];
        let decoded = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0x5000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded).expect("doubleword operations lift");
        let cpp = emit_cpp(&program).expect("doubleword C++");
        assert!(cpp.contains("state.gpr[10].low = state.gpr[8].low + state.gpr[9].low"));
        assert!(cpp.contains("state.gpr[11].low = state.gpr[8].low - state.gpr[9].low"));
        assert!(cpp.contains("static_cast<std::uint64_t>(-16LL)"));
        assert!(cpp.contains("state.gpr[13].low = state.gpr[9].low << 7U"));
        assert!(cpp.contains("state.gpr[14].low = state.gpr[9].low >> 39U"));
        assert!(cpp.contains("arithmetic_shift_right_doubleword(state.gpr[9].low, 39U)"));
        assert!(!cpp.contains("state.gpr[10] ="));
    }

    #[test]
    fn emits_register_variable_word_and_doubleword_shifts_with_masked_counts() {
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
            .map(|(index, word)| crate::mips::decode(*word, 0x8000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded).expect("variable shifts lift");
        let cpp = emit_cpp(&program).expect("variable shift C++");
        assert!(
            cpp.contains(
                "shift_amount_0000 = static_cast<std::uint32_t>(state.gpr[4].low) & 0x1FU"
            )
        );
        assert!(cpp.contains("state.gpr[6].low = sign_extend_word(static_cast<std::uint32_t>(state.gpr[5].low) << shift_amount_0000)"));
        assert!(cpp.contains("state.gpr[7].low = sign_extend_word(static_cast<std::uint32_t>(state.gpr[5].low) >> shift_amount_0001)"));
        assert!(cpp.contains("arithmetic_shift_right_word(static_cast<std::uint32_t>(state.gpr[5].low), static_cast<std::uint8_t>(shift_amount_0002))"));
        assert!(
            cpp.contains(
                "shift_amount_0003 = static_cast<std::uint32_t>(state.gpr[4].low) & 0x3FU"
            )
        );
        assert!(cpp.contains("state.gpr[30].low = state.gpr[5].low << shift_amount_0003"));
        assert!(cpp.contains("state.gpr[31].low = state.gpr[5].low >> shift_amount_0004"));
        assert!(cpp.contains("arithmetic_shift_right_doubleword(state.gpr[5].low, static_cast<std::uint8_t>(shift_amount_0005))"));
        assert!(cpp.contains(
            "if (!is_word_value(state.gpr[5].low)) return Status::undefined_word_operand"
        ));
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

    #[test]
    fn emits_r5900_hilo_moves_and_signed_unsigned_three_operand_multiply() {
        let words = [
            (1 << 21) | 0x11,
            (20 << 11) | 0x10,
            (2 << 21) | 0x13,
            (21 << 11) | 0x12,
            (1 << 21) | (2 << 16) | (22 << 11) | 0x18,
            (24 << 11) | 0x10,
            (25 << 11) | 0x12,
            (3 << 21) | (2 << 16) | (23 << 11) | 0x19,
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0x9000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded_stream).expect("HI/LO and multiply IR");
        let cpp = emit_cpp(&program).expect("HI/LO and multiply C++");
        assert!(cpp.contains("state.hi = state.gpr[1].low"));
        assert!(cpp.contains("state.gpr[20].low = state.hi"));
        assert!(cpp.contains("state.lo = state.gpr[2].low"));
        assert!(cpp.contains("state.gpr[21].low = state.lo"));
        assert!(
            cpp.contains("sign_extend_word_signed(static_cast<std::uint32_t>(state.gpr[1].low))")
        );
        assert!(cpp.contains("multiply_product_0004 = multiply_lhs_0004 * multiply_rhs_0004"));
        assert!(cpp.contains("state.gpr[22].low = state.lo"));
        assert!(cpp.contains("state.gpr[24].low = state.hi"));
        assert!(cpp.contains("state.gpr[25].low = state.lo"));
        assert!(cpp.contains("multiply_lhs_0007 = static_cast<std::uint32_t>(state.gpr[3].low)"));
        assert!(cpp.contains("multiply_product_bits_0007 = multiply_lhs_0007 * multiply_rhs_0007"));
        assert!(cpp.contains("state.gpr[23].low = state.lo"));
        assert!(cpp.contains("state.hi = sign_extend_word(multiply_high_0007)"));
    }

    #[test]
    fn emits_r5900_signed_unsigned_comparisons_and_nor_with_exact_lane_semantics() {
        let words = [
            (4 << 21) | (5 << 16) | (6 << 11) | 0x27,
            (4 << 21) | (5 << 16) | (7 << 11) | 0x2A,
            (4 << 21) | (5 << 16) | (8 << 11) | 0x2B,
            (0x0A << 26) | (9 << 21) | (10 << 16) | 0x8001,
            (0x0B << 26) | (11 << 21) | (12 << 16) | 0xFFFF,
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0xA000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded_stream).expect("comparison/NOR IR");
        let cpp = emit_cpp(&program).expect("comparison/NOR C++");

        assert!(cpp.contains("~(state.gpr[4].low | state.gpr[5].low)"));
        assert!(cpp.contains("std::bit_cast<std::int64_t>(state.gpr[4].low) < std::bit_cast<std::int64_t>(state.gpr[5].low)"));
        assert!(cpp.contains("state.gpr[4].low < state.gpr[5].low"));
        assert!(cpp.contains(
            "std::bit_cast<std::int64_t>(state.gpr[9].low) < static_cast<std::int64_t>(-32767LL)"
        ));
        assert!(cpp.contains("state.gpr[11].low < static_cast<std::uint64_t>(-1LL)"));
        assert!(cpp.contains("? 1ULL : 0ULL"));
        assert!(cpp.contains("#include <bit>"));
    }

    #[test]
    fn emits_trapping_doubleword_arithmetic_with_portable_overflow_detection() {
        let words = [
            (8 << 21) | (9 << 16) | (10 << 11) | 0x2C,
            (12 << 21) | (13 << 16) | (11 << 11) | 0x2E,
            (0x18 << 26) | (15 << 21) | (14 << 16) | 0xFFFE,
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0xB000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded_stream).expect("signed doubleword IR");
        let cpp = emit_cpp(&program).expect("signed doubleword C++");

        assert!(cpp.contains("signed_add_result_0000 = signed_add_lhs_0000 + signed_add_rhs_0000"));
        assert!(cpp.contains("~(signed_add_lhs_0000 ^ signed_add_rhs_0000)"));
        assert!(cpp.contains("signed_sub_result_0001 = signed_sub_lhs_0001 - signed_sub_rhs_0001"));
        assert!(cpp.contains("(signed_sub_lhs_0001 ^ signed_sub_rhs_0001)"));
        assert!(cpp.contains("signed_addi_rhs_0002 = static_cast<std::uint64_t>(-2LL)"));
        assert!(
            cpp.contains("signed_addi_result_0002 = signed_addi_lhs_0002 + signed_addi_rhs_0002")
        );
        assert_eq!(cpp.matches("ExceptionCode::Overflow, false);").count(), 3);
        assert!(cpp.contains("if (10 != 0) state.gpr[10].low = signed_add_result_0000"));
        assert!(cpp.contains("if (11 != 0) state.gpr[11].low = signed_sub_result_0001"));
        assert!(cpp.contains("if (14 != 0) state.gpr[14].low = signed_addi_result_0002"));
    }

    #[test]
    fn emits_mmi_pipeline_one_hilo_moves_with_upper_lane_preservation() {
        let words = [
            (0x1C << 26) | (4 << 21) | 0x11,
            (0x1C << 26) | (5 << 21) | 0x13,
            (0x1C << 26) | (6 << 11) | 0x10,
            (0x1C << 26) | (7 << 11) | 0x12,
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0xD000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded_stream).expect("HI1/LO1 IR");
        let cpp = emit_cpp(&program).expect("HI1/LO1 C++");

        assert!(cpp.contains("state.hi1 = state.gpr[4].low;"));
        assert!(cpp.contains("state.lo1 = state.gpr[5].low;"));
        assert!(cpp.contains("if (6 != 0) state.gpr[6].low = state.hi1;"));
        assert!(cpp.contains("if (7 != 0) state.gpr[7].low = state.lo1;"));
        assert!(!cpp.contains("state.gpr[6] = state.hi1"));
    }

    #[test]
    fn emits_parallel_add_unsigned_word_as_lane_saturating_runtime_call() {
        let words = [
            (0x1C << 26) | (1 << 21) | (2 << 16) | (3 << 11) | (0x10 << 6) | 0x28,
            (0x1C << 26) | (1 << 21) | (2 << 16) | (0x10 << 6) | 0x28,
        ];
        let decoded_stream = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0xC000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded_stream).expect("PADDUW IR");
        let cpp = emit_cpp(&program).expect("PADDUW C++");

        assert!(cpp.contains("using reps2::runtime::parallel_add_u32;"));
        assert!(
            cpp.contains(
                "if (3 != 0) state.gpr[3] = parallel_add_u32(state.gpr[1], state.gpr[2]);"
            )
        );
        assert!(
            cpp.contains(
                "if (0 != 0) state.gpr[0] = parallel_add_u32(state.gpr[1], state.gpr[2]);"
            )
        );
        assert!(!cpp.contains("state.gpr[3].low = parallel_add_u32"));
    }

    #[test]
    fn emits_r5900_sa_instructions_with_upper_lane_preservation() {
        let words = [
            0x0080_0029,                                      // mtsa $4
            (0x01 << 26) | (4 << 21) | (0x18 << 16) | 0x000F, // mtsab $4, 0xF
            (0x01 << 26) | (4 << 21) | (0x19 << 16) | 0x0007, // mtsah $4, 0x7
            0x0000_2828,                                      // mfsa $5
            0x0000_0028,                                      // mfsa $0
        ];
        let decoded = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0xE000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded).expect("SA IR");
        let cpp = emit_cpp(&program).expect("SA C++");

        // MTSA/MFSA transfer the whole 64-bit low lane; SA is not a byte field.
        assert!(cpp.contains("state.sa = state.gpr[4].low;"));
        assert!(cpp.contains("if (5 != 0) state.gpr[5].low = state.sa;"));
        assert!(cpp.contains("if (0 != 0) state.gpr[0].low = state.sa;"));
        assert!(!cpp.contains("state.gpr[5] = state.sa;"));

        // MTSAB/MTSAH scale the XOR result into a bit count (x8 / x16).
        assert!(cpp.contains("state.sa = ((state.gpr[4].low & 0x0FU) ^ 0x0FU) * 8U;"));
        assert!(cpp.contains("state.sa = ((state.gpr[4].low & 0x07U) ^ 0x07U) * 16U;"));
    }

    #[test]
    fn emits_mmi1_qfsrv_funnel_shift() {
        let decoded = [crate::mips::decode(0x7085_36E8, 0xF000)];
        let program = ir::lift(&decoded).expect("QFSRV IR");
        let cpp = emit_cpp(&program).expect("QFSRV C++");

        assert!(cpp.contains(
            "if (6 != 0) state.gpr[6] = funnel_shift_right_vec(state.gpr[4], state.gpr[5], state.sa);"
        ));
        assert!(cpp.contains("using reps2::runtime::funnel_shift_right_vec;"));

        let zero_destination = [crate::mips::decode(0x7080_06E8, 0xF004)];
        let program = ir::lift(&zero_destination).expect("QFSRV $0 IR");
        let cpp = emit_cpp(&program).expect("QFSRV $0 C++");
        assert!(cpp.contains("if (0 != 0) state.gpr[0] = funnel_shift_right_vec("));
    }

    #[test]
    fn emits_cop1_moves_with_fpr_sign_extension_and_zero_guard() {
        let words = [
            0x4402_2800, // mfc1 $2, $f5
            0x4400_2800, // mfc1 $0, $f5
            0x4487_6000, // mtc1 $7, $f12
            0x4480_0000, // mtc1 $zero, $f0
            0x4443_0000, // cfc1 $3, FIR
            0x4443_F800, // cfc1 $3, FCSR
            0x44C4_F800, // ctc1 $4, FCSR
        ];
        let decoded = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0x1000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded).expect("COP1 IR");
        let cpp = emit_cpp(&program).expect("COP1 C++");

        // MFC1 sign-extends into the low lane and keeps GPR[127:64]; GPR[0]
        // stays hard-wired zero, FPR[0] does not.
        assert!(cpp.contains("if (2 != 0) state.gpr[2].low = sign_extend_word(state.fpr[5]);"));
        assert!(cpp.contains("if (0 != 0) state.gpr[0].low = sign_extend_word(state.fpr[5]);"));
        assert!(cpp.contains("state.fpr[12] = static_cast<std::uint32_t>(state.gpr[7].low);"));
        assert!(cpp.contains("state.fpr[0] = static_cast<std::uint32_t>(state.gpr[0].low);"));
        assert!(!cpp.contains("if (0 != 0) state.fpr[0]"));
        assert!(cpp.contains(
            "if (3 != 0) state.gpr[3].low = sign_extend_word(reps2::runtime::kFpuImplementationRegister);"
        ));
        assert!(cpp.contains("if (3 != 0) state.gpr[3].low = sign_extend_word(state.fcsr);"));
        assert!(cpp.contains(
            "store_fpu_control(state.fcsr, static_cast<std::uint32_t>(state.gpr[4].low));"
        ));
        assert!(cpp.contains("using reps2::runtime::store_fpu_control;"));
        // FIR is read-only, so no generated statement may write it.
        assert!(!cpp.contains("kFpuImplementationRegister ="));
    }

    #[test]
    fn emits_cop1_word_memory_through_the_shared_helpers() {
        let load = [crate::mips::decode(0xC506_0010, 0x1000)]; // lwc1 $f6, 0x10($8)
        let cpp = emit_cpp(&ir::lift(&load).expect("lwc1 IR")).expect("lwc1 C++");
        assert!(cpp.contains("load_u32(state.memory, effective_address_0000, memory_value_0000)"));
        assert!(cpp.contains("state.fpr[6] = memory_value_0000;"));
        assert!(!cpp.contains("state.gpr[6] = memory_value_0000"));

        let store = [crate::mips::decode(0xE506_FFFC, 0x1000)]; // swc1 $f6, -4($8)
        let cpp = emit_cpp(&ir::lift(&store).expect("swc1 IR")).expect("swc1 C++");
        assert!(cpp.contains("store_u32(state.memory, effective_address_0000, state.fpr[6])"));
        assert!(cpp.contains("sign_extend_immediate(0xFFFCU)"));
    }

    #[test]
    fn emits_cop1_arithmetic_through_the_runtime_with_fcr31_and_acc() {
        let words = [
            0x4602_08C0, // add.s   $f3, $f1, $f2
            0x4602_08C1, // sub.s   $f3, $f1, $f2
            0x4602_00C4, // sqrt.s  $f3, $f2
            0x4602_0818, // adda.s  $f1, $f2
            0x4602_08DC, // madd.s  $f3, $f1, $f2
            0x4602_081E, // madda.s $f1, $f2
            0x4680_08E0, // cvt.s.w $f3, $f1
            0x4600_08E4, // cvt.w.s $f3, $f1
        ];
        let decoded = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0x1000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded).expect("COP1 arithmetic IR");
        let cpp = emit_cpp(&program).expect("COP1 arithmetic C++");

        // fs is the first argument and ft the second, so a swapped mapping
        // would change SUB.S from fs-ft to ft-fs.
        assert!(cpp.contains("const auto fpu_result_0000 = fpu_add(state.fpr[1], state.fpr[2]);"));
        assert!(cpp.contains("const auto fpu_result_0001 = fpu_sub(state.fpr[1], state.fpr[2]);"));
        // SQRT.S reads ft, not fs (which is reserved zero).
        assert!(cpp.contains("const auto fpu_result_0002 = fpu_sqrt(state.fpr[2]);"));
        // The accumulate forms write ACC instead of an FPR, and MADD reads it.
        assert!(cpp.contains("const auto fpu_result_0003 = fpu_add(state.fpr[1], state.fpr[2]);"));
        assert!(cpp.contains("state.acc = fpu_result_0003.bits;"));
        assert!(cpp.contains(
            "const auto fpu_result_0004 = fpu_madd(state.acc, state.fpr[1], state.fpr[2]);"
        ));
        assert!(cpp.contains("state.fpr[3] = fpu_result_0004.bits;"));
        assert!(cpp.contains(
            "const auto fpu_result_0005 = fpu_madd(state.acc, state.fpr[1], state.fpr[2]);"
        ));
        assert!(cpp.contains("state.acc = fpu_result_0005.bits;"));
        assert!(cpp.contains("const auto fpu_result_0006 = fpu_cvt_s_w(state.fpr[1]);"));
        assert!(cpp.contains("const auto fpu_result_0007 = fpu_cvt_w_s(state.fpr[1]);"));
        // Every form routes its flags through FCR31's cause/sticky fields.
        for tag in 0..8 {
            assert!(
                cpp.contains(&format!(
                    "state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_{tag:04});"
                )),
                "instruction {tag} must update FCR31"
            );
        }
        for helper in [
            "apply_fpu_flags",
            "fpu_add",
            "fpu_sub",
            "fpu_mul",
            "fpu_div",
            "fpu_sqrt",
            "fpu_rsqrt",
            "fpu_abs",
            "fpu_neg",
            "fpu_mov",
            "fpu_max",
            "fpu_min",
            "fpu_cvt_w_s",
            "fpu_cvt_s_w",
            "fpu_madd",
            "fpu_msub",
        ] {
            assert!(
                cpp.contains(&format!("using reps2::runtime::{helper};")),
                "prologue must expose {helper}"
            );
        }
    }

    #[test]
    fn emits_cop1_compares_as_a_condition_bit_update() {
        let words = [
            0x4602_0830, // c.f.s  $f1, $f2
            0x4602_0832, // c.eq.s $f1, $f2
            0x4602_0834, // c.lt.s $f1, $f2
            0x4602_0836, // c.le.s $f1, $f2
        ];
        let decoded = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0x1000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded).expect("COP1 compare IR");
        let cpp = emit_cpp(&program).expect("COP1 compare C++");

        // fs is the first argument and ft the second, and every form updates
        // exactly the condition bit instead of an FPR.
        for (index, comparison) in ["False", "Equal", "LessThan", "LessThanOrEqual"]
            .into_iter()
            .enumerate()
        {
            assert!(
                cpp.contains(&format!(
                    "state.fcsr = apply_fpu_condition(state.fcsr, fpu_compare(FpuCompare::{comparison}, state.fpr[1], state.fpr[2]));"
                )),
                "compare {index} must lower to apply_fpu_condition"
            );
        }
        assert!(!cpp.contains("state.fpr[0] ="));
        for helper in ["apply_fpu_condition", "fpu_compare", "FpuCompare"] {
            assert!(
                cpp.contains(&format!("using reps2::runtime::{helper};")),
                "prologue must expose {helper}"
            );
        }
    }

    #[test]
    fn emits_cop0_transfers_and_interrupt_controls() {
        let words = [
            (0x10 << 26) | (2 << 16) | (12 << 11), // mfc0 $v0, $12 (Status)
            (0x10 << 26) | (4 << 21) | (3 << 16) | (12 << 11), // mtc0 $v1, $12 (Status)
            0x4200_0038,                           // ei
            0x4200_0039,                           // di
        ];
        let decoded = words
            .iter()
            .enumerate()
            .map(|(index, word)| crate::mips::decode(*word, 0x1000 + index as u32 * 4))
            .collect::<Vec<_>>();
        let program = ir::lift(&decoded).expect("COP0 IR");
        let cpp = emit_cpp(&program).expect("COP0 C++");

        assert!(
            cpp.contains("if (2 != 0) state.gpr[2].low = sign_extend_word(read_cop0(state, 12));")
        );
        assert!(
            cpp.contains("write_cop0(state, 12, static_cast<std::uint32_t>(state.gpr[3].low));")
        );
        assert!(cpp.contains("enable_interrupts(state);"));
        assert!(cpp.contains("disable_interrupts(state);"));
        assert!(cpp.contains("using reps2::runtime::read_cop0;"));
        assert!(cpp.contains("using reps2::runtime::write_cop0;"));
        assert!(cpp.contains("using reps2::runtime::enable_interrupts;"));
        assert!(cpp.contains("using reps2::runtime::disable_interrupts;"));
    }
}
