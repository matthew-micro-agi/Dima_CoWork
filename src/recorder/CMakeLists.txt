# Recorder: plain functions, linked into nodecap and rec-tool; unit-tested on the laptop.
add_library(recorder STATIC
  recorder.c
  rec_manifest.c
  rec_scan.c
)
target_include_directories(recorder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(recorder PUBLIC fwt_common)
