// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

inline constexpr std::uint32_t kEeSyscallSetupThread = 0x3CU;
inline constexpr std::uint32_t kEeSyscallFlushCache = 0x64U;

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

// SetupThread (0x3C): SetupThread(gp, stack, stack_size, args, root_func).
//
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

    // A 32-bit EE result write sign-extends into the 64-bit GPR lane.
    state.gpr[kEeSyscallResultRegister].low = sign_extend_word(stack_pointer);
    // Observed kernel behavior: the call clears $v1 on return.
    state.gpr[kEeSyscallNumberRegister].low = 0;
    return Status::ok;
}

// FlushCache (0x64): the guest asks the kernel to make written instructions
// visible to the instruction stream. The runtime keeps no instruction-cache
// model and the host is cache-coherent, so the call completes with $v0 = 0 and
// touches no other state. It stops being a stub only once a real cache model
// exists; until then this is an accurate description of the host, not a
// fabricated kernel behavior.
[[nodiscard]] inline Status hle_flush_cache(EeState& state) noexcept {
    state.gpr[kEeSyscallResultRegister].low = 0;
    return Status::ok;
}

// Sorted by service number so a lookup can stop as soon as it passes the key.
inline constexpr EeSyscallService kEeSyscallServices[] = {
    {kEeSyscallSetupThread, "SetupThread", &hle_setup_thread},
    {kEeSyscallFlushCache, "FlushCache", &hle_flush_cache},
};
inline constexpr std::size_t kEeSyscallServiceCount =
    sizeof(kEeSyscallServices) / sizeof(kEeSyscallServices[0]);

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
    // The "i" wrappers negate the number. Unsigned subtraction avoids the
    // INT32_MIN negation that signed arithmetic cannot represent.
    const std::uint32_t number =
        (code & 0x8000'0000U) != 0U ? (0U - code) : code;

    ++state.hle.syscall_count;
    state.hle.last_syscall_code = code;
    state.hle.last_syscall_number = number;

    const EeSyscallService* const service = find_ee_syscall_service(number);
    if (service == nullptr) {
        return Status::unhandled_syscall;
    }
    const Status handler_status = service->handler(state);
    if (handler_status != Status::ok) {
        return handler_status;
    }
    ++state.hle.syscall_handled_count;
    state.pc = resume_pc;
    return Status::ok;
}

} // namespace reps2::runtime
