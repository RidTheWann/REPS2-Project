cmake_minimum_required(VERSION 3.25)

project(REPS2
    VERSION 0.1.0
    DESCRIPTION "Independent PlayStation 2 static recompilation research project"
    LANGUAGES CXX
)

option(REPS2_BUILD_TESTS "Build REPS2 tests" ON)

add_library(reps2_core
    src/elf.cpp
    src/mips.cpp
)
add_library(REPS2::Core ALIAS reps2_core)

target_include_directories(reps2_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(reps2_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(reps2_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(reps2_core PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
endif()

add_executable(reps2_cli src/main.cpp)
set_target_properties(reps2_cli PROPERTIES OUTPUT_NAME reps2)
target_link_libraries(reps2_cli PRIVATE REPS2::Core)
target_compile_features(reps2_cli PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(reps2_cli PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(reps2_cli PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
endif()

if(REPS2_BUILD_TESTS)
    enable_testing()
    add_executable(reps2_tests tests/test_main.cpp)
    target_link_libraries(reps2_tests PRIVATE REPS2::Core)
    target_compile_features(reps2_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(reps2_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(reps2_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        )
    endif()
    add_test(NAME reps2_tests COMMAND reps2_tests)
endif()
