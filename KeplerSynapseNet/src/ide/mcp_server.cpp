#include "ide/mcp_server.h"

#include <atomic>
#include <mutex>

namespace synapse {
namespace ide {

struct McpServer::Impl {
    mutable std::mutex mtx;
    std::unordered_map<std::string, McpToolSchema> tools;
    std::unordered_map<std::string, McpToolHandler> toolHandlers;
    std::unordered_map<std::string, McpResource> resources;
    std::unordered_map<std::string, McpResourceHandler> resourceHandlers;
    std::atomic<bool> running{false};
};

McpServer::McpServer() : impl_(std::make_unique<Impl>()) {}
McpServer::~McpServer() { stop(); }

void McpServer::registerTool(const std::string& name, const std::string& description,
                              const std::string& inputSchemaJson, McpToolHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    McpToolSchema schema;
    schema.name = name;
    schema.description = description;
    schema.inputSchemaJson = inputSchemaJson;
    impl_->tools[name] = schema;
    impl_->toolHandlers[name] = std::move(handler);
}

void McpServer::registerResource(const std::string& uri, const std::string& name,
                                  const std::string& mimeType, McpResourceHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    McpResource res;
    res.uri = uri;
    res.name = name;
    res.mimeType = mimeType;
    impl_->resources[uri] = res;
    impl_->resourceHandlers[uri] = std::move(handler);
}

bool McpServer::start(const std::string& transportType) {
    (void)transportType;
    bool expected = false;
    if (!impl_->running.compare_exchange_strong(expected, true)) return false;
    return true;
}

void McpServer::stop() {
    impl_->running.store(false);
}

bool McpServer::isRunning() const {
    return impl_->running.load();
}

std::string McpServer::handleRequest(const std::string& requestJson) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    std::string method;
    auto methodPos = requestJson.find("\"method\"");
    if (methodPos == std::string::npos) {
        return "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}";
    }

    auto colonPos = requestJson.find(':', methodPos);
    auto quoteStart = requestJson.find('"', colonPos + 1);
    auto quoteEnd = requestJson.find('"', quoteStart + 1);
    if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
        method = requestJson.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    }

    if (method == "tools/list") {
        std::string result = "{\"jsonrpc\":\"2.0\",\"result\":{\"tools\":[";
        bool first = true;
        for (const auto& pair : impl_->tools) {
            if (!first) result += ",";
            result += "{\"name\":\"" + pair.second.name + "\",\"description\":\"" +
                      pair.second.description + "\"}";
            first = false;
        }
        result += "]}}";
        return result;
    }

    if (method == "tools/call") {
        auto namePos = requestJson.find("\"name\"");
        if (namePos != std::string::npos) {
            auto nc = requestJson.find(':', namePos);
            auto nqs = requestJson.find('"', nc + 1);
            auto nqe = requestJson.find('"', nqs + 1);
            if (nqs != std::string::npos && nqe != std::string::npos) {
                std::string toolName = requestJson.substr(nqs + 1, nqe - nqs - 1);
                auto handlerIt = impl_->toolHandlers.find(toolName);
                if (handlerIt != impl_->toolHandlers.end()) {
                    auto toolResult = handlerIt->second("{}");
                    if (toolResult.success) {
                        return "{\"jsonrpc\":\"2.0\",\"result\":{\"content\":\"" +
                               toolResult.content + "\"}}";
                    }
                    return "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,\"message\":\"" +
                           toolResult.errorMessage + "\"}}";
                }
            }
        }
        return "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32601,\"message\":\"Tool not found\"}}";
    }

    if (method == "resources/list") {
        std::string result = "{\"jsonrpc\":\"2.0\",\"result\":{\"resources\":[";
        bool first = true;
        for (const auto& pair : impl_->resources) {
            if (!first) result += ",";
            result += "{\"uri\":\"" + pair.second.uri + "\",\"name\":\"" +
                      pair.second.name + "\"}";
            first = false;
        }
        result += "]}}";
        return result;
    }

    if (method == "resources/read") {
        auto uriPos = requestJson.find("\"uri\"");
        if (uriPos != std::string::npos) {
            auto uc = requestJson.find(':', uriPos);
            auto uqs = requestJson.find('"', uc + 1);
            auto uqe = requestJson.find('"', uqs + 1);
            if (uqs != std::string::npos && uqe != std::string::npos) {
                std::string resUri = requestJson.substr(uqs + 1, uqe - uqs - 1);
                auto handlerIt = impl_->resourceHandlers.find(resUri);
                if (handlerIt != impl_->resourceHandlers.end()) {
                    auto content = handlerIt->second();
                    return "{\"jsonrpc\":\"2.0\",\"result\":{\"contents\":[{\"uri\":\"" +
                           content.uri + "\",\"text\":\"" + content.text + "\"}]}}";
                }
            }
        }
        return "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32002,\"message\":\"Resource not found\"}}";
    }

    return "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}";
}

std::vector<McpToolSchema> McpServer::listTools() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<McpToolSchema> result;
    result.reserve(impl_->tools.size());
    for (const auto& pair : impl_->tools) {
        result.push_back(pair.second);
    }
    return result;
}

McpToolResult McpServer::callTool(const std::string& name, const std::string& argsJson) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->toolHandlers.find(name);
    if (it == impl_->toolHandlers.end()) {
        McpToolResult r;
        r.success = false;
        r.errorMessage = "Tool not found: " + name;
        return r;
    }
    return it->second(argsJson);
}

std::vector<McpResource> McpServer::listResources() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<McpResource> result;
    result.reserve(impl_->resources.size());
    for (const auto& pair : impl_->resources) {
        result.push_back(pair.second);
    }
    return result;
}

McpResourceContent McpServer::readResource(const std::string& uri) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->resourceHandlers.find(uri);
    if (it == impl_->resourceHandlers.end()) {
        McpResourceContent empty;
        empty.uri = uri;
        return empty;
    }
    return it->second();
}

}
}
