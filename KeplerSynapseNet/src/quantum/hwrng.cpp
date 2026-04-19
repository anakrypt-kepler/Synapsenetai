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
        sodiumReady = (sodium_init() >= 0);
    }

    void fillRandom(uint8_t* buf, size_t len) {
        if (sodiumReady) {
            randombytes_buf(buf, len);
        } else {
            readDevRandom(buf, len);
        }
        stats.bytesGenerated += len;
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

    void mixWithHash(uint8_t* buf, size_t len) {
        if (!mixEntropy || len == 0) return;
        std::vector<uint8_t> temp(len);
        if (sodiumReady) {
            randombytes_buf(temp.data(), len);
        } else if (!readDevRandom(temp.data(), len)) {
            return;
        }
        for (size_t i = 0; i < len; i++) {
            buf[i] ^= temp[i];
        }
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
    if (impl_->sodiumReady) {
        randombytes_buf(test, 32);
        impl_->stats.hardwareAvailable = true;
    } else if (impl_->readDevRandom(test, 32)) {
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
    std::vector<uint8_t> result(length);

    if (impl_->sodiumReady) {
        randombytes_buf(result.data(), length);
        impl_->stats.bytesGenerated += length;
    } else if (impl_->useFallback) {
        if (!impl_->readDevRandom(result.data(), length)) {
            impl_->fillRandom(result.data(), length);
        }
    } else {
        impl_->fillRandom(result.data(), length);
    }

    if (impl_->mixEntropy) {
        impl_->mixWithHash(result.data(), length);
    }

    return result;
}

void HWRNG::fill(uint8_t* buffer, size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    if (impl_->sodiumReady) {
        randombytes_buf(buffer, length);
        impl_->stats.bytesGenerated += length;
    } else if (impl_->useFallback) {
        if (!impl_->readDevRandom(buffer, length)) {
            impl_->fillRandom(buffer, length);
        }
    } else {
        impl_->fillRandom(buffer, length);
    }

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
