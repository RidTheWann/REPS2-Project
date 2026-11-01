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
    COMMAND "${REPS2_FRONTEND}" compile-cfg-fixture "${fixture}" --tu-count 4 --native-build
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