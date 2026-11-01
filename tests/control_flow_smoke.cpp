// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <span>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute_cfg(
    reps2::runtime::EeState& state,
    std::uint64_t instruction_budget
) noexcept;
[[nodiscard]] reps2::runtime::Status initialize_ee_state(
    reps2::runtime::EeState& state,
    std::span<const std::uint8_t> main_ram_image
) noexcept;
[[nodiscard]] reps2::runtime::Status initialize_ee_state(
    reps2::runtime::EeState& state,
    std::span<const std::uint8_t> main_ram_image
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
    if (call_status != Status::syscall || call.pc != 0x2008) {
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
    if (link_not_taken_status != Status::syscall || link_not_taken.pc != 0x3008) {
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
    if (link_taken_status != Status::syscall || link_taken.pc != 0x300C) {
        return fail(16);
    }
    if (link_taken.gpr[31].low != 0x3008 || link_taken.gpr[9].low != 23) {
        return fail(17);
    }

    // BREAK is an architectural guest trap, distinct from an unknown
    // encoding. The PC stays on the trapping instruction.
    EeState break_trap{};
    break_trap.pc = 0x8000U;
    const auto break_status = reps2::generated::execute_cfg(break_trap, 64U);
    if (break_status != Status::breakpoint || break_trap.pc != 0x8000U) {
        std::fprintf(stderr, "BREAK trap status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(break_status), break_trap.pc);
        return fail(88);
    }
    if (!reps2::runtime::is_guest_trap(Status::syscall)
        || !reps2::runtime::is_guest_trap(Status::breakpoint)
        || reps2::runtime::is_guest_trap(Status::unsupported_instruction)) {
        std::fprintf(stderr, "guest-trap status classification mismatch\n");
        return fail(89);
    }

    // A branch-likely executes its delay slot only on the taken path.
    EeState likely_taken{};
    likely_taken.pc = 0x3100U;
    likely_taken.gpr[8].low = 0;
    likely_taken.gpr[31].low = 0x9000U;
    const auto likely_taken_status = reps2::generated::execute_cfg(likely_taken, 64U);
    if (likely_taken_status != Status::external_control_flow || likely_taken.pc != 0x9000U) {
        std::fprintf(stderr, "taken branch-likely status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(likely_taken_status), likely_taken.pc);
        return fail(56);
    }
    if (likely_taken.gpr[9].low != 7 || likely_taken.gpr[10].low != 0
        || likely_taken.gpr[11].low != 11 || likely_taken.gpr[15].low != 0
        || likely_taken.gpr[13].low != 13) {
        std::fprintf(stderr, "taken branch-likely register mismatch\n");
        return fail(57);
    }

    EeState likely_not_taken{};
    likely_not_taken.pc = 0x3100U;
    likely_not_taken.gpr[8].low = 1;
    likely_not_taken.gpr[31].low = 0x9000U;
    const auto likely_not_taken_status = reps2::generated::execute_cfg(likely_not_taken, 64U);
    if (likely_not_taken_status != Status::external_control_flow || likely_not_taken.pc != 0x9000U) {
        std::fprintf(stderr, "not-taken branch-likely status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(likely_not_taken_status), likely_not_taken.pc);
        return fail(58);
    }
    if (likely_not_taken.gpr[9].low != 0 || likely_not_taken.gpr[10].low != 5
        || likely_not_taken.gpr[11].low != 0 || likely_not_taken.gpr[15].low != 9
        || likely_not_taken.gpr[13].low != 13) {
        std::fprintf(stderr, "not-taken branch-likely executed its annulled delay slot\n");
        return fail(59);
    }

    // Link-likely writes PC+8 unconditionally, even when the branch is not
    // taken and the delay slot is annulled.
    EeState likely_link_taken{};
    likely_link_taken.pc = 0x3200U;
    likely_link_taken.gpr[8].low = 0xFFFFFFFFFFFFFFFFULL;
    likely_link_taken.gpr[30].low = 0x9000U;
    const auto likely_link_taken_status = reps2::generated::execute_cfg(likely_link_taken, 64U);
    if (likely_link_taken_status != Status::external_control_flow || likely_link_taken.pc != 0x9000U) {
        std::fprintf(stderr, "taken link-likely status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(likely_link_taken_status), likely_link_taken.pc);
        return fail(60);
    }
    if (likely_link_taken.gpr[31].low != 0x3208U || likely_link_taken.gpr[9].low != 7
        || likely_link_taken.gpr[10].low != 0 || likely_link_taken.gpr[11].low != 11
        || likely_link_taken.gpr[15].low != 0 || likely_link_taken.gpr[13].low != 13) {
        std::fprintf(stderr, "taken link-likely register mismatch\n");
        return fail(61);
    }

    EeState likely_link_not_taken{};
    likely_link_not_taken.pc = 0x3200U;
    likely_link_not_taken.gpr[8].low = 0;
    likely_link_not_taken.gpr[30].low = 0x9000U;
    const auto likely_link_not_taken_status = reps2::generated::execute_cfg(likely_link_not_taken, 64U);
    if (likely_link_not_taken_status != Status::external_control_flow || likely_link_not_taken.pc != 0x9000U) {
        std::fprintf(stderr, "not-taken link-likely status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(likely_link_not_taken_status), likely_link_not_taken.pc);
        return fail(62);
    }
    if (likely_link_not_taken.gpr[31].low != 0x3208U || likely_link_not_taken.gpr[9].low != 0
        || likely_link_not_taken.gpr[10].low != 5 || likely_link_not_taken.gpr[11].low != 0
        || likely_link_not_taken.gpr[15].low != 9 || likely_link_not_taken.gpr[13].low != 13) {
        std::fprintf(stderr, "not-taken link-likely did not preserve unconditional link write\n");
        return fail(63);
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
                  main_ram.begin() + static_cast<std::ptrdiff_t>(physical + 16U), std::uint8_t{0});
        std::fill(main_ram.begin() + static_cast<std::ptrdiff_t>(physical + 0x20U),
                  main_ram.begin() + static_cast<std::ptrdiff_t>(physical + 0x30U), std::uint8_t{0});
        std::fill(main_ram.begin() + static_cast<std::ptrdiff_t>(physical + 0x38U),
                  main_ram.begin() + static_cast<std::ptrdiff_t>(physical + 0x41U), std::uint8_t{0xCC});

        EeState memory_state{};
        memory_state.pc = 0x5000U;
        memory_state.memory.main_ram = main_ram;
        memory_state.gpr[8].low = virtual_bases[index] + 4U;
        memory_state.gpr[9].low = 0xFFFFFFFF80FF8001ULL;
        memory_state.gpr[10].high = 0xAAAAAAAAAAAAAAAAULL;
        memory_state.gpr[12].high = 0xBBBBBBBBBBBBBBBBULL;
        memory_state.gpr[17].high = 0xCCCCCCCCCCCCCCCCULL;
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
        if (memory_state.gpr[16].low != 0x8001000180FF8001ULL
            || memory_state.gpr[16].high != 0x0000000080FF8001ULL) {
            std::fprintf(stderr, "LQ lane mismatch alias[%zu]: low=0x%016llX high=0x%016llX\n",
                index, static_cast<unsigned long long>(memory_state.gpr[16].low),
                static_cast<unsigned long long>(memory_state.gpr[16].high));
            return fail(35);
        }
        if (memory_state.gpr[17].low != 0x0000000080FF8001ULL
            || memory_state.gpr[17].high != 0xCCCCCCCCCCCCCCCCULL) {
            std::fprintf(stderr, "LD did not load only the low GPR lane alias[%zu]\n", index);
            return fail(38);
        }
        const std::array<std::uint8_t, 8> expected_doubleword{
            0x01U, 0x80U, 0xFFU, 0x80U, 0x00U, 0x00U, 0x00U, 0x00U
        };
        for (std::size_t byte = 0; byte < expected_doubleword.size(); ++byte) {
            if (main_ram[physical + 0x38U + byte] != expected_doubleword[byte]) {
                std::fprintf(stderr, "SD byte mismatch alias[%zu] byte[%zu]\n", index, byte);
                return fail(39);
            }
        }
        if (main_ram[physical + 0x40U] != 0xCCU) {
            std::fprintf(stderr, "SD wrote outside its 8-byte destination alias[%zu]\n", index);
            return fail(40);
        }
        for (std::size_t byte = 0; byte < 16U; ++byte) {
            if (main_ram[physical + 0x20U + byte] != main_ram[physical + byte]) {
                std::fprintf(stderr, "SQ byte mismatch alias[%zu] byte[%zu]\n", index, byte);
                return fail(36);
            }
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

    // LD uses natural 8-byte alignment and must leave both GPR lanes intact on a fault.
    EeState unaligned_doubleword{};
    unaligned_doubleword.pc = 0x5200U;
    unaligned_doubleword.memory.main_ram = main_ram;
    unaligned_doubleword.gpr[8].low = 0x1004U;
    unaligned_doubleword.gpr[14].low = 0x123456789ABCDEF0ULL;
    unaligned_doubleword.gpr[14].high = 0x0BADF00D0BADF00DULL;
    const auto doubleword_alignment_status = reps2::generated::execute_cfg(unaligned_doubleword, 8U);
    if (doubleword_alignment_status != Status::address_error || unaligned_doubleword.pc != 0x5200U
        || unaligned_doubleword.gpr[14].low != 0x123456789ABCDEF0ULL
        || unaligned_doubleword.gpr[14].high != 0x0BADF00D0BADF00DULL) {
        std::fprintf(stderr, "LD alignment fault was not precise: status=%u pc=0x%08X\n",
            static_cast<unsigned>(doubleword_alignment_status), unaligned_doubleword.pc);
        return fail(41);
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
    reps2::runtime::Gpr128 short_quadword{};
    const auto short_quadword_status = reps2::runtime::load_u128(short_backing.memory, 0x10U, short_quadword);
    if (short_quadword_status != Status::unmapped_memory) {
        std::fprintf(stderr, "short quadword backing status mismatch: %u\n", static_cast<unsigned>(short_quadword_status));
        return fail(37);
    }
    std::uint64_t short_doubleword = 0x1122334455667788ULL;
    if (reps2::runtime::load_u64(short_backing.memory, 0x10U, short_doubleword) != Status::unmapped_memory
        || short_doubleword != 0x1122334455667788ULL) {
        std::fprintf(stderr, "short LD backing span was not rejected atomically\n");
        return fail(42);
    }
    if (reps2::runtime::store_u64(short_backing.memory, 0x10U, 0x8877665544332211ULL) != Status::unmapped_memory
        || std::any_of(short_ram.begin(), short_ram.end(), [](std::uint8_t byte) { return byte != 0; })) {
        std::fprintf(stderr, "short SD backing span was not rejected atomically\n");
        return fail(43);
    }
    if (reps2::runtime::store_u64(short_backing.memory, 0x11U, 0x8877665544332211ULL) != Status::address_error) {
        std::fprintf(stderr, "unaligned SD did not report an address error\n");
        return fail(44);
    }

    // Generated LWL/LWR/SWL/SWR instructions exercise all four byte lanes.
    EeState unaligned_words{};
    unaligned_words.pc = 0x5300U;
    unaligned_words.memory.main_ram = main_ram;
    unaligned_words.gpr[8].low = 0x6000U;
    unaligned_words.gpr[9].low = 0x89ABCDEFU;
    unaligned_words.gpr[31].low = 0x9000U;
    constexpr std::uint64_t old_word_register = 0x1122334455667788ULL;
    constexpr std::uint32_t loaded_word = 0xA1B2C3D4U;
    constexpr std::uint32_t original_word = 0x11223344U;
    constexpr std::uint32_t word_source = 0x89ABCDEFU;
    for (std::uint32_t lane = 0; lane < 4U; ++lane) {
        if (reps2::runtime::store_u32(unaligned_words.memory, 0x6000U + lane * 4U, loaded_word) != Status::ok
            || reps2::runtime::store_u32(unaligned_words.memory, 0x6010U + lane * 4U, loaded_word) != Status::ok
            || reps2::runtime::store_u32(unaligned_words.memory, 0x6020U + lane * 4U, original_word) != Status::ok
            || reps2::runtime::store_u32(unaligned_words.memory, 0x6030U + lane * 4U, original_word) != Status::ok) {
            return fail(45);
        }
        unaligned_words.gpr[10U + lane].low = old_word_register;
        unaligned_words.gpr[10U + lane].high = 0xAABBCCDD00000000ULL + lane;
        unaligned_words.gpr[14U + lane].low = old_word_register;
        unaligned_words.gpr[14U + lane].high = 0xBBCCDDEE00000000ULL + lane;
    }
    const auto word_merge_status = reps2::generated::execute_cfg(unaligned_words, 64U);
    if (word_merge_status != Status::external_control_flow || unaligned_words.pc != 0x9000U) {
        std::fprintf(stderr, "generated word-merge fixture failed: status=%u pc=0x%08X\n",
            static_cast<unsigned>(word_merge_status), unaligned_words.pc);
        return fail(46);
    }
    for (std::uint32_t lane = 0; lane < 4U; ++lane) {
        const auto left_shift = (3U - lane) * 8U;
        const auto left_mask = 0x00FFFFFFU >> (lane * 8U);
        const auto left_merged = (static_cast<std::uint32_t>(old_word_register) & left_mask)
            | (loaded_word << left_shift);
        const auto expected_left = reps2::runtime::sign_extend_word(left_merged);
        const auto right_shift = lane * 8U;
        const auto right_mask = 0xFFFFFF00U << ((3U - lane) * 8U);
        const auto right_merged = (static_cast<std::uint32_t>(old_word_register) & right_mask)
            | (loaded_word >> right_shift);
        const auto expected_right = lane == 0U
            ? reps2::runtime::sign_extend_word(right_merged)
            : ((old_word_register & 0xFFFFFFFF00000000ULL) | right_merged);
        if (unaligned_words.gpr[10U + lane].low != expected_left
            || unaligned_words.gpr[14U + lane].low != expected_right
            || unaligned_words.gpr[10U + lane].high != 0xAABBCCDD00000000ULL + lane
            || unaligned_words.gpr[14U + lane].high != 0xBBCCDDEE00000000ULL + lane) {
            std::fprintf(stderr, "LWL/LWR lane mismatch at byte offset %u\n", lane);
            return fail(47);
        }
        const auto swl_shift = (3U - lane) * 8U;
        const auto swl_mask = 0xFFFFFF00U << (lane * 8U);
        const auto expected_swl = (word_source >> swl_shift) | (original_word & swl_mask);
        const auto swr_shift = lane * 8U;
        const auto swr_mask = 0x00FFFFFFU >> ((3U - lane) * 8U);
        const auto expected_swr = (word_source << swr_shift) | (original_word & swr_mask);
        std::uint32_t observed_swl{}, observed_swr{};
        if (reps2::runtime::load_u32(unaligned_words.memory, 0x6020U + lane * 4U, observed_swl) != Status::ok
            || reps2::runtime::load_u32(unaligned_words.memory, 0x6030U + lane * 4U, observed_swr) != Status::ok
            || observed_swl != expected_swl || observed_swr != expected_swr) {
            std::fprintf(stderr, "SWL/SWR lane mismatch at byte offset %u\n", lane);
            return fail(48);
        }
    }

    // Generated LDL/LDR/SDL/SDR instructions exercise all eight byte lanes.
    EeState unaligned_doublewords{};
    unaligned_doublewords.pc = 0x5400U;
    unaligned_doublewords.memory.main_ram = main_ram;
    unaligned_doublewords.gpr[8].low = 0x6100U;
    unaligned_doublewords.gpr[9].low = 0x89ABCDEF01234567ULL;
    unaligned_doublewords.gpr[31].low = 0x9000U;
    constexpr std::uint64_t old_doubleword_register = 0x1122334455667788ULL;
    constexpr std::uint64_t loaded_doubleword = 0xA1B2C3D4E5F60718ULL;
    constexpr std::uint64_t original_doubleword = 0x1122334455667788ULL;
    constexpr std::uint64_t doubleword_source = 0x89ABCDEF01234567ULL;
    for (std::uint32_t lane = 0; lane < 8U; ++lane) {
        if (reps2::runtime::store_u64(unaligned_doublewords.memory, 0x6100U + lane * 8U, loaded_doubleword) != Status::ok
            || reps2::runtime::store_u64(unaligned_doublewords.memory, 0x6140U + lane * 8U, loaded_doubleword) != Status::ok
            || reps2::runtime::store_u64(unaligned_doublewords.memory, 0x6180U + lane * 8U, original_doubleword) != Status::ok
            || reps2::runtime::store_u64(unaligned_doublewords.memory, 0x61C0U + lane * 8U, original_doubleword) != Status::ok) {
            return fail(49);
        }
        unaligned_doublewords.gpr[10U + lane].low = old_doubleword_register;
        unaligned_doublewords.gpr[10U + lane].high = 0xC000000000000000ULL + lane;
        unaligned_doublewords.gpr[18U + lane].low = old_doubleword_register;
        unaligned_doublewords.gpr[18U + lane].high = 0xD000000000000000ULL + lane;
    }
    const auto doubleword_merge_status = reps2::generated::execute_cfg(unaligned_doublewords, 64U);
    if (doubleword_merge_status != Status::external_control_flow || unaligned_doublewords.pc != 0x9000U) {
        std::fprintf(stderr, "generated doubleword-merge fixture failed: status=%u pc=0x%08X\n",
            static_cast<unsigned>(doubleword_merge_status), unaligned_doublewords.pc);
        return fail(50);
    }
    for (std::uint32_t lane = 0; lane < 8U; ++lane) {
        const auto left_shift = (7U - lane) * 8U;
        const auto left_mask = 0x00FFFFFFFFFFFFFFULL >> (lane * 8U);
        const auto expected_left = (old_doubleword_register & left_mask) | (loaded_doubleword << left_shift);
        const auto right_shift = lane * 8U;
        const auto right_mask = 0xFFFFFFFFFFFFFF00ULL << ((7U - lane) * 8U);
        const auto expected_right = (old_doubleword_register & right_mask) | (loaded_doubleword >> right_shift);
        if (unaligned_doublewords.gpr[10U + lane].low != expected_left
            || unaligned_doublewords.gpr[18U + lane].low != expected_right
            || unaligned_doublewords.gpr[10U + lane].high != 0xC000000000000000ULL + lane
            || unaligned_doublewords.gpr[18U + lane].high != 0xD000000000000000ULL + lane) {
            std::fprintf(stderr, "LDL/LDR lane mismatch at byte offset %u\n", lane);
            return fail(51);
        }
        const auto sdl_shift = (7U - lane) * 8U;
        const auto sdl_mask = 0xFFFFFFFFFFFFFF00ULL << (lane * 8U);
        const auto expected_sdl = (doubleword_source >> sdl_shift) | (original_doubleword & sdl_mask);
        const auto sdr_shift = lane * 8U;
        const auto sdr_mask = 0x00FFFFFFFFFFFFFFULL >> ((7U - lane) * 8U);
        const auto expected_sdr = (doubleword_source << sdr_shift) | (original_doubleword & sdr_mask);
        std::uint64_t observed_sdl{}, observed_sdr{};
        if (reps2::runtime::load_u64(unaligned_doublewords.memory, 0x6180U + lane * 8U, observed_sdl) != Status::ok
            || reps2::runtime::load_u64(unaligned_doublewords.memory, 0x61C0U + lane * 8U, observed_sdr) != Status::ok
            || observed_sdl != expected_sdl || observed_sdr != expected_sdr) {
            std::fprintf(stderr, "SDL/SDR lane mismatch at byte offset %u\n", lane);
            return fail(52);
        }
    }

    // Unaligned instructions still validate the complete aligned backing access
    // before modifying guest state.
    EeState unaligned_short{};
    unaligned_short.pc = 0x5300U;
    unaligned_short.memory.main_ram = short_ram;
    unaligned_short.gpr[8].low = 0x6000U;
    unaligned_short.gpr[10].low = old_word_register;
    const auto unaligned_short_status = reps2::generated::execute_cfg(unaligned_short, 64U);
    if (unaligned_short_status != Status::unmapped_memory || unaligned_short.pc != 0x5300U
        || unaligned_short.gpr[10].low != old_word_register) {
        std::fprintf(stderr, "failed LWL backing access was not atomic\n");
        return fail(53);
    }

    // Every partial helper must reject a short aligned backing unit atomically.
    std::uint64_t partial_load64 = 0x1122334455667788ULL;
    if (reps2::runtime::load_u32_left(short_backing.memory, 0x6001U, old_word_register, partial_load64) != Status::unmapped_memory
        || partial_load64 != 0x1122334455667788ULL
        || reps2::runtime::load_u32_right(short_backing.memory, 0x6002U, old_word_register, partial_load64) != Status::unmapped_memory
        || partial_load64 != 0x1122334455667788ULL
        || reps2::runtime::load_u64_left(short_backing.memory, 0x6003U, old_doubleword_register, partial_load64) != Status::unmapped_memory
        || partial_load64 != 0x1122334455667788ULL
        || reps2::runtime::load_u64_right(short_backing.memory, 0x6004U, old_doubleword_register, partial_load64) != Status::unmapped_memory
        || partial_load64 != 0x1122334455667788ULL) {
        std::fprintf(stderr, "partial load helper changed its destination after a failed aligned read\\n");
        return fail(54);
    }
    if (reps2::runtime::store_u32_left(short_backing.memory, 0x6001U, 0x89ABCDEFU) != Status::unmapped_memory
        || reps2::runtime::store_u32_right(short_backing.memory, 0x6002U, 0x89ABCDEFU) != Status::unmapped_memory
        || reps2::runtime::store_u64_left(short_backing.memory, 0x6003U, 0x89ABCDEF01234567ULL) != Status::unmapped_memory
        || reps2::runtime::store_u64_right(short_backing.memory, 0x6004U, 0x89ABCDEF01234567ULL) != Status::unmapped_memory
        || std::any_of(short_ram.begin(), short_ram.end(), [](std::uint8_t byte) { return byte != 0; })) {
        std::fprintf(stderr, "partial store helper modified short backing after a failed aligned read\\n");
        return fail(55);
    }

    // COP1 compares write only FCR31 bit 23 (the condition bit) and the BC1
    // family branches on it. Every block starts with the condition set and
    // unrelated cause and sticky bits dirty, so a compare that clobbered any
    // other field would be caught.
    constexpr std::uint32_t fcsr_condition_set = 0x0181'0041U;
    constexpr std::uint32_t fcsr_condition_clear = 0x0101'0041U;
    constexpr std::uint32_t single_one = 0x3F80'0000U;
    constexpr std::uint32_t single_two = 0x4000'0000U;

    EeState fpu_bc1t_taken{};
    fpu_bc1t_taken.pc = 0x7000U;
    fpu_bc1t_taken.gpr[31].low = 0x9000U;
    fpu_bc1t_taken.fpr[0] = single_one;
    fpu_bc1t_taken.fpr[1] = single_two;
    fpu_bc1t_taken.fcsr = fcsr_condition_set;
    const auto bc1t_taken_status = reps2::generated::execute_cfg(fpu_bc1t_taken, 64U);
    if (bc1t_taken_status != Status::external_control_flow || fpu_bc1t_taken.pc != 0x9000U) {
        std::fprintf(stderr, "taken BC1T status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(bc1t_taken_status), fpu_bc1t_taken.pc);
        return fail(64);
    }
    if (fpu_bc1t_taken.gpr[9].low != 7 || fpu_bc1t_taken.gpr[10].low != 0
        || fpu_bc1t_taken.gpr[11].low != 9) {
        std::fprintf(stderr, "taken BC1T executed the wrong path\n");
        return fail(65);
    }
    if (fpu_bc1t_taken.fcsr != fcsr_condition_set
        || fpu_bc1t_taken.fpr[0] != single_one || fpu_bc1t_taken.fpr[1] != single_two) {
        std::fprintf(stderr, "C.LT.S disturbed state beyond the condition bit\n");
        return fail(66);
    }

    EeState fpu_bc1t_not_taken{};
    fpu_bc1t_not_taken.pc = 0x7000U;
    fpu_bc1t_not_taken.gpr[31].low = 0x9000U;
    fpu_bc1t_not_taken.fpr[0] = 0x4040'0000U; // 3.0
    fpu_bc1t_not_taken.fpr[1] = single_two;
    fpu_bc1t_not_taken.fcsr = fcsr_condition_set;
    const auto bc1t_not_taken_status = reps2::generated::execute_cfg(fpu_bc1t_not_taken, 64U);
    if (bc1t_not_taken_status != Status::external_control_flow || fpu_bc1t_not_taken.pc != 0x9000U) {
        std::fprintf(stderr, "not-taken BC1T status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(bc1t_not_taken_status), fpu_bc1t_not_taken.pc);
        return fail(67);
    }
    if (fpu_bc1t_not_taken.gpr[9].low != 7 || fpu_bc1t_not_taken.gpr[10].low != 5
        || fpu_bc1t_not_taken.gpr[11].low != 9) {
        std::fprintf(stderr, "not-taken BC1T did not run its delay slot and fallthrough\n");
        return fail(68);
    }
    if (fpu_bc1t_not_taken.fcsr != fcsr_condition_clear) {
        std::fprintf(stderr, "false C.LT.S did not clear only the condition bit: fcsr=0x%08X\n",
            fpu_bc1t_not_taken.fcsr);
        return fail(69);
    }

    EeState fpu_bc1f{};
    fpu_bc1f.pc = 0x7100U;
    fpu_bc1f.gpr[31].low = 0x9000U;
    fpu_bc1f.fpr[0] = single_one;
    fpu_bc1f.fpr[1] = single_one;
    fpu_bc1f.fcsr = fcsr_condition_set;
    const auto bc1f_status = reps2::generated::execute_cfg(fpu_bc1f, 64U);
    if (bc1f_status != Status::external_control_flow || fpu_bc1f.pc != 0x9000U) {
        std::fprintf(stderr, "BC1F status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(bc1f_status), fpu_bc1f.pc);
        return fail(70);
    }
    if (fpu_bc1f.gpr[9].low != 7 || fpu_bc1f.gpr[10].low != 0
        || fpu_bc1f.gpr[11].low != 9) {
        std::fprintf(stderr, "BC1F did not take the false branch after C.F.S\n");
        return fail(71);
    }
    if (fpu_bc1f.fcsr != fcsr_condition_clear) {
        std::fprintf(stderr, "C.F.S did not clear the condition bit: fcsr=0x%08X\n",
            fpu_bc1f.fcsr);
        return fail(72);
    }

    EeState fpu_bc1fl_taken{};
    fpu_bc1fl_taken.pc = 0x7200U;
    fpu_bc1fl_taken.gpr[31].low = 0x9000U;
    fpu_bc1fl_taken.fpr[0] = single_one;
    fpu_bc1fl_taken.fpr[1] = single_two;
    fpu_bc1fl_taken.fcsr = fcsr_condition_set;
    const auto bc1fl_taken_status = reps2::generated::execute_cfg(fpu_bc1fl_taken, 64U);
    if (bc1fl_taken_status != Status::external_control_flow || fpu_bc1fl_taken.pc != 0x9000U) {
        std::fprintf(stderr, "taken BC1FL status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(bc1fl_taken_status), fpu_bc1fl_taken.pc);
        return fail(73);
    }
    if (fpu_bc1fl_taken.gpr[9].low != 7 || fpu_bc1fl_taken.gpr[10].low != 0
        || fpu_bc1fl_taken.gpr[12].low != 9 || fpu_bc1fl_taken.gpr[13].low != 11
        || fpu_bc1fl_taken.gpr[14].low != 0) {
        std::fprintf(stderr, "taken BC1FL executed the wrong path\n");
        return fail(74);
    }
    if (fpu_bc1fl_taken.fcsr != fcsr_condition_clear) {
        std::fprintf(stderr, "false C.EQ.S did not clear the condition bit\n");
        return fail(75);
    }

    EeState fpu_bc1fl_annulled{};
    fpu_bc1fl_annulled.pc = 0x7200U;
    fpu_bc1fl_annulled.gpr[31].low = 0x9000U;
    fpu_bc1fl_annulled.fpr[0] = single_one;
    fpu_bc1fl_annulled.fpr[1] = single_one;
    fpu_bc1fl_annulled.fcsr = fcsr_condition_clear;
    const auto bc1fl_annulled_status = reps2::generated::execute_cfg(fpu_bc1fl_annulled, 64U);
    if (bc1fl_annulled_status != Status::external_control_flow || fpu_bc1fl_annulled.pc != 0x9000U) {
        std::fprintf(stderr, "annulled BC1FL status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(bc1fl_annulled_status), fpu_bc1fl_annulled.pc);
        return fail(76);
    }
    if (fpu_bc1fl_annulled.gpr[9].low != 0 || fpu_bc1fl_annulled.gpr[10].low != 5
        || fpu_bc1fl_annulled.gpr[12].low != 0 || fpu_bc1fl_annulled.gpr[13].low != 0
        || fpu_bc1fl_annulled.gpr[14].low != 13) {
        std::fprintf(stderr, "not-taken BC1FL did not annul its delay slot\n");
        return fail(77);
    }
    if (fpu_bc1fl_annulled.fcsr != fcsr_condition_set) {
        std::fprintf(stderr, "true C.EQ.S did not set the condition bit\n");
        return fail(78);
    }

    // +0 and -0 compare equal even though their encodings differ.
    EeState fpu_bc1fl_signed_zero{};
    fpu_bc1fl_signed_zero.pc = 0x7200U;
    fpu_bc1fl_signed_zero.gpr[31].low = 0x9000U;
    fpu_bc1fl_signed_zero.fpr[0] = 0x0000'0000U;
    fpu_bc1fl_signed_zero.fpr[1] = 0x8000'0000U;
    fpu_bc1fl_signed_zero.fcsr = fcsr_condition_clear;
    const auto bc1fl_signed_zero_status = reps2::generated::execute_cfg(fpu_bc1fl_signed_zero, 64U);
    if (bc1fl_signed_zero_status != Status::external_control_flow
        || fpu_bc1fl_signed_zero.pc != 0x9000U) {
        std::fprintf(stderr, "signed-zero BC1FL status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(bc1fl_signed_zero_status), fpu_bc1fl_signed_zero.pc);
        return fail(79);
    }
    if (fpu_bc1fl_signed_zero.gpr[9].low != 0 || fpu_bc1fl_signed_zero.gpr[10].low != 5
        || fpu_bc1fl_signed_zero.gpr[12].low != 0 || fpu_bc1fl_signed_zero.gpr[13].low != 0
        || fpu_bc1fl_signed_zero.gpr[14].low != 13) {
        std::fprintf(stderr, "C.EQ.S did not treat +0 and -0 as equal\n");
        return fail(80);
    }
    if (fpu_bc1fl_signed_zero.fcsr != fcsr_condition_set) {
        std::fprintf(stderr, "signed-zero equality did not set the condition bit\n");
        return fail(81);
    }

    EeState fpu_bc1tl_taken{};
    fpu_bc1tl_taken.pc = 0x7300U;
    fpu_bc1tl_taken.gpr[31].low = 0x9000U;
    fpu_bc1tl_taken.fpr[0] = single_two;
    fpu_bc1tl_taken.fpr[1] = single_two;
    fpu_bc1tl_taken.fcsr = fcsr_condition_clear;
    const auto bc1tl_taken_status = reps2::generated::execute_cfg(fpu_bc1tl_taken, 64U);
    if (bc1tl_taken_status != Status::external_control_flow || fpu_bc1tl_taken.pc != 0x9000U) {
        std::fprintf(stderr, "taken BC1TL status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(bc1tl_taken_status), fpu_bc1tl_taken.pc);
        return fail(82);
    }
    if (fpu_bc1tl_taken.gpr[9].low != 7 || fpu_bc1tl_taken.gpr[10].low != 0
        || fpu_bc1tl_taken.gpr[12].low != 9 || fpu_bc1tl_taken.gpr[13].low != 11
        || fpu_bc1tl_taken.gpr[14].low != 0) {
        std::fprintf(stderr, "taken BC1TL executed the wrong path\n");
        return fail(83);
    }
    if (fpu_bc1tl_taken.fcsr != fcsr_condition_set) {
        std::fprintf(stderr, "true C.LE.S did not set the condition bit\n");
        return fail(84);
    }

    EeState fpu_bc1tl_annulled{};
    fpu_bc1tl_annulled.pc = 0x7300U;
    fpu_bc1tl_annulled.gpr[31].low = 0x9000U;
    fpu_bc1tl_annulled.fpr[0] = 0x4040'0000U; // 3.0
    fpu_bc1tl_annulled.fpr[1] = single_two;
    fpu_bc1tl_annulled.fcsr = fcsr_condition_set;
    const auto bc1tl_annulled_status = reps2::generated::execute_cfg(fpu_bc1tl_annulled, 64U);
    if (bc1tl_annulled_status != Status::external_control_flow || fpu_bc1tl_annulled.pc != 0x9000U) {
        std::fprintf(stderr, "annulled BC1TL status mismatch: status=%u pc=0x%08X\n",
            static_cast<unsigned>(bc1tl_annulled_status), fpu_bc1tl_annulled.pc);
        return fail(85);
    }
    if (fpu_bc1tl_annulled.gpr[9].low != 0 || fpu_bc1tl_annulled.gpr[10].low != 5
        || fpu_bc1tl_annulled.gpr[12].low != 0 || fpu_bc1tl_annulled.gpr[13].low != 0
        || fpu_bc1tl_annulled.gpr[14].low != 13) {
        std::fprintf(stderr, "not-taken BC1TL did not annul its delay slot\n");
        return fail(86);
    }
    if (fpu_bc1tl_annulled.fcsr != fcsr_condition_clear) {
        std::fprintf(stderr, "false C.LE.S did not clear the condition bit\n");
        return fail(87);
    }

    // The generated module initializer accepts a complete 32 MiB image,
    // copies it into caller-owned RAM, clears architectural GPR state, and
    // selects the ELF entry point.
    std::vector<std::uint8_t> initial_image(reps2::runtime::kEeMainRamSize, std::uint8_t{0});
    initial_image[0x1000U] = 0x12U;
    initial_image[0x1001U] = 0x34U;
    EeState initialized{};
    initialized.memory.main_ram = main_ram;
    initialized.pc = 0xDEADBEEFU;
    initialized.gpr[8].low = 0x12345678U;
    initialized.hi = 0x1111222233334444ULL;
    initialized.lo = 0x5555666677778888ULL;
    const auto initialize_status = reps2::generated::initialize_ee_state(initialized, initial_image);
    if (initialize_status != Status::ok || initialized.pc != 0x1000U) {
        std::fprintf(stderr, "ELF runtime initializer failed: status=%u pc=0x%08X\n",
            static_cast<unsigned>(initialize_status), initialized.pc);
        return fail(32);
    }
    if (initialized.gpr[8].low != 0 || initialized.gpr[0].low != 0
        || initialized.hi != 0 || initialized.lo != 0
        || initialized.memory.main_ram[0x1000U] != 0x12U
        || initialized.memory.main_ram[0x1001U] != 0x34U) {
        std::fprintf(stderr, "ELF runtime initializer did not copy RAM/reset GPR state\n");
        return fail(33);
    }

    // Invalid image sizes fail before mutating guest state or the existing RAM.
    initialized.pc = 0x4444U;
    initialized.gpr[8].low = 0x5555U;
    initialized.hi = 0xAABBCCDDEEFF0011ULL;
    initialized.lo = 0x1122334455667788ULL;
    initialized.memory.main_ram[0x1000U] = 0xAAU;
    const std::array<std::uint8_t, 8> short_image{};
    const auto invalid_image_status = reps2::generated::initialize_ee_state(initialized, short_image);
    if (invalid_image_status != Status::invalid_memory_image || initialized.pc != 0x4444U
        || initialized.gpr[8].low != 0x5555U
        || initialized.hi != 0xAABBCCDDEEFF0011ULL
        || initialized.lo != 0x1122334455667788ULL
        || initialized.memory.main_ram[0x1000U] != 0xAAU) {
        std::fprintf(stderr, "invalid RAM image was not rejected atomically\n");
        return fail(34);
    }

    return 0;
}
