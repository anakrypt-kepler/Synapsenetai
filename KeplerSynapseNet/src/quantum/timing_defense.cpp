#include "quantum/quantum_security.h"
#include <mutex>
#include <random>
#include <thread>
#include <chrono>
#include <cstring>

namespace synapse {
namespace quantum {

struct TimingDefense::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    bool enabled = true;
    
    Impl() : rng(std::random_device{}()) {}
};

TimingDefense::TimingDefense() : impl_(std::make_unique<Impl>()) {}
TimingDefense::~TimingDefense() = default;

void TimingDefense::constantTimeCompare(const uint8_t* a, const uint8_t* b, size_t length, bool& equal) {
    uint8_t result = 0;
    for (size_t i = 0; i < length; i++) {
        result |= a[i] ^ b[i];
    }
    equal = (result == 0);
}

void TimingDefense::constantTimeCopy(uint8_t* dest, const uint8_t* src, size_t length) {
    volatile uint8_t* vdest = dest;
    const volatile uint8_t* vsrc = src;
    for (size_t i = 0; i < length; i++) {
        vdest[i] = vsrc[i];
    }
}

void TimingDefense::constantTimeSelect(uint8_t* dest, const uint8_t* a, const uint8_t* b,
                                        size_t length, bool selectA) {
    uint8_t mask = selectA ? 0xff : 0x00;
    for (size_t i = 0; i < length; i++) {
        dest[i] = (a[i] & mask) | (b[i] & ~mask);
    }
}

void TimingDefense::addRandomDelay(uint32_t minMicros, uint32_t maxMicros) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->enabled) return;
    
    uint32_t range = maxMicros - minMicros;
    uint32_t delay = minMicros + (impl_->rng() % (range + 1));
    
    std::this_thread::sleep_for(std::chrono::microseconds(delay));
}

void TimingDefense::normalizeResponseTime(uint64_t targetMicros) {
    auto start = std::chrono::high_resolution_clock::now();
    
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start).count();
    
    if (static_cast<uint64_t>(elapsed) < targetMicros) {
        std::this_thread::sleep_for(std::chrono::microseconds(targetMicros - elapsed));
    }
}

void TimingDefense::enableTimingProtection(bool enable) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->enabled = enable;
}

bool TimingDefense::isTimingProtectionEnabled() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->enabled;
}

}
}
