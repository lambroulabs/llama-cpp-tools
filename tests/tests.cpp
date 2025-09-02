#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "llama_cpp_tools/tool_registry.h"

#include <thread>
#include <chrono>
#include <cctype>

using json = nlohmann::json;
using namespace lct;

TEST_CASE("Basic types: int, number, string, bool") {
    ToolRegistry reg;

    ToolSpec t_int;
    t_int.name = "t_int";
    t_int.description = "int test";
    t_int.parameters = { {"type","object"}, {"properties", {{"x", {{"type","integer"}}}}}, {"required", {"x"}} };
    t_int.handler = [](const json& args){ return json{{"ok", args.at("x").get<int>() * 2}}; };
    reg.register_tool_spec(t_int);

    ToolSpec t_num;
    t_num.name = "t_num";
    t_num.description = "number test";
    t_num.parameters = { {"type","object"}, {"properties", {{"v", {{"type","number"}}}}}, {"required", {"v"}} };
    t_num.handler = [](const json& args){ return json{{"ok", args.at("v").get<double>() * 1.5}}; };
    reg.register_tool_spec(t_num);

    ToolSpec t_str;
    t_str.name = "t_str";
    t_str.description = "string test";
    t_str.parameters = { {"type","object"}, {"properties", {{"s", {{"type","string"}}}}}, {"required", {"s"}} };
    t_str.handler = [](const json& args){ return json{{"ok", args.at("s").get<std::string>() + "!"}}; };
    reg.register_tool_spec(t_str);

    ToolSpec t_bool;
    t_bool.name = "t_bool";
    t_bool.description = "bool test";
    t_bool.parameters = { {"type","object"}, {"properties", {{"b", {{"type","boolean"}}}}}, {"required", {"b"}} };
    t_bool.handler = [](const json& args){ return json{{"ok", !args.at("b").get<bool>()}}; };
    reg.register_tool_spec(t_bool);

    REQUIRE(reg.invoke("t_int", json{{"x", 5}}).at("ok").get<int>() == 10);
    REQUIRE(reg.invoke("t_num", json{{"v", 2.0}}).at("ok").get<double>() == Catch::Approx(3.0));
    REQUIRE(reg.invoke("t_str", json{{"s", "hi"}}).at("ok").get<std::string>() == "hi!");
    REQUIRE(reg.invoke("t_bool", json{{"b", true}}).at("ok").get<bool>() == false);

    REQUIRE_THROWS_AS(reg.invoke("t_int", json{{"x", "notint"}}), std::exception);

    // large string input
    std::string large(10*1024*1024, 'a');
    auto out = reg.invoke("t_str", json{{"s", large}});
    REQUIRE(out.at("ok").get<std::string>().size() == large.size() + 1);
}

// ------------------ New tests for the added APIs ------------------

TEST_CASE("process_remote_response_and_execute executes tool calls") {
    ToolRegistry reg;

    ToolSpec echo;
    echo.name = "echo";
    echo.description = "echo args";
    echo.parameters = {{"type","object"}, {"properties", {{"msg", {{"type","string"}}}}}, {"required", {"msg"}}};
    echo.handler = [](const json& args){ return json{{"echoed", args.at("msg")}}; };
    reg.register_tool_spec(echo);

    json api_resp = {
        {"choices", {{
            {"message", {
                {"tool_calls", {{
                    {"function", {
                        {"name", "echo"},
                        {"arguments", R"({"msg":"hi"})"}
                    }}
                }}}
            }}
        }}}
    };

    auto results = reg.process_remote_response_and_execute(api_resp);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].tool_name == "echo");
    REQUIRE(results[0].result.at("echoed") == "hi");
    REQUIRE(results[0].error.empty());
}

TEST_CASE("process_remote_response_and_execute handles errors") {
    ToolRegistry reg;

    ToolSpec bad;
    bad.name = "bad";
    bad.description = "always throws";
    bad.parameters = {{"type","object"}, {"properties", {}}, {"required", json::array() }};
    // <--- Explicit return type so the lambda is convertible to ToolHandler
    bad.handler = [](const json& args) -> json { throw std::runtime_error("fail"); };
    reg.register_tool_spec(bad);

    json api_resp = {
        {"choices", {{
            {"message", {
                {"tool_calls", {{
                    {"function", {
                        {"name", "bad"},
                        {"arguments", "{}"}
                    }}
                }}}
            }}
        }}}
    };

    auto results = reg.process_remote_response_and_execute(api_resp);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].tool_name == "bad");
    REQUIRE(!results[0].error.empty());
}

TEST_CASE("process_remote_response_and_execute concurrent execution") {
    ToolRegistry reg;

    ToolSpec slow;
    slow.name = "slow";
    slow.description = "sleep then return";
    slow.parameters = {{"type","object"}, {"properties", {{"v", {{"type","integer"}}}}}, {"required", {"v"}}};
    slow.handler = [](const json& args){
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return json{{"ok", args.at("v").get<int>()}};
    };
    reg.register_tool_spec(slow);

    json api_resp = {
        {"choices", {{
            {"message", {
                {"tool_calls", {
                    {{"function", {{"name", "slow"}, {"arguments", R"({"v":1})"}}}},
                    {{"function", {{"name", "slow"}, {"arguments", R"({"v":2})"}}}}
                }}
            }}
        }}}
    };

    auto t1 = std::chrono::steady_clock::now();
    auto results = reg.process_remote_response_and_execute(api_resp, true); // concurrent
    auto t2 = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    REQUIRE(results.size() == 2);
    REQUIRE(results[0].result.at("ok") == 1);
    REQUIRE(results[1].result.at("ok") == 2);
    // should finish much faster than serial (100ms+)
    REQUIRE(elapsed_ms < 90);
}

TEST_CASE("process_streaming_response_and_execute processes JSON chunks") {
    ToolRegistry reg;

    ToolSpec upper;
    upper.name = "upper";
    upper.description = "uppercase";
    upper.parameters = {{"type","object"}, {"properties", {{"s", {{"type","string"}}}}}, {"required", {"s"}}};
    upper.handler = [](const json& args){
        std::string s = args.at("s");
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return json{{"out", s}};
    };
    reg.register_tool_spec(upper);

    // Simulate streaming JSON delivered in chunks
    std::string full_json = R"({
        "choices":[{"message":{"tool_calls":[{"function":{"name":"upper","arguments":"{\"s\":\"hey\"}"}}]}}]
    })";

    size_t pos = 0;
    auto get_chunk = [&](std::string& out) -> bool {
        if (pos >= full_json.size()) return false;
        size_t n = std::min<size_t>(5, full_json.size() - pos);
        out = full_json.substr(pos, n);
        pos += n;
        return true;
    };

    std::vector<ToolRegistry::ExecutionResult> got;
    reg.process_streaming_response_and_execute(get_chunk, [&](const ToolRegistry::ExecutionResult& r){
        got.push_back(r);
    });

    REQUIRE(got.size() == 1);
    REQUIRE(got[0].tool_name == "upper");
    REQUIRE(got[0].result.at("out") == "HEY");
}
