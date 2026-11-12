# SPDX-License-Identifier: GPL-3.0-or-later
# Verify stable JSON error envelopes and non-zero exit codes from the runner.

foreach(required REPS2_ENGINE_RUNNER REPS2_WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "engine_runner_error_smoke: missing -D${required}=...")
    endif()
endforeach()

file(REMOVE_RECURSE "${REPS2_WORK_DIR}")
file(MAKE_DIRECTORY "${REPS2_WORK_DIR}")

# No module argument must be a structured usage error, never an empty or
# human-only stderr response.
execute_process(
    COMMAND "${REPS2_ENGINE_RUNNER}"
    RESULT_VARIABLE arguments_result
    OUTPUT_VARIABLE arguments_output
    ERROR_VARIABLE arguments_stderr
)
if(NOT arguments_result EQUAL 2)
    message(FATAL_ERROR
        "runner without arguments returned ${arguments_result}, expected 2\n${arguments_output}\n${arguments_stderr}")
endif()
string(JSON arguments_kind GET "${arguments_output}" kind)
string(JSON arguments_code GET "${arguments_output}" error code)
if(NOT arguments_kind STREQUAL "rg.reps2.engine.error.v1"
    OR NOT arguments_code STREQUAL "REPS2_ENGINE_INVALID_ARGUMENTS")
    message(FATAL_ERROR "unexpected invalid-arguments JSON: ${arguments_output}")
endif()

# A validly parsed command with an absent module path must reach the module
# loader, fail closed, and still produce syntactically valid JSON.
set(missing_module "${REPS2_WORK_DIR}/missing-native-module")
execute_process(
    COMMAND "${REPS2_ENGINE_RUNNER}" "${missing_module}"
    RESULT_VARIABLE module_result
    OUTPUT_VARIABLE module_output
    ERROR_VARIABLE module_stderr
)
if(NOT module_result EQUAL 4)
    message(FATAL_ERROR
        "runner missing-module returned ${module_result}, expected 4\n${module_output}\n${module_stderr}")
endif()
string(JSON module_kind GET "${module_output}" kind)
string(JSON module_code GET "${module_output}" error code)
string(JSON module_message GET "${module_output}" error message)
if(NOT module_kind STREQUAL "rg.reps2.engine.error.v1"
    OR NOT module_code STREQUAL "REPS2_ENGINE_MODULE_LOAD_FAILED"
    OR module_message STREQUAL "")
    message(FATAL_ERROR "unexpected module-load error JSON: ${module_output}")
endif()

# Invalid budgets are rejected before path loading and retain their own stable
# error code, so an agent can distinguish bad arguments from missing artifacts.
execute_process(
    COMMAND "${REPS2_ENGINE_RUNNER}" "${missing_module}" --budget 0
    RESULT_VARIABLE budget_result
    OUTPUT_VARIABLE budget_output
    ERROR_VARIABLE budget_stderr
)
if(NOT budget_result EQUAL 2)
    message(FATAL_ERROR
        "runner zero-budget returned ${budget_result}, expected 2\n${budget_output}\n${budget_stderr}")
endif()
string(JSON budget_kind GET "${budget_output}" kind)
string(JSON budget_code GET "${budget_output}" error code)
if(NOT budget_kind STREQUAL "rg.reps2.engine.error.v1"
    OR NOT budget_code STREQUAL "REPS2_ENGINE_INVALID_ARGUMENTS")
    message(FATAL_ERROR "unexpected zero-budget JSON: ${budget_output}")
endif()

message(STATUS "RG REPS2 ENGINE runner error JSON smoke: OK")
