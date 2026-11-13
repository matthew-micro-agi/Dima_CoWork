# Pure logic, also used by the laptop unit tests.
add_library(nodecap_core STATIC nodecap_core.c nc_match.c nc_source_test.c nc_source_emu.c nc_wq.c)
target_include_directories(nodecap_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(nodecap_core PUBLIC fwt_common Threads::Threads)

# GStreamer from the host, or from the SDK sysroot when cross-compiling
# (pkg-config environment set by cmake/j722s-sdk.cmake).
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0)

add_executable(nodecap main.c)
target_link_libraries(nodecap PRIVATE nodecap_core recorder fwt_common PkgConfig::GST)
install(TARGETS nodecap RUNTIME DESTINATION bin)
