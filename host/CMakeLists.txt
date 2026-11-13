# SPDX-License-Identifier: GPL-3.0-or-later
# Layer 06: PC host backend. Provides graphics, audio, input, display and
# platform-window contracts consumed by the runtime through injected services.

add_library(reps2_host STATIC
    src/graphics.cpp
    src/input.cpp
    src/audio.cpp
    src/display.cpp
    src/platform.cpp
)
add_library(REPS2::Host ALIAS reps2_host)
target_include_directories(reps2_host PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_compile_features(reps2_host PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(reps2_host PRIVATE /W4 /permissive-)
else()
    target_compile_options(reps2_host PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(BUILD_TESTING)
    add_executable(reps2_host_smoke tests/host_smoke.cpp)
    target_link_libraries(reps2_host_smoke PRIVATE REPS2::Host)
    target_compile_features(reps2_host_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(reps2_host_smoke PRIVATE /W4 /permissive-)
    else()
        target_compile_options(reps2_host_smoke PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
    add_test(NAME reps2_host_smoke COMMAND reps2_host_smoke)
    set_tests_properties(reps2_host_smoke PROPERTIES PASS_REGULAR_EXPRESSION "all checks passed")
endif()

install(TARGETS reps2_host ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
