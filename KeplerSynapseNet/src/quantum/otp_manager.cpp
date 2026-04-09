#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <random>
#include <map>
#include <cstring>
#include <ctime>
#include <fstream>

namespace synapse {
namespace quantum {

struct OTPManager::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    std::map<uint64_t, OTPKey> keys;
    std::string storagePath;
    bool initialized = false;
    OTP otp;
    
    Impl() : rng(std::random_device{}()) {}
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
};

OTPManager::OTPManager() : impl_(std::make_unique<Impl>()) {}
OTPManager::~OTPManager() { shutdown(); }

bool OTPManager::init(const std::string& storagePath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->storagePath = storagePath;
    impl_->initialized = true;
    return true;
}

void OTPManager::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->keys.clear();
    impl_->initialized = false;
}

uint64_t OTPManager::createKey(size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    OTPKey key = impl_->otp.generateKey(length);
    impl_->keys[key.keyId] = key;
    return key.keyId;
}

OTPKey OTPManager::getKey(uint64_t keyId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->keys.find(keyId);
    if (it != impl_->keys.end()) {
        return it->second;
    }
    return OTPKey{};
}

bool OTPManager::deleteKey(uint64_t keyId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->keys.erase(keyId) > 0;
}

std::vector<uint8_t> OTPManager::encryptWithKey(uint64_t keyId, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->keys.find(keyId);
    if (it == impl_->keys.end()) return {};
    
    return impl_->otp.encrypt(data, it->second);
}

std::vector<uint8_t> OTPManager::decryptWithKey(uint64_t keyId, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->keys.find(keyId);
    if (it == impl_->keys.end()) return {};
    
    return impl_->otp.decrypt(data, it->second);
}

void OTPManager::pruneExpiredKeys() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    uint64_t now = std::time(nullptr);
    for (auto it = impl_->keys.begin(); it != impl_->keys.end();) {
        if (it->second.expiresAt < now) {
            it = impl_->keys.erase(it);
        } else {
            ++it;
        }
    }
}

void OTPManager::pruneExhaustedKeys() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    for (auto it = impl_->keys.begin(); it != impl_->keys.end();) {
        if (it->second.exhausted) {
            it = impl_->keys.erase(it);
        } else {
            ++it;
        }
    }
}

size_t OTPManager::keyCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->keys.size();
}

size_t OTPManager::totalKeyMaterial() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    size_t total = 0;
    for (const auto& [id, key] : impl_->keys) {
        total += key.keyMaterial.size();
    }
    return total;
}

}
}
