// Copyright 2026 Andrew Smith
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "../src/agent.h"
#include "../src/config.h"
#include "../src/connector.h"
#include "../src/ui.h"
#include "harness.h"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// White-box accessor — requires `friend struct AgentTests;` in agent.h
struct AgentTests {
  static bool save(Agent &a, const std::string &p) { return a.save_context(p); }
  static bool restore(Agent &a, const std::string &p) {
    return a.restore_context(p);
  }
  static bool requires_approval(const Agent &a, const ToolDef &d) {
    return a.requires_approval(d);
  }
  static bool slash(Agent &a, std::string_view s) {
    return a.handle_slash_command(s);
  }
  static const std::vector<SlashCommand> &cmds(const Agent &a) {
    return a.slash_commands_;
  }
  static const ContextManager &ctx(const Agent &a) { return a.context_; }
  static double cost(const Agent &a, const LlmResponse::Usage &u) {
    return a.compute_cost(u);
  }
};

// Minimal Connector stub for injecting canned responses in tests.
struct StubConnector : Connector {
  std::vector<LlmResponse> responses;
  size_t call_count = 0;

  LlmResponse complete(const ContextManager &,
                       const std::vector<ToolDef> &) override {
    if (call_count < responses.size())
      return responses[call_count++];
    LlmResponse done;
    done.content = "[stub exhausted]";
    return done;
  }
};

static Config test_cfg() { return Config::defaults(); }

static std::string tmp_path(const char *suffix = ".json") {
  return fs::temp_directory_path().string() + "/ccl_agent_test_" +
         std::to_string(std::rand()) + suffix;
}

static std::string write_tmp(const std::string &content,
                             const char *suffix = ".json") {
  std::string p = tmp_path(suffix);
  std::ofstream f(p);
  f << content;
  return p;
}

// ============================================================================
// save / restore
// ============================================================================

TEST(agent_restore_then_save_roundtrip) {
  Ui ui;
  Agent a(test_cfg(), ui);

  std::string src = write_tmp(R"({
        "version": 1,
        "total_tokens": 100,
        "messages": [
            {"role": "system",    "content": "sys",   "estimated_tokens": 10},
            {"role": "user",      "content": "hello", "estimated_tokens": 5},
            {"role": "assistant", "content": "world", "estimated_tokens": 5}
        ]
    })");
  CHECK(AgentTests::restore(a, src));
  fs::remove(src);

  std::string dst = tmp_path();
  CHECK(AgentTests::save(a, dst));

  // Restore the saved file into a second agent
  Agent a2(test_cfg(), ui);
  CHECK(AgentTests::restore(a2, dst));
  fs::remove(dst);

  const auto &msgs = AgentTests::ctx(a2).messages();
  CHECK_EQ(msgs.size(), size_t(3));
  CHECK_EQ(msgs[0].content, std::string("sys"));
  CHECK_EQ(msgs[1].content, std::string("hello"));
  CHECK_EQ(msgs[2].content, std::string("world"));
  CHECK_EQ(AgentTests::ctx(a2).total_tokens(), size_t(100));
}

// A legacy "mode" field in a saved context is ignored, not rejected.
TEST(agent_restore_ignores_legacy_mode_field) {
  Ui ui;
  Agent a(test_cfg(), ui);
  std::string p = write_tmp(R"({
        "version": 1, "mode": "act", "total_tokens": 0, "messages": []
    })");
  CHECK(AgentTests::restore(a, p));
  fs::remove(p);
}

TEST(agent_restore_invalid_version) {
  Ui ui;
  Agent a(test_cfg(), ui);
  std::string p =
      write_tmp(R"({"version": 2, "total_tokens": 0, "messages": []})");
  CHECK(!AgentTests::restore(a, p));
  fs::remove(p);
}

TEST(agent_restore_missing_messages) {
  Ui ui;
  Agent a(test_cfg(), ui);
  std::string p = write_tmp(R"({"version": 1, "total_tokens": 0})");
  CHECK(!AgentTests::restore(a, p));
  fs::remove(p);
}

TEST(agent_restore_malformed_json) {
  Ui ui;
  Agent a(test_cfg(), ui);
  std::string p = write_tmp("not json at all }{");
  CHECK(!AgentTests::restore(a, p));
  fs::remove(p);
}

TEST(agent_restore_skips_incomplete_messages) {
  Ui ui;
  Agent a(test_cfg(), ui);
  std::string p = write_tmp(R"({
        "version": 1,
        "total_tokens": 5,
        "messages": [
            {"role": "user", "content": "good", "estimated_tokens": 5},
            {"role": "user"}
        ]
    })");
  CHECK(AgentTests::restore(a, p));
  fs::remove(p);
  CHECK_EQ(AgentTests::ctx(a).message_count(), size_t(1));
}

TEST(agent_restore_nonexistent_file) {
  Ui ui;
  Agent a(test_cfg(), ui);
  CHECK(!AgentTests::restore(a, "/nonexistent/path/to/file.json"));
}

// ============================================================================
// requires_approval
// ============================================================================

TEST(agent_requires_approval_read_auto) {
  Config cfg = test_cfg();
  cfg.permissions.auto_approve_read = true;
  Ui ui;
  Agent a(cfg, ui);
  ToolDef def;
  def.permission = Permission::Read;
  CHECK(!AgentTests::requires_approval(a, def));
}

TEST(agent_requires_approval_read_manual) {
  Config cfg = test_cfg();
  cfg.permissions.auto_approve_read = false;
  Ui ui;
  Agent a(cfg, ui);
  ToolDef def;
  def.permission = Permission::Read;
  CHECK(AgentTests::requires_approval(a, def));
}

TEST(agent_requires_approval_write_auto) {
  Config cfg = test_cfg();
  cfg.permissions.auto_approve_write = true;
  Ui ui;
  Agent a(cfg, ui);
  ToolDef def;
  def.permission = Permission::Write;
  CHECK(!AgentTests::requires_approval(a, def));
}

TEST(agent_requires_approval_execute_default) {
  Ui ui;
  Agent a(test_cfg(), ui);
  ToolDef def;
  def.permission = Permission::Execute;
  CHECK(AgentTests::requires_approval(a, def));
}

// ============================================================================
// slash commands
// ============================================================================

TEST(agent_slash_command_unknown_returns_false) {
  Ui ui;
  Agent a(test_cfg(), ui);
  CHECK(!AgentTests::slash(a, "/nonexistent"));
}

TEST(agent_slash_command_empty_returns_false) {
  Ui ui;
  Agent a(test_cfg(), ui);
  CHECK(!AgentTests::slash(a, ""));
}

TEST(agent_slash_command_no_slash_returns_false) {
  Ui ui;
  Agent a(test_cfg(), ui);
  CHECK(!AgentTests::slash(a, "mode plan"));
}

TEST(agent_slash_commands_all_registered) {
  Ui ui;
  Agent a(test_cfg(), ui);
  const auto &cmds = AgentTests::cmds(a);

  auto has = [&](const std::string &name) {
    for (const auto &c : cmds)
      if (c.name == name)
        return true;
    return false;
  };

  CHECK(has("compact"));
  CHECK(has("clear"));
  CHECK(has("context"));
  CHECK(has("edit"));
  CHECK(has("mcp"));
  CHECK(has("stats"));
  CHECK(has("quit"));
  CHECK(has("help"));
}

// ============================================================================
// compute_cost — pricing tiers
// ============================================================================

TEST(agent_compute_cost_empty_pricing_is_zero) {
  Ui ui;
  Agent a(test_cfg(), ui);
  LlmResponse::Usage u;
  u.prompt_tokens = 1000;
  u.completion_tokens = 500;
  CHECK(std::abs(AgentTests::cost(a, u)) < 1e-12);
}

TEST(agent_compute_cost_selects_tier_and_sums) {
  Config cfg = test_cfg();
  cfg.model = "priced-model";
  PricingTier t0;
  t0.model = "priced-model";
  t0.context_min = 0;
  t0.context_max = 200000;
  t0.input_cost_per_token = 0.000003;
  t0.cache_read_input_token_cost = 0.0000003;
  t0.cache_creation_input_token_cost = 0.00000375;
  t0.output_cost_per_token = 0.000015;
  cfg.pricing.push_back(t0);

  Ui ui;
  Agent a(cfg, ui);

  LlmResponse::Usage u;
  u.prompt_tokens = 1000;    // 1000 input, of which 400 cache_read, 100 write
  u.cache_read_tokens = 400; // -> uncached = 500
  u.cache_write_tokens = 100;
  u.completion_tokens = 200;

  double expected = 500 * 0.000003     // uncached input
                    + 400 * 0.0000003  // cache read
                    + 100 * 0.00000375 // cache creation
                    + 200 * 0.000015;  // output
  CHECK(std::abs(AgentTests::cost(a, u) - expected) < 1e-12);
}

TEST(agent_compute_cost_tier_boundary_upper_selects_second) {
  Config cfg = test_cfg();
  cfg.model = "m";
  PricingTier lo;
  lo.model = "m";
  lo.context_min = 0;
  lo.context_max = 200000;
  lo.input_cost_per_token = 0.000003;
  PricingTier hi;
  hi.model = "m";
  hi.context_min = 200000;
  hi.context_max = 1000000;
  hi.input_cost_per_token = 0.000006;
  cfg.pricing.push_back(lo);
  cfg.pricing.push_back(hi);

  Ui ui;
  Agent a(cfg, ui);

  LlmResponse::Usage u;
  u.prompt_tokens = 200000; // exactly the boundary -> upper tier ([min, max))
  double expected = 200000 * 0.000006;
  CHECK(std::abs(AgentTests::cost(a, u) - expected) < 1e-9);
}

TEST(agent_compute_cost_model_mismatch_is_zero) {
  Config cfg = test_cfg();
  cfg.model = "active-model";
  PricingTier t;
  t.model = "other-model";
  t.context_min = 0;
  t.context_max = 1000000;
  t.input_cost_per_token = 0.001;
  cfg.pricing.push_back(t);

  Ui ui;
  Agent a(cfg, ui);
  LlmResponse::Usage u;
  u.prompt_tokens = 1000;
  CHECK(std::abs(AgentTests::cost(a, u)) < 1e-12);
}

// ============================================================================
// DI constructor — injected StubConnector
// ============================================================================

TEST(agent_run_text_response_returns_0) {
  Ui ui(true);
  LlmResponse resp;
  resp.content = "All done.";
  auto conn = std::make_unique<StubConnector>();
  conn->responses.push_back(resp);
  Config cfg = test_cfg();
  cfg.permissions.auto_approve_read = true;
  Agent a(cfg, ui, std::move(conn));
  CHECK_EQ(a.run("do something"), 0);
}

TEST(agent_run_error_response_returns_1) {
  Ui ui(true);
  LlmResponse resp;
  resp.is_error = true;
  resp.content = "Connection refused";
  auto conn = std::make_unique<StubConnector>();
  conn->responses.push_back(resp);
  Agent a(test_cfg(), ui, std::move(conn));
  CHECK_EQ(a.run("do something"), 1);
}

TEST(agent_run_stub_connector_called_once) {
  Ui ui(true);
  LlmResponse resp;
  resp.content = "Done.";
  auto *raw = new StubConnector();
  raw->responses.push_back(resp);
  Agent a(test_cfg(), ui, std::unique_ptr<Connector>(raw));
  a.run("ping");
  CHECK_EQ(raw->call_count, size_t(1));
}

TEST(agent_run_empty_response_exits_cleanly) {
  Ui ui(true);
  LlmResponse resp; // empty content, no tool calls, no error
  auto conn = std::make_unique<StubConnector>();
  conn->responses.push_back(resp);
  Agent a(test_cfg(), ui, std::move(conn));
  CHECK_EQ(a.run("go"), 0);
}
