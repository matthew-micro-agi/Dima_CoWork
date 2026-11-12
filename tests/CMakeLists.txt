add_executable(test_index test_index.c)
target_link_libraries(test_index PRIVATE fwt_common)
add_test(NAME index COMMAND test_index)
