cmake_minimum_required(VERSION 3.20)
project(ccl)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Semantic version — bump these when branching for a release
set(CCL_VERSION_MAJOR 0)
set(CCL_VERSION_MINOR 1)
set(CCL_VERSION_PATCH 0)
set(CCL_VERSION_RC    "")  # e.g. "rc.1"; empty for final releases

# Capture build-type optimization flags (e.g. -O2 -DNDEBUG for Release)
if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _BT_UPPER)
    set(_CCL_TYPE_FLAGS "${CMAKE_CXX_FLAGS_${_BT_UPPER}}")
else()
    set(_CCL_TYPE_FLAGS "")
endif()
string(STRIP "${_CCL_TYPE_FLAGS}" _CCL_TYPE_FLAGS)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Main binary (stub until all src files exist)
add_executable(ccl
    src/main.cpp
    src/signals.cpp
    src/json.cpp
    src/config.cpp
    src/context.cpp
    src/tools.cpp
    src/mcp_client.cpp
    src/connector.cpp
    src/connector_base.cpp
    src/connector_openai.cpp
    src/connector_bedrock.cpp
    src/llm_client.cpp
    src/agent.cpp
    src/ui.cpp
)
target_link_libraries(ccl PRIVATE CURL::libcurl Threads::Threads)
target_compile_options(ccl PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
target_link_options(ccl PRIVATE $<$<CONFIG:Release>:-s>)

# Always-run target: regenerates version.h before every build
add_custom_target(ccl_version
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_MAJOR=${CCL_VERSION_MAJOR}
        -DVERSION_MINOR=${CCL_VERSION_MINOR}
        -DVERSION_PATCH=${CCL_VERSION_PATCH}
        "-DVERSION_RC=${CCL_VERSION_RC}"
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DCXX_FLAGS=${_CCL_TYPE_FLAGS}"
        -P ${CMAKE_SOURCE_DIR}/cmake/version.cmake
    COMMENT "Generating version.h"
    VERBATIM
)
add_dependencies(ccl ccl_version)
target_include_directories(ccl PRIVATE ${CMAKE_BINARY_DIR})

# Test binary
enable_testing()
add_executable(ccl_test
    tests/main.cpp
    tests/test_json.cpp
    tests/test_config.cpp
    tests/test_context.cpp
    tests/test_tools.cpp
    tests/test_llm_client.cpp
    tests/test_mcp_client.cpp
    src/signals.cpp
    src/json.cpp
    src/config.cpp
    src/context.cpp
    src/tools.cpp
    src/mcp_client.cpp
    src/connector.cpp
    src/connector_base.cpp
    src/connector_openai.cpp
    src/connector_bedrock.cpp
    src/llm_client.cpp
)
target_include_directories(ccl_test PRIVATE src tests)
target_link_libraries(ccl_test PRIVATE CURL::libcurl Threads::Threads)
target_compile_options(ccl_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
add_test(NAME ccl_tests COMMAND ccl_test)
