# SPDX-License-Identifier: GPL-3.0-or-later
# Configures, builds and runs a native project emitted by --native-build.

foreach(required REPS2_FRONTEND REPS2_RUNTIME_INCLUDE REPS2_WORK_DIR REPS2_CMAKE REPS2_CTEST)
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

execute_process(
    COMMAND "${REPS2_CMAKE}" -S "${native_source}" -B "${native_build}"
        "-DREPS2_RUNTIME_INCLUDE=${REPS2_RUNTIME_INCLUDE}"
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
if(NOT toolchain_schema EQUAL 1)
    message(FATAL_ERROR "unexpected native toolchain report schema: ${toolchain_schema}")
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
if(NOT runtime_abi_version EQUAL 1 OR manifest_schema_version LESS 1 OR control_ir_schema_version LESS 1)
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