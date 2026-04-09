#include "web/rpc_server.h"
#include "../third_party/llama.cpp/vendor/nlohmann/json.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

uint16_t findFreePort() {
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return 0;
    }

    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
        ::close(sock);
        return 0;
    }
    const uint16_t port = ntohs(bound.sin_port);
    ::close(sock);
    return port;
}

bool httpPostJson(uint16_t port,
                  const std::string& body,
                  const std::string& authorizationHeader,
                  std::string& responseBody);

bool httpRequest(uint16_t port,
                 const std::string& method,
                 const std::string& path,
                 const std::string& body,
                 const std::string& authorizationHeader,
                 int& statusCode,
                 std::string& responseBody) {
    responseBody.clear();
    statusCode = 0;

    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return false;
    }
    timeval timeout{};
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: 127.0.0.1\r\n";
    if (!body.empty()) {
        req << "Content-Type: application/json\r\n";
    }
    if (!authorizationHeader.empty()) {
        req << "Authorization: " << authorizationHeader << "\r\n";
    }
    req << "Content-Length: " << body.size() << "\r\n";
    req << "Connection: close\r\n\r\n";
    req << body;
    const std::string request = req.str();

    size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t n = ::send(sock, request.data() + sent, request.size() - sent, 0);
        if (n <= 0) {
            ::close(sock);
            return false;
        }
        sent += static_cast<size_t>(n);
    }

    std::string response;
    char buffer[4096];
    while (true) {
        const ssize_t n = ::recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        response.append(buffer, buffer + n);
        const size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd == std::string::npos) continue;
        const size_t statusEnd = response.find("\r\n");
        if (statusEnd == std::string::npos) continue;
        const std::string statusLine = response.substr(0, statusEnd);
        if (statusCode == 0) {
            std::istringstream statusStream(statusLine);
            std::string httpVersion;
            statusStream >> httpVersion >> statusCode;
        }
        const size_t clPos = response.find("Content-Length:");
        if (clPos == std::string::npos) continue;
        const size_t clEnd = response.find("\r\n", clPos);
        if (clEnd == std::string::npos) continue;
        const std::string clString = response.substr(clPos + 15, clEnd - (clPos + 15));
        size_t contentLength = 0;
        try {
            contentLength = static_cast<size_t>(std::stoul(clString));
        } catch (...) {
            ::close(sock);
            return false;
        }
        const size_t bodyStart = headerEnd + 4;
        if (response.size() >= bodyStart + contentLength) {
            responseBody = response.substr(bodyStart, contentLength);
            ::close(sock);
            return true;
        }
    }
    ::close(sock);
    return false;
}

bool httpPostJson(uint16_t port,
                  const std::string& body,
                  const std::string& authorizationHeader,
                  std::string& responseBody) {
    int statusCode = 0;
    return httpRequest(port, "POST", "/", body, authorizationHeader, statusCode, responseBody);
}

}

int main() {
    const uint16_t port = findFreePort();
    if (!check(port != 0, "failed to allocate a test port")) return 1;
    synapse::web::RpcServer server;
    server.setAuthCallback([](const std::string& token) {
        return token == "cookie-token" || token == "Basic dXNlcjpwYXNz";
    });
    server.setHealthEndpointHandler([]() {
        synapse::web::HttpEndpointResponse response;
        response.statusCode = 200;
        response.body = R"({"status":"ok","version":"0.1.0-beta"})";
        return response;
    });
    server.setReadinessEndpointHandler([]() {
        synapse::web::HttpEndpointResponse response;
        response.statusCode = 503;
        response.body = R"({"status":"degraded","reason":"tor_not_ready"})";
        return response;
    });
    if (!check(server.start(port, "127.0.0.1"), "rpc server failed to start")) return 1;
    server.registerMethod("ping", [](const std::string&) {
        return std::string(R"({"ok":true})");
    }, false, 100);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string request = R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{}})";

    std::string body;
    if (!check(httpPostJson(port, request, "", body), "unauthenticated RPC request failed")) return 1;
    auto parsed = nlohmann::json::parse(body);
    if (!check(parsed.contains("error"), "expected RPC auth error")) return 1;
    if (!check(parsed["error"]["code"].get<int>() == -32001, "unexpected auth error code")) return 1;

    if (!check(httpPostJson(port, request, "Bearer cookie-token", body), "bearer-auth RPC request failed")) return 1;
    parsed = nlohmann::json::parse(body);
    if (!check(parsed.contains("result"), "expected bearer-auth RPC result")) return 1;
    if (!check(parsed["result"]["ok"].get<bool>(), "bearer-auth RPC result mismatch")) return 1;

    if (!check(httpPostJson(port, request, "Basic dXNlcjpwYXNz", body), "basic-auth RPC request failed")) return 1;
    parsed = nlohmann::json::parse(body);
    if (!check(parsed.contains("result"), "expected basic-auth RPC result")) return 1;
    if (!check(parsed["result"]["ok"].get<bool>(), "basic-auth RPC result mismatch")) return 1;

    if (!check(httpPostJson(port, request, "Bearer wrong-token", body), "invalid-token RPC request failed")) return 1;
    parsed = nlohmann::json::parse(body);
    if (!check(parsed.contains("error"), "expected invalid-token RPC error")) return 1;
    if (!check(parsed["error"]["code"].get<int>() == -32001, "unexpected invalid-token error code")) return 1;

    int statusCode = 0;
    if (!check(httpRequest(port, "GET", "/health", "", "", statusCode, body), "health endpoint request failed")) return 1;
    if (!check(statusCode == 200, "health endpoint status code mismatch")) return 1;
    parsed = nlohmann::json::parse(body);
    if (!check(parsed.value("status", std::string()) == "ok", "health endpoint payload mismatch")) return 1;

    if (!check(httpRequest(port, "GET", "/ready", "", "", statusCode, body), "ready endpoint request failed")) return 1;
    if (!check(statusCode == 503, "ready endpoint status code mismatch")) return 1;
    parsed = nlohmann::json::parse(body);
    if (!check(parsed.value("reason", std::string()) == "tor_not_ready", "ready endpoint payload mismatch")) return 1;

    server.stop();
    std::cout << "rpc server tests passed\n";
    return 0;
}
