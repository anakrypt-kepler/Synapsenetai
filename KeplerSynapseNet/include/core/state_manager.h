#pragma once

#include <string>
#include <map>
#include <cstdint>

namespace synapse {
namespace core {

enum class NodeState : uint8_t {
    Initializing,
    Connecting,
    Syncing,
    Running,
    Paused,
    ShuttingDown,
    Error
};

enum class SyncState : uint8_t {
    NotStarted,
    DownloadingHeaders,
    DownloadingBlocks,
    VerifyingBlocks,
    Indexing,
    Complete,
    Failed
};

struct StateSnapshot {
    NodeState nodeState;
    SyncState syncState;
    uint64_t blockHeight;
    uint64_t headerHeight;
    int peerCount;
    uint64_t memPoolSize;
    uint64_t balance;
    uint64_t pendingBalance;
    bool walletLocked;
    bool miningActive;
    bool modelLoaded;
    uint64_t timestamp;
    std::map<std::string, std::string> metadata;
};

class StatePersistence {
public:
    StatePersistence(const std::string& path);
    ~StatePersistence();
    bool save(const StateSnapshot& state);
    bool load(StateSnapshot& state);
    bool exists() const;
    void remove();
private:
    struct Impl;
};

}
}
