# vst3host: a command-line VST3 host that loads a built ChordLab.vst3 in-process and runs the shell's scenarios
# (per-track instruments plan, M2 Task 8b). macOS only. Built from the VST3 SDK's hosting sources that iPlug2
# vendors, plus the engine (to build the states it hands the plug-in and to compute the notes it must play), plus
# editor.mm (the editor's way of sending the plug-in a message).
#   cmake -S ChordLab/Tests/vst3host -B <build dir> && cmake --build <build dir>
#   <build dir>/vst3host <path to ChordLab.vst3>
cmake_minimum_required(VERSION 3.20)
project(chordlab_vst3host C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(CHORDLAB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(ENGINE_DIR ${CHORDLAB_DIR}/../engine)

# The iPlug2 checkout, found as the Xcode project finds it: IPLUG2_ROOT in ChordLab/config/ChordLab-mac.xcconfig,
# relative to ChordLab/projects (in a worktree that reaches the .worktrees/iPlug2 symlink). -DIPLUG2_ROOT=<path>
# overrides it. Read only: nothing is ever written there.
if(NOT IPLUG2_ROOT)
  file(STRINGS ${CHORDLAB_DIR}/config/ChordLab-mac.xcconfig IPLUG2_LINE REGEX "^IPLUG2_ROOT *=")
  if(NOT IPLUG2_LINE)
    message(FATAL_ERROR "vst3host: no IPLUG2_ROOT line in ChordLab/config/ChordLab-mac.xcconfig; pass -DIPLUG2_ROOT=<path>")
  endif()
  string(REGEX REPLACE "^IPLUG2_ROOT *= *" "" IPLUG2_RELATIVE "${IPLUG2_LINE}")
  string(STRIP "${IPLUG2_RELATIVE}" IPLUG2_RELATIVE)
  get_filename_component(IPLUG2_ROOT "${CHORDLAB_DIR}/projects/${IPLUG2_RELATIVE}" ABSOLUTE)
endif()
set(VST3_SDK ${IPLUG2_ROOT}/Dependencies/IPlug/VST3_SDK)
if(NOT EXISTS ${VST3_SDK}/public.sdk/source/vst/hosting/module.h)
  message(FATAL_ERROR "vst3host: no VST3 SDK at ${VST3_SDK} (IPLUG2_ROOT=${IPLUG2_ROOT})")
endif()
message(STATUS "vst3host: VST3 SDK at ${VST3_SDK}")

# The host keeps every symbol of its own private: the plug-in carries its own copy of the engine, and an exported
# weak definition here could be coalesced with the plug-in's by the dynamic loader.
set(HIDDEN -fvisibility=hidden -fvisibility-inlines-hidden)

add_library(vst3host_sdk STATIC
  ${VST3_SDK}/pluginterfaces/base/conststringtable.cpp
  ${VST3_SDK}/pluginterfaces/base/coreiids.cpp
  ${VST3_SDK}/pluginterfaces/base/funknown.cpp
  ${VST3_SDK}/pluginterfaces/base/ustring.cpp
  ${VST3_SDK}/public.sdk/source/common/commonstringconvert.cpp
  ${VST3_SDK}/public.sdk/source/common/memorystream.cpp
  ${VST3_SDK}/public.sdk/source/vst/hosting/eventlist.cpp
  ${VST3_SDK}/public.sdk/source/vst/hosting/hostclasses.cpp
  ${VST3_SDK}/public.sdk/source/vst/hosting/module.cpp
  ${VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm
  ${VST3_SDK}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
  ${VST3_SDK}/public.sdk/source/vst/utility/stringconvert.cpp
  ${VST3_SDK}/public.sdk/source/vst/vstinitiids.cpp)
target_include_directories(vst3host_sdk SYSTEM PUBLIC ${VST3_SDK})   # SYSTEM: its headers' warnings are not ours
target_compile_definitions(vst3host_sdk PUBLIC $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
target_compile_options(vst3host_sdk PRIVATE ${HIDDEN} -w)   # third-party: its warnings are not ours
set_source_files_properties(${VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm
  PROPERTIES COMPILE_OPTIONS "-fobjc-arc")                    # the SDK's module_mac.mm requires ARC
target_link_libraries(vst3host_sdk PUBLIC "-framework Cocoa" "-framework CoreFoundation")

file(GLOB ENGINE_SRC CONFIGURE_DEPENDS ${ENGINE_DIR}/src/*.cpp)
add_library(vst3host_engine STATIC ${ENGINE_SRC})
target_include_directories(vst3host_engine PUBLIC ${ENGINE_DIR}/include PRIVATE ${ENGINE_DIR}/third_party)
target_compile_options(vst3host_engine PRIVATE ${HIDDEN})

# editor.mm opens an instance's editor and sends it messages as the Swift editor does (editor.h); it reads the message
# tags from ChordLab/ChordLab-Shared.h.
add_executable(vst3host main.cpp editor.mm)
target_include_directories(vst3host PRIVATE ${ENGINE_DIR}/third_party ${CHORDLAB_DIR})
set_source_files_properties(editor.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_compile_definitions(vst3host PRIVATE CHORDLAB_ENGINE_FIXTURE_DIR="${ENGINE_DIR}/tests/fixtures")
target_compile_options(vst3host PRIVATE ${HIDDEN} -Wall -Wextra)
target_link_libraries(vst3host PRIVATE vst3host_sdk vst3host_engine)
