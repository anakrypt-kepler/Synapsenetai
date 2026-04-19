#include "web/web.h"
#include <mutex>
#include <thread>
#include <chrono>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

namespace synapse {
namespace web {

struct TorEngine::Impl {
    TorConfig config;
    mutable std::mutex mtx;
    bool connected = false;
    int controlSocket = -1;
    std::string currentExitNode;
    TorStats stats{};
    std::function<void(bool)> connectionCallback;
    std::function<void(const std::string&)> circuitCallback;
    uint32_t maxRetries = 3;
    
    bool connectControl();
    void disconnectControl();
    bool sendControlCommand(const std::string& cmd, std::string& response);
    bool authenticate();
    std::string getCircuitInfo();
    std::string socksConnect(const std::string& host, uint16_t port, const std::string& path);
};

bool TorEngine::Impl::connectControl() {
    if (controlSocket >= 0) return true;
    
    controlSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (controlSocket < 0) return false;
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.controlPort);
    inet_pton(AF_INET, config.controlHost.c_str(), &addr.sin_addr);
    
    if (connect(controlSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(controlSocket);
        controlSocket = -1;
        return false;
    }
    
    return authenticate();
}

void TorEngine::Impl::disconnectControl() {
    if (controlSocket >= 0) {
        close(controlSocket);
        controlSocket = -1;
    }
}

bool TorEngine::Impl::sendControlCommand(const std::string& cmd, std::string& response) {
    if (controlSocket < 0) return false;
    
    std::string fullCmd = cmd + "\r\n";
    if (send(controlSocket, fullCmd.c_str(), fullCmd.size(), 0) < 0) {
        return false;
    }
    
    char buffer[4096];
    ssize_t received = recv(controlSocket, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return false;
    
    buffer[received] = '\0';
    response = buffer;
    
    return response.find("250") != std::string::npos;
}

bool TorEngine::Impl::authenticate() {
    std::string response;
    std::string authCmd = "AUTHENTICATE \"" + config.controlPassword + "\"";
    return sendControlCommand(authCmd, response);
}

std::string TorEngine::Impl::getCircuitInfo() {
    std::string response;
    if (!sendControlCommand("GETINFO circuit-status", response)) {
        return "";
    }
    
    size_t exitPos = response.find("EXIT");
    if (exitPos != std::string::npos) {
        size_t start = response.rfind(' ', exitPos);
        size_t end = response.find('\n', exitPos);
        if (start != std::string::npos && end != std::string::npos) {
            return response.substr(start + 1, end - start - 1);
        }
    }
    
    return "";
}

std::string TorEngine::Impl::socksConnect(const std::string& host, uint16_t port, const std::string& path) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";
    
    uint32_t timeout = config.circuitTimeout > 0 ? config.circuitTimeout : 10;
    struct timeval tv;
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    struct sockaddr_in proxyAddr;
    std::memset(&proxyAddr, 0, sizeof(proxyAddr));
    proxyAddr.sin_family = AF_INET;
    proxyAddr.sin_port = htons(config.socksPort);
    inet_pton(AF_INET, config.socksHost.c_str(), &proxyAddr.sin_addr);
    
    if (connect(sock, reinterpret_cast<struct sockaddr*>(&proxyAddr), sizeof(proxyAddr)) < 0) {
        close(sock);
        return "";
    }
    
    uint8_t greeting[3] = {0x05, 0x01, 0x00};
    send(sock, greeting, 3, 0);
    
    uint8_t response[2];
    if (recv(sock, response, 2, 0) != 2 || response[0] != 0x05) {
        close(sock);
        return "";
    }
    
    std::vector<uint8_t> request;
    request.push_back(0x05);
    request.push_back(0x01);
    request.push_back(0x00);
    request.push_back(0x03);
    request.push_back(static_cast<uint8_t>(host.length()));
    request.insert(request.end(), host.begin(), host.end());
    request.push_back((port >> 8) & 0xff);
    request.push_back(port & 0xff);
    
    send(sock, request.data(), request.size(), 0);
    
    uint8_t connectResponse[10];
    if (recv(sock, connectResponse, 10, 0) < 4 || connectResponse[1] != 0x00) {
        close(sock);
        return "";
    }
    
    std::string result;
    std::stringstream requestStream;
    requestStream << "GET " << (path.empty() ? "/" : path) << " HTTP/1.1\r\n";
    requestStream << "Host: " << host << "\r\n";
    requestStream << "User-Agent: Mozilla/5.0 (compatible; SynapseNet/1.0)\r\n";
    requestStream << "Accept: text/html\r\n";
    requestStream << "Connection: close\r\n\r\n";
    std::string reqStr = requestStream.str();
    if (send(sock, reqStr.c_str(), reqStr.size(), 0) < 0) {
        close(sock);
        return "";
    }
    char buffer[4096];
    ssize_t bytesRead;
    
    size_t maxBytes = 1024 * 1024;
    while ((bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
        if (result.size() >= maxBytes) break;
    }
    
    close(sock);
    
    size_t headerEnd = result.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        result = result.substr(headerEnd + 4);
    }
    return result;
}

TorEngine::TorEngine() : impl_(std::make_unique<Impl>()) {}
TorEngine::~TorEngine() { shutdown(); }

bool TorEngine::init(const TorConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    impl_->config = config;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.socksPort);
    inet_pton(AF_INET, config.socksHost.c_str(), &addr.sin_addr);
    
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    impl_->connected = (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    close(sock);
    
    if (impl_->connected && impl_->connectionCallback) {
        impl_->connectionCallback(true);
    }
    
    return impl_->connected;
}

void TorEngine::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    impl_->disconnectControl();
    impl_->connected = false;
    
    if (impl_->connectionCallback) {
        impl_->connectionCallback(false);
    }
}

bool TorEngine::isConnected() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->connected;
}

std::string TorEngine::fetch(const std::string& url) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->connected) return "";
    
    std::string host;
    std::string path = "/";
    uint16_t port = 80;
    
    size_t start = url.find("://");
    if (start != std::string::npos) {
        start += 3;
        size_t end = url.find('/', start);
        std::string hostPort = end != std::string::npos ? url.substr(start, end - start) : url.substr(start);
        if (end != std::string::npos) {
            path = url.substr(end);
        }
        size_t colon = hostPort.find(':');
        if (colon != std::string::npos) {
            host = hostPort.substr(0, colon);
            port = static_cast<uint16_t>(std::stoi(hostPort.substr(colon + 1)));
        } else {
            host = hostPort;
        }
    }
    
    if (host.empty()) return "";
    
    impl_->stats.requestsSent++;
    std::string response = impl_->socksConnect(host, port, path);
    impl_->stats.bytesTransferred += response.size();
    
    return response;
}

std::string TorEngine::fetchOnion(const std::string& onionUrl) {
    if (!isOnionUrl(onionUrl)) return "";
    return fetch(onionUrl);
}

bool TorEngine::newCircuit() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->connectControl()) return false;
    
    std::string response;
    bool success = impl_->sendControlCommand("SIGNAL NEWNYM", response);
    
    if (success) {
        impl_->stats.circuitsUsed++;
        impl_->currentExitNode = impl_->getCircuitInfo();
        
        if (impl_->circuitCallback) {
            impl_->circuitCallback(impl_->currentExitNode);
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return success;
}

std::string TorEngine::getExitNode() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->currentExitNode;
}

void TorEngine::setConfig(const TorConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config = config;
}

TorConfig TorEngine::getConfig() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->config;
}

void TorEngine::setTimeout(uint32_t seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.circuitTimeout = seconds;
}

void TorEngine::setMaxRetries(uint32_t retries) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxRetries = retries;
}

void TorEngine::onConnectionChange(std::function<void(bool)> callback) {
    impl_->connectionCallback = callback;
}

void TorEngine::onCircuitChange(std::function<void(const std::string&)> callback) {
    impl_->circuitCallback = callback;
}

TorEngine::TorStats TorEngine::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    TorStats stats = impl_->stats;
    stats.connected = impl_->connected;
    return stats;
}

}
}
