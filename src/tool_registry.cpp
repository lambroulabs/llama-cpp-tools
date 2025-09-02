#include "llama_cpp_tools/tool_registry.h"
#include <future>
#include <mutex>

namespace lct {

json ToolRegistry::invoke_concurrent(const std::string& name, const json& args) const {
    auto it = tools_.find(name);
    if (it == tools_.end()) throw std::runtime_error("Tool not found: " + name);
    auto fut = std::async(std::launch::async, it->second, args);
    return fut.get();
}

json ToolRegistry::handle_tool_call_response(const json& api_response) const {
    json entries = api_response;
    if (api_response.is_object()) {
        if (api_response.contains("choices")) entries = api_response["choices"];
        else entries = json::array({ api_response });
    }

    for (const auto& choice : entries) {
        const json* message = nullptr;
        if (choice.contains("message")) message = &choice["message"];
        else if (choice.contains("message")) message = &choice["message"];

        if (!message) continue;

        if (message->contains("tool_calls") && (*message)["tool_calls"].is_array()) {
            for (const auto& tc : (*message)["tool_calls"]) {
                if (!tc.contains("function")) continue;
                const auto& func = tc["function"];
                std::string name = func.value("name", "");
                std::string args_str = func.value("arguments", "{}");
                json args = json::parse(args_str);
                return invoke(name, args);
            }
        }
    }
    throw std::runtime_error("No tool call found in response");
}

ToolRegistry& global_registry() {
    static ToolRegistry reg;
    return reg;
}


// ---------- helpers (anonymous namespace) ----------
namespace {
    // Parse "arguments" which may be a JSON string or already a JSON value.
    inline json parse_function_arguments(const json& func) {
        if (!func.contains("arguments")) return json::object();
        const auto& a = func["arguments"];
        if (a.is_string()) {
            try { return json::parse(a.get<std::string>()); }
            catch (...) { return json::object(); }
        }
        if (a.is_object() || a.is_array()) return a;
        return json::object();
    }

    // Collect tool calls from a response object (supports OpenAI-style fields).
    inline void collect_tool_calls_from_node(const json& node,
                                             std::vector<std::pair<std::string, json>>& out)
    {
        // Newer OpenAI: message.tool_calls:[{type:"function", function:{name,arguments}}]
        if (node.contains("tool_calls") && node["tool_calls"].is_array()) {
            for (const auto& tc : node["tool_calls"]) {
                const json& func = tc.contains("function") ? tc["function"] : tc;
                std::string name = func.value("name", "");
                if (!name.empty()) {
                    out.emplace_back(name, parse_function_arguments(func));
                }
            }
        }

        // Older OpenAI: message.function_call:{name, arguments}
        if (node.contains("function_call") && node["function_call"].is_object()) {
            const auto& fc = node["function_call"];
            std::string name = fc.value("name", "");
            if (!name.empty()) {
                out.emplace_back(name, parse_function_arguments(fc));
            }
        }
    }

    // Extract the logical "message-like" node from a choice-ish entry.
    inline const json& pick_message_like(const json& choice_or_msg) {
        if (choice_or_msg.is_object()) {
            if (choice_or_msg.contains("message")) return choice_or_msg["message"];
            if (choice_or_msg.contains("delta"))   return choice_or_msg["delta"];
        }
        return choice_or_msg;
    }

    // Robust, string/escape-aware extractor of complete top-level JSON values.
    // Pulls full objects or arrays from 'buffer' and erases consumed text.
    inline std::vector<std::string> extract_complete_json_values(std::string& buffer) {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < buffer.size()) {
            // find next JSON start
            while (i < buffer.size() && buffer[i] != '{' && buffer[i] != '[') ++i;
            if (i >= buffer.size()) break;

            const char opener = buffer[i];
            const char closer = (opener == '{') ? '}' : ']';
            size_t start = i;

            int depth = 0;
            bool in_string = false;
            bool escape = false;

            for (; i < buffer.size(); ++i) {
                char c = buffer[i];

                if (in_string) {
                    if (escape) { escape = false; continue; }
                    if (c == '\\') { escape = true; continue; }
                    if (c == '"') { in_string = false; continue; }
                    continue;
                } else {
                    if (c == '"') { in_string = true; continue; }
                    if (c == opener) { ++depth; continue; }
                    if (c == closer) {
                        --depth;
                        if (depth == 0) {
                            // complete JSON value [start..i]
                            size_t end = i + 1;
                            out.emplace_back(buffer.substr(start, end - start));
                            buffer.erase(0, end);  // drop consumed prefix
                            i = 0;                 // restart scanning from beginning
                            break;
                        }
                        continue;
                    }
                }
            }

            // if loop ended without closing, need more data
            if (i >= buffer.size()) break;
        }
        return out;
    }

} // namespace


// ---------- implementations ----------

std::vector<ToolRegistry::ExecutionResult>
ToolRegistry::process_remote_response_and_execute(const json& api_response, bool concurrent) const
{
    // Normalize to a list of "entries" that each might contain a message/delta.
    json entries = api_response;
    if (api_response.is_object() && api_response.contains("choices")) {
        entries = api_response["choices"];
    } else if (!api_response.is_array()) {
        entries = json::array({ api_response });
    }

    // 1) Discover all tool calls in order.
    std::vector<std::pair<std::string, json>> calls;
    for (const auto& entry : entries) {
        const json& node = pick_message_like(entry);
        collect_tool_calls_from_node(node, calls);
    }

    // 2) Execute them (sync or concurrent).
    std::vector<ExecutionResult> results;
    results.reserve(calls.size());

    if (!concurrent) {
        for (const auto& [name, args] : calls) {
            ExecutionResult r;
            r.tool_name = name;
            r.arguments = args;
            try {
                r.result = invoke(name, args);
            } catch (const std::exception& e) {
                r.error = e.what();
            } catch (...) {
                r.error = "Unknown error invoking tool";
            }
            results.push_back(std::move(r));
        }
        return results;
    }

    // concurrent path
    std::vector<std::future<ExecutionResult>> futs;
    futs.reserve(calls.size());
    for (const auto& [name, args] : calls) {
        futs.emplace_back(std::async(std::launch::async, [this, name, args]() -> ExecutionResult {
            ExecutionResult r;
            r.tool_name = name;
            r.arguments = args;
            try {
                r.result = this->invoke(name, args);
            } catch (const std::exception& e) {
                r.error = e.what();
            } catch (...) {
                r.error = "Unknown error invoking tool";
            }
            return r;
        }));
    }

    // Preserve discovery order in the returned vector.
    for (auto& f : futs) {
        results.push_back(f.get());
    }
    return results;
}


void ToolRegistry::process_streaming_response_and_execute(
    std::function<bool(std::string&)> get_chunk,
    std::function<void(const ExecutionResult&)> on_result,
    bool concurrent) const
{
    std::string buffer;
    std::string chunk;

    while (true) {
        chunk.clear();
        if (!get_chunk(chunk)) break;
        buffer.append(chunk);

        // Pull any complete JSON values from the buffer.
        auto json_blobs = extract_complete_json_values(buffer);
        for (const auto& s : json_blobs) {
            try {
                json obj = json::parse(s);
                auto batch = process_remote_response_and_execute(obj, concurrent);
                for (const auto& r : batch) on_result(r);
            } catch (...) {
                // Ignore parse errors for partial/garbage fragments; keep accumulating.
            }
        }
    }

    // Final flush in case the buffer ends with a complete JSON value.
    auto final_blobs = extract_complete_json_values(buffer);
    for (const auto& s : final_blobs) {
        try {
            json obj = json::parse(s);
            auto batch = process_remote_response_and_execute(obj, concurrent);
            for (const auto& r : batch) on_result(r);
        } catch (...) {
            // swallow
        }
    }
}

} // namespace lct
