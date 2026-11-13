# Laptop tool: decodes with ffmpeg, so only built for the host.
add_executable(sei-dump main.c)
target_link_libraries(sei-dump PRIVATE fwt_common)
