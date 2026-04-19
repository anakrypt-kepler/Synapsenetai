#include "privacy/privacy.h"
#include <mutex>
#include <map>
#include <cstring>
#include <random>
#include <sys/mman.h>

namespace synapse {
namespace privacy {

struct Amnesia::Impl {
    mutable std::mutex mtx;
    std::map<void*, size_t> allocations;
    bool enabled = false;
    std::mt19937_64 rng;
    
    Impl() : rng(std::random_device{}()) {}
};

Amnesia::Amnesia() : impl_(std::make_unique<Impl>()) {}
Amnesia::~Amnesia() { wipeAllAllocations(); }

bool Amnesia::enable() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->enabled = true;
    return true;
}

void Amnesia::disable() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->enabled = false;
}

bool Amnesia::isEnabled() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->enabled;
}

void* Amnesia::allocateSecure(size_t size) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    void* ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
        impl_->allocations[ptr] = size;
    }
    return ptr;
}

void Amnesia::freeSecure(void* ptr) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->allocations.find(ptr);
    if (it != impl_->allocations.end()) {
        volatile uint8_t* vptr = static_cast<volatile uint8_t*>(ptr);
        for (size_t i = 0; i < it->second; i++) {
            vptr[i] = static_cast<uint8_t>(impl_->rng());
        }
        memset(ptr, 0, it->second);
        impl_->allocations.erase(it);
    }
    free(ptr);
}

void Amnesia::wipeMemory(void* ptr, size_t size) {
    volatile uint8_t* vptr = static_cast<volatile uint8_t*>(ptr);
    for (size_t i = 0; i < size; i++) {
        vptr[i] = static_cast<uint8_t>(impl_->rng());
    }
    memset(ptr, 0, size);
}

void Amnesia::wipeAllAllocations() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    for (auto& [ptr, size] : impl_->allocations) {
        volatile uint8_t* vptr = static_cast<volatile uint8_t*>(ptr);
        for (size_t i = 0; i < size; i++) {
            vptr[i] = 0;
        }
        free(ptr);
    }
    impl_->allocations.clear();
}

size_t Amnesia::secureMemoryUsage() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    size_t total = 0;
    for (const auto& [ptr, size] : impl_->allocations) {
        total += size;
    }
    return total;
}

bool Amnesia::lockMemory(void* ptr, size_t size) {
    return mlock(ptr, size) == 0;
}

bool Amnesia::unlockMemory(void* ptr, size_t size) {
    return munlock(ptr, size) == 0;
}

}
}
