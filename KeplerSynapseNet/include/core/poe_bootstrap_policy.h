#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace synapse {
namespace core {

struct PoeSelfValidatorBootstrapPolicyConfig {
    bool autoDisableEnabled = true;
    uint32_t strictConnectedPeers = 4;
    uint32_t strictKnownPeers = 8;
    uint32_t strictValidatorCount = 3;
    uint32_t activationChecks = 5;
    uint64_t temporaryOverrideUntil = 0;
};

struct PoeSelfValidatorBootstrapPolicyState {
    bool configuredAllowSelfBootstrap = false;
    bool persistentStrictMode = false;
    uint32_t consecutiveEligibleChecks = 0;
    uint64_t strictModeActivatedAt = 0;
    std::string strictModeReason;
};

struct PoeSelfValidatorBootstrapPolicyInput {
    bool configuredAllowSelfBootstrap = false;
    uint32_t connectedPeers = 0;
    uint32_t knownPeers = 0;
    uint32_t validatorCount = 0;
    uint64_t now = 0;
};

struct PoeSelfValidatorBootstrapPolicyDecision {
    PoeSelfValidatorBootstrapPolicyState state;
    bool effectiveAllowSelfBootstrap = false;
    bool temporaryOverrideActive = false;
    bool transitionedToStrictMode = false;
    bool shouldPersistState = false;
    uint32_t eligibleSignals = 0;
    std::string mode = "disabled";
    std::string statusReason;
};

inline std::string joinBootstrapReasons(const std::vector<std::string>& reasons) {
    std::string out;
    for (size_t i = 0; i < reasons.size(); ++i) {
        if (!out.empty()) out += ",";
        out += reasons[i];
    }
    return out;
}

inline PoeSelfValidatorBootstrapPolicyDecision evaluatePoeSelfValidatorBootstrapPolicy(
    const PoeSelfValidatorBootstrapPolicyConfig& config,
    const PoeSelfValidatorBootstrapPolicyState& currentState,
    const PoeSelfValidatorBootstrapPolicyInput& input) {
    PoeSelfValidatorBootstrapPolicyDecision decision;
    decision.state = currentState;
    decision.state.configuredAllowSelfBootstrap = input.configuredAllowSelfBootstrap;

    const bool overrideActive =
        config.temporaryOverrideUntil != 0 && input.now != 0 && input.now < config.temporaryOverrideUntil;
    decision.temporaryOverrideActive = overrideActive;

    if (!config.autoDisableEnabled) {
        decision.state.consecutiveEligibleChecks = 0;
        decision.effectiveAllowSelfBootstrap = input.configuredAllowSelfBootstrap || overrideActive;
        decision.mode = decision.effectiveAllowSelfBootstrap ? "bootstrap" : "disabled";
        decision.statusReason = decision.effectiveAllowSelfBootstrap
            ? "auto_disable_disabled"
            : "disabled_in_config";
        return decision;
    }

    if (overrideActive) {
        decision.state.consecutiveEligibleChecks = 0;
        decision.effectiveAllowSelfBootstrap = true;
        decision.mode = "bootstrap_override";
        decision.statusReason = "temporary_override_active";
        return decision;
    }

    if (!input.configuredAllowSelfBootstrap) {
        decision.state.consecutiveEligibleChecks = 0;
        decision.effectiveAllowSelfBootstrap = false;
        decision.mode = "disabled";
        decision.statusReason = "disabled_in_config";
        return decision;
    }

    if (decision.state.persistentStrictMode) {
        decision.state.consecutiveEligibleChecks = 0;
        decision.effectiveAllowSelfBootstrap = false;
        decision.mode = "strict";
        decision.statusReason = decision.state.strictModeReason.empty()
            ? "persistent_strict_mode"
            : decision.state.strictModeReason;
        return decision;
    }

    std::vector<std::string> reasons;
    if (config.strictConnectedPeers > 0 && input.connectedPeers >= config.strictConnectedPeers) {
        reasons.push_back("connected_peers");
    }
    if (config.strictKnownPeers > 0 && input.knownPeers >= config.strictKnownPeers) {
        reasons.push_back("known_peers");
    }
    if (config.strictValidatorCount > 0 && input.validatorCount >= config.strictValidatorCount) {
        reasons.push_back("validator_count");
    }

    decision.eligibleSignals = static_cast<uint32_t>(reasons.size());
    if (!reasons.empty()) {
        const uint32_t requiredChecks = std::max<uint32_t>(1, config.activationChecks);
        if (decision.state.consecutiveEligibleChecks < requiredChecks) {
            ++decision.state.consecutiveEligibleChecks;
        }
        if (decision.state.consecutiveEligibleChecks >= requiredChecks) {
            decision.state.persistentStrictMode = true;
            if (decision.state.strictModeActivatedAt == 0) {
                decision.state.strictModeActivatedAt = input.now;
            }
            decision.state.strictModeReason = joinBootstrapReasons(reasons);
            decision.effectiveAllowSelfBootstrap = false;
            decision.mode = "strict";
            decision.statusReason = decision.state.strictModeReason;
            decision.transitionedToStrictMode = true;
            decision.shouldPersistState = true;
            return decision;
        }
        decision.effectiveAllowSelfBootstrap = true;
        decision.mode = "bootstrap";
        decision.statusReason = "awaiting_stable_network_growth";
        return decision;
    }

    decision.state.consecutiveEligibleChecks = 0;
    decision.effectiveAllowSelfBootstrap = true;
    decision.mode = "bootstrap";
    decision.statusReason = "bootstrap_mode_active";
    return decision;
}

}
}
