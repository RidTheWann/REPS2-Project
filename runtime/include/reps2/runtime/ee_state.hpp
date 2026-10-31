// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <bit>
#include <cstdint>
#include <span>

namespace reps2::runtime {

struct Gpr128 {
    std::uint64_t low{};
    std::uint64_t high{};
};

inline constexpr std::uint32_t kEeMainRamSize = 32U * 1024U * 1024U;

struct EeMemory {
    std::span<std::uint8_t> main_ram{};
};

struct EeState {
    std::uint32_t pc{};
    std::array<Gpr128, 32> gpr{};
    // Base EE HI/LO state; MMI pipeline-1 HI1/LO1 is not modeled yet.
    std::uint64_t hi{};
    std::uint64_t lo{};
    EeMemory memory{};
};

enum class Status : std::uint8_t {
    ok,
    undefined_word_operand,
    integer_overflow,
    instruction_budget_exhausted,
    external_control_flow,
    unsupported_instruction,
    address_error,
    unmapped_memory,
    unsupported_memory_access,
    invalid_memory_image,
};

[[nodiscard]] constexpr bool is_unsupported_physical_region(std::uint32_t address) noexcept {
    // The physical 0x10000000..0x1FFFFFFF window contains EE registers,
    // reserved areas, IOP memory and BIOS; none is backed by main RAM here.
    return (address >= 0x10000000U && address < 0x20000000U)
        || (address >= 0x70000000U && address < 0x70004000U);
}

[[nodiscard]] constexpr bool resolve_ee_main_ram_address(
    std::uint32_t virtual_address,
    std::uint32_t& physical_address
) noexcept {
    if (virtual_address < kEeMainRamSize) {
        physical_address = virtual_address;
        return true;
    }
    if (virtual_address >= 0x20000000U && virtual_address < 0x22000000U) {
        physical_address = virtual_address - 0x20000000U;
        return true;
    }
    if (virtual_address >= 0x30100000U && virtual_address < 0x32000000U) {
        physical_address = virtual_address - 0x30000000U;
        return true;
    }

    // Direct-mapped KSEG0/KSEG1 addresses translate through the low 29 bits.
    if ((virtual_address >= 0x80000000U && virtual_address < 0xA0000000U)
        || (virtual_address >= 0xA0000000U && virtual_address < 0xC0000000U)) {
        const auto physical = virtual_address & 0x1FFFFFFFU;
        if (physical < kEeMainRamSize) {
            physical_address = physical;
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline Status initialize_ee_state(
    EeState& state,
    std::span<const std::uint8_t> initial_main_ram,
    std::uint32_t entry_pc
) noexcept {
    if (state.memory.main_ram.size() != kEeMainRamSize
        || initial_main_ram.size() != kEeMainRamSize
        || (entry_pc & 3U) != 0U) {
        return Status::invalid_memory_image;
    }
    std::uint32_t entry_physical{};
    if (!resolve_ee_main_ram_address(entry_pc, entry_physical)
        || static_cast<std::size_t>(entry_physical) + 4U > state.memory.main_ram.size()) {
        return Status::invalid_memory_image;
    }

    std::copy(initial_main_ram.begin(), initial_main_ram.end(), state.memory.main_ram.begin());
    state.gpr = {};
    state.gpr[0] = {};
    state.hi = 0;
    state.lo = 0;
    state.pc = entry_pc;
    return Status::ok;
}

[[nodiscard]] inline Status resolve_ee_memory_access(
    const EeMemory& memory,
    std::uint32_t virtual_address,
    std::size_t width,
    std::size_t alignment,
    std::uint32_t& physical_address
) noexcept {
    if (alignment > 1U && (virtual_address % alignment) != 0U) {
        return Status::address_error;
    }

    if (!resolve_ee_main_ram_address(virtual_address, physical_address)) {
        std::uint32_t translated = virtual_address;
        if ((virtual_address >= 0x80000000U && virtual_address < 0xC0000000U)) {
            translated = virtual_address & 0x1FFFFFFFU;
        }
        return is_unsupported_physical_region(translated)
            ? Status::unsupported_memory_access
            : Status::unmapped_memory;
    }

    const auto offset = static_cast<std::size_t>(physical_address);
    if (offset > memory.main_ram.size() || width > memory.main_ram.size() - offset) {
        return Status::unmapped_memory;
    }
    return Status::ok;
}

[[nodiscard]] inline Status load_u8(
    const EeMemory& memory,
    std::uint32_t address,
    std::uint8_t& value
) noexcept {
    std::uint32_t physical{};
    const auto status = resolve_ee_memory_access(memory, address, 1U, 1U, physical);
    if (status != Status::ok) return status;
    value = memory.main_ram[static_cast<std::size_t>(physical)];
    return Status::ok;
}

[[nodiscard]] inline Status load_u16(
    const EeMemory& memory,
    std::uint32_t address,
    std::uint16_t& value
) noexcept {
    std::uint32_t physical{};
    const auto status = resolve_ee_memory_access(memory, address, 2U, 2U, physical);
    if (status != Status::ok) return status;
    const auto offset = static_cast<std::size_t>(physical);
    value = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(memory.main_ram[offset])
        | (static_cast<std::uint32_t>(memory.main_ram[offset + 1U]) << 8U)
    );
    return Status::ok;
}

[[nodiscard]] inline Status load_u32(
    const EeMemory& memory,
    std::uint32_t address,
    std::uint32_t& value
) noexcept {
    std::uint32_t physical{};
    const auto status = resolve_ee_memory_access(memory, address, 4U, 4U, physical);
    if (status != Status::ok) return status;
    const auto offset = static_cast<std::size_t>(physical);
    value = static_cast<std::uint32_t>(memory.main_ram[offset])
        | (static_cast<std::uint32_t>(memory.main_ram[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(memory.main_ram[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(memory.main_ram[offset + 3U]) << 24U);
    return Status::ok;
}

[[nodiscard]] inline Status load_u64(
    const EeMemory& memory,
    std::uint32_t address,
    std::uint64_t& value
) noexcept {
    std::uint32_t physical{};
    const auto status = resolve_ee_memory_access(memory, address, 8U, 8U, physical);
    if (status != Status::ok) return status;
    const auto offset = static_cast<std::size_t>(physical);
    std::uint64_t loaded{};
    for (std::size_t index = 0; index < 8U; ++index) {
        loaded |= static_cast<std::uint64_t>(memory.main_ram[offset + index]) << (index * 8U);
    }
    value = loaded;
    return Status::ok;
}

[[nodiscard]] inline Status load_u128(
    const EeMemory& memory,
    std::uint32_t address,
    Gpr128& value
) noexcept {
    std::uint32_t physical{};
    const auto aligned_address = address & ~0x0FU;
    const auto status = resolve_ee_memory_access(memory, aligned_address, 16U, 1U, physical);
    if (status != Status::ok) return status;
    const auto offset = static_cast<std::size_t>(physical);
    std::uint64_t low{};
    std::uint64_t high{};
    for (std::size_t index = 0; index < 8U; ++index) {
        low |= static_cast<std::uint64_t>(memory.main_ram[offset + index]) << (index * 8U);
        high |= static_cast<std::uint64_t>(memory.main_ram[offset + 8U + index]) << (index * 8U);
    }
    value.low = low;
    value.high = high;
    return Status::ok;
}

[[nodiscard]] inline Status store_u8(
    EeMemory& memory,
    std::uint32_t address,
    std::uint8_t value
) noexcept {
    std::uint32_t physical{};
    const auto status = resolve_ee_memory_access(memory, address, 1U, 1U, physical);
    if (status != Status::ok) return status;
    memory.main_ram[static_cast<std::size_t>(physical)] = value;
    return Status::ok;
}

[[nodiscard]] inline Status store_u16(
    EeMemory& memory,
    std::uint32_t address,
    std::uint16_t value
) noexcept {
    std::uint32_t physical{};
    const auto status = resolve_ee_memory_access(memory, address, 2U, 2U, physical);
    if (status != Status::ok) return status;
    const auto offset = static_cast<std::size_t>(physical);
    memory.main_ram[offset] = static_cast<std::uint8_t>(value);
    memory.main_ram[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    return Status::ok;
}

[[nodiscard]] inline Status store_u32(
    EeMemory& memory,
    std::uint32_t address,
    std::uint32_t value
) noexcept {
    std::uint32_t physical{};
    const auto status = resolve_ee_memory_access(memory, address, 4U, 4U, physical);
    if (status != Status::ok) return status;
    const auto offset = static_cast<std::size_t>(physical);
    memory.main_ram[offset] = static_cast<std::uint8_t>(value);
    memory.main_ram[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    memory.main_ram[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    memory.main_ram[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
    return Status::ok;
}

[[nodiscard]] inline Status store_u64(
    EeMemory& memory,
    std::uint32_t address,
    std::uint64_t value
) noexcept {
    std::uint32_t physical{};
    const auto status = resolve_ee_memory_access(memory, address, 8U, 8U, physical);
    if (status != Status::ok) return status;
    const auto offset = static_cast<std::size_t>(physical);
    for (std::size_t index = 0; index < 8U; ++index) {
        memory.main_ram[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    return Status::ok;
}

// EE left/right operations align the backing access down and merge the
// selected byte lanes. They intentionally reuse checked aligned accessors.
[[nodiscard]] inline Status load_u32_left(
    const EeMemory& memory,
    std::uint32_t address,
    std::uint64_t previous,
    std::uint64_t& value
) noexcept {
    const auto byte_offset = address & 3U;
    std::uint32_t loaded{};
    const auto status = load_u32(memory, address & ~3U, loaded);
    if (status != Status::ok) return status;
    const auto shift = (3U - byte_offset) * 8U;
    const auto preserve_mask = 0x00FFFFFFU >> (byte_offset * 8U);
    const auto merged = (static_cast<std::uint32_t>(previous) & preserve_mask)
        | (loaded << shift);
    value = static_cast<std::uint64_t>(merged)
        | ((merged & 0x80000000U) != 0U ? 0xFFFFFFFF00000000ULL : 0ULL);
    return Status::ok;
}

[[nodiscard]] inline Status load_u32_right(
    const EeMemory& memory,
    std::uint32_t address,
    std::uint64_t previous,
    std::uint64_t& value
) noexcept {
    const auto byte_offset = address & 3U;
    std::uint32_t loaded{};
    const auto status = load_u32(memory, address & ~3U, loaded);
    if (status != Status::ok) return status;
    const auto shift = byte_offset * 8U;
    const auto preserve_mask = 0xFFFFFF00U << ((3U - byte_offset) * 8U);
    const auto merged = (static_cast<std::uint32_t>(previous) & preserve_mask)
        | (loaded >> shift);
    value = byte_offset == 0U
        ? (static_cast<std::uint64_t>(merged)
            | ((merged & 0x80000000U) != 0U ? 0xFFFFFFFF00000000ULL : 0ULL))
        : ((previous & 0xFFFFFFFF00000000ULL) | static_cast<std::uint64_t>(merged));
    return Status::ok;
}

[[nodiscard]] inline Status store_u32_left(
    EeMemory& memory,
    std::uint32_t address,
    std::uint32_t value
) noexcept {
    const auto byte_offset = address & 3U;
    const auto aligned_address = address & ~3U;
    std::uint32_t previous{};
    const auto load_status = load_u32(memory, aligned_address, previous);
    if (load_status != Status::ok) return load_status;
    const auto shift = (3U - byte_offset) * 8U;
    const auto preserve_mask = 0xFFFFFF00U << (byte_offset * 8U);
    const auto merged = (value >> shift) | (previous & preserve_mask);
    return store_u32(memory, aligned_address, merged);
}

[[nodiscard]] inline Status store_u32_right(
    EeMemory& memory,
    std::uint32_t address,
    std::uint32_t value
) noexcept {
    const auto byte_offset = address & 3U;
    const auto aligned_address = address & ~3U;
    std::uint32_t previous{};
    const auto load_status = load_u32(memory, aligned_address, previous);
    if (load_status != Status::ok) return load_status;
    const auto shift = byte_offset * 8U;
    const auto preserve_mask = 0x00FFFFFFU >> ((3U - byte_offset) * 8U);
    const auto merged = (value << shift) | (previous & preserve_mask);
    return store_u32(memory, aligned_address, merged);
}

[[nodiscard]] inline Status load_u64_left(
    const EeMemory& memory,
    std::uint32_t address,
    std::uint64_t previous,
    std::uint64_t& value
) noexcept {
    const auto byte_offset = address & 7U;
    std::uint64_t loaded{};
    const auto status = load_u64(memory, address & ~7U, loaded);
    if (status != Status::ok) return status;
    const auto shift = (7U - byte_offset) * 8U;
    const auto preserve_mask = 0x00FFFFFFFFFFFFFFULL >> (byte_offset * 8U);
    value = (previous & preserve_mask) | (loaded << shift);
    return Status::ok;
}

[[nodiscard]] inline Status load_u64_right(
    const EeMemory& memory,
    std::uint32_t address,
    std::uint64_t previous,
    std::uint64_t& value
) noexcept {
    const auto byte_offset = address & 7U;
    std::uint64_t loaded{};
    const auto status = load_u64(memory, address & ~7U, loaded);
    if (status != Status::ok) return status;
    const auto shift = byte_offset * 8U;
    const auto preserve_mask = 0xFFFFFFFFFFFFFF00ULL << ((7U - byte_offset) * 8U);
    value = (previous & preserve_mask) | (loaded >> shift);
    return Status::ok;
}

[[nodiscard]] inline Status store_u64_left(
    EeMemory& memory,
    std::uint32_t address,
    std::uint64_t value
) noexcept {
    const auto byte_offset = address & 7U;
    const auto aligned_address = address & ~7U;
    std::uint64_t previous{};
    const auto load_status = load_u64(memory, aligned_address, previous);
    if (load_status != Status::ok) return load_status;
    const auto shift = (7U - byte_offset) * 8U;
    const auto preserve_mask = 0xFFFFFFFFFFFFFF00ULL << (byte_offset * 8U);
    const auto merged = (value >> shift) | (previous & preserve_mask);
    return store_u64(memory, aligned_address, merged);
}

[[nodiscard]] inline Status store_u64_right(
    EeMemory& memory,
    std::uint32_t address,
    std::uint64_t value
) noexcept {
    const auto byte_offset = address & 7U;
    const auto aligned_address = address & ~7U;
    std::uint64_t previous{};
    const auto load_status = load_u64(memory, aligned_address, previous);
    if (load_status != Status::ok) return load_status;
    const auto shift = byte_offset * 8U;
    const auto preserve_mask = 0x00FFFFFFFFFFFFFFULL >> ((7U - byte_offset) * 8U);
    const auto merged = (value << shift) | (previous & preserve_mask);
    return store_u64(memory, aligned_address, merged);
}

[[nodiscard]] inline Status store_u128(
    EeMemory& memory,
    std::uint32_t address,
    const Gpr128& value
) noexcept {
    std::uint32_t physical{};
    const auto aligned_address = address & ~0x0FU;
    const auto status = resolve_ee_memory_access(memory, aligned_address, 16U, 1U, physical);
    if (status != Status::ok) return status;
    const auto offset = static_cast<std::size_t>(physical);
    for (std::size_t index = 0; index < 8U; ++index) {
        memory.main_ram[offset + index] = static_cast<std::uint8_t>(value.low >> (index * 8U));
        memory.main_ram[offset + 8U + index] = static_cast<std::uint8_t>(value.high >> (index * 8U));
    }
    return Status::ok;
}

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

[[nodiscard]] constexpr std::uint64_t arithmetic_shift_right_doubleword(
    std::uint64_t value,
    std::uint8_t amount
) noexcept {
    if (amount == 0U) {
        return value;
    }
    if (amount >= 64U) {
        return (value & 0x8000000000000000ULL) != 0U
            ? 0xFFFFFFFFFFFFFFFFULL
            : 0ULL;
    }

    const auto shifted = value >> amount;
    if ((value & 0x8000000000000000ULL) == 0U) {
        return shifted;
    }
    return shifted | (0xFFFFFFFFFFFFFFFFULL << (64U - amount));
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
