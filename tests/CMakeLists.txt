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

add_executable(test_nodecap test_nodecap.c)
target_link_libraries(test_nodecap PRIVATE nodecap_core fwt_common)
add_test(NAME nodecap COMMAND test_nodecap)

add_executable(test_recorder test_recorder.c)
target_link_libraries(test_recorder PRIVATE recorder fwt_common)
add_test(NAME recorder COMMAND test_recorder)

add_executable(test_nodeagent test_nodeagent.c)
target_link_libraries(test_nodeagent PRIVATE na_fetch recorder fwt_common)
add_test(NAME nodeagent COMMAND test_nodeagent)
