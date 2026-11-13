// SPDX-License-Identifier: GPL-3.0-or-later
//
// EE syscall HLE smoke test.
//
// The synthetic CFG fixture exposes four HLE entries:
//   0xE000  syscall; addiu v0, zero, 1; jr ra
//   0xE100  syscall; addiu v0, zero, 1        (service number has no handler)
//   0xE200  beq t0, zero, 0xE214; syscall (delay slot); ... ; either path jr ra
//   0xE400  syscall; jr ra                    ($v0 keeps the service result)
//
// The EE kernel service number is passed in $v1 (GPR[3]), so these tests set it
// directly instead of relying on the instruction's 20-bit code field.
#include "reps2/runtime/ee_hle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string_view>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute_cfg(
    reps2::runtime::EeState& state,
    std::uint64_t instruction_budget
) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Status;

constexpr std::uint32_t kUnreachableReturnPc = 0xDEAD0000U;
constexpr std::uint32_t kSetupThread = reps2::runtime::kEeSyscallSetupThread;
constexpr std::uint32_t kSetupHeap = reps2::runtime::kEeSyscallSetupHeap;
constexpr std::uint32_t kCreateSema = reps2::runtime::kEeSyscallCreateSema;
constexpr std::uint32_t kDeleteSema = reps2::runtime::kEeSyscallDeleteSema;
constexpr std::uint32_t kSignalSema = reps2::runtime::kEeSyscallSignalSema;
constexpr std::uint32_t kISignalSema = reps2::runtime::kEeSyscallISignalSema;
constexpr std::uint32_t kWaitSema = reps2::runtime::kEeSyscallWaitSema;
constexpr std::uint32_t kPollSema = reps2::runtime::kEeSyscallPollSema;
constexpr std::uint32_t kIPollSema = reps2::runtime::kEeSyscallIPollSema;
constexpr std::uint32_t kSetSyscall = reps2::runtime::kEeSyscallSetSyscall;
constexpr std::uint32_t kFlushCache = reps2::runtime::kEeSyscallFlushCache;
constexpr std::uint32_t kFindAddress = reps2::runtime::kEeSyscallFindAddress;
constexpr std::uint32_t kAddDmacHandler = reps2::runtime::kEeSyscallAddDmacHandler;
constexpr std::uint32_t kEnableDmac = reps2::runtime::kEeSyscallEnableDmac;
constexpr std::uint32_t kDisableDmac = reps2::runtime::kEeSyscallDisableDmac;
constexpr std::uint32_t kRemoveDmacHandler = reps2::runtime::kEeSyscallRemoveDmacHandler;
constexpr std::uint32_t kSifStopDma = reps2::runtime::kEeSyscallSifStopDma;
constexpr std::uint32_t kSifGetReg = reps2::runtime::kEeSyscallSifGetReg;
constexpr std::uint32_t kSifSetReg = reps2::runtime::kEeSyscallSifSetReg;
constexpr std::uint32_t kSifSetDma = reps2::runtime::kEeSyscallSifSetDma;
// The SMFLAG command-init flag the target's sceSifInitCmd polls: the guest at
// 0x0025C324 loads `lui s0, 0x2` and then `and v0, v0, s0`, i.e. 0x00020000.
constexpr std::uint32_t kSifStatCmdInit = reps2::runtime::kEeSifStatCmdInit;
// The CreateSema tests need guest-readable parameters, but a full 32 MiB image
// per case is unnecessary: main RAM is a span, so a small aligned window proves
// the same address-resolution and bounds behavior.
using RamWindow = std::array<std::uint8_t, 0x1000>;
// SetSyscall publishes handlers into the guest syscall table at 0x80011F80, so
// these cases need RAM that covers its 256 entries (KSEG0 alias 0x11F80).
using SyscallTableWindow = std::array<std::uint8_t, 0x20000>;
// The SIF inbound-reply cases place a guest `cmd_data`, a 32-entry `sregs`
// array, a command packet and an EE receive buffer, so they need a window large
// enough to hold all four disjoint regions.
using SifWindow = std::array<std::uint8_t, 0x20000>;
constexpr std::uint32_t kSemaParameters = 0x0000'0100U;

template <typename Ram>
void store_word(Ram& ram, std::uint32_t address, std::uint32_t value) {
    const auto offset = static_cast<std::size_t>(address);
    ram[offset] = static_cast<std::uint8_t>(value);
    ram[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    ram[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    ram[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

template <typename Ram>
std::uint32_t load_word(const Ram& ram, std::uint32_t address) {
    const auto offset = static_cast<std::size_t>(address);
    return static_cast<std::uint32_t>(ram[offset])
        | (static_cast<std::uint32_t>(ram[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(ram[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(ram[offset + 3U]) << 24U);
}

// Writes the six documented parameter words: count, max_count, init_count,
// wait_threads, attr, option.
void store_sema_parameters(
    RamWindow& ram,
    std::uint32_t address,
    std::int32_t count,
    std::int32_t max_count,
    std::int32_t init_count,
    std::int32_t wait_threads,
    std::uint32_t attr,
    std::uint32_t option
) {
    const auto words = {count, max_count, init_count, wait_threads};
    std::uint32_t index = 0;
    for (const std::int32_t word : words) {
        store_word(ram, address + index * 4U, static_cast<std::uint32_t>(word));
        ++index;
    }
    store_word(ram, address + 16U, attr);
    store_word(ram, address + 20U, option);
}

// Writes the nine documented ee_thread_t words. `status` is deliberately
// supplied by the caller because the observed retail call leaves it
// uninitialized; the HLE layer must not interpret it.
void store_thread_parameters(
    RamWindow& ram,
    std::uint32_t address,
    std::uint32_t status,
    std::uint32_t function,
    std::uint32_t stack,
    std::uint32_t stack_size,
    std::uint32_t gp,
    std::uint32_t initial_priority,
    std::uint32_t attr = 0U,
    std::uint32_t option = 0U
) {
    store_word(ram, address + 0x00U, status);
    store_word(ram, address + 0x04U, function);
    store_word(ram, address + 0x08U, stack);
    store_word(ram, address + 0x0CU, stack_size);
    store_word(ram, address + 0x10U, gp);
    store_word(ram, address + 0x14U, initial_priority);
    store_word(ram, address + 0x18U, initial_priority);
    store_word(ram, address + 0x1CU, attr);
    store_word(ram, address + 0x20U, option);
}

// A serviced call resumes at the instruction after the trap and the following
// register write proves the guest continued past the SYSCALL.
int test_disabled_layer_keeps_the_architectural_exception() {
    EeState state{};
    state.pc = 0xE000U;
    state.gpr[3].low = kSetupThread;
    const Status status = reps2::generated::execute_cfg(state, 8U);
    if (status != Status::exception_delivered) {
        return 10;
    }
    if (state.pc != reps2::runtime::kEeCommonExceptionVector) {
        return 11;
    }
    if ((state.cop0[reps2::runtime::kCop0Cause] & reps2::runtime::kCop0CauseExcCodeMask)
        != (8U << 2U)) {
        return 12;
    }
    if (state.cop0[reps2::runtime::kCop0Epc] != 0xE000U) {
        return 13;
    }
    if ((state.cop0[reps2::runtime::kCop0Cause]
         & reps2::runtime::kCop0CauseBranchDelayMask)
        != 0U) {
        return 14;
    }
    if ((state.cop0[reps2::runtime::kCop0Status] & reps2::runtime::kCop0StatusExlMask) == 0U) {
        return 15;
    }
    // The host service layer was never consulted.
    if (state.hle.syscall_enabled || state.hle.syscall_count != 0U
        || state.hle.syscall_handled_count != 0U) {
        return 16;
    }
    return 0;
}

int test_setup_thread_with_the_stack_sentinel() {
    EeState state{};
    state.pc = 0xE000U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupThread;
    state.gpr[4].low = 0x80100000U; // $a0: gp
    state.gpr[5].low = 0xFFFFFFFFU; // $a1: no caller stack
    state.gpr[6].low = 0x4000U;     // $a2: stack size
    state.gpr[7].low = 0x00200000U; // $a3: argv block
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 20;
    }
    // The trailing `addiu v0, zero, 1` ran, so the call resumed after the trap.
    if (state.gpr[2].low != 1U) {
        return 21;
    }
    if (state.hle.syscall_count != 1U || state.hle.syscall_handled_count != 1U) {
        return 22;
    }
    if (state.hle.last_syscall_code != kSetupThread
        || state.hle.last_syscall_number != kSetupThread) {
        return 23;
    }
    if (!state.hle.setup_thread_called) {
        return 24;
    }
    // 32 MiB main RAM minus the 4 KiB top guard, then the 0x2A0 reserved bytes.
    const std::uint32_t expected_top =
        reps2::runtime::kEeMainRamSize - reps2::runtime::kEeSetupThreadTopGuard;
    const std::uint32_t expected_sp =
        expected_top - reps2::runtime::kEeSetupThreadStackReserve;
    if (state.hle.setup_thread_stack_top != expected_top
        || state.hle.setup_thread_stack_pointer != expected_sp) {
        return 25;
    }
    if (state.hle.setup_thread_stack_size != 0x4000U
        || state.hle.setup_thread_gp != 0x80100000U
        || state.hle.setup_thread_args != 0x00200000U) {
        return 26;
    }
    // No COP0 exception context was created by the serviced call.
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 27;
    }
    return 0;
}

int test_setup_thread_with_an_explicit_stack() {
    EeState state{};
    state.pc = 0xE000U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupThread;
    state.gpr[5].low = 0x00100000U; // $a1: caller stack base
    state.gpr[6].low = 0x00008000U; // $a2: stack size
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 30;
    }
    if (state.hle.setup_thread_stack_top != 0x00108000U) {
        return 31;
    }
    if (state.hle.setup_thread_stack_pointer
        != 0x00108000U - reps2::runtime::kEeSetupThreadStackReserve) {
        return 32;
    }
    if (state.hle.setup_thread_stack_size != 0x00008000U) {
        return 33;
    }
    return 0;
}

int test_negated_service_number_dispatches_on_its_magnitude() {
    EeState state{};
    state.pc = 0xE000U;
    state.hle.syscall_enabled = true;
    // The ps2sdk "i" wrappers pass the negated service number.
    state.gpr[3].low = static_cast<std::uint64_t>(0U - kSetupThread);
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 40;
    }
    if (state.hle.last_syscall_code != 0U - kSetupThread
        || state.hle.last_syscall_number != kSetupThread) {
        return 41;
    }
    if (!state.hle.setup_thread_called || state.hle.syscall_handled_count != 1U) {
        return 42;
    }
    return 0;
}

int test_unknown_service_stops_without_faking_success() {
    EeState state{};
    state.pc = 0xE100U;
    state.hle.syscall_enabled = true;
    state.gpr[2].low = 0x1234U;  // sentinel result the guest must keep
    state.gpr[3].low = 0x00000099U; // no host handler

    const Status status = reps2::generated::execute_cfg(state, 8U);
    if (status != Status::unhandled_syscall) {
        return 50;
    }
    // The stop is a host boundary at the SYSCALL, not a guest exception.
    if (state.pc != 0xE100U) {
        return 51;
    }
    if (state.gpr[2].low != 0x1234U) {
        return 52;
    }
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U
        || (state.cop0[reps2::runtime::kCop0Status] & reps2::runtime::kCop0StatusExlMask) != 0U) {
        return 53;
    }
    if (state.hle.syscall_count != 1U || state.hle.syscall_handled_count != 0U) {
        return 54;
    }
    if (state.hle.last_syscall_number != 0x99U || state.hle.setup_thread_called) {
        return 55;
    }
    if (reps2::runtime::ee_syscall_service_name(0x99U) != nullptr) {
        return 56;
    }
    return 0;
}

int test_flush_cache_is_an_honest_no_op() {
    EeState state{};
    state.pc = 0xE000U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kFlushCache;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 60;
    }
    if (state.hle.syscall_handled_count != 1U || state.hle.last_syscall_number != kFlushCache) {
        return 61;
    }
    // The resume marker still ran and no thread state was invented.
    if (state.gpr[2].low != 1U || state.hle.setup_thread_called) {
        return 62;
    }
    return 0;
}

int test_sif_set_d_chain_starts_the_idle_channel() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.gpr[2].low = 0x1234U; // declared void: no result register contract

    if (reps2::runtime::hle_sif_set_d_chain(state) != Status::ok) return 310;
    if (state.gpr[2].low != 0x1234U) return 311;
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok) {
        return 312;
    }
    if ((d_stat & reps2::runtime::kEeDmacDStatSif0StatusBit) == 0U) return 313;
    std::uint32_t chcr = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacChcr5Address, chcr) != Status::ok) {
        return 314;
    }
    // The start is a completed transfer: STR self-clears.
    if ((chcr & reps2::runtime::kEeDmacChcrStrMask) != 0U) return 315;
    return 0;
}

int test_sif_set_d_chain_leaves_a_running_channel_alone() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.memory.dmac.seed_d_stat(0U);
    state.memory.dmac.seed_chcr5(reps2::runtime::kEeDmacChcrStrMask);

    if (reps2::runtime::hle_sif_set_d_chain(state) != Status::ok) return 320;
    std::uint32_t value = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, value) != Status::ok) {
        return 321;
    }
    // A second start while STR is set must not invent a completion event.
    if ((value & reps2::runtime::kEeDmacDStatSif0StatusBit) != 0U) return 322;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacChcr5Address, value) != Status::ok) {
        return 323;
    }
    if ((value & reps2::runtime::kEeDmacChcrStrMask) == 0U) return 324;
    return 0;
}

int test_sif_set_d_chain_refuses_unknown_state() {
    EeState state{};
    state.gpr[2].low = 0x1234U;
    if (reps2::runtime::hle_sif_set_d_chain(state) != Status::unsupported_memory_access) {
        return 330;
    }
    if (state.memory.dmac.chcr5_known || state.memory.dmac.d_stat_known) return 331;
    if (state.gpr[2].low != 0x1234U) return 332;
    return 0;
}

int test_sif_set_d_chain_via_dispatch() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.syscall_enabled = true;
    state.pc = 0xE000U;
    state.gpr[3].low = reps2::runtime::kEeSyscallSifSetDChain;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 340;
    }
    if (state.hle.syscall_handled_count != 1U
        || state.hle.last_syscall_number != reps2::runtime::kEeSyscallSifSetDChain) {
        return 341;
    }
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok
        || (d_stat & reps2::runtime::kEeDmacDStatSif0StatusBit) == 0U) {
        return 342;
    }
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 343;
    }
    return 0;
}

int test_add_dmac_handler_registers_the_sif0_handler() {
    EeState state{};
    state.pc = 0xE800U;
    state.gpr[4].low = 5U;            // SIF0 is DMAC channel 5
    state.gpr[5].low = 0x0025C5F8U;   // handler the observed title registers
    state.gpr[6].low = 0U;            // no handler chain

    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok) return 350;
    if (state.hle.dmac_handler_count != 1U) return 351;
    const auto& record = state.hle.dmac_handlers[5U];
    if (!record.in_use || record.handler != 0x0025C5F8U || record.next != 0U) return 352;
    if (record.register_pc != 0xE800U) return 353;
    if (state.hle.last_dmac_handler_rejection
        != reps2::runtime::EeDmacHandlerRejection::none) return 354;
    if (state.hle.last_dmac_channel != 5U || state.hle.last_dmac_handler != 0x0025C5F8U) {
        return 355;
    }
    if (state.hle.last_dmac_handler_id != 5 || state.gpr[2].low != 5U) return 356;
    return 0;
}

int test_add_dmac_handler_refuses_invalid_requests() {
    EeState state{};
    state.pc = 0xE900U;
    state.gpr[4].low = 10U;           // last channel is 9
    state.gpr[5].low = 0x0025C5F8U;
    state.gpr[6].low = 0U;
    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok) return 360;
    if (state.hle.last_dmac_handler_rejection
        != reps2::runtime::EeDmacHandlerRejection::invalid_channel) return 361;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0xFFFF'FFFFU
        || state.hle.dmac_handler_count != 0U) return 362;

    state.gpr[4].low = 5U;
    state.gpr[5].low = 0U;            // null handler
    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok
        || state.hle.last_dmac_handler_rejection
            != reps2::runtime::EeDmacHandlerRejection::invalid_handler) return 363;
    state.gpr[5].low = 0x0025C5FAU;   // misaligned handler
    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok
        || state.hle.last_dmac_handler_rejection
            != reps2::runtime::EeDmacHandlerRejection::invalid_handler) return 364;

    // A handler chain is not modelled: a non-zero `next` is refused, not dropped.
    state.gpr[5].low = 0x0025C5F8U;
    state.gpr[6].low = 1U;
    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok
        || state.hle.last_dmac_handler_rejection
            != reps2::runtime::EeDmacHandlerRejection::chained_handler_unsupported) return 365;

    state.gpr[6].low = 0U;
    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok
        || state.hle.dmac_handler_count != 1U) return 366;

    // One handler per channel: a second registration is refused and the record
    // is left exactly as the first call stored it.
    state.gpr[5].low = 0x0025C600U;
    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok
        || state.hle.last_dmac_handler_rejection
            != reps2::runtime::EeDmacHandlerRejection::channel_already_registered) return 367;
    if (state.hle.dmac_handler_count != 1U
        || state.hle.dmac_handlers[5U].handler != 0x0025C5F8U) return 368;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0xFFFF'FFFFU) return 369;
    return 0;
}

int test_add_dmac_handler_via_dispatch() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.pc = 0xE000U;
    state.gpr[3].low = kAddDmacHandler;
    state.gpr[4].low = 5U;
    state.gpr[5].low = 0x0025C5F8U;
    state.gpr[6].low = 0U;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 370;
    }
    if (state.hle.syscall_handled_count != 1U
        || state.hle.last_syscall_number != kAddDmacHandler) return 371;
    const auto& record = state.hle.dmac_handlers[5U];
    if (!record.in_use || record.handler != 0x0025C5F8U) return 372;
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 373;
    }
    return 0;
}

int test_enable_dmac_sets_the_channel_mask_bit() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.gpr[2].low = 0x1234U; // no established result contract
    // Seed an unrelated status field and an unrelated channel mask to prove both
    // survive the read-modify-write of the toggling mask register.
    state.memory.dmac.seed_d_stat((1U << 0U) | (1U << 18U));
    state.gpr[4].low = 5U; // SIF0

    if (reps2::runtime::hle_enable_dmac(state) != Status::ok) return 380;
    if (state.hle.last_enable_dmac_outcome
        != reps2::runtime::EeDmacEnableOutcome::enabled) return 381;
    if (state.hle.last_enable_dmac_channel != 5U) return 382;
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok) {
        return 383;
    }
    if ((d_stat & (1U << 21U)) == 0U) return 384; // SIF0 interrupt mask set
    if ((d_stat & (1U << 18U)) == 0U) return 385; // unrelated mask preserved
    if ((d_stat & (1U << 0U)) == 0U) return 386;  // status field preserved
    if (state.gpr[2].low != 0x1234U) return 387;  // $v0 untouched
    return 0;
}

int test_enable_dmac_is_idempotent_for_an_enabled_channel() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.gpr[4].low = 5U;
    if (reps2::runtime::hle_enable_dmac(state) != Status::ok
        || state.hle.last_enable_dmac_outcome
            != reps2::runtime::EeDmacEnableOutcome::enabled) return 390;
    // Writing the mask bit again would toggle it off, so the second call must
    // leave it enabled and report that nothing had to change.
    if (reps2::runtime::hle_enable_dmac(state) != Status::ok
        || state.hle.last_enable_dmac_outcome
            != reps2::runtime::EeDmacEnableOutcome::already_enabled) return 391;
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok) {
        return 392;
    }
    if ((d_stat & (1U << 21U)) == 0U) return 393;
    return 0;
}

int test_remove_dmac_handler_clears_the_record() {
    EeState state{};
    state.gpr[4].low = 5U;
    state.gpr[5].low = 0x0025C5F8U;
    state.gpr[6].low = 0U;
    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok) return 860;
    if (state.hle.dmac_handler_count != 1U
        || !state.hle.dmac_handlers[5U].in_use) return 861;

    state.gpr[2].low = 0x1234U; // no established result contract
    state.gpr[4].low = 5U;      // channel
    state.gpr[5].low = 5U;      // id the HLE AddDmacHandler returned
    if (reps2::runtime::hle_remove_dmac_handler(state) != Status::ok) return 862;
    if (state.hle.last_remove_dmac_outcome
        != reps2::runtime::EeDmacRemoveOutcome::removed) return 863;
    if (state.hle.last_remove_dmac_channel != 5U
        || state.hle.last_remove_dmac_id != 5U) return 864;
    if (state.hle.last_remove_dmac_handler != 0x0025C5F8U) return 865;
    if (state.hle.dmac_handlers[5U].in_use) return 866;
    if (state.hle.dmac_handler_count != 0U) return 867;
    if (state.gpr[2].low != 0x1234U) return 868; // $v0 untouched
    return 0;
}

int test_remove_dmac_handler_refuses_unknown_records() {
    EeState state{};
    // No registration at all.
    state.gpr[4].low = 5U;
    state.gpr[5].low = 5U;
    if (reps2::runtime::hle_remove_dmac_handler(state) != Status::ok) return 870;
    if (state.hle.last_remove_dmac_outcome
        != reps2::runtime::EeDmacRemoveOutcome::not_registered) return 871;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0xFFFF'FFFFU) return 872;

    // A registered channel with the wrong id is refused and left intact.
    state.gpr[4].low = 5U;
    state.gpr[5].low = 0x0025C5F8U;
    state.gpr[6].low = 0U;
    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok) return 873;
    state.gpr[4].low = 5U;
    state.gpr[5].low = 6U;
    if (reps2::runtime::hle_remove_dmac_handler(state) != Status::ok) return 874;
    if (state.hle.last_remove_dmac_outcome
        != reps2::runtime::EeDmacRemoveOutcome::not_registered) return 875;
    if (!state.hle.dmac_handlers[5U].in_use
        || state.hle.dmac_handlers[5U].handler != 0x0025C5F8U) return 876;

    // An out-of-range channel is refused without touching any record.
    state.gpr[4].low = 10U;
    state.gpr[5].low = 5U;
    if (reps2::runtime::hle_remove_dmac_handler(state) != Status::ok) return 877;
    if (state.hle.last_remove_dmac_outcome
        != reps2::runtime::EeDmacRemoveOutcome::invalid_channel) return 878;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0xFFFF'FFFFU) return 879;
    return 0;
}

int test_remove_dmac_handler_via_dispatch() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.syscall_enabled = true;
    state.gpr[4].low = 5U;
    state.gpr[5].low = 0x0025C5F8U;
    state.gpr[6].low = 0U;
    if (reps2::runtime::hle_add_dmac_handler(state) != Status::ok) return 880;

    state.pc = 0xE000U;
    state.gpr[3].low = kRemoveDmacHandler;
    state.gpr[4].low = 5U;
    state.gpr[5].low = 5U;
    state.gpr[31].low = kUnreachableReturnPc;
    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 881;
    }
    if (state.hle.syscall_handled_count != 1U
        || state.hle.last_syscall_number != kRemoveDmacHandler) return 882;
    if (state.hle.dmac_handlers[5U].in_use
        || state.hle.last_remove_dmac_outcome
            != reps2::runtime::EeDmacRemoveOutcome::removed) return 883;
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 884;
    }
    return 0;
}

int test_sif_stop_dma_clears_only_the_start_bit() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.memory.dmac.seed_chcr5(reps2::runtime::kEeDmacChcrStrMask | 0x4U);
    state.gpr[2].low = 0x1234U; // the SDK entry is void

    if (reps2::runtime::hle_sif_stop_dma(state) != Status::ok) return 890;
    if (!state.hle.last_sif_stop_dma_str_cleared) return 891;
    std::uint32_t chcr = 0U;
    if (!state.memory.dmac.read_chcr5(chcr)) return 892;
    if ((chcr & reps2::runtime::kEeDmacChcrStrMask) != 0U) return 893;
    if ((chcr & 0x4U) == 0U) return 894; // unrelated CHCR bits preserved
    if (state.gpr[2].low != 0x1234U) return 895;

    // An already stopped channel is left alone.
    if (reps2::runtime::hle_sif_stop_dma(state) != Status::ok) return 896;
    if (state.hle.last_sif_stop_dma_str_cleared) return 897;
    if (!state.memory.dmac.read_chcr5(chcr) || chcr != 0x4U) return 898;
    return 0;
}

int test_sif_stop_dma_fails_closed_without_a_known_channel() {
    EeState state{}; // no boot environment: CHCR5 is unknown
    if (reps2::runtime::hle_sif_stop_dma(state)
        != Status::unsupported_memory_access) return 899;
    return 0;
}

int test_sif_stop_dma_via_dispatch() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.syscall_enabled = true;
    state.memory.dmac.seed_chcr5(reps2::runtime::kEeDmacChcrStrMask);
    state.pc = 0xE000U;
    state.gpr[3].low = kSifStopDma;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 900;
    }
    if (state.hle.syscall_handled_count != 1U
        || state.hle.last_syscall_number != kSifStopDma) return 901;
    std::uint32_t chcr = 0U;
    if (!state.memory.dmac.read_chcr5(chcr)
        || (chcr & reps2::runtime::kEeDmacChcrStrMask) != 0U) return 902;
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 903;
    }
    return 0;
}

int test_disable_dmac_clears_the_channel_mask_bit() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.gpr[2].low = 0x1234U; // no established result contract
    state.memory.dmac.seed_d_stat((1U << 0U) | (1U << 18U) | (1U << 21U));
    state.gpr[4].low = 5U; // SIF0

    if (reps2::runtime::hle_disable_dmac(state) != Status::ok) return 770;
    if (state.hle.last_disable_dmac_outcome
        != reps2::runtime::EeDmacDisableOutcome::disabled) return 771;
    if (state.hle.last_disable_dmac_channel != 5U) return 772;
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok) {
        return 773;
    }
    if ((d_stat & (1U << 21U)) != 0U) return 774; // SIF0 interrupt mask cleared
    if ((d_stat & (1U << 18U)) == 0U) return 775; // unrelated mask preserved
    if ((d_stat & (1U << 0U)) == 0U) return 776;  // status field preserved
    if (state.gpr[2].low != 0x1234U) return 777;  // $v0 untouched
    return 0;
}

int test_disable_dmac_is_idempotent_for_a_disabled_channel() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.memory.dmac.seed_d_stat(1U << 0U);
    state.gpr[4].low = 5U;
    if (reps2::runtime::hle_disable_dmac(state) != Status::ok
        || state.hle.last_disable_dmac_outcome
            != reps2::runtime::EeDmacDisableOutcome::already_disabled) return 778;
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok) {
        return 779;
    }
    // Writing the mask bit again would toggle it on, so the register must be
    // unchanged.
    if (d_stat != (1U << 0U)) return 780;
    return 0;
}

int test_disable_dmac_refuses_an_invalid_channel() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.gpr[4].low = 10U; // last channel is 9
    if (reps2::runtime::hle_disable_dmac(state) != Status::ok) return 781;
    if (state.hle.last_disable_dmac_outcome
        != reps2::runtime::EeDmacDisableOutcome::invalid_channel) return 782;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0xFFFF'FFFFU) return 783;
    std::uint32_t d_stat = 1U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok) {
        return 784;
    }
    if (d_stat != 0U) return 785;
    return 0;
}

int test_enable_dmac_refuses_invalid_requests() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.gpr[4].low = 10U; // last channel is 9
    if (reps2::runtime::hle_enable_dmac(state) != Status::ok) return 400;
    if (state.hle.last_enable_dmac_outcome
        != reps2::runtime::EeDmacEnableOutcome::invalid_channel) return 401;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0xFFFF'FFFFU) return 402;
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok
        || d_stat != 0U) return 403;

    // Without an established register state the service stops instead of
    // guessing a value: the DMAC owner refuses the access.
    EeState uninitialized{};
    uninitialized.gpr[4].low = 5U;
    if (reps2::runtime::hle_enable_dmac(uninitialized)
        != Status::unsupported_memory_access) return 404;
    if (uninitialized.hle.last_enable_dmac_outcome
        != reps2::runtime::EeDmacEnableOutcome::unknown_register_state) return 405;
    return 0;
}

int test_enable_dmac_via_dispatch() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.syscall_enabled = true;
    state.pc = 0xE000U;
    state.gpr[3].low = kEnableDmac;
    state.gpr[4].low = 5U;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 410;
    }
    if (state.hle.syscall_handled_count != 1U
        || state.hle.last_syscall_number != kEnableDmac) return 411;
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok
        || (d_stat & (1U << 21U)) == 0U) return 412;
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 413;
    }
    return 0;
}

int test_disable_dmac_via_dispatch() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.syscall_enabled = true;
    state.memory.dmac.seed_d_stat(1U << 21U); // SIF0 interrupt mask set
    state.pc = 0xE000U;
    state.gpr[3].low = kDisableDmac;
    state.gpr[4].low = 5U;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 786;
    }
    if (state.hle.syscall_handled_count != 1U
        || state.hle.last_syscall_number != kDisableDmac) return 787;
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok
        || (d_stat & (1U << 21U)) != 0U) return 788;
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 789;
    }
    return 0;
}

int test_sif_get_reg_reports_the_uninitialised_registers() {
    EeState state{};
    // Before the IOP SIFCMD device starts, every register is zero. The observed
    // read: SIF_SYSREG_SUBADDR (SIF_REG_ID_SYSTEM | 0).
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok) return 420;
    if (state.hle.last_sif_register_rejection
        != reps2::runtime::EeSifRegisterRejection::none) return 421;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0U) return 422;
    if (state.hle.last_sif_register != reps2::runtime::kEeSifRegisterSystemBase
        || state.hle.last_sif_register_value != 0U) return 423;

    // Every documented register reads as zero while no IOP exists.
    for (std::uint32_t reg = 1U; reg <= 4U; ++reg) {
        state.gpr[4].low = reg;
        if (reps2::runtime::hle_sif_get_reg(state) != Status::ok
            || static_cast<std::uint32_t>(state.gpr[2].low) != 0U) return 424;
    }
    for (std::uint32_t reg = 0U; reg < 3U; ++reg) {
        state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase + reg;
        if (reps2::runtime::hle_sif_get_reg(state) != Status::ok
            || static_cast<std::uint32_t>(state.gpr[2].low) != 0U) return 425;
    }
    if (state.hle.sif_get_reg_calls != 8U) return 426;
    return 0;
}

int test_sif_get_reg_returns_a_modelled_value() {
    EeState state{};
    // A stored flag must come back verbatim: the service reads the register file
    // rather than answering with a constant zero.
    state.hle.sif_hardware_registers[reps2::runtime::kEeSifRegisterSmFlag] = 0x0004'0000U;
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSmFlag;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok) return 430;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0x0004'0000U) return 431;
    if (state.hle.last_sif_register_value != 0x0004'0000U) return 432;
    return 0;
}

int test_sif_get_reg_stops_on_an_unmodelled_register() {
    EeState state{};
    state.gpr[2].low = 0x1234U;
    state.gpr[4].low = 0U; // register numbers start at 1
    if (reps2::runtime::hle_sif_get_reg(state) != Status::unhandled_syscall) return 440;
    if (state.hle.last_sif_register_rejection
        != reps2::runtime::EeSifRegisterRejection::unknown_register) return 441;
    if (state.gpr[2].low != 0x1234U) return 442;

    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase + 3U; // past RPCINIT
    if (reps2::runtime::hle_sif_get_reg(state) != Status::unhandled_syscall) return 443;
    if (state.gpr[2].low != 0x1234U) return 444;
    return 0;
}

int test_sif_get_reg_via_dispatch() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.pc = 0xE000U;
    state.gpr[3].low = kSifGetReg;
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 450;
    }
    if (state.hle.syscall_handled_count != 1U
        || state.hle.last_syscall_number != kSifGetReg) return 451;
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 452;
    }
    return 0;
}

int test_sif_set_reg_writes_the_shared_register_file() {
    EeState state{};
    // Write a hardware register and read it back through sceSifGetReg.
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSmFlag;
    state.gpr[5].low = 0xABCD'1234U;
    if (reps2::runtime::hle_sif_set_reg(state) != Status::ok) return 460;
    if (state.hle.last_sif_set_register_rejection
        != reps2::runtime::EeSifRegisterRejection::none) return 461;
    if (state.hle.last_sif_set_register != reps2::runtime::kEeSifRegisterSmFlag) return 462;

    state.gpr[4].low = reps2::runtime::kEeSifRegisterSmFlag;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok) return 463;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0xABCD'1234U) return 464;
    if (state.hle.last_sif_register_value != 0xABCD'1234U) return 465;

    // Write a system register and read it back.
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase + 1U;
    state.gpr[5].low = 0xDEAD'BEEFU;
    if (reps2::runtime::hle_sif_set_reg(state) != Status::ok) return 466;
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase + 1U;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok) return 467;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0xDEAD'BEEFU) return 468;
    return 0;
}

int test_sif_set_reg_stops_on_an_unmodelled_register() {
    EeState state{};
    state.gpr[2].low = 0x1234U;
    state.gpr[4].low = 0U; // register numbers start at 1
    state.gpr[5].low = 0xFFFF'FFFFU;
    if (reps2::runtime::hle_sif_set_reg(state) != Status::unhandled_syscall) return 470;
    if (state.hle.last_sif_set_register_rejection
        != reps2::runtime::EeSifRegisterRejection::unknown_register) return 471;
    if (state.gpr[2].low != 0x1234U) return 472;

    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase + 3U; // past RPCINIT
    if (reps2::runtime::hle_sif_set_reg(state) != Status::unhandled_syscall) return 473;
    if (state.gpr[2].low != 0x1234U) return 474;
    return 0;
}

int test_sif_set_reg_via_dispatch() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.pc = 0xE000U;
    state.gpr[3].low = kSifSetReg;
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSmFlag;
    state.gpr[5].low = 0x8765'4321U;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 480;
    }
    if (state.hle.syscall_handled_count != 1U
        || state.hle.last_syscall_number != kSifSetReg) return 481;
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 482;
    }
    if (state.hle.sif_hardware_registers[reps2::runtime::kEeSifRegisterSmFlag] != 0x8765'4321U) {
        return 483;
    }
    return 0;
}

int test_sif_set_dma_programs_and_starts_sif0() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);

    // Build a two-entry SifDmaTransfer_t array at 0x100.
    store_word(ram, 0x100U, 0x2000U);       // src[0]
    store_word(ram, 0x104U, 0x3000U);       // dest[0]
    store_word(ram, 0x108U, 64U);           // size[0] = 4 quadwords
    store_word(ram, 0x10CU, 0x4U);          // attr[0] = SIF_DMA_INT_O
    store_word(ram, 0x110U, 0x4000U);       // src[1]
    store_word(ram, 0x114U, 0x5000U);       // dest[1]
    store_word(ram, 0x118U, 32U);           // size[1] = 2 quadwords
    store_word(ram, 0x11CU, 0x44U);         // attr[1] = ERT | INT_O

    state.gpr[4].low = 0x100U;              // dmat
    state.gpr[5].low = 2U;                  // count
    if (reps2::runtime::hle_sif_set_dma(state) != Status::ok) return 490;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 2U) return 491; // transfer id = 2

    // MADR5 should hold the last source address.
    std::uint32_t madr = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacMadr5Address, madr) != Status::ok) return 492;
    if (madr != 0x4000U) return 493;

    // QWC5 should hold the last size (32 bytes = 2 quadwords).
    std::uint32_t qwc = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacQwc5Address, qwc) != Status::ok) return 494;
    if (qwc != 2U) return 495;

    // D_STAT SIF0 event should be set because CHCR_STR was raised.
    std::uint32_t d_stat = 0U;
    if (reps2::runtime::load_u32(
            state.memory, reps2::runtime::kEeDmacDStatAddress, d_stat) != Status::ok) return 496;
    if ((d_stat & reps2::runtime::kEeDmacDStatSif0StatusBit) == 0U) return 497;
    return 0;
}

int test_sif_set_dma_rejects_invalid_count() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    state.gpr[4].low = 0x100U;
    state.gpr[5].low = 0U; // invalid count
    if (reps2::runtime::hle_sif_set_dma(state) != Status::ok) return 500;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0U) return 501;

    state.gpr[5].low = 3U; // count > 2 not supported
    if (reps2::runtime::hle_sif_set_dma(state) != Status::ok) return 502;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0U) return 503;
    return 0;
}

int test_sif_set_dma_via_dispatch() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.syscall_enabled = true;
    state.pc = 0xE000U;
    state.gpr[3].low = kSifSetDma;
    state.gpr[4].low = 0x100U;
    state.gpr[5].low = 1U;
    store_word(ram, 0x100U, 0x2000U);
    store_word(ram, 0x104U, 0x3000U);
    store_word(ram, 0x108U, 16U);
    store_word(ram, 0x10CU, 0x44U);
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 510;
    }
    if (state.hle.syscall_handled_count != 1U
        || state.hle.last_syscall_number != kSifSetDma) return 511;
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 512;
    }
    return 0;
}

int test_delay_slot_syscall_resumes_at_the_taken_target() {
    EeState state{};
    state.pc = 0xE200U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupThread;
    state.gpr[8].low = 0U; // t0 == 0 takes the branch
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 70;
    }
    // 9 comes from the taken target; 7 would mean the fallthrough was resumed.
    if (state.gpr[2].low != 9U) {
        return 71;
    }
    if (state.hle.syscall_handled_count != 1U) {
        return 72;
    }
    if (state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 73;
    }
    return 0;
}

int test_delay_slot_syscall_resumes_at_the_fallthrough() {
    EeState state{};
    state.pc = 0xE200U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupThread;
    state.gpr[8].low = 1U; // t0 != 0: not taken, the slot still executes
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 80;
    }
    if (state.gpr[2].low != 7U) {
        return 81;
    }
    if (state.hle.syscall_handled_count != 1U) {
        return 82;
    }
    return 0;
}

int test_setup_heap_with_explicit_range() {
    EeState state{};
    state.pc = 0xE300U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetupHeap;
    state.gpr[4].low = 0x00100000U; // $a0: heap start
    state.gpr[5].low = 0x00008000U; // $a1: heap size
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 95;
    }
    if (!state.hle.setup_heap_called
        || state.hle.setup_heap_start != 0x00100000U
        || state.hle.setup_heap_size != 0x00008000U
        || state.hle.setup_heap_end != 0x00108000U) {
        return 96;
    }
    // The fixture's first post-trap instruction overwrites $v0 with its resume
    // marker; the host observation preserves the service result independently.
    if (state.gpr[2].low != 1U) {
        return 97;
    }
    return 0;
}

int test_setup_heap_size_sentinel_uses_setup_thread_stack() {
    EeState state{};
    state.pc = 0xE300U;
    state.hle.syscall_enabled = true;
    state.hle.setup_thread_called = true;
    state.hle.setup_thread_stack_pointer = 0x01FF0000U;
    state.gpr[3].low = kSetupHeap;
    state.gpr[4].low = 0x008A0400U; // retail heap start
    state.gpr[5].low = 0xFFFFFFFFU; // ps2sdk/retail observed size sentinel
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 98;
    }
    if (state.hle.setup_heap_end != 0x01FF0000U || state.gpr[2].low != 1U) {
        return 99;
    }
    return 0;
}

int test_create_sema_returns_a_unique_id() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE400U;
    state.hle.syscall_enabled = true;
    // count/max_count/init_count/wait_threads: only the middle two are inputs.
    store_sema_parameters(ram, kSemaParameters, 0, 4, 2, 0, 0U, 0U);
    state.gpr[3].low = kCreateSema;
    state.gpr[4].low = kSemaParameters;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 110;
    }
    // The fixture does not touch $v0, so this is the service's own result.
    if (state.gpr[2].low != 0U) {
        return 111;
    }
    if (state.hle.last_sema_id != 0 || state.hle.semaphore_count != 1U) {
        return 112;
    }
    if (state.hle.last_sema_init_count != 2 || state.hle.last_sema_max_count != 4) {
        return 113;
    }
    const auto& sema = state.hle.semaphores[0];
    if (!sema.in_use || sema.count != 2 || sema.max_count != 4
        || sema.attr != 0U || sema.option != 0U) {
        return 114;
    }
    if (sema.create_pc != 0xE400U || state.hle.last_sema_rejection
        != reps2::runtime::EeSemaRejection::none) {
        return 115;
    }
    // Only $v0 changed: the service number register and the argument registers
    // are not invented side effects, and no COP0 context was created.
    if (state.gpr[3].low != kCreateSema || state.gpr[4].low != kSemaParameters
        || state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 116;
    }
    // The guest parameter block is an input, not a status mirror.
    if (load_word(ram, kSemaParameters) != 0U
        || load_word(ram, kSemaParameters + 4U) != 4U
        || load_word(ram, kSemaParameters + 8U) != 2U) {
        return 117;
    }
    // A second call must not reuse the first id.
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.gpr[2].low != 1U || state.hle.semaphore_count != 2U) {
        return 118;
    }
    return 0;
}

int test_create_sema_consumes_only_the_documented_result() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE400U;
    state.hle.syscall_enabled = true;
    // The ps2sdk "i" wrappers pass the negated service number; CreateSema has no
    // such wrapper, but dispatch normalizes by magnitude regardless.
    store_sema_parameters(ram, kSemaParameters, 0, 1, 1, 0, 0U, 0U);
    state.gpr[3].low = static_cast<std::uint64_t>(0U - kCreateSema);
    state.gpr[4].low = kSemaParameters;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 120;
    }
    if (state.hle.last_syscall_number != kCreateSema || state.hle.last_sema_id != 0) {
        return 121;
    }
    if (state.gpr[2].low != 0U) {
        return 122;
    }
    return 0;
}

int test_create_sema_rejects_inconsistent_counts() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;

    // init_count above max_count can never be represented by the object.
    store_sema_parameters(ram, kSemaParameters, 0, 2, 5, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 130;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.last_sema_rejection != reps2::runtime::EeSemaRejection::init_exceeds_max) {
        return 131;
    }
    if (state.hle.semaphore_count != 0U || state.hle.last_sema_id != -1) {
        return 132;
    }

    // A negative count is not a count.
    store_sema_parameters(ram, kSemaParameters, 0, -1, 0, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.hle.last_sema_rejection != reps2::runtime::EeSemaRejection::negative_counts) {
        return 133;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL || state.hle.semaphore_count != 0U) {
        return 134;
    }

    // An equal pair is accepted: the bound is inclusive.
    store_sema_parameters(ram, kSemaParameters, 0, 3, 3, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok || state.gpr[2].low != 0U
        || state.hle.semaphores[0].count != 3 || state.hle.semaphores[0].max_count != 3) {
        return 135;
    }
    return 0;
}

int test_create_sema_rejects_unreadable_parameters() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    store_sema_parameters(ram, kSemaParameters, 0, 1, 1, 0, 0U, 0U);

    // A misaligned pointer is refused rather than read partially.
    state.gpr[4].low = kSemaParameters + 2U;
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.hle.last_sema_rejection
            != reps2::runtime::EeSemaRejection::unreadable_parameters) {
        return 140;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL || state.hle.semaphore_count != 0U) {
        return 141;
    }

    // A block that runs past the end of main RAM is refused, not truncated.
    state.gpr[4].low = static_cast<std::uint32_t>(ram.size()) - 8U;
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.hle.last_sema_rejection
            != reps2::runtime::EeSemaRejection::unreadable_parameters) {
        return 142;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL || state.hle.semaphore_count != 0U) {
        return 143;
    }

    // An address outside any RAM alias is refused too.
    state.gpr[4].low = 0xFFF0'0000U;
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.hle.semaphore_count != 0U || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) {
        return 144;
    }
    return 0;
}

int test_create_sema_table_is_bounded() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    store_sema_parameters(ram, kSemaParameters, 0, 1, 0, 0, 0U, 0U);
    state.gpr[4].low = kSemaParameters;

    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(reps2::runtime::kEeMaxSemaphores);
         ++index) {
        if (reps2::runtime::hle_create_sema(state) != Status::ok) {
            return 150;
        }
        if (state.gpr[2].low != static_cast<std::uint64_t>(index)) {
            return 151;
        }
    }
    // The documented kernel limit is a hard bound, and the refusal is the
    // documented -1 rather than a wrapped-around id.
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 152;
    }
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.last_sema_rejection != reps2::runtime::EeSemaRejection::table_full) {
        return 153;
    }
    if (state.hle.semaphore_count != reps2::runtime::kEeMaxSemaphores
        || state.hle.last_sema_id != -1) {
        return 154;
    }
    return 0;
}

int test_signal_sema_raises_the_count() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;
    store_sema_parameters(ram, kSemaParameters, 0, 2, 0, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok || state.gpr[2].low != 0U) {
        return 700;
    }
    state.gpr[4].low = 0U;
    if (reps2::runtime::hle_signal_sema(state) != Status::ok || state.gpr[2].low != 0U) {
        return 701;
    }
    if (state.hle.semaphores[0].count != 1 || state.hle.last_sema_op_count_after != 1
        || !state.hle.last_sema_op_valid || state.hle.signal_sema_calls != 1U) {
        return 702;
    }
    // A second signal reaches max_count; a third cannot raise the count and is
    // refused with the documented failure value, changing nothing.
    if (reps2::runtime::hle_signal_sema(state) != Status::ok || state.gpr[2].low != 0U
        || state.hle.semaphores[0].count != 2) {
        return 703;
    }
    state.gpr[2].low = 0x1234U;
    if (reps2::runtime::hle_signal_sema(state) != Status::ok
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.semaphores[0].count != 2 || state.hle.last_sema_op_valid
        || state.hle.last_sema_op_count_after != -1) {
        return 704;
    }
    // An id that names no live object is the documented failure, not a signal,
    // and the live object is left untouched.
    state.gpr[4].low = 7U;
    if (reps2::runtime::hle_signal_sema(state) != Status::ok
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL || state.hle.last_sema_op_valid
        || state.hle.semaphores[0].count != 2) {
        return 705;
    }
    return 0;
}

int test_wait_sema_refuses_a_negative_id() {
    EeState state{};
    state.hle.syscall_enabled = true;
    state.gpr[2].low = 0x1234U;
    state.gpr[4].low = 0xFFFF'FFFFU;  // -1 as a 32-bit id
    // A negative id resolves to no live object: the documented failure, not a
    // block, and no stale block flag is left behind.
    if (reps2::runtime::hle_wait_sema(state) != Status::ok
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.last_sema_wait_blocked) {
        return 760;
    }
    if (reps2::runtime::hle_delete_sema(state) != Status::ok
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) {
        return 761;
    }
    return 0;
}

int test_wait_sema_consumes_a_free_count() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;
    store_sema_parameters(ram, kSemaParameters, 0, 2, 1, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 710;
    }
    state.gpr[2].low = 0x1234U;  // sentinel: a serviced wait overwrites it with 0
    state.gpr[4].low = 0U;
    if (reps2::runtime::hle_wait_sema(state) != Status::ok || state.gpr[2].low != 0U) {
        return 711;
    }
    if (state.hle.semaphores[0].count != 0 || state.hle.last_sema_wait_blocked
        || state.hle.last_sema_wait_count_before != 1
        || state.hle.last_sema_op_count_after != 0) {
        return 712;
    }
    return 0;
}

int test_wait_sema_blocks_on_a_zero_count() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE400U;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;
    store_sema_parameters(ram, kSemaParameters, 0, 4, 0, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 720;
    }
    state.gpr[2].low = 0x1234U;  // sentinel result the guest must keep
    state.gpr[3].low = kWaitSema;
    state.gpr[4].low = 0U;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 8U);
    if (status != Status::semaphore_wait_blocked) {
        return 721;
    }
    // The stop is a host boundary at the SYSCALL and no result is fabricated.
    if (state.pc != 0xE400U || state.gpr[2].low != 0x1234U) {
        return 722;
    }
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U
        || (state.cop0[reps2::runtime::kCop0Status] & reps2::runtime::kCop0StatusExlMask) != 0U) {
        return 723;
    }
    if (!state.hle.last_sema_wait_blocked || state.hle.last_sema_wait_count_before != 0
        || state.hle.semaphores[0].count != 0) {
        return 724;
    }
    if (state.hle.syscall_count != 1U || state.hle.syscall_handled_count != 0U) {
        return 725;
    }
    return 0;
}

int test_wait_sema_refuses_an_invalid_id() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE400U;
    state.hle.syscall_enabled = true;
    state.gpr[2].low = 0x1234U;
    state.gpr[3].low = kWaitSema;
    state.gpr[4].low = 9U;  // no semaphore was created
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 8U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) {
        return 730;
    }
    // An invalid id is the documented failure, so the call completes rather than
    // blocking; the guest sees -1, not a fabricated success.
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) {
        return 731;
    }
    if (state.hle.last_sema_wait_blocked || state.hle.last_sema_op_valid
        || state.hle.wait_sema_calls != 1U) {
        return 732;
    }
    return 0;
}

int test_poll_sema_success_returns_the_semaphore_id() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;
    store_sema_parameters(ram, kSemaParameters, 0, 2, 1, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 940;
    }
    if (reps2::runtime::hle_create_sema(state) != Status::ok || state.gpr[2].low != 1U) {
        return 941;
    }
    constexpr std::uint32_t sema_id = 1U;
    state.gpr[2].low = 0x1234U;
    state.gpr[4].low = sema_id;
    if (reps2::runtime::hle_poll_sema(state) != Status::ok
        || state.gpr[2].low != sema_id) return 942;
    if (state.hle.semaphores[sema_id].count != 0 || !state.hle.last_sema_poll_acquired
        || state.hle.last_sema_poll_count_before != 1
        || state.hle.last_sema_op_count_after != 0 || state.hle.poll_sema_calls != 1U
        || state.hle.first_sema_poll_count_before != 1
        || state.hle.poll_sema_acquired_total != 1U
        || state.hle.last_sema_op_valid == false) {
        return 943;
    }
    return 0;
}

int test_poll_sema_on_a_zero_count_returns_failure_without_blocking() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE400U;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;
    store_sema_parameters(ram, kSemaParameters, 0, 4, 0, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 943;
    }
    state.gpr[3].low = kPollSema;
    state.gpr[4].low = 0U;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 8U);
    // PollSema never blocks; a zero count is the documented failure result, so
    // the call completes and the guest continues with -1.
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) return 944;
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) return 945;
    if (state.hle.last_sema_poll_acquired || state.hle.last_sema_poll_count_before != 0
        || state.hle.semaphores[0].count != 0 || state.hle.last_sema_wait_blocked
        || state.hle.first_sema_poll_count_before != 0
        || state.hle.poll_sema_acquired_total != 0U
        || state.hle.poll_sema_calls != 1U) {
        return 946;
    }
    return 0;
}

int test_poll_sema_refuses_an_invalid_id() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.gpr[2].low = 0x1234U;
    state.gpr[4].low = 9U; // no semaphore was created
    if (reps2::runtime::hle_poll_sema(state) != Status::ok) return 947;
    if (state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) return 948;
    if (state.hle.last_sema_poll_acquired || state.hle.last_sema_op_valid
        || state.hle.last_sema_poll_count_before != -1 || state.hle.poll_sema_calls != 1U) {
        return 949;
    }
    return 0;
}

int test_poll_sema_via_dispatch_including_the_negated_form() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE400U;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;
    store_sema_parameters(ram, kSemaParameters, 0, 4, 2, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 950;
    }
    state.gpr[3].low = kPollSema;
    state.gpr[4].low = 0U;
    state.gpr[31].low = kUnreachableReturnPc;
    if (reps2::generated::execute_cfg(state, 8U) != Status::external_control_flow
        || state.pc != kUnreachableReturnPc || state.gpr[2].low != 0U) {
        return 951;
    }
    // iPollSema is issued as the negated number; dispatch resolves it by
    // magnitude, so both numbers share the handler.
    state.pc = 0xE400U;
    state.gpr[3].low = 0U - kIPollSema;
    state.gpr[4].low = 0U;
    state.gpr[31].low = kUnreachableReturnPc;
    if (reps2::generated::execute_cfg(state, 8U) != Status::external_control_flow
        || state.pc != kUnreachableReturnPc || state.gpr[2].low != 0U) {
        return 952;
    }
    if (state.hle.poll_sema_calls != 2U || state.hle.semaphores[0].count != 0) return 953;
    return 0;
}

int test_delete_sema_frees_the_slot() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;
    store_sema_parameters(ram, kSemaParameters, 0, 1, 1, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok
        || state.hle.semaphore_count != 1U) {
        return 740;
    }
    state.gpr[4].low = 0U;
    if (reps2::runtime::hle_delete_sema(state) != Status::ok || state.gpr[2].low != 0U) {
        return 741;
    }
    if (state.hle.semaphores[0].in_use || state.hle.semaphore_count != 0U
        || !state.hle.last_sema_op_valid || state.hle.delete_sema_calls != 1U) {
        return 742;
    }
    // Deleting the now-free id is the documented failure, not a second success.
    if (reps2::runtime::hle_delete_sema(state) != Status::ok
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) {
        return 743;
    }
    return 0;
}

int test_semaphore_services_via_dispatch() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = kSemaParameters;
    store_sema_parameters(ram, kSemaParameters, 0, 4, 1, 0, 0U, 0U);
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 750;
    }
    state.gpr[31].low = kUnreachableReturnPc;

    // WaitSema consumes the single free count and resumes in guest code.
    state.pc = 0xE400U;
    state.gpr[3].low = kWaitSema;
    state.gpr[4].low = 0U;
    if (reps2::generated::execute_cfg(state, 8U) != Status::external_control_flow
        || state.pc != kUnreachableReturnPc || state.gpr[2].low != 0U) {
        return 751;
    }
    if (state.hle.wait_sema_calls != 1U || state.hle.semaphores[0].count != 0) {
        return 752;
    }

    // The count is now zero: the next wait must stop, not fabricate success.
    state.pc = 0xE400U;
    state.gpr[2].low = 0x1234U;
    state.gpr[4].low = 0U;
    if (reps2::generated::execute_cfg(state, 8U) != Status::semaphore_wait_blocked
        || state.gpr[2].low != 0x1234U || state.pc != 0xE400U) {
        return 753;
    }

    // iSignalSema passes the negated number; dispatch resolves it by magnitude.
    state.pc = 0xE400U;
    state.gpr[3].low = static_cast<std::uint64_t>(0U - kISignalSema);
    state.gpr[4].low = 0U;
    if (reps2::generated::execute_cfg(state, 8U) != Status::external_control_flow
        || state.pc != kUnreachableReturnPc) {
        return 754;
    }
    if (state.hle.semaphores[0].count != 1 || state.hle.signal_sema_calls != 1U) {
        return 755;
    }

    // DeleteSema frees the slot; the table reports it empty again.
    state.pc = 0xE400U;
    state.gpr[3].low = kDeleteSema;
    state.gpr[4].low = 0U;
    if (reps2::generated::execute_cfg(state, 8U) != Status::external_control_flow
        || state.gpr[2].low != 0U) {
        return 756;
    }
    if (state.hle.semaphores[0].in_use || state.hle.semaphore_count != 0U) {
        return 757;
    }
    return 0;
}

int test_set_syscall_registers_guest_handler() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE500U;
    state.hle.syscall_enabled = true;
    state.gpr[3].low = kSetSyscall;
    state.gpr[4].low = 0x83U;
    state.gpr[5].low = 0x0025EF38U;
    state.gpr[31].low = kUnreachableReturnPc;
    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::external_control_flow || state.pc != kUnreachableReturnPc) return 160;
    const auto& registration = state.hle.syscall_registrations[0x83U];
    if (!registration.in_use || registration.handler_address != 0x0025EF38U) return 161;
    if (state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::none) return 162;
    if (state.gpr[2].low != 0U || state.gpr[3].low != kSetSyscall) return 163;
    // The handler is also published into the guest-visible kernel table, which
    // is what lets a title recover the table base by scanning low EE RAM.
    const std::uint32_t slot_offset = 0x83U * 4U;
    std::uint32_t table_word = 0;
    if (reps2::runtime::load_u32(
            state.memory,
            reps2::runtime::kEeSyscallTableBase + slot_offset,
            table_word) != Status::ok
        || table_word != 0x0025EF38U) return 164;
    if (load_word(ram, 0x0001'1F80U + slot_offset) != 0x0025EF38U) return 165;
    return 0;
}

int test_get_entry_address_returns_slot_address() {
    EeState state{};
    state.gpr[4].low = 0x12CU;
    if (reps2::runtime::hle_get_entry_address(state) != Status::ok
        || static_cast<std::uint32_t>(state.gpr[2].low)
            != reps2::runtime::kEeSyscallTableBase + 0x12CU * 4U) return 195;
    // libosd documents the 32-bit modular form for a negative slot:
    // 0x80011F80 + (-15358 * 4) == 0x80002F88.
    state.gpr[4].low = 0xFFFF'C402U;
    if (reps2::runtime::hle_get_entry_address(state) != Status::ok
        || static_cast<std::uint32_t>(state.gpr[2].low) != 0x8000'2F88U) return 196;
    return 0;
}

int test_set_syscall_accepts_extended_alarm_slot() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    // ps2sdk InitAlarm patches slot 0x12C (Intc12Handler) with the address
    // returned by GetEntryAddress(0x12C).
    state.gpr[4].low = 0x12CU;
    state.gpr[5].low = reps2::runtime::kEeSyscallTableBase + 0x12CU * 4U;
    if (reps2::runtime::hle_set_syscall(state) != Status::ok
        || state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::none) return 197;
    if (load_word(ram, 0x0001'1F80U + 0x12CU * 4U) != 0x8001'24'30U) return 198;
    const auto extended =
        reps2::runtime::find_ee_syscall_registration(state.hle, 0x12CU);
    if (!extended.in_use || extended.handler_address != 0x8001'2430U) return 199;
    return 0;
}

int test_create_thread_returns_dormant_id() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    constexpr std::uint32_t kParameters = 0x0000'0100U;
    store_thread_parameters(
        ram, kParameters, 0xDEAD'BEEFU, 0x0000'E600U, 0x0000'0800U,
        0x0000'0100U, 0x0000'E700U, 1U);
    state.gpr[4].low = kParameters;
    if (reps2::runtime::hle_create_thread(state) != Status::ok) return 202;
    if (state.hle.last_thread_rejection != reps2::runtime::EeThreadRejection::none) return 203;
    if (state.hle.last_thread_id != reps2::runtime::kEeFirstCreatedThreadId
        || state.hle.thread_count != 1U) return 204;
    if (state.gpr[2].low != reps2::runtime::kEeFirstCreatedThreadId) return 205;
    const auto& thread = state.hle.threads[0];
    if (!thread.in_use || thread.function != 0xE600U || thread.stack != 0x800U
        || thread.stack_size != 0x100U || thread.gp != 0xE700U
        || thread.initial_priority != 1U) return 206;
    // The guest parameter block is input only: the uninitialized status word is
    // never interpreted or rewritten.
    if (load_word(ram, kParameters) != 0xDEAD'BEEFU) return 207;
    return 0;
}

int test_create_thread_rejects_invalid_parameters() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    constexpr std::uint32_t kParameters = 0x0000'0100U;

    state.gpr[4].low = 0x0000'1000U; // parameter block starts past the window
    if (reps2::runtime::hle_create_thread(state) != Status::ok
        || state.hle.last_thread_rejection
            != reps2::runtime::EeThreadRejection::unreadable_parameters) return 210;

    store_thread_parameters(
        ram, kParameters, 0U, 0U, 0x800U, 0x100U, 0xE700U, 1U);
    state.gpr[4].low = kParameters;
    if (reps2::runtime::hle_create_thread(state) != Status::ok
        || state.hle.last_thread_rejection != reps2::runtime::EeThreadRejection::invalid_func) return 211;

    store_thread_parameters(
        ram, kParameters, 0U, 0xE600U, 0x800U, 0U, 0xE700U, 1U);
    if (reps2::runtime::hle_create_thread(state) != Status::ok
        || state.hle.last_thread_rejection != reps2::runtime::EeThreadRejection::invalid_stack) return 212;

    store_thread_parameters(
        ram, kParameters, 0U, 0xE600U, 0x0FFF'F000U, 0x2000U, 0xE700U, 1U);
    if (reps2::runtime::hle_create_thread(state) != Status::ok
        || state.hle.last_thread_rejection != reps2::runtime::EeThreadRejection::invalid_stack) return 213;

    store_thread_parameters(
        ram, kParameters, 0U, 0xE600U, 0x800U, 0x100U, 0xFFF0'0000U, 1U);
    if (reps2::runtime::hle_create_thread(state) != Status::ok
        || state.hle.last_thread_rejection != reps2::runtime::EeThreadRejection::invalid_gp) return 214;

    store_thread_parameters(
        ram, kParameters, 0U, 0xE600U, 0x800U, 0x100U, 0xE700U, 128U);
    if (reps2::runtime::hle_create_thread(state) != Status::ok
        || state.hle.last_thread_rejection != reps2::runtime::EeThreadRejection::invalid_priority) return 215;

    if (state.hle.thread_count != 0U || state.hle.last_thread_id != -1
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) return 216;
    return 0;
}

int test_create_thread_table_is_bounded() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    constexpr std::uint32_t kParameters = 0x0000'0100U;
    store_thread_parameters(
        ram, kParameters, 0U, 0xE600U, 0x800U, 0x100U, 0xE700U, 1U);
    state.gpr[4].low = kParameters;
    for (std::uint32_t offset = 0; offset < reps2::runtime::kEeMaxThreads; ++offset) {
        const std::uint32_t expected = reps2::runtime::kEeFirstCreatedThreadId + offset;
        if (reps2::runtime::hle_create_thread(state) != Status::ok
            || state.gpr[2].low != expected) return 217;
    }
    if (reps2::runtime::hle_create_thread(state) != Status::ok
        || state.hle.last_thread_rejection != reps2::runtime::EeThreadRejection::table_full
        || state.hle.thread_count != reps2::runtime::kEeMaxThreads
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) return 218;
    return 0;
}

int test_create_thread_via_dispatch() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.pc = 0xE000U;
    state.hle.syscall_enabled = true;
    constexpr std::uint32_t kParameters = 0x0000'0100U;
    store_thread_parameters(
        ram, kParameters, 0U, 0xE600U, 0x800U, 0x100U, 0xE700U, 1U);
    state.gpr[3].low = reps2::runtime::kEeSyscallCreateThread;
    state.gpr[4].low = kParameters;
    state.gpr[31].low = kUnreachableReturnPc;
    if (reps2::generated::execute_cfg(state, 32U) != Status::external_control_flow
        || state.hle.thread_count != 1U
        || state.hle.last_thread_id != reps2::runtime::kEeFirstCreatedThreadId) return 219;
    return 0;
}

// Creates one thread through the handler and returns its id, or 0 when the
// handler refused. Ids start at 2, so 0 is unambiguous as a failure marker.
std::uint32_t create_thread_with_priority(
    EeState& state,
    RamWindow& ram,
    std::uint32_t priority
) {
    constexpr std::uint32_t kThreadParameters = 0x0000'0100U;
    store_thread_parameters(
        ram, kThreadParameters, 0U, 0xE600U, 0x800U, 0x100U, 0xE700U, priority);
    state.gpr[4].low = kThreadParameters;
    if (reps2::runtime::hle_create_thread(state) != Status::ok) {
        return 0U;
    }
    return static_cast<std::uint32_t>(state.gpr[2].low);
}

int test_start_thread_transitions_to_ready() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    const std::uint32_t id = create_thread_with_priority(state, ram, 1U);
    if (id == 0U) return 230;
    if (state.hle.threads[id - reps2::runtime::kEeFirstCreatedThreadId].status
        != reps2::runtime::kEeThreadStatusDormant) return 231;
    state.gpr[4].low = id;
    state.gpr[5].low = 0x0076'1208U;
    if (reps2::runtime::hle_start_thread(state) != Status::ok) return 232;
    if (state.gpr[2].low != 0U) return 233;
    const auto& thread =
        state.hle.threads[id - reps2::runtime::kEeFirstCreatedThreadId];
    if (thread.status != reps2::runtime::kEeThreadStatusReady) return 234;
    if (thread.argv != 0x0076'1208U) return 235;
    if (state.hle.ready_count != 1U || state.hle.ready_queue[0] != id) return 236;
    if (state.hle.last_start_rejection
        != reps2::runtime::EeThreadStartRejection::none) return 237;
    return 0;
}

int test_start_thread_rejects_invalid_and_non_dormant() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    const std::uint32_t bad_ids[] = {
        0U,
        reps2::runtime::kEeMainThreadId,
        reps2::runtime::kEeFirstCreatedThreadId
            + static_cast<std::uint32_t>(reps2::runtime::kEeMaxThreads),
    };
    for (const std::uint32_t id : bad_ids) {
        state.gpr[4].low = id;
        if (reps2::runtime::hle_start_thread(state) != Status::ok
            || state.hle.last_start_rejection
                != reps2::runtime::EeThreadStartRejection::invalid_id
            || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) return 238;
    }
    const std::uint32_t id = create_thread_with_priority(state, ram, 3U);
    if (id == 0U) return 239;
    state.gpr[4].low = id;
    if (reps2::runtime::hle_start_thread(state) != Status::ok
        || state.hle.last_start_rejection
            != reps2::runtime::EeThreadStartRejection::none) return 240;
    if (reps2::runtime::hle_start_thread(state) != Status::ok
        || state.hle.last_start_rejection
            != reps2::runtime::EeThreadStartRejection::not_dormant
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.ready_count != 1U) return 241;
    return 0;
}

int test_start_thread_orders_ready_queue_by_priority() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    const std::uint32_t low = create_thread_with_priority(state, ram, 5U);
    const std::uint32_t high = create_thread_with_priority(state, ram, 1U);
    const std::uint32_t middle = create_thread_with_priority(state, ram, 3U);
    if (low == 0U || high == 0U || middle == 0U) return 242;
    const std::uint32_t order[] = {low, high, middle};
    for (const std::uint32_t id : order) {
        state.gpr[4].low = id;
        if (reps2::runtime::hle_start_thread(state) != Status::ok) return 243;
    }
    if (state.hle.ready_count != 3U) return 244;
    if (state.hle.ready_queue[0] != high || state.hle.ready_queue[1] != middle
        || state.hle.ready_queue[2] != low) return 245;
    return 0;
}

int test_start_thread_keeps_equal_priority_fifo() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    const std::uint32_t first = create_thread_with_priority(state, ram, 7U);
    const std::uint32_t second = create_thread_with_priority(state, ram, 7U);
    if (first == 0U || second == 0U) return 246;
    const std::uint32_t pair[] = {first, second};
    for (const std::uint32_t id : pair) {
        state.gpr[4].low = id;
        if (reps2::runtime::hle_start_thread(state) != Status::ok) return 247;
    }
    if (state.hle.ready_count != 2U || state.hle.ready_queue[0] != first
        || state.hle.ready_queue[1] != second) return 248;
    return 0;
}

int test_get_thread_id_reports_the_boot_thread() {
    EeState state{};
    state.gpr[2].low = 0xDEAD'BEEFULL;
    if (reps2::runtime::hle_get_thread_id(state) != Status::ok) return 249;
    if (state.gpr[2].low != reps2::runtime::kEeMainThreadId) return 250;
    if (state.hle.get_thread_id_calls != 1U) return 251;
    return 0;
}

int test_change_thread_priority_updates_main_and_queue() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.gpr[4].low = reps2::runtime::kEeMainThreadId;
    state.gpr[5].low = 1U;
    if (reps2::runtime::hle_change_thread_priority(state) != Status::ok
        || state.gpr[2].low != 0U
        || state.hle.main_thread_priority != 1U
        || state.hle.last_priority_rejection
            != reps2::runtime::EeThreadPriorityRejection::none) return 252;

    const std::uint32_t first = create_thread_with_priority(state, ram, 5U);
    const std::uint32_t second = create_thread_with_priority(state, ram, 9U);
    if (first == 0U || second == 0U) return 253;
    const std::uint32_t pair[] = {first, second};
    for (const std::uint32_t id : pair) {
        state.gpr[4].low = id;
        if (reps2::runtime::hle_start_thread(state) != Status::ok) return 254;
    }
    if (state.hle.ready_queue[0] != first || state.hle.ready_queue[1] != second) {
        return 255;
    }
    state.gpr[4].low = second;
    state.gpr[5].low = 1U;
    if (reps2::runtime::hle_change_thread_priority(state) != Status::ok
        || state.gpr[2].low != 0U) return 256;
    if (state.hle.ready_count != 2U || state.hle.ready_queue[0] != second
        || state.hle.ready_queue[1] != first) return 257;
    if (state.hle.threads[second - reps2::runtime::kEeFirstCreatedThreadId]
            .current_priority != 1U) return 258;
    return 0;
}

int test_change_thread_priority_rejects_invalid() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.gpr[4].low = reps2::runtime::kEeMainThreadId;
    state.gpr[5].low = 128U;
    if (reps2::runtime::hle_change_thread_priority(state) != Status::ok
        || state.hle.last_priority_rejection
            != reps2::runtime::EeThreadPriorityRejection::invalid_priority
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.main_thread_priority != 0U) return 259;
    state.gpr[4].low = reps2::runtime::kEeFirstCreatedThreadId
        + static_cast<std::uint32_t>(reps2::runtime::kEeMaxThreads);
    state.gpr[5].low = 3U;
    if (reps2::runtime::hle_change_thread_priority(state) != Status::ok
        || state.hle.last_priority_rejection
            != reps2::runtime::EeThreadPriorityRejection::invalid_id
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) return 260;
    return 0;
}

int test_refer_thread_status_reports_the_boot_thread() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.gpr[4].low = 0xE700U; // gp
    state.gpr[5].low = 0x0800U; // stack
    state.gpr[6].low = 0x0100U; // stack_size
    state.gpr[7].low = 0xE900U; // args
    state.gpr[8].low = 0xE600U; // root_function, as the observed crt0 passes it
    if (reps2::runtime::hle_setup_thread(state) != Status::ok
        || state.hle.setup_thread_root_func != 0xE600U) return 954;
    state.gpr[4].low = reps2::runtime::kEeMainThreadId;
    state.gpr[5].low = 1U;
    if (reps2::runtime::hle_change_thread_priority(state) != Status::ok) return 955;
    constexpr std::uint32_t buffer = 0x0000'0400U;
    state.gpr[4].low = reps2::runtime::kEeMainThreadId;
    state.gpr[5].low = buffer;
    if (reps2::runtime::hle_refer_thread_status(state) != Status::ok
        || state.gpr[2].low != 0U) return 956;
    if (load_word(ram, buffer + 0x00U) != reps2::runtime::kEeThreadStatusRun
        || load_word(ram, buffer + 0x04U) != 0xE600U
        || load_word(ram, buffer + 0x08U) != 0x0800U
        || load_word(ram, buffer + 0x0CU) != 0x0100U
        || load_word(ram, buffer + 0x10U) != 0xE700U
        || load_word(ram, buffer + 0x14U) != 0U
        || load_word(ram, buffer + 0x18U) != 1U
        || load_word(ram, buffer + 0x1CU) != 0U
        || load_word(ram, buffer + 0x20U) != 0U
        || load_word(ram, buffer + 0x24U) != reps2::runtime::kEeThreadWaitNone
        || load_word(ram, buffer + 0x28U) != 0U
        || load_word(ram, buffer + 0x2CU) != 0U) {
        return 957;
    }
    // TH_SELF names the calling thread, which is the same boot thread.
    constexpr std::uint32_t self_buffer = 0x0000'0500U;
    state.gpr[4].low = reps2::runtime::kEeThreadSelfId;
    state.gpr[5].low = self_buffer;
    if (reps2::runtime::hle_refer_thread_status(state) != Status::ok
        || state.gpr[2].low != 0U
        || load_word(ram, self_buffer) != reps2::runtime::kEeThreadStatusRun
        || load_word(ram, self_buffer + 0x04U) != 0xE600U) {
        return 958;
    }
    if (state.hle.refer_thread_status_calls != 2U
        || state.hle.last_refer_thread_status_id != reps2::runtime::kEeThreadSelfId
        || state.hle.last_refer_thread_status_value
            != reps2::runtime::kEeThreadStatusRun
        || state.hle.last_refer_thread_status_rejection
            != reps2::runtime::EeThreadStatusRejection::none) {
        return 959;
    }
    return 0;
}

int test_refer_thread_status_reports_a_created_thread() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    const std::uint32_t id = create_thread_with_priority(state, ram, 3U);
    if (id == 0U) return 960;
    constexpr std::uint32_t buffer = 0x0000'0400U;
    state.gpr[4].low = id;
    state.gpr[5].low = buffer;
    if (reps2::runtime::hle_refer_thread_status(state) != Status::ok
        || state.gpr[2].low != 0U
        || load_word(ram, buffer + 0x00U) != reps2::runtime::kEeThreadStatusDormant
        || load_word(ram, buffer + 0x04U) != 0xE600U
        || load_word(ram, buffer + 0x08U) != 0x800U
        || load_word(ram, buffer + 0x0CU) != 0x100U
        || load_word(ram, buffer + 0x10U) != 0xE700U
        || load_word(ram, buffer + 0x14U) != 3U
        || load_word(ram, buffer + 0x18U) != 3U
        || load_word(ram, buffer + 0x24U) != reps2::runtime::kEeThreadWaitNone) {
        return 961;
    }
    state.gpr[4].low = id;
    state.gpr[5].low = 0x0076'1208U;
    if (reps2::runtime::hle_start_thread(state) != Status::ok) return 962;
    state.gpr[4].low = id;
    state.gpr[5].low = buffer;
    if (reps2::runtime::hle_refer_thread_status(state) != Status::ok
        || state.gpr[2].low != 0U
        || load_word(ram, buffer + 0x00U) != reps2::runtime::kEeThreadStatusReady) {
        return 963;
    }
    // The dispatch path services 0x30 and resumes at the return PC. The 0xE400
    // fixture is `syscall; jr ra`, so `$v0` keeps the service result.
    state.pc = 0xE400U;
    state.gpr[3].low = reps2::runtime::kEeSyscallReferThreadStatus;
    state.gpr[4].low = id;
    state.gpr[5].low = 0x0000'0500U;
    state.gpr[31].low = kUnreachableReturnPc;
    if (reps2::generated::execute_cfg(state, 32U) != Status::external_control_flow
        || state.pc != kUnreachableReturnPc || state.gpr[2].low != 0U
        || load_word(ram, 0x0000'0500U) != reps2::runtime::kEeThreadStatusReady) {
        return 964;
    }
    return 0;
}

int test_refer_thread_status_refuses_without_writing() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    constexpr std::uint32_t buffer = 0x0000'0400U;
    for (std::uint32_t index = 0U; index < 12U; ++index) {
        store_word(ram, buffer + index * 4U, 0xA5A5'A5A5U);
    }
    state.gpr[4].low = 9U; // inside the created range, but no live record
    state.gpr[5].low = buffer;
    if (reps2::runtime::hle_refer_thread_status(state) != Status::ok
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.last_refer_thread_status_rejection
            != reps2::runtime::EeThreadStatusRejection::invalid_id) {
        return 965;
    }
    for (std::uint32_t index = 0U; index < 12U; ++index) {
        if (load_word(ram, buffer + index * 4U) != 0xA5A5'A5A5U) return 966;
    }
    state.gpr[4].low = reps2::runtime::kEeMainThreadId;
    state.gpr[5].low = 0x0000'2000U; // beyond the test RAM window
    if (reps2::runtime::hle_refer_thread_status(state) != Status::ok
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.last_refer_thread_status_rejection
            != reps2::runtime::EeThreadStatusRejection::unwritable_buffer) {
        return 967;
    }
    return 0;
}

int test_thread_services_via_dispatch() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    const std::uint32_t id = create_thread_with_priority(state, ram, 2U);
    if (id == 0U) return 261;

    state.pc = 0xE000U;
    state.gpr[3].low = reps2::runtime::kEeSyscallStartThread;
    state.gpr[4].low = id;
    state.gpr[5].low = 0x0076'1208U;
    state.gpr[31].low = kUnreachableReturnPc;
    if (reps2::generated::execute_cfg(state, 32U) != Status::external_control_flow
        || state.hle.ready_count != 1U) return 262;

    state.pc = 0xE000U;
    state.gpr[2].low = 0U;
    state.gpr[3].low = reps2::runtime::kEeSyscallGetThreadId;
    state.gpr[31].low = kUnreachableReturnPc;
    if (reps2::generated::execute_cfg(state, 32U) != Status::external_control_flow
        || state.gpr[2].low != reps2::runtime::kEeMainThreadId) return 263;

    state.pc = 0xE000U;
    state.gpr[3].low = reps2::runtime::kEeSyscallChangeThreadPriority;
    state.gpr[4].low = reps2::runtime::kEeMainThreadId;
    state.gpr[5].low = 1U;
    state.gpr[31].low = kUnreachableReturnPc;
    if (reps2::generated::execute_cfg(state, 32U) != Status::external_control_flow
        || state.hle.main_thread_priority != 1U) return 264;
    return 0;
}


int test_get_osd_config_param_writes_default() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    store_word(ram, 0x0000'0100U, 0xDEAD'BEEFU);
    state.gpr[4].low = 0x0000'0100U;
    if (reps2::runtime::hle_get_osd_config_param(state) != Status::ok) return 270;
    if (state.gpr[2].low != 0U) return 271;
    if (load_word(ram, 0x0000'0100U) != 0U) return 272;
    if (state.hle.get_osd_config_calls != 1U) return 273;
    if (state.hle.osd_config_param != 0U) return 274;
    if (state.hle.last_osd_config_rejection
        != reps2::runtime::EeOsdConfigRejection::none) return 275;
    return 0;
}

int test_osd_config_round_trip_preserves_bits() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    constexpr std::uint32_t kWritten = 0xA5A5'2001U;
    store_word(ram, 0x0000'0100U, kWritten);
    state.gpr[4].low = 0x0000'0100U;
    if (reps2::runtime::hle_set_osd_config_param(state) != Status::ok
        || state.gpr[2].low != 0U
        || state.hle.osd_config_param != kWritten) return 276;
    state.gpr[4].low = 0x0000'0200U;
    if (reps2::runtime::hle_get_osd_config_param(state) != Status::ok
        || state.gpr[2].low != 0U) return 277;
    if (load_word(ram, 0x0000'0200U) != kWritten) return 278;
    return 0;
}

// Mirrors the observed helper at 0x0025F148: read, clear bits 13..15, set bit
// 13, write, read back, restore, and report whether bits 13..14 read back zero.
int test_osd_config_read_back_probe() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    constexpr std::uint32_t kOriginal = 0x0000'0000U;
    store_word(ram, 0x0000'0100U, kOriginal);
    state.gpr[4].low = 0x0000'0100U;
    if (reps2::runtime::hle_get_osd_config_param(state) != Status::ok) return 279;
    const std::uint32_t read_back = load_word(ram, 0x0000'0100U);
    const std::uint32_t modified = (read_back & 0xFFFF'1FFFU) | 0x0000'2000U;
    store_word(ram, 0x0000'0104U, modified);
    state.gpr[4].low = 0x0000'0104U;
    if (reps2::runtime::hle_set_osd_config_param(state) != Status::ok) return 280;
    state.gpr[4].low = 0x0000'0108U;
    if (reps2::runtime::hle_get_osd_config_param(state) != Status::ok) return 281;
    const std::uint32_t observed = load_word(ram, 0x0000'0108U);
    if (observed != modified) return 282;
    // The guest computes ((observed >> 13) & 7) < 1; a coherent store makes
    // bit 13 stick, so that expression is false.
    if (((observed >> 13U) & 7U) < 1U) return 283;
    store_word(ram, 0x0000'010CU, read_back);
    state.gpr[4].low = 0x0000'010CU;
    if (reps2::runtime::hle_set_osd_config_param(state) != Status::ok
        || state.hle.osd_config_param != read_back) return 284;
    return 0;
}

int test_osd_config_refuses_bad_buffers() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.gpr[4].low = 0x1000'0000U;
    if (reps2::runtime::hle_get_osd_config_param(state) != Status::ok
        || state.hle.last_osd_config_rejection
            != reps2::runtime::EeOsdConfigRejection::unwritable_buffer
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL) return 285;

    state.hle.osd_config_param = 0x1234'5678U;
    state.gpr[4].low = 0x1000'0000U;
    if (reps2::runtime::hle_set_osd_config_param(state) != Status::ok
        || state.hle.last_osd_config_rejection
            != reps2::runtime::EeOsdConfigRejection::unreadable_buffer
        || state.gpr[2].low != 0xFFFF'FFFF'FFFF'FFFFULL
        || state.hle.osd_config_param != 0x1234'5678U) return 286;

    store_word(ram, 0x0000'0102U, 0xCAFE'F00DU);
    state.gpr[4].low = 0x0000'0102U;
    if (reps2::runtime::hle_set_osd_config_param(state) != Status::ok
        || state.hle.last_osd_config_rejection
            != reps2::runtime::EeOsdConfigRejection::unreadable_buffer
        || state.hle.osd_config_param != 0x1234'5678U) return 287;
    return 0;
}

int test_osd_config_via_dispatch() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    store_word(ram, 0x0000'0100U, 0x0000'2003U);

    state.pc = 0xE000U;
    state.gpr[3].low = reps2::runtime::kEeSyscallSetOsdConfigParam;
    state.gpr[4].low = 0x0000'0100U;
    state.gpr[31].low = kUnreachableReturnPc;
    if (reps2::generated::execute_cfg(state, 32U) != Status::external_control_flow
        || state.hle.osd_config_param != 0x0000'2003U) return 288;

    state.pc = 0xE000U;
    state.gpr[3].low = reps2::runtime::kEeSyscallGetOsdConfigParam;
    state.gpr[4].low = 0x0000'0200U;
    state.gpr[31].low = kUnreachableReturnPc;
    if (reps2::generated::execute_cfg(state, 32U) != Status::external_control_flow
        || load_word(ram, 0x0000'0200U) != 0x0000'2003U) return 289;
    return 0;
}


int test_timer3_mode_guard_register() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    std::uint32_t value = 0xFFFF'FFFFU;
    if (reps2::runtime::load_u32(state.memory, 0x1000'1810U, value) != Status::ok
        || value != 0U) return 200;
    // Documented writable mode bits (CLKS, GATE, GATS, GATM, ZRET, CUE, CMPE,
    // OVFE) are retained; the EQUF/OVFF status bits stay clear.
    if (reps2::runtime::store_u32(state.memory, reps2::runtime::kEeTimer3ModeAddress, 0x0000'0FDFU) != Status::ok
        || reps2::runtime::load_u32(state.memory, reps2::runtime::kEeTimer3ModeAddress, value) != Status::ok
        || value != 0x0000'03DFU) return 201;

    // KSEG1 and KSEG0 aliases must reach the same modeled physical register.
    if (reps2::runtime::load_u32(state.memory, 0xB000'1810U, value) != Status::ok
        || value != 0x0000'03DFU) return 210;
    if (reps2::runtime::store_u32(state.memory, 0xB000'1810U, 0x0000'0080U) != Status::ok
        || reps2::runtime::load_u32(state.memory, reps2::runtime::kEeTimer3ModeAddress, value) != Status::ok
        || value != 0x0000'0080U) return 211;
    if (reps2::runtime::store_u32(state.memory, 0x9000'1810U, 0x0000'03DFU) != Status::ok
        || reps2::runtime::load_u32(state.memory, 0xB000'1810U, value) != Status::ok
        || value != 0x0000'03DFU) return 212;

    // T3_COMP is the documented 16-bit compare storage register. Reads return
    // the stored value zero-extended; writes retain only bits 0..15. No
    // compare event is generated.
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeTimer3CompareAddress, value) != Status::ok
        || value != 0U) return 220;
    if (reps2::runtime::store_u32(state.memory, reps2::runtime::kEeTimer3CompareAddress, 0x0001'ABCDU) != Status::ok
        || reps2::runtime::load_u32(state.memory, reps2::runtime::kEeTimer3CompareAddress, value) != Status::ok
        || value != 0x0000'ABCDU) return 221;
    if (reps2::runtime::load_u32(state.memory, 0xB000'1820U, value) != Status::ok
        || value != 0x0000'ABCDU) return 222;
    if (reps2::runtime::store_u32(state.memory, 0xB000'1820U, 0xFFFF'1234U) != Status::ok
        || reps2::runtime::load_u32(state.memory, 0x1000'1820U, value) != Status::ok
        || value != 0x0000'1234U) return 223;
    if (reps2::runtime::load_u32(state.memory, 0x9000'1810U, value) != Status::ok
        || value != 0x0000'03DFU) return 224;

    // T3_COUNT is the documented 16-bit counter storage register: writes set
    // the counter, reads return the stored value zero-extended, reset 0. No
    // progression, compare match, overflow event or interrupt is generated.
    // The observed Downhill failure address uses the KSEG1 alias of T3_COUNT,
    // so the alias paths must reach the same storage.
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeTimer3CountAddress, value) != Status::ok
        || value != 0U) return 202;
    if (reps2::runtime::store_u32(state.memory, reps2::runtime::kEeTimer3CountAddress, 0x0001'ABCDU) != Status::ok
        || reps2::runtime::load_u32(state.memory, reps2::runtime::kEeTimer3CountAddress, value) != Status::ok
        || value != 0x0000'ABCDU) return 204;
    if (reps2::runtime::load_u32(state.memory, 0xB000'1800U, value) != Status::ok
        || value != 0x0000'ABCDU) return 206;
    if (reps2::runtime::store_u32(state.memory, 0xB000'1800U, 0xFFFF'1234U) != Status::ok
        || reps2::runtime::load_u32(state.memory, 0x9000'1800U, value) != Status::ok
        || value != 0x0000'1234U) return 207;
    if (reps2::runtime::load_u32(state.memory, reps2::runtime::kEeTimer3CountAddress, value) != Status::ok
        || value != 0x0000'1234U) return 208;
    return 0;
}

int test_set_syscall_rejection_leaves_the_table_untouched() {
    SyscallTableWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    constexpr std::uint32_t kHandlerSlot = 0x0001'1F80U + 0x83U * 4U;
    store_word(ram, kHandlerSlot, 0xCAFE'F00DU);
    state.gpr[4].low = 0x83U;
    state.gpr[5].low = 0x1002U; // misaligned handler
    if (reps2::runtime::hle_set_syscall(state) != Status::ok
        || state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::invalid_handler) return 190;
    if (load_word(ram, kHandlerSlot) != 0xCAFE'F00DU) return 191;
    // A valid KSEG0 handler is stored verbatim; the guest normalizes aliases.
    state.gpr[5].low = 0x8010'0000U;
    if (reps2::runtime::hle_set_syscall(state) != Status::ok
        || state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::none) return 192;
    if (load_word(ram, kHandlerSlot) != 0x8010'0000U) return 193;
    std::uint32_t alias_word = 0;
    if (reps2::runtime::load_u32(
            state.memory,
            reps2::runtime::kEeSyscallTableBase + 0x83U * 4U,
            alias_word) != Status::ok
        || alias_word != 0x8010'0000U) return 194;
    return 0;
}

int test_set_syscall_rejects_invalid_registration() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.gpr[4].low = reps2::runtime::kEeSyscallTableSlots;
    state.gpr[5].low = 0x1000U;
    if (reps2::runtime::hle_set_syscall(state) != Status::ok
        || state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::invalid_slot) return 170;
    state.gpr[4].low = 1U;
    state.gpr[5].low = 0x1002U;
    if (reps2::runtime::hle_set_syscall(state) != Status::ok
        || state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::invalid_handler) return 171;
    state.gpr[5].low = 0xFFF00000U;
    if (reps2::runtime::hle_set_syscall(state) != Status::ok
        || state.hle.last_set_syscall_rejection != reps2::runtime::EeSetSyscallRejection::invalid_handler) return 172;
    return 0;
}

int test_registered_handler_runs_through_the_dispatcher() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    // Install a guest handler for slot 0x83 (FindAddress) at 0xE600: addiu v0,
    // zero, 42; jr ra. The HLE layer diverts the SYSCALL into the translated
    // guest routine and resumes at the instruction after the trap.
    auto& registration = state.hle.syscall_registrations[0x83U];
    registration.in_use = true;
    registration.handler_address = 0xE600U;
    state.gpr[3].low = 0x83U;           // FindAddress service number
    state.gpr[4].low = 0x8000'0000U;    // $a0: kernel address
    state.gpr[5].low = 0x8008'0000U;    // $a1: kernel address
    state.gpr[31].low = kUnreachableReturnPc;
    state.pc = 0xE700U;                 // syscall; addiu v0, zero, 0; jr ra
    const Status status = reps2::generated::execute_cfg(state, 64U);
    if (status != Status::external_control_flow
        || state.pc != kUnreachableReturnPc) {
        return 180;
    }
    // The handler's result survives as the service result, and the caller's
    // context (including $ra, now back to the sentinel) was fully restored.
    if (state.gpr[2].low != 42ULL) return 181;
    if (state.hle.guest_handler_dispatches != 1U || state.hle.guest_handler_returns != 1U) return 182;
    if (state.hle.syscall_handler_depth != 0U) return 183;
    if (!registration.in_use || registration.handler_address != 0xE600U) return 184;
    if (state.hle.last_syscall_resume_pc != 0xE704U) return 185;
    // The boundary ring records the guest-handler path and the pre-dispatch
    // arguments, which is what identifies a caller in a real trace.
    if (state.hle.syscall_sequence_count != 1U) return 186;
    const reps2::runtime::EeSyscallObservation& observation =
        state.hle.syscall_observation(0);
    if (observation.pc != 0xE700U || observation.number != 0x83U
        || observation.arguments[0] != 0x8000'0000U
        || observation.arguments[1] != 0x8008'0000U
        || observation.path != reps2::runtime::EeSyscallPath::guest_handler) {
        return 187;
    }
    return 0;
}

int test_guest_handler_restore_preserves_service_result() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    const std::uint32_t handler = 0x1000U;
    const std::uint32_t resume = 0x2000U;
    state.gpr[4].low = 0x1111'1111U;
    state.gpr[5].low = 0x2222'2222U;
    state.hi = 0x3333'3333U;
    state.lo = 0x4444'4444U;
    state.fpr[7] = 0x5555'5555U;
    state.acc = 0x6666'6666U;
    state.fcsr = 0x7777'7777U;
    if (reps2::runtime::enter_ee_syscall_handler(state, 0x83U, handler, resume)
        != Status::ok) {
        return 190;
    }
    if (state.pc != handler || state.gpr[31].low != 0xFFFF'FFFF'FFFF'FFE0ULL) return 191;
    // The handler clobbers everything but leaves its result in $v0.
    state.gpr = {};
    state.gpr[0] = {};
    state.gpr[2].low = 42ULL;
    state.hi = state.lo = state.hi1 = state.lo1 = 0;
    state.fpr.fill(0);
    state.acc = 0;
    state.fcsr = 0;
    if (reps2::runtime::return_from_ee_syscall_handler(state) != Status::ok) return 192;
    if (state.pc != resume) return 193;
    if (state.gpr[4].low != 0x1111'1111U || state.gpr[5].low != 0x2222'2222U) return 194;
    if (state.gpr[2].low != 42ULL) return 195;
    if (state.hi != 0x3333'3333U || state.lo != 0x4444'4444U) return 196;
    if (state.fpr[7] != 0x5555'5555U || state.acc != 0x6666'6666U
        || state.fcsr != 0x7777'7777U) return 197;
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 198;
    if (state.hle.syscall_handler_depth != 0U) return 199;
    return 0;
}

int test_guest_handler_restore_preserves_sa_and_cop0() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    const std::uint32_t handler = 0x1000U;
    const std::uint32_t resume = 0x2000U;
    state.sa = 0xAAAAAAAAAAAAAAAAULL;
    state.cop0[reps2::runtime::kCop0Status] = 0x12345678U;
    state.cop0[reps2::runtime::kCop0Cause] = 0x9ABCDEF0U;
    state.cop0[reps2::runtime::kCop0Epc] = 0xDEADBEEFU;
    state.gpr[4].low = 0x1111'1111U;
    state.hle.syscall_handler_depth = 0U;

    if (reps2::runtime::enter_ee_syscall_handler(state, 0x83U, handler, resume)
        != Status::ok) {
        return 300;
    }
    // The handler clobbers SA and COP0; they must be restored on return.
    state.sa = 0;
    state.cop0.fill(0);
    state.gpr[2].low = 42ULL;
    if (reps2::runtime::return_from_ee_syscall_handler(state) != Status::ok) {
        return 301;
    }
    if (state.pc != resume) return 302;
    if (state.sa != 0xAAAAAAAAAAAAAAAAULL) return 303;
    if (state.cop0[reps2::runtime::kCop0Status] != 0x12345678U) return 304;
    if (state.cop0[reps2::runtime::kCop0Cause] != 0x9ABCDEF0U) return 305;
    if (state.cop0[reps2::runtime::kCop0Epc] != 0xDEADBEEFU) return 306;
    if (state.gpr[2].low != 42ULL) return 307;
    if (state.hle.syscall_handler_depth != 0U) return 308;
    return 0;
}

int test_guest_handler_overflow_refuses_instead_of_recursing() {
    EeState state{};
    state.hle.syscall_handler_depth = reps2::runtime::kEeMaxSyscallHandlerDepth;
    if (reps2::runtime::enter_ee_syscall_handler(state, 0, 0x1000U, 0x2000U)
        != Status::syscall_handler_overflow) {
        return 200;
    }
    if (state.hle.syscall_handler_depth != reps2::runtime::kEeMaxSyscallHandlerDepth) return 201;
    if (state.hle.guest_handler_dispatches != 0U || state.hle.guest_handler_returns != 0U) return 202;
    return 0;
}

int test_delay_slot_syscall_without_the_layer_raises_the_exception() {
    EeState state{};
    state.pc = 0xE200U;
    state.gpr[3].low = kSetupThread;
    state.gpr[8].low = 0U;
    state.gpr[31].low = kUnreachableReturnPc;

    const Status status = reps2::generated::execute_cfg(state, 16U);
    if (status != Status::exception_delivered) {
        return 90;
    }
    if (state.pc != reps2::runtime::kEeCommonExceptionVector) {
        return 91;
    }
    // EPC names the branch and Cause.BD marks the delay slot.
    if (state.cop0[reps2::runtime::kCop0Epc] != 0xE200U) {
        return 92;
    }
    if ((state.cop0[reps2::runtime::kCop0Cause]
         & reps2::runtime::kCop0CauseBranchDelayMask)
        == 0U) {
        return 93;
    }
    if ((state.cop0[reps2::runtime::kCop0Cause] & reps2::runtime::kCop0CauseExcCodeMask)
        != (8U << 2U)) {
        return 94;
    }
    return 0;
}

int test_find_address_returns_matching_entry_address() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    // Table at KSEG0 0x80000000 (physical 0x0000): [0x11111111, 0xDEADBEEF, 0x55555555]
    store_word(ram, 0x0000, 0x11111111U);
    store_word(ram, 0x0004, 0xDEADBEEFU);
    store_word(ram, 0x0008, 0x55555555U);
    state.gpr[4].low = 0x8000'0000U; // $a0: table start (KSEG0)
    state.gpr[5].low = 0x8000'000CU; // $a1: table end (exclusive)
    state.gpr[6].low = 0xDEADBEEFU;  // $a2: target

    if (reps2::runtime::hle_find_address(state) != Status::ok) return 230;
    if (state.hle.last_find_address_result != 0x8000'0004U) return 231;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0x8000'0004U) return 232;
    if (!state.hle.find_address_called) return 233;
    if (state.hle.last_find_address_start != 0x8000'0000U
        || state.hle.last_find_address_end != 0x8000'000CU
        || state.hle.last_find_address_target != 0xDEADBEEFU) {
        return 234;
    }
    return 0;
}

int test_find_address_matches_kseg_aliases() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    // Table at KSEG0 0x80000FF8: [0x00123456, 0x8000AAAA]
    store_word(ram, 0x0FF8, 0x00123456U);
    store_word(ram, 0x0FFC, 0x8000AAAAU);
    state.gpr[4].low = 0x8000'0FF8U;
    state.gpr[5].low = 0x8000'1000U;
    // Target is the KSEG alias of the first entry's value.
    state.gpr[6].low = 0x8012'3456U;

    if (reps2::runtime::hle_find_address(state) != Status::ok) return 240;
    if (state.hle.last_find_address_result != 0x8000'0FF8U) return 241;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0x8000'0FF8U) return 242;
    return 0;
}

int test_find_address_returns_zero_when_no_match() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    store_word(ram, 0x0000, 0x00000001U);
    store_word(ram, 0x0004, 0x00000002U);
    store_word(ram, 0x0008, 0x00000003U);
    state.gpr[4].low = 0x8000'0000U;
    state.gpr[5].low = 0x8000'000CU;
    state.gpr[6].low = 0xDEADBEEFU;

    if (reps2::runtime::hle_find_address(state) != Status::ok) return 250;
    if (state.hle.last_find_address_result != 0U) return 251;
    if (state.gpr[2].low != 0U) return 252;
    return 0;
}

int test_find_address_empty_window_returns_zero() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.gpr[4].low = 0x8000'4000U;
    state.gpr[5].low = 0x8000'4000U; // start == end
    state.gpr[6].low = 0x00000001U;

    if (reps2::runtime::hle_find_address(state) != Status::ok) return 260;
    if (state.hle.last_find_address_result != 0U) return 261;
    if (state.gpr[2].low != 0U) return 262;

    // Inverted window.
    state.gpr[4].low = 0x8000'4010U;
    state.gpr[5].low = 0x8000'4000U;
    if (reps2::runtime::hle_find_address(state) != Status::ok) return 263;
    if (state.hle.last_find_address_result != 0U) return 264;
    return 0;
}

int test_find_address_breaks_on_unreadable_memory() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    // First word readable and non-matching; the next word is past the end of RAM.
    store_word(ram, 0xFFC, 0x00000001U);
    state.gpr[4].low = 0x8000'0FFCU; // physical 0xFFC
    state.gpr[5].low = 0x8000'1004U; // physical 0x1004 past the 0x1000-byte window
    state.gpr[6].low = 0xDEADBEEFU;  // does not match the first entry

    if (reps2::runtime::hle_find_address(state) != Status::ok) return 270;
    if (state.hle.last_find_address_result != 0U) return 271;
    if (state.gpr[2].low != 0U) return 272;
    return 0;
}

int test_find_address_via_dispatch() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    store_word(ram, 0x0000, 0xCAFEBABEU);
    state.gpr[3].low = kFindAddress;   // $v1: service number
    state.gpr[4].low = 0x8000'0000U;   // $a0: start
    state.gpr[5].low = 0x8000'0004U;   // $a1: end (one word)
    state.gpr[6].low = 0xCAFEBABEU;    // $a2: target
    state.gpr[31].low = kUnreachableReturnPc;
    state.pc = 0xE400U;

    const Status status = reps2::runtime::dispatch_ee_syscall(state, 0xE404U);
    if (status != Status::ok) return 280;
    if (state.pc != 0xE404U) return 281; // resumed after the trap
    if (state.hle.last_find_address_result != 0x8000'0000U) return 282;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != 0x8000'0000U) return 283;
    if (state.hle.syscall_handled_count != 1U) return 284;
    if (state.hle.last_syscall_number != kFindAddress) return 285;
    // No COP0 exception context from a serviced call.
    if (state.cop0[reps2::runtime::kCop0Cause] != 0U
        || state.cop0[reps2::runtime::kCop0Epc] != 0U) {
        return 286;
    }
    return 0;
}

int test_service_table_reports_names() {
    if (reps2::runtime::ee_syscall_service_name(kSetupThread) == nullptr) {
        return 100;
    }
    if (reps2::runtime::find_ee_syscall_service(kFlushCache) == nullptr) {
        return 101;
    }
    if (reps2::runtime::find_ee_syscall_service(kSetupHeap) == nullptr) {
        return 102;
    }
    if (reps2::runtime::find_ee_syscall_service(kCreateSema) == nullptr) {
        return 104;
    }
    const char* const sema_name = reps2::runtime::ee_syscall_service_name(kCreateSema);
    if (sema_name == nullptr || std::string_view(sema_name) != "CreateSema") {
        return 105;
    }
    const char* const find_name = reps2::runtime::ee_syscall_service_name(kFindAddress);
    if (find_name == nullptr || std::string_view(find_name) != "FindAddress") {
        return 106;
    }
    const char* const sif_name =
        reps2::runtime::ee_syscall_service_name(reps2::runtime::kEeSyscallSifSetDChain);
    if (sif_name == nullptr || std::string_view(sif_name) != "SifSetDChain") {
        return 107;
    }
    const char* const dmac_name = reps2::runtime::ee_syscall_service_name(kAddDmacHandler);
    if (dmac_name == nullptr || std::string_view(dmac_name) != "AddDmacHandler") {
        return 108;
    }
    const char* const enable_name = reps2::runtime::ee_syscall_service_name(kEnableDmac);
    if (enable_name == nullptr || std::string_view(enable_name) != "EnableDmac") {
        return 109;
    }
    const char* const get_reg_name = reps2::runtime::ee_syscall_service_name(kSifGetReg);
    if (get_reg_name == nullptr || std::string_view(get_reg_name) != "SifGetReg") {
        return 110;
    }
    const char* const set_reg_name = reps2::runtime::ee_syscall_service_name(kSifSetReg);
    if (set_reg_name == nullptr || std::string_view(set_reg_name) != "SifSetReg") {
        return 111;
    }
    const char* const set_dma_name = reps2::runtime::ee_syscall_service_name(kSifSetDma);
    if (set_dma_name == nullptr || std::string_view(set_dma_name) != "SifSetDma") {
        return 112;
    }
    const char* const name = reps2::runtime::status_name(Status::unhandled_syscall);
    if (name == nullptr || std::string_view(name) != "unhandled_syscall") {
        return 103;
    }
    return 0;
}

} // namespace

int test_syscall_memory_trace_records_changes() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    state.hle.syscall_memory_trace_enabled = true;
    state.gpr[3].low = kCreateSema;
    state.gpr[4].low = kSemaParameters;
    state.gpr[7].low = kSemaParameters;
    store_sema_parameters(ram, kSemaParameters, 0, 4, 2, 0, 0U, 0U);
    state.pc = 0xE400U;
    const Status status = reps2::runtime::dispatch_ee_syscall(state, 16U);
    if (status != Status::ok
        || state.hle.syscall_memory_trace_size != 256U
        || state.hle.syscall_memory_trace_status != static_cast<std::uint8_t>(Status::ok)
        || state.hle.syscall_memory_changed_bytes != 0U) {
        return 200;
    }
    return 0;
}

int test_syscall_sequence_ring_records_path_and_evicts() {
    constexpr std::size_t capacity = reps2::runtime::EeHleState::kSyscallSequenceCapacity;
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.syscall_enabled = true;
    for (std::uint32_t i = 0; i < capacity + 4U; ++i) {
        state.gpr[3].low = 0x64U; // FlushCache, a serviced host no-op
        state.gpr[4].low = 0x1000U + i;
        state.pc = 0x2000U + (i * 4U);
        if (reps2::runtime::dispatch_ee_syscall(state, 16U) != Status::ok) {
            return 300;
        }
    }
    if (state.hle.syscall_sequence_count != capacity) {
        return 301;
    }
    const reps2::runtime::EeSyscallObservation& oldest = state.hle.syscall_observation(0);
    if (oldest.pc != 0x2000U + 4U * 4U) {
        return 302; // 20 calls into a 16-entry ring: the first four are gone
    }
    const reps2::runtime::EeSyscallObservation& newest =
        state.hle.syscall_observation(capacity - 1U);
    if (newest.pc != 0x2000U + 4U * 19U || newest.number != 0x64U
        || newest.arguments[0] != 0x1000U + 19U
        || newest.path != reps2::runtime::EeSyscallPath::host_service) {
        return 303;
    }

    state.gpr[3].low = 0xFFFFU; // no such service
    state.pc = 0x3000U;
    if (reps2::runtime::dispatch_ee_syscall(state, 16U) != Status::unhandled_syscall) {
        return 304;
    }
    const reps2::runtime::EeSyscallObservation& unhandled =
        state.hle.syscall_observation(state.hle.syscall_sequence_count - 1U);
    if (unhandled.pc != 0x3000U || unhandled.number != 0xFFFFU
        || unhandled.path != reps2::runtime::EeSyscallPath::unhandled) {
        return 305;
    }
    return 0;
}

// --- IOP SIFCMD model: command packet processing ---

template <typename Ram>
void store_sif_packet(
    Ram& ram,
    std::uint32_t address,
    std::uint32_t psize,
    std::uint32_t dsize,
    std::uint32_t dest,
    std::uint32_t cid,
    std::uint32_t opt
) {
    store_word(ram, address + 0x0U, (psize & 0xFFU) | ((dsize & 0x00FF'FFFFU) << 8U));
    store_word(ram, address + 0x4U, dest);
    store_word(ram, address + 0x8U, cid);
    store_word(ram, address + 0xCU, opt);
}

int test_sif_init_cmd_publishes_the_cmdinit_flag() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    const std::uint32_t packet = 0x200U;
    store_sif_packet(
        ram, packet, 24U, 0U, 0U, reps2::runtime::kEeSifCmdInitCmd, 0U);
    // The target sends a 24-byte `ca_pkt`: 16-byte header then a 4-byte `buf`
    // (payload offset 16). `opt` lives in the header at offset 12, set by
    // store_sif_packet; offset 20 is trailing padding the handler ignores.
    store_word(ram, packet + 16U, 0x0000'4000U);
    store_word(ram, packet + 20U, 0U);
    if (reps2::runtime::process_sif_command(state, packet) != Status::ok) {
        return 250;
    }
    if (!state.hle.sif_cmd.initialized
        || state.hle.sif_cmd.ee_pktbuf != 0x0000'4000U
        || state.hle.sif_cmd.last_init_opt != 0U) {
        return 251;
    }
    if (state.hle.sif_cmd.smflag != 0U || state.hle.sif_cmd.msflag != kSifStatCmdInit) {
        return 252;
    }
    // The IOP handler publishes CMDINIT to MSFLAG, while the boot-time
    // sceSifInitCmd handshake observed above reads SMFLAG.
    // The EE observes the boot flag through sceSifGetReg(SMFLAG = 4); after
    // INIT_CMD the IOP handler's MSFLAG response is separately visible.
    state.gpr[4].low = reps2::runtime::kEeSifRegisterMsFlag;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok
        || (static_cast<std::uint32_t>(state.gpr[2].low) & kSifStatCmdInit) == 0U) {
        return 253;
    }
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSmFlag;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok
        || static_cast<std::uint32_t>(state.gpr[2].low) != 0U) {
        return 254;
    }
    // The EE-side software SIF registers are SUBADDR=0x80000000,
    // MAINADDR=0x80000001, RPCINIT=0x80000002; none aliases SMFLAG.
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase + 1U;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok
        || static_cast<std::uint32_t>(state.gpr[2].low) != 0U) {
        return 255;
    }
    // The one-based hardware register map must not be confused with the
    // zero-based system register map: system SUBADDR is index 0, so index 1 is
    // MAINADDR and must not return the SIFCMD SMFLAG value.
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok
        || static_cast<std::uint32_t>(state.gpr[2].low) != 0U) {
        return 256;
    }
    return 0;
}

int test_sif_change_saddr_records_the_ee_receive_buffer() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    const std::uint32_t packet = 0x300U;
    store_sif_packet(
        ram, packet, 20U, 0U, 0U, reps2::runtime::kEeSifCmdChangeSaddr, 0U);
    store_word(ram, packet + 16U, 0x0076'1100U); // IOP's destination for EE commands
    if (reps2::runtime::process_sif_command(state, packet) != Status::ok) {
        return 260;
    }
    if (state.hle.sif_cmd.ee_pktbuf != 0x0076'1100U
        || state.hle.sif_cmd.iop_pktbuf != 0U) {
        return 261;
    }
    return 0;
}

int test_sif_set_sreg_writes_the_iop_software_register() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    const std::uint32_t packet = 0x400U;
    store_sif_packet(
        ram, packet, 24U, 0U, 0U, reps2::runtime::kEeSifCmdSetSreg, 0U);
    store_word(ram, packet + 16U, 3U);      // index
    store_word(ram, packet + 20U, 0xCAFEU); // value
    if (reps2::runtime::process_sif_command(state, packet) != Status::ok) {
        return 265;
    }
    if (state.hle.sif_cmd.last_set_sreg_index != 3U
        || state.hle.sif_cmd.last_set_sreg_value != 0xCAFEU
        || state.hle.sif_cmd.software_registers[3] != 0xCAFEU) {
        return 266;
    }
    // An out-of-range index is recorded but must not write the bounded bank.
    store_sif_packet(
        ram, packet, 24U, 0U, 0U, reps2::runtime::kEeSifCmdSetSreg, 0U);
    store_word(ram, packet + 16U, 40U);
    store_word(ram, packet + 20U, 0xBEEFU);
    if (reps2::runtime::process_sif_command(state, packet) != Status::ok) {
        return 267;
    }
    if (state.hle.sif_cmd.last_set_sreg_index != 40U
        || state.hle.sif_cmd.software_registers[3] != 0xCAFEU) {
        return 268;
    }
    return 0;
}

int test_sif_init_cmd_with_nonzero_opt_does_not_initialize() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    const std::uint32_t packet = 0x500U;
    // PS2SDK's IOP handler sets the 0x800 event flag and returns when opt != 0,
    // leaving the send buffer and CMDINIT flag untouched.
    store_sif_packet(
        ram, packet, 24U, 0U, 0U, reps2::runtime::kEeSifCmdInitCmd, 1U);
    store_word(ram, packet + 16U, 0x0000'4000U);
    if (reps2::runtime::process_sif_command(state, packet) != Status::ok) {
        return 269;
    }
    if (state.hle.sif_cmd.initialized || state.hle.sif_cmd.ee_pktbuf != 0U
        || state.hle.sif_cmd.msflag != 0U || state.hle.sif_cmd.last_init_opt != 1U) {
        return 275;
    }
    return 0;
}

int test_sif_set_dma_does_not_signal_a_semaphore() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    // A binary semaphore the title would wait on. A SIF DMA start must not
    // fabricate the DMAC completion interrupt/handler effect: REPS2 delivers no
    // interrupt, so the count stays zero.
    store_sema_parameters(ram, kSemaParameters, 0, 1, 0, 0, 0U, 0U);
    state.gpr[4].low = kSemaParameters;
    if (reps2::runtime::hle_create_sema(state) != Status::ok) {
        return 270;
    }
    store_word(ram, 0x100U, 0x2000U); // src
    store_word(ram, 0x104U, 0x3000U); // dest
    store_word(ram, 0x108U, 16U);     // size = 1 quadword
    store_word(ram, 0x10CU, 0x4U);    // SIF_DMA_INT_O (no ERT)
    state.gpr[4].low = 0x100U;
    state.gpr[5].low = 1U;
    if (reps2::runtime::hle_sif_set_dma(state) != Status::ok) {
        return 271;
    }
    if (state.hle.semaphores[0].count != 0) {
        return 272;
    }
    return 0;
}

int test_sif_set_dma_processes_the_ert_command_packet() {
    RamWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    // The EE packet lives at 0x300; the IOP receive buffer (0x400) is only a
    // destination address and is not read, because REPS2 models no IOP memory.
    const std::uint32_t packet = 0x300U;
    store_sif_packet(
        ram, packet, 24U, 0U, 0U, reps2::runtime::kEeSifCmdInitCmd, 0U);
    store_word(ram, packet + 16U, 0x0000'4000U);
    store_word(ram, 0x100U, packet); // src = EE packet
    store_word(ram, 0x104U, 0x400U); // dest = IOP receive buffer
    store_word(ram, 0x108U, 24U);
    store_word(ram, 0x10CU, 0x44U);  // SIF_DMA_ERT | SIF_DMA_INT_O
    state.gpr[4].low = 0x100U;
    state.gpr[5].low = 1U;
    if (reps2::runtime::hle_sif_set_dma(state) != Status::ok) {
        return 290;
    }
    if (!state.hle.sif_cmd.initialized
        || state.hle.sif_cmd.ee_pktbuf != 0x0000'4000U
        || state.hle.sif_cmd.msflag != kSifStatCmdInit) {
        return 291;
    }
    return 0;
}

int test_sif_boot_environment_starts_the_iop_sifcmd() {
    EeState state{};
    reps2::runtime::initialize_hle_boot_environment(state);
    // The replaced EE kernel loads the IOP replacement image, so the booted
    // machine has a resident `sifcmd`. Its documented startup publishes
    // SIF_STAT_CMDINIT in SMFLAG - the flag the target polls - before waiting
    // for the EE's INIT_CMD event.
    if (state.hle.sif_cmd.residency != reps2::runtime::EeIopSifResidency::resident) {
        return 280;
    }
    if (state.hle.sif_cmd.startup_count != 1U) {
        return 281;
    }
    if ((state.hle.sif_cmd.smflag & kSifStatCmdInit) == 0U) {
        return 282;
    }
    // The observed poll: `sceSifGetReg(SIF_REG_SMFLAG) & 0x20000`.
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSmFlag;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok) {
        return 283;
    }
    if ((static_cast<std::uint32_t>(state.gpr[2].low) & kSifStatCmdInit) == 0U) {
        return 284;
    }
    // MSFLAG is EE-written; no INIT_CMD has been sent at boot.
    if (state.hle.sif_cmd.msflag != 0U) {
        return 285;
    }
    // SADDR is IOP-owned but its value lives in IOP RAM, which REPS2 does not
    // model, so the device must not invent a buffer address.
    if (state.hle.sif_hardware_registers[reps2::runtime::kEeSifRegisterSubAddr] != 0U) {
        return 286;
    }
    return 0;
}

int test_iop_sifcmd_start_is_idempotent_and_absent_by_default() {
    EeState state{};
    reps2::runtime::iop_sifcmd_start(state);
    reps2::runtime::iop_sifcmd_start(state);
    if (state.hle.sif_cmd.startup_count != 1U) {
        return 480;
    }
    if (state.hle.sif_cmd.residency != reps2::runtime::EeIopSifResidency::resident) {
        return 481;
    }

    // Without the device the flags stay at zero: absence is never reported as a
    // satisfied handshake.
    EeState absent{};
    if (absent.hle.sif_cmd.residency != reps2::runtime::EeIopSifResidency::absent) {
        return 482;
    }
    absent.gpr[4].low = reps2::runtime::kEeSifRegisterSmFlag;
    if (reps2::runtime::hle_sif_get_reg(absent) != Status::ok) {
        return 483;
    }
    if (static_cast<std::uint32_t>(absent.gpr[2].low) != 0U) {
        return 484;
    }
    return 0;
}

// The resident IOP's RPC init answers the EE's INIT_CMD handshake with
// SIF_CMD_SET_SREG(index=SIF_SREG_RPCINIT, value=1). REPS2 models that as an
// inbound SIF0 transfer: the packet is written to the guest-published receive
// buffer, the D_STAT SIF0 event is published, and the EE-side receive dispatch
// applies the documented `cmd_data->sregs[index] = value` handler.
int test_iop_rpc_init_reply_writes_the_ee_software_register() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    const std::uint32_t cmd_data = 0x1000U;
    const std::uint32_t sregs = 0x2000U;
    store_word(ram, cmd_data + reps2::runtime::kEeSifCmdDataSregsOffset, sregs);
    // Publish MAINADDR (0x80000001) exactly as sceSifInitCmd does.
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase + 1U;
    state.gpr[5].low = cmd_data;
    if (reps2::runtime::hle_sif_set_reg(state) != Status::ok) return 600;

    const std::uint32_t packet = 0x3000U;
    store_sif_packet(ram, packet, 20U, 0U, 0U, reps2::runtime::kEeSifCmdInitCmd, 0U);
    store_word(ram, packet + 16U, 0x4000U); // EE receive buffer
    if (reps2::runtime::process_sif_command(state, packet) != Status::ok) return 601;

    std::uint32_t sreg0{};
    if (reps2::runtime::load_u32(state.memory, sregs, sreg0) != Status::ok) return 602;
    if (sreg0 != 1U) return 603;
    if (state.hle.sif_cmd.ee_sreg_base != sregs) return 604;
    if (state.hle.sif_cmd.ee_sreg_rpcinit != 1U) return 605;
    if (state.hle.sif_cmd.inbound_packet_count != 1U) return 606;
    if (state.hle.sif_cmd.last_inbound_cid != reps2::runtime::kEeSifCmdSetSreg) return 607;
    if (!state.hle.sif_cmd.rpc_init_sent || !state.hle.sif_cmd.ee_sif0_event_published) {
        return 608;
    }
    if ((state.memory.dmac.d_stat & reps2::runtime::kEeDmacDStatSif0StatusBit) == 0U) {
        return 609;
    }
    return 0;
}

int test_iop_rpc_init_reply_fails_closed_without_cmd_data() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    // MAINADDR (0x80000001) is never published, so sregs cannot be located. The
    // transfer and its D_STAT event still happen, but no register is written.
    const std::uint32_t packet = 0x3000U;
    store_sif_packet(ram, packet, 20U, 0U, 0U, reps2::runtime::kEeSifCmdInitCmd, 0U);
    store_word(ram, packet + 16U, 0x4000U);
    if (reps2::runtime::process_sif_command(state, packet) != Status::ok) return 610;
    if (state.hle.sif_cmd.ee_sreg_base != 0U) return 611;
    if (state.hle.sif_cmd.ee_sreg_rpcinit != 0U) return 612;
    if (state.hle.sif_cmd.inbound_packet_count != 1U) return 613;
    if ((state.memory.dmac.d_stat & reps2::runtime::kEeDmacDStatSif0StatusBit) == 0U) {
        return 614;
    }
    return 0;
}

int test_iop_rpc_init_reply_runs_once() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    const std::uint32_t cmd_data = 0x1000U;
    const std::uint32_t sregs = 0x2000U;
    store_word(ram, cmd_data + reps2::runtime::kEeSifCmdDataSregsOffset, sregs);
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSystemBase + 1U;
    state.gpr[5].low = cmd_data;
    if (reps2::runtime::hle_sif_set_reg(state) != Status::ok) return 615;
    const std::uint32_t packet = 0x3000U;
    store_sif_packet(ram, packet, 20U, 0U, 0U, reps2::runtime::kEeSifCmdInitCmd, 0U);
    store_word(ram, packet + 16U, 0x4000U);
    if (reps2::runtime::process_sif_command(state, packet) != Status::ok) return 616;
    if (reps2::runtime::process_sif_command(state, packet) != Status::ok) return 617;
    if (state.hle.sif_cmd.inbound_packet_count != 1U) return 618;
    return 0;
}

int test_ee_sif_dispatch_ignores_a_non_set_sreg_command() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    const std::uint32_t cmd_data = 0x1000U;
    const std::uint32_t sregs = 0x2000U;
    store_word(ram, cmd_data + reps2::runtime::kEeSifCmdDataSregsOffset, sregs);
    state.hle.sif_system_registers[1] = cmd_data; // SIF_SYSREG_MAINADDR
    const std::uint32_t packet = 0x3000U;
    store_sif_packet(ram, packet, 24U, 0U, 0U, reps2::runtime::kEeSifCmdInitCmd, 0U);
    store_word(ram, packet + 16U, 0U);
    store_word(ram, packet + 20U, 0xDEADU);
    if (reps2::runtime::process_ee_sif_command(state, packet) != Status::ok) return 619;
    std::uint32_t sreg0{};
    if (reps2::runtime::load_u32(state.memory, sregs, sreg0) != Status::ok) return 620;
    if (sreg0 != 0U) return 621;
    if (state.hle.sif_cmd.last_inbound_cid != reps2::runtime::kEeSifCmdInitCmd) return 622;
    return 0;
}

int test_ee_sif_dispatch_refuses_an_out_of_range_index() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    const std::uint32_t cmd_data = 0x1000U;
    const std::uint32_t sregs = 0x2000U;
    store_word(ram, cmd_data + reps2::runtime::kEeSifCmdDataSregsOffset, sregs);
    state.hle.sif_system_registers[1] = cmd_data;
    const std::uint32_t packet = 0x3000U;
    store_sif_packet(ram, packet, 24U, 0U, 0U, reps2::runtime::kEeSifCmdSetSreg, 0U);
    store_word(ram, packet + 16U, 40U);   // out of range (>= 32)
    store_word(ram, packet + 20U, 0xBEEFU);
    if (reps2::runtime::process_ee_sif_command(state, packet) != Status::ok) return 623;
    std::uint32_t sreg0{};
    if (reps2::runtime::load_u32(state.memory, sregs, sreg0) != Status::ok) return 624;
    if (sreg0 != 0U) return 625;
    if (state.hle.sif_cmd.ee_sreg_rpcinit != 0U) return 626;
    return 0;
}

// Layout shared by the RPC bind cases: the EE send packet, the client object,
// and the EE receive buffer published through SIF_CMD_INIT_CMD.
constexpr std::uint32_t kRpcBindPacket = 0x0800U;
constexpr std::uint32_t kRpcClient = 0x1000U;
constexpr std::uint32_t kRpcEePktbuf = 0x1800U;

void store_rpc_bind_request(SifWindow& ram, std::uint32_t sid) {
    store_sif_packet(
        ram, kRpcBindPacket, 64U, 0U, 0U, reps2::runtime::kEeSifCmdRpcBind, 0U);
    store_word(ram, kRpcBindPacket + 0x10U, 0x0001'0005U); // rec_id: slot 1, allocated
    store_word(ram, kRpcBindPacket + 0x14U, kRpcBindPacket); // pkt_addr
    store_word(ram, kRpcBindPacket + 0x18U, 0x0000'0002U);   // rpc_id
    store_word(ram, kRpcBindPacket + 0x1CU, kRpcClient);     // cd
    store_word(ram, kRpcBindPacket + 0x20U, sid);            // sid

    store_word(ram, kRpcClient + 0x00U, kRpcBindPacket);     // hdr.pkt_addr
    store_word(ram, kRpcClient + 0x04U, 0x0000'0002U);       // hdr.rpc_id
    store_word(ram, kRpcClient + 0x08U, 3U);                 // hdr.sema_id
    store_word(ram, kRpcClient + 0x24U, 0xDEAD'BEEFU);       // server: stale
}

int test_sif_rpc_bind_without_a_server_releases_the_wait() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    state.hle.semaphores[3].in_use = true;
    state.hle.semaphores[3].count = 0;
    state.hle.semaphores[3].max_count = 1;
    ++state.hle.semaphore_count;
    store_rpc_bind_request(ram, 7U);

    if (reps2::runtime::process_sif_command(state, kRpcBindPacket) != Status::ok) return 630;

    const auto& sif = state.hle.sif_cmd;
    if (sif.rpc_bind_replies != 1U || sif.last_rpc_bind_sid != 7U) return 631;
    if (sif.last_rpc_bind_sema_id != 3 || !sif.last_rpc_bind_sema_signalled) return 632;
    if (state.hle.semaphores[3].count != 1) return 633;

    // The reply carries the request's cd/pkt_addr and the no-server pointers.
    if (load_word(ram, kRpcEePktbuf) != 48U) return 634;
    if (load_word(ram, kRpcEePktbuf + 0x08U) != reps2::runtime::kEeSifCmdRpcEnd) return 635;
    if (load_word(ram, kRpcEePktbuf + 0x14U) != kRpcBindPacket) return 636;
    if (load_word(ram, kRpcEePktbuf + 0x1CU) != kRpcClient) return 637;
    if (load_word(ram, kRpcEePktbuf + 0x20U) != reps2::runtime::kEeSifCmdRpcBind) return 638;
    if (load_word(ram, kRpcEePktbuf + 0x24U) != 0U) return 639;
    if (load_word(ram, kRpcEePktbuf + 0x28U) != 0U) return 640;
    if (load_word(ram, kRpcEePktbuf + 0x2CU) != 0U) return 641;

    // The bind failed, so the client holds no server rather than the stale value.
    if (load_word(ram, kRpcClient + 0x24U) != 0U) return 642;
    if (load_word(ram, kRpcClient + 0x14U) != 0U
        || load_word(ram, kRpcClient + 0x18U) != 0U) return 643;
    // The request packet was freed and the client's packet pointer cleared.
    if (load_word(ram, kRpcClient + 0x00U) != 0U) return 644;
    if (load_word(ram, kRpcBindPacket + 0x18U) != 0U) return 645;
    if ((load_word(ram, kRpcBindPacket + 0x10U) & 0x01U) != 0U) return 646;

    // The signalled count is what the caller's wait consumes.
    state.gpr[4].low = 3U;
    if (reps2::runtime::hle_wait_sema(state) != Status::ok) return 647;
    if (state.hle.last_sema_wait_blocked || state.hle.semaphores[3].count != 0) return 648;
    return 0;
}

int test_sif_rpc_bind_without_an_ee_buffer_stays_silent() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    state.hle.semaphores[3].in_use = true;
    state.hle.semaphores[3].max_count = 1;
    ++state.hle.semaphore_count;
    store_rpc_bind_request(ram, 7U);

    // No SIF_CMD_INIT_CMD has published a receive buffer: no transfer, so no
    // reply, no semaphore raise, and no write anywhere in the window.
    if (reps2::runtime::process_sif_command(state, kRpcBindPacket) != Status::ok) return 650;
    if (state.hle.sif_cmd.rpc_bind_replies != 0U) return 651;
    if (state.hle.sif_cmd.last_rpc_bind_sema_signalled) return 652;
    if (state.hle.semaphores[3].count != 0) return 653;
    if (load_word(ram, kRpcClient + 0x24U) != 0xDEAD'BEEFU) return 654;
    if (load_word(ram, kRpcBindPacket + 0x18U) != 0x0000'0002U) return 655;
    return 0;
}

int test_sif_rpc_bind_reply_handles_idle_and_empty_clients() {
    // A client with no live semaphore id gets the reply but no signal.
    {
        SifWindow ram{};
        EeState state{};
        state.memory.main_ram = ram;
        state.hle.sif_cmd = {};
        state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
        store_rpc_bind_request(ram, 9U);
        store_word(ram, kRpcClient + 0x08U, 99U); // no such semaphore

        if (reps2::runtime::process_sif_command(state, kRpcBindPacket) != Status::ok) return 660;
        if (state.hle.sif_cmd.rpc_bind_replies != 1U) return 661;
        if (state.hle.sif_cmd.last_rpc_bind_sema_id != 99) return 662;
        if (state.hle.sif_cmd.last_rpc_bind_sema_signalled) return 663;
        if (load_word(ram, kRpcClient + 0x24U) != 0U) return 664;
    }

    // A reply naming no client object still delivers but completes nothing.
    {
        SifWindow ram{};
        EeState state{};
        state.memory.main_ram = ram;
        state.hle.sif_cmd = {};
        state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
        store_rpc_bind_request(ram, 7U);
        store_word(ram, kRpcBindPacket + 0x1CU, 0U); // cd

        if (reps2::runtime::process_sif_command(state, kRpcBindPacket) != Status::ok) return 665;
        if (state.hle.sif_cmd.rpc_bind_replies != 1U) return 666;
        if (state.hle.sif_cmd.last_rpc_bind_sema_signalled) return 667;
        if (load_word(ram, kRpcEePktbuf + 0x1CU) != 0U) return 668;
    }
    return 0;
}

// ---- SIFRPC server table and call dispatch --------------------------------

constexpr std::uint32_t kRpcCallPacket = 0x0900U;
constexpr std::uint32_t kRpcPayload = 0x0C00U;
constexpr std::uint32_t kRpcReply = 0x1C00U;
constexpr std::uint32_t kRpcTestServerSid = 0x0000'1234U;

struct TestServerLog {
    std::uint32_t calls{};
    std::uint32_t last_number{};
    std::uint32_t last_send_size{};
    std::uint32_t last_recv_size{};
    std::uint32_t last_request{};
};

TestServerLog g_server_log{};

// A synthetic server that serves function 3: it echoes request_word + 1 into
// the reply buffer and refuses every other function number.
bool serve_test_server(EeState& state, const reps2::runtime::EeSifRpcCall& call) noexcept {
    ++g_server_log.calls;
    g_server_log.last_number = call.rpc_number;
    g_server_log.last_send_size = call.send_size;
    g_server_log.last_recv_size = call.recv_size;
    if (call.rpc_number != 3U || call.send_size < 4U || call.recv_size < 4U) {
        return false;
    }
    std::uint32_t request{};
    if (reps2::runtime::load_u32(state.memory, call.send_addr, request) != Status::ok) {
        return false;
    }
    g_server_log.last_request = request;
    return reps2::runtime::store_u32(state.memory, call.recv_addr, request + 1U) == Status::ok;
}

void store_rpc_call_request(
    SifWindow& ram,
    std::uint32_t sd,
    std::uint32_t rpc_number,
    std::uint32_t send_size,
    std::uint32_t recv_size
) {
    store_sif_packet(
        ram, kRpcCallPacket, 64U, 0U, 0U, reps2::runtime::kEeSifCmdRpcCall, 0U);
    store_word(ram, kRpcCallPacket + 0x10U, 0x0001'0005U); // rec_id: slot 1, allocated
    store_word(ram, kRpcCallPacket + 0x14U, kRpcCallPacket); // pkt_addr
    store_word(ram, kRpcCallPacket + 0x18U, 0x0000'0004U);   // rpc_id
    store_word(ram, kRpcCallPacket + 0x1CU, kRpcClient);     // cd
    store_word(ram, kRpcCallPacket + 0x20U, rpc_number);     // rpc_number
    store_word(ram, kRpcCallPacket + 0x24U, send_size);      // send_size
    store_word(ram, kRpcCallPacket + 0x28U, kRpcReply);      // recvbuf
    store_word(ram, kRpcCallPacket + 0x2CU, recv_size);      // recv_size
    store_word(ram, kRpcCallPacket + 0x30U, 1U);             // rmode
    store_word(ram, kRpcCallPacket + 0x34U, sd);             // server token

    store_word(ram, kRpcClient + 0x00U, kRpcCallPacket);     // hdr.pkt_addr
    store_word(ram, kRpcClient + 0x04U, 0x0000'0004U);       // hdr.rpc_id
    store_word(ram, kRpcClient + 0x08U, 3U);                 // hdr.sema_id
    store_word(ram, kRpcClient + 0x1CU, 0U);                 // end_function
}

void make_rpc_call_semaphore(EeState& state) {
    state.hle.semaphores[3].in_use = true;
    state.hle.semaphores[3].count = 0;
    state.hle.semaphores[3].max_count = 1;
    ++state.hle.semaphore_count;
}

int test_sif_rpc_bind_with_a_registered_server_returns_its_token() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);
    if (!reps2::runtime::register_ee_sif_rpc_server(
            state, kRpcTestServerSid, 0U, &serve_test_server)) {
        return 780;
    }
    store_rpc_bind_request(ram, kRpcTestServerSid);

    if (reps2::runtime::process_sif_command(state, kRpcBindPacket) != Status::ok) return 781;
    const auto& sif = state.hle.sif_cmd;
    if (!sif.last_rpc_bind_had_server || sif.last_rpc_bind_server_sid != kRpcTestServerSid) {
        return 782;
    }
    // The reply carries the server's sid as the opaque token; buf/cbuf stay
    // zero because no IOP-side buffer address is modelled.
    if (load_word(ram, kRpcEePktbuf + 0x24U) != kRpcTestServerSid) return 783;
    if (load_word(ram, kRpcEePktbuf + 0x28U) != 0U
        || load_word(ram, kRpcEePktbuf + 0x2CU) != 0U) {
        return 784;
    }
    if (load_word(ram, kRpcClient + 0x24U) != kRpcTestServerSid) return 785;
    if (sif.last_rpc_bind_sema_id != 3 || !sif.last_rpc_bind_sema_signalled) return 786;
    if (state.hle.semaphores[3].count != 1) return 787;
    return 0;
}

int test_sif_rpc_server_registration_is_bounded_and_unique() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};

    if (reps2::runtime::register_ee_sif_rpc_server(state, 0U, 0U, &serve_test_server)) return 790;
    if (reps2::runtime::register_ee_sif_rpc_server(state, 1U, 0U, nullptr)) return 791;
    if (!reps2::runtime::register_ee_sif_rpc_server(state, 1U, 0U, &serve_test_server)) return 792;
    // A duplicate (sid, queue) is refused; the same sid on another queue is a
    // distinct server, as it is for the two CDVD disk-ready queues.
    if (reps2::runtime::register_ee_sif_rpc_server(state, 1U, 0U, &serve_test_server)) return 793;
    if (!reps2::runtime::register_ee_sif_rpc_server(state, 1U, 1U, &serve_test_server)) return 794;
    for (std::uint32_t sid = 2U; sid < 10U; ++sid) {
        (void)reps2::runtime::register_ee_sif_rpc_server(state, sid, 0U, &serve_test_server);
    }
    if (state.hle.sif_cmd.server_count != reps2::runtime::kEeMaxSifRpcServers) return 795;
    if (reps2::runtime::find_ee_sif_rpc_server(state, 0U) != nullptr) return 796;
    // The lowest queue wins, matching the active-queue walk in `search_svdata`.
    const auto* found = reps2::runtime::find_ee_sif_rpc_server(state, 1U);
    if (found == nullptr || found->queue != 0U) return 797;
    return 0;
}

int test_sif_rpc_call_dispatches_to_the_registered_server() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);
    g_server_log = {};
    if (!reps2::runtime::register_ee_sif_rpc_server(
            state, kRpcTestServerSid, 0U, &serve_test_server)) {
        return 800;
    }
    store_rpc_call_request(ram, kRpcTestServerSid, 3U, 4U, 8U);
    store_word(ram, kRpcPayload, 41U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};

    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 801;
    }
    const auto& sif = state.hle.sif_cmd;
    if (g_server_log.calls != 1U || g_server_log.last_number != 3U
        || g_server_log.last_send_size != 4U || g_server_log.last_recv_size != 8U
        || g_server_log.last_request != 41U) {
        return 802;
    }
    if (sif.rpc_call_count != 1U || sif.rpc_call_replies != 1U
        || !sif.last_rpc_call_replied || sif.last_rpc_call_server_sid != kRpcTestServerSid) {
        return 803;
    }
    // The handler's reply reached the caller's buffer and the client semaphore
    // was raised by the same iSignalSema rule a bind reply uses.
    if (load_word(ram, kRpcReply) != 42U) return 804;
    if (sif.last_rpc_bind_sema_id != 3 || !sif.last_rpc_bind_sema_signalled) return 805;
    if (state.hle.semaphores[3].count != 1) return 806;

    // The reply packet names the call as the request it ends, so the EE takes
    // the call branch of `_request_end` and frees the request packet.
    if (load_word(ram, kRpcEePktbuf) != 48U) return 807;
    if (load_word(ram, kRpcEePktbuf + 0x08U) != reps2::runtime::kEeSifCmdRpcEnd) return 808;
    if (load_word(ram, kRpcEePktbuf + 0x14U) != kRpcCallPacket) return 809;
    if (load_word(ram, kRpcEePktbuf + 0x1CU) != kRpcClient) return 810;
    if (load_word(ram, kRpcEePktbuf + 0x20U) != reps2::runtime::kEeSifCmdRpcCall) return 811;
    if (load_word(ram, kRpcEePktbuf + 0x24U) != kRpcTestServerSid) return 812;
    if (load_word(ram, kRpcClient + 0x00U) != 0U) return 813;
    if (load_word(ram, kRpcCallPacket + 0x18U) != 0U) return 814;
    if ((load_word(ram, kRpcCallPacket + 0x10U) & 0x01U) != 0U) return 815;
    return 0;
}

int test_sif_rpc_call_without_a_server_is_not_answered() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);
    g_server_log = {};
    if (!reps2::runtime::register_ee_sif_rpc_server(
            state, kRpcTestServerSid, 0U, &serve_test_server)) {
        return 820;
    }
    // A stale token from a server that is not registered must not reach the
    // test server, and nothing may be written into the receive buffer.
    store_rpc_call_request(ram, 0x0000'BEEFU, 3U, 4U, 8U);
    store_word(ram, kRpcPayload, 41U);
    store_word(ram, kRpcEePktbuf + 0x24U, 0xAAAA'AAAAU); // canary
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};

    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 821;
    }
    const auto& sif = state.hle.sif_cmd;
    if (g_server_log.calls != 0U) return 822;
    if (sif.rpc_calls_unmatched != 1U || sif.rpc_call_replies != 0U
        || sif.last_rpc_call_replied) {
        return 823;
    }
    if (load_word(ram, kRpcEePktbuf + 0x24U) != 0xAAAA'AAAAU) return 824;
    if (state.hle.semaphores[3].count != 0) return 825;
    if (load_word(ram, kRpcCallPacket + 0x18U) != 0x0000'0004U) return 826;
    return 0;
}

int test_sif_rpc_call_refused_by_the_server_is_not_answered() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    state.hle.sif_cmd = {};
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);
    g_server_log = {};
    if (!reps2::runtime::register_ee_sif_rpc_server(
            state, kRpcTestServerSid, 0U, &serve_test_server)) {
        return 830;
    }
    store_rpc_call_request(ram, kRpcTestServerSid, 9U, 4U, 8U);
    store_word(ram, kRpcPayload, 41U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};

    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 831;
    }
    const auto& sif = state.hle.sif_cmd;
    if (g_server_log.calls != 1U || g_server_log.last_number != 9U) return 832;
    if (sif.rpc_call_replies != 0U || sif.last_rpc_call_replied) return 833;
    if (state.hle.semaphores[3].count != 0) return 834;
    if (load_word(ram, kRpcCallPacket + 0x18U) != 0x0000'0004U) return 835;
    return 0;
}

int test_boot_environment_registers_the_cdvd_init_server() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    if (reps2::runtime::find_ee_sif_rpc_server(
            state, reps2::runtime::kEeSifServerCdInit) == nullptr) {
        return 840;
    }
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    // The bind now reaches a real server.
    store_rpc_bind_request(ram, reps2::runtime::kEeSifServerCdInit);
    if (reps2::runtime::process_sif_command(state, kRpcBindPacket) != Status::ok) return 841;
    if (!state.hle.sif_cmd.last_rpc_bind_had_server) return 842;
    if (load_word(ram, kRpcClient + 0x24U) != reps2::runtime::kEeSifServerCdInit) return 843;

    // CdInit is function 0 with a one-word mode and a four-word reply.
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdInit, 0U, 4U, 16U);
    store_word(ram, kRpcPayload, 2U); // SCECdINIT
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 844;
    }
    const auto& sif = state.hle.sif_cmd;
    if (sif.rpc_call_replies != 1U || !sif.last_rpc_call_replied) return 845;
    if (sif.last_rpc_call_request_word != 2U) return 846;
    if (load_word(ram, kRpcReply) != 1U) return 847;
    if (load_word(ram, kRpcReply + 4U) != 0U || load_word(ram, kRpcReply + 8U) != 0U
        || load_word(ram, kRpcReply + 12U) != 0U) {
        return 848;
    }
    if (state.hle.semaphores[3].count != 1) return 849;

    // An unimplemented function number is not answered: the caller stays
    // blocked, as it would with a real server whose callback returns NULL.
    make_rpc_call_semaphore(state);
    state.hle.semaphores[3].count = 0;
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdInit, 1U, 4U, 16U);
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 850;
    }
    if (state.hle.sif_cmd.rpc_call_replies != 1U) return 851;
    if (state.hle.semaphores[3].count != 0) return 852;
    return 0;
}

int test_boot_environment_registers_the_cdvd_scmd_server() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    if (reps2::runtime::find_ee_sif_rpc_server(
            state, reps2::runtime::kEeSifServerCdScm) == nullptr) {
        return 968;
    }
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    // The bind completes with the server's sid as the opaque token.
    store_rpc_bind_request(ram, reps2::runtime::kEeSifServerCdScm);
    if (reps2::runtime::process_sif_command(state, kRpcBindPacket) != Status::ok) {
        return 969;
    }
    if (!state.hle.sif_cmd.last_rpc_bind_had_server
        || state.hle.sif_cmd.last_rpc_bind_server_sid
            != reps2::runtime::kEeSifServerCdScm) {
        return 970;
    }
    if (load_word(ram, kRpcClient + 0x24U) != reps2::runtime::kEeSifServerCdScm) {
        return 971;
    }

    // An S-command call (function number = SCmd code, here 0x0C STATUS with no
    // payload) is dispatched and recorded, but refused: no reply and no
    // semaphore signal, so the caller stays blocked instead of receiving an
    // invented answer.
    make_rpc_call_semaphore(state);
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdScm, 0x0CU, 0U, 4U);
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket) != Status::ok) {
        return 972;
    }
    if (state.hle.sif_cmd.last_rpc_call_number != 0x0CU
        || state.hle.sif_cmd.last_rpc_call_server_sid
            != reps2::runtime::kEeSifServerCdScm
        || state.hle.sif_cmd.rpc_call_replies != 0U
        || state.hle.sif_cmd.last_rpc_call_replied) {
        return 973;
    }
    if (state.hle.semaphores[3].count != 0) return 974;

    // A call carrying a payload records its first word for the probe.
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdScm, 0x05U, 4U, 8U);
    store_word(ram, kRpcPayload, 0xBEEFU);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 975;
    }
    if (state.hle.sif_cmd.last_rpc_call_number != 0x05U
        || state.hle.sif_cmd.last_rpc_call_request_word != 0xBEEFU
        || state.hle.sif_cmd.rpc_call_replies != 0U) {
        return 976;
    }
    return 0;
}

int test_cdvd_scmd_mmode_records_the_media_mode_and_replies_one() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    // CD_SCMD_MMODE (0x22): the title's first S-command. PS2SDK sceCdMmode
    // sends one s32 media word (SCECdMmodeDvd = 2) and reads a 4-byte reply
    // whose first word is m_retres; the IOP stores the mode and answers 1.
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdScm, 0x22U, 4U, 4U);
    store_word(ram, kRpcPayload, 2U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 977;
    }
    const auto& sif = state.hle.sif_cmd;
    if (sif.scmd_call_count != 1U || sif.scmd_replies != 1U) return 978;
    if (sif.last_scmd_number != 0x22U || !sif.last_scmd_replied) return 979;
    if (sif.last_scmd_media != 2U || sif.cdvd_mmode != 2U) return 980;
    if (sif.last_scmd_retres != 1U) return 981;
    // The reply word (m_retres = 1) reached the caller's receive buffer, and the
    // RPC_END completion raised the caller's semaphore.
    if (load_word(ram, kRpcReply) != 1U) return 982;
    if (sif.rpc_call_replies != 1U || !sif.last_rpc_call_replied) return 983;
    if (!sif.last_rpc_bind_sema_signalled || state.hle.semaphores[3].count != 1U) {
        return 984;
    }
    if (load_word(ram, kRpcEePktbuf + 0x08U) != reps2::runtime::kEeSifCmdRpcEnd) return 985;
    return 0;
}

int test_cdvd_scmd_mmode_refuses_a_malformed_request() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    // An MMODE with a send size other than the documented 4-byte word is not the
    // path PS2SDK exercises: nothing is answered, so no reply and no RPC_END.
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdScm, 0x22U, 8U, 4U);
    store_word(ram, kRpcPayload, 2U);
    store_word(ram, kRpcReply, 0xA5A5'A5A5U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 8, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 986;
    }
    const auto& sif = state.hle.sif_cmd;
    if (sif.last_scmd_number != 0x22U || sif.last_scmd_replied) return 987;
    if (sif.scmd_replies != 0U || sif.rpc_call_replies != 0U) return 988;
    if (load_word(ram, kRpcReply) != 0xA5A5'A5A5U) return 989;
    if (load_word(ram, kRpcEePktbuf + 0x08U) == reps2::runtime::kEeSifCmdRpcEnd) return 990;
    return 0;
}

bool dummy_media_read(void*, std::uint32_t, std::uint32_t, std::span<std::uint8_t>) noexcept {
    return true;
}

int test_boot_environment_registers_the_diskready_servers() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    if (reps2::runtime::find_ee_sif_rpc_server(
            state, reps2::runtime::kEeSifServerCdDiskReady) == nullptr) {
        return 991;
    }
    if (reps2::runtime::find_ee_sif_rpc_server(
            state, reps2::runtime::kEeSifServerCdDiskReadyAlt) == nullptr) {
        return 992;
    }
    return 0;
}

int test_cdvd_diskready_answers_complete_with_media() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.media.read_sectors = &dummy_media_read; // host disc image installed
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    // sceCdDiskReady(mode=0): 4-byte mode word in, 4-byte m_retres out. A ready
    // disc answers SCECdComplete (2) — the same value the IOP returns.
    store_rpc_call_request(
        ram, reps2::runtime::kEeSifServerCdDiskReadyAlt, 0U, 4U, 4U);
    store_word(ram, kRpcPayload, 0U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 993;
    }
    const auto& sif = state.hle.sif_cmd;
    if (sif.diskready_call_count != 1U || sif.diskready_replies != 1U) return 994;
    if (sif.last_diskready_mode != 0U || !sif.last_diskready_replied) return 995;
    if (sif.last_diskready_retres != 2U) return 996;
    if (load_word(ram, kRpcReply) != 2U) return 997;
    if (sif.rpc_call_replies != 1U || !sif.last_rpc_call_replied) return 998;
    if (state.hle.semaphores[3].count != 1U) return 999;
    if (load_word(ram, kRpcEePktbuf + 0x08U) != reps2::runtime::kEeSifCmdRpcEnd) return 1000;
    return 0;
}

int test_cdvd_diskready_mode8_returns_the_raw_ready_status_with_media() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.media.read_sectors = &dummy_media_read;
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    // mode 8 asks for the raw drive status byte; a ready disc has the 0x40
    // pattern in bits 6-7 (cdvdman_devready's ready predicate).
    store_rpc_call_request(
        ram, reps2::runtime::kEeSifServerCdDiskReady, 0U, 4U, 4U);
    store_word(ram, kRpcPayload, 8U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 1001;
    }
    const auto& sif = state.hle.sif_cmd;
    if (sif.last_diskready_mode != 8U || sif.last_diskready_retres != 0x40U) return 1002;
    if (load_word(ram, kRpcReply) != 0x40U) return 1003;
    return 0;
}

int test_cdvd_diskready_answers_not_ready_without_media() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    // No host media source installed: the truthful answer is SCECdNotReady (6),
    // never a fabricated ready.
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    store_rpc_call_request(
        ram, reps2::runtime::kEeSifServerCdDiskReady, 0U, 4U, 4U);
    store_word(ram, kRpcPayload, 1U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 1004;
    }
    const auto& sif = state.hle.sif_cmd;
    if (sif.diskready_replies != 1U || sif.last_diskready_retres != 6U) return 1005;
    if (load_word(ram, kRpcReply) != 6U) return 1006;
    if (!sif.last_diskready_replied) return 1007;
    return 0;
}

int test_cdvd_diskready_refuses_unmodelled_function_and_malformed_request() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.media.read_sectors = &dummy_media_read;
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    // Function 1 is not the documented DiskReady entry: no reply, no RPC_END.
    store_rpc_call_request(
        ram, reps2::runtime::kEeSifServerCdDiskReady, 1U, 4U, 4U);
    store_word(ram, kRpcPayload, 0U);
    store_word(ram, kRpcReply, 0xA5A5'A5A5U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 1008;
    }
    const auto& sif = state.hle.sif_cmd;
    if (sif.diskready_replies != 0U || sif.last_diskready_replied) return 1009;
    if (load_word(ram, kRpcReply) != 0xA5A5'A5A5U) return 1010;
    if (load_word(ram, kRpcEePktbuf + 0x08U) == reps2::runtime::kEeSifCmdRpcEnd) return 1011;

    // The documented shape is exactly one 4-byte mode word; an 8-byte send is
    // refused so the caller stays blocked instead of getting a guessed answer.
    store_rpc_call_request(
        ram, reps2::runtime::kEeSifServerCdDiskReady, 0U, 8U, 4U);
    const reps2::runtime::SifDmaTransfer wide{kRpcPayload, 0U, 8, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &wide) != Status::ok) {
        return 1012;
    }
    if (sif.diskready_replies != 0U || sif.rpc_call_replies != 0U) return 1013;
    if (state.hle.semaphores[3].count != 0) return 1014;
    return 0;
}

int test_boot_environment_registers_the_ncmd_and_searchfile_servers() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    if (reps2::runtime::find_ee_sif_rpc_server(
            state, reps2::runtime::kEeSifServerCdNCmd) == nullptr) return 1015;
    if (reps2::runtime::find_ee_sif_rpc_server(
            state, reps2::runtime::kEeSifServerCdSearchFile) == nullptr) return 1016;
    return 0;
}

int test_ncmd_and_searchfile_calls_are_recorded_and_refused() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.media.read_sectors = &dummy_media_read;
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    // An NCMD call (function 14 = NCmdDiskReady in PS2SDK ncmd.c) is recorded
    // by number but refused: no reply, no RPC_END, semaphore untouched.
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdNCmd, 14U, 4U, 4U);
    store_word(ram, kRpcPayload, 0U);
    store_word(ram, kRpcReply, 0xA5A5'A5A5U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 4, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 1017;
    }
    const auto& sif = state.hle.sif_cmd;
    if (sif.ncmd_call_count != 1U || sif.last_ncmd_number != 14U) return 1018;
    if (sif.last_rpc_call_replied || sif.rpc_call_replies != 0U) return 1019;
    if (load_word(ram, kRpcReply) != 0xA5A5'A5A5U) return 1020;
    if (load_word(ram, kRpcEePktbuf + 0x08U) == reps2::runtime::kEeSifCmdRpcEnd) return 1021;
    if (state.hle.semaphores[3].count != 0) return 1022;

    // Same fail-closed contract for the SearchFile server.
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdSearchFile, 0U, 292U, 4U);
    const reps2::runtime::SifDmaTransfer fs{kRpcPayload, 0U, 292, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &fs) != Status::ok) {
        return 1023;
    }
    if (sif.searchfile_call_count != 1U || sif.last_searchfile_number != 0U) return 1024;
    if (sif.rpc_call_replies != 0U || state.hle.semaphores[3].count != 0) return 1025;
    return 0;
}

int test_ncmd_diskready_answers_from_media_presence() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.media.read_sectors = &dummy_media_read;
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    make_rpc_call_semaphore(state);

    // CD_NCMD_DISKREADY (14): empty request, 4-byte m_retres. Ready disc -> 2.
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdNCmd, 14U, 0U, 4U);
    const reps2::runtime::SifDmaTransfer payload{kRpcPayload, 0U, 0, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 1026;
    }
    const auto& sif = state.hle.sif_cmd;
    if (sif.ncmd_call_count != 1U || sif.ncmd_replies != 1U) return 1027;
    if (sif.last_ncmd_number != 14U || !sif.last_ncmd_replied) return 1028;
    if (sif.last_ncmd_retres != 2U || load_word(ram, kRpcReply) != 2U) return 1029;
    if (state.hle.semaphores[3].count != 1U) return 1030;
    if (load_word(ram, kRpcEePktbuf + 0x08U) != reps2::runtime::kEeSifCmdRpcEnd) return 1031;

    // Without host media the truthful answer is SCECdNotReady (6).
    state.media.read_sectors = nullptr;
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdNCmd, 14U, 0U, 4U);
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &payload) != Status::ok) {
        return 1032;
    }
    if (sif.last_ncmd_retres != 6U || load_word(ram, kRpcReply) != 6U) return 1033;

    // A non-empty request is not the documented shape: refused, no reply.
    store_rpc_call_request(ram, reps2::runtime::kEeSifServerCdNCmd, 14U, 4U, 4U);
    store_word(ram, kRpcReply, 0xA5A5'A5A5U);
    const reps2::runtime::SifDmaTransfer nonempty{kRpcPayload, 0U, 4, 0U};
    if (reps2::runtime::process_sif_command(state, kRpcCallPacket, &nonempty) != Status::ok) {
        return 1034;
    }
    if (sif.ncmd_replies != 2U || load_word(ram, kRpcReply) != 0xA5A5'A5A5U) return 1035;
    return 0;
}

int test_iop_reset_rearms_the_handshake_and_completes_the_boot() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.sif_cmd.ee_pktbuf = kRpcEePktbuf;
    state.hle.sif_cmd.rpc_init_sent = true; // the first boot already handshook

    constexpr std::uint32_t kResetPacket = 0x0E00U;
    store_sif_packet(
        ram, kResetPacket, 104U, 0U, 0U, reps2::runtime::kEeSifCmdResetCmd, 0U);
    store_word(ram, kResetPacket + 0x10U, 21U); // arglen
    store_word(ram, kResetPacket + 0x14U, 0U);  // mode
    if (reps2::runtime::process_sif_command(state, kResetPacket) != Status::ok) return 910;

    auto& sif = state.hle.sif_cmd;
    if (sif.iop_reset_count != 1U || !sif.reset_pending_boot || sif.rpc_init_sent) {
        return 911;
    }
    if (sif.last_reset_arg_len != 21U || sif.last_reset_mode != 0U) return 912;

    // The modelled reboot completes on the next SMFLAG observation.
    state.gpr[4].low = reps2::runtime::kEeSifRegisterSmFlag;
    if (reps2::runtime::hle_sif_get_reg(state) != Status::ok) return 913;
    constexpr std::uint32_t kBootedFlags = reps2::runtime::kEeSifStatSifInit
        | reps2::runtime::kEeSifStatCmdInit | reps2::runtime::kEeSifStatBootEnd;
    if (static_cast<std::uint32_t>(state.gpr[2].low) != kBootedFlags) return 914;
    if (sif.reset_pending_boot || sif.completed_resets != 1U) return 915;

    // The re-armed RPCINIT handshake answers the EE's next INIT_CMD again.
    constexpr std::uint32_t kCmdData = 0x1100U;
    constexpr std::uint32_t kSregs = 0x2000U;
    store_word(ram, kCmdData + reps2::runtime::kEeSifCmdDataSregsOffset, kSregs);
    state.hle.sif_system_registers[1] = kCmdData; // SIF_SYSREG_MAINADDR
    constexpr std::uint32_t kInitPacket = 0x0F00U;
    store_sif_packet(ram, kInitPacket, 20U, 0U, 0U, reps2::runtime::kEeSifCmdInitCmd, 0U);
    store_word(ram, kInitPacket + 0x10U, kRpcEePktbuf);
    const std::uint32_t before = sif.inbound_packet_count;
    if (reps2::runtime::process_sif_command(state, kInitPacket) != Status::ok) return 916;
    if (sif.inbound_packet_count != before + 1U) return 917;
    if (sif.ee_sreg_rpcinit != 1U) return 918;
    return 0;
}

int test_iop_reset_with_a_short_packet_changes_nothing() {
    SifWindow ram{};
    EeState state{};
    state.memory.main_ram = ram;
    reps2::runtime::initialize_hle_boot_environment(state);
    state.hle.sif_cmd.rpc_init_sent = true;
    constexpr std::uint32_t kResetPacket = 0x0E00U;
    store_sif_packet(
        ram, kResetPacket, 18U, 0U, 0U, reps2::runtime::kEeSifCmdResetCmd, 0U);
    if (reps2::runtime::process_sif_command(state, kResetPacket) != Status::ok) return 919;
    const auto& sif = state.hle.sif_cmd;
    if (sif.iop_reset_count != 0U || sif.reset_pending_boot || !sif.rpc_init_sent) return 920;
    return 0;
}

int main() {
    const struct {
        const char* what;
        int (*run)();
    } cases[] = {
        {"disabled layer keeps the architectural exception",
         &test_disabled_layer_keeps_the_architectural_exception},
        {"setup thread with the stack sentinel", &test_setup_thread_with_the_stack_sentinel},
        {"setup thread with an explicit stack", &test_setup_thread_with_an_explicit_stack},
        {"negated service number", &test_negated_service_number_dispatches_on_its_magnitude},
        {"unknown service", &test_unknown_service_stops_without_faking_success},
        {"flush cache", &test_flush_cache_is_an_honest_no_op},
        {"add dmac handler registers the sif0 handler",
         &test_add_dmac_handler_registers_the_sif0_handler},
        {"add dmac handler refuses invalid requests",
         &test_add_dmac_handler_refuses_invalid_requests},
        {"add dmac handler via dispatch", &test_add_dmac_handler_via_dispatch},
        {"remove dmac handler clears the record",
         &test_remove_dmac_handler_clears_the_record},
        {"remove dmac handler refuses unknown records",
         &test_remove_dmac_handler_refuses_unknown_records},
        {"remove dmac handler via dispatch", &test_remove_dmac_handler_via_dispatch},
        {"enable dmac sets the channel mask bit",
         &test_enable_dmac_sets_the_channel_mask_bit},
        {"enable dmac is idempotent for an enabled channel",
         &test_enable_dmac_is_idempotent_for_an_enabled_channel},
        {"enable dmac refuses invalid requests", &test_enable_dmac_refuses_invalid_requests},
        {"enable dmac via dispatch", &test_enable_dmac_via_dispatch},
        {"disable dmac clears the channel mask bit",
         &test_disable_dmac_clears_the_channel_mask_bit},
        {"disable dmac is idempotent for a disabled channel",
         &test_disable_dmac_is_idempotent_for_a_disabled_channel},
        {"disable dmac refuses an invalid channel",
         &test_disable_dmac_refuses_an_invalid_channel},
        {"disable dmac via dispatch", &test_disable_dmac_via_dispatch},
        {"sif stop dma clears only the start bit",
         &test_sif_stop_dma_clears_only_the_start_bit},
        {"sif stop dma fails closed without a known channel",
         &test_sif_stop_dma_fails_closed_without_a_known_channel},
        {"sif stop dma via dispatch", &test_sif_stop_dma_via_dispatch},
        {"sif get reg reports the uninitialised registers",
         &test_sif_get_reg_reports_the_uninitialised_registers},
        {"sif get reg returns a modelled value", &test_sif_get_reg_returns_a_modelled_value},
        {"sif get reg stops on an unmodelled register",
         &test_sif_get_reg_stops_on_an_unmodelled_register},
        {"sif get reg via dispatch", &test_sif_get_reg_via_dispatch},
        {"sif set reg writes the shared register file",
         &test_sif_set_reg_writes_the_shared_register_file},
        {"sif set reg stops on an unmodelled register",
         &test_sif_set_reg_stops_on_an_unmodelled_register},
        {"sif set reg via dispatch", &test_sif_set_reg_via_dispatch},
        {"sif set dma programs and starts sif0", &test_sif_set_dma_programs_and_starts_sif0},
        {"sif set dma rejects invalid count", &test_sif_set_dma_rejects_invalid_count},
        {"sif set dma via dispatch", &test_sif_set_dma_via_dispatch},
        {"sif set dchain starts the idle channel",
         &test_sif_set_d_chain_starts_the_idle_channel},
        {"sif set dchain leaves a running channel alone",
         &test_sif_set_d_chain_leaves_a_running_channel_alone},
        {"sif set dchain refuses unknown state", &test_sif_set_d_chain_refuses_unknown_state},
        {"sif set dchain via dispatch", &test_sif_set_d_chain_via_dispatch},
        {"syscall sequence ring records path and evicts",
         &test_syscall_sequence_ring_records_path_and_evicts},
        {"delay slot taken target", &test_delay_slot_syscall_resumes_at_the_taken_target},
        {"delay slot fallthrough", &test_delay_slot_syscall_resumes_at_the_fallthrough},
        {"delay slot without the layer",
         &test_delay_slot_syscall_without_the_layer_raises_the_exception},
        {"setup heap explicit range", &test_setup_heap_with_explicit_range},
        {"setup heap size sentinel", &test_setup_heap_size_sentinel_uses_setup_thread_stack},
        {"create sema unique id", &test_create_sema_returns_a_unique_id},
        {"create sema negated service number",
         &test_create_sema_consumes_only_the_documented_result},
        {"create sema inconsistent counts", &test_create_sema_rejects_inconsistent_counts},
        {"create sema unreadable parameters", &test_create_sema_rejects_unreadable_parameters},
        {"create sema table bound", &test_create_sema_table_is_bounded},
        {"signal sema raises the count", &test_signal_sema_raises_the_count},
        {"wait sema consumes a free count", &test_wait_sema_consumes_a_free_count},
        {"wait sema blocks on a zero count", &test_wait_sema_blocks_on_a_zero_count},
        {"wait sema refuses an invalid id", &test_wait_sema_refuses_an_invalid_id},
        {"wait sema refuses a negative id", &test_wait_sema_refuses_a_negative_id},
        {"delete sema frees the slot", &test_delete_sema_frees_the_slot},
        {"poll sema success returns semaphore id",
         &test_poll_sema_success_returns_the_semaphore_id},
        {"poll sema on a zero count returns failure without blocking",
         &test_poll_sema_on_a_zero_count_returns_failure_without_blocking},
        {"poll sema refuses an invalid id", &test_poll_sema_refuses_an_invalid_id},
        {"poll sema via dispatch including the negated form",
         &test_poll_sema_via_dispatch_including_the_negated_form},
        {"semaphore services via dispatch", &test_semaphore_services_via_dispatch},
        {"sif init cmd publishes the cmdinit flag",
         &test_sif_init_cmd_publishes_the_cmdinit_flag},
        {"sif change saddr records the ee buffer",
         &test_sif_change_saddr_records_the_ee_receive_buffer},
        {"sif set sreg writes the iop software register",
         &test_sif_set_sreg_writes_the_iop_software_register},
        {"sif init cmd with nonzero opt does not initialise",
         &test_sif_init_cmd_with_nonzero_opt_does_not_initialize},
        {"sif set dma processes the ert command packet",
         &test_sif_set_dma_processes_the_ert_command_packet},
        {"sif set dma does not signal a semaphore",
         &test_sif_set_dma_does_not_signal_a_semaphore},
        {"sif boot environment starts the iop sifcmd",
         &test_sif_boot_environment_starts_the_iop_sifcmd},
        {"iop sifcmd start is idempotent and absent by default",
         &test_iop_sifcmd_start_is_idempotent_and_absent_by_default},
        {"iop rpc init reply writes the ee software register",
         &test_iop_rpc_init_reply_writes_the_ee_software_register},
        {"iop rpc init reply fails closed without cmd data",
         &test_iop_rpc_init_reply_fails_closed_without_cmd_data},
        {"iop rpc init reply runs once", &test_iop_rpc_init_reply_runs_once},
        {"ee sif dispatch ignores a non-set-sreg command",
         &test_ee_sif_dispatch_ignores_a_non_set_sreg_command},
        {"ee sif dispatch refuses an out-of-range index",
         &test_ee_sif_dispatch_refuses_an_out_of_range_index},
        {"sif rpc bind without a server releases the wait",
         &test_sif_rpc_bind_without_a_server_releases_the_wait},
        {"sif rpc bind without an ee buffer stays silent",
         &test_sif_rpc_bind_without_an_ee_buffer_stays_silent},
        {"sif rpc bind reply handles idle and empty clients",
         &test_sif_rpc_bind_reply_handles_idle_and_empty_clients},
        {"sif rpc bind with a registered server returns its token",
         &test_sif_rpc_bind_with_a_registered_server_returns_its_token},
        {"sif rpc server registration is bounded and unique",
         &test_sif_rpc_server_registration_is_bounded_and_unique},
        {"sif rpc call dispatches to the registered server",
         &test_sif_rpc_call_dispatches_to_the_registered_server},
        {"sif rpc call without a server is not answered",
         &test_sif_rpc_call_without_a_server_is_not_answered},
        {"sif rpc call refused by the server is not answered",
         &test_sif_rpc_call_refused_by_the_server_is_not_answered},
        {"boot environment registers the cdvd scmd server",
         &test_boot_environment_registers_the_cdvd_scmd_server},
        {"cdvd scmd mmode records the media mode and replies one",
         &test_cdvd_scmd_mmode_records_the_media_mode_and_replies_one},
        {"cdvd scmd mmode refuses a malformed request",
         &test_cdvd_scmd_mmode_refuses_a_malformed_request},
        {"boot environment registers the diskready servers",
         &test_boot_environment_registers_the_diskready_servers},
        {"cdvd diskready answers complete with media",
         &test_cdvd_diskready_answers_complete_with_media},
        {"cdvd diskready mode 8 returns raw ready status with media",
         &test_cdvd_diskready_mode8_returns_the_raw_ready_status_with_media},
        {"cdvd diskready answers not ready without media",
         &test_cdvd_diskready_answers_not_ready_without_media},
        {"cdvd diskready refuses unmodelled function and malformed request",
         &test_cdvd_diskready_refuses_unmodelled_function_and_malformed_request},
        {"boot environment registers the ncmd and searchfile servers",
         &test_boot_environment_registers_the_ncmd_and_searchfile_servers},
        {"ncmd and searchfile calls are recorded and refused",
         &test_ncmd_and_searchfile_calls_are_recorded_and_refused},
        {"ncmd diskready answers from media presence",
         &test_ncmd_diskready_answers_from_media_presence},
        {"boot environment registers the cdvd init server",
         &test_boot_environment_registers_the_cdvd_init_server},
        {"iop reset re-arms the handshake and completes the boot",
         &test_iop_reset_rearms_the_handshake_and_completes_the_boot},
        {"iop reset with a short packet changes nothing",
         &test_iop_reset_with_a_short_packet_changes_nothing},
        {"set syscall registers handler", &test_set_syscall_registers_guest_handler},
        {"set syscall rejects invalid registration", &test_set_syscall_rejects_invalid_registration},
        {"set syscall rejection leaves the table untouched",
         &test_set_syscall_rejection_leaves_the_table_untouched},
        {"set syscall accepts extended alarm slot", &test_set_syscall_accepts_extended_alarm_slot},
        {"get entry address returns slot address", &test_get_entry_address_returns_slot_address},
        {"create thread returns dormant id", &test_create_thread_returns_dormant_id},
        {"create thread rejects invalid parameters",
         &test_create_thread_rejects_invalid_parameters},
        {"create thread table is bounded", &test_create_thread_table_is_bounded},
        {"create thread via dispatch", &test_create_thread_via_dispatch},
        {"start thread transitions to ready", &test_start_thread_transitions_to_ready},
        {"start thread rejects invalid and non-dormant",
         &test_start_thread_rejects_invalid_and_non_dormant},
        {"start thread orders ready queue by priority",
         &test_start_thread_orders_ready_queue_by_priority},
        {"start thread keeps equal priority fifo",
         &test_start_thread_keeps_equal_priority_fifo},
        {"get thread id reports the boot thread",
         &test_get_thread_id_reports_the_boot_thread},
        {"change thread priority updates main and queue",
         &test_change_thread_priority_updates_main_and_queue},
        {"change thread priority rejects invalid",
         &test_change_thread_priority_rejects_invalid},
        {"refer thread status reports the boot thread",
         &test_refer_thread_status_reports_the_boot_thread},
        {"refer thread status reports a created thread",
         &test_refer_thread_status_reports_a_created_thread},
        {"refer thread status refuses without writing",
         &test_refer_thread_status_refuses_without_writing},
        {"thread services via dispatch", &test_thread_services_via_dispatch},
        {"get osd config writes default",
         &test_get_osd_config_param_writes_default},
        {"osd config round trip preserves bits",
         &test_osd_config_round_trip_preserves_bits},
        {"osd config read-back probe", &test_osd_config_read_back_probe},
        {"osd config refuses bad buffers", &test_osd_config_refuses_bad_buffers},
        {"osd config via dispatch", &test_osd_config_via_dispatch},
        {"timer 3 mode and compare registers", &test_timer3_mode_guard_register},
        {"service table names", &test_service_table_reports_names},
        {"find address matching entry", &test_find_address_returns_matching_entry_address},
        {"find address kseg alias match", &test_find_address_matches_kseg_aliases},
        {"find address no match", &test_find_address_returns_zero_when_no_match},
        {"find address empty window", &test_find_address_empty_window_returns_zero},
        {"find address unreadable memory", &test_find_address_breaks_on_unreadable_memory},
        {"find address via dispatch", &test_find_address_via_dispatch},
        {"syscall memory trace", &test_syscall_memory_trace_records_changes},
        {"registered handler runs through the dispatcher", &test_registered_handler_runs_through_the_dispatcher},
        {"guest handler restore preserves service result", &test_guest_handler_restore_preserves_service_result},
        {"guest handler restore preserves sa and cop0",
         &test_guest_handler_restore_preserves_sa_and_cop0},
        {"guest handler overflow refuses recursion", &test_guest_handler_overflow_refuses_instead_of_recursing},
    };

    for (const auto& test : cases) {
        const int result = test.run();
        if (result != 0) {
            std::fprintf(stderr, "hle syscall smoke failed: %s (assertion %d)\n", test.what, result);
            return result;
        }
    }
    std::printf("EE syscall HLE smoke passed: %zu cases\n", std::size(cases));
    return 0;
}
