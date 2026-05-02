# Queries git for branch + SHA and writes ${BINARY_DIR}/version.h.
# Invoked as a CMake script (-P) so it runs fresh on every build.

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _r
)
if(NOT _r EQUAL 0)
    set(GIT_BRANCH "unknown")
endif()

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _r
)
if(NOT _r EQUAL 0)
    set(GIT_SHA "0000000")
endif()

if(GIT_BRANCH MATCHES "^release/v(.+)$")
    set(VERSION_STRING "v${CMAKE_MATCH_1}+${GIT_SHA}")
elseif(GIT_BRANCH STREQUAL "main")
    set(VERSION_STRING "main+${GIT_SHA}")
else()
    set(VERSION_STRING "dev+${GIT_SHA}")
endif()

if(NOT BUILD_TYPE)
    set(BUILD_TYPE "unspecified")
endif()

# configure_file only writes when content changes, avoiding spurious recompilation.
configure_file(
    "${SOURCE_DIR}/cmake/version.h.in"
    "${BINARY_DIR}/version.h"
    @ONLY
    NEWLINE_STYLE LF
)
