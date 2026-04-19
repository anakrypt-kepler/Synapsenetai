#pragma once

#include "core/tor_peer_identity.h"
#include "network/discovery.h"
#include "network/network.h"
#include "utils/logger.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace synapse::rpc {

std::string buildRpcNodePeersResponse(
    const std::vector<network::Peer>& peers,
    const std::function<core::PeerDisplayInfo(const network::Peer&)>& peerDisplayInfo);

std::string buildRpcNodeLogsResponse(
    const std::vector<utils::LogEntry>& logs);

struct RpcNodeQuantumInputs {
    bool enabled = false;
    std::string requestedLevel = "standard";
    bool managerInitialized = false;
    bool isQuantumSafe = false;
    std::string effectiveLevel = "standard";
    std::string capabilityMode = "development_simulation";
    bool liboqsEnabled = false;
    bool qkdConnected = false;
    bool qkdSessionActive = false;
    std::string selectedKEM = "classic_x25519";
    std::string selectedSignature = "classic_ed25519";
    std::string selectedEncryption = "classic_aes256gcm";
    uint64_t qkdEncryptOperations = 0;
    uint64_t hybridEncryptOperations = 0;
    uint64_t qkdDecryptOperations = 0;
    uint64_t hybridDecryptOperations = 0;
    uint64_t qkdFallbackDecryptOperations = 0;
    uint64_t hybridOperations = 0;
    uint64_t qkdSessionsEstablished = 0;
    uint64_t kyberEncapsulations = 0;
    uint64_t kyberDecapsulations = 0;
    uint64_t dilithiumSignatures = 0;
    uint64_t dilithiumVerifications = 0;
    uint64_t sphincsSignatures = 0;
    uint64_t sphincsVerifications = 0;
    uint64_t otpBytesUsed = 0;
    bool kyberReal = false;
    bool dilithiumReal = false;
    bool sphincsReal = false;
    std::string kyberImplementation = "simulation";
    std::string dilithiumImplementation = "simulation";
    std::string sphincsImplementation = "simulation";
};

std::string buildRpcNodeQuantumStatusJson(const RpcNodeQuantumInputs& inputs);

struct RpcNodeTorStatusInputs {
    bool privacyMode = false;
    bool torRequired = false;
    bool torSocksReachable = false;
    bool torReadyForWeb = false;
    bool torManaged = false;
    int64_t torManagedPid = 0;
    std::string torRuntimeMode;
    std::string torSocksHost = "127.0.0.1";
    uint16_t torSocksPort = 0;
    uint16_t torControlPort = 0;
    bool torControlReachable = false;
    std::string onionAddress;
    std::string torSeedAddress;
    bool torConflictHint9050 = false;
    bool onionServiceActive = false;
    bool torDegraded = false;
    std::string routeMode;
    bool allowWebClearnet = false;
    bool allowWebOnion = false;
    bool allowP2PDiscovery = false;
    bool clearnetFallbackAllowed = false;
    bool p2pFallbackAllowed = false;
    uint64_t torWebProbeLastAt = 0;
    uint64_t torWebProbeLastOkAt = 0;
    int64_t torWebProbeExitCode = 0;
    std::string torWebProbeLastError;
    uint64_t torWebProbeConsecutiveFailures = 0;
    uint64_t torWebProbeConsecutiveSuccesses = 0;
    uint64_t torBridgeSubsetPersistCount = 0;
    uint64_t torBridgeSubsetLastPersistAt = 0;
    uint64_t torBridgeSubsetLastEpoch = 0;
    uint64_t torBridgeSubsetLastCount = 0;
    std::string torBridgeSubsetPath;
    uint64_t torBridgeRemoteLastFetchAt = 0;
    uint64_t torBridgeRemoteFetchAttempts = 0;
    uint64_t torBridgeRemoteFetchSuccesses = 0;
    uint64_t torBridgeRemoteRateLimitedSkips = 0;
    std::string torBridgeProviderJson = "{}";
    uint64_t torBridgeProviderUpdatedAt = 0;
};

std::string buildRpcNodeTorStatusJson(const RpcNodeTorStatusInputs& inputs);

struct RpcNodeNetworkHealthInputs {
    bool available = false;
    uint64_t totalPeers = 0;
    uint64_t inboundPeers = 0;
    uint64_t outboundPeers = 0;
    uint64_t peerPressurePercent = 0;
    uint64_t inboundPressurePercent = 0;
    uint64_t outboundPressurePercent = 0;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint64_t messagesSent = 0;
    uint64_t messagesReceived = 0;
    bool overloadMode = false;
    uint64_t bufferedRxBytes = 0;
    uint64_t rejectedConnections = 0;
    uint64_t evictedPeers = 0;
    uint64_t tempBans = 0;
    uint64_t malformedMessages = 0;
    uint64_t rateLimitedEvents = 0;
    uint64_t overloadTransitions = 0;
    size_t activeBans = 0;
    uint64_t invBackpressureDrops = 0;
    uint64_t getdataBackpressureDrops = 0;
    uint64_t gossipSuppressed = 0;
    uint64_t gossipSubsetRouted = 0;
    uint64_t consensusLag = 0;
};

std::string buildRpcNodeNetworkHealthJson(const RpcNodeNetworkHealthInputs& inputs);

struct RpcNodeStatusInputs {
    bool running = false;
    std::string networkType;
    uint16_t p2pPort = 0;
    uint16_t rpcPort = 0;
    uint64_t peersConnected = 0;
    uint64_t uptimeSeconds = 0;
    std::string uptime;
    double syncProgress = 0.0;
    bool ledgerAvailable = false;
    uint64_t ledgerHeight = 0;
    uint64_t ledgerEvents = 0;
    std::string tipHash;
    uint64_t knowledgeEntries = 0;
    std::string walletAddress;
    bool privacyMode = false;
    bool quantumSecurity = false;
    RpcNodeQuantumInputs quantum;
    std::string poeBootstrapJson = "{}";
    RpcNodeTorStatusInputs tor;
    RpcNodeNetworkHealthInputs networkHealth;
};

std::string buildRpcNodeStatusResponse(const RpcNodeStatusInputs& inputs);

struct RpcNodeTorControlModeSwitchInputs {
    std::string requestedMode;
    std::string appliedMode;
    std::string socksHost = "127.0.0.1";
    uint16_t socksPort = 0;
    uint16_t controlPort = 0;
    bool persisted = false;
    bool stoppedManagedTor = false;
    bool startedManagedTor = false;
    bool configSavedSet = false;
    bool configSaved = false;
    std::string configPath;
};

struct RpcNodeTorControlManagedRestartInputs {
    bool stoppedManagedTor = false;
    bool startedManagedTor = false;
    std::string mode;
};

struct RpcNodeTorControlResponseInputs {
    std::string action;
    RpcNodeTorStatusInputs tor;
    bool includeBridgeRefresh = false;
    std::string bridgeRefreshJson = "{}";
    bool includeModeSwitch = false;
    RpcNodeTorControlModeSwitchInputs modeSwitch;
    bool includeManagedRestart = false;
    RpcNodeTorControlManagedRestartInputs managedRestart;
    bool includeLastKnownGoodBridgeSubsetPersisted = false;
    bool lastKnownGoodBridgeSubsetPersisted = false;
};

std::string buildRpcNodeTorControlResponse(const RpcNodeTorControlResponseInputs& inputs);

struct RpcNodeSeedsInputs {
    std::string onionAddress;
    uint16_t onionPort = 0;
    bool onionActive = false;
    std::string torSeedAddress;
    std::vector<network::BootstrapNode> bootstrapNodes;
    std::vector<std::string> dnsSeeds;
};

std::string buildRpcNodeSeedsResponse(const RpcNodeSeedsInputs& inputs);

std::string buildRpcNodeDiscoveryStatsResponse(
    const std::optional<network::DiscoveryStats>& stats,
    bool running,
    size_t dnsSeedCount,
    size_t bootstrapNodeCount);

std::string normalizeRpcTorControlAction(std::string action);

} // namespace synapse::rpc
