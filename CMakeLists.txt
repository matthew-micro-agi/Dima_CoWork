cmake_minimum_required(VERSION 3.20)
project(fwtrial VERSION 0.1.0 LANGUAGES C)

# Two builds of the same code (decision log DL-05):
#   laptop: cmake -S . -B build-host
#   board:  cmake -S . -B build-board -DCMAKE_TOOLCHAIN_FILE=cmake/j722s-sdk.cmake

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)            # gnu11: clock_gettime, POSIX
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wshadow -Wformat=2 -Wundef)

configure_file(src/common/fwt_version.h.in
               ${CMAKE_BINARY_DIR}/generated/fwt_version.h @ONLY)

add_subdirectory(src/common)
add_subdirectory(src/trigd)
add_subdirectory(src/recorder)
add_subdirectory(src/nodecap)
add_subdirectory(src/nodeagent)
add_subdirectory(src/tools/fwt-info)
add_subdirectory(src/tools/trig-listen)
add_subdirectory(src/tools/rec-tool)
if(NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(src/tools/sei-dump)   # laptop only (uses ffmpeg)
endif()

# Unit tests run on the laptop only (not when cross-compiling).
if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(tests)
endif()
