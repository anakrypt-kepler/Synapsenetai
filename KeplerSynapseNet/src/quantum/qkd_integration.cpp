#include "quantum/quantum_security.h"
#include <cstring>
#include <map>
#include <mutex>

namespace synapse {
namespace quantum {

struct QKDIntegration::Impl {
    mutable std::mutex mtx;
    std::string devicePath;
    bool connected = false;
    std::map<std::string, QKDSession> sessions;
    uint64_t minKeyRate = 1000;
    double maxErrorRate = 0.11;
    std::function<void(const QKDSession&)> sessionCallback;
    std::function<void(const std::string&)> closedCallback;
    std::function<void(const std::string&, size_t)> keyCallback;
};

QKDIntegration::QKDIntegration() : impl_(std::make_unique<Impl>()) {}
QKDIntegration::~QKDIntegration() { shutdown(); }

bool QKDIntegration::init(const std::string& devicePath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->devicePath = devicePath;
    // An in-process BB84 rehearsal is not a quantum channel.
    impl_->connected = false;
    impl_->sessions.clear();
    return false;
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
    session.remoteNodeId = remoteNodeId;
    session.active = false;
    session.errorRate = 1.0;
    session.keyBitsGenerated = 0;
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

    if (!impl_->connected || length == 0) {
        return {};
    }

    auto it = impl_->sessions.find(sessionId);
    if (it == impl_->sessions.end() || !it->second.active) {
        return {};
    }

    // A short key must not be padded. Callers would otherwise repeat it.
    if (it->second.sharedKey.size() < length) {
        return {};
    }

    std::vector<uint8_t> key(length);
    std::memcpy(key.data(), it->second.sharedKey.data(), length);

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
