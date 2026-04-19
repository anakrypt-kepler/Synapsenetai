#include "privacy/privacy.h"
#include "utils/logger.h"
#include <mutex>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <ctime>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace synapse {
namespace privacy {

namespace {

constexpr const char* kTorEd25519V3Prefix = "ED25519-V3:";

std::string normalizeStoredPrivateKeySpec(const std::string& storedKey) {
    if (storedKey.empty()) return "";
    if (storedKey.rfind(kTorEd25519V3Prefix, 0) == 0) {
        return storedKey;
    }
    return std::string(kTorEd25519V3Prefix) + storedKey;
}

}

struct ControlConnection {
    int sock = -1;
    std::string buffer;
    
    bool connectTo(const std::string& host, uint16_t port) {
        sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            close(sock);
            sock = -1;
            return false;
        }
        
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(sock);
            sock = -1;
            return false;
        }
        return true;
    }
    
    void closeConn() {
        if (sock >= 0) {
            close(sock);
            sock = -1;
        }
        buffer.clear();
    }
    
    bool sendLine(const std::string& line) {
        std::string data = line + "\r\n";
        size_t sent = 0;
        while (sent < data.size()) {
            ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }
    
    bool readLine(std::string& line) {
        while (true) {
            size_t pos = buffer.find('\n');
            if (pos != std::string::npos) {
                line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                return true;
            }
            
            char temp[1024];
            ssize_t n = recv(sock, temp, sizeof(temp), 0);
            if (n <= 0) return false;
            buffer.append(temp, temp + n);
        }
    }
    
    bool readResponse(std::vector<std::string>& lines) {
        std::string line;
        while (readLine(line)) {
            lines.push_back(line);
            if (line.size() >= 4) {
                if (line.rfind("250 ", 0) == 0) return true;
                if (line[0] == '5' && line[3] == ' ') return false;
            }
        }
        return false;
    }
};

struct OnionService::Impl {
    mutable std::mutex mtx;
    std::string serviceDir;
    std::string hostname;
    std::string serviceId;
    std::string privateKey;
    uint16_t virtualPort = 0;
    uint16_t targetPort = 0;
    bool running = false;
    uint64_t rotationInterval = 86400;
    uint64_t lastRotation = 0;
    bool autoRotation = false;
    std::string controlHost = "127.0.0.1";
    uint16_t controlPort = 9051;
    std::string controlPassword;
    std::string controlCookiePath;
    
    bool loadState() {
        if (serviceDir.empty()) return false;
        std::ifstream keyFile(serviceDir + "/private_key");
        if (keyFile.good()) {
            std::getline(keyFile, privateKey);
        }
        keyFile.close();
        
        std::ifstream hostFile(serviceDir + "/hostname");
        if (hostFile.good()) {
            std::getline(hostFile, hostname);
            if (hostname.size() > 6 && hostname.rfind(".onion") == hostname.size() - 6) {
                serviceId = hostname.substr(0, hostname.size() - 6);
            }
        }
        hostFile.close();
        return true;
    }
    
    bool saveState() {
        if (serviceDir.empty() || serviceId.empty()) return false;
        std::filesystem::create_directories(serviceDir);
        std::ofstream keyFile(serviceDir + "/private_key", std::ios::trunc);
        if (!privateKey.empty()) {
            keyFile << privateKey;
        }
        keyFile.close();
        
        std::ofstream hostFile(serviceDir + "/hostname", std::ios::trunc);
        hostFile << (serviceId + ".onion");
        hostFile.close();
        return true;
    }
    
    static std::string toHex(const std::vector<uint8_t>& data) {
        static const char hex[] = "0123456789abcdef";
        std::string out;
        out.reserve(data.size() * 2);
        for (uint8_t b : data) {
            out.push_back(hex[b >> 4]);
            out.push_back(hex[b & 0x0f]);
        }
        return out;
    }
    
    std::string loadCookieHex() {
        std::vector<std::string> paths;
        if (!controlCookiePath.empty()) {
            paths.push_back(controlCookiePath);
        } else {
            const char* home = std::getenv("HOME");
            if (home) {
                paths.push_back(std::string(home) + "/.tor/control_auth_cookie");
            }
            paths.push_back("/run/tor/control.authcookie");
            paths.push_back("/var/run/tor/control.authcookie");
            paths.push_back("/var/lib/tor/control.authcookie");
            paths.push_back("/var/lib/tor/control_auth_cookie");
        }
        
        for (const auto& path : paths) {
            std::ifstream f(path, std::ios::binary);
            if (!f.good()) continue;
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (!data.empty()) return toHex(data);
        }
        return "";
    }
    
    bool authenticate(ControlConnection& conn) {
        if (!controlPassword.empty()) {
            std::vector<std::string> lines;
            if (!conn.sendLine("AUTHENTICATE \"" + controlPassword + "\"")) return false;
            return conn.readResponse(lines);
        }
        std::string cookieHex = loadCookieHex();
        if (!cookieHex.empty()) {
            std::vector<std::string> lines;
            if (!conn.sendLine("AUTHENTICATE " + cookieHex)) return false;
            return conn.readResponse(lines);
        }
        std::vector<std::string> lines;
        if (!conn.sendLine("AUTHENTICATE")) return false;
        return conn.readResponse(lines);
    }
    
    bool sendCommand(const std::string& cmd, std::vector<std::string>& lines) {
        ControlConnection conn;
        if (!conn.connectTo(controlHost, controlPort)) return false;
        if (!authenticate(conn)) {
            conn.closeConn();
            return false;
        }
        if (!conn.sendLine(cmd)) {
            conn.closeConn();
            return false;
        }
        bool ok = conn.readResponse(lines);
        conn.closeConn();
        return ok;
    }
    
    bool addOnion(bool reuseKey) {
        std::string keySpec = "NEW:ED25519-V3";
        if (reuseKey && !privateKey.empty()) {
            keySpec = normalizeStoredPrivateKeySpec(privateKey);
        }
        std::string cmd = "ADD_ONION " + keySpec + " Port=" +
                          std::to_string(virtualPort) + ",127.0.0.1:" + std::to_string(targetPort);
        std::vector<std::string> lines;
        if (!sendCommand(cmd, lines)) {
            if (!reuseKey || privateKey.empty()) {
                return false;
            }
            utils::Logger::warn(
                "Stored onion private key reuse failed; rotating to a new persistent onion identity");
            privateKey.clear();
            lines.clear();
            cmd = "ADD_ONION NEW:ED25519-V3 Port=" +
                  std::to_string(virtualPort) + ",127.0.0.1:" + std::to_string(targetPort);
            if (!sendCommand(cmd, lines)) return false;
        }

        std::string newServiceId;
        std::string newPrivateKey;
        for (const auto& line : lines) {
            if (line.rfind("250-ServiceID=", 0) == 0) {
                newServiceId = line.substr(14);
            } else if (line.rfind("250-PrivateKey=", 0) == 0) {
                newPrivateKey = line.substr(15);
            }
        }
        if (newServiceId.empty()) return false;
        
        serviceId = newServiceId;
        hostname = serviceId + ".onion";
        if (!newPrivateKey.empty()) {
            privateKey = newPrivateKey;
        }
        saveState();
        return true;
    }
    
    bool delOnion() {
        if (serviceId.empty()) return true;
        std::vector<std::string> lines;
        bool ok = sendCommand("DEL_ONION " + serviceId, lines);
        return ok;
    }
};

OnionService::OnionService() : impl_(std::make_unique<Impl>()) {}
OnionService::~OnionService() { stop(); }

bool OnionService::init(const std::string& serviceDir) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->serviceDir = serviceDir;
    impl_->loadState();
    return true;
}

bool OnionService::start(uint16_t virtualPort, uint16_t targetPort) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->virtualPort = virtualPort;
    impl_->targetPort = targetPort;
    if (!impl_->addOnion(true)) return false;
    impl_->running = true;
    impl_->lastRotation = std::time(nullptr);
    return true;
}

void OnionService::stop() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->running) {
        impl_->delOnion();
    }
    impl_->running = false;
}

bool OnionService::isRunning() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->running;
}

std::string OnionService::getHostname() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->hostname;
}

bool OnionService::rotateIdentity() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->delOnion();
    impl_->privateKey.clear();
    bool ok = impl_->addOnion(false);
    if (ok) {
        impl_->lastRotation = std::time(nullptr);
    }
    return ok;
}

void OnionService::setRotationInterval(uint64_t seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->rotationInterval = seconds;
}

void OnionService::enableAutoRotation(bool enable) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->autoRotation = enable;
}

bool OnionService::shouldRotate() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->autoRotation) return false;
    return std::time(nullptr) - impl_->lastRotation >= impl_->rotationInterval;
}

void OnionService::setControlConfig(const std::string& host, uint16_t port,
                                    const std::string& password,
                                    const std::string& cookiePath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->controlHost = host;
    impl_->controlPort = port;
    impl_->controlPassword = password;
    impl_->controlCookiePath = cookiePath;
}

}
}
