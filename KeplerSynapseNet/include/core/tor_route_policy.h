#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace synapse::core {

struct TorRoutePolicyInput {
    bool torRequired = true;
    bool torReachable = false;
    bool allowClearnetFallback = false;
    bool allowP2PFallback = false;
};

struct TorRoutePolicyDecision {
    bool torDegraded = false;
    std::string routeMode;
    bool allowWebClearnet = false;
    bool allowWebOnion = false;
    bool allowP2PDiscovery = false;
};

TorRoutePolicyDecision evaluateTorRoutePolicy(const TorRoutePolicyInput& input);

struct OnionServicePolicyInput {
    uint16_t networkPort = 8333;
    int64_t overrideVirtualPort = 0;
    int64_t overrideTargetPort = 0;
    std::string dataDir;
};

struct OnionServicePolicyDecision {
    uint16_t virtualPort = 8333;
    uint16_t targetPort = 8333;
    std::string serviceDir;
};

OnionServicePolicyDecision evaluateOnionServicePolicy(const OnionServicePolicyInput& input);

struct TorPrivacyBootstrapProbe {
    bool torReachable = false;
    bool torDegraded = true;
};

struct TorPrivacyEnableRecoveryResult {
    bool enabled = false;
    int retryAttemptsUsed = 0;
};

TorPrivacyEnableRecoveryResult runTorPrivacyEnableRecovery(
    bool initialEnabled,
    bool torRequired,
    int maxAttempts,
    const std::function<TorPrivacyBootstrapProbe()>& refreshRoute,
    const std::function<bool()>& reinitPrivacy,
    const std::function<bool()>& enablePrivacy,
    const std::function<void(int)>& sleepMs);

std::string normalizeTorRuntimeMode(std::string mode);

bool isManagedTorAutostartAllowedForMode(const std::string& runtimeMode);

bool evaluateTor9050vs9150ConflictHint(const std::string& runtimeMode,
                                       uint16_t configuredSocksPort,
                                       bool torSocksReachable,
                                       bool port9150Reachable);

struct TorBootstrapStateInput {
    bool torRequired = true;
    bool torSocksReachable = false;
    bool torWebReady = false;
    bool torDegraded = true;
};

std::string evaluateTorBootstrapState(const TorBootstrapStateInput& input);

uint32_t evaluateTorBootstrapPercent(const TorBootstrapStateInput& input);

bool evaluateTorReadyForOnion(const TorBootstrapStateInput& input);

std::string evaluateTorBootstrapReasonCode(const TorBootstrapStateInput& input,
                                           const std::string& torWebProbeLastError);

struct TorOnionServiceStateInput {
    bool torRequired = true;
    bool torSocksReachable = false;
    bool torWebReady = false;
    bool torDegraded = true;
    bool privacyModeEnabled = false;
    bool torControlReachable = false;
    bool onionServiceActive = false;
};

std::string evaluateTorOnionServiceState(const TorOnionServiceStateInput& input);

bool evaluateTorReadyForOnionService(const TorOnionServiceStateInput& input);

std::string classifyWebSearchFailureKind(const std::string& errorMessage,
                                         bool torRequired,
                                         bool torReadyForWeb);

uint32_t evaluateManagedTorRestartBackoffSeconds(uint32_t consecutiveFailures,
                                                 uint32_t baseSeconds,
                                                 uint32_t maxSeconds);

struct ManagedTorRestartGateInput {
    bool torRequired = true;
    bool allowStartManagedTor = true;
    bool allowManagedAutostart = true;
    bool torSocksReachable = false;
    uint64_t now = 0;
    uint64_t nextAllowedAttemptAt = 0;
};

struct ManagedTorRestartGateDecision {
    bool shouldAttempt = false;
    std::string reason;
};

ManagedTorRestartGateDecision evaluateManagedTorRestartGate(const ManagedTorRestartGateInput& input);

struct ManagedTorStartupResult {
    bool socksReachable = false;
    bool usedExistingSocks = false;
    bool attemptedSpawn = false;
    bool spawnSucceeded = false;
    int probeAttemptsUsed = 0;
    int64_t managedPid = 0;
    std::string stage;
};

ManagedTorStartupResult runManagedTorStartup(
    bool allowManagedAutostart,
    int maxProbeAttempts,
    const std::function<bool()>& probeSocks,
    const std::function<bool()>& checkTorBinary,
    const std::function<bool()>& prepareRuntimeDir,
    const std::function<bool()>& spawnManagedTor,
    const std::function<std::optional<int64_t>()>& readManagedPid,
    const std::function<void(int)>& sleepMs);

}
