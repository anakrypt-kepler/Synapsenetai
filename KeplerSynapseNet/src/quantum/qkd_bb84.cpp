#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <algorithm>
#include <mutex>
#include <random>
#include <cstring>
#include <cmath>

namespace synapse {
namespace quantum {

struct QKDBB84::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    bool isAlice = false;
    bool isBob = false;
    std::vector<uint8_t> rawBits;
    std::vector<uint8_t> bases;
    
    Impl() : rng(std::random_device{}()) {}
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
    
    uint8_t randomBit() {
        return rng() & 1;
    }
};

QKDBB84::QKDBB84() : impl_(std::make_unique<Impl>()) {}
QKDBB84::~QKDBB84() = default;

bool QKDBB84::initAlice() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->isAlice = true;
    impl_->isBob = false;
    return true;
}

bool QKDBB84::initBob() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->isAlice = false;
    impl_->isBob = true;
    return true;
}

std::vector<uint8_t> QKDBB84::generateQubits(size_t count) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    impl_->rawBits.resize(count);
    impl_->bases.resize(count);
    impl_->fillRandom(impl_->rawBits.data(), count);
    impl_->fillRandom(impl_->bases.data(), count);
    
    for (size_t i = 0; i < count; i++) {
        impl_->rawBits[i] &= 1;
        impl_->bases[i] &= 1;
    }
    
    std::vector<uint8_t> qubits(count);
    for (size_t i = 0; i < count; i++) {
        qubits[i] = (impl_->rawBits[i] << 1) | impl_->bases[i];
    }
    
    return qubits;
}

std::vector<uint8_t> QKDBB84::measureQubits(const std::vector<uint8_t>& qubits,
                                             const std::vector<uint8_t>& bases) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> measurements(qubits.size());
    
    for (size_t i = 0; i < qubits.size(); i++) {
        uint8_t qubitBasis = qubits[i] & 1;
        uint8_t qubitValue = (qubits[i] >> 1) & 1;
        uint8_t measureBasis = bases[i] & 1;
        
        if (qubitBasis == measureBasis) {
            measurements[i] = qubitValue;
        } else {
            measurements[i] = impl_->randomBit();
        }
    }
    
    return measurements;
}

std::vector<uint8_t> QKDBB84::siftKey(const std::vector<uint8_t>& rawKey,
                                       const std::vector<uint8_t>& aliceBases,
                                       const std::vector<uint8_t>& bobBases) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> siftedKey;
    
    for (size_t i = 0; i < rawKey.size() && i < aliceBases.size() && i < bobBases.size(); i++) {
        if ((aliceBases[i] & 1) == (bobBases[i] & 1)) {
            siftedKey.push_back(rawKey[i] & 1);
        }
    }
    
    return siftedKey;
}

double QKDBB84::estimateErrorRate(const std::vector<uint8_t>& aliceKey,
                                   const std::vector<uint8_t>& bobKey,
                                   size_t sampleSize) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (aliceKey.empty() || bobKey.empty()) return 1.0;
    
    size_t errors = 0;
    size_t samples = std::min({sampleSize, aliceKey.size(), bobKey.size()});
    
    for (size_t i = 0; i < samples; i++) {
        if ((aliceKey[i] & 1) != (bobKey[i] & 1)) {
            errors++;
        }
    }
    
    return static_cast<double>(errors) / samples;
}

std::vector<uint8_t> QKDBB84::privacyAmplification(const std::vector<uint8_t>& siftedKey,
                                                    double errorRate) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (siftedKey.empty() || errorRate >= 0.5) {
        return std::vector<uint8_t>();
    }

    size_t outputLen = static_cast<size_t>(std::floor(static_cast<double>(siftedKey.size()) * (1.0 - 2.0 * errorRate)));
    if (outputLen == 0) return std::vector<uint8_t>();

    auto hash = crypto::sha256(siftedKey.data(), siftedKey.size());
    if (outputLen >= hash.size()) return std::vector<uint8_t>(hash.begin(), hash.end());
    return std::vector<uint8_t>(hash.begin(), hash.begin() + outputLen);
}

}
}
