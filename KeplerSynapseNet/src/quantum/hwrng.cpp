#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <fstream>
#include <cstring>
#include <ctime>
#include <sodium.h>

namespace synapse {
namespace quantum {

struct HWRNG::Impl {
    mutable std::mutex mtx;
    bool initialized = false;
    bool hasRdrand = false;
    bool hasRdseed = false;
    bool hasTpm = false;
    bool useFallback = true;
    bool mixEntropy = true;
    bool sodiumReady = false;
    RNGStats stats{};

    Impl() {
        crypto::containForeignCryptoModules();
        sodiumReady = (sodium_init() >= 0);
    }

    bool drawEntropy(uint8_t* buf, size_t len) {
        if (buf == nullptr || len == 0) return false;
        if (sodiumReady) {
            randombytes_buf(buf, len);
            return true;
        }
        if (!useFallback) return false;
        return readDevRandom(buf, len);
    }

    bool readDevRandom(uint8_t* buf, size_t len) {
        std::ifstream urandom("/dev/urandom", std::ios::binary);
        if (!urandom.is_open()) return false;
        urandom.read(reinterpret_cast<char*>(buf), len);
        bool success = urandom.good();
        if (success) {
            stats.devRandomCalls++;
        }
        return success;
    }

    bool mixWithHash(uint8_t* buf, size_t len) {
        if (!mixEntropy || len == 0 || buf == nullptr) return false;
        std::vector<uint8_t> temp(len);
        if (!drawEntropy(temp.data(), len)) return false;
        for (size_t i = 0; i < len; i++) {
            buf[i] ^= temp[i];
        }
        return true;
    }
};

HWRNG::HWRNG() : impl_(std::make_unique<Impl>()) {}
HWRNG::~HWRNG() = default;

bool HWRNG::init() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->initialized = true;
    impl_->stats = RNGStats{};
    impl_->hasRdrand = false;
    impl_->hasRdseed = false;
    impl_->hasTpm = false;

    uint8_t test[32];
    if (impl_->drawEntropy(test, 32)) {
        impl_->stats.hardwareAvailable = true;
    }

    return true;
}

bool HWRNG::isAvailable() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->initialized;
}

std::vector<uint8_t> HWRNG::generate(size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (length == 0) return {};
    std::vector<uint8_t> result(length);
    if (!impl_->drawEntropy(result.data(), length)) return {};
    impl_->stats.bytesGenerated += length;
    if (impl_->mixEntropy) {
        impl_->mixWithHash(result.data(), length);
    }
    return result;
}

void HWRNG::fill(uint8_t* buffer, size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (buffer == nullptr || length == 0) return;
    if (!impl_->drawEntropy(buffer, length)) {
        std::memset(buffer, 0, length);
        return;
    }
    impl_->stats.bytesGenerated += length;
    if (impl_->mixEntropy) {
        impl_->mixWithHash(buffer, length);
    }
}

bool HWRNG::hasRDRAND() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->hasRdrand;
}

bool HWRNG::hasRDSEED() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->hasRdseed;
}

bool HWRNG::hasTPM() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->hasTpm;
}

void HWRNG::setFallbackToDevRandom(bool enable) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->useFallback = enable;
}

void HWRNG::setEntropyMixing(bool enable) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->mixEntropy = enable;
}

HWRNG::RNGStats HWRNG::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->stats;
}

}
}
