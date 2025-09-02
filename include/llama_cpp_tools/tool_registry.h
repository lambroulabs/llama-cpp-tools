#pragma once

#include <functional>
#include <map>
#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace lct {
using json = nlohmann::json;
using ToolHandler = std::function<json(const json&)>;

struct ToolSpec {
    std::string name;
    std::string description;
    json parameters;
    ToolHandler handler;
};

class ToolRegistry {
public:
    ToolRegistry() = default;

    void register_tool(const std::string& name, ToolHandler handler, const json& schema) {
        tools_.emplace(name, std::move(handler));
        schemas_.emplace(name, schema);
    }

    json schemas() const {
        json arr = json::array();
        for (const auto& [name, schema] : schemas_) {
            arr.push_back(schema);
        }
        return arr;
    }

    json invoke(const std::string& name, const json& args) const {
        auto it = tools_.find(name);
        if (it == tools_.end()) throw std::runtime_error("Tool not found: " + name);
        return it->second(args);
    }

    json invoke_concurrent(const std::string& name, const json& args) const;

    json tools_for_openai() const { return schemas(); }

    std::string tools_for_openai_string() const { return tools_for_openai().dump(); }

    json handle_tool_call_response(const json& api_response) const;

    void register_tool_spec(const ToolSpec& spec) {
        json schema = { {"name", spec.name}, {"description", spec.description}, {"parameters", spec.parameters} };
        register_tool(spec.name, spec.handler, schema);
    }

    // Result for executing a single tool call
    struct ExecutionResult {
        std::string tool_name;
        json arguments;
        json result;        // valid if error.empty()
        std::string error;  // non-empty if an error occurred
    };

    // Find all tool calls in api_response, invoke them (sync or concurrently),
    // and return the list of results in order discovered.
    std::vector<ExecutionResult> process_remote_response_and_execute(const json& api_response, bool concurrent=false) const;

    // Streaming helper: accepts a callback `get_chunk(string& out)` which should
    // append/return the next chunk (return false when no more chunks). The
    // handler `on_result` is called for each ExecutionResult as it becomes
    // available. Useful for streaming responses from servers.
    void process_streaming_response_and_execute(std::function<bool(std::string&)> get_chunk,
                                               std::function<void(const ExecutionResult&)> on_result,
                                               bool concurrent=false) const;

private:
    std::map<std::string, ToolHandler> tools_;
    std::map<std::string, json> schemas_;
};

#define LCT_REGISTER_TOOL(REG, FUNC, SCHEMA) \
    do { REG.register_tool(#FUNC, FUNC, SCHEMA); } while(0)

// Static registration macro to emulate a decorator-like style. Usage:
//
// LCT_TOOL(my_tool, R"({"name":"my_tool","description":"...","parameters":{...}})")
// json my_tool(const json& args) { ... }
//
// The macro registers the function in the global registry at static init time.
#define LCT_TOOL(NAME, SCHEMA_STR) \
    json NAME(const lct::json& args); \
    static bool LCT_REG_##NAME = ([](){ lct::json schema = lct::json::parse(SCHEMA_STR); lct::global_registry().register_tool(#NAME, NAME, schema); return true; })(); \
    json NAME(const lct::json& args)

    // Global registry declaration
    ToolRegistry& global_registry();

    // Parameter helper macros
    // Usage:
    // static const char* add_description = "Add two integers";
    // LCT_PARAMS_START(add)
    // LCT_PARAM_INT(add, a, true)
    // LCT_PARAM_INT(add, b, true)
    // LCT_REGISTER_WITH_SCHEMA_FROM_PARAMS(add, add_description)

#define LCT_PARAMS_START(NAME) \
    static lct::json NAME##_params = lct::json::object({{"type","object"},{"properties", lct::json::object()},{"required", lct::json::array()}});

#define LCT_PARAM_INT(NAME, PARAM, REQUIRED) \
    static bool NAME##_param_##PARAM = ([](){ NAME##_params["properties"][#PARAM] = lct::json::object({{"type","integer"}}); if (REQUIRED) NAME##_params["required"].push_back(#PARAM); return true; })();

#define LCT_PARAM_STRING(NAME, PARAM, REQUIRED) \
    static bool NAME##_param_##PARAM = ([](){ NAME##_params["properties"][#PARAM] = lct::json::object({{"type","string"}}); if (REQUIRED) NAME##_params["required"].push_back(#PARAM); return true; })();

#define LCT_PARAM_NUMBER(NAME, PARAM, REQUIRED) \
    static bool NAME##_param_##PARAM = ([](){ NAME##_params["properties"][#PARAM] = lct::json::object({{"type","number"}}); if (REQUIRED) NAME##_params["required"].push_back(#PARAM); return true; })();

#define LCT_PARAM_BOOL(NAME, PARAM, REQUIRED) \
    static bool NAME##_param_##PARAM = ([](){ NAME##_params["properties"][#PARAM] = lct::json::object({{"type","boolean"}}); if (REQUIRED) NAME##_params["required"].push_back(#PARAM); return true; })();

#define LCT_REGISTER_WITH_SCHEMA_FROM_PARAMS(NAME, DESCRIPTION_VAR) \
    lct::json NAME(const lct::json& args); \
    static bool LCT_REG_##NAME = ([](){ lct::json schema = { {"name", #NAME}, {"description", DESCRIPTION_VAR}, {"parameters", NAME##_params} }; lct::global_registry().register_tool(#NAME, NAME, schema); return true; })(); \
    lct::json NAME(const lct::json& args)

}

namespace lct { ToolRegistry& global_registry(); }

namespace lct {
    inline ToolRegistry& tool_registry() { return global_registry(); }
    inline json handle_tool_call_response(const json& api_response) { return global_registry().handle_tool_call_response(api_response); }
}
