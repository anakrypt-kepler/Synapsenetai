#pragma once

#include "core/tor_route_policy.h"
#include "rpc/rpc_node_views.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace synapse::node {

struct TorStatusRuntimeInputs {
    bool privacyMode = false;
    const std::atomic<bool>* agentTorRequired = nullptr;
    const std::atomic<bool>* agentTorReachable = nullptr;
    const std::atomic<bool>* agentTorWebReady = nullptr;
    const std::atomic<bool>* agentTorManaged = nullptr;
    const std::atomic<int64_t>* managedTorPid = nullptr;
    const std::atomic<bool>* agentAllowClearnetFallback = nullptr;
    const std::atomic<bool>* agentAllowP2PFallback = nullptr;
    const std::atomic<uint64_t>* agentTorWebProbeLastAt = nullptr;
    const std::atomic<uint64_t>* agentTorWebProbeLastOkAt = nullptr;
    const std::atomic<int>* agentTorWebProbeExitCode = nullptr;
    const std::atomic<uint32_t>* agentTorWebProbeConsecutiveFailures = nullptr;
    const std::atomic<uint32_t>* agentTorWebProbeConsecutiveSuccesses = nullptr;
    const std::atomic<uint64_t>* agentTorBridgeSubsetPersistCount = nullptr;
    const std::atomic<uint64_t>* agentTorBridgeSubsetLastPersistAt = nullptr;
    const std::atomic<uint64_t>* agentTorBridgeSubsetLastEpoch = nullptr;
    const std::atomic<uint32_t>* agentTorBridgeSubsetLastCount = nullptr;
    const std::atomic<uint64_t>* agentTorBridgeRemoteLastFetchAt = nullptr;
    const std::atomic<uint64_t>* agentTorBridgeRemoteFetchAttempts = nullptr;
    const std::atomic<uint64_t>* agentTorBridgeRemoteFetchSuccesses = nullptr;
    const std::atomic<uint64_t>* agentTorBridgeRemoteRateLimitedSkips = nullptr;
    const std::atomic<uint64_t>* torBridgeProviderMetaUpdatedAt = nullptr;
    std::function<std::string()> configuredTorRuntimeMode;
    std::function<std::string()> configuredTorSocksHost;
    std::function<uint16_t()> configuredTorSocksPort;
    std::function<uint16_t()> configuredTorControlPort;
    std::function<bool()> probeTorControl;
    std::function<std::string()> activeOnionAddress;
    std::function<std::string()> activeTorSeedAddress;
    std::function<bool(bool)> likelyTor9050vs9150ConflictHint;
    std::function<bool()> isOnionServiceActive;
    std::function<std::string()> getTorWebProbeLastError;
    std::function<std::string()> torLastKnownGoodBridgeSubsetPath;
    std::function<std::string()> getTorBridgeProviderMetaSnapshotJson;
};

rpc::RpcNodeTorStatusInputs collectTorStatusInputs(
    const TorStatusRuntimeInputs& runtime,
    const core::TorRoutePolicyDecision& route);

} // namespace synapse::node
