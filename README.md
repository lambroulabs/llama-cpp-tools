# llama-cpp-tools

A tiny C++ library to register "tools" (functions) and expose them to LLMs using structured function calling (llama.cpp / OpenAI-style). Supports concurrent tool calls and streaming.

Compatible with `llama.cpp` and `openai-cpp`.

This repository provides:

- A tiny `ToolRegistry` that holds named tools and their JSON schemas.
- Two convenient registration styles: `LCT_TOOL` (static macro) and `ToolSpec` (runtime object).
- Helpers to emit OpenAI-style `tools` schemas (`tools_for_openai()` / `tools_for_openai_string()`).
- A handler to parse model responses that request a tool call and invoke the correct tool.
- CMake infrastructure with FetchContent for dependencies, tests (Catch2), and packaging helpers (CPack / pkg-config template).

## Quick start (build & run)

Requires: CMake >= 3.14, a C++17 compiler, network access for FetchContent.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -- -j
```

## API overview

All public API is under the `lct` namespace and uses `nlohmann::json` for arguments and return values.

- `ToolRegistry` — core registry type (also available as a global singleton via `tool_registry()` / `global_registry()`).
- `ToolSpec` — struct describing a tool (name, description, parameters JSON, handler). Use `register_tool_spec()` to register it.
- `LCT_TOOL(name, schema_json_string)` — macro that registers a function at static init time. The function must be `json func(const json& args)`.
- `tools_for_openai()` / `tools_for_openai_string()` — produce the array/string of schemas suitable for passing to llama.cpp or other OpenAI-compatible endpoints.
- `handle_tool_call_response(const json& response)` — helper that finds the first tool call in an API response and invokes the registered tool, returning the tool's JSON result.
- `invoke(name, args)` and `invoke_concurrent(name, args)` — low-level invocations (sync and async).

### Registering tools — examples

There are 2 methods of registering tools for use with Llama.cpp/OpenAI API

1) Runtime `ToolSpec` (recommended for programmatic registration):

```cpp
ToolSpec s;
s.name = "get_current_weather";
s.description = "Get current weather for a location";
s.parameters = {
	{"type","object"},
	{"properties", {{"location", {{"type","string"}}}}},
	{"required", {"location"}}
};
s.handler = [](const json& args){
	std::string loc = args.at("location").get<std::string>();
	return json{{"temp_c", -63}};
};
tool_registry().register_tool_spec(s);
```

2) Static macro (`LCT_TOOL`) — minimal and convenient for simple projects:

```cpp
LCT_TOOL(get_current_weather, R"({
	"name":"get_current_weather",
	"description":"Get current weather for a location",
	"parameters":{ "type":"object", "properties":{ "location": {"type":"string"} }, "required":["location"] }
})") {
	std::string loc = args.at("location").get<std::string>();
	// ... compute result ...
	return json{{"temp_c", -63}, {"desc","thin CO2 atmosphere"}};
}
```



## Sending `tools` to llama.cpp (or other LLM servers)

When you call the LLM, include the `tools` array so the model can produce structured tool call responses:

```cpp
json payload = {
	{"model","llama2-6B"},
	{"tools", tool_registry().tools_for_openai()},
	{"messages", json::array({ json{{"role","user"},{"content","What is the weather on Mars?"}} })}
};
// send payload.dump() to /v1/chat/completions
```

The registry builds per-tool schemas (name, description, parameters) using the schema you registered.

## Handling the model's response and invoking tools

1. Parse the HTTP response body into `nlohmann::json`.
2. Use `handle_tool_call_response(api_response)` to find and invoke the first tool call. It returns the tool's JSON result.
3. Send the tool result back to the model as a follow-up message so the model can produce a final assistant message.

Example using the convenience helper:

```cpp
json api = json::parse(response_body_from_server);
try {
	json tool_result = handle_tool_call_response(api);
	std::cout << "Tool result: " << tool_result.dump() << std::endl;
} catch (const std::exception& e) {
	std::cerr << "No tool call found or error invoking tool: " << e.what() << std::endl;
}
```

## Packaging

`CMakeLists.txt` is configured to support packaging via CPack (DEB/RPM/TGZ) and installs a `pkg-config` file template (`cmake/llama-cpp-tools.pc.in`).

Suggested workflows:

- Build .deb/.rpm quickly with CPack:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -- -j
cpack -G DEB   # or -G RPM
```

- For distro-quality packages, create `debian/` (debhelper) or an RPM spec and build in clean chroots. For Arch, create a `PKGBUILD` that runs the normal cmake build and `make install` into `$pkgdir`.

See `packaging/README.md` for details and tips.

## Tests

Build and run the test suite (Catch2):

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build . -- -j
ctest --output-on-failure
```