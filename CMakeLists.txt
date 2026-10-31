cmake_minimum_required(VERSION 3.25)

project(REPS2
    VERSION 0.7.0
    DESCRIPTION "Independent PlayStation 2 static recompilation toolchain"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(REPS2_CARGO_EXECUTABLE cargo REQUIRED)

set(REPS2_RUST_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.toml")
set(REPS2_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo-target")
if(WIN32)
    set(REPS2_FRONTEND_EXECUTABLE "${REPS2_CARGO_TARGET_DIR}/release/reps2.exe")
else()
    set(REPS2_FRONTEND_EXECUTABLE "${REPS2_CARGO_TARGET_DIR}/release/reps2")
endif()

file(GLOB_RECURSE REPS2_RUST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/rust/frontend/src/*.rs"
)
list(APPEND REPS2_RUST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.lock"
    "${CMAKE_CURRENT_SOURCE_DIR}/rust/frontend/Cargo.toml"
)

add_custom_command(
    OUTPUT "${REPS2_FRONTEND_EXECUTABLE}"
    COMMAND "${CMAKE_COMMAND}" -E env
        "CARGO_TARGET_DIR=${REPS2_CARGO_TARGET_DIR}"
        "${REPS2_CARGO_EXECUTABLE}" build
        --locked
        --release
        --manifest-path "${REPS2_RUST_MANIFEST}"
    DEPENDS ${REPS2_RUST_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building the REPS2 Rust frontend"
    VERBATIM
)

add_custom_target(reps2 ALL DEPENDS "${REPS2_FRONTEND_EXECUTABLE}")

add_library(reps2_runtime INTERFACE)
add_library(REPS2::Runtime ALIAS reps2_runtime)
target_include_directories(reps2_runtime INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_compile_features(reps2_runtime INTERFACE cxx_std_20)

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME reps2_rust_tests
        COMMAND "${CMAKE_COMMAND}" -E env
            "CARGO_TARGET_DIR=${REPS2_CARGO_TARGET_DIR}"
            "${REPS2_CARGO_EXECUTABLE}" test
            --locked
            --manifest-path "${REPS2_RUST_MANIFEST}"
    )

    set(REPS2_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(REPS2_GENERATED_CPP "${REPS2_GENERATED_DIR}/synthetic_program.cpp")

    add_custom_command(
        OUTPUT "${REPS2_GENERATED_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${REPS2_GENERATED_DIR}"
        COMMAND "${REPS2_FRONTEND_EXECUTABLE}" compile-words
            "${REPS2_GENERATED_CPP}"
            0x00100000
            0x24080005
            0x24090007
            0x01095021
            0x3C0B8000
            0x340C1234
            0x398DFFFF
            0x31AE00FF
            0x00087880
            0x000F8042
            0x000B8FC3
            0x26920001
            0x22B60001
            0x0274B82D
            0x0274C02F
            0x6679FFF0
            0x0014D138
            0x0013D83E
            0x0013E03F
            0x66BD0001
        DEPENDS reps2
        COMMENT "AOT-compiling the synthetic EE block to C++"
        VERBATIM
    )
    set_source_files_properties("${REPS2_GENERATED_CPP}" PROPERTIES GENERATED TRUE)

    add_custom_target(reps2_codegen_fixture DEPENDS "${REPS2_GENERATED_CPP}")
    add_executable(reps2_codegen_smoke
        "${REPS2_GENERATED_CPP}"
        tests/generated_smoke.cpp
    )
    add_dependencies(reps2_codegen_smoke reps2_codegen_fixture)
    target_link_libraries(reps2_codegen_smoke PRIVATE REPS2::Runtime)
    target_compile_features(reps2_codegen_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(reps2_codegen_smoke PRIVATE /W4 /permissive-)
    else()
        target_compile_options(reps2_codegen_smoke PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion
        )
    endif()

    add_test(NAME reps2_codegen_smoke COMMAND reps2_codegen_smoke)

    set(REPS2_CFG_GENERATED_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/control_flow.cpp")
    add_custom_command(
        OUTPUT "${REPS2_CFG_GENERATED_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${REPS2_GENERATED_DIR}"
        COMMAND "${REPS2_FRONTEND_EXECUTABLE}" compile-cfg-fixture
            "${REPS2_CFG_GENERATED_CPP}"
        DEPENDS reps2
        COMMENT "Generating and lowering the synthetic EE control-flow graph"
        VERBATIM
    )
    set_source_files_properties("${REPS2_CFG_GENERATED_CPP}" PROPERTIES GENERATED TRUE)

    add_custom_target(reps2_cfg_codegen_fixture DEPENDS "${REPS2_CFG_GENERATED_CPP}")
    add_executable(reps2_cfg_codegen_smoke
        "${REPS2_CFG_GENERATED_CPP}"
        tests/control_flow_smoke.cpp
    )
    add_dependencies(reps2_cfg_codegen_smoke reps2_cfg_codegen_fixture)
    target_link_libraries(reps2_cfg_codegen_smoke PRIVATE REPS2::Runtime)
    target_compile_features(reps2_cfg_codegen_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(reps2_cfg_codegen_smoke PRIVATE /W4 /permissive-)
    else()
        target_compile_options(reps2_cfg_codegen_smoke PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion
        )
    endif()

    add_test(NAME reps2_cfg_codegen_smoke COMMAND reps2_cfg_codegen_smoke)
endif()

install(PROGRAMS "${REPS2_FRONTEND_EXECUTABLE}" DESTINATION bin)
install(DIRECTORY runtime/include/ DESTINATION include)
