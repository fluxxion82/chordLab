# Unit tests for the plug-in shell's pure C++ helpers (ChordLab/bridge). Standalone: the helpers are
# header-only and need only the engine's headers (PreviewCmd, kJsonSchema), nlohmann and doctest, so
# nothing from the engine is compiled or linked here.
#   cmake -S ChordLab/tests -B ChordLab/build-bridge-tests && cmake --build ChordLab/build-bridge-tests
#   (cd ChordLab/build-bridge-tests && ctest --output-on-failure)
cmake_minimum_required(VERSION 3.20)
project(chordlab_bridge_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
set(CHORDLAB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../engine)
file(GLOB BRIDGE_TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(bridge_tests ${BRIDGE_TEST_SRC})
target_include_directories(bridge_tests PRIVATE
  ${CHORDLAB_DIR}                 # "bridge/..."
  ${ENGINE_DIR}/include           # "chordlab/..."
  ${ENGINE_DIR}/third_party       # <nlohmann/json.hpp>
  ${ENGINE_DIR}/tests)            # "doctest.h"
if(NOT MSVC)
  target_compile_options(bridge_tests PRIVATE -Wall -Wextra -Wpedantic)   # as engine/CMakeLists.txt
else()
  target_compile_options(bridge_tests PRIVATE /W4)
endif()
add_test(NAME bridge_tests COMMAND bridge_tests)
