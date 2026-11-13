add_executable(trig-listen main.c)
target_link_libraries(trig-listen PRIVATE fwt_common m)
install(TARGETS trig-listen RUNTIME DESTINATION bin)
