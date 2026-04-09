#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace synapse {
namespace ide {

struct McpToolSchema {
    std::string name;
    std::string description;
    std::string inputSchemaJson;
};

struct McpToolResult {
    bool success = false;
    std::string content;
    std::string errorMessage;
};

struct McpResource {
    std::string uri;
    std::string name;
    std::string mimeType;
};

struct McpResourceContent {
    std::string uri;
    std::string mimeType;
    std::string text;
};

struct McpPrompt {
    std::string name;
    std::string description;
};

struct McpPromptMessage {
    std::string role;
    std::string content;
};

using McpToolHandler = std::function<McpToolResult(const std::string& argsJson)>;
using McpResourceHandler = std::function<McpResourceContent()>;

class McpServer {
public:
    McpServer();
    ~McpServer();

    void registerTool(const std::string& name, const std::string& description,
                      const std::string& inputSchemaJson, McpToolHandler handler);
    void registerResource(const std::string& uri, const std::string& name,
                          const std::string& mimeType, McpResourceHandler handler);

    bool start(const std::string& transportType = "stdio");
    void stop();
    bool isRunning() const;

    std::string handleRequest(const std::string& requestJson);

    std::vector<McpToolSchema> listTools() const;
    McpToolResult callTool(const std::string& name, const std::string& argsJson);

    std::vector<McpResource> listResources() const;
    McpResourceContent readResource(const std::string& uri);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
