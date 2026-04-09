#include "network/network.h"
#include "crypto/crypto.h"
#include <cstring>
#include <mutex>
#include <map>
#include <queue>
#include <set>
#include <ctime>

namespace synapse {
namespace network {

struct SyncState {
    uint64_t localHeight = 0;
    uint64_t networkHeight = 0;
    uint64_t syncedHeight = 0;
    bool syncing = false;
    std::string currentPeer;
    uint64_t startTime = 0;
    uint64_t startSyncedHeight = 0;
    uint64_t bytesReceived = 0;
};

struct BlockRequest {
    uint64_t height;
    std::string peerId;
    uint64_t requestTime;
    int retries;
};

struct Sync::Impl {
    SyncState state;
    std::map<std::string, uint64_t> peerHeights;
    std::queue<BlockRequest> pendingRequests;
    std::set<uint64_t> receivedBlocks;
    std::vector<uint8_t> lastBlockHash;
    uint32_t maxConcurrentRequests = 16;
    uint32_t requestTimeout = 30;
    mutable std::mutex mtx;
    
    std::function<void(uint64_t, uint64_t)> onProgress;
    std::function<void(const std::vector<uint8_t>&)> onBlockReceived;
    std::function<void()> onSyncComplete;
    
    std::string selectBestPeer();
    void requestNextBlocks();
};

std::string Sync::Impl::selectBestPeer() {
    std::string bestPeer;
    uint64_t bestHeight = 0;
    
    for (const auto& [peer, height] : peerHeights) {
        if (height > bestHeight) {
            bestHeight = height;
            bestPeer = peer;
        }
    }
    
    return bestPeer;
}

void Sync::Impl::requestNextBlocks() {
    while (pendingRequests.size() < maxConcurrentRequests) {
        uint64_t nextHeight = state.syncedHeight + pendingRequests.size() + 1;
        
        if (nextHeight > state.networkHeight) break;
        if (receivedBlocks.count(nextHeight) > 0) continue;
        
        std::string peer = selectBestPeer();
        if (peer.empty()) break;
        
        BlockRequest req;
        req.height = nextHeight;
        req.peerId = peer;
        req.requestTime = std::time(nullptr);
        req.retries = 0;
        
        pendingRequests.push(req);
    }
}

Sync::Sync() : impl_(std::make_unique<Impl>()) {}
Sync::~Sync() = default;

void Sync::start() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    impl_->state.syncing = true;
    impl_->state.startTime = std::time(nullptr);
    impl_->state.startSyncedHeight = impl_->state.syncedHeight;
    impl_->state.bytesReceived = 0;
    
    impl_->requestNextBlocks();
}

void Sync::stop() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    impl_->state.syncing = false;
    while (!impl_->pendingRequests.empty()) {
        impl_->pendingRequests.pop();
    }
}

bool Sync::isSyncing() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->state.syncing;
}

bool Sync::isSynced() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->state.localHeight >= impl_->state.networkHeight;
}

void Sync::setLocalHeight(uint64_t height) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->state.localHeight = height;
    impl_->state.syncedHeight = height;
}

void Sync::updatePeerHeight(const std::string& peerId, uint64_t height) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    impl_->peerHeights[peerId] = height;
    
    if (height > impl_->state.networkHeight) {
        impl_->state.networkHeight = height;
    }
}

void Sync::removePeer(const std::string& peerId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->peerHeights.erase(peerId);
}

uint64_t Sync::getLocalHeight() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->state.localHeight;
}

uint64_t Sync::getNetworkHeight() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->state.networkHeight;
}

int Sync::getProgress() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->state.networkHeight == 0) return 100;
    
    return (impl_->state.syncedHeight * 100) / impl_->state.networkHeight;
}

void Sync::processBlock(const std::vector<uint8_t>& blockData, uint64_t height) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    impl_->receivedBlocks.insert(height);
    impl_->state.bytesReceived += blockData.size();
    
    if (height == impl_->state.syncedHeight + 1) {
        impl_->state.syncedHeight = height;
        impl_->state.localHeight = height;
        
        auto hash = crypto::sha256(blockData.data(), blockData.size());
        impl_->lastBlockHash.assign(hash.begin(), hash.end());
        
        while (impl_->receivedBlocks.count(impl_->state.syncedHeight + 1) > 0) {
            impl_->state.syncedHeight++;
            impl_->state.localHeight++;
        }
        // Prune receivedBlocks to keep only latest 1000 heights
        if (impl_->state.syncedHeight > 1000) {
            uint64_t threshold = impl_->state.syncedHeight - 1000;
            auto it = impl_->receivedBlocks.begin();
            while (it != impl_->receivedBlocks.end() && *it <= threshold) {
                it = impl_->receivedBlocks.erase(it);
            }
        }
        
        if (impl_->onBlockReceived) {
            impl_->onBlockReceived(blockData);
        }
        
        if (impl_->onProgress) {
            impl_->onProgress(impl_->state.syncedHeight, impl_->state.networkHeight);
        }
    }
    
    if (impl_->state.syncedHeight >= impl_->state.networkHeight) {
        impl_->state.syncing = false;
        if (impl_->onSyncComplete) {
            impl_->onSyncComplete();
        }
    } else {
        impl_->requestNextBlocks();
    }
}

void Sync::processTimeout() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    uint64_t now = std::time(nullptr);
    
    std::queue<BlockRequest> newQueue;
    while (!impl_->pendingRequests.empty()) {
        BlockRequest req = impl_->pendingRequests.front();
        impl_->pendingRequests.pop();
        
        if (now - req.requestTime > impl_->requestTimeout) {
            if (req.retries < 3) {
                req.retries++;
                req.requestTime = now;
                req.peerId = impl_->selectBestPeer();
                newQueue.push(req);
            }
        } else {
            newQueue.push(req);
        }
    }
    
    impl_->pendingRequests = newQueue;
    impl_->requestNextBlocks();
}

std::vector<uint64_t> Sync::getPendingHeights() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<uint64_t> heights;
    std::queue<BlockRequest> temp = impl_->pendingRequests;
    
    while (!temp.empty()) {
        heights.push_back(temp.front().height);
        temp.pop();
    }
    
    return heights;
}

void Sync::setMaxConcurrentRequests(uint32_t max) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxConcurrentRequests = max;
}

void Sync::setRequestTimeout(uint32_t seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->requestTimeout = seconds;
}

void Sync::onProgress(std::function<void(uint64_t, uint64_t)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->onProgress = callback;
}

void Sync::onBlockReceived(std::function<void(const std::vector<uint8_t>&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->onBlockReceived = callback;
}

void Sync::onSyncComplete(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->onSyncComplete = callback;
}

SyncStats Sync::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    SyncStats stats;
    stats.localHeight = impl_->state.localHeight;
    stats.networkHeight = impl_->state.networkHeight;
    stats.syncedHeight = impl_->state.syncedHeight;
    stats.syncing = impl_->state.syncing;
    stats.progress = getProgress();
    stats.bytesReceived = impl_->state.bytesReceived;
    stats.peerCount = impl_->peerHeights.size();
    
    if (impl_->state.startTime > 0) {
        uint64_t elapsed = std::time(nullptr) - impl_->state.startTime;
        if (elapsed > 0) {
            // Use startSyncedHeight captured at start() to compute blocks/sec
            stats.blocksPerSecond = static_cast<double>(impl_->state.syncedHeight - impl_->state.startSyncedHeight) / static_cast<double>(elapsed);
        }
    }
    
    return stats;
}

}
}
