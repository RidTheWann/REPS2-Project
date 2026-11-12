// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "reps2/runtime/ee_state.hpp"

// EE kernel syscall HLE (high-level emulation) layer.
//
// The EE kernel interface is an ordinary `syscall` trap whose service number is
// carried in a register rather than in the instruction's 20-bit code field:
//
//   * $v1 (GPR[3]) holds the service number. The ps2sdk wrappers load it with
//     `li $3, __NR_x` immediately before a bare `syscall`, and the "i" variants
//     pass the negated number, so dispatch uses its magnitude.
//   * $a0..$a3 (GPR[4..7]) carry the first four arguments; further arguments
//     follow the EE calling convention on the guest stack.
//   * $v0 (GPR[2]) carries the result. The SDK treats a negative result as
//     failure; a specific negative-code taxonomy is not reproducible here.
//
// Evidence, citations and known gaps are recorded in
// docs/decisions/0062-ee-syscall-hle-dispatch.md and docs/RESEARCH.md. A
// service is listed here only when its host-side behavior is described there.
// Anything else stops as Status::unhandled_syscall: this layer never invents a
// successful return for a service it does not implement.

namespace reps2::runtime {

inline constexpr std::uint8_t kEeSyscallNumberRegister = 3;
inline constexpr std::uint8_t kEeSyscallResultRegister = 2;
inline constexpr std::uint8_t kEeSyscallArgumentBase = 4;
inline constexpr std::uint8_t kEeSyscallArgumentCount = 4;

inline constexpr std::uint32_t kEeSyscallAddDmacHandler = 0x12U;
inline constexpr std::uint32_t kEeSyscallRemoveDmacHandler = 0x13U;
inline constexpr std::uint32_t kEeSyscallEnableDmac = 0x16U;
inline constexpr std::uint32_t kEeSyscallDisableDmac = 0x17U;
inline constexpr std::uint32_t kEeSyscallCreateThread = 0x20U;
inline constexpr std::uint32_t kEeSyscallStartThread = 0x22U;
inline constexpr std::uint32_t kEeSyscallChangeThreadPriority = 0x29U;
inline constexpr std::uint32_t kEeSyscallGetThreadId = 0x2FU;
inline constexpr std::uint32_t kEeSyscallReferThreadStatus = 0x30U;
inline constexpr std::uint32_t kEeSyscallSetOsdConfigParam = 0x4AU;
inline constexpr std::uint32_t kEeSyscallGetOsdConfigParam = 0x4BU;
inline constexpr std::uint32_t kEeSyscallSetupThread = 0x3CU;
inline constexpr std::uint32_t kEeSyscallSetupHeap = 0x3DU;
inline constexpr std::uint32_t kEeSyscallCreateSema = 0x40U;
inline constexpr std::uint32_t kEeSyscallDeleteSema = 0x41U;
inline constexpr std::uint32_t kEeSyscallSignalSema = 0x42U;
inline constexpr std::uint32_t kEeSyscallISignalSema = 0x43U;
inline constexpr std::uint32_t kEeSyscallWaitSema = 0x44U;
inline constexpr std::uint32_t kEeSyscallPollSema = 0x45U;
inline constexpr std::uint32_t kEeSyscallIPollSema = 0x46U;
inline constexpr std::uint32_t kEeSyscallGetEntryAddress = 0x5BU;
inline constexpr std::uint32_t kEeSyscallSetSyscall = 0x74U;
inline constexpr std::uint32_t kEeSyscallSifSetDma = 0x77U;
inline constexpr std::uint32_t kEeSyscallSifSetDChain = 0x78U;
inline constexpr std::uint32_t kEeSyscallSifStopDma = 0x6BU;
inline constexpr std::uint32_t kEeSyscallSifSetReg = 0x79U;
inline constexpr std::uint32_t kEeSyscallSifGetReg = 0x7AU;
inline constexpr std::uint32_t kEeSyscallFlushCache = 0x64U;
inline constexpr std::uint32_t kEeSyscallFindAddress = 0x83U;

// The EE kernel keeps its syscall handler table in low main RAM as a flat array
// of 32-bit pointers indexed by service number. ps2sdk's `libosd_full.c` names
// 0x80011F80 as the start of the syscall table and computes an entry as
// `0x80011F80 + number * 4`; a ps2dev probe recovers the same base by scanning
// low EE RAM for two registered handler values. Real BIOS builds may place the
// table elsewhere, but in HLE REPS2 is the kernel: it fixes the documented base
// and the guest recovers it dynamically, as the observed Downhill Domination
// init does (`handler_slot - number * 4`).
inline constexpr std::uint32_t kEeSyscallTableBase = 0x8001'1F80U;
inline constexpr std::uint32_t kEeSyscallTableSize =
    static_cast<std::uint32_t>(kEeSyscallTableSlots) * 4U;

// GetEntryAddress (0x5B): return the guest address of a syscall-table slot.
//
// ps2sdk `kernel.h` declares `void *GetEntryAddress(int syscall)` next to
// SetSyscall under "Helper functions for kernel patching". Its `libosd.c`
// patch table pins the arithmetic: the entry for the special slot 0xFFFFC402
// documents `0x80011F80 + (-15358 * 4) = 0x80002F88`, i.e. the result is the
// slot address itself, not the handler stored there. The computation is
// 32-bit modular; the guest is allowed to address outside the 512 slots REPS2
// models, so no range check is imposed here.
[[nodiscard]] inline Status hle_get_entry_address(EeState& state) noexcept {
    const std::uint32_t slot = static_cast<std::uint32_t>(
        state.gpr[kEeSyscallArgumentBase].low);
    const std::uint32_t address = kEeSyscallTableBase + slot * 4U;
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(address);
    return Status::ok;
}

// The public description of CreateSema states that the call returns the new
// semaphore's id on success and -1 on failure, so -1 is a guest-visible failure
// code the guest already expects rather than an invented one.
inline constexpr std::uint32_t kEeSyscallFailure = 0xFFFF'FFFFU;

// Sentinel PC meaning "the invoked guest handler executed jr $ra". It lies in
// kseg3, outside every EE mapping, and is word-aligned so it can never be
// confused with a fetchable guest address. The generated dispatcher recognises
// it before the block lookup and never fetches from it.
inline constexpr std::uint32_t kEeSyscallHandlerReturnPc = 0xFFFF'FFE0U;

// SetupThread's documented "no caller stack" sentinel for $a1.
inline constexpr std::uint32_t kEeSyscallStackSentinel = 0xFFFF'FFFFU;
// The kernel keeps the top 4 KiB of main RAM out of the main-thread stack when
// the caller passes the sentinel.
inline constexpr std::uint32_t kEeSetupThreadTopGuard = 0x1000U;
// Bytes between the returned stack pointer and the stack top. Emulator-observed
// rather than documented, so it is named as an observation, not a law.
inline constexpr std::uint32_t kEeSetupThreadStackReserve = 0x2A0U;

using EeSyscallHandler = Status (*)(EeState&) noexcept;

struct EeSyscallService {
    std::uint32_t number;
    const char* name;
    EeSyscallHandler handler;
};

[[nodiscard]] inline std::uint32_t ee_syscall_argument(const EeState& state, std::uint8_t index) noexcept {
    return static_cast<std::uint32_t>(
        state.gpr[static_cast<std::size_t>(kEeSyscallArgumentBase) + index].low);
}

/// Unified view over indexed (0..255) and sparse extended registrations.
struct EeSyscallRegistrationView {
    bool in_use{};
    std::uint32_t handler_address{};
};

[[nodiscard]] inline EeSyscallRegistrationView find_ee_syscall_registration(
    const EeHleState& hle,
    std::uint32_t number
) noexcept {
    if (number < kEeMaxSyscallSlots) {
        const EeSyscallRegistration& registration =
            hle.syscall_registrations[number];
        return {registration.in_use, registration.handler_address};
    }
    for (const EeExtendedSyscallRegistration& registration :
         hle.extended_syscall_registrations) {
        if (registration.in_use && registration.number == number) {
            return {true, registration.handler_address};
        }
    }
    return {};
}

// AddDmacHandler (0x12): register a DMAC channel completion handler.
//
// PS2SDK declares `s32 AddDmacHandler(s32 channel, s32 (*handler)(s32 channel),
// s32 next)` and treats a negative result as failure. The observed title calls
// it as `AddDmacHandler(5, 0x0025C5F8, 0)` immediately after starting the SIF0
// channel and stores the returned id; that handler processes the SIF command
// packet.
//
// The kernel keeps a per-channel handler chain. This layer keeps one record per
// channel: it refuses a non-zero `next` instead of silently dropping the chain,
// and it refuses a second registration on a channel it already holds. Ids are
// HLE-local handles equal to the channel index, because the kernel's own id
// values are not observable from the target and reproducing them would be a
// guess (the same reasoning as the thread and semaphore ids in ADRs 0075/0066).
//
// No DMAC interrupt is delivered, so a registered handler never runs and no
// interrupt mask is touched; this creates guest-visible records only. See
// ADR 0095.
[[nodiscard]] inline Status hle_add_dmac_handler(EeState& state) noexcept {
    const std::uint32_t channel = ee_syscall_argument(state, 0);
    const std::uint32_t handler = ee_syscall_argument(state, 1);
    const std::uint32_t next = ee_syscall_argument(state, 2);
    ++state.hle.add_dmac_handler_calls;
    state.hle.last_dmac_channel = channel;
    state.hle.last_dmac_handler = handler;
    state.hle.last_dmac_handler_next = next;
    state.hle.last_dmac_handler_id = -1;
    state.hle.last_dmac_handler_rejection = EeDmacHandlerRejection::none;
    const auto refuse = [&state](EeDmacHandlerRejection reason) {
        state.hle.last_dmac_handler_rejection = reason;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    };
    if (channel >= kEeMaxDmacChannels) {
        return refuse(EeDmacHandlerRejection::invalid_channel);
    }
    std::uint32_t physical_handler{};
    if (handler == 0U || (handler & 3U) != 0U
        || !resolve_ee_main_ram_address(handler, physical_handler)) {
        return refuse(EeDmacHandlerRejection::invalid_handler);
    }
    if (next != 0U) {
        return refuse(EeDmacHandlerRejection::chained_handler_unsupported);
    }
    EeDmacHandler& record = state.hle.dmac_handlers[channel];
    if (record.in_use) {
        return refuse(EeDmacHandlerRejection::channel_already_registered);
    }
    record.in_use = true;
    record.handler = handler;
    record.next = next;
    record.register_pc = state.pc;
    ++state.hle.dmac_handler_count;
    const auto id = static_cast<std::uint32_t>(channel);
    state.hle.last_dmac_handler_id = static_cast<std::int32_t>(id);
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(id);
    return Status::ok;
}

// RemoveDmacHandler (0x13): remove the handler registered on one channel.
//
// PS2SDK's `sceSifExitCmd` teardown is
// `if (sif0_id > 0) { DisableDmac(5); RemoveDmacHandler(5, sif0_id); sif0_id = 0; }`
// (`ee/kernel/src/sifcmd.c:287-291`), and the observed post-CdInit path runs
// exactly that sequence at `0x25C3B0`..`0x25C3D8`; the probe stops on this
// service with `$a0 = 5` and `$a1 = 5`, the id the HLE `AddDmacHandler`
// returned.
//
// The layer keeps one record per channel with an HLE-local id equal to the
// channel index (ADR 0095). Removal succeeds only when the channel is valid and
// its record is in use with that id; the record is cleared and the handler
// count drops. The kernel's own return value is not established by target or
// public evidence, so `$v0` is left untouched on success and a bad request uses
// the layer's documented negative failure value. Removing a handler cannot
// deliver an interrupt; it only clears guest-visible HLE state. See ADR 0116.
[[nodiscard]] inline Status hle_remove_dmac_handler(EeState& state) noexcept {
    const std::uint32_t channel = ee_syscall_argument(state, 0);
    const std::uint32_t id = ee_syscall_argument(state, 1);
    auto& hle = state.hle;
    ++hle.remove_dmac_handler_calls;
    hle.last_remove_dmac_channel = channel;
    hle.last_remove_dmac_id = id;
    hle.last_remove_dmac_handler = 0U;
    if (channel >= kEeMaxDmacChannels) {
        hle.last_remove_dmac_outcome = EeDmacRemoveOutcome::invalid_channel;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    EeDmacHandler& record = hle.dmac_handlers[channel];
    if (!record.in_use || id != channel) {
        // The id names no record this layer issued; nothing is cleared.
        hle.last_remove_dmac_outcome = EeDmacRemoveOutcome::not_registered;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    hle.last_remove_dmac_handler = record.handler;
    record = EeDmacHandler{};
    if (hle.dmac_handler_count > 0U) {
        --hle.dmac_handler_count;
    }
    hle.last_remove_dmac_outcome = EeDmacRemoveOutcome::removed;
    return Status::ok;
}

// _EnableDmac (0x16): enable the DMAC interrupt for one channel.
//
// The observed wrapper at `0x0025B068` calls this with `$a0 = 5` immediately
// after registering the SIF0 completion handler with `AddDmacHandler(5, ..)`,
// and only after that call does the SIF command path continue.
//
// The guest-visible effect is the `D_STAT` per-channel interrupt mask. PS2Tek's
// EE register map documents `D_STAT` bits 16..25 as the per-channel interrupt
// mask with "1 = enabled, write 1 to reverse" (write-one-to-toggle), and the
// assertion rule "INT1 is asserted when (status & mask) != 0". Enabling a
// channel therefore sets its mask bit only when that bit is clear: writing it
// again would toggle it back off, so an already-enabled channel is left alone.
// This mirrors the read-modify-write the kernel must perform on a toggling mask
// register.
//
// The kernel's own return value for `_EnableDmac` is not established by target
// or public evidence, so the service leaves `$v0` untouched rather than
// inventing a result (an invalid channel still uses the layer's documented
// negative failure value). REPS2 delivers no DMAC interrupt, so the mask bit
// has no further effect in this runtime: the register is left agreeing with the
// real kernel and interrupt delivery stays out of scope. See ADR 0096.
[[nodiscard]] inline Status hle_enable_dmac(EeState& state) noexcept {
    const std::uint32_t channel = ee_syscall_argument(state, 0);
    ++state.hle.enable_dmac_calls;
    state.hle.last_enable_dmac_channel = channel;
    if (channel >= kEeMaxDmacChannels) {
        state.hle.last_enable_dmac_outcome = EeDmacEnableOutcome::invalid_channel;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    auto& dmac = state.memory.dmac;
    std::uint32_t d_stat = 0U;
    if (!dmac.read_d_stat(d_stat)) {
        state.hle.last_enable_dmac_outcome = EeDmacEnableOutcome::unknown_register_state;
        return Status::unsupported_memory_access;
    }
    const std::uint32_t mask_bit = 1U << (16U + channel);
    if ((d_stat & mask_bit) != 0U) {
        state.hle.last_enable_dmac_outcome = EeDmacEnableOutcome::already_enabled;
        return Status::ok;
    }
    if (!dmac.write_d_stat(mask_bit)) {
        state.hle.last_enable_dmac_outcome = EeDmacEnableOutcome::unknown_register_state;
        return Status::unsupported_memory_access;
    }
    state.hle.last_enable_dmac_outcome = EeDmacEnableOutcome::enabled;
    return Status::ok;
}

// _DisableDmac (0x17): disable the DMAC interrupt for one channel.
//
// The observed post-CdInit path calls this with `$a0 = 5` (SIF0) at
// `0x0025A454`, immediately after libcdvd creates its command semaphores.
// PS2SDK maps `__NR__DisableDmac` to `0x17`
// (`ee/kernel/include/syscallnr.h`). The guest-visible effect is the same
// `D_STAT` per-channel interrupt mask `_EnableDmac` touches: a write-one-to-
// toggle bit. Disabling a channel therefore clears its mask bit only when the
// bit is set; an already-disabled channel is left alone. The kernel's own
// return value for `_DisableDmac` is not established by target or public
// evidence, so the service leaves `$v0` untouched (an invalid channel still
// uses the layer's documented negative failure value). Disabling a channel
// does not cancel a transfer, and REPS2 delivers no DMAC interrupt, so the
// mask bit has no further effect here. See ADR 0115.
[[nodiscard]] inline Status hle_disable_dmac(EeState& state) noexcept {
    const std::uint32_t channel = ee_syscall_argument(state, 0);
    ++state.hle.disable_dmac_calls;
    state.hle.last_disable_dmac_channel = channel;
    if (channel >= kEeMaxDmacChannels) {
        state.hle.last_disable_dmac_outcome = EeDmacDisableOutcome::invalid_channel;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    auto& dmac = state.memory.dmac;
    std::uint32_t d_stat = 0U;
    if (!dmac.read_d_stat(d_stat)) {
        state.hle.last_disable_dmac_outcome = EeDmacDisableOutcome::unknown_register_state;
        return Status::unsupported_memory_access;
    }
    const std::uint32_t mask_bit = 1U << (16U + channel);
    if ((d_stat & mask_bit) == 0U) {
        state.hle.last_disable_dmac_outcome = EeDmacDisableOutcome::already_disabled;
        return Status::ok;
    }
    if (!dmac.write_d_stat(mask_bit)) {
        state.hle.last_disable_dmac_outcome = EeDmacDisableOutcome::unknown_register_state;
        return Status::unsupported_memory_access;
    }
    state.hle.last_disable_dmac_outcome = EeDmacDisableOutcome::disabled;
    return Status::ok;
}

// CreateThread (0x20): CreateThread(ee_thread_t *thread).
//
// ps2sdk declares `s32 CreateThread(ee_thread_t *thread)`; the struct is nine
// 32-bit words (status, func, stack, stack_size, gp_reg, initial_priority,
// current_priority, attr, option). The kernel owns `status` and the current
// priority: the observed retail call leaves `status` uninitialized, so it is
// read for bounds only and never interpreted. Returning a negative value means
// failure; -1 is used for every refusal and allocates nothing.
//
// This creates a dormant record only. No thread status, stack contents, argv
// block, ready queue or scheduler state is created, and no guest memory is
// written; StartThread (0x22) is the next required service and is not modelled.
// Thread 0 is the documented idle thread and id 1 is reserved for the running
// boot thread (kEeMainThreadId), so created ids start at 2. The host record
// table is bounded (kEeMaxThreads); a full table is refused rather than
// fabricating an id. See ADR 0075.
[[nodiscard]] inline Status hle_create_thread(EeState& state) noexcept {
    constexpr std::size_t kParameterWords = 9U;
    constexpr std::size_t kParameterBytes = kParameterWords * 4U;
    constexpr std::uint32_t kMaxPriority = 128U; // ps2sdk MAX_PRIORITY

    const std::uint32_t parameters = ee_syscall_argument(state, 0);
    ++state.hle.create_thread_calls;
    state.hle.last_thread_parameter_address = parameters;
    state.hle.last_thread_id = -1;
    state.hle.last_thread_rejection = EeThreadRejection::none;

    const auto refuse = [&state](EeThreadRejection reason) {
        state.hle.last_thread_rejection = reason;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    };

    std::uint32_t physical{};
    if (resolve_ee_memory_access(state.memory, parameters, kParameterBytes, 4U, physical)
        != Status::ok) {
        return refuse(EeThreadRejection::unreadable_parameters);
    }
    std::array<std::uint32_t, kParameterWords> words{};
    for (std::size_t index = 0; index < kParameterWords; ++index) {
        const std::size_t offset = static_cast<std::size_t>(physical) + index * 4U;
        words[index] = static_cast<std::uint32_t>(state.memory.main_ram[offset])
            | (static_cast<std::uint32_t>(state.memory.main_ram[offset + 1U]) << 8U)
            | (static_cast<std::uint32_t>(state.memory.main_ram[offset + 2U]) << 16U)
            | (static_cast<std::uint32_t>(state.memory.main_ram[offset + 3U]) << 24U);
    }

    const std::uint32_t function = words[1];
    const std::uint32_t stack = words[2];
    const std::uint32_t stack_size = words[3];
    const std::uint32_t gp = words[4];
    const auto initial_priority = static_cast<std::int32_t>(words[5]);
    state.hle.last_thread_function = function;
    state.hle.last_thread_stack = stack;
    state.hle.last_thread_stack_size = stack_size;
    state.hle.last_thread_gp = gp;
    state.hle.last_thread_initial_priority = initial_priority;
    state.hle.last_thread_attr = words[7];
    state.hle.last_thread_option = words[8];

    std::uint32_t physical_function{};
    if (function == 0U || (function & 3U) != 0U
        || !resolve_ee_main_ram_address(function, physical_function)) {
        return refuse(EeThreadRejection::invalid_func);
    }
    std::uint32_t physical_stack{};
    if (stack == 0U || (stack & 3U) != 0U || stack_size == 0U
        || !resolve_ee_main_ram_address(stack, physical_stack)
        || stack_size > kEeMainRamSize - physical_stack) {
        return refuse(EeThreadRejection::invalid_stack);
    }
    if (gp != 0U) {
        std::uint32_t physical_gp{};
        if (!resolve_ee_main_ram_address(gp, physical_gp)) {
            return refuse(EeThreadRejection::invalid_gp);
        }
    }
    if (initial_priority < 0
        || static_cast<std::uint32_t>(initial_priority) >= kMaxPriority) {
        return refuse(EeThreadRejection::invalid_priority);
    }

    for (std::size_t index = 0; index < kEeMaxThreads; ++index) {
        EeThread& thread = state.hle.threads[index];
        if (thread.in_use) {
            continue;
        }
        thread.in_use = true;
        thread.status = kEeThreadStatusDormant;
        thread.function = function;
        thread.stack = stack;
        thread.stack_size = stack_size;
        thread.gp = gp;
        thread.initial_priority = static_cast<std::uint32_t>(initial_priority);
        thread.current_priority = thread.initial_priority;
        thread.attr = words[7];
        thread.option = words[8];
        thread.argv = 0U;
        ++state.hle.thread_count;
        const auto id = static_cast<std::uint32_t>(kEeFirstCreatedThreadId + index);
        state.hle.last_thread_id = static_cast<std::int32_t>(id);
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(id);
        return Status::ok;
    }
    return refuse(EeThreadRejection::table_full);
}

// Insert a READY thread id after every queued thread whose current priority is
// less than or equal to its own. Lower numbers are higher priority on the PS2,
// and equal priorities keep their relative order. Returns false for an id
// outside the created range or a full queue; callers validate the id first.
[[nodiscard]] inline bool ee_insert_ready_thread(
    EeHleState& hle,
    std::uint32_t id
) noexcept {
    if (id < kEeFirstCreatedThreadId
        || id >= kEeFirstCreatedThreadId + kEeMaxThreads) {
        return false;
    }
    if (hle.ready_count >= kEeMaxThreads) {
        return false;
    }
    const std::uint32_t priority =
        hle.threads[id - kEeFirstCreatedThreadId].current_priority;
    std::size_t position = hle.ready_count;
    while (position > 0U) {
        const std::uint32_t previous_id = hle.ready_queue[position - 1U];
        const std::uint32_t previous_priority =
            hle.threads[previous_id - kEeFirstCreatedThreadId].current_priority;
        if (previous_priority <= priority) {
            break;
        }
        hle.ready_queue[position] = previous_id;
        --position;
    }
    hle.ready_queue[position] = id;
    ++hle.ready_count;
    return true;
}

// StartThread (0x22): StartThread(thread_id, args).
//
// ps2sdk declares `s32 StartThread(s32 thread_id, void *args)`. Its thread
// example calls it without checking the result and then lowers the *main*
// thread's priority, which shows the running thread and the started thread are
// distinct. The kernel-visible effect modeled here is the documented
// DORMANT -> READY transition and priority-ordered ready-queue insertion; the
// `args` pointer is stored opaquely because the kernel does not dereference it
// until the thread runs. Returns 0 on success and -1 on refusal.
//
// This implementation deliberately does NOT switch to the new thread: REPS2 has
// no dispatcher that can execute a second translated context, and claiming one
// had run would fabricate execution. The boot thread keeps executing and later
// observes itself through GetThreadId.
[[nodiscard]] inline Status hle_start_thread(EeState& state) noexcept {
    ++state.hle.start_thread_calls;
    state.hle.last_start_thread_id = ee_syscall_argument(state, 0);
    state.hle.last_start_thread_args = ee_syscall_argument(state, 1);
    state.hle.last_start_rejection = EeThreadStartRejection::none;

    const auto refuse = [&state](EeThreadStartRejection reason) {
        state.hle.last_start_rejection = reason;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    };

    const std::uint32_t id = state.hle.last_start_thread_id;
    if (id < kEeFirstCreatedThreadId
        || id >= kEeFirstCreatedThreadId + kEeMaxThreads) {
        return refuse(EeThreadStartRejection::invalid_id);
    }
    EeThread& thread = state.hle.threads[id - kEeFirstCreatedThreadId];
    if (!thread.in_use) {
        return refuse(EeThreadStartRejection::invalid_id);
    }
    if (thread.status != kEeThreadStatusDormant) {
        return refuse(EeThreadStartRejection::not_dormant);
    }
    if (state.hle.ready_count >= kEeMaxThreads) {
        return refuse(EeThreadStartRejection::queue_full);
    }
    thread.argv = state.hle.last_start_thread_args;
    thread.status = kEeThreadStatusReady;
    if (!ee_insert_ready_thread(state.hle, id)) {
        return refuse(EeThreadStartRejection::queue_full);
    }
    state.gpr[kEeSyscallResultRegister].low = 0U;
    return Status::ok;
}

// GetThreadId (0x2F): GetThreadId(void).
//
// ps2sdk declares `s32 GetThreadId(void)` and examples use it to refer to the
// calling thread. REPS2 executes only one guest context, so the caller is
// always the boot thread, whose id is kEeMainThreadId. A future dispatcher must
// replace this with the running record's id; until then the value is honest but
// limited.
[[nodiscard]] inline Status hle_get_thread_id(EeState& state) noexcept {
    ++state.hle.get_thread_id_calls;
    state.gpr[kEeSyscallResultRegister].low = kEeMainThreadId;
    return Status::ok;
}

// ChangeThreadPriority (0x29): ChangeThreadPriority(thread_id, priority).
//
// ps2sdk declares `s32 ChangeThreadPriority(s32 thread_id, s32 priority)` and
// its thread example guards the value with `priority < 128`. The boot thread has
// no created record, so its priority is kept separately. A READY thread is
// removed and re-inserted so the queue stays ordered. Returns 0 on success and
// -1 on refusal; a refused call changes nothing.
[[nodiscard]] inline Status hle_change_thread_priority(EeState& state) noexcept {
    ++state.hle.change_priority_calls;
    state.hle.last_priority_thread_id = ee_syscall_argument(state, 0);
    state.hle.last_priority_value =
        static_cast<std::int32_t>(ee_syscall_argument(state, 1));
    state.hle.last_priority_rejection = EeThreadPriorityRejection::none;

    const auto refuse = [&state](EeThreadPriorityRejection reason) {
        state.hle.last_priority_rejection = reason;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    };

    const std::int32_t priority = state.hle.last_priority_value;
    if (priority < 0 || priority >= kEeMaxThreadPriority) {
        return refuse(EeThreadPriorityRejection::invalid_priority);
    }
    const std::uint32_t id = state.hle.last_priority_thread_id;
    if (id == kEeMainThreadId) {
        state.hle.main_thread_priority = static_cast<std::uint32_t>(priority);
        state.gpr[kEeSyscallResultRegister].low = 0U;
        return Status::ok;
    }
    if (id < kEeFirstCreatedThreadId
        || id >= kEeFirstCreatedThreadId + kEeMaxThreads) {
        return refuse(EeThreadPriorityRejection::invalid_id);
    }
    EeThread& thread = state.hle.threads[id - kEeFirstCreatedThreadId];
    if (!thread.in_use) {
        return refuse(EeThreadPriorityRejection::invalid_id);
    }
    thread.current_priority = static_cast<std::uint32_t>(priority);
    if (thread.status == kEeThreadStatusReady) {
        std::uint32_t write = 0U;
        for (std::uint32_t read = 0U; read < state.hle.ready_count; ++read) {
            if (state.hle.ready_queue[read] == id) {
                continue;
            }
            state.hle.ready_queue[write] = state.hle.ready_queue[read];
            ++write;
        }
        state.hle.ready_count = write;
        if (!ee_insert_ready_thread(state.hle, id)) {
            return refuse(EeThreadPriorityRejection::invalid_id);
        }
    }
    state.gpr[kEeSyscallResultRegister].low = 0U;
    return Status::ok;
}

// ReferThreadStatus (0x30): ReferThreadStatus(thread_id, ee_thread_status_t *info).
//
// ps2sdk declares `s32 ReferThreadStatus(s32 thread_id, ee_thread_status_t
// *info)` and documents `TH_SELF 0` as referring to the calling thread for this
// service. `ee_thread_status_t` is twelve 32-bit words (sizeof == 0x30): status,
// func, stack, stack_size, gp_reg, initial_priority, current_priority, attr,
// option, waitType, waitId, wakeupCount, with documented THS_* status values
// and TSW_* wait codes.
//
// Every field comes from the host record, never from a guess:
//   * The boot thread reports THS_RUN because it is the executing context, and
//     reports the SetupThread-recorded gp, stack region and root function
//     (`$8` = 0x0010A1D0 at the observed crt0 call), with the ps2tek-documented
//     boot priority 0 as its initial priority and the recorded host value as its
//     current priority.
//   * A created record reports the CreateThread fields and the DORMANT/READY
//     status its services assigned.
//   * attr/option of the boot thread have no recorded source (SetupThread takes
//     no attribute word and the kernel's own TCB is not observed), so the host
//     record's zero is reported. waitType/waitId/wakeupCount are TSW_NONE/0/0:
//     this runtime has no wait or wakeup history for any thread.
// The observed caller at 0x00247010 writes the block to a module-global buffer
// and ignores `$v0`; the documented 0/-1 result is still produced. A refused
// call changes nothing: the buffer is validated and every word is formed before
// the first store. See ADR 0120.
[[nodiscard]] inline Status hle_refer_thread_status(EeState& state) noexcept {
    constexpr std::size_t kStatusWords = 12U;
    constexpr std::size_t kStatusBytes = kStatusWords * 4U;

    const std::uint32_t requested = ee_syscall_argument(state, 0);
    const std::uint32_t info = ee_syscall_argument(state, 1);
    ++state.hle.refer_thread_status_calls;
    state.hle.last_refer_thread_status_id = requested;
    state.hle.last_refer_thread_status_value = 0U;
    state.hle.last_refer_thread_status_rejection = EeThreadStatusRejection::none;

    const auto refuse = [&state](EeThreadStatusRejection reason) {
        state.hle.last_refer_thread_status_rejection = reason;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    };

    // TH_SELF names the calling thread; only the boot thread executes here.
    const std::uint32_t id = requested == kEeThreadSelfId ? kEeMainThreadId : requested;

    std::array<std::uint32_t, kStatusWords> words{};
    if (id == kEeMainThreadId) {
        words[0] = kEeThreadStatusRun;
        words[1] = state.hle.setup_thread_root_func;
        words[2] = state.hle.setup_thread_stack_top - state.hle.setup_thread_stack_size;
        words[3] = state.hle.setup_thread_stack_size;
        words[4] = state.hle.setup_thread_gp;
        words[5] = 0U; // ps2tek: boot creates the main thread with priority 0
        words[6] = state.hle.main_thread_priority;
        words[9] = kEeThreadWaitNone;
    } else {
        if (id < kEeFirstCreatedThreadId
            || id >= kEeFirstCreatedThreadId + kEeMaxThreads) {
            return refuse(EeThreadStatusRejection::invalid_id);
        }
        const EeThread& thread = state.hle.threads[id - kEeFirstCreatedThreadId];
        if (!thread.in_use) {
            return refuse(EeThreadStatusRejection::invalid_id);
        }
        words[0] = thread.status;
        words[1] = thread.function;
        words[2] = thread.stack;
        words[3] = thread.stack_size;
        words[4] = thread.gp;
        words[5] = thread.initial_priority;
        words[6] = thread.current_priority;
        words[7] = thread.attr;
        words[8] = thread.option;
        words[9] = kEeThreadWaitNone;
    }

    std::uint32_t physical{};
    if (resolve_ee_memory_access(state.memory, info, kStatusBytes, 4U, physical)
        != Status::ok) {
        return refuse(EeThreadStatusRejection::unwritable_buffer);
    }
    const auto offset = static_cast<std::size_t>(physical);
    for (std::size_t index = 0; index < kStatusWords; ++index) {
        const std::uint32_t value = words[index];
        const std::size_t word_offset = offset + index * 4U;
        state.memory.main_ram[word_offset] = static_cast<std::uint8_t>(value);
        state.memory.main_ram[word_offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
        state.memory.main_ram[word_offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
        state.memory.main_ram[word_offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
    }
    state.hle.last_refer_thread_status_value = words[0];
    state.gpr[kEeSyscallResultRegister].low = 0U;
    return Status::ok;
}

// GetOsdConfigParam (0x4B): GetOsdConfigParam(void *addr).
//
// ps2sdk declares `void GetOsdConfigParam(void *addr)`; the observed caller at
// 0x0025F148 passes a 4-byte stack buffer and reads it immediately. The kernel
// serves the board configuration from NVRAM; REPS2 has no NVRAM, so this writes
// a host-session word initialized to zero. The documented return type is void,
// so `$v0` is not part of the contract; a refused buffer reports -1 for
// diagnostics and writes nothing.
[[nodiscard]] inline Status hle_get_osd_config_param(EeState& state) noexcept {
    ++state.hle.get_osd_config_calls;
    const std::uint32_t address = ee_syscall_argument(state, 0);
    state.hle.last_osd_config_address = address;
    state.hle.last_osd_config_value = state.hle.osd_config_param;
    state.hle.last_osd_config_rejection = EeOsdConfigRejection::none;
    if (store_u32(state.memory, address, state.hle.osd_config_param) != Status::ok) {
        state.hle.last_osd_config_rejection = EeOsdConfigRejection::unwritable_buffer;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    state.gpr[kEeSyscallResultRegister].low = 0U;
    return Status::ok;
}

// SetOsdConfigParam (0x4A): SetOsdConfigParam(void *addr).
//
// ps2sdk declares `void SetOsdConfigParam(void *addr)`. The observed guest
// stores a modified 32-bit configuration and reads it back with 0x4B, so the
// guest-visible contract is a coherent store. The value is retained verbatim
// for the host session and is never persisted; no hardware-specific bit policy
// is invented.
[[nodiscard]] inline Status hle_set_osd_config_param(EeState& state) noexcept {
    ++state.hle.set_osd_config_calls;
    const std::uint32_t address = ee_syscall_argument(state, 0);
    state.hle.last_osd_config_address = address;
    state.hle.last_osd_config_rejection = EeOsdConfigRejection::none;
    std::uint32_t value{};
    if (load_u32(state.memory, address, value) != Status::ok) {
        state.hle.last_osd_config_rejection = EeOsdConfigRejection::unreadable_buffer;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    state.hle.osd_config_param = value;
    state.hle.last_osd_config_value = value;
    state.gpr[kEeSyscallResultRegister].low = 0U;
    return Status::ok;
}

// SetupThread (0x3C): SetupThread(gp, stack, stack_size, args, root_func).
// The guest-visible contract this implementation honors is the documented one:
// the call returns the initial stack pointer in $v0, computed from $a1/$a2 with
// the 0xFFFFFFFF sentinel meaning "place the stack below the top of main RAM",
// and leaves $v1 = 0.
//
// Not modeled, and therefore recorded as unobserved rather than assumed: no
// thread control block or ready-queue entry is created, the argv block at $a3 is
// not rewritten, and the fifth argument (root_func) is not consumed. Those
// require a scheduler and an EE-side kernel object model that do not exist yet,
// so a title that depends on them will stop at the next service it needs instead
// of silently running with a half-built thread.
[[nodiscard]] inline Status hle_setup_thread(EeState& state) noexcept {
    const std::uint32_t gp = ee_syscall_argument(state, 0);
    const std::uint32_t stack = ee_syscall_argument(state, 1);
    const std::uint32_t stack_size = ee_syscall_argument(state, 2);
    const std::uint32_t args = ee_syscall_argument(state, 3);

    const std::uint32_t stack_top = stack == kEeSyscallStackSentinel
        ? kEeMainRamSize - kEeSetupThreadTopGuard
        : stack + stack_size;
    const std::uint32_t stack_pointer = stack_top - kEeSetupThreadStackReserve;

    state.hle.setup_thread_called = true;
    state.hle.setup_thread_gp = gp;
    state.hle.setup_thread_stack_top = stack_top;
    state.hle.setup_thread_stack_size = stack_size;
    state.hle.setup_thread_stack_pointer = stack_pointer;
    state.hle.setup_thread_args = args;
    // The observed crt0 call sets $8 to 0x0010A1D0 (the ExitThread thunk) right
    // before the trap; the EE syscall ABI carries only four argument registers,
    // so the fifth argument arrives in $8. Record it as the boot thread's root
    // function rather than discarding it.
    state.hle.setup_thread_root_func = state.gpr[8].low;

    // A 32-bit EE result write sign-extends into the 64-bit GPR lane.
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(stack_pointer);
    // Observed kernel behavior: the call clears $v1 on return.
    state.gpr[kEeSyscallNumberRegister].low = 0;
    return Status::ok;
}

// SetupHeap (0x3D): SetupHeap(heap_start, heap_size).
//
// Two forms are implemented, and they are not equally well evidenced:
//
//   * An explicit `(heap_start, heap_size)` pair records the per-thread range
//     and returns its end. The addition is 32-bit modular, matching an
//     EE-pointer-width computation rather than trapping on overflow.
//   * `heap_size == -1` treats the heap as ending at the stack pointer that
//     SetupThread returned. ps2sdk's own crt0 passes -1 in that slot while the
//     public description places its sentinel on the address operand, so this
//     form is a named compatibility extension required by the observed retail
//     call, not a settled hardware fact (ADR 0064).
//
// Not modeled: no kernel allocator, overlap or alignment policy, no repeated-
// call semantics, and no CreateHeap promotion. The recorded range is a host
// observation of what the guest asked for; nothing allocates from it yet.
[[nodiscard]] inline Status hle_setup_heap(EeState& state) noexcept {
    const std::uint32_t heap_start = ee_syscall_argument(state, 0);
    const std::uint32_t heap_size = ee_syscall_argument(state, 1);

    // ps2tek documents -1 on the heap address, while ps2sdk's crt0 and the
    // observed retail call pass -1 as heap_size. Accept the latter as an
    // explicitly named compatibility extension: the heap ends at the current
    // thread's SetupThread stack pointer. Do not invent allocator metadata or
    // an error code; the kernel API is declared void by ps2sdk.
    const bool inferred_end = heap_size == kEeSyscallStackSentinel;
    const std::uint32_t heap_end = inferred_end
        ? state.hle.setup_thread_stack_pointer
        : heap_start + heap_size;

    state.hle.setup_heap_called = true;
    state.hle.setup_heap_start = heap_start;
    state.hle.setup_heap_size = heap_size;
    state.hle.setup_heap_end = heap_end;
    // ps2tek describes the machine-level result as the end of the current
    // thread's heap, although ps2sdk declares SetupHeap(void) and callers often
    // discard it. Preserve that documented result for code that reads $v0.
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(heap_end);
    return Status::ok;
}

// FlushCache (0x64): the guest asks the kernel to make written instructions
// visible to the instruction stream. The runtime keeps no instruction-cache
// model and the host is cache-coherent, so the call completes with $v0 = 0 and
// touches no other state. It stops being a stub only once a real cache model
// exists; until then this is an accurate description of the host, not a
// fabricated kernel behavior.
// CreateSema (0x40): CreateSema(ee_sema_t *sema).
//
// The guest parameter block is four signed ints -- count, max_count, init_count,
// wait_threads -- followed by the attribute and option words. Only init_count
// and max_count are inputs: count and wait_threads are the fields
// ReferSemaStatus (0x47) reports back, so they are read but never interpreted.
//
// The documented result is the new semaphore's id on success and -1 on failure.
// A refused call writes that -1 and allocates nothing, so the guest sees the
// documented failure code and the host never invents an id. What is validated,
// with the reason recorded for diagnostics: word-aligned readable parameters, a
// non-negative max_count/init_count pair, init_count not above max_count, and a
// free slot in the kernel's 256-object table.
//
// Not modeled: no waiter list, no dispatcher interaction, and no write-back to
// the parameter block (the guest block is an input, not a status mirror). Id
// allocation order -- lowest free slot -- is a host choice, because only id
// uniqueness and the documented table bound are evidenced, not the kernel's own
// allocation order. See ADR 0066.
[[nodiscard]] inline Status hle_create_sema(EeState& state) noexcept {
    constexpr std::size_t kParameterWords = 6U;
    constexpr std::size_t kParameterBytes = kParameterWords * 4U;

    const std::uint32_t parameters = ee_syscall_argument(state, 0);
    ++state.hle.create_sema_calls;
    state.hle.last_sema_parameter_address = parameters;
    state.hle.last_sema_id = -1;
    state.hle.last_sema_rejection = EeSemaRejection::none;
    state.hle.last_sema_init_count = 0;
    state.hle.last_sema_max_count = 0;
    state.hle.last_sema_attr = 0;
    state.hle.last_sema_option = 0;

    const auto refuse = [&state](EeSemaRejection reason) {
        state.hle.last_sema_rejection = reason;
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    };

    std::uint32_t physical{};
    if (resolve_ee_memory_access(state.memory, parameters, kParameterBytes, 4U, physical)
        != Status::ok) {
        return refuse(EeSemaRejection::unreadable_parameters);
    }

    std::array<std::uint32_t, kParameterWords> words{};
    for (std::size_t index = 0; index < kParameterWords; ++index) {
        const std::size_t offset = static_cast<std::size_t>(physical) + index * 4U;
        words[index] = static_cast<std::uint32_t>(state.memory.main_ram[offset])
            | (static_cast<std::uint32_t>(state.memory.main_ram[offset + 1U]) << 8U)
            | (static_cast<std::uint32_t>(state.memory.main_ram[offset + 2U]) << 16U)
            | (static_cast<std::uint32_t>(state.memory.main_ram[offset + 3U]) << 24U);
    }

    const auto max_count = static_cast<std::int32_t>(words[1]);
    const auto init_count = static_cast<std::int32_t>(words[2]);
    state.hle.last_sema_init_count = init_count;
    state.hle.last_sema_max_count = max_count;
    state.hle.last_sema_attr = words[4];
    state.hle.last_sema_option = words[5];

    if (init_count < 0 || max_count < 0) {
        return refuse(EeSemaRejection::negative_counts);
    }
    if (init_count > max_count) {
        return refuse(EeSemaRejection::init_exceeds_max);
    }

    for (std::size_t index = 0; index < kEeMaxSemaphores; ++index) {
        EeSema& sema = state.hle.semaphores[index];
        if (sema.in_use) {
            continue;
        }
        sema.in_use = true;
        sema.count = init_count;
        sema.max_count = max_count;
        sema.attr = words[4];
        sema.option = words[5];
        sema.create_pc = state.pc;
        ++state.hle.semaphore_count;
        const auto id = static_cast<std::uint32_t>(index);
        state.hle.last_sema_id = static_cast<std::int32_t>(id);
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(id);
        return Status::ok;
    }
    return refuse(EeSemaRejection::table_full);
}

// Resolve a semaphore id to a live object, or nullptr. The id is the table slot
// CreateSema returned, so an id outside the 256-object table or one whose slot
// is free is not a live object.
[[nodiscard]] inline EeSema* find_ee_sema(EeHleState& hle, std::int32_t id) noexcept {
    if (id < 0) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(id);
    if (index >= kEeMaxSemaphores || !hle.semaphores[index].in_use) {
        return nullptr;
    }
    return &hle.semaphores[index];
}

// The documented result of the semaphore services that return an int: 0 on
// success and -1 on failure. The observed retail callers ignore the value, and
// the kernel's own failure taxonomy is not reproducible here, so only these two
// documented values are emitted.
inline constexpr std::uint32_t kEeSyscallSemaphoreSuccess = 0U;

// Reset the per-call semaphore observation and resolve the requested id. Every
// semaphore service starts here so a stale count or block flag from a previous
// call can never be attributed to this one.
[[nodiscard]] inline EeSema* begin_ee_sema_service(
    EeHleState& hle,
    std::int32_t id
) noexcept {
    hle.last_sema_op_id = id;
    hle.last_sema_op_valid = false;
    hle.last_sema_op_count_after = -1;
    hle.last_sema_wait_count_before = -1;
    hle.last_sema_wait_blocked = false;
    return find_ee_sema(hle, id);
}

// Raise a live semaphore's count, reporting whether the raise happened. A
// signalled semaphore that already holds its guest-supplied max_count cannot go
// higher; the kernel's overflow behaviour is not established by the pinned
// sources, so the raise is refused rather than counted.
[[nodiscard]] inline bool raise_ee_sema_count(EeSema* sema) noexcept {
    if (sema == nullptr || sema->count >= sema->max_count) {
        return false;
    }
    ++sema->count;
    return true;
}

// DeleteSema (0x41): DeleteSema(int sema_id).
//
// Releases the object's table slot. An id that names no live object writes the
// documented -1 and changes nothing. There is no waiter list to wake, so no
// side effect beyond the slot release is invented.
[[nodiscard]] inline Status hle_delete_sema(EeState& state) noexcept {
    const auto id = static_cast<std::int32_t>(ee_syscall_argument(state, 0));
    ++state.hle.delete_sema_calls;
    EeSema* const sema = begin_ee_sema_service(state.hle, id);
    if (sema == nullptr) {
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    *sema = EeSema{};
    --state.hle.semaphore_count;
    state.hle.last_sema_op_valid = true;
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallSemaphoreSuccess);
    return Status::ok;
}

// SignalSema (0x42) / iSignalSema (0x43): raise the semaphore's count.
//
// Without a scheduler there is no thread to make runnable, so the only modelled
// effect is the count going up. Dispatch resolves the negated i-form by
// magnitude, so both numbers share this handler. An id that names no live object
// writes the documented -1. A signal on a semaphore already at the guest-supplied
// max_count cannot raise the count; the kernel's overflow behaviour is not
// established by the pinned sources, so the call is refused with the documented
// failure value and changes nothing rather than reporting success for a no-op.
[[nodiscard]] inline Status hle_signal_sema(EeState& state) noexcept {
    const auto id = static_cast<std::int32_t>(ee_syscall_argument(state, 0));
    ++state.hle.signal_sema_calls;
    EeSema* const sema = begin_ee_sema_service(state.hle, id);
    if (!raise_ee_sema_count(sema)) {
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    state.hle.last_sema_op_count_after = sema->count;
    state.hle.last_sema_op_valid = true;
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallSemaphoreSuccess);
    return Status::ok;
}

// WaitSema (0x44): WaitSema(int sema_id).
//
// With a free count the call consumes it and returns the documented 0. With a
// zero count a real kernel suspends the caller; REPS2 cannot schedule and does
// not produce the awaited event, so it stops at the SYSCALL as
// Status::semaphore_wait_blocked rather than returning a fabricated 0. An id
// that names no live object is the documented failure, not a block.
[[nodiscard]] inline Status hle_wait_sema(EeState& state) noexcept {
    const auto id = static_cast<std::int32_t>(ee_syscall_argument(state, 0));
    ++state.hle.wait_sema_calls;
    EeSema* const sema = begin_ee_sema_service(state.hle, id);
    if (sema == nullptr) {
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    state.hle.last_sema_wait_count_before = sema->count;
    if (sema->count <= 0) {
        state.hle.last_sema_wait_blocked = true;
        return Status::semaphore_wait_blocked;
    }
    --sema->count;
    state.hle.last_sema_op_count_after = sema->count;
    state.hle.last_sema_op_valid = true;
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallSemaphoreSuccess);
    return Status::ok;
}

// PollSema (0x45) / iPollSema (0x46): try to acquire without blocking.
//
// ps2tek's EE syscall list documents `45h int PollSema(int sema_id)` /
// `46h int iPollSema(int sema_id)` as "decrements the semaphore's count
// variable. Returns -1 if unsuccessful", explicitly contrasted with `44h
// WaitSema`, which reschedules the caller when the count is zero. It states
// only the failure value, so the success value comes from the target: the
// retail thunk at `0x0025A730` is `li $v1,0x45; syscall; jr $ra`, and the
// module-private wrappers at `0x00246FA0` and its siblings call it with the
// stored id and then require `$v0 == id` before continuing
// (`lw $v1,-0x7D8($s6)`; `bne $v1,$v0,<bail>`). A success value of anything
// but the id would make every one of those wrappers take its bail path, so a
// consumed count returns the id itself, exactly as `CreateSema` returns the new
// id. Failure is the documented -1: a zero count and an id that names no live
// object are both "unsuccessful". A poll never blocks and never makes a thread
// runnable, so the count and the result are the only modelled effects and the
// status is a normal completion even on failure.
[[nodiscard]] inline Status hle_poll_sema(EeState& state) noexcept {
    const auto id = static_cast<std::int32_t>(ee_syscall_argument(state, 0));
    ++state.hle.poll_sema_calls;
    EeSema* const sema = begin_ee_sema_service(state.hle, id);
    state.hle.last_sema_poll_count_before = sema != nullptr ? sema->count : -1;
    state.hle.last_sema_poll_acquired = false;
    if (state.hle.first_sema_poll_count_before < 0) {
        state.hle.first_sema_poll_count_before = state.hle.last_sema_poll_count_before;
    }
    if (sema == nullptr || sema->count <= 0) {
        state.gpr[kEeSyscallResultRegister].low = sign_extend_word(kEeSyscallFailure);
        return Status::ok;
    }
    --sema->count;
    ++state.hle.poll_sema_acquired_total;
    state.hle.last_sema_poll_acquired = true;
    state.hle.last_sema_op_count_after = sema->count;
    state.hle.last_sema_op_valid = true;
    state.gpr[kEeSyscallResultRegister].low =
        sign_extend_word(static_cast<std::uint32_t>(id));
    return Status::ok;
}

[[nodiscard]] inline Status hle_set_syscall(EeState& state) noexcept {
    const std::uint32_t slot = ee_syscall_argument(state, 0);
    const std::uint32_t handler = ee_syscall_argument(state, 1);
    ++state.hle.set_syscall_calls;
    state.hle.last_set_syscall_slot = slot;
    state.hle.last_set_syscall_handler = handler;
    state.hle.last_set_syscall_rejection = EeSetSyscallRejection::none;
    const auto refuse = [&state](EeSetSyscallRejection reason) {
        state.hle.last_set_syscall_rejection = reason;
        return Status::ok;
    };
    if (slot >= kEeSyscallTableSlots) {
        return refuse(EeSetSyscallRejection::invalid_slot);
    }
    std::uint32_t physical_handler{};
    if (handler == 0U || (handler & 3U) != 0U
        || !resolve_ee_main_ram_address(handler, physical_handler)) {
        return refuse(EeSetSyscallRejection::invalid_handler);
    }
    // Reserve the host-side registration before touching guest memory so a
    // refusal cannot leave a half-applied registration.
    EeExtendedSyscallRegistration* extended = nullptr;
    if (slot >= kEeMaxSyscallSlots) {
        for (EeExtendedSyscallRegistration& registration :
             state.hle.extended_syscall_registrations) {
            if (registration.in_use && registration.number == slot) {
                extended = &registration;
                break;
            }
            if (!registration.in_use && extended == nullptr) {
                extended = &registration;
            }
        }
        if (extended == nullptr) {
            return refuse(EeSetSyscallRejection::registration_table_full);
        }
    }
    // The kernel-visible half of the registration: the guest can read and patch
    // the table directly, so the pointer must be present in guest RAM itself.
    if (store_u32(state.memory, kEeSyscallTableBase + slot * 4U, handler)
        != Status::ok) {
        return refuse(EeSetSyscallRejection::table_unwritable);
    }
    if (extended != nullptr) {
        extended->in_use = true;
        extended->number = slot;
        extended->handler_address = handler;
        return Status::ok;
    }
    auto& registration = state.hle.syscall_registrations[slot];
    registration.in_use = true;
    registration.handler_address = handler;
    return Status::ok;
}

[[nodiscard]] inline Status hle_flush_cache(EeState& state) noexcept {
    state.gpr[kEeSyscallResultRegister].low = 0;
    return Status::ok;
}

// sceSifSetDChain (0x78): start the SIF0 DMA channel.
//
// The observed retail init (SCUS_971.77 at `0x0025C26C`) reads D_STAT, clears
// the SIF0 status bit, reads SIF0 CHCR and calls this service only when
// `CHCR_STR` is clear. PS2SDK's `sceSifInitCmd` has the same shape and declares
// the entry `void`. The kernel-side chain builder is not modelled: this starts
// whatever SIF0 transfer the channel registers already describe, and the DMAC
// owner turns a 0->1 `STR` start into the SIF0 completion event. It never
// invents a transfer buffer or an IOP response. The service writes no $v0
// because the SDK contract defines no result, and it refuses to start when the
// channel or D_STAT state is unknown rather than starting without an event.
[[nodiscard]] inline Status hle_sif_set_d_chain(EeState& state) noexcept {
    std::uint32_t chcr = 0U;
    if (!state.memory.dmac.read_chcr5(chcr) || !state.memory.dmac.d_stat_known) {
        return Status::unsupported_memory_access;
    }
    if ((chcr & kEeDmacChcrStrMask) != 0U) {
        return Status::ok; // already running: a second start has no effect
    }
    return state.memory.dmac.write_chcr5(chcr | kEeDmacChcrStrMask)
        ? Status::ok
        : Status::unsupported_memory_access;
}

// sceSifStopDma (0x6B): stop SIF0 (IOP -> EE) transfers.
//
// PS2SDK declares `void sceSifStopDma(void)` and comments "Disables SIF0
// (IOP -> EE)" (`ee/kernel/include/kernel.h:482`). `SifIopReset` calls it before
// sending the reset command (`ee/kernel/src/iopcontrol.c:42`), and the observed
// post-teardown path reaches it at `0x25EBE4` inside the retail `SifIopReset`.
// PCSX2's SIF0 interrupt handler clears `CHCR.STR` when a transfer completes, so
// stopping clears the same start bit and leaves the other CHCR fields alone.
// REPS2 models every SIF0 start as an immediately completed transfer that
// self-clears `STR`, so an idle channel is unchanged; the service never reports
// a transfer it did not perform, and it fails closed when the channel state is
// unknown. PS2SDK declares the entry `void`, so `$v0` is not written.
[[nodiscard]] inline Status hle_sif_stop_dma(EeState& state) noexcept {
    ++state.hle.sif_stop_dma_calls;
    std::uint32_t chcr = 0U;
    if (!state.memory.dmac.read_chcr5(chcr)) {
        state.hle.last_sif_stop_dma_str_cleared = false;
        return Status::unsupported_memory_access;
    }
    state.hle.last_sif_stop_dma_str_cleared = (chcr & kEeDmacChcrStrMask) != 0U;
    if ((chcr & kEeDmacChcrStrMask) == 0U) {
        return Status::ok; // already stopped: nothing to clear
    }
    return state.memory.dmac.write_chcr5(chcr & ~kEeDmacChcrStrMask)
        ? Status::ok
        : Status::unsupported_memory_access;
}

// The SIF register numbers and status flags are declared with the SIF device
// state in ee_state.hpp. This returns the host slot holding one SIF register, or
// nullptr when the number is not a documented register, so sceSifGetReg (0x7a)
// and sceSifSetReg (0x79) share exactly one address space.
[[nodiscard]] inline std::uint32_t* find_sif_register(
    EeHleState& hle,
    std::uint32_t reg
) noexcept {
    if (reg >= kEeSifRegisterMainAddr && reg <= kEeSifRegisterSmFlag) {
        return &hle.sif_hardware_registers[reg];
    }
    if (reg >= kEeSifRegisterSystemBase
        && reg < kEeSifRegisterSystemBase + kEeSifSystemRegisterCount) {
        return &hle.sif_system_registers[reg - kEeSifRegisterSystemBase];
    }
    return nullptr;
}

// sceSifGetReg (0x7a): read a SIF register.
//
// PS2SDK declares `unsigned int sceSifGetReg(int reg)` and uses it to read both
// the hardware SIF registers (`SIF_REG_MAINADDR` 1, `SUBADDR` 2, `MSFLAG` 3,
// `SMFLAG` 4; EE addresses `0x1000F200`..`0x1000F230`) and the kernel's software
// registers (`SIF_SYSREG_*`, `0x80000000` + 0..2). Its own `sceSifInitCmd` shows
// what the observed title does with the result: it reads `SIF_SYSREG_SUBADDR`
// and, when non-zero, treats the IOP-side SIF command system as already
// initialised; when zero it waits for the IOP's `SMFLAG` command-init flag and
// then adopts the hardware `SUBADDR`.
//
// REPS2 has no IOP, so the honest model is that the IOP-side SIF command system
// is *not* initialised: every register reads as zero. Reporting a non-zero
// sub-address would supply an invented IOP-side buffer address that the SDK
// stores and later programs into SIF DMA transfer structures, which is exactly
// the fabricated device state this layer must not produce.
//
// A register number outside the documented set stops the run as
// `unhandled_syscall` rather than answering with an invented value; the observed
// path never reaches that branch. The value is returned in `$v0` with the same
// 32-bit sign extension the rest of the layer uses.
[[nodiscard]] inline Status hle_sif_get_reg(EeState& state) noexcept {
    const std::uint32_t reg = ee_syscall_argument(state, 0);
    ++state.hle.sif_get_reg_calls;
    state.hle.last_sif_register = reg;
    std::uint32_t* const slot = find_sif_register(state.hle, reg);
    if (slot == nullptr) {
        state.hle.last_sif_register_rejection = EeSifRegisterRejection::unknown_register;
        return Status::unhandled_syscall;
    }
    state.hle.last_sif_register_rejection = EeSifRegisterRejection::none;
    // A modelled IOP reboot completes between the EE's reset DMA and its first
    // SMFLAG observation: the booted IOP asserts SIFINIT, CMDINIT and BOOTEND.
    // This models boot completion timing only; the requested IOP image itself
    // is never executed (ADR 0118).
    if (reg == kEeSifRegisterSmFlag && state.hle.sif_cmd.reset_pending_boot) {
        auto& sif = state.hle.sif_cmd;
        sif.smflag = kEeSifStatSifInit | kEeSifStatCmdInit | kEeSifStatBootEnd;
        *slot = sif.smflag;
        sif.reset_pending_boot = false;
        ++sif.completed_resets;
    }
    // The SIFCMD model publishes SMFLAG/MSFLAG into these same slots, so the
    // register file is the single source of truth.
    state.hle.last_sif_register_value = *slot;
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(state.hle.last_sif_register_value);
    return Status::ok;
}

// sceSifSetReg (0x79): write a SIF register.
//
// PS2SDK declares `void sceSifSetReg(int reg, unsigned int value)` and uses it
// to write both the hardware SIF registers (1..4) and the kernel software
// registers (0x80000000+0..2). The observed title uses it after the IOP
// handshake to adopt the IOP's sub-address into the software registers.
//
// REPS2 has no IOP, so this simply writes into the HLE register file. The
// register file is shared with sceSifGetReg, so a later read returns the
// written value. An undocumented register number stops as `unhandled_syscall`.
[[nodiscard]] inline Status hle_sif_set_reg(EeState& state) noexcept {
    const std::uint32_t reg = ee_syscall_argument(state, 0);
    const std::uint32_t value = ee_syscall_argument(state, 1);
    ++state.hle.sif_set_reg_calls;
    state.hle.last_sif_set_register = reg;
    std::uint32_t* const slot = find_sif_register(state.hle, reg);
    if (slot == nullptr) {
        state.hle.last_sif_set_register_rejection = EeSifRegisterRejection::unknown_register;
        return Status::unhandled_syscall;
    }
    state.hle.last_sif_set_register_rejection = EeSifRegisterRejection::none;
    *slot = value;
    return Status::ok;
}

// SIF DMA transfer structure, matching PS2SDK SifDmaTransfer_t.
struct SifDmaTransfer {
    std::uint32_t src;
    std::uint32_t dest;
    std::int32_t size;
    // SDK names this field int, but it is a raw 32-bit flag mask. Keep its
    // guest-memory bits unsigned so bit tests do not require signed conversion.
    std::uint32_t attr;
};

// Forward declaration: process_sif_command is defined below. `payload` is the
// extra-data transfer entry `_SifSendCmd` puts before the command packet; it
// carries an RPC call's request bytes.
[[nodiscard]] inline Status process_sif_command(
    EeState& state,
    std::uint32_t packet_addr,
    const SifDmaTransfer* payload = nullptr) noexcept;

// DMAC SIF0 channel attribute bits per PS2SDK ee/include/sifdma.h.
inline constexpr std::uint32_t kEeSifDmaAttrIntI = 0x2U;
inline constexpr std::uint32_t kEeSifDmaAttrIntO = 0x4U;
inline constexpr std::uint32_t kEeSifDmaAttrErt  = 0x40U;

// sceSifSetDma (0x77): program and start a SIF0 DMA transfer.
//
// PS2SDK declares `int sceSifSetDma(SifDmaTransfer_t *dmat, int count)`.
// The observed title reaches this service after the IOP handshake completes.
// The kernel-side `_SifSendCmd` builds a two-entry transfer array: an optional
// extra-data entry (when `size > 0`) and the packet entry with
// `attr = SIF_DMA_ERT | SIF_DMA_INT_O`. The transfer count is 1 or 2.
// The EE kernel writes the entries to SIF0 channel registers (CHCR5, MADR5,
// QWC5, TADR5) and starts the channel via CHCR_STR.
//
// REPS2 models the EE-side register programming exactly as the kernel would.
// Each entry in `dmat` is translated to a SIF0 channel register setup. The
// transfer is started by setting CHCR_STR. The DMAC owner models the channel
// as an immediately completed transfer that publishes the SIF0 event to D_STAT
// and self-clears STR. The service returns the transfer ID (index + 1) on
// success, or 0 on failure (matching the SDK convention).
[[nodiscard]] inline Status hle_sif_set_dma(EeState& state) noexcept {
    const std::uint32_t dmat_addr = ee_syscall_argument(state, 0);
    const std::int32_t count = static_cast<std::int32_t>(ee_syscall_argument(state, 1));
    ++state.hle.sif_set_dma_calls;
    state.hle.last_sif_set_dma_addr = dmat_addr;
    state.hle.last_sif_set_dma_count = count;

    if (count <= 0 || count > 2) {
        state.gpr[kEeSyscallResultRegister].low = 0;
        return Status::ok;
    }

    // Read the transfer array from guest memory.
    SifDmaTransfer entries[2];
    std::uint32_t current = dmat_addr;
    for (std::int32_t i = 0; i < count; ++i) {
        std::uint32_t src{}, dest{}, size{}, attr{};
        if (load_u32(state.memory, current, src) != Status::ok
            || load_u32(state.memory, current + 4U, dest) != Status::ok
            || load_u32(state.memory, current + 8U, size) != Status::ok
            || load_u32(state.memory, current + 12U, attr) != Status::ok) {
            state.gpr[kEeSyscallResultRegister].low = 0;
            return Status::ok;
        }
        entries[i] = {src, dest, static_cast<std::int32_t>(size), attr};
        current += 16U;
    }

    // Use the DMAC owner to program the SIF0 channel. The SDK typically
    // submits 1 or 2 entries; we process them sequentially, with the last
    // entry starting the channel. The DMAC owner models each STR write as
    // an immediately completed transfer that publishes D_STAT SIF0 event.
    std::uint32_t transfer_id = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        const auto& e = entries[i];
        // Program MADR5 (source address).
        if (!state.memory.dmac.write_madr5(e.src)) {
            state.gpr[kEeSyscallResultRegister].low = 0;
            return Status::ok;
        }
        // Program QWC5 (quadword count = size / 16, rounded up).
        const std::uint32_t qwc = (static_cast<std::uint32_t>(e.size) + 15U) / 16U;
        if (!state.memory.dmac.write_qwc5(qwc)) {
            state.gpr[kEeSyscallResultRegister].low = 0;
            return Status::ok;
        }
        // Program TADR5 (chain address - for multi-entry chains).
        if (!state.memory.dmac.write_tadr5(0U)) {
            state.gpr[kEeSyscallResultRegister].low = 0;
            return Status::ok;
        }
        // Start the channel via CHCR_STR.
        std::uint32_t chcr = 0U;
        if (!state.memory.dmac.read_chcr5(chcr)) {
            state.gpr[kEeSyscallResultRegister].low = 0;
            return Status::ok;
        }
        // CHCR bit 8 = STR, bit 0 = DIR (0 = to device, 1 = from device).
        // SIF_DMA_INT_O (0x4) means interrupt on completion (DIR=0: to device).
        std::uint32_t new_chcr = (chcr & ~kEeDmacChcrDirMask) | kEeDmacChcrStrMask;
        if ((e.attr & kEeSifDmaAttrIntI) != 0U) {
            new_chcr |= kEeDmacChcrDirMask; // from device
        }
        if (!state.memory.dmac.write_chcr5(new_chcr)) {
            state.gpr[kEeSyscallResultRegister].low = 0;
            return Status::ok;
        }
        ++transfer_id;
    }

    // Return transfer ID (1-based, matching SDK convention).
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(transfer_id);

    // The SIF0 start publishes its completion event to D_STAT (see ADR 0092/0091).
    // REPS2 delivers no DMAC interrupt, so a channel-5 handler registered through
    // AddDmacHandler is never invoked and no thread is signalled here; a title
    // that waits on a completion semaphore therefore stops at that wait rather
    // than advancing on a fabricated signal.

    // If the last transfer entry had SIF_DMA_ERT set, it carries a SIF command
    // packet. Process it as the IOP-side SIFCMD would.
    if (count > 0) {
        const auto& last = entries[count - 1];
        if ((last.attr & kEeSifDmaAttrErt) != 0U) {
            // Per PS2SDK `_SifSendCmd`, the command entry copies the packet from
            // the EE source address (`src`) into the IOP receive buffer (`dest`).
            // REPS2 does not model IOP memory, so the packet is parsed from the
            // EE source it is still resident at, not the IOP destination.
            // When a second entry exists it is the command's extra data (an RPC
            // call's request payload), which the IOP-side handler reads.
            const SifDmaTransfer* const payload =
                count == 2 ? &entries[0] : nullptr;
            if (process_sif_command(state, last.src, payload) != Status::ok) {
                // Process failure is non-fatal for the EE kernel path.
            }
        }
    }
    return Status::ok;
}

// SIF command IDs (from PS2SDK ee/include/sifcmd-common.h).
inline constexpr std::uint32_t kEeSifCmdIdSystem = 0x8000'0000U;
inline constexpr std::uint32_t kEeSifCmdChangeSaddr = kEeSifCmdIdSystem | 0;
inline constexpr std::uint32_t kEeSifCmdSetSreg     = kEeSifCmdIdSystem | 1;
inline constexpr std::uint32_t kEeSifCmdInitCmd     = kEeSifCmdIdSystem | 2;
inline constexpr std::uint32_t kEeSifCmdResetCmd    = kEeSifCmdIdSystem | 3;
inline constexpr std::uint32_t kEeSifCmdRpcEnd      = kEeSifCmdIdSystem | 8;
inline constexpr std::uint32_t kEeSifCmdRpcBind     = kEeSifCmdIdSystem | 9;
inline constexpr std::uint32_t kEeSifCmdRpcCall     = kEeSifCmdIdSystem | 10;
inline constexpr std::uint32_t kEeSifCmdRpcRdata    = kEeSifCmdIdSystem | 12;

// SIF status flags are declared in ee_state.hpp next to the device state.

// SIF CMD packet header size (16 bytes).
inline constexpr std::uint32_t kEeSifCmdHeaderSize = 16U;

// SIF RPC packet field offsets (PS2SDK common/include/sifrpc-common.h). Every
// packet starts with a 16-byte SifCmdHeader, then the RPC header fields. A bind
// request carries `cd` and `sid`; the reply carries `cd`, the originating
// command id, and the server/buffer pointers. Both are 32-bit on the EE.
inline constexpr std::uint32_t kEeSifRpcPktRecIdOffset = 0x10U;
inline constexpr std::uint32_t kEeSifRpcPktAddrOffset = 0x14U;
inline constexpr std::uint32_t kEeSifRpcPktRpcIdOffset = 0x18U;
inline constexpr std::uint32_t kEeSifRpcPktCdOffset = 0x1CU;
inline constexpr std::uint32_t kEeSifRpcPktCidOffset = 0x20U; // reply: originating command
inline constexpr std::uint32_t kEeSifRpcBindSidOffset = 0x20U; // request: server id
inline constexpr std::uint32_t kEeSifRpcPktSdOffset = 0x24U;
inline constexpr std::uint32_t kEeSifRpcPktBufOffset = 0x28U;
inline constexpr std::uint32_t kEeSifRpcPktCbufOffset = 0x2CU;
// A call request reuses the same 16-byte RPC header and then carries its own
// fields (PS2SDK `SifRpcCallPkt_t`): the function number at 0x20 where a reply
// carries the originating command id, then the sizes, buffers, mode and token.
inline constexpr std::uint32_t kEeSifRpcCallNumberOffset = 0x20U;
inline constexpr std::uint32_t kEeSifRpcCallSendSizeOffset = 0x24U;
inline constexpr std::uint32_t kEeSifRpcCallRecvBufOffset = 0x28U;
inline constexpr std::uint32_t kEeSifRpcCallRecvSizeOffset = 0x2CU;
inline constexpr std::uint32_t kEeSifRpcCallRmodeOffset = 0x30U;
inline constexpr std::uint32_t kEeSifRpcCallSdOffset = 0x34U;
// The used prefix of an `SifRpcRendPkt_t`; the IOP sends a full 64-byte slot,
// whose trailing 16 bytes are IOP packet-slot scratch that REPS2 does not model.
inline constexpr std::uint32_t kEeSifRpcRendPacketSize = 48U;
// The EE-side client object (`SifRpcClientData_t`) fields the reply touches.
inline constexpr std::uint32_t kEeSifRpcClientPktAddrOffset = 0x00U;
inline constexpr std::uint32_t kEeSifRpcClientSemaIdOffset = 0x08U;
inline constexpr std::uint32_t kEeSifRpcClientBufOffset = 0x14U;
inline constexpr std::uint32_t kEeSifRpcClientCbufOffset = 0x18U;
inline constexpr std::uint32_t kEeSifRpcClientEndFunctionOffset = 0x1CU;
inline constexpr std::uint32_t kEeSifRpcClientServerOffset = 0x24U;
// `PACKET_F_ALLOC` from PS2SDK ee/kernel/src/sifrpc.c.
inline constexpr std::uint32_t kEeSifRpcPacketAllocFlag = 0x01U;

// SIF command packet header, matching PS2SDK SifCmdHeader_t.
struct SifCmdHeader {
    std::uint32_t psize_dsize; // psize (8 bits) | dsize (24 bits)
    std::uint32_t dest;
    std::int32_t  cid;
    std::uint32_t opt;
};

[[nodiscard]] constexpr const char* ee_sif_cmd_name(std::uint32_t cid) noexcept {
    switch (cid) {
    case kEeSifCmdChangeSaddr: return "SIF_CMD_CHANGE_SADDR";
    case kEeSifCmdSetSreg:     return "SIF_CMD_SET_SREG";
    case kEeSifCmdInitCmd:     return "SIF_CMD_INIT_CMD";
    case kEeSifCmdResetCmd:    return "SIF_CMD_RESET_CMD";
    case kEeSifCmdRpcEnd:      return "SIF_CMD_RPC_END";
    case kEeSifCmdRpcBind:     return "SIF_CMD_RPC_BIND";
    case kEeSifCmdRpcCall:     return "SIF_CMD_RPC_CALL";
    case kEeSifCmdRpcRdata:    return "SIF_CMD_RPC_RDATA";
    }
    return "SIF_CMD_UNKNOWN";
}

// EE-side handling of SIF_CMD_RPC_END, the reply an IOP RPC server sends when
// a bind or call completes (PS2SDK `ee/kernel/src/sifrpc.c` `_request_end`).
//
// It writes the reply's server/buffer pointers into the guest client object for
// a bind, raises the client semaphore the caller is waiting on, and frees the
// request packet the caller allocated. The service that completes is the
// kernel's, so the semaphore raise here is the kernel's iSignalSema with the
// same raise rules; nothing about the reply is invented, it is whatever the
// IOP-side model put in the packet.
[[nodiscard]] inline Status process_ee_sif_rpc_end(
    EeState& state,
    std::uint32_t packet_addr
) noexcept {
    std::uint32_t cd{}, end_cid{};
    if (load_u32(state.memory, packet_addr + kEeSifRpcPktCdOffset, cd) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifRpcPktCidOffset, end_cid) != Status::ok) {
        return Status::ok;
    }
    if (cd == 0U) {
        return Status::ok; // no client object to complete
    }

    if (end_cid == kEeSifCmdRpcBind) {
        std::uint32_t sd{}, buf{}, cbuf{};
        if (load_u32(state.memory, packet_addr + kEeSifRpcPktSdOffset, sd) == Status::ok
            && load_u32(state.memory, packet_addr + kEeSifRpcPktBufOffset, buf) == Status::ok
            && load_u32(state.memory, packet_addr + kEeSifRpcPktCbufOffset, cbuf) == Status::ok
            && store_u32(state.memory, cd + kEeSifRpcClientServerOffset, sd) == Status::ok
            && store_u32(state.memory, cd + kEeSifRpcClientBufOffset, buf) == Status::ok
            && store_u32(state.memory, cd + kEeSifRpcClientCbufOffset, cbuf) == Status::ok) {
            state.hle.sif_cmd.last_rpc_bind_cd = cd;
        }
    }

    std::uint32_t sema_id{};
    if (load_u32(state.memory, cd + kEeSifRpcClientSemaIdOffset, sema_id) == Status::ok) {
        const auto id = static_cast<std::int32_t>(sema_id);
        state.hle.sif_cmd.last_rpc_bind_sema_id = id;
        if (raise_ee_sema_count(find_ee_sema(state.hle, id))) {
            state.hle.sif_cmd.last_rpc_bind_sema_signalled = true;
        }
    }

    // rpc_packet_free: clear the request's rpc_id, drop its alloc flag, then
    // clear the client's packet pointer.
    std::uint32_t client_pkt{};
    if (load_u32(state.memory, cd + kEeSifRpcClientPktAddrOffset, client_pkt) == Status::ok
        && client_pkt != 0U) {
        std::uint32_t rec_id{};
        (void)store_u32(state.memory, client_pkt + kEeSifRpcPktRpcIdOffset, 0U);
        if (load_u32(state.memory, client_pkt + kEeSifRpcPktRecIdOffset, rec_id) == Status::ok) {
            (void)store_u32(
                state.memory, client_pkt + kEeSifRpcPktRecIdOffset,
                rec_id & ~kEeSifRpcPacketAllocFlag);
        }
        (void)store_u32(state.memory, cd + kEeSifRpcClientPktAddrOffset, 0U);
    }
    return Status::ok;
}

// EE-side SIFCMD receive dispatch: the model of the EE kernel's
// `_SifCmdIntHandler` and its system handlers (PS2SDK `ee/kernel/src/sifcmd.c`).
//
// It reads a command packet that a modelled IOP->EE SIF0 transfer has already
// written into EE-visible memory, then runs the system handler for its command
// id. Only `SIF_CMD_SET_SREG` is implemented, because it is the only inbound
// system command the observed boot path needs. Its handler writes
// `cmd_data->sregs[index] = value`, where `cmd_data` is the guest-published
// `SIF_SYSREG_MAINADDR` and `sregs` sits at `kEeSifCmdDataSregsOffset`. That is
// exactly the retail handler at 0x0025C0D8. An unknown command id, an
// unpublished or invalid `cmd_data`, or an out-of-range index writes nothing.
[[nodiscard]] inline Status process_ee_sif_command(
    EeState& state,
    std::uint32_t packet_addr
) noexcept {
    auto& sif = state.hle.sif_cmd;

    std::uint32_t w0{}, w2{};
    if (load_u32(state.memory, packet_addr, w0) != Status::ok
        || load_u32(state.memory, packet_addr + 8U, w2) != Status::ok) {
        return Status::ok;
    }
    const std::uint32_t psize = w0 & 0xFFU;
    const std::uint32_t cid = w2;
    ++sif.inbound_packet_count;
    sif.last_inbound_cid = cid;
    sif.last_inbound_size = psize;

    if ((cid & kEeSifCmdIdSystem) == 0U) {
        return Status::ok; // only system commands are modelled
    }
    if ((cid & ~kEeSifCmdIdSystem) == 8U) { // SIF_CMD_RPC_END
        return process_ee_sif_rpc_end(state, packet_addr);
    }
    if ((cid & ~kEeSifCmdIdSystem) != 1U) { // not SIF_CMD_SET_SREG
        return Status::ok;
    }

    std::uint32_t index{}, value{};
    if (psize < kEeSifCmdHeaderSize + 8U
        || load_u32(state.memory, packet_addr + kEeSifCmdHeaderSize, index) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifCmdHeaderSize + 4U, value) != Status::ok) {
        return Status::ok;
    }

    // Locate the guest's software-register array through the published cmd_data.
    std::uint32_t main_addr{};
    if (std::uint32_t* const slot =
            find_sif_register(state.hle, kEeSifRegisterSystemBase + 1U);
        slot != nullptr) {
        main_addr = *slot;
    }
    if (main_addr == 0U) {
        return Status::ok; // cmd_data unpublished: fail closed, write nothing
    }
    std::uint32_t sregs{};
    if (load_u32(state.memory, main_addr + kEeSifCmdDataSregsOffset, sregs) != Status::ok
        || sregs == 0U) {
        return Status::ok;
    }
    sif.ee_sreg_base = sregs;
    if (index >= sif.software_registers.size()) {
        return Status::ok; // bounded bank: out-of-range index is refused
    }
    if (store_u32(state.memory, sregs + index * 4U, value) != Status::ok) {
        return Status::ok;
    }
    if (index == kEeSifSregRpcInit) {
        std::uint32_t observed{};
        if (load_u32(state.memory, sregs + kEeSifSregRpcInit * 4U, observed) == Status::ok) {
            sif.ee_sreg_rpcinit = observed;
        }
    }
    return Status::ok;
}

// Deliver the IOP's documented RPC-init reply to the EE over SIF0.
//
// The resident IOP SIFCMD/SIFRPC performs RPC initialisation after the EE's
// `SIF_CMD_INIT_CMD` handshake: PS2SDK `iop/system/sifrpc/src/sifrpc.c`
// `sceSifInitRpc` builds `SifCmdSRegData_t{ index = SIF_SREG_RPCINIT; value = 1 }`
// and `sceSifSendCmd(SIF_CMD_SET_SREG, ...)` to the EE receive buffer the EE
// published with INIT_CMD (recorded as `sif.ee_pktbuf`).
//
// This models that transfer: the 24-byte packet is written into the EE-visible
// receive buffer, the SIF0 (channel 5) completion event is published to D_STAT,
// and the EE-side receive dispatch applies the command. It invents no IOP
// address (the buffer is the guest's own) and no register value beyond the
// documented (index 0, value 1) reply. It runs at most once per boot, and only
// when the resident IOP is modelled.
[[nodiscard]] inline Status iop_sifcmd_rpc_init_reply(EeState& state) noexcept {
    auto& sif = state.hle.sif_cmd;
    if (sif.residency != EeIopSifResidency::resident || sif.rpc_init_sent) {
        return Status::ok;
    }
    if (sif.ee_pktbuf == 0U) {
        return Status::ok; // no published EE receive buffer: nothing to address
    }

    // SifCmdSRegData_t: 16-byte header + index(u32) + value(u32) = 24 bytes.
    const std::uint32_t packet[6] = {
        24U,                 // psize (8 bits) | dsize (24 bits)
        0U,                  // dest
        kEeSifCmdSetSreg,    // cid 0x80000001
        0U,                  // opt
        kEeSifSregRpcInit,   // index 0
        1U,                  // value 1
    };
    for (std::uint32_t i = 0; i < 6U; ++i) {
        if (store_u32(state.memory, sif.ee_pktbuf + i * 4U, packet[i]) != Status::ok) {
            return Status::ok; // buffer not writable: no transfer, no dispatch
        }
    }
    sif.rpc_init_sent = true;
    sif.ee_sif0_event_published = state.memory.dmac.publish_sif0_event();
    return process_ee_sif_command(state, sif.ee_pktbuf);
}

// ---- Modelled IOP SIFRPC server table -------------------------------------
//
// PS2SDK `ee/kernel/src/sifrpc.c` `search_svdata` walks the active queues in
// order and, inside a queue, the linked server list in registration order. The
// bounded table preserves that order: the lowest queue number wins, and within
// one queue the earliest registration wins. Registration is refused for sid 0,
// a null handler, a duplicate (sid, queue), or a full table; the caller must
// not assume a refused registration exists.

[[nodiscard]] inline bool register_ee_sif_rpc_server(
    EeState& state,
    std::uint32_t sid,
    std::uint32_t queue,
    EeSifRpcServerHandler handler
) noexcept {
    auto& sif = state.hle.sif_cmd;
    if (sid == 0U || handler == nullptr || sif.server_count >= sif.servers.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < sif.server_count; ++index) {
        if (sif.servers[index].sid == sid && sif.servers[index].queue == queue) {
            return false;
        }
    }
    sif.servers[sif.server_count++] = EeSifRpcServer{sid, queue, handler};
    return true;
}

[[nodiscard]] inline const EeSifRpcServer* find_ee_sif_rpc_server(
    const EeState& state,
    std::uint32_t sid
) noexcept {
    const auto& sif = state.hle.sif_cmd;
    const EeSifRpcServer* best = nullptr;
    for (std::size_t index = 0U; index < sif.server_count; ++index) {
        const EeSifRpcServer& server = sif.servers[index];
        if (server.sid == sid && (best == nullptr || server.queue < best->queue)) {
            best = &server;
        }
    }
    return best;
}

// Server ids from PS2SDK `ee/rpc/cdvd/src/libcdvd.c` and `scmd.c`/`ncmd.c`,
// cross-checked against ps2tek's CDVDFSV RPC list.
inline constexpr std::uint32_t kEeSifServerCdInit = 0x8000'0592U;
inline constexpr std::uint32_t kEeSifServerCdScm = 0x8000'0593U;
// DiskReady servers from PS2SDK `ee/rpc/cdvd/src/libcdvd.c`
// (CD_SERVER_DISKREADY 0x8000059A) and the OSD path that binds 0x8000059C;
// both are served by cdvdfsv's cbrpc_rpc2_diskready with function 0.
inline constexpr std::uint32_t kEeSifServerCdDiskReady = 0x8000'059AU;
inline constexpr std::uint32_t kEeSifServerCdDiskReadyAlt = 0x8000'059CU;
// PS2SDK `ee/rpc/cdvd/src/libcdvd.c`: CD_SERVER_NCMD / CD_SERVER_SEARCHFILE.
inline constexpr std::uint32_t kEeSifServerCdNCmd = 0x8000'0595U;
inline constexpr std::uint32_t kEeSifServerCdSearchFile = 0x8000'0597U;

// CdInit (sid 0x80000592, function 0).
//
// `cdvdfsv_rpc1_inpacket_t` is one s32 init mode. The reply
// `cdvdfsv_rpc1_outpacket_t` is {retres, cdvdfsv_ver, cdvdman_ver, debug_mode}.
// `retres` is 1 because the HLE CDVD driver accepted initialization; it implies
// nothing about a disc being present. The two module versions are 0, the SDK's
// own "not reported" value, because REPS2 has no CDVDFSV/CDVDMAN build to name.
// DiskReady and SearchFile are different servers and stay fail-closed until
// they are modelled.
[[nodiscard]] inline bool serve_ee_cdvd_init_call(
    EeState& state,
    const EeSifRpcCall& call
) noexcept {
    if (call.rpc_number != 0U) {
        return false; // cdvdfsv's callback serves only function 0
    }
    std::uint32_t mode{};
    if (call.send_size < 4U
        || load_u32(state.memory, call.send_addr, mode) != Status::ok) {
        return false; // the request payload was not transferred
    }
    if (call.recv_addr == 0U || call.recv_size < 16U) {
        return false; // no reply buffer of the documented size
    }
    state.hle.sif_cmd.last_rpc_call_request_word = mode;
    const std::uint32_t reply[4] = {1U, 0U, 0U, 0U};
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        if (store_u32(state.memory, call.recv_addr + index * 4U, reply[index]) != Status::ok) {
            return false;
        }
    }
    return true;
}

// CdSCmd (sid 0x80000593): the S-command RPC server. PS2SDK
// `ee/rpc/cdvd/src/scmd.c` binds `CD_SERVER_SCMD 0x80000593` and issues every
// S-command as `sceSifCallRpc(&clientSCmd, <scmd code>, ...)`, so the RPC
// function number IS the S-command number (0x0C STATUS, 0x22 MMODE, ...) and
// the reply packet starts with the result word `m_retres`. ps2tek lists the
// same id as "CDVD S commands (CDVDFSV)".
//
// Only CD_SCMD_MMODE (0x22, "set media mode") is modelled. The title's first
// observed S-command is exactly this one (PostPR72: rpc_call_number 0x22 with
// request word 2 = SCECdMmodeDvd). PS2SDK `ee/rpc/cdvd/src/scmd.c sceCdMmode`
// sends one s32 media word and reads a 4-byte reply; the IOP side
// (`iop/cdvd/cdvdman/src/cdvdman.c`) stores it (`g_cdvdman_mmode = media`) and
// returns 1, and `iop/cdvd/cdvdfsv/src/cdvdfsv.c cdvdfsv_rpc3_22_mmode` copies
// that into `outbuf->m_retres`. The HLE driver therefore records the requested
// mode and answers `m_retres = 1` — the same observable effect, with no disc
// presence implied (sceCdMmode is a mode switch, not a media query).
//
// Every other S-command is still refused: the IOP sends no RPC_END and the EE
// stays blocked exactly as it would against a real server that does not answer —
// nothing is faked. A malformed MMODE request (not a 4-byte payload, or no
// readable request word / writable 4-byte reply) is likewise refused. See ADR 0122.
inline constexpr std::uint32_t kEeCdvdScmdMmode = 0x22U;

[[nodiscard]] inline bool serve_ee_cdvd_scmd_call(
    EeState& state,
    const EeSifRpcCall& call
) noexcept {
    auto& sif = state.hle.sif_cmd;
    ++sif.scmd_call_count;
    sif.last_scmd_number = call.rpc_number;
    sif.last_scmd_replied = false;

    if (call.rpc_number != kEeCdvdScmdMmode) {
        return false; // unmodelled S-command: no RPC_END, caller stays blocked
    }
    if (call.send_size != 4U || call.recv_addr == 0U || call.recv_size < 4U) {
        return false; // not the documented one-word request + 4-byte reply
    }
    std::uint32_t media{};
    if (load_u32(state.memory, call.send_addr, media) != Status::ok) {
        return false; // the request word was not transferred
    }
    if (store_u32(state.memory, call.recv_addr, 1U) != Status::ok) {
        return false; // reply buffer not writable: no reply, no RPC_END
    }
    sif.cdvd_mmode = media; // mirrors cdvdman's g_cdvdman_mmode
    sif.last_scmd_media = media;
    sif.last_scmd_retres = 1U;
    sif.last_scmd_replied = true;
    ++sif.scmd_replies;
    return true;
}

// DiskReady RPC server (sids 0x8000059A/0x8000059C), function 0.
//
// PS2SDK `ee/rpc/cdvd/src/libcdvd.c sceCdDiskReady(mode)` sends a 4-byte
// `m_mode` and reads a 4-byte `m_retres`. The IOP cdvdfsv handler
// (cbrpc_rpc2_diskready) calls `sceCdDiskReady(mode)`, which for a ready disc
// returns SCECdComplete (2) and for no disc SCECdNotReady (6); mode 8 returns
// the raw status register byte, whose ready pattern is (x & 0xC0) == 0x40
// (cdvdman_devready). REPS2 has no physical drive: the truthful answer is
// derived from host media presence — present answers 2 (0x40 for mode 8),
// absent answers 6 (0x00 for mode 8), matching a real drive in both cases.
// mode 0 would block the IOP until ready; the HLE never hangs the guest and
// answers immediately. ADR 0124.
[[nodiscard]] inline bool serve_ee_cdvd_diskready_call(
    EeState& state,
    const EeSifRpcCall& call
) noexcept {
    auto& sif = state.hle.sif_cmd;
    ++sif.diskready_call_count;
    sif.last_diskready_replied = false;

    if (call.rpc_number != 0U) {
        return false; // unmodelled function: no RPC_END, caller stays blocked
    }
    if (call.send_size != 4U || call.recv_addr == 0U || call.recv_size < 4U) {
        return false; // not the documented one-word request + 4-byte reply
    }
    std::uint32_t mode{};
    if (load_u32(state.memory, call.send_addr, mode) != Status::ok) {
        return false; // the request word was not transferred
    }
    const bool disc_ready = state.media.present();
    std::uint32_t retres{};
    if (mode == 8U) {
        retres = disc_ready ? 0x40U : 0x00U; // raw status byte, ready = 0x40 pattern
    } else {
        retres = disc_ready ? 2U : 6U; // SCECdComplete / SCECdNotReady
    }
    if (store_u32(state.memory, call.recv_addr, retres) != Status::ok) {
        return false; // reply buffer not writable: no reply, no RPC_END
    }
    sif.last_diskready_mode = mode;
    sif.last_diskready_retres = retres;
    sif.last_diskready_replied = true;
    ++sif.diskready_replies;
    return true;
}

// NCmd server (0x80000595).
//
// Function 14 (CD_NCMD_DISKREADY, PS2SDK `ee/rpc/cdvd/src/ncmd.c
// sceCdNCmdDiskReady) sends no request bytes and reads a 4-byte `m_retres`;
// the IOP answers 2 (SCECdComplete) for a ready disc and 6 (SCECdNotReady)
// otherwise. The HLE answers from host media presence with the same values.
// Every other NCMD function is recorded and refused (no RPC_END, caller
// stays blocked) until modelled. ADR 0125.
[[nodiscard]] inline bool serve_ee_cdvd_ncmd_call(
    EeState& state,
    const EeSifRpcCall& call
) noexcept {
    auto& sif = state.hle.sif_cmd;
    ++sif.ncmd_call_count;
    sif.last_ncmd_number = call.rpc_number;

    constexpr std::uint32_t kCdNcmdDiskReady = 14U;
    if (call.rpc_number == kCdNcmdDiskReady) {
        if (call.send_size != 0U || call.recv_addr == 0U || call.recv_size < 4U) {
            return false; // not the documented empty-request + 4-byte reply
        }
        const std::uint32_t retres = state.media.present() ? 2U : 6U;
        if (store_u32(state.memory, call.recv_addr, retres) != Status::ok) {
            return false;
        }
        ++sif.ncmd_replies;
        sif.last_ncmd_replied = true;
        sif.last_ncmd_retres = retres;
        return true;
    }
    return false;
}

// SearchFile server (0x80000597). Same fail-closed registry: the ISO9660
// path-table walker it needs is not implemented, so calls are recorded and
// refused. ADR 0125.
[[nodiscard]] inline bool serve_ee_cdvd_searchfile_call(
    EeState& state,
    const EeSifRpcCall& call
) noexcept {
    auto& sif = state.hle.sif_cmd;
    ++sif.searchfile_call_count;
    sif.last_searchfile_number = call.rpc_number;
    return false;
}

// Answer an EE RPC bind the way the resident IOP SIFRPC module does.
//
// PS2SDK `iop/system/sifcmd/src/sifrpc.c` `_request_bind` looks the sid up with
// `_search_svdata` and always sends SIF_CMD_RPC_END back, carrying the found
// `sd`/`buf` or NULL for both when no server is registered. REPS2 runs no IOP
// module, so no `sceSifRegisterRpc` has populated an active queue and the
// no-server reply is the truthful answer, not a fabricated success: the EE side
// then reports a failed bind exactly as it would from a real IOP without that
// module. A stub IOP server that claimed success without the corresponding
// module behaviour would be fabrication and is deliberately not modelled.
//
// The reply is delivered into the EE receive buffer the guest published with
// SIF_CMD_INIT_CMD. REPS2 does not model IOP memory, so the packet is written
// straight into that buffer instead of through an IOP-side transfer; the SIF0
// completion event is published so the device state matches an inbound
// transfer. A missing or unwritable receive buffer delivers nothing.
[[nodiscard]] inline Status deliver_iop_rpc_bind_reply(
    EeState& state,
    std::uint32_t bind_packet_addr
) noexcept {
    auto& sif = state.hle.sif_cmd;
    if (sif.ee_pktbuf == 0U) {
        return Status::ok; // no published EE receive buffer: nothing to address
    }

    std::uint32_t pkt_addr{}, rpc_id{}, cd{}, sid{};
    if (load_u32(state.memory, bind_packet_addr + kEeSifRpcPktAddrOffset, pkt_addr) != Status::ok
        || load_u32(state.memory, bind_packet_addr + kEeSifRpcPktRpcIdOffset, rpc_id) != Status::ok
        || load_u32(state.memory, bind_packet_addr + kEeSifRpcPktCdOffset, cd) != Status::ok
        || load_u32(state.memory, bind_packet_addr + kEeSifRpcBindSidOffset, sid) != Status::ok) {
        return Status::ok; // unreadable request: nothing to answer
    }

    // The IOP sends a full 64-byte slot (sizeof(sif_rpc_tbl_data_t)); only the
    // 48-byte SifRpcRendPkt_t prefix is modelled, so the trailing slot bytes are
    // left untouched rather than invented.
    //
    // A registered server answers with its own sid as the `sd` token. The EE
    // never dereferences that value: every observed bind site only zero-tests
    // `cd->server`, and `sceSifCallRpc` copies it verbatim into the call packet.
    // `buf`/`cbuf` name IOP-side server buffers, which REPS2 does not model, so
    // they stay zero. A sid with no registered server keeps the no-server reply.
    const EeSifRpcServer* const server = find_ee_sif_rpc_server(state, sid);
    const std::uint32_t sd = server != nullptr ? server->sid : 0U;
    const std::uint32_t reply[kEeSifRpcRendPacketSize / 4U] = {
        kEeSifRpcRendPacketSize, // psize
        0U,                      // dest
        kEeSifCmdRpcEnd,         // cid
        0U,                      // opt
        0U,                      // rec_id
        pkt_addr,                // pkt_addr: the caller's request packet
        rpc_id,
        cd,
        kEeSifCmdRpcBind,        // cid: the request this ends
        sd,                      // sd: the registered server's token, or NULL
        0U,                      // buf
        0U,                      // cbuf
    };
    for (std::uint32_t i = 0; i < kEeSifRpcRendPacketSize / 4U; ++i) {
        if (store_u32(state.memory, sif.ee_pktbuf + i * 4U, reply[i]) != Status::ok) {
            return Status::ok; // buffer not writable: no transfer, no dispatch
        }
    }
    ++sif.rpc_bind_replies;
    sif.last_rpc_bind_sid = sid;
    sif.last_rpc_bind_server_sid = sd;
    sif.last_rpc_bind_had_server = server != nullptr;
    sif.last_rpc_bind_sema_id = -1;
    sif.last_rpc_bind_sema_signalled = false;
    sif.ee_sif0_event_published = state.memory.dmac.publish_sif0_event();
    return process_ee_sif_command(state, sif.ee_pktbuf);
}

// Serve an RPC call as the modelled IOP SIFRPC server table would.
//
// PS2SDK `ee/kernel/src/sifrpc.c` `sceSifCallRpc` writes the function number,
// sizes, reply buffer and server token into the call packet and sends the
// request payload as the DMA entry that precedes the command packet. The IOP
// runs the server function, sends the reply payload to the caller's `recvbuf`,
// then sends SIF_CMD_RPC_END with `cid = SIF_CMD_RPC_CALL`, which makes the EE's
// `_request_end` take the call branch and signal the caller's semaphore.
//
// REPS2 has no IOP memory: the handler writes its reply payload straight into
// the caller's EE buffer, and the reply packet is written into the published EE
// receive buffer with the SIF0 completion event published. An unregistered
// token, a handler that refuses the function number, a zero `rmode`, or an
// unreachable receive buffer produces no reply at all, so the caller stays
// blocked instead of receiving an invented status.
[[nodiscard]] inline Status deliver_iop_rpc_call_reply(
    EeState& state,
    std::uint32_t packet_addr,
    const SifDmaTransfer* payload
) noexcept {
    auto& sif = state.hle.sif_cmd;
    if (sif.ee_pktbuf == 0U) {
        return Status::ok; // no published EE receive buffer: nothing to address
    }

    std::uint32_t pkt_addr{}, rpc_id{}, cd{}, rpc_number{};
    std::uint32_t send_size{}, recvbuf{}, recv_size{}, rmode{}, sd{};
    if (load_u32(state.memory, packet_addr + kEeSifRpcPktAddrOffset, pkt_addr) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifRpcPktRpcIdOffset, rpc_id) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifRpcPktCdOffset, cd) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifRpcCallNumberOffset, rpc_number) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifRpcCallSendSizeOffset, send_size) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifRpcCallRecvBufOffset, recvbuf) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifRpcCallRecvSizeOffset, recv_size) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifRpcCallRmodeOffset, rmode) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifRpcCallSdOffset, sd) != Status::ok) {
        return Status::ok; // unreadable request: nothing to answer
    }

    ++sif.rpc_call_count;
    sif.last_rpc_call_number = rpc_number;
    sif.last_rpc_call_send_size = send_size;
    sif.last_rpc_call_recv_size = recv_size;
    sif.last_rpc_call_replied = false;
    sif.last_rpc_call_server_sid = 0U;

    const EeSifRpcServer* const server = find_ee_sif_rpc_server(state, sd);
    if (server == nullptr) {
        ++sif.rpc_calls_unmatched;
        return Status::ok; // no server for this token: no reply
    }
    sif.last_rpc_call_server_sid = server->sid;

    std::uint32_t end_function{};
    (void)load_u32(state.memory, cd + kEeSifRpcClientEndFunctionOffset, end_function);
    sif.last_rpc_call_end_function = end_function;

    const EeSifRpcCall call{
        cd,
        rpc_number,
        payload != nullptr ? payload->src : 0U,
        payload != nullptr ? static_cast<std::uint32_t>(payload->size) : 0U,
        recvbuf,
        recv_size,
        pkt_addr,
    };
    // Record the first payload word of any call, whatever the server does with
    // it: for CdInit it is the init mode, for the S-command server the first
    // parameter word. A payload that cannot be read records nothing.
    if (call.send_size >= 4U) {
        std::uint32_t request_word{};
        if (load_u32(state.memory, call.send_addr, request_word) == Status::ok) {
            sif.last_rpc_call_request_word = request_word;
        }
    }
    if (!server->handler(state, call)) {
        return Status::ok; // the server refused: the IOP sends no RPC_END
    }
    if (rmode == 0U) {
        return Status::ok; // NOWAIT without an end function: no reply packet
    }

    const std::uint32_t reply[kEeSifRpcRendPacketSize / 4U] = {
        kEeSifRpcRendPacketSize, // psize
        0U,                      // dest
        kEeSifCmdRpcEnd,         // cid
        0U,                      // opt
        0U,                      // rec_id
        pkt_addr,                // pkt_addr: the caller's request packet
        rpc_id,
        cd,
        kEeSifCmdRpcCall,        // cid: the request this ends
        server->sid,             // sd: the opaque server token
        0U,                      // buf: IOP payload buffer, not modelled
        0U,                      // cbuf: IOP callback buffer, not modelled
    };
    for (std::uint32_t i = 0; i < kEeSifRpcRendPacketSize / 4U; ++i) {
        if (store_u32(state.memory, sif.ee_pktbuf + i * 4U, reply[i]) != Status::ok) {
            return Status::ok; // buffer not writable: no transfer, no dispatch
        }
    }
    ++sif.rpc_call_replies;
    sif.last_rpc_call_replied = true;
    sif.ee_sif0_event_published = state.memory.dmac.publish_sif0_event();
    return process_ee_sif_command(state, sif.ee_pktbuf);
}

// The EE's SIF_CMD_RESET_CMD (0x80000003) asks the IOP to reboot.
//
// PS2SDK `SifIopReset` (`ee/kernel/src/iopcontrol.c`) stops SIF0, sends this
// packet with the image argument and mode, then writes SMFLAG BOOTEND/SIFINIT/
// CMDINIT and clears RPCINIT/SUBADDR. The observed title follows that path and
// then polls SMFLAG for `SIF_STAT_BOOTEND` at `0x0025ED10` (`SifIopSync`).
//
// REPS2 cannot execute the requested IOP image: there is no IOP CPU model and a
// second execution engine is out of scope. The modelled IOP therefore restarts
// into its resident HLE configuration: the per-boot RPCINIT handshake is
// re-armed (so the EE's next `sceSifInitRpc` gets its documented SET_SREG reply)
// and the boot completion is recorded, to be published on the next SMFLAG
// observation. Servers installed by the boot environment survive because they
// *are* the resident configuration; any service the requested image would have
// provided but REPS2 does not model stays absent, so a later bind for it still
// fails closed. The image itself is recorded as requested, never as executed.
[[nodiscard]] inline Status accept_iop_reset(
    EeState& state,
    std::uint32_t packet_addr,
    std::uint32_t psize
) noexcept {
    auto& sif = state.hle.sif_cmd;
    std::uint32_t arg_len{}, mode{};
    if (psize < kEeSifCmdHeaderSize + 8U
        || load_u32(state.memory, packet_addr + kEeSifCmdHeaderSize, arg_len) != Status::ok
        || load_u32(state.memory, packet_addr + kEeSifCmdHeaderSize + 4U, mode) != Status::ok) {
        return Status::ok; // malformed reset packet: change nothing
    }
    ++sif.iop_reset_count;
    sif.last_reset_arg_len = arg_len > 80U ? 80U : arg_len;
    sif.last_reset_mode = mode;
    sif.rpc_init_sent = false;
    sif.reset_pending_boot = true;
    return Status::ok;
}

// Process a SIF command packet as the IOP-side SIFCMD would.
//
// This is called from hle_sif_set_dma when the last transfer entry has
// SIF_DMA_ERT set (indicating a command packet). The packet is read from
// guest memory, its header parsed, and the appropriate IOP-side handler
// runs. INIT_CMD publishes MSFLAG SIF_STAT_CMDINIT. The EE's boot wait loop
// polls the IOP-written SMFLAG bit instead; that is published by the modelled
// IOP restart completion in hle_sif_get_reg after a SIF_CMD_RESET_CMD (ADR
// 0118), not by INIT_CMD.
[[nodiscard]] inline Status process_sif_command(
    EeState& state,
    std::uint32_t packet_addr,
    const SifDmaTransfer* payload
) noexcept {
    auto& sif = state.hle.sif_cmd;

    // Read the SifCmdHeader from guest memory.
    SifCmdHeader header{};
    std::uint32_t w0{}, w1{}, w2{}, w3{};
    if (load_u32(state.memory, packet_addr, w0) != Status::ok
        || load_u32(state.memory, packet_addr + 4U, w1) != Status::ok
        || load_u32(state.memory, packet_addr + 8U, w2) != Status::ok
        || load_u32(state.memory, packet_addr + 12U, w3) != Status::ok) {
        return Status::ok;
    }
    header.psize_dsize = w0;
    header.dest = w1;
    header.cid = static_cast<std::int32_t>(w2);
    header.opt = w3;

    const std::uint32_t psize = header.psize_dsize & 0xFFU;
    ++sif.packet_count;
    sif.last_cid = static_cast<std::uint32_t>(header.cid);
    sif.last_packet_addr = packet_addr;
    sif.last_packet_size = psize;

    if ((static_cast<std::uint32_t>(header.cid) & kEeSifCmdIdSystem) != 0U) {
        const std::uint32_t id = static_cast<std::uint32_t>(header.cid) & ~kEeSifCmdIdSystem;
        switch (id) {
        case 0: // SIF_CMD_CHANGE_SADDR: payload is the new EE receive buffer.
            if (psize >= kEeSifCmdHeaderSize + 4U) {
                std::uint32_t newaddr{};
                if (load_u32(state.memory, packet_addr + kEeSifCmdHeaderSize, newaddr) == Status::ok) {
                    sif.ee_pktbuf = newaddr;
                }
            }
            break;
        case 1: { // SIF_CMD_SET_SREG: payload is an IOP software-register index/value.
            std::uint32_t index{};
            std::uint32_t value{};
            if (psize >= kEeSifCmdHeaderSize + 8U
                && load_u32(state.memory, packet_addr + kEeSifCmdHeaderSize, index) == Status::ok
                && load_u32(state.memory, packet_addr + kEeSifCmdHeaderSize + 4U, value) == Status::ok) {
                sif.last_set_sreg_index = index;
                sif.last_set_sreg_value = value;
                // PS2SDK has 32 software registers; the SDK handler does not
                // bounds-check, so an out-of-range index is recorded but not
                // written into the bounded host bank.
                if (index < sif.software_registers.size()) {
                    sif.software_registers[index] = value;
                }
            }
            break;
        }
        case 2: { // SIF_CMD_INIT_CMD: install the EE receive buffer on the IOP.
            if (psize >= kEeSifCmdHeaderSize + 4U) {
                std::uint32_t ee_pktbuf{};
                if (load_u32(state.memory, packet_addr + kEeSifCmdHeaderSize, ee_pktbuf) == Status::ok) {
                    sif.last_ee_pktbuf = ee_pktbuf;
                    sif.last_init_opt = header.opt;
                    if (header.opt == 0U) {
                        sif.ee_pktbuf = ee_pktbuf;
                        sif.initialized = true;
                        sif.msflag |= kEeSifStatCmdInit;
                        // The resident IOP's RPC initialisation follows this
                        // CMDINIT handshake and replies SIF_SREG_RPCINIT=1 to
                        // the EE receive buffer just published.
                        (void)iop_sifcmd_rpc_init_reply(state);
                    }
                }
            }
            break;
        }
        case 3: // SIF_CMD_RESET_CMD: the EE asked the IOP to reboot.
            (void)accept_iop_reset(state, packet_addr, psize);
            break;
        case 8: // SIF_CMD_RPC_END is IOP->EE; an EE-side copy is not a request.
            break;
        case 9: // SIF_CMD_RPC_BIND: reply as the resident IOP RPC module does.
            (void)deliver_iop_rpc_bind_reply(state, packet_addr);
            break;
        case 10: // SIF_CMD_RPC_CALL: run the registered server, if any.
            (void)deliver_iop_rpc_call_reply(state, packet_addr, payload);
            break;
        default:
            break;
        }
    }

    // Reflect only state changes made by this SIFCMD transition.
    if (std::uint32_t* slot = find_sif_register(state.hle, kEeSifRegisterSmFlag); slot != nullptr) {
        *slot = sif.smflag;
    }
    if (std::uint32_t* slot = find_sif_register(state.hle, kEeSifRegisterMsFlag); slot != nullptr) {
        *slot = sif.msflag;
    }
    return Status::ok;
}
// Establish the device state the HLE boot environment presents to the title.
//
// The real EE kernel runs before the title and leaves the DMAC and INTC in a
// defined idle state; REPS2 replaces that kernel with HLE, so it must
// reproduce the state the title depends on. This seeds only documented idle
// state (all modelled DMAC channels stopped, no pending status, no interrupt
// masks) and does not invent a hardware reset value. It is applied explicitly
// by a host that opts into the HLE layer; without it every DMAC read still
// fails closed. See ADR 0086.
//
// The same reasoning applies to the IOP: the replaced kernel loads the IOP
// replacement image, so the booted machine has a resident `sifcmd`. The boot
// environment therefore runs that module's startup transaction rather than
// leaving the SIF flags at an unreachable zero. See ADR 0099.
//
// Finally it installs the SIFRPC servers this runtime actually implements, in
// the same table `_search_svdata` would walk. A sid without an entry still gets
// the documented no-server bind reply, so a title never sees a server that is
// not there. See ADR 0114.
inline void initialize_hle_boot_environment(EeState& state) noexcept {
    state.memory.dmac.seed_idle_boot_state();
    state.memory.intc.seed_idle_boot_state();
    state.hle.sif_cmd = EeSifCmdState{};
    iop_sifcmd_start(state);
    (void)register_ee_sif_rpc_server(
        state, kEeSifServerCdInit, 0U, &serve_ee_cdvd_init_call);
    // The S-command server is registered so the title's bind succeeds; its
    // handler refuses every S-command until one is modelled (ADR 0121).
    (void)register_ee_sif_rpc_server(
        state, kEeSifServerCdScm, 0U, &serve_ee_cdvd_scmd_call);
    // DiskReady servers (ADR 0124): both sids share cbrpc_rpc2_diskready on
    // the IOP, so both are served by the host-media-presence handler.
    (void)register_ee_sif_rpc_server(
        state, kEeSifServerCdDiskReady, 0U, &serve_ee_cdvd_diskready_call);
    (void)register_ee_sif_rpc_server(
        state, kEeSifServerCdDiskReadyAlt, 0U, &serve_ee_cdvd_diskready_call);
    // Registered so the title's binds succeed; handlers refuse (and record)
    // every call until a function is modelled. ADR 0125.
    (void)register_ee_sif_rpc_server(
        state, kEeSifServerCdNCmd, 0U, &serve_ee_cdvd_ncmd_call);
    (void)register_ee_sif_rpc_server(
        state, kEeSifServerCdSearchFile, 0U, &serve_ee_cdvd_searchfile_call);
}

// outside the kernel window are returned unchanged. Mirrors the translation
// performed by resolve_ee_main_ram_address for direct-mapped kernel addresses.
[[nodiscard]] inline std::uint32_t normalize_ee_kernel_alias(
    std::uint32_t address
) noexcept {
    if (address >= 0x80000000U && address < 0xC0000000U) {
        return address & 0x1FFFFFFFU;
    }
    return address;
}

// FindAddress (0x83): FindAddress(table_start, table_end, target).
//
// Scans the guest word table [table_start, table_end) for a word equal to
// `target` (or its KSEG alias) and returns the guest address of the first
// matching word entry, or 0 when none matches. The service reads guest memory
// only and writes nothing; an unreadable entry ends the scan.
//
// This is an honest, bounded table scan: it returns 0 ("not found") for an
// empty, inverted, or unpopulated window rather than inventing a match.
// SetSyscall (0x74) publishes handler pointers into the guest-visible table at
// kEeSyscallTableBase, so a window covering that table reports the slots the
// guest registered. The observed Downhill Domination and Black call sites pass
// a kernel window (0x80000000..0x80080000) and a code address as the target.
[[nodiscard]] inline Status hle_find_address(EeState& state) noexcept {
    const std::uint32_t table_start = ee_syscall_argument(state, 0);
    const std::uint32_t table_end = ee_syscall_argument(state, 1);
    const std::uint32_t target = ee_syscall_argument(state, 2);
    const std::uint32_t target_normalized = normalize_ee_kernel_alias(target);

    state.hle.find_address_called = true;
    state.hle.last_find_address_start = table_start;
    state.hle.last_find_address_end = table_end;
    state.hle.last_find_address_target = target;
    state.hle.last_find_address_result = 0;

    // Align the search window to 32-bit word boundaries.
    std::uint32_t start = (table_start + 3U) & ~0x3U;
    std::uint32_t end = table_end & ~0x3U;

    std::uint32_t result = 0;
    for (std::uint32_t addr = start; addr < end; addr += 4U) {
        std::uint32_t entry = 0;
        if (load_u32(state.memory, addr, entry) != Status::ok) {
            break;
        }
        if (entry == target || normalize_ee_kernel_alias(entry) == target_normalized) {
            result = addr;
            break;
        }
    }

    state.hle.last_find_address_result = result;
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(result);
    return Status::ok;
}

// Finish the optional syscall-boundary memory comparison, shared by host-serviced
// calls and guest-handler returns so both report the same observation.
inline void finish_ee_syscall_memory_trace(EeState& state) noexcept {
    if (!state.hle.syscall_memory_trace_enabled
        || state.hle.syscall_memory_trace_size == 0U) {
        return;
    }
    std::uint32_t physical{};
    if (resolve_ee_memory_access(
            state.memory, state.hle.syscall_memory_trace_address,
            state.hle.syscall_memory_trace_size, 1U, physical) == Status::ok) {
        for (std::size_t index = 0; index < state.hle.syscall_memory_trace_size; ++index) {
            const auto value = state.memory.main_ram[static_cast<std::size_t>(physical) + index];
            state.hle.syscall_memory_after[index] = value;
            if (value != state.hle.syscall_memory_before[index]) {
                ++state.hle.syscall_memory_changed_bytes;
            }
        }
    }
}

// Enter a guest routine installed through SetSyscall (0x74) for `slot`.
//
// Evidence: ps2sdk's GetSyscallHandler reads the kernel's syscall table by
// number, SetSyscall writes the same slot, and ps2sdk itself installs a
// kernel-side printf for service 0x75 that its `_print` wrapper reaches through
// an ordinary SYSCALL. The handler therefore receives the caller's $a0..$a3
// unchanged, returns through $ra, and its $v0 is the service result. REPS2 has
// no privilege rings, so the handler runs as translated host code on the
// current guest stack; that boundary is recorded in ADR 0069.
[[nodiscard]] inline Status enter_ee_syscall_handler(
    EeState& state,
    std::uint32_t slot,
    std::uint32_t handler_address,
    std::uint32_t resume_pc
) noexcept {
    if (state.hle.syscall_handler_depth >= kEeMaxSyscallHandlerDepth) {
        return Status::syscall_handler_overflow;
    }
    EeSyscallHandlerFrame& frame =
        state.hle.syscall_handler_frames[state.hle.syscall_handler_depth];
    frame.saved_gpr = state.gpr;
    frame.saved_hi = state.hi;
    frame.saved_lo = state.lo;
    frame.saved_hi1 = state.hi1;
    frame.saved_lo1 = state.lo1;
    frame.saved_fpr = state.fpr;
    frame.saved_acc = state.acc;
    frame.saved_fcsr = state.fcsr;
    frame.saved_sa = state.sa;
    frame.saved_vu0 = state.vu0;
    frame.saved_cop0 = state.cop0;
    frame.handler_address = handler_address;
    frame.resume_pc = resume_pc;
    frame.slot = slot;

    ++state.hle.syscall_handler_depth;
    ++state.hle.guest_handler_dispatches;
    state.gpr[31].low = sign_extend_word(kEeSyscallHandlerReturnPc);
    state.pc = handler_address;
    return Status::ok;
}

[[nodiscard]] inline bool ee_syscall_handler_return_pending(const EeState& state) noexcept {
    return state.hle.syscall_handler_depth != 0U
        && state.pc == kEeSyscallHandlerReturnPc;
}

// Complete the innermost guest-handler invocation: restore the user context
// saved at the SYSCALL and keep the handler's $v0 as the service result.
[[nodiscard]] inline Status return_from_ee_syscall_handler(EeState& state) noexcept {
    if (state.hle.syscall_handler_depth == 0U) {
        return Status::unhandled_syscall;
    }
    const EeSyscallHandlerFrame& frame =
        state.hle.syscall_handler_frames[state.hle.syscall_handler_depth - 1U];
    const Gpr128 result = state.gpr[kEeSyscallResultRegister];
    const std::uint32_t resume_pc = frame.resume_pc;

    state.gpr = frame.saved_gpr;
    state.gpr[0] = {};
    state.gpr[kEeSyscallResultRegister] = result;
    state.hi = frame.saved_hi;
    state.lo = frame.saved_lo;
    state.hi1 = frame.saved_hi1;
    state.lo1 = frame.saved_lo1;
    state.fpr = frame.saved_fpr;
    state.acc = frame.saved_acc;
    state.fcsr = frame.saved_fcsr;
    state.sa = frame.saved_sa;
    state.vu0 = frame.saved_vu0;
    state.cop0 = frame.saved_cop0;

    --state.hle.syscall_handler_depth;
    ++state.hle.guest_handler_returns;
    finish_ee_syscall_memory_trace(state);
    state.hle.last_syscall_v0_after =
        static_cast<std::uint32_t>(state.gpr[kEeSyscallResultRegister].low);
    state.hle.last_syscall_resume_pc = resume_pc;
    state.pc = resume_pc;
    return Status::ok;
}

// Sorted by service number so a lookup can stop as soon as it passes the key.
inline constexpr EeSyscallService kEeSyscallServices[] = {
    {kEeSyscallAddDmacHandler, "AddDmacHandler", &hle_add_dmac_handler},
    {kEeSyscallRemoveDmacHandler, "RemoveDmacHandler", &hle_remove_dmac_handler},
    {kEeSyscallEnableDmac, "EnableDmac", &hle_enable_dmac},
    {kEeSyscallDisableDmac, "DisableDmac", &hle_disable_dmac},
    {kEeSyscallCreateThread, "CreateThread", &hle_create_thread},
    {kEeSyscallStartThread, "StartThread", &hle_start_thread},
    {kEeSyscallChangeThreadPriority, "ChangeThreadPriority", &hle_change_thread_priority},
    {kEeSyscallGetThreadId, "GetThreadId", &hle_get_thread_id},
    {kEeSyscallReferThreadStatus, "ReferThreadStatus", &hle_refer_thread_status},
    {kEeSyscallSetupThread, "SetupThread", &hle_setup_thread},
    {kEeSyscallSetupHeap, "SetupHeap", &hle_setup_heap},
    {kEeSyscallCreateSema, "CreateSema", &hle_create_sema},
    {kEeSyscallDeleteSema, "DeleteSema", &hle_delete_sema},
    {kEeSyscallSignalSema, "SignalSema", &hle_signal_sema},
    {kEeSyscallISignalSema, "iSignalSema", &hle_signal_sema},
    {kEeSyscallWaitSema, "WaitSema", &hle_wait_sema},
    {kEeSyscallPollSema, "PollSema", &hle_poll_sema},
    {kEeSyscallIPollSema, "iPollSema", &hle_poll_sema},
    {kEeSyscallSetOsdConfigParam, "SetOsdConfigParam", &hle_set_osd_config_param},
    {kEeSyscallGetOsdConfigParam, "GetOsdConfigParam", &hle_get_osd_config_param},
    {kEeSyscallGetEntryAddress, "GetEntryAddress", &hle_get_entry_address},
    {kEeSyscallFlushCache, "FlushCache", &hle_flush_cache},
    {kEeSyscallSifStopDma, "SifStopDma", &hle_sif_stop_dma},
    {kEeSyscallSetSyscall, "SetSyscall", &hle_set_syscall},
    {kEeSyscallSifSetDma, "SifSetDma", &hle_sif_set_dma},
    {kEeSyscallSifSetDChain, "SifSetDChain", &hle_sif_set_d_chain},
    {kEeSyscallSifSetReg, "SifSetReg", &hle_sif_set_reg},
    {kEeSyscallSifGetReg, "SifGetReg", &hle_sif_get_reg},
    {kEeSyscallFindAddress, "FindAddress", &hle_find_address},
};
inline constexpr std::size_t kEeSyscallServiceCount =
    sizeof(kEeSyscallServices) / sizeof(kEeSyscallServices[0]);

// Lookup below stops at the first number greater than the target, so the table
// must be strictly ascending. A duplicate or out-of-order entry would silently
// hide every later service; keep this a compile-time property.
[[nodiscard]] constexpr bool ee_syscall_services_are_ascending() noexcept {
    for (std::size_t index = 1; index < kEeSyscallServiceCount; ++index) {
        if (kEeSyscallServices[index - 1U].number
            >= kEeSyscallServices[index].number) {
            return false;
        }
    }
    return true;
}
static_assert(
    ee_syscall_services_are_ascending(),
    "kEeSyscallServices must be strictly ascending by service number");

[[nodiscard]] inline const EeSyscallService* find_ee_syscall_service(
    std::uint32_t number
) noexcept {
    for (std::size_t index = 0; index < kEeSyscallServiceCount; ++index) {
        const EeSyscallService& service = kEeSyscallServices[index];
        if (service.number == number) {
            return &service;
        }
        if (service.number > number) {
            break;
        }
    }
    return nullptr;
}

// Host-side name of a service number, or nullptr when no handler exists. A name
// here means the layer could service the number, not that it ran.
[[nodiscard]] inline const char* ee_syscall_service_name(std::uint32_t number) noexcept {
    const EeSyscallService* const service = find_ee_syscall_service(number);
    return service == nullptr ? nullptr : service->name;
}

// Dispatch a SYSCALL trap at the generated-code boundary.
//
// `resume_pc` is where guest execution continues after a serviced call: the
// instruction after the SYSCALL for an ordinary trap, or the branch target or
// fallthrough for a SYSCALL executed in a branch delay slot, which is why the
// caller passes it rather than deriving it here.
//
// A disabled layer or an unknown number never guesses. With the layer disabled
// the architectural Level-1 exception is raised exactly as before it existed
// (EPC/BD/Cause and vector entry); with the layer enabled an unknown number
// stops at the SYSCALL as Status::unhandled_syscall with no COP0 context, so the
// caller can report the exact missing service instead of a fictitious kernel.
[[nodiscard]] inline Status dispatch_ee_syscall(
    EeState& state,
    std::uint32_t resume_pc,
    bool in_branch_delay = false
) noexcept {
    if (!state.hle.syscall_enabled) {
        return raise_ee_exception(state, ExceptionCode::SystemCall, in_branch_delay);
    }

    const std::uint32_t code =
        static_cast<std::uint32_t>(state.gpr[kEeSyscallNumberRegister].low);
    state.hle.last_syscall_pc = state.pc;
    state.hle.last_syscall_v0_before =
        static_cast<std::uint32_t>(state.gpr[kEeSyscallResultRegister].low);
    for (std::uint8_t index = 0; index < kEeSyscallArgumentCount; ++index) {
        state.hle.last_syscall_arguments[index] = ee_syscall_argument(state, index);
    }
    state.hle.last_syscall_sp = static_cast<std::uint32_t>(state.gpr[29].low);
    if (state.hle.syscall_memory_trace_enabled) {
        const std::uint32_t address = ee_syscall_argument(state, 3);
        state.hle.syscall_memory_trace_address = address;
        state.hle.syscall_memory_trace_size = 0U;
        state.hle.syscall_memory_changed_bytes = 0U;
        state.hle.syscall_memory_trace_status = static_cast<std::uint8_t>(Status::ok);
        std::uint32_t physical{};
        const auto memory_status = resolve_ee_memory_access(
            state.memory, address, EeHleState::kSyscallMemoryWindowSize, 1U, physical);
        if (memory_status == Status::ok) {
            state.hle.syscall_memory_trace_size = EeHleState::kSyscallMemoryWindowSize;
            for (std::size_t index = 0; index < EeHleState::kSyscallMemoryWindowSize; ++index) {
                state.hle.syscall_memory_before[index] =
                    state.memory.main_ram[static_cast<std::size_t>(physical) + index];
            }
        } else {
            state.hle.syscall_memory_trace_status = static_cast<std::uint8_t>(memory_status);
        }
    }
    // The "i" wrappers negate the number. Unsigned subtraction avoids the
    // INT32_MIN negation that signed arithmetic cannot represent.
    const std::uint32_t number =
        (code & 0x8000'0000U) != 0U ? (0U - code) : code;

    ++state.hle.syscall_count;
    state.hle.last_syscall_code = code;
    state.hle.last_syscall_number = number;

    // A title-installed handler replaces the kernel slot, so it takes
    // precedence over any host service for the same number. The handler is
    // translated guest code and runs through the dispatcher; the HLE layer only
    // services slots no guest handler has replaced.
    const EeSyscallRegistrationView registration =
        find_ee_syscall_registration(state.hle, number);
    const bool guest_handler = registration.in_use;
    const EeSyscallService* const service =
        guest_handler ? nullptr : find_ee_syscall_service(number);

    // Record the boundary before dispatch so the ring reflects the guest's
    // registers rather than any handler's side effects.
    EeSyscallObservation& observation =
        state.hle.syscall_sequence[state.hle.syscall_sequence_next];
    observation = EeSyscallObservation{
        .pc = state.pc,
        .number = number,
        .arguments = state.hle.last_syscall_arguments,
        .v0_before = state.hle.last_syscall_v0_before,
        .path = guest_handler
            ? EeSyscallPath::guest_handler
            : (service != nullptr ? EeSyscallPath::host_service : EeSyscallPath::unhandled),
    };
    state.hle.syscall_sequence_next =
        (state.hle.syscall_sequence_next + 1U) % EeHleState::kSyscallSequenceCapacity;
    if (state.hle.syscall_sequence_count < EeHleState::kSyscallSequenceCapacity) {
        ++state.hle.syscall_sequence_count;
    }

    if (guest_handler) {
        return enter_ee_syscall_handler(
            state,
            number,
            registration.handler_address,
            resume_pc);
    }

    if (service == nullptr) {
        return Status::unhandled_syscall;
    }
    const Status handler_status = service->handler(state);
    if (handler_status != Status::ok) {
        return handler_status;
    }
    finish_ee_syscall_memory_trace(state);
    ++state.hle.syscall_handled_count;
    state.pc = resume_pc;
    state.hle.last_syscall_v0_after =
        static_cast<std::uint32_t>(state.gpr[kEeSyscallResultRegister].low);
    state.hle.last_syscall_resume_pc = resume_pc;
    return Status::ok;
}

} // namespace reps2::runtime
