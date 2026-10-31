// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace reps2::runtime {

struct Gpr128 {
    std::uint64_t low{};
    std::uint64_t high{};
};

struct EeState {
    std::uint32_t pc{};
    std::array<Gpr128, 32> gpr{};
};

enum class Status : std::uint8_t {
    ok,
    undefined_word_operand,
    integer_overflow,
    instruction_budget_exhausted,
    external_control_flow,
    unsupported_instruction,
};

[[nodiscard]] constexpr std::uint64_t sign_extend_word(std::uint32_t value) noexcept {
    return (value & 0x80000000U) != 0
        ? (0xFFFFFFFF00000000ULL | static_cast<std::uint64_t>(value))
        : static_cast<std::uint64_t>(value);
}

[[nodiscard]] constexpr std::uint32_t sign_extend_immediate(std::uint16_t value) noexcept {
    return (value & 0x8000U) != 0
        ? (0xFFFF0000U | static_cast<std::uint32_t>(value))
        : static_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr std::int64_t sign_extend_word_signed(std::uint32_t value) noexcept {
    return static_cast<std::int64_t>(std::bit_cast<std::int32_t>(value));
}

[[nodiscard]] constexpr bool is_word_value(std::uint64_t value) noexcept {
    const auto low = static_cast<std::uint32_t>(value);
    const auto expected_high = (low & 0x80000000U) != 0 ? 0xFFFFFFFFULL : 0ULL;
    return (value >> 32U) == expected_high;
}

[[nodiscard]] constexpr std::uint32_t arithmetic_shift_right_word(
    std::uint32_t value,
    std::uint8_t amount
) noexcept {
    if (amount == 0 || amount >= 32) {
        return amount == 0 ? value : ((value & 0x80000000U) != 0 ? 0xFFFFFFFFU : 0U);
    }
    const auto shifted = value >> amount;
    if ((value & 0x80000000U) == 0) {
        return shifted;
    }
    const auto fill = 0xFFFFFFFFU << (32U - amount);
    return shifted | fill;
}

} // namespace reps2::runtime
