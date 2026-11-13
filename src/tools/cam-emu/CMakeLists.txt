# Camera emulator (DL-26): a test tool, deliberately without an install() rule,
# so it never ends up in the node bundle (deploy/make-bundle.sh).
add_library(camemu_core STATIC camemu_core.c)
target_include_directories(camemu_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(camemu_core PUBLIC fwt_common)

add_executable(cam-emu main.c)
target_link_libraries(cam-emu PRIVATE camemu_core fwt_common)
