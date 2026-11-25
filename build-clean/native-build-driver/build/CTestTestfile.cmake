# CMake generated Testfile for 
# Source directory: D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native
# Build directory: D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test("reps2_generated_probe" "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/build/Debug/reps2_probe.exe")
  set_tests_properties("reps2_generated_probe" PROPERTIES  PASS_REGULAR_EXPRESSION "entry=0x[0-9A-Fa-f]+ status=[0-9]+( stage=[a-z_]+)? pc=0x[0-9A-Fa-f]+ budget=[0-9]+ executed=[0-9]+" WORKING_DIRECTORY "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/.." _BACKTRACE_TRIPLES "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/CMakeLists.txt;279;add_test;D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test("reps2_generated_probe" "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/build/Release/reps2_probe.exe")
  set_tests_properties("reps2_generated_probe" PROPERTIES  PASS_REGULAR_EXPRESSION "entry=0x[0-9A-Fa-f]+ status=[0-9]+( stage=[a-z_]+)? pc=0x[0-9A-Fa-f]+ budget=[0-9]+ executed=[0-9]+" WORKING_DIRECTORY "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/.." _BACKTRACE_TRIPLES "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/CMakeLists.txt;279;add_test;D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test("reps2_generated_probe" "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/build/MinSizeRel/reps2_probe.exe")
  set_tests_properties("reps2_generated_probe" PROPERTIES  PASS_REGULAR_EXPRESSION "entry=0x[0-9A-Fa-f]+ status=[0-9]+( stage=[a-z_]+)? pc=0x[0-9A-Fa-f]+ budget=[0-9]+ executed=[0-9]+" WORKING_DIRECTORY "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/.." _BACKTRACE_TRIPLES "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/CMakeLists.txt;279;add_test;D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test("reps2_generated_probe" "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/build/RelWithDebInfo/reps2_probe.exe")
  set_tests_properties("reps2_generated_probe" PROPERTIES  PASS_REGULAR_EXPRESSION "entry=0x[0-9A-Fa-f]+ status=[0-9]+( stage=[a-z_]+)? pc=0x[0-9A-Fa-f]+ budget=[0-9]+ executed=[0-9]+" WORKING_DIRECTORY "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/.." _BACKTRACE_TRIPLES "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/CMakeLists.txt;279;add_test;D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/src/control_flow_split.native/CMakeLists.txt;0;")
else()
  add_test("reps2_generated_probe" NOT_AVAILABLE)
endif()
