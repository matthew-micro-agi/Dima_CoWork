add_executable(test_index test_index.c)
target_link_libraries(test_index PRIVATE fwt_common)
add_test(NAME index COMMAND test_index)

add_executable(test_trigd test_trigd.c)
target_link_libraries(test_trigd PRIVATE trigd_core fwt_common)
add_test(NAME trigd COMMAND test_trigd)

add_executable(test_sei test_sei.c)
target_link_libraries(test_sei PRIVATE fwt_common)
add_test(NAME sei COMMAND test_sei)

add_executable(test_stamp test_stamp.c)
target_link_libraries(test_stamp PRIVATE fwt_common)
add_test(NAME stamp COMMAND test_stamp)
