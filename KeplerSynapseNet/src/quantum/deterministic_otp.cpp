#include "quantum/quantum_security.h"
#include "crypto/crypto.h"
#include <mutex>
#include <cstring>
#include <ctime>
#include <random>

namespace synapse {
namespace quantum {

struct DeterministicOTP::Impl {
    std::vector<uint8_t> seed;
    uint64_t counter = 0;
    mutable std::mutex mtx;
    
    std::vector<uint8_t> deriveBlock(uint64_t index);
};

std::vector<uint8_t> DeterministicOTP::Impl::deriveBlock(uint64_t index) {
    std::vector<uint8_t> input;
    input.insert(input.end(), seed.begin(), seed.end());
    
    for (int i = 0; i < 8; i++) {
        input.push_back((index >> (i * 8)) & 0xff);
    }
    
    auto hash = crypto::sha256(input.data(), input.size());
    return std::vector<uint8_t>(hash.begin(), hash.end());
}

DeterministicOTP::DeterministicOTP() : impl_(std::make_unique<Impl>()) {}
DeterministicOTP::~DeterministicOTP() = default;

void DeterministicOTP::setSeed(const std::vector<uint8_t>& seed) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->seed = seed;
    impl_->counter = 0;
}

void DeterministicOTP::setCounter(uint64_t counter) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->counter = counter;
}

std::vector<uint8_t> DeterministicOTP::deriveKey(size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> result;
    result.reserve(length);
    
    while (result.size() < length) {
        auto block = impl_->deriveBlock(impl_->counter++);
        size_t needed = std::min(block.size(), length - result.size());
        result.insert(result.end(), block.begin(), block.begin() + needed);
    }
    
    return result;
}

std::vector<uint8_t> DeterministicOTP::deriveKeyAt(uint64_t index, size_t length) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint8_t> result;
    result.reserve(length);
    
    uint64_t blockIndex = index;
    while (result.size() < length) {
        auto block = impl_->deriveBlock(blockIndex++);
        size_t needed = std::min(block.size(), length - result.size());
        result.insert(result.end(), block.begin(), block.begin() + needed);
    }
    
    return result;
}

void DeterministicOTP::advance(uint64_t steps) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->counter += steps;
}

uint64_t DeterministicOTP::currentCounter() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->counter;
}

}
}
