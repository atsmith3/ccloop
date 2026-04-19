#include "harness.h"
#include "../src/tools.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// RAII helper for temp directory
struct TmpDir {
    std::string path;
    TmpDir() {
        path = fs::temp_directory_path().string() + "/ccl_test_" + std::to_string(std::rand());
        fs::create_directories(path);
    }
    ~TmpDir() {
        if (fs::exists(path)) {
            fs::remove_all(path);
        }
    }
};

TEST(tool_read_file_existing) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/test.txt";
    {
        std::ofstream f(file_path);
        f << "Hello, world!";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(file_path);

    ToolResult result = tool_read_file(args);
    CHECK(result.success);
    CHECK_EQ(result.content, std::string("Hello, world!"));
}

TEST(tool_read_file_missing) {
    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>("/nonexistent/file.txt");

    ToolResult result = tool_read_file(args);
    CHECK(!result.success);
    CHECK(!result.error.empty());
}

TEST(tool_list_dir_existing) {
    TmpDir tmp;
    {
        std::ofstream f(tmp.path + "/file1.txt");
        f << "test";
    }
    {
        std::ofstream f(tmp.path + "/file2.txt");
        f << "test";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(tmp.path);

    ToolResult result = tool_list_dir(args);
    CHECK(result.success);
    // Should contain file names
    CHECK(result.content.find("file1.txt") != std::string::npos);
    CHECK(result.content.find("file2.txt") != std::string::npos);
}

TEST(tool_list_dir_missing) {
    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>("/nonexistent/dir");

    ToolResult result = tool_list_dir(args);
    CHECK(!result.success);
}

TEST(tool_file_info_existing) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/test.txt";
    {
        std::ofstream f(file_path);
        f << "test content";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(file_path);

    ToolResult result = tool_file_info(args);
    CHECK(result.success);
    CHECK(result.content.find("exists: true") != std::string::npos);
    CHECK(result.content.find("is_file: true") != std::string::npos);
}

TEST(tool_file_info_missing) {
    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>("/nonexistent/path");

    ToolResult result = tool_file_info(args);
    CHECK(result.success);
    CHECK(result.content.find("exists: false") != std::string::npos);
}

TEST(tool_search_files_finds_match) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/test.txt";
    {
        std::ofstream f(file_path);
        f << "hello world\n";
        f << "goodbye world\n";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(tmp.path);
    args["pattern"] = JsonValue();
    args["pattern"].data.emplace<std::string>("hello");

    ToolResult result = tool_search_files(args);
    CHECK(result.success);
    CHECK(result.content.find("hello") != std::string::npos);
}

TEST(tool_search_files_no_match) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/test.txt";
    {
        std::ofstream f(file_path);
        f << "hello world\n";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(tmp.path);
    args["pattern"] = JsonValue();
    args["pattern"].data.emplace<std::string>("nomatch");

    ToolResult result = tool_search_files(args);
    CHECK(result.success);
    CHECK_EQ(result.content, std::string(""));
}

TEST(tool_registry_find_existing) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Explore, cfg);

    auto tool = registry.find("read_file");
    CHECK(tool.has_value());
    CHECK_EQ(tool.value()->def.name, std::string("read_file"));
}

TEST(tool_registry_find_missing) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Explore, cfg);

    auto tool = registry.find("nonexistent_tool");
    CHECK(!tool.has_value());
}

TEST(tool_registry_definitions_count) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Explore, cfg);

    auto defs = registry.definitions();
    // Should have 4 read-only tools
    CHECK_EQ(defs.size(), size_t(4));
}

TEST(tool_result_ok_fields) {
    ToolResult result = ToolResult::ok("success message");
    CHECK(result.success);
    CHECK_EQ(result.content, std::string("success message"));
    CHECK_EQ(result.error, std::string(""));
}

TEST(tool_result_fail_fields) {
    ToolResult result = ToolResult::fail("error message");
    CHECK(!result.success);
    CHECK_EQ(result.content, std::string(""));
    CHECK_EQ(result.error, std::string("error message"));
}
