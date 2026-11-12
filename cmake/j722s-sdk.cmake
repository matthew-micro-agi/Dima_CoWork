# CMake toolchain for the J722S board, using the TI Processor SDK cross-compiler.
# Mirrors linux-devkit/environment-setup-aarch64-oe-linux.
#
# Usage:
#   cmake -S . -B build-board -DCMAKE_TOOLCHAIN_FILE=cmake/j722s-sdk.cmake
# SDK location: environment variable FWT_SDK, default below.

if(DEFINED ENV{FWT_SDK})
  set(FWT_SDK "$ENV{FWT_SDK}")
else()
  set(FWT_SDK "$ENV{HOME}/ti/ti-processor-sdk-linux-adas-j722s-evm-11_02_02_05")
endif()

set(FWT_SDK_HOST   "${FWT_SDK}/linux-devkit/sysroots/x86_64-arago-linux")
set(FWT_SDK_TARGET "${FWT_SDK}/linux-devkit/sysroots/aarch64-oe-linux")

if(NOT EXISTS "${FWT_SDK_TARGET}")
  message(FATAL_ERROR "J722S SDK sysroot not found: ${FWT_SDK_TARGET} (set FWT_SDK)")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_SYSROOT "${FWT_SDK_TARGET}")
set(_bin "${FWT_SDK_HOST}/usr/bin/aarch64-oe-linux/aarch64-oe-linux-")
set(CMAKE_C_COMPILER   "${_bin}gcc")
set(CMAKE_CXX_COMPILER "${_bin}g++")
set(CMAKE_AR           "${_bin}ar" CACHE FILEPATH "")
set(CMAKE_STRIP        "${_bin}strip" CACHE FILEPATH "")

# Same flags as the SDK environment script.
set(CMAKE_C_FLAGS_INIT "-mbranch-protection=standard")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed")

# Search headers/libraries only in the sysroot, programs only on the host.
set(CMAKE_FIND_ROOT_PATH "${FWT_SDK_TARGET}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config resolves against the sysroot (GStreamer, libmicrohttpd later).
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${FWT_SDK_TARGET}")
set(ENV{PKG_CONFIG_LIBDIR} "${FWT_SDK_TARGET}/usr/lib/pkgconfig:${FWT_SDK_TARGET}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
