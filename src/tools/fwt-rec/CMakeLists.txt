# fwt-rec: one recording on one node from a laptop or any Linux box; the start
# of the controller code (ctl_node = protocol, ctl_http = transport). Not part
# of the node bundle (no install rule). Needs libcurl: laptop
# libcurl4-openssl-dev; the SDK sysroot has it.
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL IMPORTED_TARGET libcurl)

# Protocol + sidecar: pure, unit-tested on the laptop.
add_library(fwtrec_core STATIC ctl_node.c rec_json.c)
target_include_directories(fwtrec_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fwtrec_core PUBLIC icd)

if(CURL_FOUND)
  add_executable(fwt-rec main.c ctl_http.c)
  target_link_libraries(fwt-rec PRIVATE fwtrec_core fwt_common PkgConfig::CURL)
else()
  message(STATUS "fwt-rec not built: libcurl not found (laptop: apt install libcurl4-openssl-dev)")
endif()
