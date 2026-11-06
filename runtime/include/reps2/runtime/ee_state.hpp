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
    // EE Timer 3 mode (0x10001810). No timer counting or interrupts are
    // modelled: the register keeps only the documented writable mode bits so a
    // guest that configures and reads it back sees its own value. Reset is 0,
    // as no clock source is enabled out of reset. See ADR 0073.
    std::uint32_t timer3_mode{};
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
    Trap = 13,
};

/// Level-1 address errors for instruction fetches and data accesses.
enum class AddressErrorCode : std::uint8_t { InstructionFetch = 4, Load = 4, Store = 5 };

// ps2sdk's documented kernel limit (`MAX_SEMAPHORES 256`). Sema ids are indices
// into this table, so the bound is the same one the guest sees.
inline constexpr std::size_t kEeMaxSemaphores = 256;
// Host records for created threads. ps2sdk documents `MAX_THREADS 256` and says
// thread 0 is the idle thread; the host keeps a compact sparse table because
// EeState is held on the stack by tests and probes. A title that needs more
// concurrent thread records than this is refused honestly rather than
// fabricating an id.
inline constexpr std::size_t kEeMaxThreads = 16;

/// Id reserved for the guest's primordial thread. The kernel creates the main
/// thread at boot before any user thread, and ps2sdk's thread example lowers the
/// *main* thread's priority with `ChangeThreadPriority(GetThreadId(), 1)` after
/// starting a new one, so the two handles must differ. Thread 0 is the
/// documented idle thread. Exact hardware numbering is not independently
/// verified; REPS2 reserves 1 for the running boot thread and allocates created
/// ids from 2.
inline constexpr std::uint32_t kEeMainThreadId = 1U;
inline constexpr std::uint32_t kEeFirstCreatedThreadId = 2U;

// Host-visible thread status values. ps2sdk documents `THS_RUN 0x01`,
// `THS_READY 0x02`, `THS_WAIT 0x04`, `THS_SUSPEND 0x08`,
// `THS_WAITSUSPEND 0x0C` and `THS_DORMANT 0x10`. REPS2 models only the
// transitions its implemented services perform: CreateThread leaves a record
// DORMANT and StartThread moves it to READY. NO other status is fabricated.
inline constexpr std::uint32_t kEeThreadStatusReady = 0x02U;
inline constexpr std::uint32_t kEeThreadStatusDormant = 0x10U;

// ps2sdk's thread example guards a priority change with `priority < 128`, so
// 0..127 is the documented priority range in the observed usage.
inline constexpr std::int32_t kEeMaxThreadPriority = 128;

/// Why CreateThread (0x20) returned the failure code instead of an id.
enum class EeThreadRejection : std::uint8_t {
    none,
    unreadable_parameters,
    invalid_func,
    invalid_stack,
    invalid_gp,
    invalid_priority,
    table_full,
};

[[nodiscard]] constexpr const char* ee_thread_rejection_name(
    EeThreadRejection rejection
) noexcept {
    switch (rejection) {
    case EeThreadRejection::none: return "none";
    case EeThreadRejection::unreadable_parameters: return "unreadable_parameters";
    case EeThreadRejection::invalid_func: return "invalid_func";
    case EeThreadRejection::invalid_stack: return "invalid_stack";
    case EeThreadRejection::invalid_gp: return "invalid_gp";
    case EeThreadRejection::invalid_priority: return "invalid_priority";
    case EeThreadRejection::table_full: return "table_full";
    }
    return "unknown";
}

/// Why GetOsdConfigParam (0x4B) or SetOsdConfigParam (0x4A) refused its buffer.
enum class EeOsdConfigRejection : std::uint8_t {
    none,
    unreadable_buffer,
    unwritable_buffer,
};

[[nodiscard]] constexpr const char* ee_osd_config_rejection_name(
    EeOsdConfigRejection rejection
) noexcept {
    switch (rejection) {
    case EeOsdConfigRejection::none: return "none";
    case EeOsdConfigRejection::unreadable_buffer: return "unreadable_buffer";
    case EeOsdConfigRejection::unwritable_buffer: return "unwritable_buffer";
    }
    return "unknown";
}

/// Why StartThread (0x22) returned the failure code.
enum class EeThreadStartRejection : std::uint8_t {
    none,
    invalid_id,
    not_dormant,
    queue_full,
};

[[nodiscard]] constexpr const char* ee_thread_start_rejection_name(
    EeThreadStartRejection rejection
) noexcept {
    switch (rejection) {
    case EeThreadStartRejection::none: return "none";
    case EeThreadStartRejection::invalid_id: return "invalid_id";
    case EeThreadStartRejection::not_dormant: return "not_dormant";
    case EeThreadStartRejection::queue_full: return "queue_full";
    }
    return "unknown";
}

/// Why ChangeThreadPriority (0x29) returned the failure code.
enum class EeThreadPriorityRejection : std::uint8_t {
    none,
    invalid_id,
    invalid_priority,
};

[[nodiscard]] constexpr const char* ee_thread_priority_rejection_name(
    EeThreadPriorityRejection rejection
) noexcept {
    switch (rejection) {
    case EeThreadPriorityRejection::none: return "none";
    case EeThreadPriorityRejection::invalid_id: return "invalid_id";
    case EeThreadPriorityRejection::invalid_priority: return "invalid_priority";
    }
    return "unknown";
}

/// A created thread record. ps2sdk's `ee_thread_t` is nine 32-bit words:
/// status, func, stack, stack_size, gp_reg, initial_priority, current_priority,
/// attr, option. The kernel owns `status` and the current priority; the guest's
/// status, current-priority, attr and option words are uninitialized in the
/// observed call and are never interpreted. `status` here is host state, not a
/// guest-memory write. No stack contents or args block is initialized.
struct EeThread {
    bool in_use{};
    std::uint32_t status{};
    std::uint32_t function{};
    std::uint32_t stack{};
    std::uint32_t stack_size{};
    std::uint32_t gp{};
    std::uint32_t initial_priority{};
    std::uint32_t current_priority{};
    std::uint32_t attr{};
    std::uint32_t option{};
    std::uint32_t argv{};
};
// Host registrations for the documented 0..255 service range are indexed
// directly. ps2sdk's `alarm.c` InitAlarm also patches slot 0x12C
// ("Intc12Handler"), which is held in the small sparse list below so EeState
// stays a fixed, compact value; tests and probes keep many states on the stack.
inline constexpr std::size_t kEeMaxSyscallSlots = 256;
// The guest-visible table is addressed arithmetically (`base + number*4`) and
// covers 512 entries, including the extended slots used by kernel patches.
inline constexpr std::size_t kEeSyscallTableSlots = 512;
inline constexpr std::size_t kEeMaxExtendedSyscallSlots = 8;

struct EeSyscallRegistration {
    bool in_use{};
    std::uint32_t handler_address{};
};

/// A registration outside the indexed 0..255 range.
struct EeExtendedSyscallRegistration {
    bool in_use{};
    std::uint32_t number{};
    std::uint32_t handler_address{};
};

// The EE kernel syscall table holds a handler address per slot (ps2sdk's
// GetSyscallHandler indexes exactly that table). A title can replace a slot
// through SetSyscall (0x74) and then reach its own kernel-mode routine by
// issuing the corresponding SYSCALL, so REPS2 must be able to continue guest
// execution at that handler and return to the user continuation afterwards.
//
// The real kernel enters the handler from its exception path, so the user
// register context is saved before the call and restored on return, with only
// $v0 kept from the handler as the service result. The host models that here:
// one frame per active handler invocation, saved before the handler runs.
struct EeSyscallHandlerFrame {
    std::array<Gpr128, 32> saved_gpr{};
    std::uint64_t saved_hi{};
    std::uint64_t saved_lo{};
    std::uint64_t saved_hi1{};
    std::uint64_t saved_lo1{};
    std::array<std::uint32_t, 32> saved_fpr{};
    std::uint32_t saved_acc{};
    std::uint32_t saved_fcsr{};
    std::uint64_t saved_sa{};
    std::array<std::uint32_t, 32> saved_cop0{};
    std::uint32_t handler_address{};
    std::uint32_t resume_pc{};
    std::uint32_t slot{};
};

// Handlers can themselves issue syscalls, so dispatch keeps a small stack of
// frames rather than a single slot.
inline constexpr std::size_t kEeMaxSyscallHandlerDepth = 8;

enum class EeSetSyscallRejection : std::uint8_t {
    none,
    invalid_slot,
    invalid_handler,
    table_unwritable,
    registration_table_full,
};

[[nodiscard]] constexpr const char* ee_set_syscall_rejection_name(
    EeSetSyscallRejection rejection
) noexcept {
    switch (rejection) {
    case EeSetSyscallRejection::none: return "none";
    case EeSetSyscallRejection::invalid_slot: return "invalid_slot";
    case EeSetSyscallRejection::invalid_handler: return "invalid_handler";
    case EeSetSyscallRejection::table_unwritable: return "table_unwritable";
    case EeSetSyscallRejection::registration_table_full: return "registration_table_full";
    }
    return "unknown";
}

// Why a CreateSema (0x40) call returned the documented -1 instead of an id.
// A rejection is a guest-visible failure code, not a host error: nothing is
// allocated and no id is invented. The reason is recorded for diagnostics only.
enum class EeSemaRejection : std::uint8_t {
    none,
    unreadable_parameters,
    negative_counts,
    init_exceeds_max,
    table_full,
};

[[nodiscard]] constexpr const char* ee_sema_rejection_name(EeSemaRejection rejection) noexcept {
    switch (rejection) {
    case EeSemaRejection::none: return "none";
    case EeSemaRejection::unreadable_parameters: return "unreadable_parameters";
    case EeSemaRejection::negative_counts: return "negative_counts";
    case EeSemaRejection::init_exceeds_max: return "init_exceeds_max";
    case EeSemaRejection::table_full: return "table_full";
    }
    return "unknown";
}

// A semaphore object created through CreateSema (0x40).
//
// The public description of the guest parameter block lists count, max_count,
// init_count, wait_threads, attr and option, and states that only init_count and
// max_count need to be supplied. The kernel's own object keeps a count, a
// maximum, the attribute/option words and a waiter list.
//
// What this model deliberately does not have is the waiter list: there is no
// scheduler, so no thread can block. The object is therefore a counter and
// nothing can be released from a wait by observing it. Signal/Wait/Poll are not
// implemented yet, so this table is created state only (ADR 0066).
struct EeSema {
    bool in_use{};
    std::int32_t count{};
    std::int32_t max_count{};
    std::uint32_t attr{};
    std::uint32_t option{};
    // The guest PC of the creating syscall, so a session can attribute an object
    // back to the code that asked for it.
    std::uint32_t create_pc{};
};

// Host-configured HLE (high-level emulation) service layer for the EE kernel
// syscall interface declared in ee_hle.hpp. None of these fields is
// guest-architectural state: they record what the host service layer observed,
// so a probe or session can report which services ran without claiming that the
// title's kernel executed them. Everything here is reset by
// initialize_ee_state; enabling the layer is a host decision taken afterwards.
// Which dispatch path served a syscall boundary.
enum class EeSyscallPath : std::uint8_t {
    guest_handler = 0, ///< A slot the title replaced through SetSyscall.
    host_service = 1,  ///< A REPS2 HLE service.
    unhandled = 2,     ///< No handler: execution stops on the trap.
};

// One syscall boundary, captured before dispatch. The sequence ring keeps the
// last few boundaries so a caller/callee order can be inspected without a
// per-call trace line.
struct EeSyscallObservation {
    std::uint32_t pc{};
    std::uint32_t number{};
    std::array<std::uint32_t, 4> arguments{};
    std::uint32_t v0_before{};
    EeSyscallPath path{};
};

struct EeHleState {
    // When false, a generated SYSCALL delivers the architectural Level-1
    // exception exactly as it did before this layer existed.
    bool syscall_enabled{};
    std::uint64_t syscall_count{};
    std::uint64_t syscall_handled_count{};
    // Raw 32-bit $v1 value and the normalized dispatch number: the ps2sdk "i"
    // wrappers pass the negated service number, so dispatch uses its magnitude.
    std::uint32_t last_syscall_code{};
    std::uint32_t last_syscall_number{};
    std::uint32_t last_syscall_pc{};
    std::array<std::uint32_t, 4> last_syscall_arguments{};
    std::uint32_t last_syscall_v0_before{};
    std::uint32_t last_syscall_v0_after{};
    std::uint32_t last_syscall_resume_pc{};
    std::uint32_t last_syscall_sp{};
    // Bounded ring of recent syscall boundaries, oldest first once the ring is
    // full. It records the dispatch path as well as the pre-dispatch arguments.
    static constexpr std::size_t kSyscallSequenceCapacity = 16U;
    std::array<EeSyscallObservation, kSyscallSequenceCapacity> syscall_sequence{};
    std::size_t syscall_sequence_next{};
    std::size_t syscall_sequence_count{};

    /// Observation at `index`, counting from the oldest retained boundary.
    [[nodiscard]] const EeSyscallObservation& syscall_observation(std::size_t index) const noexcept {
        const std::size_t oldest = syscall_sequence_count < kSyscallSequenceCapacity
            ? std::size_t{0}
            : syscall_sequence_next;
        return syscall_sequence[(oldest + index) % kSyscallSequenceCapacity];
    }
    // Optional syscall-boundary memory window. The pre-call snapshot records
    // observable reads; the post-call comparison records guest writes.
    static constexpr std::size_t kSyscallMemoryWindowSize = 256U;
    bool syscall_memory_trace_enabled{};
    std::uint32_t syscall_memory_trace_address{};
    std::uint32_t syscall_memory_trace_size{};
    std::array<std::uint8_t, kSyscallMemoryWindowSize> syscall_memory_before{};
    std::array<std::uint8_t, kSyscallMemoryWindowSize> syscall_memory_after{};
    std::uint32_t syscall_memory_changed_bytes{};
    std::uint8_t syscall_memory_trace_status{};
    // SetupThread (0x3C) observation. No thread context object exists yet, so
    // this records the decoded arguments and the stack pointer the service
    // returned rather than a thread that could be scheduled.
    bool setup_thread_called{};
    std::uint32_t setup_thread_gp{};
    std::uint32_t setup_thread_stack_top{};
    std::uint32_t setup_thread_stack_size{};
    std::uint32_t setup_thread_stack_pointer{};
    std::uint32_t setup_thread_args{};
    // SetupHeap (0x3D) observation. The heap is recorded as a per-thread range;
    // no kernel allocator or thread scheduler is modeled by this layer.
    bool setup_heap_called{};
    std::uint32_t setup_heap_start{};
    std::uint32_t setup_heap_size{};
    std::uint32_t setup_heap_end{};
    // CreateThread (0x20) observation. `thread_count` counts records, not ids
    // handed out; nothing deletes threads yet.
    std::uint64_t create_thread_calls{};
    std::uint32_t thread_count{};
    std::uint32_t last_thread_parameter_address{};
    std::int32_t last_thread_id{-1};
    std::uint32_t last_thread_function{};
    std::uint32_t last_thread_stack{};
    std::uint32_t last_thread_stack_size{};
    std::uint32_t last_thread_gp{};
    std::int32_t last_thread_initial_priority{};
    std::uint32_t last_thread_attr{};
    std::uint32_t last_thread_option{};
    EeThreadRejection last_thread_rejection{};
    std::array<EeThread, kEeMaxThreads> threads{};
    // StartThread (0x22), GetThreadId (0x2F) and ChangeThreadPriority (0x29).
    // The ready queue is a host model: StartThread records READY and inserts by
    // priority, but no dispatcher switches contexts yet, so the boot thread
    // keeps executing. `get_thread_id_calls` always observes the boot thread.
    std::uint64_t start_thread_calls{};
    std::uint32_t last_start_thread_id{};
    std::uint32_t last_start_thread_args{};
    EeThreadStartRejection last_start_rejection{};
    std::uint32_t ready_count{};
    std::array<std::uint32_t, kEeMaxThreads> ready_queue{};
    std::uint64_t get_thread_id_calls{};
    std::uint64_t change_priority_calls{};
    std::uint32_t last_priority_thread_id{};
    std::int32_t last_priority_value{};
    EeThreadPriorityRejection last_priority_rejection{};
    // ps2tek: boot "creates a thread with priority 0 (main thread)".
    std::uint32_t main_thread_priority{};
    // GetOsdConfigParam (0x4B) and SetOsdConfigParam (0x4A). The kernel keeps
    // this word in NVRAM; REPS2 has no NVRAM, so it is a host-session value
    // initialized to zero. The observed caller at 0x0025F148 uses Set/Get
    // read-back over bits 13..15, so a coherent store is the guest-visible
    // contract; persistence across runs is explicitly not provided.
    std::uint32_t osd_config_param{};
    std::uint64_t get_osd_config_calls{};
    std::uint64_t set_osd_config_calls{};
    std::uint32_t last_osd_config_address{};
    std::uint32_t last_osd_config_value{};
    EeOsdConfigRejection last_osd_config_rejection{};
    // CreateSema (0x40) observation. `semaphore_count` counts live objects, not
    // ids handed out: nothing deletes objects yet, so the two happen to agree.
    std::uint64_t create_sema_calls{};
    std::uint32_t semaphore_count{};
    std::uint32_t last_sema_parameter_address{};
    std::int32_t last_sema_init_count{};
    std::int32_t last_sema_max_count{};
    std::uint32_t last_sema_attr{};
    std::uint32_t last_sema_option{};
    // -1 when the call was refused; otherwise the id written to $v0.
    std::int32_t last_sema_id{-1};
    EeSemaRejection last_sema_rejection{};
    std::uint64_t set_syscall_calls{};
    std::uint32_t last_set_syscall_slot{};
    std::uint32_t last_set_syscall_handler{};
    EeSetSyscallRejection last_set_syscall_rejection{};
    // FindAddress (0x83) observation. The service scans a guest word table for a
    // target entry and reports the match address; it writes nothing to guest
    // memory. SetSyscall (0x74) publishes handler pointers into the guest
    // syscall table, so a window covering that table reports real matches; an
    // unpopulated window still reports "not found" (0).
    bool find_address_called{};
    std::uint32_t last_find_address_start{};
    std::uint32_t last_find_address_end{};
    std::uint32_t last_find_address_target{};
    std::uint32_t last_find_address_result{};
    std::array<EeSyscallRegistration, kEeMaxSyscallSlots> syscall_registrations{};
    std::array<EeExtendedSyscallRegistration, kEeMaxExtendedSyscallSlots>
        extended_syscall_registrations{};
    // Guest-handler invocation, distinct from host-serviced HLE calls: the
    // handler itself is translated guest code and runs through the dispatcher.
    std::array<EeSyscallHandlerFrame, kEeMaxSyscallHandlerDepth> syscall_handler_frames{};
    std::uint8_t syscall_handler_depth{};
    std::uint64_t guest_handler_dispatches{};
    std::uint64_t guest_handler_returns{};
    std::array<EeSema, kEeMaxSemaphores> semaphores{};
};

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
    // Host-side execution accounting, not guest-architectural state: the
    // generated dispatcher charges an admitted basic block's instruction count
    // (the same `required` it subtracts from the budget) here, so a host probe
    // can report how much guest code actually ran. A block that faults part-way
    // is still charged in full, matching the block-atomic budget model.
    std::uint64_t instructions_executed{};
    EeMemory memory{};
    static constexpr std::size_t kInstructionTraceWindowSize = 16U;
    bool instruction_trace_enabled{};
    std::array<std::uint32_t, kInstructionTraceWindowSize> instruction_trace_pcs{};
    std::uint32_t instruction_trace_count{};
    EeHleState hle{};
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
    // 25..6 of the word at that PC. The EE kernel service layer is ee_hle.hpp;
    // a guest `syscall` reaches it through `dispatch_ee_syscall`.
    syscall,
    breakpoint,
    // A Level-1 guest exception was entered and COP0/PC now hold its context.
    exception_delivered,
    // ERET was executed without exactly one valid EXL/ERL return context.
    invalid_exception_return,
    // HLE boundary: the generated SYSCALL site asked the host service layer for
    // the EE kernel service numbered by $v1 and no handler exists. PC still
    // points at the SYSCALL and no COP0 exception context was created, so the
    // guest never observed this service; it is a host limitation, not a guest
    // exception and not success. See ee_hle.hpp.
    unhandled_syscall,
    // HLE boundary: a registered guest handler was about to run but the
    // handler-invocation stack is full. The syscall did not execute and the
    // guest never observed it.
    syscall_handler_overflow,
};

[[nodiscard]] constexpr bool is_guest_trap(Status status) noexcept {
    return status == Status::syscall || status == Status::breakpoint
        || status == Status::exception_delivered;
}

// Host-observable classification of how a bounded execution stopped. The names
// are a stable, machine-readable contract for the native probe trace and any
// session layer that consumes it; they describe the host boundary, not a claim
// that a game booted. `guest_exception` means COP0/PC hold the exception
// context; `unresolved_control_flow` means the dispatcher reached a PC with no
// translated block. Neither is success or playability.
[[nodiscard]] constexpr const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::ok: return "ok";
    case Status::undefined_word_operand: return "undefined_word_operand";
    case Status::integer_overflow: return "integer_overflow";
    case Status::instruction_budget_exhausted: return "budget_exhausted";
    case Status::external_control_flow: return "unresolved_control_flow";
    case Status::unsupported_instruction: return "unsupported_instruction";
    case Status::address_error: return "address_error";
    case Status::unmapped_memory: return "unmapped_memory";
    case Status::unsupported_memory_access: return "unsupported_memory_access";
    case Status::invalid_memory_image: return "invalid_memory_image";
    case Status::syscall: return "syscall";
    case Status::breakpoint: return "breakpoint";
    case Status::exception_delivered: return "guest_exception";
    case Status::invalid_exception_return: return "invalid_exception_return";
    case Status::unhandled_syscall: return "unhandled_syscall";
    case Status::syscall_handler_overflow: return "syscall_handler_overflow";
    }
    return "unknown";
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
// device failures retain their distinct host/runtime statuses.
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
    state.memory.timer3_mode = 0;
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
    state.instructions_executed = 0;
    state.instruction_trace_count = 0;
    state.instruction_trace_pcs.fill(0);
    // The HLE layer is host configuration, not guest state: reset it so a probe
    // starts from "disabled, nothing observed" and must opt in explicitly.
    state.hle = {};
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

// EE Timer 3 mode register. ps2sdk `timer.h` defines
// `T3_MODE ((volatile unsigned int *)0x10001810)`; ps2tek documents bit 8 as
// "compare interrupt enable" and bit 7 as "timer enable". Only the mode word is
// modelled: no clock source, counting, compare match or interrupt is
// implemented. Bits 0..9 are the documented writable mode bits; the EQUF/OVFF
// status bits read as zero because no event can set them yet. Reset is zero.
inline constexpr std::uint32_t kEeTimer3ModeAddress{0x1000'1810U};
inline constexpr std::uint32_t kEeTimer3ModeWritableMask{0x0000'03DFU};

[[nodiscard]] inline Status load_u32(
    const EeMemory& memory,
    std::uint32_t address,
    std::uint32_t& value
) noexcept {
    if (address == kEeTimer3ModeAddress) {
        value = memory.timer3_mode;
        return Status::ok;
    }
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
    if (address == kEeTimer3ModeAddress) {
        memory.timer3_mode = value & kEeTimer3ModeWritableMask;
        return Status::ok;
    }
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

// PADDW adds four corresponding 32-bit lanes modulo 2^32. Arithmetic
// is performed on unsigned lane values, so wraparound is defined C++ behavior.
[[nodiscard]] constexpr Gpr128 parallel_add_word(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    const auto lane0 = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(lhs.low) + static_cast<std::uint32_t>(rhs.low)
    );
    const auto lane1 = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(lhs.low >> 32U)
        + static_cast<std::uint32_t>(rhs.low >> 32U)
    );
    const auto lane2 = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(lhs.high) + static_cast<std::uint32_t>(rhs.high)
    );
    const auto lane3 = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(lhs.high >> 32U)
        + static_cast<std::uint32_t>(rhs.high >> 32U)
    );
    return Gpr128{
        static_cast<std::uint64_t>(lane0) | (static_cast<std::uint64_t>(lane1) << 32U),
        static_cast<std::uint64_t>(lane2) | (static_cast<std::uint64_t>(lane3) << 32U),
    };
}

// PSUBW subtracts four corresponding 32-bit lanes modulo 2^32. No borrow
// crosses a lane boundary; unsigned subtraction gives the defined bit result.
[[nodiscard]] constexpr Gpr128 parallel_sub_word(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    const auto lane0 = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(lhs.low) - static_cast<std::uint32_t>(rhs.low)
    );
    const auto lane1 = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(lhs.low >> 32U)
        - static_cast<std::uint32_t>(rhs.low >> 32U)
    );
    const auto lane2 = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(lhs.high) - static_cast<std::uint32_t>(rhs.high)
    );
    const auto lane3 = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(lhs.high >> 32U)
        - static_cast<std::uint32_t>(rhs.high >> 32U)
    );
    return Gpr128{
        static_cast<std::uint64_t>(lane0) | (static_cast<std::uint64_t>(lane1) << 32U),
        static_cast<std::uint64_t>(lane2) | (static_cast<std::uint64_t>(lane3) << 32U),
    };
}

// PADDH adds eight corresponding 16-bit lanes modulo 2^16. Arithmetic is
// performed on unsigned lane values so wraparound is defined C++ behavior.
// No carry crosses a halfword boundary.
[[nodiscard]] constexpr Gpr128 parallel_add_halfword(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    auto pack = [](std::uint64_t a, std::uint64_t b) noexcept -> std::uint64_t {
        std::uint64_t result = 0;
        for (unsigned shift = 0; shift < 64; shift += 16) {
            const auto lane = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(a >> shift) + static_cast<std::uint16_t>(b >> shift)
            );
            result |= static_cast<std::uint64_t>(lane) << shift;
        }
        return result;
    };
    return Gpr128{pack(lhs.low, rhs.low), pack(lhs.high, rhs.high)};
}

// PSUBH subtracts eight corresponding 16-bit lanes modulo 2^16. No borrow
// crosses a halfword boundary; unsigned subtraction gives the defined bit result.
[[nodiscard]] constexpr Gpr128 parallel_sub_halfword(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    auto pack = [](std::uint64_t a, std::uint64_t b) noexcept -> std::uint64_t {
        std::uint64_t result = 0;
        for (unsigned shift = 0; shift < 64; shift += 16) {
            const auto lane = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(a >> shift) - static_cast<std::uint16_t>(b >> shift)
            );
            result |= static_cast<std::uint64_t>(lane) << shift;
        }
        return result;
    };
    return Gpr128{pack(lhs.low, rhs.low), pack(lhs.high, rhs.high)};
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

// PCPYLD builds a 128-bit value from the low doubleword of each source:
// result.low is rt.low (the second operand) and result.high is rs.low (the
// first operand). The value is formed before assignment, so a destination that
// aliases either source observes the complete result.
[[nodiscard]] constexpr Gpr128 parallel_copy_low_doubleword(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    return Gpr128{rhs.low, lhs.low};
}

// PAND is the plain 128-bit bitwise AND of the two operands. It touches no
// architectural state beyond the destination GPR and cannot fault, so both
// doublewords are computed independently and assigned as one value.
[[nodiscard]] constexpr Gpr128 parallel_and(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    return Gpr128{lhs.low & rhs.low, lhs.high & rhs.high};
}

// PXOR is plain 128-bit bitwise XOR two operands.
[[nodiscard]] constexpr Gpr128 parallel_xor(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    return Gpr128{lhs.low ^ rhs.low, lhs.high ^ rhs.high};
}

// POR is the plain 128-bit bitwise OR of the two operands. It touches no
// architectural state beyond the destination GPR and cannot fault, so both
// doublewords are computed independently and assigned as one value.
[[nodiscard]] constexpr Gpr128 parallel_or(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    return Gpr128{lhs.low | rhs.low, lhs.high | rhs.high};
}

// PNOR is the plain 128-bit bitwise NOR of the two operands.
[[nodiscard]] constexpr Gpr128 parallel_nor(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    return Gpr128{~(lhs.low | rhs.low), ~(lhs.high | rhs.high)};
}

// PEXTLW interleaves the four low words of its operands: result word 0 is
// rt word 0, word 1 is rs word 0, word 2 is rt word 1 and word 3 is rs word 1.
// Both high doublewords are ignored. The complete value is formed before
// assignment, so a destination that aliases either source observes the
// original operands.
[[nodiscard]] constexpr Gpr128 parallel_extend_low_word(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    const auto lhs_low = static_cast<std::uint32_t>(lhs.low);
    const auto lhs_high = static_cast<std::uint32_t>(lhs.low >> 32U);
    const auto rhs_low = static_cast<std::uint32_t>(rhs.low);
    const auto rhs_high = static_cast<std::uint32_t>(rhs.low >> 32U);
    return Gpr128{
        static_cast<std::uint64_t>(rhs_low) | (static_cast<std::uint64_t>(lhs_low) << 32U),
        static_cast<std::uint64_t>(rhs_high) | (static_cast<std::uint64_t>(lhs_high) << 32U),
    };
}

// PEXTLB interleaves the low eight bytes of both operands: `lhs` is `rs` and
// `rhs` is `rt`, so each byte pair is `(rt[i], rs[i])`. The complete result is
// formed before assignment, so source/destination aliasing is safe.
[[nodiscard]] constexpr Gpr128 parallel_extend_low_byte(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
    for (std::uint32_t index = 0; index < 8U; ++index) {
        const auto lhs_byte = (lhs.low >> (index * 8U)) & 0xFFU;
        const auto rhs_byte = (rhs.low >> (index * 8U)) & 0xFFU;
        const auto pair = rhs_byte | (lhs_byte << 8U);
        if (index < 4U) {
            low |= pair << (index * 16U);
        } else {
            high |= pair << ((index - 4U) * 16U);
        }
    }
    return Gpr128{low, high};
}

// PPACW packs the low word of each 64-bit half of its operands: result word 0
// is rt word 0, word 1 is rt word 2, word 2 is rs word 0 and word 3 is rs word
// 2. Each result doubleword therefore comes from one source only, and the
// second word of that pair is the low word of the source's upper doubleword --
// the opposite of PEXTLW, which pairs the two sources within each doubleword.
// The complete value is formed before assignment, so a destination that
// aliases either source observes the original operands.
[[nodiscard]] constexpr Gpr128 parallel_pack_word(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    const auto lhs_lower = static_cast<std::uint32_t>(lhs.low);
    const auto lhs_upper = static_cast<std::uint32_t>(lhs.high);
    const auto rhs_lower = static_cast<std::uint32_t>(rhs.low);
    const auto rhs_upper = static_cast<std::uint32_t>(rhs.high);
    return Gpr128{
        static_cast<std::uint64_t>(rhs_lower) | (static_cast<std::uint64_t>(rhs_upper) << 32U),
        static_cast<std::uint64_t>(lhs_lower) | (static_cast<std::uint64_t>(lhs_upper) << 32U),
    };
}

// PCPYUD copies the upper doubleword of each operand: result.low is rs.high and
// result.high is rt.high. Unlike PCPYLD the two halves are not crossed, so the
// first operand supplies the low half. Both reads happen before the single
// assignment, so a destination that aliases either source observes the original
// operands.
[[nodiscard]] constexpr Gpr128 parallel_copy_upper_doubleword(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    return Gpr128{lhs.high, rhs.high};
}

// PEXEW exchanges the low word of each 64-bit half with the low word of the
// other half: result words are [rt1, rt2, rt3, rt0].
[[nodiscard]] constexpr Gpr128 parallel_exchange_even_word(
    const Gpr128& src
) noexcept {
    return Gpr128{
        (src.low & 0xFFFFFFFF00000000ULL) | (src.high & 0x00000000FFFFFFFFULL),
        (src.high & 0xFFFFFFFF00000000ULL) | (src.low & 0x00000000FFFFFFFFULL),
    };
}

// PROT3W rotates the four 32-bit words left by one position: result words are
// [rt1, rt2, rt0, rt3].
[[nodiscard]] constexpr Gpr128 parallel_rotate_3_words(
    const Gpr128& src
) noexcept {
    const auto word0 = static_cast<std::uint32_t>(src.low);
    const auto word1 = static_cast<std::uint32_t>(src.low >> 32U);
    const auto word2 = static_cast<std::uint32_t>(src.high);
    const auto word3 = static_cast<std::uint32_t>(src.high >> 32U);
    return Gpr128{
        static_cast<std::uint64_t>(word1) | (static_cast<std::uint64_t>(word2) << 32U),
        static_cast<std::uint64_t>(word0) | (static_cast<std::uint64_t>(word3) << 32U),
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

[[nodiscard]] constexpr Gpr128 parallel_add_u8(
    const Gpr128& lhs,
    const Gpr128& rhs
) noexcept {
    auto pack = [](std::uint64_t a, std::uint64_t b) noexcept -> std::uint64_t {
        std::uint64_t result = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            const auto sum = static_cast<unsigned>((a >> shift) & 0xFFU)
                + static_cast<unsigned>((b >> shift) & 0xFFU);
            const auto lane = sum > 0xFFU ? 0xFFU : sum;
            result |= static_cast<std::uint64_t>(lane) << shift;
        }
        return result;
    };
    return Gpr128{pack(lhs.low, rhs.low), pack(lhs.high, rhs.high)};
}

} // namespace reps2::runtime
