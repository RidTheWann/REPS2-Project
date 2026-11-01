// SPDX-License-Identifier: GPL-3.0-or-later
// Differential test of the COP1 compare family and the FCR31 condition bit
// against tools/ee_fpu_model.py. The expectations in the generated table are
// an independent exact-rational derivation; the runtime compares decoded
// sign/exponent/mantissa forms with its own integer pipeline.
#include "reps2/runtime/ee_state.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

using reps2::runtime::apply_fpu_condition;
using reps2::runtime::FpuCompare;
using reps2::runtime::fpu_compare;
using reps2::runtime::kFpuCondition;

// The oracle writes these ids into the table; changing one here without
// regenerating the table would silently redefine every row.
enum Cop1CompareIds : int {
    kCompareFalse = 0,
    kCompareEqual = 1,
    kCompareLessThan = 2,
    kCompareLessThanOrEqual = 3,
};

struct CompareVector {
    int kind;
    std::uint32_t lhs;
    std::uint32_t rhs;
    bool expected;
    std::uint32_t fcsr;
};

// C clear with the D cause and the I sticky set: compare must leave every bit
// except C alone, including its sticky partner.
constexpr std::uint32_t kPreset = 0x0101'0041U;

constexpr CompareVector vectors[] =
#include "cop1_compare_vectors.inc"
;

constexpr std::size_t kExpectedVectors = 136U;

} // namespace

int main() {
    std::size_t failures = 0;
    std::size_t rows = 0;

    if (kFpuCondition != 0x0080'0000U) {
        std::printf("condition mask is 0x%08X, want 0x00800000\n", kFpuCondition);
        ++failures;
    }

    for (const CompareVector& vector : vectors) {
        ++rows;
        const auto comparison = static_cast<FpuCompare>(vector.kind);
        const bool result = fpu_compare(comparison, vector.lhs, vector.rhs);
        if (result != vector.expected) {
            std::printf(
                "row %zu kind %d: compare(0x%08X, 0x%08X) = %d, want %d\n",
                rows, vector.kind, vector.lhs, vector.rhs, result ? 1 : 0,
                vector.expected ? 1 : 0);
            ++failures;
        }
        const auto fcsr = apply_fpu_condition(kPreset, result);
        if (fcsr != vector.fcsr) {
            std::printf(
                "row %zu kind %d: fcsr 0x%08X, want 0x%08X\n",
                rows, vector.kind, fcsr, vector.fcsr);
            ++failures;
        }
    }

    if (rows != kExpectedVectors) {
        std::printf("table has %zu rows, want %zu\n", rows, kExpectedVectors);
        ++failures;
    }

    // The condition update is a read-modify-write of exactly bit 23. Starting
    // from every writable bit set proves a false compare clears C without
    // disturbing the causes or stickies, and a true one restores only C.
    constexpr std::uint32_t kAllWritable = 0x0183'C079U;
    if (apply_fpu_condition(kAllWritable, false) != 0x0103'C079U) {
        std::printf("clearing C disturbed other bits\n");
        ++failures;
    }
    if (apply_fpu_condition(kAllWritable, true) != kAllWritable) {
        std::printf("setting C disturbed other bits\n");
        ++failures;
    }

    // +0 and -0 are equal and neither is less than the other, while both order
    // below 1.0. A host-float comparison without the signed-zero rule or the
    // exponent-0 flush would disagree here.
    if (!fpu_compare(FpuCompare::Equal, 0x0000'0000U, 0x8000'0000U)) {
        std::printf("+0 did not compare equal to -0\n");
        ++failures;
    }
    if (fpu_compare(FpuCompare::LessThan, 0x0000'0000U, 0x8000'0000U)
        || fpu_compare(FpuCompare::LessThan, 0x8000'0000U, 0x0000'0000U)) {
        std::printf("signed zeros did not order as equal\n");
        ++failures;
    }
    if (!fpu_compare(FpuCompare::LessThan, 0x8000'0000U, 0x3F80'0000U)) {
        std::printf("-0 did not compare below 1.0\n");
        ++failures;
    }

    // The EE order differs from IEEE at the top of the range: 2^128 (exponent
    // field 0xFF) is normal, so it is above the IEEE largest finite and below
    // Fmax. A host float would saturate all three to the same value.
    if (!fpu_compare(FpuCompare::LessThan, 0x7F7F'FFFFU, 0x7F80'0000U)) {
        std::printf("IEEE max did not compare below 2^128\n");
        ++failures;
    }
    if (!fpu_compare(FpuCompare::LessThan, 0x7F80'0000U, 0x7FFF'FFFFU)) {
        std::printf("2^128 did not compare below Fmax\n");
        ++failures;
    }

    std::printf("cop1 compare: %zu vectors, %zu failures\n", rows, failures);
    return failures == 0 ? 0 : 1;
}
