#include "rpc/rpc_node_views.h"

#include "core/tor_route_policy.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace synapse::rpc {

namespace {

using json = nlohmann::json;

json parseJsonObjectOrEmpty(const std::string& raw) {
    if (raw.empty()) {
        return json::object();
    }
    try {
        auto parsed = json::parse(raw);
        if (parsed.is_object()) {
            return parsed;
        }
    } catch (...) {
    }
    return json::object();
}

std::string peerStateToString(network::PeerState state) {
    switch (state) {
        case network::PeerState::CONNECTING: return "CONNECTING";
        case network::PeerState::HANDSHAKING: return "HANDSHAKING";
        case network::PeerState::CONNECTED: return "CONNECTED";
        case network::PeerState::DISCONNECTING: return "DISCONNECTING";
        case network::PeerState::DISCONNECTED: return "DISCONNECTED";
        case network::PeerState::BANNED: return "BANNED";
    }
    return "UNKNOWN";
}

} // namespace

std::string buildRpcNodePeersResponse(
    const std::vector<network::Peer>& peers,
    const std::function<core::PeerDisplayInfo(const network::Peer&)>& peerDisplayInfo) {
    json out = json::array();
    for (const auto& peer : peers) {
        const auto display = peerDisplayInfo ? peerDisplayInfo(peer) : core::PeerDisplayInfo{};
        json item;
        item["id"] = peer.id;
        item["address"] = peer.address;
        item["port"] = peer.port;
        item["rawAddress"] = display.rawAddress;
        item["rawPort"] = display.rawPort;
        item["displayAddress"] = display.displayAddress;
        item["transport"] = display.transport;
        item["connectedAt"] = peer.connectedAt;
        item["lastSeen"] = peer.lastSeen;
        item["bytesRecv"] = peer.bytesRecv;
        item["bytesSent"] = peer.bytesSent;
        item["version"] = peer.version;
        item["startHeight"] = peer.startHeight;
        item["outbound"] = peer.isOutbound;
        item["state"] = peerStateToString(peer.state);
        out.push_back(std::move(item));
    }
    return out.dump();
}

std::string buildRpcNodeLogsResponse(
    const std::vector<utils::LogEntry>& logs) {
    json out = json::array();
    for (const auto& entry : logs) {
        json item;
        item["timestamp"] = entry.timestamp;
        item["level"] = static_cast<int>(entry.level);
        item["category"] = entry.category;
        item["message"] = entry.message;
        out.push_back(std::move(item));
    }
    return out.dump();
}

std::string buildRpcNodeQuantumStatusJson(const RpcNodeQuantumInputs& inputs) {
    json counters;
    counters["hybridOperations"] = inputs.hybridOperations;
    counters["qkdSessionsEstablished"] = inputs.qkdSessionsEstablished;
    counters["kyberEncapsulations"] = inputs.kyberEncapsulations;
    counters["kyberDecapsulations"] = inputs.kyberDecapsulations;
    counters["dilithiumSignatures"] = inputs.dilithiumSignatures;
    counters["dilithiumVerifications"] = inputs.dilithiumVerifications;
    counters["sphincsSignatures"] = inputs.sphincsSignatures;
    counters["sphincsVerifications"] = inputs.sphincsVerifications;
    counters["otpBytesUsed"] = inputs.otpBytesUsed;

    json implementations;
    implementations["kyber"] = inputs.kyberImplementation;
    implementations["dilithium"] = inputs.dilithiumImplementation;
    implementations["sphincs"] = inputs.sphincsImplementation;

    json out;
    out["enabled"] = inputs.enabled;
    out["requestedLevel"] = inputs.requestedLevel;
    out["managerInitialized"] = inputs.managerInitialized;
    out["isQuantumSafe"] = inputs.isQuantumSafe;
    out["effectiveLevel"] = inputs.effectiveLevel;
    out["capabilityMode"] = inputs.capabilityMode;
    out["liboqsEnabled"] = inputs.liboqsEnabled;
    out["qkdConnected"] = inputs.qkdConnected;
    out["qkdSessionActive"] = inputs.qkdSessionActive;
    out["selectedKEM"] = inputs.selectedKEM;
    out["selectedSignature"] = inputs.selectedSignature;
    out["selectedEncryption"] = inputs.selectedEncryption;
    out["qkdEncryptOperations"] = inputs.qkdEncryptOperations;
    out["hybridEncryptOperations"] = inputs.hybridEncryptOperations;
    out["qkdDecryptOperations"] = inputs.qkdDecryptOperations;
    out["hybridDecryptOperations"] = inputs.hybridDecryptOperations;
    out["qkdFallbackDecryptOperations"] = inputs.qkdFallbackDecryptOperations;
    out["kyberReal"] = inputs.kyberReal;
    out["dilithiumReal"] = inputs.dilithiumReal;
    out["sphincsReal"] = inputs.sphincsReal;
    out["counters"] = std::move(counters);
    out["implementations"] = std::move(implementations);
    return out.dump();
}

std::string buildRpcNodeTorStatusJson(const RpcNodeTorStatusInputs& inputs) {
    const core::TorBootstrapStateInput bootIn{
        inputs.torRequired,
        inputs.torSocksReachable,
        inputs.torReadyForWeb,
        inputs.torDegraded};
    const core::TorOnionServiceStateInput onionSvcIn{
        inputs.torRequired,
        inputs.torSocksReachable,
        inputs.torReadyForWeb,
        inputs.torDegraded,
        inputs.privacyMode,
        inputs.torControlReachable,
        inputs.onionServiceActive};
    const std::string torBootstrapReasonCode =
        core::evaluateTorBootstrapReasonCode(bootIn, inputs.torWebProbeLastError);
    const std::string torLastBootstrapError = inputs.torReadyForWeb
        ? std::string()
        : (inputs.torWebProbeLastError.empty() ? torBootstrapReasonCode : inputs.torWebProbeLastError);

    const json bridgeProviderMeta = parseJsonObjectOrEmpty(inputs.torBridgeProviderJson);
    uint64_t bridgeCacheAgeSeconds = 0;
    if (bridgeProviderMeta.contains("cacheSavedAt") && bridgeProviderMeta["cacheSavedAt"].is_number_unsigned()) {
        const uint64_t cacheSavedAt = bridgeProviderMeta.value("cacheSavedAt", static_cast<uint64_t>(0));
        const uint64_t nowTs = static_cast<uint64_t>(std::time(nullptr));
        if (cacheSavedAt != 0 && nowTs >= cacheSavedAt) {
            bridgeCacheAgeSeconds = nowTs - cacheSavedAt;
        }
    }

    json out;
    out["torRequired"] = inputs.torRequired;
    out["torSocksReachable"] = inputs.torSocksReachable;
    out["torReachable"] = inputs.torSocksReachable;
    out["torReadyForWeb"] = inputs.torReadyForWeb;
    out["torReadyForOnion"] = core::evaluateTorReadyForOnion(bootIn);
    out["torManaged"] = inputs.torManaged;
    out["torManagedPid"] = inputs.torManagedPid;
    out["torRuntimeMode"] = inputs.torRuntimeMode;
    out["torSocksHost"] = inputs.torSocksHost;
    out["torSocksPort"] = inputs.torSocksPort;
    out["torControlPort"] = inputs.torControlPort;
    out["torControlReachable"] = inputs.torControlReachable;
    out["onionAddress"] = inputs.onionAddress;
    out["torSeedAddress"] = inputs.torSeedAddress;
    out["torConflictHint9050"] = inputs.torConflictHint9050;
    out["torBootstrapState"] = core::evaluateTorBootstrapState(bootIn);
    out["torBootstrapPercent"] = core::evaluateTorBootstrapPercent(bootIn);
    out["torReadyForOnionService"] = core::evaluateTorReadyForOnionService(onionSvcIn);
    out["torOnionServiceActive"] = inputs.onionServiceActive;
    out["torOnionServiceState"] = core::evaluateTorOnionServiceState(onionSvcIn);
    out["torBootstrapReasonCode"] = torBootstrapReasonCode;
    out["torLastBootstrapError"] = torLastBootstrapError;
    out["torDegraded"] = inputs.torDegraded;
    out["torWebProbeLastAt"] = inputs.torWebProbeLastAt;
    out["torWebProbeLastOkAt"] = inputs.torWebProbeLastOkAt;
    out["torWebProbeExitCode"] = inputs.torWebProbeExitCode;
    out["torWebProbeLastError"] = inputs.torWebProbeLastError;
    out["torWebProbeConsecutiveFailures"] = inputs.torWebProbeConsecutiveFailures;
    out["torWebProbeConsecutiveSuccesses"] = inputs.torWebProbeConsecutiveSuccesses;
    out["torBridgeSubsetPersistCount"] = inputs.torBridgeSubsetPersistCount;
    out["torBridgeSubsetLastPersistAt"] = inputs.torBridgeSubsetLastPersistAt;
    out["torBridgeSubsetLastEpoch"] = inputs.torBridgeSubsetLastEpoch;
    out["torBridgeSubsetLastCount"] = inputs.torBridgeSubsetLastCount;
    out["torBridgeSubsetPath"] = inputs.torBridgeSubsetPath;
    out["torBridgeRemoteLastFetchAt"] = inputs.torBridgeRemoteLastFetchAt;
    out["torBridgeRemoteFetchAttempts"] = inputs.torBridgeRemoteFetchAttempts;
    out["torBridgeRemoteFetchSuccesses"] = inputs.torBridgeRemoteFetchSuccesses;
    out["torBridgeRemoteRateLimitedSkips"] = inputs.torBridgeRemoteRateLimitedSkips;
    out["torBridgeProvider"] = bridgeProviderMeta;
    out["torBridgeProviderUpdatedAt"] = inputs.torBridgeProviderUpdatedAt;
    out["torBridgeCacheAgeSeconds"] = bridgeCacheAgeSeconds;
    out["routeMode"] = inputs.routeMode;
    out["clearnetFallbackAllowed"] = inputs.clearnetFallbackAllowed;
    out["p2pFallbackAllowed"] = inputs.p2pFallbackAllowed;
    out["allowWebClearnet"] = inputs.allowWebClearnet;
    out["allowWebOnion"] = inputs.allowWebOnion;
    out["allowP2PDiscovery"] = inputs.allowP2PDiscovery;
    return out.dump();
}

std::string buildRpcNodeNetworkHealthJson(const RpcNodeNetworkHealthInputs& inputs) {
    json out;
    out["available"] = inputs.available;
    if (!inputs.available) {
        return out.dump();
    }

    out["totalPeers"] = inputs.totalPeers;
    out["inboundPeers"] = inputs.inboundPeers;
    out["outboundPeers"] = inputs.outboundPeers;
    out["peerPressurePercent"] = inputs.peerPressurePercent;
    out["inboundPressurePercent"] = inputs.inboundPressurePercent;
    out["outboundPressurePercent"] = inputs.outboundPressurePercent;
    out["bytesSent"] = inputs.bytesSent;
    out["bytesReceived"] = inputs.bytesReceived;
    out["messagesSent"] = inputs.messagesSent;
    out["messagesReceived"] = inputs.messagesReceived;
    out["overloadMode"] = inputs.overloadMode;
    out["bufferedRxBytes"] = inputs.bufferedRxBytes;
    out["rejectedConnections"] = inputs.rejectedConnections;
    out["evictedPeers"] = inputs.evictedPeers;
    out["tempBans"] = inputs.tempBans;
    out["malformedMessages"] = inputs.malformedMessages;
    out["rateLimitedEvents"] = inputs.rateLimitedEvents;
    out["overloadTransitions"] = inputs.overloadTransitions;
    out["activeBans"] = inputs.activeBans;
    out["invBackpressureDrops"] = inputs.invBackpressureDrops;
    out["getdataBackpressureDrops"] = inputs.getdataBackpressureDrops;
    out["gossipSuppressed"] = inputs.gossipSuppressed;
    out["gossipSubsetRouted"] = inputs.gossipSubsetRouted;
    out["consensusLag"] = inputs.consensusLag;
    return out.dump();
}

std::string buildRpcNodeStatusResponse(const RpcNodeStatusInputs& inputs) {
    json out;
    out["running"] = inputs.running;
    out["networkType"] = inputs.networkType;
    out["p2pPort"] = inputs.p2pPort;
    out["rpcPort"] = inputs.rpcPort;
    out["peersConnected"] = inputs.peersConnected;
    out["uptimeSeconds"] = inputs.uptimeSeconds;
    out["uptime"] = inputs.uptime;
    out["syncProgress"] = inputs.syncProgress;
    if (inputs.ledgerAvailable) {
        out["ledgerHeight"] = inputs.ledgerHeight;
        out["ledgerEvents"] = inputs.ledgerEvents;
        out["tipHash"] = inputs.tipHash;
    }
    out["knowledgeEntries"] = inputs.knowledgeEntries;
    out["walletAddress"] = inputs.walletAddress;
    out["privacyMode"] = inputs.privacyMode;
    out["quantumSecurity"] = inputs.quantumSecurity;
    out["quantum"] = parseJsonObjectOrEmpty(buildRpcNodeQuantumStatusJson(inputs.quantum));
    out["poeBootstrap"] = parseJsonObjectOrEmpty(inputs.poeBootstrapJson);
    const json torStatus = parseJsonObjectOrEmpty(buildRpcNodeTorStatusJson(inputs.tor));
    for (auto it = torStatus.begin(); it != torStatus.end(); ++it) {
        out[it.key()] = it.value();
    }
    out["networkHealth"] = parseJsonObjectOrEmpty(buildRpcNodeNetworkHealthJson(inputs.networkHealth));
    return out.dump();
}

std::string buildRpcNodeTorControlResponse(const RpcNodeTorControlResponseInputs& inputs) {
    json out;
    out["ok"] = true;
    out["action"] = inputs.action;

    const json torStatus = parseJsonObjectOrEmpty(buildRpcNodeTorStatusJson(inputs.tor));
    for (auto it = torStatus.begin(); it != torStatus.end(); ++it) {
        out[it.key()] = it.value();
    }

    if (inputs.includeBridgeRefresh) {
        out["bridgeRefresh"] = parseJsonObjectOrEmpty(inputs.bridgeRefreshJson);
    }

    if (inputs.includeModeSwitch) {
        json modeSwitch;
        modeSwitch["requestedMode"] = inputs.modeSwitch.requestedMode;
        modeSwitch["appliedMode"] = inputs.modeSwitch.appliedMode;
        modeSwitch["socksHost"] = inputs.modeSwitch.socksHost;
        modeSwitch["socksPort"] = inputs.modeSwitch.socksPort;
        modeSwitch["controlPort"] = inputs.modeSwitch.controlPort;
        modeSwitch["persisted"] = inputs.modeSwitch.persisted;
        modeSwitch["stoppedManagedTor"] = inputs.modeSwitch.stoppedManagedTor;
        modeSwitch["startedManagedTor"] = inputs.modeSwitch.startedManagedTor;
        if (inputs.modeSwitch.configSavedSet) {
            modeSwitch["configSaved"] = inputs.modeSwitch.configSaved;
            modeSwitch["configPath"] = inputs.modeSwitch.configPath;
        }
        out["modeSwitch"] = std::move(modeSwitch);
    }

    if (inputs.includeManagedRestart) {
        out["managedRestart"] = {
            {"stoppedManagedTor", inputs.managedRestart.stoppedManagedTor},
            {"startedManagedTor", inputs.managedRestart.startedManagedTor},
            {"mode", inputs.managedRestart.mode}
        };
    }

    if (inputs.includeLastKnownGoodBridgeSubsetPersisted) {
        out["lastKnownGoodBridgeSubsetPersisted"] =
            inputs.lastKnownGoodBridgeSubsetPersisted;
    }

    return out.dump();
}

std::string buildRpcNodeSeedsResponse(const RpcNodeSeedsInputs& inputs) {
    json out;
    out["bootstrap"] = json::array();
    out["dnsSeeds"] = json::array();

    if (core::isValidOnionHost(inputs.onionAddress)) {
        json item;
        item["address"] = inputs.onionAddress;
        item["port"] = inputs.onionPort;
        item["transport"] = "tor-onion";
        item["active"] = inputs.onionActive;
        item["seedAddress"] = inputs.torSeedAddress;
        out["bootstrap"].push_back(std::move(item));
    }

    for (const auto& bootstrapNode : inputs.bootstrapNodes) {
        json item;
        item["address"] = bootstrapNode.address;
        item["port"] = bootstrapNode.port;
        item["transport"] = "clearnet";
        item["active"] = bootstrapNode.active;
        item["failures"] = bootstrapNode.failures;
        item["lastSeen"] = bootstrapNode.lastSeen;
        item["healthScore"] = bootstrapNode.healthScore;
        item["quarantineUntil"] = bootstrapNode.quarantineUntil;
        item["lastFailureAt"] = bootstrapNode.lastFailureAt;
        out["bootstrap"].push_back(std::move(item));
    }

    for (const auto& dnsSeed : inputs.dnsSeeds) {
        out["dnsSeeds"].push_back(dnsSeed);
    }

    return out.dump();
}

std::string buildRpcNodeDiscoveryStatsResponse(
    const std::optional<network::DiscoveryStats>& stats,
    bool running,
    size_t dnsSeedCount,
    size_t bootstrapNodeCount) {
    json out;
    if (!stats.has_value()) {
        out["running"] = false;
        out["knownPeers"] = 0;
        out["connectedPeers"] = 0;
        out["dnsSeeds"] = 0;
        out["bootstrapNodes"] = 0;
        out["activeBootstrapNodes"] = 0;
        out["quarantinedBootstrapNodes"] = 0;
        out["uniqueSeedDomains"] = 0;
        out["dnsQueries"] = 0;
        out["peerExchanges"] = 0;
        out["lastPeerRefresh"] = 0;
        out["lastAnnounce"] = 0;
        return out.dump();
    }

    out["running"] = running;
    out["knownPeers"] = stats->knownPeersCount;
    out["connectedPeers"] = stats->connectedPeers;
    out["dnsSeeds"] = dnsSeedCount;
    out["bootstrapNodes"] = bootstrapNodeCount;
    out["activeBootstrapNodes"] = stats->activeBootstrapNodes;
    out["quarantinedBootstrapNodes"] = stats->quarantinedBootstrapNodes;
    out["uniqueSeedDomains"] = stats->uniqueSeedDomains;
    out["dnsQueries"] = stats->dnsQueries;
    out["peerExchanges"] = stats->peerExchanges;
    out["peerExchangeSuccessRate"] = stats->peerExchangeSuccessRate;
    out["lastPeerRefresh"] = stats->lastRefreshTime;
    out["lastAnnounce"] = stats->lastAnnounceTime;
    out["networkSizeEstimate"] = stats->networkSize;
    out["totalDiscovered"] = stats->totalDiscovered;
    out["totalConnected"] = stats->totalConnected;
    out["totalFailed"] = stats->totalFailed;
    return out.dump();
}

std::string normalizeRpcTorControlAction(std::string action) {
    std::transform(action.begin(), action.end(), action.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (action == "refresh" || action == "refresh-bridges") return "refresh_bridges";
    if (action == "restart" || action == "restart-managed" || action == "restart-managed-tor") {
        return "restart_managed_tor";
    }
    if (action == "switch" || action == "switch-mode") return "switch_mode";
    return action;
}

} // namespace synapse::rpc
