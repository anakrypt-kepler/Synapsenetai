#include "web/rpc_server.h"
#include "web/web.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <cctype>
#include <openssl/rand.h>
#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

namespace synapse {
namespace web {

enum class RpcErrorCode {
    PARSE_ERROR = -32700,
    INVALID_REQUEST = -32600,
    METHOD_NOT_FOUND = -32601,
    INVALID_PARAMS = -32602,
    INTERNAL_ERROR = -32603,
    SERVER_ERROR = -32000,
    UNAUTHORIZED = -32001,
    RATE_LIMITED = -32002
};

struct RpcRequest {
    std::string id;
    std::string method;
    std::string params;
    std::string authToken;
    uint64_t timestamp;
    std::string clientIp;
};

struct RpcResponse {
    std::string id;
    std::string result;
    int errorCode;
    std::string errorMessage;
};

struct RpcMethod {
    std::string name;
    std::string description;
    std::function<std::string(const std::string&)> handler;
    bool requiresAuth;
    int rateLimit;
};

struct ClientSession {
    std::string sessionId;
    std::string clientIp;
    uint64_t connectedAt;
    uint64_t lastActivity;
    uint64_t requestCount;
    bool authenticated;
    std::string authToken;
};

struct RateLimitEntry {
    uint64_t windowStart;
    int requestCount;
};

static std::string escapeJsonString(const std::string& input) {
    std::string output;
    output.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    output += buf;
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

struct RpcServer::Impl {
    std::map<std::string, RpcMethod> methods;
    std::map<std::string, ClientSession> sessions;
    std::map<std::string, RateLimitEntry> rateLimits;
    std::map<std::string, RateLimitEntry> authTokenRateLimits;
    mutable std::mutex mtx;
    mutable std::mutex sessionMtx;
    std::atomic<bool> running;
    std::thread acceptThread;
    std::vector<std::thread> workerThreads;
    std::queue<int> connectionQueue;
    std::condition_variable cv;
    
    int serverSocket;
    uint16_t port;
    int rateLimitWindow;
    int maxConnections;
    int maxConnectionsPerIp;
    int globalRequestLimit;
    int requestTimeout;
    std::atomic<uint64_t> totalRequests;
    std::map<std::string, int> activeConnectionsByIp;
    uint64_t globalWindowStart;
    int globalWindowCount;
    
    std::function<bool(const std::string&)> authCallback;
    std::function<HttpEndpointResponse()> healthHandler;
    std::function<HttpEndpointResponse()> readinessHandler;
    
    void acceptLoop();
    void workerLoop();
    void handleConnection(int clientSocket);
    RpcResponse processRequest(const RpcRequest& request);
    std::string parseJsonRpc(const std::string& data, RpcRequest& request);
    std::string formatResponse(const RpcResponse& response);
    bool checkRateLimit(const std::string& clientIp, int limit);
    bool checkGlobalLimit();
    bool checkIpConnectionLimit(const std::string& clientIp);
    std::string generateSessionId();
    void cleanupSessions();
};

static std::string trimAscii(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

static bool startsWithIgnoreCase(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

static std::string parseAuthorizationValue(const std::string& rawHeaderValue) {
    const std::string value = trimAscii(rawHeaderValue);
    if (startsWithIgnoreCase(value, "Bearer ")) {
        return trimAscii(value.substr(7));
    }
    if (startsWithIgnoreCase(value, "Basic ")) {
        return "Basic " + trimAscii(value.substr(6));
    }
    return value;
}

static const char* httpStatusText(int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

RpcServer::RpcServer() : impl_(std::make_unique<Impl>()) {
    impl_->running = false;
    impl_->serverSocket = -1;
    impl_->port = 8332;
    impl_->rateLimitWindow = 60;
    impl_->maxConnections = 100;
    impl_->maxConnectionsPerIp = 10;
    impl_->globalRequestLimit = 10000;
    impl_->globalWindowStart = 0;
    impl_->globalWindowCount = 0;
    impl_->requestTimeout = 30;
    impl_->totalRequests = 0;
}

RpcServer::~RpcServer() {
    stop();
}

bool RpcServer::start(uint16_t port, const std::string& bindAddress) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->running) return false;
    
    impl_->port = port;
    
    impl_->serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->serverSocket < 0) return false;
    
    int opt = 1;
    setsockopt(impl_->serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (bindAddress.empty() || bindAddress == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (::inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1) {
        close(impl_->serverSocket);
        impl_->serverSocket = -1;
        return false;
    }
    addr.sin_port = htons(port);
    
    if (bind(impl_->serverSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(impl_->serverSocket);
        impl_->serverSocket = -1;
        return false;
    }
    
    if (listen(impl_->serverSocket, impl_->maxConnections) < 0) {
        close(impl_->serverSocket);
        impl_->serverSocket = -1;
        return false;
    }
    
    impl_->running = true;
    
    impl_->acceptThread = std::thread(&Impl::acceptLoop, impl_.get());
    
    int numWorkers = std::thread::hardware_concurrency();
    if (numWorkers < 2) numWorkers = 2;
    
    for (int i = 0; i < numWorkers; i++) {
        impl_->workerThreads.emplace_back(&Impl::workerLoop, impl_.get());
    }
    
    return true;
}

void RpcServer::stop() {
    impl_->running = false;
    impl_->cv.notify_all();
    
    if (impl_->serverSocket >= 0) {
        shutdown(impl_->serverSocket, SHUT_RDWR);
        close(impl_->serverSocket);
        impl_->serverSocket = -1;
    }
    
    if (impl_->acceptThread.joinable()) {
        impl_->acceptThread.join();
    }
    
    for (auto& thread : impl_->workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    impl_->workerThreads.clear();
}

bool RpcServer::isRunning() const {
    return impl_->running;
}

void RpcServer::registerMethod(const std::string& name,
                                std::function<std::string(const std::string&)> handler,
                                bool requiresAuth,
                                int rateLimit) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    RpcMethod method;
    method.name = name;
    method.handler = handler;
    method.requiresAuth = requiresAuth;
    method.rateLimit = rateLimit;
    
    impl_->methods[name] = method;
}

void RpcServer::unregisterMethod(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->methods.erase(name);
}

void RpcServer::setAuthCallback(std::function<bool(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->authCallback = callback;
}

void RpcServer::setHealthEndpointHandler(std::function<HttpEndpointResponse()> handler) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->healthHandler = std::move(handler);
}

void RpcServer::setReadinessEndpointHandler(std::function<HttpEndpointResponse()> handler) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->readinessHandler = std::move(handler);
}

void RpcServer::setRateLimitWindow(int seconds) {
    impl_->rateLimitWindow = seconds;
}

void RpcServer::setMaxConnections(int max) {
    impl_->maxConnections = max;
}

void RpcServer::setMaxConnectionsPerIp(int max) {
    impl_->maxConnectionsPerIp = max;
}

void RpcServer::setGlobalRequestLimit(int limit) {
    impl_->globalRequestLimit = limit;
}

void RpcServer::setRequestTimeout(int seconds) {
    impl_->requestTimeout = seconds;
}

size_t RpcServer::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(impl_->sessionMtx);
    return impl_->sessions.size();
}

size_t RpcServer::getMethodCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->methods.size();
}

uint64_t RpcServer::getTotalRequests() const {
    return impl_->totalRequests;
}

void RpcServer::Impl::acceptLoop() {
    while (running) {
        struct pollfd pfd;
        pfd.fd = serverSocket;
        pfd.events = POLLIN;
        
        int ret = poll(&pfd, 1, 1000);
        if (ret <= 0) continue;
        
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        int clientSocket = accept(serverSocket, 
                                   reinterpret_cast<struct sockaddr*>(&clientAddr),
                                   &clientLen);
        
        if (clientSocket < 0) continue;
        
        {
            std::lock_guard<std::mutex> lock(mtx);
            connectionQueue.push(clientSocket);
        }
        cv.notify_one();
    }
}

void RpcServer::Impl::workerLoop() {
    while (running) {
        int clientSocket = -1;
        
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(lock, std::chrono::seconds(1), [this] {
                return !connectionQueue.empty() || !running;
            });
            
            if (!running && connectionQueue.empty()) break;
            
            if (!connectionQueue.empty()) {
                clientSocket = connectionQueue.front();
                connectionQueue.pop();
            }
        }
        
        if (clientSocket >= 0) {
            handleConnection(clientSocket);
            close(clientSocket);
        }
    }
}

void RpcServer::Impl::handleConnection(int clientSocket) {
    struct timeval tv;
    tv.tv_sec = requestTimeout;
    tv.tv_usec = 0;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    char buffer[65536];
    std::string requestData;
    
    sockaddr_in peerAddr{};
    socklen_t peerLen = sizeof(peerAddr);
    std::string clientIp = "unknown";
    if (::getpeername(clientSocket, reinterpret_cast<sockaddr*>(&peerAddr), &peerLen) == 0) {
        char ipBuf[INET_ADDRSTRLEN] = {0};
        if (::inet_ntop(AF_INET, &peerAddr.sin_addr, ipBuf, sizeof(ipBuf)) != nullptr) {
            clientIp = ipBuf;
        }
    }

    while (running) {
        ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead <= 0) break;
        
        buffer[bytesRead] = '\0';
        requestData += buffer;
        
        size_t headerEnd = requestData.find("\r\n\r\n");
        if (headerEnd == std::string::npos) continue;
        std::string headers = requestData.substr(0, headerEnd);
        size_t requestLineEnd = headers.find("\r\n");
        std::string requestLine = requestLineEnd == std::string::npos ? headers : headers.substr(0, requestLineEnd);
        std::istringstream requestLineStream(requestLine);
        std::string httpMethod;
        std::string httpPath;
        std::string httpVersion;
        requestLineStream >> httpMethod >> httpPath >> httpVersion;
        
        size_t contentLength = 0;
        size_t clPos = headers.find("Content-Length:");
        if (clPos != std::string::npos) {
            size_t clEnd = headers.find("\r\n", clPos);
            std::string clStr = headers.substr(clPos + 15, clEnd - clPos - 15);
            contentLength = std::stoul(clStr);
            if (contentLength > 4 * 1024 * 1024) {
                std::string httpResp = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(clientSocket, httpResp.c_str(), httpResp.length(), 0);
                return;
            }
        }

        std::string authHeaderValue;
        size_t authPos = headers.find("\r\nAuthorization:");
        if (authPos == std::string::npos && startsWithIgnoreCase(headers, "Authorization:")) {
            authPos = 0;
        }
        if (authPos != std::string::npos) {
            size_t valueStart = authPos == 0 ? std::string("Authorization:").size()
                                             : authPos + 2 + std::string("Authorization:").size();
            size_t authEnd = headers.find("\r\n", valueStart);
            authHeaderValue = trimAscii(headers.substr(valueStart, authEnd - valueStart));
        }

        if (httpMethod == "GET" && (httpPath == "/health" || httpPath == "/ready")) {
            std::function<HttpEndpointResponse()> endpointHandler;
            {
                std::lock_guard<std::mutex> lock(mtx);
                endpointHandler = (httpPath == "/health") ? healthHandler : readinessHandler;
            }

            HttpEndpointResponse endpointResponse;
            if (endpointHandler) {
                endpointResponse = endpointHandler();
            } else if (httpPath == "/health") {
                endpointResponse.statusCode = 200;
                endpointResponse.body = R"({"status":"ok","endpoint":"health"})";
            } else {
                endpointResponse.statusCode = 200;
                endpointResponse.body = R"({"status":"ready","endpoint":"ready"})";
            }

            std::stringstream httpResponse;
            httpResponse << "HTTP/1.1 " << endpointResponse.statusCode << " "
                         << httpStatusText(endpointResponse.statusCode) << "\r\n";
            httpResponse << "Content-Type: " << endpointResponse.contentType << "\r\n";
            httpResponse << "Content-Length: " << endpointResponse.body.length() << "\r\n";
            httpResponse << "Connection: keep-alive\r\n";
            httpResponse << "\r\n";
            httpResponse << endpointResponse.body;

            std::string httpStr = httpResponse.str();
            send(clientSocket, httpStr.c_str(), httpStr.length(), 0);
            totalRequests++;
            requestData.clear();
            continue;
        }
        
        std::string body = requestData.substr(headerEnd + 4);
        if (body.length() < contentLength) continue;
        
        RpcRequest request;
        std::string parseError = parseJsonRpc(body, request);
        request.clientIp = clientIp;
        if (request.authToken.empty() && !authHeaderValue.empty()) {
            request.authToken = parseAuthorizationValue(authHeaderValue);
        }
        
        RpcResponse response;
        if (!parseError.empty()) {
            response.id = request.id;
            response.errorCode = static_cast<int>(RpcErrorCode::PARSE_ERROR);
            response.errorMessage = parseError;
        } else {
            response = processRequest(request);
        }
        
        std::string responseStr = formatResponse(response);
        
        std::stringstream httpResponse;
        httpResponse << "HTTP/1.1 200 OK\r\n";
        httpResponse << "Content-Type: application/json\r\n";
        httpResponse << "Content-Length: " << responseStr.length() << "\r\n";
        httpResponse << "Connection: keep-alive\r\n";
        httpResponse << "\r\n";
        httpResponse << responseStr;
        
        std::string httpStr = httpResponse.str();
        send(clientSocket, httpStr.c_str(), httpStr.length(), 0);
        
        totalRequests++;
        requestData.clear();
    }
}

RpcResponse RpcServer::Impl::processRequest(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.errorCode = 0;
    
    std::lock_guard<std::mutex> lock(mtx);
    
    auto it = methods.find(request.method);
    if (it == methods.end()) {
        response.errorCode = static_cast<int>(RpcErrorCode::METHOD_NOT_FOUND);
        response.errorMessage = "Method not found: " + request.method;
        return response;
    }
    
    const RpcMethod& method = it->second;
    
    const bool requireAuth = method.requiresAuth || static_cast<bool>(authCallback);
    if (requireAuth) {
        if (request.authToken.empty()) {
            response.errorCode = static_cast<int>(RpcErrorCode::UNAUTHORIZED);
            response.errorMessage = "Authentication required";
            return response;
        }
        
        if (!authCallback || !authCallback(request.authToken)) {
            response.errorCode = static_cast<int>(RpcErrorCode::UNAUTHORIZED);
            response.errorMessage = "Invalid authentication token";
            return response;
        }
    }
    
    if (!checkGlobalLimit()) {
        response.errorCode = static_cast<int>(RpcErrorCode::RATE_LIMITED);
        response.errorMessage = "Server overloaded";
        return response;
    }

    if (!checkRateLimit(request.clientIp, method.rateLimit)) {
        response.errorCode = static_cast<int>(RpcErrorCode::RATE_LIMITED);
        response.errorMessage = "Rate limit exceeded";
        return response;
    }

    // Per-auth-token rate limiting
    if (!request.authToken.empty()) {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto tokenIt = authTokenRateLimits.find(request.authToken);
        if (tokenIt == authTokenRateLimits.end()) {
            authTokenRateLimits[request.authToken] = {now, 1};
        } else {
            RateLimitEntry& entry = tokenIt->second;
            if (now - entry.windowStart >= static_cast<uint64_t>(rateLimitWindow)) {
                entry.windowStart = now;
                entry.requestCount = 1;
            } else if (entry.requestCount >= method.rateLimit) {
                response.errorCode = static_cast<int>(RpcErrorCode::RATE_LIMITED);
                response.errorMessage = "Rate limit exceeded";
                return response;
            } else {
                entry.requestCount++;
            }
        }
    }
    
    try {
        response.result = method.handler(request.params);
    } catch (const std::exception& e) {
        response.errorCode = static_cast<int>(RpcErrorCode::INTERNAL_ERROR);
        response.errorMessage = "Internal server error";
    }
    
    return response;
}

std::string RpcServer::Impl::parseJsonRpc(const std::string& data, RpcRequest& request) {
    request.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    nlohmann::json parsed = nlohmann::json::parse(data, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return "Invalid JSON-RPC request";
    }

    auto idIt = parsed.find("id");
    if (idIt != parsed.end()) {
        if (idIt->is_string()) {
            request.id = idIt->get<std::string>();
        } else if (idIt->is_number_integer()) {
            request.id = std::to_string(idIt->get<int64_t>());
        } else if (idIt->is_number_unsigned()) {
            request.id = std::to_string(idIt->get<uint64_t>());
        } else if (idIt->is_number_float()) {
            request.id = std::to_string(idIt->get<double>());
        }
    }

    auto methodIt = parsed.find("method");
    if (methodIt == parsed.end() || !methodIt->is_string()) {
        return "Missing method field";
    }
    request.method = methodIt->get<std::string>();

    auto paramsIt = parsed.find("params");
    if (paramsIt != parsed.end()) {
        request.params = paramsIt->dump();
    } else {
        request.params.clear();
    }

    auto authIt = parsed.find("authToken");
    if (authIt != parsed.end() && authIt->is_string()) {
        request.authToken = authIt->get<std::string>();
    }

    return "";
}

std::string RpcServer::Impl::formatResponse(const RpcResponse& response) {
    std::stringstream ss;
    ss << "{\"jsonrpc\":\"2.0\"";
    
    if (!response.id.empty()) {
        ss << ",\"id\":";
        bool isNumeric = !response.id.empty() && 
                         std::all_of(response.id.begin(), response.id.end(), ::isdigit);
        if (isNumeric) {
            ss << response.id;
        } else {
            ss << "\"" << escapeJsonString(response.id) << "\"";
        }
    }
    
    if (response.errorCode != 0) {
        ss << ",\"error\":{\"code\":" << response.errorCode;
        ss << ",\"message\":\"" << response.errorMessage << "\"}";
    } else {
        ss << ",\"result\":" << response.result;
    }
    
    ss << "}";
    return ss.str();
}

bool RpcServer::Impl::checkRateLimit(const std::string& clientIp, int limit) {
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    auto it = rateLimits.find(clientIp);
    if (it == rateLimits.end()) {
        rateLimits[clientIp] = {now, 1};
        return true;
    }
    
    RateLimitEntry& entry = it->second;
    
    if (now - entry.windowStart >= static_cast<uint64_t>(rateLimitWindow)) {
        entry.windowStart = now;
        entry.requestCount = 1;
        return true;
    }
    
    if (entry.requestCount >= limit) {
        return false;
    }
    
    entry.requestCount++;
    return true;
}

bool RpcServer::Impl::checkGlobalLimit() {
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now - globalWindowStart >= static_cast<uint64_t>(rateLimitWindow)) {
        globalWindowStart = now;
        globalWindowCount = 1;
        return true;
    }
    if (globalWindowCount >= globalRequestLimit) return false;
    globalWindowCount++;
    return true;
}

bool RpcServer::Impl::checkIpConnectionLimit(const std::string& clientIp) {
    auto it = activeConnectionsByIp.find(clientIp);
    if (it == activeConnectionsByIp.end()) return true;
    return it->second < maxConnectionsPerIp;
}

std::string RpcServer::Impl::generateSessionId() {
    uint8_t randomBytes[16];
    if (RAND_bytes(randomBytes, sizeof(randomBytes)) != 1) {
        // Fallback to timestamp if CSPRNG fails (should not happen)
        std::stringstream ss;
        uint64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        ss << std::hex << now;
        return ss.str();
    }
    std::stringstream ss;
    for (int i = 0; i < 16; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(randomBytes[i]);
    }
    return ss.str();
}

void RpcServer::Impl::cleanupSessions() {
    std::lock_guard<std::mutex> lock(sessionMtx);
    
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    for (auto it = sessions.begin(); it != sessions.end();) {
        if (now - it->second.lastActivity > 3600) {
            it = sessions.erase(it);
        } else {
            ++it;
        }
    }
}

}
}
