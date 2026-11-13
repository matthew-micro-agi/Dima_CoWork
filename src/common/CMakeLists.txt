add_library(fwt_common STATIC
  fwt_cam_proto.c
  fwt_clock.c
  fwt_index.c
  fwt_seqcheck.c
  fwt_sei.c
  fwt_stamp.c
)

target_include_directories(fwt_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}/generated
)
add_dependencies(fwt_common fwt_version)
