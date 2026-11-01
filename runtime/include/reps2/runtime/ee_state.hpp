// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <bit>
#include <cstdint>
#include <span>

#include "reps2/runtime/ee_fpu.hpp"

namespace reps2::runtime {

struct Gpr128 {
    std::uint64_t low{};
    std::uint64_t high{};
};

inline constexpr std::uint32_t kEeMainRamSize = 32U * 1024U * 1024U;

struct EeMemory {
    std::span<std::uint8_t> main_ram{};
};

// FCR31 (the condition, cause and sticky flags plus their hardwired fields) is
// declared in ee_fpu.hpp, next to the arithmetic that sets it. FIR (FCR0) is
// read-only: implementation 0x2E in bits 15..8, revision 0 in bits 7..0, and
// zero above. Tests assert the literal, not this constant, so a wrong value
// cannot pass by agreeing with the code that emits it.
inline constexpr std::uint32_t kFpuImplementationRegister{0x0000'2E00U};

// COP0 Processor Revision ID for Emotion Engine (R5900):
// Implementation 0x2E in bits 15..8, revision 0x20 in bits 7..0.
inline constexpr std::uint32_t kEeProcessorId{0x0000'2E20U};

inline constexpr std::uint8_t kCop0Index = 0;
inline constexpr std::uint8_t kCop0Random = 1;
inline constexpr std::uint8_t kCop0EntryLo0 = 2;
inline constexpr std::uint8_t kCop0EntryLo1 = 3;
inline constexpr std::uint8_t kCop0Context = 4;
inline constexpr std::uint8_t kCop0PageMask = 5;
inline constexpr std::uint8_t kCop0Wired = 6;
inline constexpr std::uint8_t kCop0BadVAddr = 8;
inline constexpr std::uint8_t kCop0Count = 9;
inline constexpr std::uint8_t kCop0EntryHi = 10;
inline constexpr std::uint8_t kCop0Compare = 11;
inline constexpr std::uint8_t kCop0Status = 12;
inline constexpr std::uint8_t kCop0Cause = 13;
inline constexpr std::uint8_t kCop0Epc = 14;
inline constexpr std::uint8_t kCop0PrId = 15;
inline constexpr std::uint8_t kCop0Config = 16;
inline constexpr std::uint8_t kCop0BadPAddr = 24;
inline constexpr std::uint8_t kCop0TagLo = 28;
inline constexpr std::uint8_t kCop0TagHi = 29;
inline constexpr std::uint8_t kCop0ErrorEpc = 30;

// COP0 Status bits used by the currently implemented exception path.
inline constexpr std::uint32_t kCop0StatusExlMask{0x0000'0002U};
inline constexpr std::uint32_t kCop0StatusErlMask{0x0000'0004U};
inline constexpr std::uint32_t kCop0StatusEieMask{0x0001'0000U};
inline constexpr std::uint32_t kCop0StatusBevMask{0x0040'0000U};

// COP0 Cause fields and EE vector addresses for synchronous exceptions.
inline constexpr std::uint32_t kCop0CauseExcCodeMask{0x0000'007CU};
inline constexpr std::uint32_t kCop0CauseBranchDelayMask{0x8000'0000U};
// V_COMMON handles synchronous general exceptions (including SYSCALL, BREAK
// and OVERFLOW); V_INTERRUPT is a distinct vector and is not used here.
inline constexpr std::uint32_t kEeCommonExceptionVector{0x8000'0180U};
inline constexpr std::uint32_t kEeBootstrapCommonExceptionVector{0xBFC0'0380U};
inline constexpr std::uint32_t kEeInterruptVector{0x8000'0200U};
inline constexpr std::uint32_t kEeBootstrapInterruptVector{0xBFC0'0400U};

enum class ExceptionCode : std::uint8_t {
    SystemCall = 8,
    Breakpoint = 9,
    Overflow = 12,
};

/// Level-1 data-address exceptions raised by naturally misaligned accesses.
enum class AddressErrorCode : std::uint8_t { Load = 4, Store = 5 };

struct EeState {
    std::uint32_t pc{};
    std::array<Gpr128, 32> gpr{};
    // Base EE HI/LO state; MMI pipeline-1 HI1/LO1 is modeled separately.
    std::uint64_t hi{};
    std::uint64_t lo{};
    std::uint64_t hi1{};
    std::uint64_t lo1{};
    // MMI SA holds a bit count consumed by QFSRV. MTSA transfers the full
    // 64-bit source lane, so the model keeps 64 bits rather than a byte.
    std::uint64_t sa{};
    // The EE FPU is single-precision only: 32 x 32-bit FPRs, plus FCR0 (FIR,
    // read-only) and FCR31 (FCSR). FCR1..FCR30 are reserved and store nothing.
    // ACC is a separate 32-bit register, not FPR[32]: section 8.2 gives it no
    // COP1 address, so only ADDA/SUBA/MULA/MADDA/MSUBA write it and only
    // MADD/MSUB/MADDA/MSUBA read it.
    std::array<std::uint32_t, 32> fpr{};
    std::uint32_t acc{};
    std::uint32_t fcsr{kFpuControlResetValue};
    std::array<std::uint32_t, 32> cop0{};
    EeMemory memory{};
};

enum class Status : std::uint8_t {
    ok,
    undefined_word_operand,
    // Legacy diagnostic retained for ABI compatibility; current generated
    // signed arithmetic raises ExceptionCode::Overflow instead.
    integer_overflow,
    instruction_budget_exhausted,
    external_control_flow,
    unsupported_instruction,
    address_error,
    unmapped_memory,
    unsupported_memory_access,
    invalid_memory_image,
    // Architectural traps, not unknown encodings. Execution stops with the PC
    // pointing at the trapping instruction; the 20-bit code field is bits
    // 25..6 of the word at that PC. HLE service delivery is a later layer.
    syscall,
    breakpoint,
    // A Level-1 guest exception was entered and COP0/PC now hold its context.
    exception_delivered,
    // ERET was executed without exactly one valid EXL/ERL return context.
    invalid_exception_return,
};

[[nodiscard]] constexpr bool is_guest_trap(Status status) noexcept {
    return status == Status::syscall || status == Status::breakpoint
        || status == Status::exception_delivered;
}

// Enter the EE's general Level-1 vector for synchronous exceptions. The caller
// sets state.pc to the faulting instruction before calling this helper.
// For a delay-slot fault, state.pc is the delay-slot PC and BD/EPC identify the
// owning branch. Nested exceptions preserve the first EPC/BD frame.
[[nodiscard]] inline Status raise_ee_exception(
    EeState& state,
    ExceptionCode code,
    bool in_branch_delay = false
) noexcept {
    auto& status = state.cop0[kCop0Status];
    auto& cause = state.cop0[kCop0Cause];
    const bool already_exl = (status & kCop0StatusExlMask) != 0U;

    cause = (cause & ~kCop0CauseExcCodeMask)
        | ((static_cast<std::uint32_t>(code) << 2U) & kCop0CauseExcCodeMask);

    // A nested Level-1 exception updates ExcCode but preserves the first
    // exception's restart address and BD bit.
    if (!already_exl) {
        state.cop0[kCop0Epc] = in_branch_delay ? state.pc - 4U : state.pc;
        if (in_branch_delay) {
            cause |= kCop0CauseBranchDelayMask;
        } else {
            cause &= ~kCop0CauseBranchDelayMask;
        }
        status |= kCop0StatusExlMask;
    }

    state.pc = (status & kCop0StatusBevMask) != 0U
        ? kEeBootstrapCommonExceptionVector
        : kEeCommonExceptionVector;
    return Status::exception_delivered;
}

// Address errors publish the offending guest virtual address to BadVAddr.
// Only checked natural-alignment faults are routed here; translation, bus and
/// device failures retain their distinct host/runtime statuses.
[[nodiscard]] inline Status raise_ee_address_error(
    EeState& state, AddressErrorCode code, std::uint32_t bad_virtual_address,
    bool in_branch_delay = false
) noexcept {
    state.cop0[kCop0BadVAddr] = bad_virtual_address;
    return raise_ee_exception(state, static_cast<ExceptionCode>(code), in_branch_delay);
}

// ERET restores one unambiguous saved exception context. Sony's EE manual
// documents EXL+ERL simultaneously set as an invalid ERET condition; reject it
// (and an empty context) rather than inventing a priority.
[[nodiscard]] inline Status return_from_ee_exception(EeState& state) noexcept {
    auto& status = state.cop0[kCop0Status];
    const bool exl = (status & kCop0StatusExlMask) != 0U;
    const bool erl = (status & kCop0StatusErlMask) != 0U;
    if (exl == erl) {
        return Status::invalid_exception_return;
    }

    if (erl) {
        state.pc = state.cop0[kCop0ErrorEpc];
        status &= ~kCop0StatusErlMask;
    } else {
        state.pc = state.cop0[kCop0Epc];
        status &= ~kCop0StatusExlMask;
    }
    return Status::ok;
}

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
    state.hi1 = 0;
    state.lo1 = 0;
    state.sa = 0;
    state.fpr = {};
    state.acc = 0;
    state.fcsr = kFpuControlResetValue;
    state.cop0 = {};
    state.cop0[kCop0PrId] = kEeProcessorId;
    state.cop0[kCop0Status] = 0x1040'0004U;
    state.pc = entry_pc;
    return Status::ok;
}

// CTC1 can program only C and the eight flag bits: bits 31..25, 22..18 and 13..7
// read as zero and bit 24 always reads as one, so a store must not be able to
// persist anything else. Rounding is fixed toward zero, which is why bits 1..0
// read back 0b01.
constexpr void store_fpu_control(std::uint32_t& control, std::uint32_t value) noexcept {
    control = (value & kFpuControlWritableMask) | kFpuControlResetValue;
}

[[nodiscard]] inline std::uint32_t read_cop0(const EeState& state, std::uint8_t reg) noexcept {
    if (reg == kCop0PrId) {
        return kEeProcessorId;
    }
    if (reg < 32U) {
        return state.cop0[reg];
    }
    return 0U;
}

inline void write_cop0(EeState& state, std::uint8_t reg, std::uint32_t value) noexcept {
    if (reg == kCop0PrId) {
        return; // PRId is read-only
    }
    if (reg < 32U) {
        state.cop0[reg] = value;
    }
}

inline void enable_interrupts(EeState& state) noexcept {
    state.cop0[kCop0Status] |= kCop0StatusEieMask;
}

inline void disable_interrupts(EeState& state) noexcept {
    state.cop0[kCop0Status] &= ~kCop0StatusEieMask;
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

[[nodiscard]] constexpr std::uint32_t saturating_add_u32(
    std::uint32_t lhs,
    std::uint32_t rhs
) noexcept {
    const auto sum = static_cast<std::uint64_t>(lhs) + static_cast<std::uint64_t>(rhs);
    return sum > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<std::uint32_t>(sum);
}

// PADDUW adds four corresponding unsigned 32-bit lanes with unsigned
// saturation. Lanes are independent: no carry crosses a 32-bit boundary.
[[nodiscard]] constexpr Gpr128 parallel_add_u32(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    const auto lane0 = saturating_add_u32(
        static_cast<std::uint32_t>(lhs.low),
        static_cast<std::uint32_t>(rhs.low)
    );
    const auto lane1 = saturating_add_u32(
        static_cast<std::uint32_t>(lhs.low >> 32U),
        static_cast<std::uint32_t>(rhs.low >> 32U)
    );
    const auto lane2 = saturating_add_u32(
        static_cast<std::uint32_t>(lhs.high),
        static_cast<std::uint32_t>(rhs.high)
    );
    const auto lane3 = saturating_add_u32(
        static_cast<std::uint32_t>(lhs.high >> 32U),
        static_cast<std::uint32_t>(rhs.high >> 32U)
    );
    return Gpr128{
        static_cast<std::uint64_t>(lane0) | (static_cast<std::uint64_t>(lane1) << 32U),
        static_cast<std::uint64_t>(lane2) | (static_cast<std::uint64_t>(lane3) << 32U),
    };
}

// QFSRV shifts the 256-bit funnel formed by GPR[rs]:GPR[rt] right by the SA
// bit count and keeps the low 128 bits, so result bit i is funnel bit i + sa.
// Counts above 127 therefore still draw bits from the rs operand.
[[nodiscard]] constexpr std::uint64_t funnel_shift_word(
    std::array<std::uint64_t, 4> funnel,
    std::size_t index,
    std::size_t word_shift,
    unsigned bit_shift
) noexcept {
    const auto at = [&](std::size_t position) noexcept {
        return position < funnel.size() ? funnel[position] : 0ULL;
    };
    const auto low_part = at(index + word_shift) >> bit_shift;
    const auto high_part = bit_shift == 0U
        ? 0U
        : at(index + word_shift + 1U) << (64U - bit_shift);
    return low_part | high_part;
}

[[nodiscard]] constexpr Gpr128 funnel_shift_right_vec(
    const Gpr128& upper,
    const Gpr128& lower,
    std::uint64_t sa
) noexcept {
    if (sa >= 256U) {
        return Gpr128{};
    }
    const std::array<std::uint64_t, 4> funnel{lower.low, lower.high, upper.low, upper.high};
    const auto word_shift = static_cast<std::size_t>(sa / 64U);
    const auto bit_shift = static_cast<unsigned>(sa % 64U);
    return Gpr128{
        funnel_shift_word(funnel, 0U, word_shift, bit_shift),
        funnel_shift_word(funnel, 1U, word_shift, bit_shift),
    };
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
