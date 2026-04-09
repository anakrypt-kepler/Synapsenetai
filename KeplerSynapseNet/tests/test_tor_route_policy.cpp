#include "core/tor_route_policy.h"

#include <cassert>
#include <cstddef>
#include <vector>

static void assertDecisionEquals(const synapse::core::TorRoutePolicyDecision& left,
                                 const synapse::core::TorRoutePolicyDecision& right) {
    assert(left.torDegraded == right.torDegraded);
    assert(left.routeMode == right.routeMode);
    assert(left.allowWebClearnet == right.allowWebClearnet);
    assert(left.allowWebOnion == right.allowWebOnion);
    assert(left.allowP2PDiscovery == right.allowP2PDiscovery);
}

static void testDefaultPolicyIsTorRequiredFailClosed() {
    synapse::core::TorRoutePolicyInput input;
    const auto out = synapse::core::evaluateTorRoutePolicy(input);
    assert(out.torDegraded);
    assert(out.routeMode == "DEGRADED");
    assert(!out.allowWebClearnet);
    assert(!out.allowWebOnion);
    assert(!out.allowP2PDiscovery);
}

static void testTorUnavailableFailClosedWithNoFallbacks() {
    synapse::core::TorRoutePolicyInput input;
    input.torRequired = true;
    input.torReachable = false;
    input.allowClearnetFallback = false;
    input.allowP2PFallback = false;

    const auto out = synapse::core::evaluateTorRoutePolicy(input);
    assert(out.torDegraded);
    assert(out.routeMode == "DEGRADED");
    assert(!out.allowWebClearnet);
    assert(!out.allowWebOnion);
    assert(!out.allowP2PDiscovery);
}

static void testTorUnavailableFallbackBehavior() {
    synapse::core::TorRoutePolicyInput input;
    input.torRequired = true;
    input.torReachable = false;
    input.allowClearnetFallback = true;
    input.allowP2PFallback = false;

    const auto out = synapse::core::evaluateTorRoutePolicy(input);
    assert(out.torDegraded);
    assert(out.routeMode == "DEGRADED_FALLBACK");
    assert(out.allowWebClearnet);
    assert(!out.allowWebOnion);
    assert(!out.allowP2PDiscovery);

    input.allowP2PFallback = true;
    const auto p2pFallbackOut = synapse::core::evaluateTorRoutePolicy(input);
    assert(p2pFallbackOut.torDegraded);
    assert(p2pFallbackOut.routeMode == "DEGRADED_FALLBACK");
    assert(p2pFallbackOut.allowWebClearnet);
    assert(!p2pFallbackOut.allowWebOnion);
    assert(p2pFallbackOut.allowP2PDiscovery);
}

static void testNonTorRequiredModeUsesClearnetProfile() {
    synapse::core::TorRoutePolicyInput input;
    input.torRequired = false;
    input.torReachable = false;
    input.allowClearnetFallback = false;
    input.allowP2PFallback = false;

    const auto out = synapse::core::evaluateTorRoutePolicy(input);
    assert(!out.torDegraded);
    assert(out.routeMode == "CLEARNET");
    assert(out.allowWebClearnet);
    assert(out.allowWebOnion);
    assert(out.allowP2PDiscovery);
}

static void testHealthyTorRequiredMode() {
    synapse::core::TorRoutePolicyInput input;
    input.torRequired = true;
    input.torReachable = true;

    const auto out = synapse::core::evaluateTorRoutePolicy(input);
    assert(!out.torDegraded);
    assert(out.routeMode == "TOR_ONLY");
    assert(out.allowWebClearnet);
    assert(out.allowWebOnion);
    assert(out.allowP2PDiscovery);
}

static void testRoutePolicyDeterminismAcrossMatrix() {
    std::vector<synapse::core::TorRoutePolicyInput> cases;
    cases.push_back({true, false, false, false});
    cases.push_back({true, false, true, false});
    cases.push_back({true, false, true, true});
    cases.push_back({true, true, false, false});
    cases.push_back({true, true, true, false});
    cases.push_back({false, false, false, false});
    cases.push_back({false, false, true, false});
    cases.push_back({false, true, false, true});

    for (const auto& input : cases) {
        const auto baseline = synapse::core::evaluateTorRoutePolicy(input);
        for (int i = 0; i < 256; ++i) {
            const auto replay = synapse::core::evaluateTorRoutePolicy(input);
            assertDecisionEquals(baseline, replay);
        }
    }
}

static void testTorPrivacyEnableRecoveryRecoversAfterRouteHealthy() {
    std::vector<synapse::core::TorPrivacyBootstrapProbe> probes = {
        {false, true},
        {true, false},
    };
    size_t probeIndex = 0;
    int reinitCalls = 0;
    int enableCalls = 0;
    int sleepCalls = 0;

    const auto out = synapse::core::runTorPrivacyEnableRecovery(
        false,
        true,
        8,
        [&]() {
            assert(probeIndex < probes.size());
            return probes[probeIndex++];
        },
        [&]() {
            ++reinitCalls;
            return true;
        },
        [&]() {
            ++enableCalls;
            return true;
        },
        [&](int ms) {
            assert(ms == 250);
            ++sleepCalls;
        });

    assert(out.enabled);
    assert(out.retryAttemptsUsed == 2);
    assert(reinitCalls == 1);
    assert(enableCalls == 1);
    assert(sleepCalls == 1);
}

static void testTorPrivacyEnableRecoverySkipsWhenTorNotRequired() {
    int refreshCalls = 0;
    int reinitCalls = 0;
    int enableCalls = 0;
    int sleepCalls = 0;

    const auto out = synapse::core::runTorPrivacyEnableRecovery(
        false,
        false,
        8,
        [&]() {
            ++refreshCalls;
            return synapse::core::TorPrivacyBootstrapProbe{};
        },
        [&]() {
            ++reinitCalls;
            return true;
        },
        [&]() {
            ++enableCalls;
            return true;
        },
        [&](int) {
            ++sleepCalls;
        });

    assert(!out.enabled);
    assert(out.retryAttemptsUsed == 0);
    assert(refreshCalls == 0);
    assert(reinitCalls == 0);
    assert(enableCalls == 0);
    assert(sleepCalls == 0);
}

static void testTorPrivacyEnableRecoveryDoesNotRetryWhileDegraded() {
    int reinitCalls = 0;
    int enableCalls = 0;
    int sleepCalls = 0;
    int refreshCalls = 0;

    const auto out = synapse::core::runTorPrivacyEnableRecovery(
        false,
        true,
        3,
        [&]() {
            ++refreshCalls;
            return synapse::core::TorPrivacyBootstrapProbe{true, true};
        },
        [&]() {
            ++reinitCalls;
            return true;
        },
        [&]() {
            ++enableCalls;
            return true;
        },
        [&](int ms) {
            assert(ms == 250);
            ++sleepCalls;
        });

    assert(!out.enabled);
    assert(out.retryAttemptsUsed == 0);
    assert(refreshCalls == 3);
    assert(reinitCalls == 0);
    assert(enableCalls == 0);
    assert(sleepCalls == 3);
}

static void testWebSearchFailureClassificationDistinguishesBootstrapAndTimeout() {
    assert(synapse::core::classifyWebSearchFailureKind("curl: (28) Connection timed out", true, false) ==
           "TOR_BOOTSTRAP_INCOMPLETE");
    assert(synapse::core::classifyWebSearchFailureKind("curl: (28) Connection timed out", true, true) ==
           "TIMEOUT");
    assert(synapse::core::classifyWebSearchFailureKind("curl: (97) socks failure", true, true) ==
           "ERROR");
    assert(synapse::core::classifyWebSearchFailureKind("operation timeout", false, true) ==
           "TIMEOUT");
}

static void testManagedTorRestartBackoffSecondsAreExponentialAndCapped() {
    assert(synapse::core::evaluateManagedTorRestartBackoffSeconds(0, 5, 60) == 0);
    assert(synapse::core::evaluateManagedTorRestartBackoffSeconds(1, 5, 60) == 5);
    assert(synapse::core::evaluateManagedTorRestartBackoffSeconds(2, 5, 60) == 10);
    assert(synapse::core::evaluateManagedTorRestartBackoffSeconds(3, 5, 60) == 20);
    assert(synapse::core::evaluateManagedTorRestartBackoffSeconds(4, 5, 60) == 40);
    assert(synapse::core::evaluateManagedTorRestartBackoffSeconds(5, 5, 60) == 60);
    assert(synapse::core::evaluateManagedTorRestartBackoffSeconds(10, 5, 60) == 60);
}

static void testManagedTorRestartGateHonorsBackoffAndMode() {
    synapse::core::ManagedTorRestartGateInput in;
    in.torRequired = true;
    in.allowStartManagedTor = true;
    in.allowManagedAutostart = true;
    in.torSocksReachable = false;
    in.now = 100;
    in.nextAllowedAttemptAt = 120;
    auto out = synapse::core::evaluateManagedTorRestartGate(in);
    assert(!out.shouldAttempt);
    assert(out.reason == "backoff");

    in.now = 120;
    out = synapse::core::evaluateManagedTorRestartGate(in);
    assert(out.shouldAttempt);
    assert(out.reason == "allow");

    in.torSocksReachable = true;
    out = synapse::core::evaluateManagedTorRestartGate(in);
    assert(!out.shouldAttempt);
    assert(out.reason == "already_reachable");

    in.torSocksReachable = false;
    in.allowManagedAutostart = false;
    out = synapse::core::evaluateManagedTorRestartGate(in);
    assert(!out.shouldAttempt);
    assert(out.reason == "autostart_disabled");
}

int main() {
    testDefaultPolicyIsTorRequiredFailClosed();
    testTorUnavailableFailClosedWithNoFallbacks();
    testTorUnavailableFallbackBehavior();
    testNonTorRequiredModeUsesClearnetProfile();
    testHealthyTorRequiredMode();
    testRoutePolicyDeterminismAcrossMatrix();
    testTorPrivacyEnableRecoveryRecoversAfterRouteHealthy();
    testTorPrivacyEnableRecoverySkipsWhenTorNotRequired();
    testTorPrivacyEnableRecoveryDoesNotRetryWhileDegraded();
    testWebSearchFailureClassificationDistinguishesBootstrapAndTimeout();
    testManagedTorRestartBackoffSecondsAreExponentialAndCapped();
    testManagedTorRestartGateHonorsBackoffAndMode();
    return 0;
}
