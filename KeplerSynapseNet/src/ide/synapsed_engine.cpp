#include "ide/synapsed_engine.h"

#include <chrono>
#include <cstring>
#include <random>
#include <sstream>

namespace synapse {
namespace ide {

namespace {

std::string generateNodeId() {
    static std::mt19937 gen(std::random_device{}());
    static const char chars[] = "0123456789abcdef";
    std::uniform_int_distribution<> dist(0, 15);
    std::string id;
    id.reserve(16);
    for (int i = 0; i < 16; ++i) {
        id.push_back(chars[dist(gen)]);
    }
    return id;
}

int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}

SynapsedEngine::SynapsedEngine() = default;
SynapsedEngine::~SynapsedEngine() { shutdown(); }

SynapsedEngine& SynapsedEngine::instance() {
    static SynapsedEngine eng;
    return eng;
}

int SynapsedEngine::init(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (initialized_) return -1;

    configPath_ = configPath;
    nodeId_ = generateNodeId();
    startTime_ = nowMillis();
    peerCount_ = 0;
    initialized_ = true;
    return 0;
}

void SynapsedEngine::shutdown() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) return;
    initialized_ = false;
    subscribers_.clear();
    nodeId_.clear();
    peerCount_ = 0;
}

bool SynapsedEngine::isInitialized() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return initialized_;
}

std::string SynapsedEngine::rpcCall(const std::string& method, const std::string& paramsJson) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) {
        return "{\"error\":\"not initialized\"}";
    }

    if (method == "node.status") {
        return getStatus();
    }

    if (method == "node.peers") {
        return "{\"peer_count\":" + std::to_string(peerCount_) + "}";
    }

    (void)paramsJson;
    return "{\"error\":\"unknown method\",\"method\":\"" + method + "\"}";
}

int SynapsedEngine::subscribe(const std::string& eventType, EventCallback callback) {
    std::lock_guard<std::mutex> lock(mtx_);
    subscribers_[eventType].push_back(std::move(callback));
    return 0;
}

std::string SynapsedEngine::getStatus() const {
    if (!initialized_) {
        return "{\"error\":\"not initialized\"}";
    }

    int64_t uptime = nowMillis() - startTime_;

    std::ostringstream ss;
    ss << "{\"node_id\":\"" << nodeId_
       << "\",\"peer_count\":" << peerCount_
       << ",\"uptime\":" << uptime
       << ",\"version\":\"0.1.0\""
       << ",\"model_status\":\"idle\""
       << "}";
    return ss.str();
}

}
}
