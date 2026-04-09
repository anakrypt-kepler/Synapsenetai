#include "core/poe_bootstrap_policy.h"
#include <iostream>
#include <string>

namespace {

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

}

int main() {
    using synapse::core::PoeSelfValidatorBootstrapPolicyConfig;
    using synapse::core::PoeSelfValidatorBootstrapPolicyDecision;
    using synapse::core::PoeSelfValidatorBootstrapPolicyInput;
    using synapse::core::PoeSelfValidatorBootstrapPolicyState;
    using synapse::core::evaluatePoeSelfValidatorBootstrapPolicy;

    PoeSelfValidatorBootstrapPolicyConfig cfg;
    cfg.autoDisableEnabled = true;
    cfg.strictConnectedPeers = 4;
    cfg.strictKnownPeers = 8;
    cfg.strictValidatorCount = 3;
    cfg.activationChecks = 2;

    PoeSelfValidatorBootstrapPolicyState state;
    PoeSelfValidatorBootstrapPolicyInput input;
    input.configuredAllowSelfBootstrap = true;
    input.now = 100;

    PoeSelfValidatorBootstrapPolicyDecision decision =
        evaluatePoeSelfValidatorBootstrapPolicy(cfg, state, input);
    if (!check(decision.effectiveAllowSelfBootstrap, "bootstrap should remain enabled before thresholds")) return 1;
    if (!check(decision.mode == "bootstrap", "unexpected initial mode")) return 1;
    if (!check(decision.state.consecutiveEligibleChecks == 0, "unexpected initial streak")) return 1;

    input.connectedPeers = 4;
    input.knownPeers = 2;
    input.validatorCount = 1;
    input.now = 101;
    decision = evaluatePoeSelfValidatorBootstrapPolicy(cfg, decision.state, input);
    if (!check(decision.effectiveAllowSelfBootstrap, "bootstrap should stay enabled until hysteresis completes")) return 1;
    if (!check(decision.state.consecutiveEligibleChecks == 1, "eligible streak should increment")) return 1;
    if (!check(!decision.transitionedToStrictMode, "strict mode should not trigger on first eligible check")) return 1;

    input.now = 102;
    decision = evaluatePoeSelfValidatorBootstrapPolicy(cfg, decision.state, input);
    if (!check(!decision.effectiveAllowSelfBootstrap, "strict mode should disable self-bootstrap")) return 1;
    if (!check(decision.mode == "strict", "strict mode expected after second eligible check")) return 1;
    if (!check(decision.transitionedToStrictMode, "strict mode transition flag missing")) return 1;
    if (!check(decision.shouldPersistState, "strict mode transition should request persistence")) return 1;
    if (!check(decision.state.persistentStrictMode, "strict mode should persist")) return 1;
    if (!check(decision.state.strictModeActivatedAt == 102, "strict mode activation timestamp mismatch")) return 1;

    cfg.temporaryOverrideUntil = 200;
    input.now = 150;
    decision = evaluatePoeSelfValidatorBootstrapPolicy(cfg, decision.state, input);
    if (!check(decision.temporaryOverrideActive, "temporary override should be active")) return 1;
    if (!check(decision.effectiveAllowSelfBootstrap, "temporary override should re-enable bootstrap")) return 1;
    if (!check(decision.mode == "bootstrap_override", "override mode expected")) return 1;

    cfg.temporaryOverrideUntil = 0;
    input.now = 201;
    decision = evaluatePoeSelfValidatorBootstrapPolicy(cfg, decision.state, input);
    if (!check(!decision.effectiveAllowSelfBootstrap, "strict mode should resume after override expiry")) return 1;
    if (!check(decision.mode == "strict", "strict mode should resume after override expiry")) return 1;

    PoeSelfValidatorBootstrapPolicyState disabledState;
    PoeSelfValidatorBootstrapPolicyInput disabledInput;
    disabledInput.configuredAllowSelfBootstrap = false;
    disabledInput.connectedPeers = 10;
    disabledInput.knownPeers = 20;
    disabledInput.validatorCount = 5;
    disabledInput.now = 300;
    decision = evaluatePoeSelfValidatorBootstrapPolicy(cfg, disabledState, disabledInput);
    if (!check(!decision.effectiveAllowSelfBootstrap, "disabled config should keep bootstrap off")) return 1;
    if (!check(decision.mode == "disabled", "disabled config mode mismatch")) return 1;

    std::cout << "poe bootstrap policy tests passed\n";
    return 0;
}
