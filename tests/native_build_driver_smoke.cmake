# SPDX-License-Identifier: GPL-3.0-or-later
# Configures, builds and runs a native project emitted by --native-build.

foreach(required REPS2_FRONTEND REPS2_RUNTIME_INCLUDE REPS2_ENGINE_INCLUDE REPS2_ENGINE_LOADER_SMOKE REPS2_ENGINE_RUNNER REPS2_WORK_DIR REPS2_CMAKE REPS2_CTEST)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "native_build_driver_smoke: missing -D${required}=...")
    endif()
endforeach()

file(REMOVE_RECURSE "${REPS2_WORK_DIR}")
file(MAKE_DIRECTORY "${REPS2_WORK_DIR}/src")

set(fixture "${REPS2_WORK_DIR}/src/control_flow_split.cpp")
execute_process(
    COMMAND "${REPS2_FRONTEND}" compile-cfg-fixture "${fixture}" --tu-count 4 --native-build --probe-budget 64
    RESULT_VARIABLE frontend_result
    OUTPUT_VARIABLE frontend_output
    ERROR_VARIABLE frontend_error
)
if(NOT frontend_result EQUAL 0)
    message(FATAL_ERROR
        "frontend generation failed (${frontend_result})\n${frontend_output}\n${frontend_error}")
endif()

set(native_source "${REPS2_WORK_DIR}/src/control_flow_split.native")
set(native_build "${REPS2_WORK_DIR}/build")
if(NOT EXISTS "${native_source}/CMakeLists.txt")
    message(FATAL_ERROR "frontend did not write ${native_source}/CMakeLists.txt")
endif()
if(NOT EXISTS "${native_source}/probe_main.cpp")
    message(FATAL_ERROR "frontend did not write ${native_source}/probe_main.cpp")
endif()
if(NOT EXISTS "${native_source}/rg_reps2_module.cpp")
    message(FATAL_ERROR "frontend did not write ${native_source}/rg_reps2_module.cpp")
endif()

execute_process(
    COMMAND "${REPS2_CMAKE}" -S "${native_source}" -B "${native_build}"
        "-DREPS2_RUNTIME_INCLUDE=${REPS2_RUNTIME_INCLUDE}"
        "-DREPS2_ENGINE_INCLUDE=${REPS2_ENGINE_INCLUDE}"
        "-DCMAKE_BUILD_TYPE=Release"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "native configure failed (${configure_result})\n${configure_output}\n${configure_error}")
endif()

set(toolchain_report "${native_build}/reps2-native-toolchain-Release.json")
if(NOT EXISTS "${toolchain_report}")
    message(FATAL_ERROR "native configure did not emit ${toolchain_report}")
endif()
file(READ "${toolchain_report}" toolchain_json)
string(JSON toolchain_schema GET "${toolchain_json}" schema_version)
string(JSON compiler_id GET "${toolchain_json}" compiler id)
string(JSON compiler_version GET "${toolchain_json}" compiler version)
string(JSON configuration GET "${toolchain_json}" cmake configuration)
string(JSON cxx_standard GET "${toolchain_json}" compile cxx_standard)
string(JSON compile_option_count LENGTH "${toolchain_json}" compile target_options)
string(JSON compiler_sha256 GET "${toolchain_json}" compiler executable_sha256)
string(JSON runtime_abi_version GET "${toolchain_json}" runtime abi_version)
string(JSON runtime_headers_sha256 GET "${toolchain_json}" runtime headers_sha256)
string(JSON manifest_schema_version GET "${toolchain_json}" frontend manifest_schema_version)
string(JSON control_ir_schema_version GET "${toolchain_json}" frontend control_ir_schema_version)
string(JSON input_kind GET "${toolchain_json}" frontend input_kind)
string(JSON translation_sources_sha256 GET "${toolchain_json}" frontend translation_sources_sha256)
string(JSON parent_iso_sha256 GET "${toolchain_json}" frontend parent_iso_sha256)
string(JSON frontend_version GET "${toolchain_json}" frontend_version)
if(NOT toolchain_schema EQUAL 1)
    message(FATAL_ERROR "unexpected native toolchain report schema: ${toolchain_schema}")
endif()
if(NOT frontend_version MATCHES "^[0-9]+[.][0-9]+[.][0-9]+")
    message(FATAL_ERROR "native toolchain report has invalid frontend_version '${frontend_version}'")
endif()
if(compiler_id STREQUAL "" OR compiler_version STREQUAL "")
    message(FATAL_ERROR "native toolchain report omitted compiler identity/version")
endif()
if(NOT configuration STREQUAL "Release")
    message(FATAL_ERROR "native toolchain report has configuration '${configuration}', expected Release")
endif()
if(NOT cxx_standard EQUAL 20 OR compile_option_count LESS 1)
    message(FATAL_ERROR "native toolchain report omitted C++ standard or compile options")
endif()
foreach(digest compiler_sha256 runtime_headers_sha256 translation_sources_sha256)
    string(LENGTH "${${digest}}" digest_length)
    if(NOT digest_length EQUAL 64 OR NOT "${${digest}}" MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR "native toolchain report has invalid ${digest}")
    endif()
endforeach()
if(parent_iso_sha256 STREQUAL "")
    message(STATUS "native toolchain report parent_iso_sha256 correctly null for synthetic fixture")
else()
    string(LENGTH "${parent_iso_sha256}" parent_iso_length)
    if(NOT parent_iso_length EQUAL 64 OR NOT "${parent_iso_sha256}" MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR "native toolchain report has invalid parent_iso_sha256")
    endif()
endif()
if(NOT runtime_abi_version EQUAL 3 OR manifest_schema_version LESS 1 OR control_ir_schema_version LESS 1)
    message(FATAL_ERROR "native toolchain report omitted runtime/frontend schema versions")
endif()
if(NOT input_kind STREQUAL "synthetic_fixture")
    message(FATAL_ERROR "synthetic smoke input kind mismatch: ${input_kind}")
endif()

execute_process(
    COMMAND "${REPS2_CMAKE}" --build "${native_build}" --config Release
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "native build failed (${build_result})\n${build_output}\n${build_error}")
endif()

set(identity_report "${native_build}/reps2-native-build-identity-Release.json")
if(NOT EXISTS "${identity_report}")
    message(FATAL_ERROR "native build did not emit its configuration-specific build identity")
endif()

set(module_path_report "${native_build}/reps2-native-module-Release.path")
if(NOT EXISTS "${module_path_report}")
    message(FATAL_ERROR "native configure did not emit the generated module path")
endif()
file(READ "${module_path_report}" generated_module_path)
string(STRIP "${generated_module_path}" generated_module_path)
if(NOT EXISTS "${generated_module_path}")
    message(FATAL_ERROR "native shared module was not built at '${generated_module_path}'")
endif()
execute_process(
    COMMAND "${REPS2_ENGINE_LOADER_SMOKE}" "${generated_module_path}" --generated
    RESULT_VARIABLE module_load_result
    OUTPUT_VARIABLE module_load_output
    ERROR_VARIABLE module_load_error
)
if(NOT module_load_result EQUAL 0)
    message(FATAL_ERROR
        "RG REPS2 ENGINE failed to load/run generated native module (${module_load_result})\n${module_load_output}\n${module_load_error}")
endif()

# Exercise the installed-style runner against that exact generated AOT library.
execute_process(
    COMMAND "${REPS2_ENGINE_RUNNER}" "${generated_module_path}"
        --budget 64 --max-batches 1 --instruction-trace
    RESULT_VARIABLE runner_result
    OUTPUT_VARIABLE runner_output
    ERROR_VARIABLE runner_error
)
if(NOT runner_result EQUAL 0)
    message(FATAL_ERROR
        "RG REPS2 ENGINE runner failed (${runner_result})\n${runner_output}\n${runner_error}")
endif()
string(JSON runner_kind GET "${runner_output}" kind)
string(JSON runner_engine_name GET "${runner_output}" engine name)
string(JSON runner_module_name GET "${runner_output}" module name)
string(JSON runner_executed GET "${runner_output}" session instructions_executed)
string(JSON runner_pc_hex GET "${runner_output}" session pc_hex)
string(JSON runner_timer_mode GET "${runner_output}" session timer3 mode)
string(JSON runner_timer_compare GET "${runner_output}" session timer3 compare)
string(JSON runner_timer_count GET "${runner_output}" session timer3 count)
if(NOT runner_kind STREQUAL "rg.reps2.engine.run_report.v1"
    OR NOT runner_engine_name STREQUAL "RG REPS2 ENGINE"
    OR runner_module_name STREQUAL "")
    message(FATAL_ERROR "RG REPS2 ENGINE runner returned an invalid report: ${runner_output}")
endif()
if(runner_executed LESS_EQUAL 0 OR NOT runner_pc_hex MATCHES "^0x[0-9A-Fa-f]+$")
    message(FATAL_ERROR "RG REPS2 ENGINE runner did not report real guest execution: ${runner_output}")
endif()
if(NOT runner_timer_mode EQUAL 0 OR NOT runner_timer_compare EQUAL 0 OR NOT runner_timer_count EQUAL 0)
    message(FATAL_ERROR "RG REPS2 ENGINE runner reported unexpected Timer 3 state: ${runner_output}")
endif()

file(READ "${identity_report}" identity_json)
string(JSON identity_schema GET "${identity_json}" schema_version)
string(JSON identity_kind GET "${identity_json}" kind)
string(JSON cache_key_sha256 GET "${identity_json}" cache_key_sha256)
string(SHA256 expected_cache_key_sha256 "${toolchain_json}")
string(LENGTH "${cache_key_sha256}" cache_key_length)
if(NOT identity_schema EQUAL 1 OR NOT identity_kind STREQUAL "reps2.native_build_identity")
    message(FATAL_ERROR "unexpected native build identity schema or kind")
endif()
if(NOT cache_key_length EQUAL 64 OR NOT cache_key_sha256 STREQUAL expected_cache_key_sha256)
    message(FATAL_ERROR "native build identity key does not match the toolchain report SHA-256")
endif()
string(FIND "${toolchain_json}" "${REPS2_RUNTIME_INCLUDE}" runtime_path_index)
if(NOT runtime_path_index EQUAL -1)
    message(FATAL_ERROR "native toolchain report must not expose an absolute runtime include path")
endif()

execute_process(
    COMMAND "${REPS2_CTEST}" --test-dir "${native_build}" -C Release --output-on-failure
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error
)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR
        "generated probe failed (${test_result})\n${test_output}\n${test_error}")
endif()

message(STATUS "native build driver smoke: OK")