#include "privacy/privacy.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

namespace synapse {
namespace privacy {

static constexpr int SOCKS5_TIMEOUT_MS = 5000;

struct Socks5Proxy::Impl {
    std::string proxyHost = "127.0.0.1";
    uint16_t proxyPort = 9050;
    int socket = -1;
    bool connected = false;
    bool authenticated = false;
    mutable std::mutex mtx;
    
    bool sendAll(const uint8_t* data, size_t len);
    bool recvAll(uint8_t* data, size_t len);
};

bool Socks5Proxy::Impl::sendAll(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(socket, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool Socks5Proxy::Impl::recvAll(uint8_t* data, size_t len) {
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(socket, data + received, len - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

Socks5Proxy::Socks5Proxy() : impl_(std::make_unique<Impl>()) {}
Socks5Proxy::~Socks5Proxy() { disconnect(); }

bool Socks5Proxy::connect(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    impl_->proxyHost = host;
    impl_->proxyPort = port;
    
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;

    impl_->socket = ::socket(res->ai_family, SOCK_STREAM, 0);
    if (impl_->socket < 0) { freeaddrinfo(res); return false; }

    // set port in address and connect
    if (res->ai_family == AF_INET) {
        struct sockaddr_in* a = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
        a->sin_port = htons(port);
    } else if (res->ai_family == AF_INET6) {
        struct sockaddr_in6* a6 = reinterpret_cast<struct sockaddr_in6*>(res->ai_addr);
        a6->sin6_port = htons(port);
    }

    int origFlags = fcntl(impl_->socket, F_GETFL, 0);
    if (origFlags >= 0) {
        fcntl(impl_->socket, F_SETFL, origFlags | O_NONBLOCK);
    }

    int rc = ::connect(impl_->socket, res->ai_addr, res->ai_addrlen);
    if (rc < 0) {
        if (errno != EINPROGRESS) {
            close(impl_->socket);
            impl_->socket = -1;
            freeaddrinfo(res);
            return false;
        }

        struct pollfd pfd;
        pfd.fd = impl_->socket;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int prc = poll(&pfd, 1, SOCKS5_TIMEOUT_MS);
        if (prc <= 0) {
            close(impl_->socket);
            impl_->socket = -1;
            freeaddrinfo(res);
            return false;
        }

        int soerr = 0;
        socklen_t slen = sizeof(soerr);
        if (getsockopt(impl_->socket, SOL_SOCKET, SO_ERROR, &soerr, &slen) < 0 || soerr != 0) {
            close(impl_->socket);
            impl_->socket = -1;
            freeaddrinfo(res);
            return false;
        }
    }

    if (origFlags >= 0) {
        fcntl(impl_->socket, F_SETFL, origFlags);
    }
    freeaddrinfo(res);

    struct timeval tv { SOCKS5_TIMEOUT_MS / 1000, (SOCKS5_TIMEOUT_MS % 1000) * 1000 };
    setsockopt(impl_->socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(impl_->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    impl_->connected = true;
    impl_->authenticated = false;
    auto reset = [&]() {
        if (impl_->socket >= 0) {
            close(impl_->socket);
            impl_->socket = -1;
        }
        impl_->connected = false;
        impl_->authenticated = false;
    };
    uint8_t greeting[3] = {0x05, 0x01, 0x00};
    if (!impl_->sendAll(greeting, 3)) {
        reset();
        return false;
    }
    uint8_t response[2];
    if (!impl_->recvAll(response, 2)) {
        reset();
        return false;
    }
    if (!(response[0] == 0x05 && response[1] == 0x00)) {
        reset();
        return false;
    }
    impl_->authenticated = true;
    return true;
}

void Socks5Proxy::disconnect() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->socket >= 0) {
        close(impl_->socket);
        impl_->socket = -1;
    }
    impl_->connected = false;
    impl_->authenticated = false;
}

bool Socks5Proxy::isConnected() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->connected;
}

bool Socks5Proxy::authenticate() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->connected) return false;
    if (impl_->authenticated) return true;
    return false;
}

bool Socks5Proxy::connectToTarget(const std::string& targetHost, uint16_t targetPort) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->connected) return false;
    if (!impl_->authenticated) {
        uint8_t greeting[3] = {0x05, 0x01, 0x00};
        if (!impl_->sendAll(greeting, 3)) return false;
        uint8_t response[2];
        if (!impl_->recvAll(response, 2)) return false;
        if (!(response[0] == 0x05 && response[1] == 0x00)) return false;
        impl_->authenticated = true;
    }
    
    std::vector<uint8_t> request;
    request.push_back(0x05);
    request.push_back(0x01);
    request.push_back(0x00);
    
    in_addr ipv4{};
    in6_addr ipv6{};
    if (inet_pton(AF_INET, targetHost.c_str(), &ipv4) == 1) {
        request.push_back(0x01);
        uint8_t* addr = reinterpret_cast<uint8_t*>(&ipv4);
        request.insert(request.end(), addr, addr + 4);
    } else if (inet_pton(AF_INET6, targetHost.c_str(), &ipv6) == 1) {
        request.push_back(0x04);
        uint8_t* addr = reinterpret_cast<uint8_t*>(&ipv6);
        request.insert(request.end(), addr, addr + 16);
    } else {
        request.push_back(0x03);
        request.push_back(static_cast<uint8_t>(targetHost.size()));
        request.insert(request.end(), targetHost.begin(), targetHost.end());
    }
    request.push_back((targetPort >> 8) & 0xFF);
    request.push_back(targetPort & 0xFF);
    
    if (!impl_->sendAll(request.data(), request.size())) return false;
    
    uint8_t header[4];
    if (!impl_->recvAll(header, 4)) return false;
    if (!(header[0] == 0x05 && header[1] == 0x00)) return false;
    
    uint8_t atyp = header[3];
    size_t addrLen = 0;
    if (atyp == 0x01) {
        addrLen = 4;
    } else if (atyp == 0x04) {
        addrLen = 16;
    } else if (atyp == 0x03) {
        uint8_t len = 0;
        if (!impl_->recvAll(&len, 1)) return false;
        addrLen = len;
    } else {
        return false;
    }
    
    std::vector<uint8_t> discard(addrLen + 2);
    if (!impl_->recvAll(discard.data(), discard.size())) return false;
    
    return true;
}

std::vector<uint8_t> Socks5Proxy::sendData(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->connected) return {};
    
    if (!impl_->sendAll(data.data(), data.size())) return {};
    
    std::vector<uint8_t> response;
    std::vector<uint8_t> buffer(4096);
    size_t maxBytes = 1024 * 1024;
    
    while (response.size() < maxBytes) {
        struct pollfd pfd;
        pfd.fd = impl_->socket;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 200);
        if (ret <= 0) break;
        if (!(pfd.revents & POLLIN)) break;
        ssize_t n = recv(impl_->socket, buffer.data(), buffer.size(), 0);
        if (n <= 0) break;
        response.insert(response.end(), buffer.begin(), buffer.begin() + n);
    }
    
    return response;
}

void Socks5Proxy::setProxy(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->proxyHost = host;
    impl_->proxyPort = port;
}

}
}
