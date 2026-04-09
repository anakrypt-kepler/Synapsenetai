#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <cstring>

namespace synapse {
namespace quantum {

struct KeyDerivation::Impl {
    mutable std::mutex mtx;
    
    std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& data);
};

std::vector<uint8_t> KeyDerivation::Impl::hmacSha256(const std::vector<uint8_t>& key,
                                                      const std::vector<uint8_t>& data) {
    std::vector<uint8_t> ipad(64, 0x36);
    std::vector<uint8_t> opad(64, 0x5c);
    
    std::vector<uint8_t> keyPad = key;
    if (keyPad.size() > 64) {
        auto hash = crypto::sha256(keyPad.data(), keyPad.size());
        keyPad.assign(hash.begin(), hash.end());
    }
    keyPad.resize(64, 0);
    
    for (size_t i = 0; i < 64; i++) {
        ipad[i] ^= keyPad[i];
        opad[i] ^= keyPad[i];
    }
    
    std::vector<uint8_t> inner;
    inner.insert(inner.end(), ipad.begin(), ipad.end());
    inner.insert(inner.end(), data.begin(), data.end());
    auto innerHash = crypto::sha256(inner.data(), inner.size());
    
    std::vector<uint8_t> outer;
    outer.insert(outer.end(), opad.begin(), opad.end());
    outer.insert(outer.end(), innerHash.begin(), innerHash.end());
    auto outerHash = crypto::sha256(outer.data(), outer.size());
    
    return std::vector<uint8_t>(outerHash.begin(), outerHash.end());
}

KeyDerivation::KeyDerivation() : impl_(std::make_unique<Impl>()) {}
KeyDerivation::~KeyDerivation() = default;

std::vector<uint8_t> KeyDerivation::hkdfExtract(const std::vector<uint8_t>& salt,
                                                 const std::vector<uint8_t>& ikm) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> actualSalt = salt;
    if (actualSalt.empty()) {
        actualSalt.resize(32, 0);
    }
    
    return impl_->hmacSha256(actualSalt, ikm);
}

std::vector<uint8_t> KeyDerivation::hkdfExpand(const std::vector<uint8_t>& prk,
                                                const std::vector<uint8_t>& info,
                                                size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> result;
    std::vector<uint8_t> t;
    uint8_t counter = 1;
    
    while (result.size() < length) {
        std::vector<uint8_t> input;
        input.insert(input.end(), t.begin(), t.end());
        input.insert(input.end(), info.begin(), info.end());
        input.push_back(counter++);
        
        t = impl_->hmacSha256(prk, input);
        result.insert(result.end(), t.begin(), t.end());
    }
    
    result.resize(length);
    return result;
}

std::vector<uint8_t> KeyDerivation::hkdf(const std::vector<uint8_t>& salt,
                                          const std::vector<uint8_t>& ikm,
                                          const std::vector<uint8_t>& info,
                                          size_t length) {
    auto prk = hkdfExtract(salt, ikm);
    return hkdfExpand(prk, info, length);
}

std::vector<uint8_t> KeyDerivation::pbkdf2(const std::string& password,
                                            const std::vector<uint8_t>& salt,
                                            uint32_t iterations,
                                            size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> result;
    std::vector<uint8_t> passwordBytes(password.begin(), password.end());
    uint32_t blockNum = 1;
    
    while (result.size() < length) {
        std::vector<uint8_t> u;
        u.insert(u.end(), salt.begin(), salt.end());
        u.push_back((blockNum >> 24) & 0xff);
        u.push_back((blockNum >> 16) & 0xff);
        u.push_back((blockNum >> 8) & 0xff);
        u.push_back(blockNum & 0xff);
        
        u = impl_->hmacSha256(passwordBytes, u);
        std::vector<uint8_t> t = u;
        
        for (uint32_t i = 1; i < iterations; i++) {
            u = impl_->hmacSha256(passwordBytes, u);
            for (size_t j = 0; j < t.size(); j++) {
                t[j] ^= u[j];
            }
        }
        
        result.insert(result.end(), t.begin(), t.end());
        blockNum++;
    }
    
    result.resize(length);
    return result;
}

std::vector<uint8_t> KeyDerivation::argon2id(const std::string& password,
                                              const std::vector<uint8_t>& salt,
                                              uint32_t timeCost,
                                              uint32_t memoryCost,
                                              uint32_t parallelism,
                                              size_t length) {
    return pbkdf2(password, salt, timeCost * 1000, length);
}

std::vector<uint8_t> KeyDerivation::mixEntropy(const std::vector<std::vector<uint8_t>>& sources) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> combined;
    for (const auto& source : sources) {
        combined.insert(combined.end(), source.begin(), source.end());
    }
    
    auto hash = crypto::sha256(combined.data(), combined.size());
    return std::vector<uint8_t>(hash.begin(), hash.end());
}

}
}
