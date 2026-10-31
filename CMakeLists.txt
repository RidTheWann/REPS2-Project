cmake_minimum_required(VERSION 3.25)

project(REPS2
    VERSION 0.2.0
    DESCRIPTION "Independent PlayStation 2 static recompilation toolchain"
    LANGUAGES NONE
)

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
endif()

install(PROGRAMS "${REPS2_FRONTEND_EXECUTABLE}" DESTINATION bin)
