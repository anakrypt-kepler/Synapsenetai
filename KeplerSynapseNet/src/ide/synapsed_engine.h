#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace synapse {
namespace ide {

using EventCallback = std::function<void(const char* event_type, const char* payload_json)>;

class SynapsedEngine {
public:
    static SynapsedEngine& instance();

    int init(const std::string& configPath);
    void shutdown();
    bool isInitialized() const;

    std::string rpcCall(const std::string& method, const std::string& paramsJson);
    int subscribe(const std::string& eventType, EventCallback callback);
    std::string getStatus() const;

    SynapsedEngine(const SynapsedEngine&) = delete;
    SynapsedEngine& operator=(const SynapsedEngine&) = delete;

private:
    SynapsedEngine();
    ~SynapsedEngine();

    mutable std::mutex mtx_;
    bool initialized_ = false;
    std::string configPath_;
    std::string nodeId_;
    int64_t startTime_ = 0;
    int peerCount_ = 0;
    std::unordered_map<std::string, std::vector<EventCallback>> subscribers_;
};

}
}
