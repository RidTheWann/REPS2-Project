# SPDX-License-Identifier: GPL-3.0-or-later
# Verify the versioned syscall census section of the engine run report (ADR 0170).
#
# The fixture (tests/rg_reps2_engine_census_fixture.cpp) dispatches, in one
# batch: GetThreadId twice from 0x00100C00 and one unserved number from
# 0x00100C10. The checks below are written against those known boundaries, so a
# regression in attribution or accounting fails here rather than in a probe.

foreach(required REPS2_ENGINE_RUNNER REPS2_MODULE REPS2_WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "engine_runner_census_report: missing -D${required}=...")
    endif()
endforeach()

file(REMOVE_RECURSE "${REPS2_WORK_DIR}")
file(MAKE_DIRECTORY "${REPS2_WORK_DIR}")

execute_process(
    COMMAND "${REPS2_ENGINE_RUNNER}" "${REPS2_MODULE}"
        --hle-syscalls --budget 64 --syscall-trace 8
    RESULT_VARIABLE runner_result
    OUTPUT_VARIABLE report
    ERROR_VARIABLE runner_stderr
)
if(NOT runner_result EQUAL 0)
    message(FATAL_ERROR
        "census runner returned ${runner_result}, expected 0\n${report}\n${runner_stderr}")
endif()

string(JSON report_kind GET "${report}" kind)
if(NOT report_kind STREQUAL "rg.reps2.engine.run_report.v1")
    message(FATAL_ERROR "unexpected run report kind: ${report_kind}")
endif()

string(JSON census_version GET "${report}" syscall_census version)
string(JSON attribution GET "${report}" syscall_census attribution)
if(NOT census_version EQUAL 1)
    message(FATAL_ERROR "unexpected census version: ${census_version}")
endif()
if(NOT attribution STREQUAL "space_saving_hot_sites")
    message(FATAL_ERROR "unexpected census attribution: ${attribution}")
endif()

string(JSON total_seen GET "${report}" syscall_census total_seen)
string(JSON total_handled GET "${report}" syscall_census total_handled)
string(JSON overflow_seen GET "${report}" syscall_census overflow_seen)
if(NOT total_seen EQUAL 3 OR NOT total_handled EQUAL 2 OR NOT overflow_seen EQUAL 0)
    message(FATAL_ERROR
        "unexpected census totals: seen=${total_seen} handled=${total_handled} overflow=${overflow_seen}")
endif()

string(JSON service_count LENGTH "${report}" syscall_census services)
if(NOT service_count EQUAL 2)
    message(FATAL_ERROR "expected two serviced census slots, found ${service_count}")
endif()

set(saw_handled FALSE)
set(saw_refused FALSE)
set(service_index 0)
while(service_index LESS service_count)
    string(JSON service_name GET "${report}" syscall_census services ${service_index} name)
    string(JSON service_seen GET "${report}" syscall_census services ${service_index} seen)
    string(JSON service_handled GET "${report}" syscall_census services ${service_index} handled)
    string(JSON service_refused GET "${report}" syscall_census services ${service_index} refused)
    math(EXPR accounted "${service_handled} + ${service_refused}")
    if(NOT accounted EQUAL service_seen)
        message(FATAL_ERROR
            "census slot ${service_index} (${service_name}) has seen=${service_seen} handled=${service_handled} refused=${service_refused}")
    endif()
    if(service_name STREQUAL "GetThreadId")
        if(NOT service_seen EQUAL 2 OR NOT service_handled EQUAL 2 OR NOT service_refused EQUAL 0)
            message(FATAL_ERROR "GetThreadId census slot is wrong: ${service_seen}/${service_handled}/${service_refused}")
        endif()
        set(saw_handled TRUE)
    else()
        if(NOT service_seen EQUAL 1 OR NOT service_handled EQUAL 0 OR NOT service_refused EQUAL 1)
            message(FATAL_ERROR "refused census slot is wrong: ${service_seen}/${service_handled}/${service_refused}")
        endif()
        set(saw_refused TRUE)
    endif()
    math(EXPR service_index "${service_index} + 1")
endwhile()
if(NOT saw_handled OR NOT saw_refused)
    message(FATAL_ERROR "census did not attribute both a serviced and a refused call")
endif()

string(JSON hot_site_count LENGTH "${report}" syscall_census hot_sites)
set(saw_hot_site FALSE)
set(site_index 0)
while(site_index LESS hot_site_count)
    string(JSON site_pc GET "${report}" syscall_census hot_sites ${site_index} pc)
    string(JSON site_count GET "${report}" syscall_census hot_sites ${site_index} count)
    if(site_pc EQUAL 1051648 AND site_count EQUAL 2)
        set(saw_hot_site TRUE)
    endif()
    math(EXPR site_index "${site_index} + 1")
endwhile()
if(NOT saw_hot_site)
    message(FATAL_ERROR "hot-site attribution lost 0x00100C00 with count 2")
endif()

string(JSON trace_capacity GET "${report}" syscall_census trace capacity)
string(JSON trace_retained GET "${report}" syscall_census trace retained)
string(JSON trace_dropped GET "${report}" syscall_census trace dropped)
if(NOT trace_capacity EQUAL 8 OR NOT trace_retained EQUAL 3 OR NOT trace_dropped EQUAL 0)
    message(FATAL_ERROR
        "unexpected trace ring state: capacity=${trace_capacity} retained=${trace_retained} dropped=${trace_dropped}")
endif()

string(JSON first_pc GET "${report}" syscall_census trace events 0 pc)
string(JSON first_path GET "${report}" syscall_census trace events 0 path)
string(JSON last_path GET "${report}" syscall_census trace events 2 path)
string(JSON last_refused GET "${report}" syscall_census trace events 2 refused)
if(NOT first_pc EQUAL 1051648
    OR NOT first_path STREQUAL "host_service"
    OR NOT last_path STREQUAL "unhandled"
    OR NOT last_refused EQUAL 1)
    message(FATAL_ERROR
        "unexpected trace order: first=${first_pc}/${first_path} last=${last_path}/${last_refused}")
endif()

string(JSON last_number GET "${report}" syscall_census last number)
string(JSON last_pc GET "${report}" syscall_census last pc)
if(NOT last_pc EQUAL 1051664)
    message(FATAL_ERROR "census last boundary pc is ${last_pc}, expected 0x00100C10")
endif()
if(last_number LESS 0)
    message(FATAL_ERROR "census last boundary has no service number")
endif()

message(STATUS "RG REPS2 ENGINE runner census report smoke: OK")