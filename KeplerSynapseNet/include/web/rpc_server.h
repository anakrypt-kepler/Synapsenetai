#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace synapse::web {

struct HttpEndpointResponse {
    int statusCode = 200;
    std::string contentType = "application/json";
    std::string body = "{}";
};

class RpcServer {
public:
    RpcServer();
    ~RpcServer();

    bool start(uint16_t port, const std::string& bindAddress = "127.0.0.1");
    void stop();
    bool isRunning() const;

    void registerMethod(const std::string& name,
                        std::function<std::string(const std::string&)> handler,
                        bool requiresAuth = false,
                        int rateLimit = 100);
    void unregisterMethod(const std::string& name);

    void setAuthCallback(std::function<bool(const std::string&)> callback);
    void setHealthEndpointHandler(std::function<HttpEndpointResponse()> handler);
    void setReadinessEndpointHandler(std::function<HttpEndpointResponse()> handler);
    void setRateLimitWindow(int seconds);
    void setMaxConnections(int max);
    void setMaxConnectionsPerIp(int max);
    void setGlobalRequestLimit(int limit);
    void setRequestTimeout(int seconds);

    size_t getConnectionCount() const;
    size_t getMethodCount() const;
    uint64_t getTotalRequests() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace synapse::web
