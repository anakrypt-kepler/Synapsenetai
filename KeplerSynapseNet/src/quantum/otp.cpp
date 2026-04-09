#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <random>
#include <cstring>
#include <ctime>

namespace synapse {
namespace quantum {

struct OTP::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    uint64_t keyExpiry = 3600;
    
    Impl() : rng(std::random_device{}()) {}
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
};

OTP::OTP() : impl_(std::make_unique<Impl>()) {}
OTP::~OTP() = default;

OTPKey OTP::generateKey(size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    OTPKey key;
    key.keyMaterial.resize(length);
    impl_->fillRandom(key.keyMaterial.data(), length);
    key.keyId = impl_->rng();
    key.createdAt = std::time(nullptr);
    key.expiresAt = key.createdAt + impl_->keyExpiry;
    key.usedBytes = 0;
    key.exhausted = false;
    
    return key;
}

std::vector<uint8_t> OTP::encrypt(const std::vector<uint8_t>& plaintext, OTPKey& key) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (key.usedBytes + plaintext.size() > key.keyMaterial.size()) {
        key.exhausted = true;
        return {};
    }
    
    std::vector<uint8_t> ciphertext(plaintext.size());
    for (size_t i = 0; i < plaintext.size(); i++) {
        ciphertext[i] = plaintext[i] ^ key.keyMaterial[key.usedBytes + i];
    }
    
    key.usedBytes += plaintext.size();
    if (key.usedBytes >= key.keyMaterial.size()) {
        key.exhausted = true;
    }
    
    return ciphertext;
}

std::vector<uint8_t> OTP::decrypt(const std::vector<uint8_t>& ciphertext, OTPKey& key) {
    return encrypt(ciphertext, key);
}

bool OTP::isKeyExhausted(const OTPKey& key) const {
    return key.exhausted;
}

size_t OTP::remainingBytes(const OTPKey& key) const {
    if (key.exhausted) return 0;
    return key.keyMaterial.size() - key.usedBytes;
}

void OTP::setKeyExpiry(uint64_t seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->keyExpiry = seconds;
}

bool OTP::isKeyExpired(const OTPKey& key) const {
    return std::time(nullptr) > static_cast<time_t>(key.expiresAt);
}

}
}
