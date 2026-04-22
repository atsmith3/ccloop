cmake_minimum_required(VERSION 3.20)
project(ccl)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

# Main binary (stub until all src files exist)
add_executable(ccl
    src/main.cpp
    src/json.cpp
    src/config.cpp
    src/context.cpp
    src/tools.cpp
    src/mcp_client.cpp
    src/connector.cpp
    src/connector_base.cpp
    src/connector_qwen.cpp
    src/connector_openai.cpp
    src/connector_bedrock.cpp
    src/llm_client.cpp
    src/agent.cpp
    src/ui.cpp
)
target_link_libraries(ccl PRIVATE CURL::libcurl)
target_compile_options(ccl PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

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
    src/json.cpp
    src/config.cpp
    src/context.cpp
    src/tools.cpp
    src/mcp_client.cpp
    src/connector.cpp
    src/connector_base.cpp
    src/connector_qwen.cpp
    src/connector_openai.cpp
    src/connector_bedrock.cpp
    src/llm_client.cpp
)
target_include_directories(ccl_test PRIVATE src tests)
target_link_libraries(ccl_test PRIVATE CURL::libcurl)
target_compile_options(ccl_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
add_test(NAME ccl_tests COMMAND ccl_test)
