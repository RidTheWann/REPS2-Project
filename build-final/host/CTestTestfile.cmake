# CMake generated Testfile for 
# Source directory: D:/REPS2-Project/.worktrees/ee-vblank-timing/host
# Build directory: D:/REPS2-Project/.worktrees/ee-vblank-timing/build-final/host
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test("reps2_host_smoke" "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-final/host/Debug/reps2_host_smoke.exe")
  set_tests_properties("reps2_host_smoke" PROPERTIES  PASS_REGULAR_EXPRESSION "all checks passed" _BACKTRACE_TRIPLES "D:/REPS2-Project/.worktrees/ee-vblank-timing/host/CMakeLists.txt;33;add_test;D:/REPS2-Project/.worktrees/ee-vblank-timing/host/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test("reps2_host_smoke" "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-final/host/Release/reps2_host_smoke.exe")
  set_tests_properties("reps2_host_smoke" PROPERTIES  PASS_REGULAR_EXPRESSION "all checks passed" _BACKTRACE_TRIPLES "D:/REPS2-Project/.worktrees/ee-vblank-timing/host/CMakeLists.txt;33;add_test;D:/REPS2-Project/.worktrees/ee-vblank-timing/host/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test("reps2_host_smoke" "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-final/host/MinSizeRel/reps2_host_smoke.exe")
  set_tests_properties("reps2_host_smoke" PROPERTIES  PASS_REGULAR_EXPRESSION "all checks passed" _BACKTRACE_TRIPLES "D:/REPS2-Project/.worktrees/ee-vblank-timing/host/CMakeLists.txt;33;add_test;D:/REPS2-Project/.worktrees/ee-vblank-timing/host/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test("reps2_host_smoke" "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-final/host/RelWithDebInfo/reps2_host_smoke.exe")
  set_tests_properties("reps2_host_smoke" PROPERTIES  PASS_REGULAR_EXPRESSION "all checks passed" _BACKTRACE_TRIPLES "D:/REPS2-Project/.worktrees/ee-vblank-timing/host/CMakeLists.txt;33;add_test;D:/REPS2-Project/.worktrees/ee-vblank-timing/host/CMakeLists.txt;0;")
else()
  add_test("reps2_host_smoke" NOT_AVAILABLE)
endif()
