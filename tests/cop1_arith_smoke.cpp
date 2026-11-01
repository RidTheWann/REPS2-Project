// SPDX-License-Identifier: GPL-3.0-or-later
// Differential test of the EE COP1 numeric core.
//
// The expectations in tests/cop1_arith_vectors.inc are produced by
// tools/ee_fpu_model.py, which evaluates every operation on exact arbitrary
// precision rationals and truncates once. This binary reaches the same numbers
// through the runtime's own integer pipeline, so agreement is evidence about
// both implementations rather than about one of them twice.
//
// The rows also carry the whole FCR31 word after the instruction, so the cause
// and sticky rules are checked alongside the arithmetic.

#include "reps2/runtime/ee_fpu.hpp"
#include "reps2/runtime/ee_state.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

using reps2::runtime::apply_fpu_flags;
using reps2::runtime::FpuResult;
using reps2::runtime::kFpuCauseDivideByZero;
using reps2::runtime::kFpuCauseInvalid;
using reps2::runtime::kFpuCauseOverflow;
using reps2::runtime::kFpuCauseUnderflow;
using reps2::runtime::kFpuCondition;
using reps2::runtime::kFpuControlResetValue;
using reps2::runtime::kFpuControlWritableMask;
using reps2::runtime::kFpuStickyDivideByZero;
using reps2::runtime::kFpuStickyInvalid;
using reps2::runtime::kFpuStickyOverflow;
using reps2::runtime::kFpuStickyUnderflow;

// The numbering is the oracle's OP_ID table, not this file's choice.
enum Cop1OpIds : std::uint32_t {
    kOpAdd = 0,
    kOpSub = 1,
    kOpMul = 2,
    kOpDiv = 3,
    kOpSqrt = 4,
    kOpRsqrt = 5,
    kOpAbs = 6,
    kOpNeg = 7,
    kOpMov = 8,
    kOpAddA = 9,
    kOpSubA = 10,
    kOpMulA = 11,
    kOpMadd = 12,
    kOpMsub = 13,
    kOpMaddA = 14,
    kOpMsubA = 15,
    kOpMax = 16,
    kOpMin = 17,
    kOpCvtWS = 18,
    kOpCvtSW = 19,
};

struct Row {
    std::uint32_t op;
    std::uint32_t a;
    std::uint32_t b;
    std::uint32_t acc;
    std::uint32_t out;
    std::uint32_t fcsr;
};

constexpr Row kVectors[] =
#include "cop1_arith_vectors.inc"
;

constexpr std::size_t kVectorCount = sizeof(kVectors) / sizeof(Row);

[[nodiscard]] const char* op_name(std::uint32_t op) noexcept {
    switch (op) {
        case kOpAdd: return "add";
        case kOpSub: return "sub";
        case kOpMul: return "mul";
        case kOpDiv: return "div";
        case kOpSqrt: return "sqrt";
        case kOpRsqrt: return "rsqrt";
        case kOpAbs: return "abs";
        case kOpNeg: return "neg";
        case kOpMov: return "mov";
        case kOpAddA: return "adda";
        case kOpSubA: return "suba";
        case kOpMulA: return "mula";
        case kOpMadd: return "madd";
        case kOpMsub: return "msub";
        case kOpMaddA: return "madda";
        case kOpMsubA: return "msuba";
        case kOpMax: return "max";
        case kOpMin: return "min";
        case kOpCvtWS: return "cvt_w_s";
        case kOpCvtSW: return "cvt_s_w";
        default: return "unknown";
    }
}

/// Run one vector. The .A forms share their .S counterpart's arithmetic and
/// differ only in the destination register, which is the lifter's decision, so
/// they bind to the same runtime entry point here on purpose.
[[nodiscard]] bool run(const Row& row, FpuResult& result) noexcept {
    using namespace reps2::runtime;
    switch (row.op) {
        case kOpAdd: case kOpAddA: result = fpu_add(row.a, row.b); return true;
        case kOpSub: case kOpSubA: result = fpu_sub(row.a, row.b); return true;
        case kOpMul: case kOpMulA: result = fpu_mul(row.a, row.b); return true;
        case kOpDiv: result = fpu_div(row.a, row.b); return true;
        case kOpSqrt: result = fpu_sqrt(row.b); return true;
        case kOpRsqrt: result = fpu_rsqrt(row.a, row.b); return true;
        case kOpAbs: result = fpu_abs(row.a); return true;
        case kOpNeg: result = fpu_neg(row.a); return true;
        case kOpMov: result = fpu_mov(row.a); return true;
        case kOpMadd: case kOpMaddA:
            result = fpu_madd(row.acc, row.a, row.b);
            return true;
        case kOpMsub: case kOpMsubA:
            result = fpu_msub(row.acc, row.a, row.b);
            return true;
        case kOpMax: result = fpu_max(row.a, row.b); return true;
        case kOpMin: result = fpu_min(row.a, row.b); return true;
        case kOpCvtWS: result = fpu_cvt_w_s(row.a); return true;
        case kOpCvtSW: result = fpu_cvt_s_w(row.a); return true;
        default: return false;
    }
}

void report(const Row& row, const FpuResult& result, std::uint32_t fcsr) {
    std::printf(
        "vector %s(0x%08X,0x%08X,acc=0x%08X): got 0x%08X fcsr 0x%08X,"
        " want 0x%08X fcsr 0x%08X\n",
        op_name(row.op),
        static_cast<unsigned>(row.a),
        static_cast<unsigned>(row.b),
        static_cast<unsigned>(row.acc),
        static_cast<unsigned>(result.bits),
        static_cast<unsigned>(fcsr),
        static_cast<unsigned>(row.out),
        static_cast<unsigned>(row.fcsr)
    );
}

[[nodiscard]] bool check_control_store() {
    // CTC1 may program C and the eight flag bits and nothing else: bit 24
    // always reads as 1 and bits 1..0 always read as 0b01. A store that could
    // clear bit 24 or persist one of the reserved-zero fields would be a
    // hardware divergence a game could observe by reading the register back.
    struct Case {
        std::uint32_t source;
        std::uint32_t expected;
    };
    constexpr Case cases[] = {
        {0xFFFF'FFFFU, 0x0183'C079U},
        {0x0000'0000U, 0x0100'0001U},
        // Every reserved-zero bit set, bit 24 clear and every writable bit
        // clear: the register must read back exactly the reset image.
        {0xFF7C'3F87U, 0x0100'0001U},
        {kFpuCondition | 0x0000'0002U, 0x0180'0001U},
        // Cause I and sticky U survive; the reserved 13..7 field does not.
        {kFpuCauseInvalid | kFpuStickyUnderflow | 0x0000'1F80U, 0x0102'0009U},
    };
    bool ok = true;
    for (const auto& item : cases) {
        std::uint32_t control = 0U;
        reps2::runtime::store_fpu_control(control, item.source);
        if (control != item.expected) {
            std::printf("ctc1 0x%08X: got 0x%08X, want 0x%08X\n",
                static_cast<unsigned>(item.source),
                static_cast<unsigned>(control),
                static_cast<unsigned>(item.expected));
            ok = false;
        }
    }
    return ok;
}

[[nodiscard]] bool check_flag_routing() {
    // Every cause flag starts set, so an operation that names a flag as cleared
    // is observable. The Instruction Set Manual's Ch.5 opening states that
    // "flags that are not indicated in the instructions are not changed", which
    // contradicts the User's Manual Table 8-2 sentence about the other fields
    // being cleared; this is the reading the runtime implements, and these
    // checks are what makes the choice falsifiable rather than implicit.
    constexpr std::uint32_t all_flags = kFpuCauseInvalid | kFpuCauseDivideByZero
        | kFpuCauseOverflow | kFpuCauseUnderflow | kFpuStickyInvalid
        | kFpuStickyDivideByZero | kFpuStickyOverflow | kFpuStickyUnderflow;
    const std::uint32_t preset = kFpuControlResetValue | all_flags | kFpuCondition;

    struct Case {
        const char* name;
        FpuResult result;
        std::uint32_t expected;
    };

    const std::uint32_t keep_everything = preset;
    Case cases[] = {
        {"add", {0x3F800000U, 0U, 0U}, keep_everything},
        {"mov", {0x3F800000U, 0U, 0U}, keep_everything},
        {"mul", {0x3F800000U, 0U, 0U}, keep_everything},
        // SQRT.S names D; NEG.S, MAX.S and MIN.S name O and U.
        {"sqrt", {0x3F800000U, 0U, kFpuCauseDivideByZero},
            preset & ~kFpuCauseDivideByZero},
        {"neg", {0xBF800000U, 0U, kFpuCauseOverflow | kFpuCauseUnderflow},
            preset & ~(kFpuCauseOverflow | kFpuCauseUnderflow)},
        {"max", {0x3F800000U, 0U, kFpuCauseOverflow | kFpuCauseUnderflow},
            preset & ~(kFpuCauseOverflow | kFpuCauseUnderflow)},
        {"min", {0x3F800000U, 0U, kFpuCauseOverflow | kFpuCauseUnderflow},
            preset & ~(kFpuCauseOverflow | kFpuCauseUnderflow)},
    };

    bool ok = true;
    for (const auto& item : cases) {
        const auto actual = apply_fpu_flags(preset, item.result);
        if (actual != item.expected) {
            std::printf("flags after %s: got 0x%08X, want 0x%08X\n",
                item.name,
                static_cast<unsigned>(actual),
                static_cast<unsigned>(item.expected));
            ok = false;
        }
    }

    // An operation that sets a cause leaves the other cause bits alone even
    // when they were already set, and never lowers a sticky flag.
    const std::uint32_t invalid_only = apply_fpu_flags(
        preset, {0x00000000U, kFpuCauseInvalid, 0U});
    if (invalid_only != preset) {
        std::printf("flags after an already-sticky invalid: got 0x%08X, want 0x%08X\n",
            static_cast<unsigned>(invalid_only),
            static_cast<unsigned>(preset));
        ok = false;
    }
    const auto from_reset = apply_fpu_flags(
        kFpuControlResetValue, {0x00000000U, kFpuCauseInvalid, 0U});
    constexpr std::uint32_t invalid_from_reset = kFpuControlResetValue
        | kFpuCauseInvalid | kFpuStickyInvalid;
    if (from_reset != invalid_from_reset) {
        std::printf("invalid from reset: got 0x%08X, want 0x%08X\n",
            static_cast<unsigned>(from_reset),
            static_cast<unsigned>(invalid_from_reset));
        ok = false;
    }
    return ok;
}

[[nodiscard]] bool check_helper_flag_clears() {
    // check_flag_routing builds its FpuResult values by hand, so it proves
    // apply_fpu_flags and nothing about the helpers themselves. These cases
    // feed the helpers' own output: SQRT.S must name D as cleared,
    // NEG.S/MAX.S/MIN.S O and U, and every form that names no clear must
    // leave all four cause flags set.
    using namespace reps2::runtime;
    constexpr std::uint32_t all_flags = kFpuCauseInvalid | kFpuCauseDivideByZero
        | kFpuCauseOverflow | kFpuCauseUnderflow | kFpuStickyInvalid
        | kFpuStickyDivideByZero | kFpuStickyOverflow | kFpuStickyUnderflow;
    const std::uint32_t preset = kFpuControlResetValue | all_flags | kFpuCondition;
    const std::uint32_t no_o_u = preset & ~(kFpuCauseOverflow | kFpuCauseUnderflow);
    const std::uint32_t no_d = preset & ~kFpuCauseDivideByZero;

    struct Case {
        const char* name;
        FpuResult result;
        std::uint32_t expected;
    };
    const Case cases[] = {
        {"sqrt", fpu_sqrt(0x40800000U), no_d},
        {"neg", fpu_neg(0x3F800000U), no_o_u},
        {"max", fpu_max(0x3F800000U, 0x40000000U), no_o_u},
        {"min", fpu_min(0x3F800000U, 0x40000000U), no_o_u},
        {"add", fpu_add(0x3F800000U, 0x3F800000U), preset},
        {"div", fpu_div(0x3F800000U, 0x3F800000U), preset},
        {"mov", fpu_mov(0x3F800000U), preset},
    };

    bool ok = true;
    for (const auto& item : cases) {
        const auto actual = apply_fpu_flags(preset, item.result);
        if (actual != item.expected) {
            std::printf("helper clears after %s: got 0x%08X, want 0x%08X\n",
                item.name,
                static_cast<unsigned>(actual),
                static_cast<unsigned>(item.expected));
            ok = false;
        }
    }
    return ok;
}

[[nodiscard]] bool check_state_defaults() {
    // ACC is a register of its own, not FPR[32], and it starts at zero; FCR31
    // starts at the documented reset image.
    const reps2::runtime::EeState state;
    bool ok = true;
    if (state.acc != 0U) {
        std::printf("ACC reset value: got 0x%08X, want 0x00000000\n",
            static_cast<unsigned>(state.acc));
        ok = false;
    }
    if (state.fcsr != 0x0100'0001U) {
        std::printf("FCR31 reset value: got 0x%08X, want 0x01000001\n",
            static_cast<unsigned>(state.fcsr));
        ok = false;
    }
    if (kFpuControlWritableMask != 0x0083'C078U) {
        std::printf("FCR31 writable mask: got 0x%08X, want 0x0083C078\n",
            static_cast<unsigned>(kFpuControlWritableMask));
        ok = false;
    }
    return ok;
}

} // namespace

auto main() -> int {
    unsigned failures = 0U;
    unsigned unbound = 0U;
    for (const auto& row : kVectors) {
        FpuResult result{};
        if (!run(row, result)) {
            std::printf("vector %s(...): no runtime entry point for op %u\n",
                op_name(row.op), static_cast<unsigned>(row.op));
            ++unbound;
            continue;
        }
        const auto fcsr = apply_fpu_flags(kFpuControlResetValue, result);
        if (result.bits != row.out || fcsr != row.fcsr) {
            report(row, result, fcsr);
            ++failures;
        }
    }

    // A table that failed to reach this binary would otherwise look like a
    // clean run, so the row count is itself an assertion.
    if (kVectorCount < 400U) {
        std::printf("vector table too small: %zu rows\n", kVectorCount);
        ++failures;
    }
    if (!check_control_store()) {
        ++failures;
    }
    if (!check_flag_routing()) {
        ++failures;
    }
    if (!check_helper_flag_clears()) {
        ++failures;
    }
    if (!check_state_defaults()) {
        ++failures;
    }

    std::printf("cop1 arithmetic: %u vectors checked, %u unbound, %u failures\n",
        static_cast<unsigned>(kVectorCount), unbound, failures);
    return (failures == 0U && unbound == 0U) ? 0 : 1;
}
