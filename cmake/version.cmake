# Writes fwt_version.h at build time: project version + git commit (DL-24),
# e.g. 0.1.0-g1a2b3c4, with -dirty if the tree has uncommitted changes.
# Run as: cmake -DSRC=<repo> -DOUT=<header> -DPROJECT_VERSION=<x.y.z> -P version.cmake
# configure_file only rewrites the header when the text changes (no needless rebuilds).
execute_process(
  COMMAND git -C "${SRC}" describe --always --dirty --abbrev=7 --match "NO-TAGS-USED"
  OUTPUT_VARIABLE GIT_DESC OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE GIT_RC ERROR_QUIET)
if(GIT_RC EQUAL 0 AND GIT_DESC)
  set(FWT_VERSION_STR "${PROJECT_VERSION}-g${GIT_DESC}")
else()
  set(FWT_VERSION_STR "${PROJECT_VERSION}-nogit")
endif()
configure_file("${SRC}/src/common/fwt_version.h.in" "${OUT}" @ONLY)
