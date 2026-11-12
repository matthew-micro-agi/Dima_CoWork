add_executable(fwt-info main.c)
target_link_libraries(fwt-info PRIVATE fwt_common)
install(TARGETS fwt-info RUNTIME DESTINATION bin)
