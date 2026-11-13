add_library(fwt_common STATIC
  fwt_index.c
  fwt_seqcheck.c
  fwt_sei.c
  fwt_stamp.c
)

target_include_directories(fwt_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}/generated
)
