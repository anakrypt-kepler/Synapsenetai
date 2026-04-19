#include "privacy/privacy.h"
#include <mutex>
#include <map>
#include <random>
#include <ctime>

namespace synapse {
namespace privacy {

struct Dandelion::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    uint32_t minHops = 2;
    uint32_t maxHops = 4;
    double fluffProbability = 0.1;
    uint64_t stemTimeout = 30;
    std::map<std::string, std::string> stemPeers;
    std::map<std::string, bool> fluffed;
    std::map<std::string, uint64_t> timestamps;
    
    Impl() : rng(std::random_device{}()) {}
    
    uint32_t randomHops() {
        return minHops + (rng() % (maxHops - minHops + 1));
    }
    
    double randomDouble() {
        return static_cast<double>(rng()) / static_cast<double>(rng.max());
    }
    
    size_t randomIndex(size_t max) {
        return rng() % max;
    }
};

Dandelion::Dandelion() : impl_(std::make_unique<Impl>()) {}
Dandelion::~Dandelion() = default;

void Dandelion::setHopRange(uint32_t minHops, uint32_t maxHops) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->minHops = minHops;
    impl_->maxHops = maxHops;
}

void Dandelion::setFluffProbability(double probability) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->fluffProbability = probability;
}

void Dandelion::setStemTimeout(uint64_t seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->stemTimeout = seconds;
}

DandelionPhase Dandelion::submitTransaction(const std::string& txId,
                                             const std::vector<uint8_t>& txData,
                                             const std::vector<std::string>& peers) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (peers.empty()) {
        impl_->fluffed[txId] = true;
        return DandelionPhase::FLUFF;
    }
    
    size_t peerIdx = impl_->randomIndex(peers.size());
    impl_->stemPeers[txId] = peers[peerIdx];
    impl_->timestamps[txId] = std::time(nullptr);
    impl_->fluffed[txId] = false;
    
    return DandelionPhase::STEM;
}

DandelionPhase Dandelion::relayTransaction(const std::string& txId,
                                            const std::vector<uint8_t>& txData,
                                            const std::vector<std::string>& peers,
                                            uint32_t currentHop) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (currentHop >= impl_->randomHops() || impl_->randomDouble() < impl_->fluffProbability) {
        impl_->fluffed[txId] = true;
        return DandelionPhase::FLUFF;
    }
    
    if (!peers.empty()) {
        size_t peerIdx = impl_->randomIndex(peers.size());
        impl_->stemPeers[txId] = peers[peerIdx];
    }
    
    return DandelionPhase::STEM;
}

std::string Dandelion::getStemPeer(const std::string& txId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->stemPeers.find(txId);
    if (it != impl_->stemPeers.end()) {
        return it->second;
    }
    return "";
}

bool Dandelion::isFluffed(const std::string& txId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->fluffed.find(txId);
    return it != impl_->fluffed.end() && it->second;
}

void Dandelion::markFluffed(const std::string& txId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->fluffed[txId] = true;
}

void Dandelion::clearTransaction(const std::string& txId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->stemPeers.erase(txId);
    impl_->fluffed.erase(txId);
    impl_->timestamps.erase(txId);
}

void Dandelion::adaptToNetworkSize(size_t networkSize) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (networkSize < 10) {
        impl_->minHops = 1;
        impl_->maxHops = 2;
    } else if (networkSize < 100) {
        impl_->minHops = 2;
        impl_->maxHops = 4;
    } else {
        impl_->minHops = 3;
        impl_->maxHops = 6;
    }
}

}
}
