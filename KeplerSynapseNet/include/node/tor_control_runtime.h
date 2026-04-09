#pragma once

#include "core/tor_route_policy.h"
#include "rpc/rpc_node_views.h"
#include "web/web.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace synapse::node {

struct ReloadNaanWebConfigInputs {
    std::string dataDir;
    bool persistSanitized = false;
    web::WebSearch* webSearch = nullptr;
    web::HtmlExtractor* webExtractor = nullptr;
    std::atomic<uint64_t>* webConfigTotalLines = nullptr;
    std::atomic<uint64_t>* webConfigAppliedLines = nullptr;
    std::atomic<uint64_t>* webConfigInvalidLines = nullptr;
    std::atomic<uint64_t>* webConfigUnknownKeys = nullptr;
    std::vector<std::string>* webConfigUnknownKeySamples = nullptr;
    std::atomic<uint64_t>* webConfigSanitizedWrites = nullptr;
    std::atomic<uint64_t>* agentTorBridgeRemoteLastFetchAt = nullptr;
    std::atomic<uint64_t>* agentTorBridgeRemoteFetchAttempts = nullptr;
    std::atomic<uint64_t>* agentTorBridgeRemoteFetchSuccesses = nullptr;
    std::atomic<uint64_t>* agentTorBridgeRemoteRateLimitedSkips = nullptr;
    std::function<std::string()> configuredTorRuntimeMode;
    std::function<std::string()> configuredTorSocksHost;
    std::function<uint16_t()> configuredTorSocksPort;
    std::function<uint16_t()> configuredTorControlPort;
    std::function<bool()> agentTorRequired;
    std::function<bool()> agentTorReachable;
    std::function<core::TorRoutePolicyDecision(bool)> refreshTorRoutePolicy;
    std::function<void(const std::string&)> setTorBridgeProviderMetaSnapshotJson;
};

struct TorControlActionCallbacks {
    std::function<rpc::RpcNodeTorStatusInputs(const core::TorRoutePolicyDecision&)> collectTorControlStatus;
    std::function<std::string(bool)> reloadNaanWebConfig;
    std::function<core::TorRoutePolicyDecision(bool)> refreshTorRoutePolicy;
    std::function<bool(bool)> stopManagedTorRuntimeIfOwned;
    std::function<void()> resetManagedTorRestartBackoffState;
    std::function<bool()> startManagedTorRuntime;
    std::function<std::string()> configuredTorRuntimeMode;
    std::function<bool(const std::string&)> persistLastKnownGoodBridgeSubset;
    std::function<bool(const std::string&)> saveConfig;
    std::string configPath;
};

std::string reloadNaanWebConfigJson(ReloadNaanWebConfigInputs& inputs);

std::string handleTorControlAction(
    const nlohmann::json& params,
    const std::string& actionRaw,
    TorControlActionCallbacks& callbacks,
    std::atomic<bool>& restartInFlight);

} // namespace synapse::node
