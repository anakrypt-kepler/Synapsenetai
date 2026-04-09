#pragma once

#include "core/tor_route_policy.h"
#include "core/ledger.h"
#include "network/discovery.h"
#include "network/network.h"

#include "node/node_config.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace synapse::node {

using LoopCallback = std::function<void()>;

void startNodeThreads(
    std::thread& networkThread,
    std::thread& consensusThread,
    std::thread& maintenanceThread,
    std::thread& syncThread,
    LoopCallback networkLoop,
    LoopCallback consensusLoop,
    LoopCallback maintenanceLoop,
    LoopCallback syncLoop);

void stopNodeThreads(
    std::thread& networkThread,
    std::thread& consensusThread,
    std::thread& maintenanceThread,
    std::thread& syncThread);

struct DaemonRuntimeStats {
    uint64_t uptime = 0;
    uint64_t peersConnected = 0;
    uint64_t knowledgeEntries = 0;
    double syncProgress = 0.0;
};

struct DaemonRuntimeHooks {
    const NodeConfig* config = nullptr;
    std::function<void()> startThreads;
    std::function<void()> stopThreads;
    std::function<void()> reload;
    std::function<DaemonRuntimeStats()> getStats;
    std::function<std::string(uint64_t)> formatUptime;
    std::function<bool()> shouldKeepRunning;
    std::function<bool()> consumeReloadRequested;
    std::function<bool()> walletReady;
    std::function<std::string()> walletAddress;
    std::function<bool()> networkOnline;
};

int runDaemonLoop(const DaemonRuntimeHooks& hooks);

struct DaemonRuntimeAdapterInputs {
    const NodeConfig* config = nullptr;
    std::thread* networkThread = nullptr;
    std::thread* consensusThread = nullptr;
    std::thread* maintenanceThread = nullptr;
    std::thread* syncThread = nullptr;
    LoopCallback networkLoop;
    LoopCallback consensusLoop;
    LoopCallback maintenanceLoop;
    LoopCallback syncLoop;
    std::function<void()> reload;
    std::function<DaemonRuntimeStats()> getStats;
    std::function<std::string(uint64_t)> formatUptime;
    std::function<bool()> shouldKeepRunning;
    std::function<bool()> consumeReloadRequested;
    std::function<bool()> walletReady;
    std::function<std::string()> walletAddress;
    std::function<bool()> networkOnline;
};

int runDaemonRuntimeAdapter(const DaemonRuntimeAdapterInputs& inputs);

struct NetworkLoopHooks {
    const NodeConfig* config = nullptr;
    std::function<bool()> shouldKeepRunning;
    std::function<bool()> torRequired;
    std::function<bool()> probeTorSocks;
    std::function<bool(bool, const char*)> maybeStartManagedTorRuntimeWithBackoff;
    std::function<void(bool)> setTorManaged;
    std::function<void()> resetManagedTorRestartBackoffState;
    std::function<void(bool)> setTorReachable;
    std::function<void(bool, bool)> refreshTorWebReadiness;
    std::function<bool()> allowClearnetFallback;
    std::function<bool()> allowP2PFallback;
    std::function<void(bool)> setTorDegraded;
    std::function<bool()> torWebReady;
    std::function<void(bool, bool, bool, bool)> updateAndLogTorReadinessState;
    std::function<network::NetworkConfig()> getNetworkConfig;
    std::function<void(const network::NetworkConfig&)> setNetworkConfig;
    std::function<std::vector<network::Peer>()> getPeers;
    std::function<void(const std::string&)> disconnectPeer;
    std::function<void(const std::string&, uint16_t)> connectPeer;
    std::function<void()> refreshDiscoveryFromPeers;
    std::function<void()> announceDiscovery;
    std::function<std::vector<network::BootstrapNode>()> getBootstrapNodes;
    std::function<std::vector<network::PeerInfo>(size_t)> getRandomPeers;
    std::function<std::string()> configuredTorSocksHost;
    std::function<uint16_t()> configuredTorSocksPort;
};

void runNetworkLoop(const NetworkLoopHooks& hooks);

struct NetworkRuntimeAdapterInputs {
    const NodeConfig* config = nullptr;
    std::function<bool()> shouldKeepRunning;
    std::function<bool()> torRequired;
    std::function<bool()> probeTorSocks;
    std::function<bool(bool, const char*)> maybeStartManagedTorRuntimeWithBackoff;
    std::function<void(bool)> setTorManaged;
    std::function<void()> resetManagedTorRestartBackoffState;
    std::function<void(bool)> setTorReachable;
    std::function<void(bool, bool)> refreshTorWebReadiness;
    std::function<bool()> allowClearnetFallback;
    std::function<bool()> allowP2PFallback;
    std::function<void(bool)> setTorDegraded;
    std::function<bool()> torWebReady;
    std::function<void(bool, bool, bool, bool)> updateAndLogTorReadinessState;
    network::Network* network = nullptr;
    network::Discovery* discovery = nullptr;
    std::function<std::string()> configuredTorSocksHost;
    std::function<uint16_t()> configuredTorSocksPort;
};

void runNetworkRuntimeAdapter(const NetworkRuntimeAdapterInputs& inputs);

struct SyncLoopHooks {
    std::function<bool()> shouldKeepRunning;
    std::function<uint64_t()> localHeight;
    std::function<uint64_t()> networkHeight;
    std::function<void(double)> setSyncProgress;
    std::function<bool()> hasLedger;
    std::function<void(bool)> setSyncing;
    std::function<std::vector<network::Peer>()> getPeers;
    std::function<size_t(uint64_t)> pruneRequestedBlocks;
    std::function<bool(uint64_t, uint64_t)> reserveRequestedBlock;
    std::function<void(const std::string&, uint64_t)> requestBlock;
};

void runSyncLoop(const SyncLoopHooks& hooks);

struct SyncRuntimeAdapterInputs {
    std::function<bool()> shouldKeepRunning;
    const core::Ledger* ledger = nullptr;
    const std::atomic<uint64_t>* networkHeight = nullptr;
    double* syncProgress = nullptr;
    std::atomic<bool>* syncing = nullptr;
    network::Network* network = nullptr;
    std::unordered_map<uint64_t, uint64_t>* requestedBlocks = nullptr;
    std::mutex* syncMtx = nullptr;
    std::function<void(const std::string&, uint64_t)> requestBlock;
};

void runSyncRuntimeAdapter(const SyncRuntimeAdapterInputs& inputs);

void runConsensusRuntimeAdapter(
    const std::function<bool()>& shouldKeepRunning,
    const std::function<void()>& processTimeouts);

struct MaintenanceLoopHooks {
    const NodeConfig* config = nullptr;
    std::function<bool()> shouldKeepRunning;
    std::function<void(uint64_t)> tickNaanCoordinationSupervised;
    std::function<void(uint64_t)> applyPoeSelfValidatorBootstrapPolicy;
    std::function<void(uint64_t)> maybeRunAutoPoeEpoch;
    std::function<void()> autoVoteSweepAllPending;
    std::function<void(uint64_t, uint32_t)> processPoeSyncRetries;
    std::function<void()> compactDatabase;
    std::function<void()> performQuantumMaintenance;
    std::function<void()> handleBlockBuildTick;
    std::function<void(uint64_t)> processRemoteModelMaintenance;
    std::function<bool(uint64_t)> maybeBroadcastLocalOffer;
};

void runMaintenanceLoop(const MaintenanceLoopHooks& hooks);

} // namespace synapse::node
