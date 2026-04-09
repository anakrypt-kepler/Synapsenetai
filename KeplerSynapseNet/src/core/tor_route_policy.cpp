#include "core/tor_route_policy.h"

#include <algorithm>
#include <cctype>

namespace synapse::core {

namespace {

uint16_t sanitizePort(int64_t raw, uint16_t fallback) {
    if (raw < 1 || raw > 65535) return fallback;
    return static_cast<uint16_t>(raw);
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

}

TorRoutePolicyDecision evaluateTorRoutePolicy(const TorRoutePolicyInput& input) {
    TorRoutePolicyDecision out;
    out.torDegraded = input.torRequired && !input.torReachable;

    if (input.torRequired) {
        if (out.torDegraded) {
            out.routeMode = input.allowClearnetFallback ? "DEGRADED_FALLBACK" : "DEGRADED";
        } else {
            out.routeMode = input.allowClearnetFallback ? "TOR_PLUS_FALLBACK" : "TOR_ONLY";
        }
    } else {
        out.routeMode = input.allowClearnetFallback ? "HYBRID" : "CLEARNET";
    }

    out.allowP2PDiscovery = !input.torRequired || input.torReachable || input.allowP2PFallback;

    if (!input.torRequired) {
        out.allowWebClearnet = true;
        out.allowWebOnion = true;
        return out;
    }

    if (input.torReachable) {
        out.allowWebClearnet = true;
        out.allowWebOnion = true;
        return out;
    }

    out.allowWebClearnet = input.allowClearnetFallback;
    out.allowWebOnion = false;
    return out;
}

OnionServicePolicyDecision evaluateOnionServicePolicy(const OnionServicePolicyInput& input) {
    OnionServicePolicyDecision out;

    uint16_t basePort = input.networkPort == 0 ? static_cast<uint16_t>(8333) : input.networkPort;
    out.virtualPort = sanitizePort(input.overrideVirtualPort, basePort);
    out.targetPort = sanitizePort(input.overrideTargetPort, basePort);

    if (input.dataDir.empty()) {
        out.serviceDir = "./onion_service";
    } else {
        out.serviceDir = input.dataDir + "/onion_service";
    }

    return out;
}

TorPrivacyEnableRecoveryResult runTorPrivacyEnableRecovery(
    bool initialEnabled,
    bool torRequired,
    int maxAttempts,
    const std::function<TorPrivacyBootstrapProbe()>& refreshRoute,
    const std::function<bool()>& reinitPrivacy,
    const std::function<bool()>& enablePrivacy,
    const std::function<void(int)>& sleepMs) {
    TorPrivacyEnableRecoveryResult out;
    out.enabled = initialEnabled;
    if (out.enabled || !torRequired || maxAttempts <= 0) {
        return out;
    }

    for (int attempt = 0; attempt < maxAttempts && !out.enabled; ++attempt) {
        const auto probe = refreshRoute ? refreshRoute() : TorPrivacyBootstrapProbe{};
        if (probe.torReachable && !probe.torDegraded) {
            const bool reinitOk = reinitPrivacy ? reinitPrivacy() : false;
            if (!reinitOk) {
                break;
            }
            out.enabled = enablePrivacy ? enablePrivacy() : false;
            if (out.enabled) {
                out.retryAttemptsUsed = attempt + 1;
                break;
            }
        }
        if (sleepMs) {
            sleepMs(250);
        }
    }

    return out;
}

std::string normalizeTorRuntimeMode(std::string mode) {
    std::transform(mode.begin(), mode.end(), mode.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (mode == "external_only") mode = "external";
    if (mode == "managed_only") mode = "managed";
    if (mode != "auto" && mode != "external" && mode != "managed") {
        mode = "auto";
    }
    return mode;
}

bool isManagedTorAutostartAllowedForMode(const std::string& runtimeMode) {
    return normalizeTorRuntimeMode(runtimeMode) != "external";
}

bool evaluateTor9050vs9150ConflictHint(const std::string& runtimeMode,
                                       uint16_t configuredSocksPort,
                                       bool torSocksReachable,
                                       bool port9150Reachable) {
    if (normalizeTorRuntimeMode(runtimeMode) == "external") return false;
    if (configuredSocksPort != 9050) return false;
    if (torSocksReachable) return false;
    return port9150Reachable;
}

std::string evaluateTorBootstrapState(const TorBootstrapStateInput& input) {
    if (!input.torRequired) return "NOT_REQUIRED";
    if (!input.torSocksReachable) return input.torDegraded ? "SOCKS_DOWN_DEGRADED" : "SOCKS_DOWN";
    if (!input.torWebReady) return input.torDegraded ? "SOCKS_UP_DEGRADED" : "SOCKS_UP_BOOTSTRAP";
    return input.torDegraded ? "WEB_READY_DEGRADED" : "WEB_READY";
}

uint32_t evaluateTorBootstrapPercent(const TorBootstrapStateInput& input) {
    if (!input.torRequired) return 100;
    if (!input.torSocksReachable) return 0;
    if (!input.torWebReady) return input.torDegraded ? 40u : 70u;
    return 100;
}

bool evaluateTorReadyForOnion(const TorBootstrapStateInput& input) {
    if (!input.torRequired) return false;
    return input.torSocksReachable && input.torWebReady && !input.torDegraded;
}

std::string evaluateTorBootstrapReasonCode(const TorBootstrapStateInput& input,
                                           const std::string& torWebProbeLastError) {
    if (!input.torRequired) return "NOT_REQUIRED";
    if (!input.torSocksReachable) {
        return input.torDegraded ? "SOCKS_UNREACHABLE_DEGRADED" : "SOCKS_UNREACHABLE";
    }
    if (!input.torWebReady) {
        if (!torWebProbeLastError.empty()) {
            if (torWebProbeLastError == "socks_unreachable") {
                return "SOCKS_UNREACHABLE";
            }
            return "WEB_PROBE_FAILED";
        }
        return "SOCKS_UP_BUT_NO_CIRCUIT";
    }
    if (input.torDegraded) return "READY_DEGRADED";
    return "READY";
}

std::string evaluateTorOnionServiceState(const TorOnionServiceStateInput& input) {
    if (!input.privacyModeEnabled) return "NOT_REQUESTED";
    if (!input.torRequired) return input.onionServiceActive ? "ACTIVE" : "TOR_NOT_REQUIRED";
    if (!input.torSocksReachable) {
        return input.torDegraded ? "SOCKS_DOWN_DEGRADED" : "SOCKS_DOWN";
    }
    if (!input.torWebReady) {
        return input.torDegraded ? "WEB_NOT_READY_DEGRADED" : "WEB_NOT_READY";
    }
    if (!input.torControlReachable) return "CONTROL_UNREACHABLE_WEB_READY";
    if (!input.onionServiceActive) {
        return input.torDegraded ? "SERVICE_DOWN_DEGRADED" : "SERVICE_DOWN";
    }
    return input.torDegraded ? "ACTIVE_DEGRADED" : "ACTIVE";
}

bool evaluateTorReadyForOnionService(const TorOnionServiceStateInput& input) {
    return evaluateTorOnionServiceState(input) == "ACTIVE";
}

std::string classifyWebSearchFailureKind(const std::string& errorMessage,
                                         bool torRequired,
                                         bool torReadyForWeb) {
    if (torRequired && !torReadyForWeb) {
        return "TOR_BOOTSTRAP_INCOMPLETE";
    }
    const std::string err = lowerAscii(errorMessage);
    if (err.find("curl: (28)") != std::string::npos ||
        err.find("timed out") != std::string::npos ||
        err.find("timeout") != std::string::npos) {
        return "TIMEOUT";
    }
    return "ERROR";
}

uint32_t evaluateManagedTorRestartBackoffSeconds(uint32_t consecutiveFailures,
                                                 uint32_t baseSeconds,
                                                 uint32_t maxSeconds) {
    if (baseSeconds == 0) baseSeconds = 1;
    if (maxSeconds == 0) maxSeconds = baseSeconds;
    if (maxSeconds < baseSeconds) maxSeconds = baseSeconds;

    if (consecutiveFailures == 0) return 0;

    uint64_t backoff = baseSeconds;
    const uint32_t shifts = consecutiveFailures > 1 ? (consecutiveFailures - 1) : 0;
    for (uint32_t i = 0; i < shifts && backoff < maxSeconds; ++i) {
        backoff *= 2u;
        if (backoff >= maxSeconds) {
            backoff = maxSeconds;
            break;
        }
    }
    if (backoff > maxSeconds) backoff = maxSeconds;
    return static_cast<uint32_t>(backoff);
}

ManagedTorRestartGateDecision evaluateManagedTorRestartGate(const ManagedTorRestartGateInput& input) {
    ManagedTorRestartGateDecision out;
    if (!input.torRequired) {
        out.reason = "tor_not_required";
        return out;
    }
    if (!input.allowStartManagedTor) {
        out.reason = "start_disallowed";
        return out;
    }
    if (!input.allowManagedAutostart) {
        out.reason = "autostart_disabled";
        return out;
    }
    if (input.torSocksReachable) {
        out.reason = "already_reachable";
        return out;
    }
    if (input.nextAllowedAttemptAt != 0 && input.now < input.nextAllowedAttemptAt) {
        out.reason = "backoff";
        return out;
    }
    out.shouldAttempt = true;
    out.reason = "allow";
    return out;
}

ManagedTorStartupResult runManagedTorStartup(
    bool allowManagedAutostart,
    int maxProbeAttempts,
    const std::function<bool()>& probeSocks,
    const std::function<bool()>& checkTorBinary,
    const std::function<bool()>& prepareRuntimeDir,
    const std::function<bool()>& spawnManagedTor,
    const std::function<std::optional<int64_t>()>& readManagedPid,
    const std::function<void(int)>& sleepMs) {
    ManagedTorStartupResult out;

    if (probeSocks && probeSocks()) {
        out.socksReachable = true;
        out.usedExistingSocks = true;
        out.stage = "already_running";
        return out;
    }

    if (!allowManagedAutostart) {
        out.stage = "external_mode";
        return out;
    }

    if (checkTorBinary && !checkTorBinary()) {
        out.stage = "tor_binary_missing";
        return out;
    }

    if (prepareRuntimeDir && !prepareRuntimeDir()) {
        out.stage = "mkdir_failed";
        return out;
    }

    out.attemptedSpawn = true;
    if (spawnManagedTor && !spawnManagedTor()) {
        out.stage = "spawn_failed";
        return out;
    }
    out.spawnSucceeded = true;

    if (maxProbeAttempts < 1) maxProbeAttempts = 1;
    for (int i = 0; i < maxProbeAttempts; ++i) {
        if (probeSocks && probeSocks()) {
            out.socksReachable = true;
            out.probeAttemptsUsed = i + 1;
            if (readManagedPid) {
                const auto pid = readManagedPid();
                if (pid.has_value() && pid.value() > 0) out.managedPid = pid.value();
            }
            out.stage = "ready";
            return out;
        }
        if (sleepMs) sleepMs(250);
    }

    out.probeAttemptsUsed = maxProbeAttempts;
    out.stage = "timeout";
    return out;
}

}
