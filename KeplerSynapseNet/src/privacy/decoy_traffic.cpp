#include "privacy/privacy.h"
#include <mutex>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>

namespace synapse {
namespace privacy {

struct DecoyTraffic::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    std::atomic<bool> running{false};
    std::thread workerThread;
    uint64_t minIntervalMs = 100;
    uint64_t maxIntervalMs = 1000;
    size_t minSize = 64;
    size_t maxSize = 1024;
    double trafficRatio = 0.1;
    std::function<void(const std::vector<uint8_t>&)> sendCallback;
    
    Impl() : rng(std::random_device{}()) {}
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
    
    uint64_t randomInterval() {
        return minIntervalMs + (rng() % (maxIntervalMs - minIntervalMs + 1));
    }
    
    size_t randomSize() {
        return minSize + (rng() % (maxSize - minSize + 1));
    }
};

DecoyTraffic::DecoyTraffic() : impl_(std::make_unique<Impl>()) {}
DecoyTraffic::~DecoyTraffic() { stop(); }

void DecoyTraffic::start() {
    if (impl_->running) return;
    
    impl_->running = true;
    impl_->workerThread = std::thread([this]() {
        while (impl_->running) {
            auto packet = generatePacket();
            if (impl_->sendCallback) {
                impl_->sendCallback(packet);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(impl_->randomInterval()));
        }
    });
}

void DecoyTraffic::stop() {
    impl_->running = false;
    if (impl_->workerThread.joinable()) {
        impl_->workerThread.join();
    }
}

bool DecoyTraffic::isRunning() const {
    return impl_->running;
}

void DecoyTraffic::setIntervalRange(uint64_t minMs, uint64_t maxMs) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->minIntervalMs = minMs;
    impl_->maxIntervalMs = maxMs;
}

void DecoyTraffic::setSizeRange(size_t minBytes, size_t maxBytes) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->minSize = minBytes;
    impl_->maxSize = maxBytes;
}

void DecoyTraffic::setTrafficRatio(double ratio) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->trafficRatio = ratio;
}

void DecoyTraffic::setSendCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    impl_->sendCallback = callback;
}

std::vector<uint8_t> DecoyTraffic::generatePacket() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    size_t size = impl_->randomSize();
    std::vector<uint8_t> packet(size);
    
    packet[0] = 0xDE;
    packet[1] = 0xC0;
    packet[2] = 0x01;
    packet[3] = 0x00;
    
    impl_->fillRandom(packet.data() + 4, size - 4);
    
    return packet;
}

bool DecoyTraffic::isDecoyPacket(const std::vector<uint8_t>& packet) {
    if (packet.size() < 4) return false;
    return packet[0] == 0xDE && packet[1] == 0xC0 && packet[2] == 0x01;
}

void DecoyTraffic::adaptToTraffic(size_t realPacketsPerSecond) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    size_t targetDecoy = static_cast<size_t>(realPacketsPerSecond * impl_->trafficRatio);
    if (targetDecoy > 0) {
        impl_->maxIntervalMs = 1000 / targetDecoy;
        impl_->minIntervalMs = impl_->maxIntervalMs / 2;
    }
}

}
}
