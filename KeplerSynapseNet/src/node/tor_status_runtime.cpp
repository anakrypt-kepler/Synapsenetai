#include "node/tor_status_runtime.h"

#include <stdexcept>

namespace synapse::node {

namespace {

void validateRuntimeInputs(const TorStatusRuntimeInputs& runtime) {
    if (!runtime.agentTorRequired || !runtime.agentTorReachable ||
        !runtime.agentTorWebReady || !runtime.agentTorManaged ||
        !runtime.managedTorPid || !runtime.agentAllowClearnetFallback ||
        !runtime.agentAllowP2PFallback || !runtime.agentTorWebProbeLastAt ||
        !runtime.agentTorWebProbeLastOkAt || !runtime.agentTorWebProbeExitCode ||
        !runtime.agentTorWebProbeConsecutiveFailures ||
        !runtime.agentTorWebProbeConsecutiveSuccesses ||
        !runtime.agentTorBridgeSubsetPersistCount ||
        !runtime.agentTorBridgeSubsetLastPersistAt ||
        !runtime.agentTorBridgeSubsetLastEpoch ||
        !runtime.agentTorBridgeSubsetLastCount ||
        !runtime.agentTorBridgeRemoteLastFetchAt ||
        !runtime.agentTorBridgeRemoteFetchAttempts ||
        !runtime.agentTorBridgeRemoteFetchSuccesses ||
        !runtime.agentTorBridgeRemoteRateLimitedSkips ||
        !runtime.torBridgeProviderMetaUpdatedAt ||
        !runtime.configuredTorRuntimeMode || !runtime.configuredTorSocksHost ||
        !runtime.configuredTorSocksPort || !runtime.configuredTorControlPort ||
        !runtime.probeTorControl || !runtime.activeOnionAddress ||
        !runtime.activeTorSeedAddress || !runtime.likelyTor9050vs9150ConflictHint ||
        !runtime.isOnionServiceActive || !runtime.getTorWebProbeLastError ||
        !runtime.torLastKnownGoodBridgeSubsetPath ||
        !runtime.getTorBridgeProviderMetaSnapshotJson) {
        throw std::runtime_error("invalid_tor_status_runtime_inputs");
    }
}

} // namespace

rpc::RpcNodeTorStatusInputs collectTorStatusInputs(
    const TorStatusRuntimeInputs& runtime,
    const core::TorRoutePolicyDecision& route) {
    validateRuntimeInputs(runtime);

    rpc::RpcNodeTorStatusInputs status;
    status.privacyMode = runtime.privacyMode;
    status.torRequired = runtime.agentTorRequired->load();
    status.torSocksReachable = runtime.agentTorReachable->load();
    status.torReadyForWeb = runtime.agentTorWebReady->load();
    status.torManaged = runtime.agentTorManaged->load();
    status.torManagedPid = runtime.managedTorPid->load();
    status.torRuntimeMode = runtime.configuredTorRuntimeMode();
    status.torSocksHost = runtime.configuredTorSocksHost();
    status.torSocksPort = runtime.configuredTorSocksPort();
    status.torControlPort = runtime.configuredTorControlPort();
    status.torControlReachable = runtime.probeTorControl();
    status.onionAddress = runtime.activeOnionAddress();
    status.torSeedAddress = runtime.activeTorSeedAddress();
    status.torConflictHint9050 =
        runtime.likelyTor9050vs9150ConflictHint(status.torSocksReachable);
    status.onionServiceActive = runtime.isOnionServiceActive();
    status.torDegraded = route.torDegraded;
    status.routeMode = route.routeMode;
    status.allowWebClearnet = route.allowWebClearnet;
    status.allowWebOnion = route.allowWebOnion;
    status.allowP2PDiscovery = route.allowP2PDiscovery;
    status.clearnetFallbackAllowed = runtime.agentAllowClearnetFallback->load();
    status.p2pFallbackAllowed = runtime.agentAllowP2PFallback->load();
    status.torWebProbeLastAt = runtime.agentTorWebProbeLastAt->load();
    status.torWebProbeLastOkAt = runtime.agentTorWebProbeLastOkAt->load();
    status.torWebProbeExitCode = runtime.agentTorWebProbeExitCode->load();
    status.torWebProbeLastError = runtime.getTorWebProbeLastError();
    status.torWebProbeConsecutiveFailures =
        runtime.agentTorWebProbeConsecutiveFailures->load();
    status.torWebProbeConsecutiveSuccesses =
        runtime.agentTorWebProbeConsecutiveSuccesses->load();
    status.torBridgeSubsetPersistCount =
        runtime.agentTorBridgeSubsetPersistCount->load();
    status.torBridgeSubsetLastPersistAt =
        runtime.agentTorBridgeSubsetLastPersistAt->load();
    status.torBridgeSubsetLastEpoch = runtime.agentTorBridgeSubsetLastEpoch->load();
    status.torBridgeSubsetLastCount = runtime.agentTorBridgeSubsetLastCount->load();
    status.torBridgeSubsetPath = runtime.torLastKnownGoodBridgeSubsetPath();
    status.torBridgeRemoteLastFetchAt =
        runtime.agentTorBridgeRemoteLastFetchAt->load();
    status.torBridgeRemoteFetchAttempts =
        runtime.agentTorBridgeRemoteFetchAttempts->load();
    status.torBridgeRemoteFetchSuccesses =
        runtime.agentTorBridgeRemoteFetchSuccesses->load();
    status.torBridgeRemoteRateLimitedSkips =
        runtime.agentTorBridgeRemoteRateLimitedSkips->load();
    status.torBridgeProviderJson = runtime.getTorBridgeProviderMetaSnapshotJson();
    status.torBridgeProviderUpdatedAt = runtime.torBridgeProviderMetaUpdatedAt->load();
    return status;
}

} // namespace synapse::node
