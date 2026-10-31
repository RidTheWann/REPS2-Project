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
            0x0022B82D
            0x0022C02F
            0x6439FFF0
            0x0002D138
            0x0001D83E
            0x0001E03F
            0x647D0001
            0x00853004
            0x00853806
            0x0085B007
            0x0085F014
            0x0085F816
            0x00859817
            0x70221C28
            0x70220428
        DEPENDS "${REPS2_FRONTEND_EXECUTABLE}"
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

    set(REPS2_COMPARE_GENERATED_CPP "${REPS2_GENERATED_DIR}/compare_nor_program.cpp")
    add_custom_command(
        OUTPUT "${REPS2_COMPARE_GENERATED_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${REPS2_GENERATED_DIR}"
        COMMAND "${REPS2_FRONTEND_EXECUTABLE}" compile-words
            "${REPS2_COMPARE_GENERATED_CPP}"
            0x0000A000
            0x00856027
            0x0022302A
            0x0022382B
            0x28280001
            0x2849FFFF
            0x2C0AFFFF
            0x2C6BFFFF
            0x0041682A
            0x0041702B
            0x0022002A
            0x2C30FFFF
            0x2C710000
            0x2832FFFF
            0x28538000
            0x2C748000
        DEPENDS "${REPS2_FRONTEND_EXECUTABLE}"
        COMMENT "AOT-compiling the synthetic R5900 compare/NOR block"
        VERBATIM
    )
    set_source_files_properties("${REPS2_COMPARE_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
    add_custom_target(reps2_compare_codegen_fixture DEPENDS "${REPS2_COMPARE_GENERATED_CPP}")
    add_executable(reps2_compare_codegen_smoke
        "${REPS2_COMPARE_GENERATED_CPP}"
        tests/compare_nor_smoke.cpp
    )
    add_dependencies(reps2_compare_codegen_smoke reps2_compare_codegen_fixture)
    target_link_libraries(reps2_compare_codegen_smoke PRIVATE REPS2::Runtime)
    target_compile_features(reps2_compare_codegen_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(reps2_compare_codegen_smoke PRIVATE /W4 /permissive-)
    else()
        target_compile_options(reps2_compare_codegen_smoke PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion
        )
    endif()
    add_test(NAME reps2_compare_codegen_smoke COMMAND reps2_compare_codegen_smoke)

    set(REPS2_SIGNED_DOUBLEWORD_GENERATED_CPP "${REPS2_GENERATED_DIR}/signed_doubleword_program.cpp")
    add_custom_command(
        OUTPUT "${REPS2_SIGNED_DOUBLEWORD_GENERATED_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${REPS2_GENERATED_DIR}"
        COMMAND "${REPS2_FRONTEND_EXECUTABLE}" compile-words
            "${REPS2_SIGNED_DOUBLEWORD_GENERATED_CPP}"
            0x0000B000
            0x0109502C
            0x018D582E
            0x61EEFFFE
            0x0211002C
            0x0253902C
        DEPENDS "${REPS2_FRONTEND_EXECUTABLE}"
        COMMENT "AOT-compiling the synthetic R5900 trapping doubleword arithmetic block"
        VERBATIM
    )
    set_source_files_properties("${REPS2_SIGNED_DOUBLEWORD_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
    add_custom_target(reps2_signed_doubleword_codegen_fixture DEPENDS "${REPS2_SIGNED_DOUBLEWORD_GENERATED_CPP}")
    add_executable(reps2_signed_doubleword_codegen_smoke
        "${REPS2_SIGNED_DOUBLEWORD_GENERATED_CPP}"
        tests/signed_doubleword_smoke.cpp
    )
    add_dependencies(reps2_signed_doubleword_codegen_smoke reps2_signed_doubleword_codegen_fixture)
    target_link_libraries(reps2_signed_doubleword_codegen_smoke PRIVATE REPS2::Runtime)
    target_compile_features(reps2_signed_doubleword_codegen_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(reps2_signed_doubleword_codegen_smoke PRIVATE /W4 /permissive-)
    else()
        target_compile_options(reps2_signed_doubleword_codegen_smoke PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion
        )
    endif()
    add_test(NAME reps2_signed_doubleword_codegen_smoke COMMAND reps2_signed_doubleword_codegen_smoke)

    set(REPS2_HILO_GENERATED_CPP "${REPS2_GENERATED_DIR}/hilo_program.cpp")
    add_custom_command(
        OUTPUT "${REPS2_HILO_GENERATED_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${REPS2_GENERATED_DIR}"
        COMMAND "${REPS2_FRONTEND_EXECUTABLE}" compile-words
            "${REPS2_HILO_GENERATED_CPP}"
            0x00009000
            0x00200011
            0x0000A010
            0x00400013
            0x0000A812
            0x0022B018
            0x0000C010
            0x0000C812
            0x00220018
            0x0000D010
            0x0000D812
            0x0062B819
            0x00853018
            0x00006810
            0x00007012
            0x01095018
            0x00007810
            0x00008012
            0x016C8818
            0x00009010
            0x00009812
            0x00621819
            0x00220818
            0x73800011
            0x73A00013
            0x7000F010
            0x7000F812
        DEPENDS "${REPS2_FRONTEND_EXECUTABLE}"
        COMMENT "AOT-compiling the synthetic R5900 HI/LO and multiply block"
        VERBATIM
    )
    set_source_files_properties("${REPS2_HILO_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
    add_custom_target(reps2_hilo_codegen_fixture DEPENDS "${REPS2_HILO_GENERATED_CPP}")
    add_executable(reps2_hilo_codegen_smoke
        "${REPS2_HILO_GENERATED_CPP}"
        tests/hilo_codegen_smoke.cpp
    )
    add_dependencies(reps2_hilo_codegen_smoke reps2_hilo_codegen_fixture)
    target_link_libraries(reps2_hilo_codegen_smoke PRIVATE REPS2::Runtime)
    target_compile_features(reps2_hilo_codegen_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(reps2_hilo_codegen_smoke PRIVATE /W4 /permissive-)
    else()
        target_compile_options(reps2_hilo_codegen_smoke PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion
        )
    endif()
    add_test(NAME reps2_hilo_codegen_smoke COMMAND reps2_hilo_codegen_smoke)

    set(REPS2_SA_GENERATED_CPP "${REPS2_GENERATED_DIR}/sa_program.cpp")
    add_custom_command(
        OUTPUT "${REPS2_SA_GENERATED_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${REPS2_GENERATED_DIR}"
        COMMAND "${REPS2_FRONTEND_EXECUTABLE}" compile-words
            "${REPS2_SA_GENERATED_CPP}"
            0x0000C000
            0x00200029  # mtsa   $1        SA <- GPR[1][63:0]
            0x00000028  # mfsa   $0        non-zero SA makes a missing
                         # mfsa $0 guard observable
            0x00001028  # mfsa   $2        GPR[2][63:0] <- SA
            0x0478000F  # mtsab  $3, 0x0F  SA <- (GPR[3][3:0] ^ 0xF) * 8
            0x70A736E8  # qfsrv  $6, $5, $7 funnel shift by SA = 88
            0x00004828  # mfsa   $9        SA is read, not consumed
            0x05190007  # mtsah  $8, 0x07  SA <- (GPR[8][2:0] ^ 0x7) * 16
            0x70A756E8  # qfsrv  $10, $5, $7 funnel shift by SA = 64
            0x05780005  # mtsab  $11, 0x05 SA <- (0x5 ^ 0x5) * 8 = 0
            0x70A766E8  # qfsrv  $12, $5, $7 funnel shift by SA = 0
            0x04780003  # mtsab  $3, 0x03  SA <- (0x4 ^ 0x3) * 8 = 56
        DEPENDS "${REPS2_FRONTEND_EXECUTABLE}"
        COMMENT "AOT-compiling the synthetic R5900 MMI SA and QFSRV block"
        VERBATIM
    )
    set_source_files_properties("${REPS2_SA_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
    add_custom_target(reps2_sa_codegen_fixture DEPENDS "${REPS2_SA_GENERATED_CPP}")
    add_executable(reps2_sa_codegen_smoke
        "${REPS2_SA_GENERATED_CPP}"
        tests/sa_codegen_smoke.cpp
    )
    add_dependencies(reps2_sa_codegen_smoke reps2_sa_codegen_fixture)
    target_link_libraries(reps2_sa_codegen_smoke PRIVATE REPS2::Runtime)
    target_compile_features(reps2_sa_codegen_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(reps2_sa_codegen_smoke PRIVATE /W4 /permissive-)
    else()
        target_compile_options(reps2_sa_codegen_smoke PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion
        )
    endif()
    add_test(NAME reps2_sa_codegen_smoke COMMAND reps2_sa_codegen_smoke)

    set(REPS2_COP1_GENERATED_CPP "${REPS2_GENERATED_DIR}/cop1_program.cpp")
    add_custom_command(
        OUTPUT "${REPS2_COP1_GENERATED_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${REPS2_GENERATED_DIR}"
        COMMAND "${REPS2_FRONTEND_EXECUTABLE}" compile-words
            "${REPS2_COP1_GENERATED_CPP}"
            0x0000E000
            0x44813000  # mtc1   $1, $f6   FPR[6] <- GPR[1][31:0]
            0x44033000  # mfc1   $3, $f6   sign-extended, upper lane kept
            0x44003000  # mfc1   $0, $f6   FPR[6] is non-zero here, so a
                         # missing zero-guard is observable
            0xE5060040  # swc1   $f6, 0x40($8)
            0xC5000040  # lwc1   $f0, 0x40($8)  FPR[0] is a real register
            0x44040000  # mfc1   $4, $f0   read FPR[0] back
            0x44820000  # mtc1   $2, $f0   MTC1 must write FPR[0], not skip it
            0x440A0000  # mfc1   $10, $f0
            0x44800000  # mtc1   $zero, $f0 the first unsupported word found
                         # in every retail entry scan so far
            0x440B0000  # mfc1   $11, $f0
            0xC527FFFC  # lwc1   $f7, -4($9)  signed offset to the same word
            0x44073800  # mfc1   $7, $f7
            0x44450000  # cfc1   $5, $0    FIR is a read-only constant
            0x44C2F800  # ctc1   $2, $31   FCSR <- GPR[2][31:0] with bits 1..0 forced
            0x4446F800  # cfc1   $6, $31   read FCSR back
        DEPENDS "${REPS2_FRONTEND_EXECUTABLE}"
        COMMENT "AOT-compiling the synthetic R5900 COP1 data-movement block"
        VERBATIM
    )
    set_source_files_properties("${REPS2_COP1_GENERATED_CPP}" PROPERTIES GENERATED TRUE)
    add_custom_target(reps2_cop1_codegen_fixture DEPENDS "${REPS2_COP1_GENERATED_CPP}")
    add_executable(reps2_cop1_codegen_smoke
        "${REPS2_COP1_GENERATED_CPP}"
        tests/cop1_codegen_smoke.cpp
    )
    add_dependencies(reps2_cop1_codegen_smoke reps2_cop1_codegen_fixture)
    target_link_libraries(reps2_cop1_codegen_smoke PRIVATE REPS2::Runtime)
    target_compile_features(reps2_cop1_codegen_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(reps2_cop1_codegen_smoke PRIVATE /W4 /permissive-)
    else()
        target_compile_options(reps2_cop1_codegen_smoke PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion
        )
    endif()
    add_test(NAME reps2_cop1_codegen_smoke COMMAND reps2_cop1_codegen_smoke)

    set(REPS2_CFG_GENERATED_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/control_flow.cpp")
    add_custom_command(
        OUTPUT "${REPS2_CFG_GENERATED_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${REPS2_GENERATED_DIR}"
        COMMAND "${REPS2_FRONTEND_EXECUTABLE}" compile-cfg-fixture
            "${REPS2_CFG_GENERATED_CPP}"
        DEPENDS "${REPS2_FRONTEND_EXECUTABLE}"
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
