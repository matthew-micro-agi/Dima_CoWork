add_executable(rec-tool main.c)
target_link_libraries(rec-tool PRIVATE recorder fwt_common)
install(TARGETS rec-tool RUNTIME DESTINATION bin)
