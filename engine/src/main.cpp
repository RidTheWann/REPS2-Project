// SPDX-License-Identifier: GPL-3.0-or-later
// RG REPS2 ENGINE bounded native-module runner.
//
// This is a developer/runtime host, not a claim that every PS2 title boots.
// It loads only trusted native modules, validates their ABI, owns EE state/RAM,
// and emits one machine-readable JSON execution report.
#include "rg_reps2_engine/rg_reps2_engine.hpp"
#include "reps2/runtime/ee_state.hpp"
#include "reps2/runtime/host_media.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <stdexcept>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace rg::reps2::engine;
using reps2::runtime::HostMediaSource;

constexpr std::uint64_t kDefaultInstructionBudget{5'000'000U};
constexpr std::uint32_t kDefaultMaxBatches{1U};
constexpr std::uint32_t kMaxBatchesLimit{10'000U};

struct Options {
    std::filesystem::path module_path;
    std::filesystem::path ram_path;
    std::filesystem::path media_path;
    std::uint64_t instruction_budget{kDefaultInstructionBudget};
    std::uint32_t max_batches{kDefaultMaxBatches};
    bool enable_hle{};
    bool enable_trace{};
};

struct MediaImage {
    std::ifstream stream;
    std::uint64_t byte_size{};
    std::uint64_t sector_count{};
};

[[nodiscard]] std::string_view engine_error_name(EngineError error) noexcept {
    switch (error) {
    case EngineError::none: return "none";
    case EngineError::unsupported_host_target: return "unsupported_host_target";
    case EngineError::invalid_module_name: return "invalid_module_name";
    case EngineError::invalid_module_descriptor: return "invalid_module_descriptor";
    case EngineError::module_descriptor_abi_mismatch: return "module_descriptor_abi_mismatch";
    case EngineError::engine_abi_mismatch: return "engine_abi_mismatch";
    case EngineError::runtime_abi_mismatch: return "runtime_abi_mismatch";
    case EngineError::toolchain_abi_mismatch: return "toolchain_abi_mismatch";
    case EngineError::native_target_mismatch: return "native_target_mismatch";
    case EngineError::missing_initializer: return "missing_initializer";
    case EngineError::missing_entry_point: return "missing_entry_point";
    case EngineError::invalid_prepared_module: return "invalid_prepared_module";
    case EngineError::invalid_instruction_budget: return "invalid_instruction_budget";
    case EngineError::invalid_module_status: return "invalid_module_status";
    }
    return "unknown";
}

[[nodiscard]] std::string_view session_error_name(SessionError error) noexcept {
    switch (error) {
    case SessionError::none: return "none";
    case SessionError::invalid_phase: return "invalid_phase";
    case SessionError::incompatible_engine: return "incompatible_engine";
    case SessionError::invalid_instruction_budget: return "invalid_instruction_budget";
    case SessionError::invalid_ram_image_size: return "invalid_ram_image_size";
    case SessionError::out_of_memory: return "out_of_memory";
    case SessionError::initialization_failed: return "initialization_failed";
    case SessionError::execution_failed: return "execution_failed";
    }
    return "unknown";
}

[[nodiscard]] std::string_view phase_name(SessionPhase phase) noexcept {
    switch (phase) {
    case SessionPhase::empty: return "empty";
    case SessionPhase::ready: return "ready";
    case SessionPhase::running: return "running";
    case SessionPhase::paused: return "paused";
    case SessionPhase::completed: return "completed";
    case SessionPhase::stopped: return "stopped";
    }
    return "unknown";
}

[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const char raw_character : value) {
        encoded.push_back(static_cast<char8_t>(
            static_cast<unsigned char>(raw_character)));
    }
    return std::filesystem::path{encoded};
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size());
}

#if defined(_WIN32)
[[nodiscard]] std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Windows command-line argument is too long");
    }
    const int input_size = static_cast<int>(value.size());
    const int output_size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), input_size,
        nullptr, 0, nullptr, nullptr);
    if (output_size <= 0) {
        throw std::runtime_error("cannot convert Windows command-line argument to UTF-8");
    }
    std::string output(static_cast<std::size_t>(output_size), '\\0');
    const int written = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), input_size,
        output.data(), output_size, nullptr, nullptr);
    if (written != output_size) {
        throw std::runtime_error("incomplete UTF-8 conversion of Windows command-line argument");
    }
    return output;
}
#endif

[[nodiscard]] bool parse_u64(std::string_view value, std::uint64_t& result) noexcept {
    if (value.empty()) return false;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, result, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options, bool& show_help) {
    show_help = false;
    if (argc < 2) return false;
    if (std::string_view{argv[1]} == "--help" || std::string_view{argv[1]} == "-h") {
        show_help = true;
        return true;
    }
    options.module_path = path_from_utf8(argv[1]);
    if (options.module_path.empty()) return false;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--hle-syscalls") {
            options.enable_hle = true;
        } else if (argument == "--instruction-trace") {
            options.enable_trace = true;
        } else if (argument == "--ram" || argument == "--media"
                   || argument == "--budget" || argument == "--max-batches") {
            if (index + 1 >= argc) return false;
            const std::string_view value{argv[++index]};
            if (value.empty()) return false;
            if (argument == "--ram") {
                options.ram_path = path_from_utf8(value);
            } else if (argument == "--media") {
                options.media_path = path_from_utf8(value);
            } else if (argument == "--budget") {
                if (!parse_u64(value, options.instruction_budget)
                    || options.instruction_budget == 0U) return false;
            } else {
                std::uint64_t batches{};
                if (!parse_u64(value, batches) || batches == 0U
                    || batches > kMaxBatchesLimit) return false;
                options.max_batches = static_cast<std::uint32_t>(batches);
            }
        } else {
            return false;
        }
    }
    return true;
}

void print_help() {
    std::cout
        << "RG REPS2 ENGINE " << kEngineVersion.major << '.'
        << kEngineVersion.minor << '.' << kEngineVersion.patch << "\n"
        << "Usage:\n"
        << "  rg-reps2-engine <module.dll|module.so> [options]\n\n"
        << "Options:\n"
        << "  --ram <image.ram.bin>   Exact 32 MiB initial EE main-RAM image\n"
        << "  --media <disc.iso>      Optional raw 2048-byte-sector PS2 ISO source\n"
        << "  --budget <instructions> Guest instruction budget per run batch\n"
        << "  --max-batches <count>  Maximum resumable run batches (1..10000)\n"
        << "  --hle-syscalls          Enable only the implemented EE syscall HLE services\n"
        << "  --instruction-trace     Include the bounded EE instruction-PC trace\n"
        << "  --version               Print engine version\n"
        << "  --help                  Print this help\n\n"
        << "The runner loads trusted native code in-process; modules are not sandboxed.\n"
        << "The runner returns a JSON report on stdout. A guest stop is reported as data,\n"
        << "not silently converted into a successful game boot.\n";
}

void json_string(std::ostream& output, std::string_view value) {
    output.put('"');
    constexpr char kHex[] = "0123456789abcdef";
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) {
                output << "\\u00" << kHex[(character >> 4U) & 0x0FU]
                       << kHex[character & 0x0FU];
            } else {
                output.put(static_cast<char>(character));
            }
            break;
        }
    }
    output.put('"');
}

void print_error(
    std::string_view code,
    std::string_view message,
    std::uint32_t platform_error = 0U,
    std::string_view engine_error = {}
) {
    std::cout << "{\"kind\":\"rg.reps2.engine.error.v1\",\"error\":{\"code\":";
    json_string(std::cout, code);
    std::cout << ",\"message\":";
    json_string(std::cout, message);
    if (platform_error != 0U) {
        std::cout << ",\"platform_error\":" << platform_error;
    }
    if (!engine_error.empty()) {
        std::cout << ",\"engine_error\":";
        json_string(std::cout, engine_error);
    }
    std::cout << "}}\n";
}

[[nodiscard]] bool load_ram_image(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& image,
    std::string& error
) {
    std::error_code filesystem_error{};
    const std::uintmax_t size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) {
        error = "cannot stat RAM image: " + filesystem_error.message();
        return false;
    }
    if (size != reps2::runtime::kEeMainRamSize) {
        error = "RAM image must be exactly 33554432 bytes";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open RAM image";
        return false;
    }
    try {
        image.resize(reps2::runtime::kEeMainRamSize);
    } catch (const std::bad_alloc&) {
        error = "out of memory allocating the initial EE RAM image";
        return false;
    }
    stream.read(
        reinterpret_cast<char*>(image.data()),
        static_cast<std::streamsize>(image.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(image.size())) {
        error = "could not read the complete 32 MiB RAM image";
        image.clear();
        return false;
    }
    return true;
}

[[nodiscard]] bool open_media_image(
    const std::filesystem::path& path,
    MediaImage& media,
    std::string& error
) {
    std::error_code filesystem_error{};
    media.byte_size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) {
        error = "cannot stat media image: " + filesystem_error.message();
        return false;
    }
    if (media.byte_size == 0U
        || (media.byte_size % reps2::runtime::kHostMediaSectorSize) != 0U) {
        error = "media image must be a non-empty raw 2048-byte-sector ISO";
        return false;
    }
    media.sector_count =
        media.byte_size / reps2::runtime::kHostMediaSectorSize;
    if (media.sector_count > reps2::runtime::kHostMediaSectorAddressLimit) {
        error = "media image exceeds the runtime's 32-bit sector-address range";
        return false;
    }
    media.stream.open(path, std::ios::binary);
    if (!media.stream) {
        error = "cannot open media image";
        return false;
    }
    return true;
}

[[nodiscard]] bool read_media_sectors(
    void* context,
    std::uint32_t lbn,
    std::uint32_t count,
    std::span<std::uint8_t> destination
) noexcept {
    if (context == nullptr || count == 0U) return count == 0U;
    auto& media = *static_cast<MediaImage*>(context);
    const std::uint64_t required =
        static_cast<std::uint64_t>(count) * reps2::runtime::kHostMediaSectorSize;
    const std::uint64_t start =
        static_cast<std::uint64_t>(lbn) * reps2::runtime::kHostMediaSectorSize;
    const std::uint64_t end = start + required;
    if (required > destination.size() || end < start || end > media.byte_size
        || required > static_cast<std::uint64_t>(
               std::numeric_limits<std::streamsize>::max())
        || start > static_cast<std::uint64_t>(
               std::numeric_limits<std::streamoff>::max())) {
        return false;
    }

    try {
        std::vector<std::uint8_t> scratch(static_cast<std::size_t>(required));
        media.stream.clear();
        media.stream.seekg(static_cast<std::streamoff>(start), std::ios::beg);
        if (!media.stream) return false;
        media.stream.read(
            reinterpret_cast<char*>(scratch.data()),
            static_cast<std::streamsize>(scratch.size()));
        if (!media.stream
            || media.stream.gcount() != static_cast<std::streamsize>(scratch.size())) {
            return false;
        }
        std::copy(scratch.begin(), scratch.end(), destination.begin());
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] int run_engine(const Options& options) {
    std::vector<std::uint8_t> ram_image;
    std::string input_error;
    if (!options.ram_path.empty()
        && !load_ram_image(options.ram_path, ram_image, input_error)) {
        print_error("REPS2_ENGINE_RAM_IMAGE_INVALID", input_error);
        return 3;
    }

    MediaImage media{};
    if (!options.media_path.empty()
        && !open_media_image(options.media_path, media, input_error)) {
        print_error("REPS2_ENGINE_MEDIA_IMAGE_INVALID", input_error);
        return 3;
    }

    const Engine engine{};
    PreparedNativeModule module{};
    const ModuleLoadResult loaded =
        NativeModuleLoader::load(engine, options.module_path, module);
    if (!loaded.ok()) {
        print_error(
            "REPS2_ENGINE_MODULE_LOAD_FAILED",
            module_load_error_name(loaded.error),
            loaded.platform_error,
            loaded.engine_error == EngineError::none
                ? std::string_view{} : engine_error_name(loaded.engine_error));
        return 4;
    }

    SessionConfig config{};
    config.instruction_budget = options.instruction_budget;
    config.initial_main_ram = std::span<const std::uint8_t>(ram_image);
    config.enable_ee_syscall_hle = options.enable_hle;
    config.enable_instruction_trace = options.enable_trace;
    if (media.stream.is_open()) {
        config.media = HostMediaSource{&media, &read_media_sectors};
    }

    EngineSession session{};
    const SessionStartResult started = session.start(engine, module, config);
    if (!started.ok()) {
        print_error(
            "REPS2_ENGINE_SESSION_START_FAILED",
            session_error_name(started.error),
            0U,
            started.engine_error == EngineError::none
                ? std::string_view{} : engine_error_name(started.engine_error));
        return 5;
    }

    std::vector<SessionRunResult> runs;
    try {
        runs.reserve(options.max_batches);
    } catch (const std::bad_alloc&) {
        print_error("REPS2_ENGINE_OUT_OF_MEMORY", "could not allocate bounded run report");
        return 6;
    }

    const auto start_time = std::chrono::steady_clock::now();
    bool execution_error = false;
    for (std::uint32_t batch = 0U; batch < options.max_batches; ++batch) {
        const SessionRunResult result = session.run();
        runs.push_back(result);
        if (!result.ok()) {
            execution_error = true;
            break;
        }
        if (result.disposition != RunDisposition::budget_exhausted) {
            break;
        }
    }
    const auto finish_time = std::chrono::steady_clock::now();
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        finish_time - start_time).count();

    const auto& state = session.state();
    const bool budget_limit_reached = !runs.empty()
        && runs.back().disposition == RunDisposition::budget_exhausted
        && runs.size() >= options.max_batches;
    const char* outcome = execution_error
        ? "engine_error"
        : (budget_limit_reached
            ? "budget_limit_reached"
            : (session.phase() == SessionPhase::completed
                ? "completed" : "guest_stopped"));

    const NativeTarget target = module.target();
    std::cout << "{\"kind\":\"rg.reps2.engine.run_report.v1\","
              << "\"engine\":{\"name\":";
    json_string(std::cout, kEngineName);
    std::cout << ",\"version\":\"" << kEngineVersion.major << '.'
              << kEngineVersion.minor << '.' << kEngineVersion.patch
              << "\",\"abi_version\":" << kEngineAbiVersion << "},"
              << "\"module\":{\"name\":";
    json_string(std::cout, module.name());
    std::cout << ",\"path\":";
    json_string(std::cout, path_to_utf8(options.module_path));
    std::cout << ",\"target\":{\"architecture\":";
    json_string(std::cout, architecture_name(target.architecture));
    std::cout << ",\"operating_system\":";
    json_string(std::cout, operating_system_name(target.operating_system));
    std::cout << "}},\"input\":{\"ram_image\":";
    if (options.ram_path.empty()) {
        std::cout << "null";
    } else {
        json_string(std::cout, path_to_utf8(options.ram_path));
    }
    std::cout << ",\"ram_bytes\":" << reps2::runtime::kEeMainRamSize
              << ",\"media_image\":";
    if (options.media_path.empty()) {
        std::cout << "null";
    } else {
        json_string(std::cout, path_to_utf8(options.media_path));
    }
    std::cout << ",\"media_sectors\":" << media.sector_count
              << "},\"config\":{\"instruction_budget_per_batch\":"
              << options.instruction_budget
              << ",\"max_batches\":" << options.max_batches
              << ",\"hle_syscalls\":" << (options.enable_hle ? "true" : "false")
              << ",\"instruction_trace\":" << (options.enable_trace ? "true" : "false")
              << "},\"outcome\":";
    json_string(std::cout, outcome);
    std::cout << ",\"session\":{\"phase\":";
    json_string(std::cout, phase_name(session.phase()));
    std::cout << ",\"pc\":" << state.pc
              << ",\"pc_hex\":\"0x";
    constexpr char kHex[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) {
        std::cout.put(kHex[(state.pc >> static_cast<unsigned>(shift)) & 0x0FU]);
    }
    std::cout << "\",\"instructions_executed\":" << state.instructions_executed
              << ",\"run_batches\":" << runs.size()
              << ",\"elapsed_microseconds\":" << elapsed_us
              << ",\"media_present\":" << (state.media.present() ? "true" : "false")
              << ",\"syscalls_seen\":" << state.hle.syscall_count
              << ",\"syscalls_handled\":" << state.hle.syscall_handled_count
              << ",\"gprs\":[";
    for (std::size_t gpr = 0; gpr < 32; ++gpr) {
        if (gpr != 0U) std::cout << ',';
        std::cout << "\"0x";
        for (int shift = 60; shift >= 0; shift -= 4) {
            std::cout.put(kHex[(state.gpr[gpr].low >> static_cast<unsigned>(shift)) & 0x0FU]);
        }
        std::cout << '"';
    }
    std::cout << "],\"timer3\":{\"mode\":" << state.memory.timer3_mode
              << ",\"compare\":" << state.memory.timer3_compare
              << "},\"sif\":{\"rpc_bind_sid\":" << state.hle.sif_cmd.last_rpc_bind_sid
              << ",\"rpc_bind_server\":" << state.hle.sif_cmd.last_rpc_bind_server_sid
              << ",\"rpc_bind_had\":" << (state.hle.sif_cmd.last_rpc_bind_had_server ? 1U : 0U)
              << ",\"rpc_call_server\":" << state.hle.sif_cmd.last_rpc_call_server_sid
              << ",\"rpc_call_number\":" << state.hle.sif_cmd.last_rpc_call_number
              << ",\"rpc_call_replied\":" << (state.hle.sif_cmd.last_rpc_call_replied ? 1U : 0U)
              << ",\"scmd_calls\":" << state.hle.sif_cmd.scmd_call_count
              << ",\"scmd_replies\":" << state.hle.sif_cmd.scmd_replies
              << ",\"scmd_number\":" << state.hle.sif_cmd.last_scmd_number
              << ",\"diskready_calls\":" << state.hle.sif_cmd.diskready_call_count
              << ",\"diskready_replies\":" << state.hle.sif_cmd.diskready_replies
              << ",\"diskready_mode\":" << state.hle.sif_cmd.last_diskready_mode
              << ",\"diskready_retres\":" << state.hle.sif_cmd.last_diskready_retres
              << ",\"diskready_replied\":" << (state.hle.sif_cmd.last_diskready_replied ? 1U : 0U)
              << ",\"ncmd_calls\":" << state.hle.sif_cmd.ncmd_call_count
              << ",\"ncmd_replies\":" << state.hle.sif_cmd.ncmd_replies
              << ",\"ncmd_number\":" << state.hle.sif_cmd.last_ncmd_number
              << ",\"ncmd_retres\":" << state.hle.sif_cmd.last_ncmd_retres
              << ",\"ncmd_replied\":" << (state.hle.sif_cmd.last_ncmd_replied ? 1U : 0U)
              << ",\"searchfile_calls\":" << state.hle.sif_cmd.searchfile_call_count
              << ",\"searchfile_number\":" << state.hle.sif_cmd.last_searchfile_number
              << "}},\"runs\":[";
    for (std::size_t index = 0; index < runs.size(); ++index) {
        if (index != 0U) std::cout << ',';
        const auto& result = runs[index];
        std::cout << "{\"batch\":" << (index + 1U)
                  << ",\"guest_status\":";
        json_string(std::cout, reps2::runtime::status_name(result.guest_status));
        std::cout << ",\"guest_status_code\":"
                  << static_cast<unsigned>(result.guest_status)
                  << ",\"budget_supplied\":" << result.budget_supplied
                  << ",\"instructions_before\":" << result.instructions_before
                  << ",\"instructions_after\":" << result.instructions_after
                  << ",\"instructions_this_run\":" << result.instructions_this_run
                  << ",\"pc_before\":" << result.pc_before
                  << ",\"pc_after\":" << result.pc_after
                  << ",\"invoked\":" << (result.invoked ? "true" : "false")
                  << ",\"monotonic_counter\":"
                  << (result.instruction_counter_monotonic ? "true" : "false");
        if (result.error != SessionError::none) {
            std::cout << ",\"session_error\":";
            json_string(std::cout, session_error_name(result.error));
        }
        if (result.engine_error != EngineError::none) {
            std::cout << ",\"engine_error\":";
            json_string(std::cout, engine_error_name(result.engine_error));
        }
        std::cout << '}';
    }
    std::cout << "],\"instruction_trace_pcs\":[";
    if (options.enable_trace) {
        for (std::uint32_t index = 0; index < state.instruction_trace_count; ++index) {
            if (index != 0U) std::cout << ',';
            std::cout << state.instruction_trace_pcs[index];
        }
    }
    std::cout << "]}\n";

    return execution_error ? 6 : 0;
}

} // namespace

int run_cli(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--version") {
            std::cout << kEngineName << " v" << kEngineVersion.major << '.'
                      << kEngineVersion.minor << '.' << kEngineVersion.patch
                      << " (ABI " << kEngineAbiVersion << ")\n";
            return 0;
        }
        Options options{};
        bool show_help = false;
        if (!parse_options(argc, argv, options, show_help)) {
            print_error(
                "REPS2_ENGINE_INVALID_ARGUMENTS",
                "invalid arguments; run rg-reps2-engine --help");
            return 2;
        }
        if (show_help) {
            print_help();
            return 0;
        }
        return run_engine(options);
    } catch (const std::exception& exception) {
        print_error("REPS2_ENGINE_UNHANDLED_HOST_EXCEPTION", exception.what());
        return 10;
    } catch (...) {
        print_error("REPS2_ENGINE_UNKNOWN_HOST_EXCEPTION", "unknown exception");
        return 10;
    }
}

#if defined(_WIN32)
int wmain(int argc, wchar_t** wide_argv) {
    try {
        std::vector<std::string> utf8_arguments;
        utf8_arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            utf8_arguments.push_back(wide_to_utf8(wide_argv[index]));
        }
        std::vector<char*> argv;
        argv.reserve(utf8_arguments.size());
        for (std::string& argument : utf8_arguments) {
            argv.push_back(argument.data());
        }
        return run_cli(argc, argv.data());
    } catch (const std::exception& exception) {
        print_error("REPS2_ENGINE_UNHANDLED_HOST_EXCEPTION", exception.what());
        return 10;
    } catch (...) {
        print_error("REPS2_ENGINE_UNKNOWN_HOST_EXCEPTION", "unknown exception converting Windows arguments");
        return 10;
    }
}
#else
int main(int argc, char** argv) {
    return run_cli(argc, argv);
}
#endif
