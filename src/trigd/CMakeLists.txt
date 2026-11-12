# Pure logic, also used by the laptop unit tests.
add_library(trigd_core STATIC
  trigd_t0.c
  trigd_subs.c
)
target_include_directories(trigd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(trigd_core PUBLIC fwt_common)

add_executable(trigd
  main.c
  trigd_clock.c
  trigd_perout.c
  edge_computed.c
)
target_link_libraries(trigd PRIVATE trigd_core fwt_common)
install(TARGETS trigd RUNTIME DESTINATION bin)
