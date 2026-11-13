# CBOR code generated from icd/fwt-icd-v1.cddl by zcbor (gen.sh) + zcbor runtime.
# Generated/third-party code: built without our warning set.
add_library(icd STATIC
  gen/icd_encode.c gen/icd_decode.c
  gen/zcbor_common.c gen/zcbor_encode.c gen/zcbor_decode.c
)
target_include_directories(icd SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/gen)
target_compile_options(icd PRIVATE -w)

# Fetch planning: pure logic over the recorder index, unit-tested on the laptop.
add_library(na_fetch STATIC na_fetch.c)
target_include_directories(na_fetch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(na_fetch PUBLIC recorder)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MHD REQUIRED IMPORTED_TARGET libmicrohttpd)
find_package(Threads REQUIRED)

add_executable(nodeagent main.c na_session.c)
target_link_libraries(nodeagent PRIVATE na_fetch icd recorder trigd_core fwt_common
                      PkgConfig::MHD Threads::Threads)
install(TARGETS nodeagent RUNTIME DESTINATION bin)
