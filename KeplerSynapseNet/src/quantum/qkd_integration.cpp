#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <random>
#include <map>
#include <cstring>
#include <ctime>

namespace synapse {
namespace quantum {

struct QKDIntegration::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    std::string devicePath;
    bool connected = false;
    std::map<std::string, QKDSession> sessions;
    uint64_t minKeyRate = 1000;
    double maxErrorRate = 0.11;
    std::function<void(const QKDSession&)> sessionCallback;
    std::function<void(const std::string&)> closedCallback;
    std::function<void(const std::string&, size_t)> keyCallback;
    QKDBB84 bb84;
    
    Impl() : rng(std::random_device{}()) {}
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
    
    std::string generateSessionId() {
        char hex[33];
        uint8_t buf[16];
        fillRandom(buf, 16);
        for (int i = 0; i < 16; i++) {
            snprintf(hex + i * 2, 3, "%02x", buf[i]);
        }
        return std::string(hex);
    }
};

QKDIntegration::QKDIntegration() : impl_(std::make_unique<Impl>()) {}
QKDIntegration::~QKDIntegration() { shutdown(); }

bool QKDIntegration::init(const std::string& devicePath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->devicePath = devicePath;
    impl_->connected = true;
    impl_->bb84.initAlice();
    return true;
}

void QKDIntegration::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->sessions.clear();
    impl_->connected = false;
}

bool QKDIntegration::isConnected() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->connected;
}

QKDSession QKDIntegration::establishSession(const std::string& remoteNodeId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    QKDSession session;
    session.sessionId = impl_->generateSessionId();
    session.remoteNodeId = remoteNodeId;
    session.establishedAt = std::time(nullptr);
    session.active = true;
    
    auto qubits = impl_->bb84.generateQubits(1024);
    std::vector<uint8_t> bases(1024);
    impl_->fillRandom(bases.data(), bases.size());
    for (auto& b : bases) b &= 1;
    
    auto measurements = impl_->bb84.measureQubits(qubits, bases);
    
    std::vector<uint8_t> aliceBases(1024);
    impl_->fillRandom(aliceBases.data(), aliceBases.size());
    for (auto& b : aliceBases) b &= 1;
    
    auto siftedKey = impl_->bb84.siftKey(measurements, aliceBases, bases);
    session.errorRate = impl_->bb84.estimateErrorRate(siftedKey, siftedKey, 100);
    
    auto finalKey = impl_->bb84.privacyAmplification(siftedKey, session.errorRate);
    session.sharedKey = finalKey;
    session.keyBitsGenerated = finalKey.size() * 8;
    
    impl_->sessions[session.sessionId] = session;
    
    if (impl_->sessionCallback) {
        impl_->sessionCallback(session);
    }
    
    return session;
}

bool QKDIntegration::closeSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->sessions.find(sessionId);
    if (it == impl_->sessions.end()) return false;
    
    it->second.active = false;
    
    if (impl_->closedCallback) {
        impl_->closedCallback(sessionId);
    }
    
    impl_->sessions.erase(it);
    return true;
}

std::vector<uint8_t> QKDIntegration::getKeyMaterial(const std::string& sessionId, size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->sessions.find(sessionId);
    if (it == impl_->sessions.end()) {
        // Session not found - log error and return empty
        return std::vector<uint8_t>();
    }
    
    if (!it->second.active) {
        // Session is not active - return empty
        return std::vector<uint8_t>();
    }
    
    std::vector<uint8_t> key(length);
    size_t copyLen = std::min(length, it->second.sharedKey.size());
    if (copyLen > 0) {
        std::memcpy(key.data(), it->second.sharedKey.data(), copyLen);
    }
    
    // Invoke callback to track key usage
    if (impl_->keyCallback) {
        impl_->keyCallback(sessionId, length);
    }
    
    return key;
}

double QKDIntegration::getErrorRate(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->sessions.find(sessionId);
    if (it == impl_->sessions.end()) return 1.0;
    
    return it->second.errorRate;
}

void QKDIntegration::setMinKeyRate(uint64_t bitsPerSecond) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->minKeyRate = bitsPerSecond;
}

void QKDIntegration::setMaxErrorRate(double rate) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxErrorRate = rate;
}

void QKDIntegration::onSessionEstablished(std::function<void(const QKDSession&)> callback) {
    impl_->sessionCallback = callback;
}

void QKDIntegration::onSessionClosed(std::function<void(const std::string&)> callback) {
    impl_->closedCallback = callback;
}

void QKDIntegration::onKeyGenerated(std::function<void(const std::string&, size_t)> callback) {
    impl_->keyCallback = callback;
}

}
}
