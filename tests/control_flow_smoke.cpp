// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute_cfg(
    reps2::runtime::EeState& state,
    std::uint64_t instruction_budget
) noexcept;
}

int main() {
    using reps2::runtime::EeState;
    using reps2::runtime::Status;

    const auto fail = [](int code) {
        std::fprintf(stderr, "control-flow/memory smoke failed at assertion %d\n", code);
        return code;
    };

    // Conditional branch not taken: the delay slot and fallthrough block run,
    // then a direct jump's delay slot runs before the return.
    EeState not_taken{};
    not_taken.pc = 0x1000;
    not_taken.gpr[8].low = 1;
    not_taken.gpr[31].low = 0x1020;
    const auto not_taken_status = reps2::generated::execute_cfg(not_taken, 64);
    if (not_taken_status != Status::external_control_flow || not_taken.pc != 0x1020) {
        return fail(1);
    }
    if (not_taken.gpr[9].low != 7 || not_taken.gpr[10].low != 5) {
        return fail(2);
    }
    if (not_taken.gpr[11].low != 9 || not_taken.gpr[12].low != 0) {
        return fail(3);
    }
    if (not_taken.gpr[13].low != 13) {
        return fail(4);
    }

    // Conditional branch taken: delay slot still runs, the fallthrough block
    // is skipped, and the target block reaches the same return.
    EeState taken{};
    taken.pc = 0x1000;
    taken.gpr[8].low = 0;
    taken.gpr[31].low = 0x1020;
    const auto taken_status = reps2::generated::execute_cfg(taken, 64);
    if (taken_status != Status::external_control_flow || taken.pc != 0x1020) {
        return fail(5);
    }
    if (taken.gpr[9].low != 7 || taken.gpr[10].low != 0) {
        return fail(6);
    }
    if (taken.gpr[11].low != 0 || taken.gpr[12].low != 11) {
        return fail(7);
    }
    if (taken.gpr[13].low != 13) {
        return fail(8);
    }

    // A block is admitted atomically by the instruction budget. The branch
    // plus its delay slot requires two guest instructions.
    EeState limited{};
    limited.pc = 0x1000;
    limited.gpr[8].low = 0;
    const auto budget_status = reps2::generated::execute_cfg(limited, 1);
    if (budget_status != Status::instruction_budget_exhausted || limited.pc != 0x1000) {
        return fail(9);
    }
    if (limited.gpr[9].low != 0) {
        return fail(10);
    }

    // JAL writes PC+8, executes its delay slot, enters the callee, and JR RA
    // executes its own delay slot before returning to the continuation block.
    EeState call{};
    call.pc = 0x2000;
    const auto call_status = reps2::generated::execute_cfg(call, 64);
    if (call_status != Status::unsupported_instruction || call.pc != 0x2008) {
        return fail(11);
    }
    if (call.gpr[31].low != 0x2008) {
        return fail(12);
    }
    if (call.gpr[14].low != 17 || call.gpr[15].low != 19) {
        return fail(13);
    }

    // BLTZAL writes the link even when its predicate is false.
    EeState link_not_taken{};
    link_not_taken.pc = 0x3000;
    link_not_taken.gpr[8].low = 1;
    const auto link_not_taken_status = reps2::generated::execute_cfg(link_not_taken, 64);
    if (link_not_taken_status != Status::unsupported_instruction || link_not_taken.pc != 0x3008) {
        return fail(14);
    }
    if (link_not_taken.gpr[31].low != 0x3008 || link_not_taken.gpr[9].low != 23) {
        return fail(15);
    }

    // The same branch-and-link writes PC+8 when taken.
    EeState link_taken{};
    link_taken.pc = 0x3000;
    link_taken.gpr[8].low = 0xFFFFFFFFFFFFFFFFULL;
    const auto link_taken_status = reps2::generated::execute_cfg(link_taken, 64);
    if (link_taken_status != Status::unsupported_instruction || link_taken.pc != 0x300C) {
        return fail(16);
    }
    if (link_taken.gpr[31].low != 0x3008 || link_taken.gpr[9].low != 23) {
        return fail(17);
    }

    // JALR with rd == rs must jump to the captured old source value even
    // though that GPR is overwritten with the link address.
    EeState jalr_alias{};
    jalr_alias.pc = 0x4000;
    jalr_alias.gpr[8].low = 0x6000;
    const auto jalr_status = reps2::generated::execute_cfg(jalr_alias, 64);
    if (jalr_status != Status::external_control_flow || jalr_alias.pc != 0x6000) {
        return fail(18);
    }
    if (jalr_alias.gpr[8].low != 0x4008 || jalr_alias.gpr[9].low != 31) {
        return fail(19);
    }

    // Load/store operations execute against caller-owned 32 MiB EE RAM.
    // All documented main-RAM aliases must refer to the same backing bytes.
    std::vector<std::uint8_t> main_ram(reps2::runtime::kEeMainRamSize);
    const std::array<std::uint32_t, 5> virtual_bases{
        0x00001000U, // cached kuseg
        0x20001000U, // uncached alias
        0x80001000U, // KSEG0 direct map
        0xA0001000U, // KSEG1 direct map
        0x30101000U, // accelerated alias of physical 0x00101000
    };
    const std::array<std::size_t, 5> physical_offsets{
        0x1000U, 0x1000U, 0x1000U, 0x1000U, 0x101000U
    };
    for (std::size_t index = 0; index < virtual_bases.size(); ++index) {
        const auto physical = physical_offsets[index];
        std::fill(main_ram.begin() + static_cast<std::ptrdiff_t>(physical),
                  main_ram.begin() + static_cast<std::ptrdiff_t>(physical + 12U), std::uint8_t{0});

        EeState memory_state{};
        memory_state.pc = 0x5000U;
        memory_state.memory.main_ram = main_ram;
        memory_state.gpr[8].low = virtual_bases[index] + 4U;
        memory_state.gpr[9].low = 0xFFFFFFFF80FF8001ULL;
        memory_state.gpr[10].high = 0xAAAAAAAAAAAAAAAAULL;
        memory_state.gpr[12].high = 0xBBBBBBBBBBBBBBBBULL;
        memory_state.gpr[31].low = 0x9000U;

        const auto memory_status = reps2::generated::execute_cfg(memory_state, 64U);
        if (memory_status != Status::external_control_flow || memory_state.pc != 0x9000U) {
            std::fprintf(stderr, "memory status failure alias[%zu]: status=%u pc=0x%08X\n", index, static_cast<unsigned>(memory_status), memory_state.pc);
            return fail(20);
        }
        if (memory_state.gpr[10].low != 0xFFFFFFFFFFFFFF80ULL || memory_state.gpr[11].low != 0xFFU) {
            std::fprintf(stderr, "byte load mismatch alias[%zu]: lb=0x%016llX lbu=0x%016llX\n", index, static_cast<unsigned long long>(memory_state.gpr[10].low), static_cast<unsigned long long>(memory_state.gpr[11].low));
            return fail(21);
        }
        if (memory_state.gpr[12].low != 0xFFFFFFFFFFFF80FFULL
            || memory_state.gpr[13].low != 0x80FFULL) {
            std::fprintf(stderr, "halfword load mismatch alias[%zu]: lh=0x%016llX lhu=0x%016llX\n", index, static_cast<unsigned long long>(memory_state.gpr[12].low), static_cast<unsigned long long>(memory_state.gpr[13].low));
            return fail(22);
        }
        if (memory_state.gpr[14].low != 0xFFFFFFFF80FF8001ULL
            || memory_state.gpr[15].low != 0x80FF8001ULL) {
            std::fprintf(stderr, "word load mismatch alias[%zu]: lw=0x%016llX lwu=0x%016llX\n", index, static_cast<unsigned long long>(memory_state.gpr[14].low), static_cast<unsigned long long>(memory_state.gpr[15].low));
            return fail(23);
        }
        if (memory_state.gpr[10].high != 0xAAAAAAAAAAAAAAAAULL
            || memory_state.gpr[12].high != 0xBBBBBBBBBBBBBBBBULL) {
            std::fprintf(stderr, "GPR high-lane preservation failure alias[%zu]\n", index);
            return fail(24);
        }
        if (main_ram[physical] != 0x01U || main_ram[physical + 1U] != 0x80U
            || main_ram[physical + 2U] != 0xFFU || main_ram[physical + 3U] != 0x80U) {
            std::fprintf(stderr, "initial SW bytes mismatch alias[%zu]: %02X %02X %02X %02X\n", index, main_ram[physical], main_ram[physical + 1U], main_ram[physical + 2U], main_ram[physical + 3U]);
            return fail(25);
        }
        if (main_ram[physical + 4U] != 0x01U || main_ram[physical + 6U] != 0x01U
            || main_ram[physical + 7U] != 0x80U || main_ram[physical + 8U] != 0x01U
            || main_ram[physical + 11U] != 0x80U) {
            std::fprintf(stderr, "SB/SH/SW bytes mismatch alias[%zu]: %02X %02X %02X %02X %02X\n", index, main_ram[physical + 4U], main_ram[physical + 6U], main_ram[physical + 7U], main_ram[physical + 8U], main_ram[physical + 11U]);
            return fail(26);
        }
    }

    // Halfword/word alignment faults must preserve the destination register
    // and report the exact guest instruction PC.
    EeState unaligned{};
    unaligned.pc = 0x5100U;
    unaligned.memory.main_ram = main_ram;
    unaligned.gpr[8].low = 0x1001U;
    unaligned.gpr[14].low = 0x123456789ABCDEF0ULL;
    unaligned.gpr[14].high = 0x0BADF00D0BADF00DULL;
    const auto alignment_status = reps2::generated::execute_cfg(unaligned, 8U);
    if (alignment_status != Status::address_error || unaligned.pc != 0x5100U) {
        std::fprintf(stderr, "alignment status mismatch: status=%u pc=0x%08X\n", static_cast<unsigned>(alignment_status), unaligned.pc);
        return fail(27);
    }
    if (unaligned.gpr[14].low != 0x123456789ABCDEF0ULL
        || unaligned.gpr[14].high != 0x0BADF00D0BADF00DULL) {
        std::fprintf(stderr, "alignment fault modified LW destination\n");
        return fail(28);
    }

    // MMIO and hardware regions are not silently modelled as RAM.
    EeState mmio{};
    mmio.pc = 0x5000U;
    mmio.memory.main_ram = main_ram;
    mmio.gpr[8].low = 0x10000004U;
    mmio.gpr[9].low = 0xDEADBEEFULL;
    const auto mmio_status = reps2::generated::execute_cfg(mmio, 64U);
    if (mmio_status != Status::unsupported_memory_access || mmio.pc != 0x5000U) {
        std::fprintf(stderr, "MMIO status mismatch: status=%u pc=0x%08X\n", static_cast<unsigned>(mmio_status), mmio.pc);
        return fail(29);
    }

    // Unmapped physical gaps fail distinctly from known-but-unimplemented devices.
    EeState unmapped{};
    unmapped.pc = 0x5000U;
    unmapped.memory.main_ram = main_ram;
    unmapped.gpr[8].low = 0x02000004U;
    const auto unmapped_status = reps2::generated::execute_cfg(unmapped, 64U);
    if (unmapped_status != Status::unmapped_memory || unmapped.pc != 0x5000U) {
        std::fprintf(stderr, "unmapped status mismatch: status=%u pc=0x%08X\n", static_cast<unsigned>(unmapped_status), unmapped.pc);
        return fail(30);
    }

    // The backing span is authoritative: an otherwise valid RAM address
    // outside a caller-supplied short span is still rejected safely.
    std::array<std::uint8_t, 16> short_ram{};
    EeState short_backing{};
    short_backing.pc = 0x5000U;
    short_backing.memory.main_ram = short_ram;
    short_backing.gpr[8].low = 0x1000U;
    const auto short_backing_status = reps2::generated::execute_cfg(short_backing, 64U);
    if (short_backing_status != Status::unmapped_memory || short_backing.pc != 0x5000U) {
        std::fprintf(stderr, "short-backing status mismatch: status=%u pc=0x%08X\n", static_cast<unsigned>(short_backing_status), short_backing.pc);
        return fail(31);
    }

    return 0;
}
