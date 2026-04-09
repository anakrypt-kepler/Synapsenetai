#include "privacy/privacy.h"
#include "crypto/crypto.h"
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <openssl/hmac.h>

namespace synapse {
namespace privacy {

struct MixInference::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    std::vector<std::string> mixNodes;
    std::queue<MixMessage> messageQueue;
    std::atomic<bool> running{false};
    std::thread workerThread;
    uint32_t batchSize = 10;
    uint64_t batchTimeout = 1000;
    uint64_t minDelay = 100;
    uint64_t maxDelay = 500;
    uint64_t dummyInterval = 5000;
    uint64_t messagesIn = 0;
    uint64_t messagesOut = 0;
    uint64_t dummyCount = 0;
    std::function<void(const MixMessage&)> sendCallback;
    
    Impl() : rng(std::random_device{}()) {}
    
    void fillRandom(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(rng());
        }
    }
    
    std::string generateId() {
        char hex[33];
        uint8_t buf[16];
        fillRandom(buf, 16);
        for (int i = 0; i < 16; i++) {
            snprintf(hex + i * 2, 3, "%02x", buf[i]);
        }
        return std::string(hex);
    }
    
    uint64_t randomDelay() {
        return minDelay + (rng() % (maxDelay - minDelay + 1));
    }
    
    std::vector<uint8_t> encryptLayer(const std::vector<uint8_t>& data, 
                                       const std::vector<uint8_t>& key) {
        // Keep simple XOR layer for compatibility, but append HMAC-SHA256 tag for authentication
        auto hash = crypto::sha256(key.data(), key.size());
        std::vector<uint8_t> result(data.size());
        for (size_t i = 0; i < data.size(); i++) {
            result[i] = data[i] ^ hash[i % hash.size()];
        }

        unsigned int hlen = 0;
        unsigned char hmacOut[EVP_MAX_MD_SIZE];
        if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), result.data(), static_cast<int>(result.size()), hmacOut, &hlen)) {
            // If HMAC fails, return unauthenticated ciphertext
            return result;
        }
        // Append 32-byte HMAC tag
        result.insert(result.end(), hmacOut, hmacOut + std::min<unsigned int>(hlen, 32));
        return result;
    }
    
    void workerLoop() {
        uint64_t lastDummy = 0;
        
        while (running) {
            std::vector<MixMessage> batch;
            
            {
                std::lock_guard<std::mutex> lock(mtx);
                
                uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                
                if (now - lastDummy >= dummyInterval) {
                    MixMessage dummy;
                    dummy.id = generateId();
                    dummy.data.resize(256);
                    fillRandom(dummy.data.data(), dummy.data.size());
                    dummy.timestamp = now;
                    dummy.delay = randomDelay();
                    dummy.isDummy = true;
                    messageQueue.push(dummy);
                    dummyCount++;
                    lastDummy = now;
                }
                
                while (!messageQueue.empty() && batch.size() < batchSize) {
                    MixMessage& msg = messageQueue.front();
                    if (now >= msg.timestamp + msg.delay) {
                        batch.push_back(msg);
                        messageQueue.pop();
                    } else {
                        break;
                    }
                }
            }
            
            for (size_t i = batch.size(); i > 1; i--) {
                size_t j = rng() % i;
                std::swap(batch[i - 1], batch[j]);
            }
            
            for (const auto& msg : batch) {
                if (sendCallback) {
                    sendCallback(msg);
                }
                messagesOut++;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
};

MixInference::MixInference() : impl_(std::make_unique<Impl>()) {}
MixInference::~MixInference() { stop(); }

bool MixInference::init(const std::vector<std::string>& mixNodes) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->mixNodes = mixNodes;
    return !mixNodes.empty();
}

void MixInference::start() {
    if (impl_->running) return;
    impl_->running = true;
    impl_->workerThread = std::thread(&Impl::workerLoop, impl_.get());
}

void MixInference::stop() {
    impl_->running = false;
    if (impl_->workerThread.joinable()) {
        impl_->workerThread.join();
    }
}

void MixInference::setBatchSize(uint32_t size) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->batchSize = size;
}

void MixInference::setBatchTimeout(uint64_t milliseconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->batchTimeout = milliseconds;
}

void MixInference::setDelayRange(uint64_t minMs, uint64_t maxMs) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->minDelay = minMs;
    impl_->maxDelay = maxMs;
}

std::string MixInference::submitQuery(const std::vector<uint8_t>& query,
                                       const std::vector<std::vector<uint8_t>>& nodeKeys) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    MixMessage msg;
    msg.id = impl_->generateId();
    msg.data = query;
    msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.delay = impl_->randomDelay();
    msg.isDummy = false;
    
    for (auto it = nodeKeys.rbegin(); it != nodeKeys.rend(); ++it) {
        msg.data = impl_->encryptLayer(msg.data, *it);
    }
    
    impl_->messageQueue.push(msg);
    impl_->messagesIn++;
    return msg.id;
}

std::vector<MixMessage> MixInference::getBatch() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<MixMessage> batch;
    
    while (!impl_->messageQueue.empty() && batch.size() < impl_->batchSize) {
        batch.push_back(impl_->messageQueue.front());
        impl_->messageQueue.pop();
    }
    
    for (size_t i = batch.size(); i > 1; i--) {
        size_t j = impl_->rng() % i;
        std::swap(batch[i - 1], batch[j]);
    }
    
    return batch;
}

bool MixInference::hasPendingMessages() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return !impl_->messageQueue.empty();
}

size_t MixInference::pendingCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->messageQueue.size();
}

void MixInference::addDummyTraffic() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    MixMessage dummy;
    dummy.id = impl_->generateId();
    dummy.data.resize(256);
    impl_->fillRandom(dummy.data.data(), dummy.data.size());
    dummy.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    dummy.delay = impl_->randomDelay();
    dummy.isDummy = true;
    
    impl_->messageQueue.push(dummy);
    impl_->dummyCount++;
}

void MixInference::setSendCallback(std::function<void(const MixMessage&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->sendCallback = callback;
}

void MixInference::setDummyInterval(uint64_t milliseconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->dummyInterval = milliseconds;
}

uint64_t MixInference::getMessagesIn() const {
    return impl_->messagesIn;
}

uint64_t MixInference::getMessagesOut() const {
    return impl_->messagesOut;
}

uint64_t MixInference::getDummyCount() const {
    return impl_->dummyCount;
}

}
}
