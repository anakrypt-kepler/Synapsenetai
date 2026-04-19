#include "core/tor_route_policy.h"

#include <cassert>
#include <string>

static void testRuntimeModeNormalizationAndManagedAutostartPolicy() {
    assert(synapse::core::normalizeTorRuntimeMode("external") == "external");
    assert(synapse::core::normalizeTorRuntimeMode("external_only") == "external");
    assert(synapse::core::normalizeTorRuntimeMode("MANAGED_ONLY") == "managed");
    assert(synapse::core::normalizeTorRuntimeMode("weird") == "auto");

    assert(!synapse::core::isManagedTorAutostartAllowedForMode("external"));
    assert(!synapse::core::isManagedTorAutostartAllowedForMode("EXTERNAL_ONLY"));
    assert(synapse::core::isManagedTorAutostartAllowedForMode("auto"));
    assert(synapse::core::isManagedTorAutostartAllowedForMode("managed"));
}

static void testConflictHintForLikely9050vs9150Mismatch() {
    assert(synapse::core::evaluateTor9050vs9150ConflictHint("auto", 9050, false, true));
    assert(!synapse::core::evaluateTor9050vs9150ConflictHint("external", 9050, false, true));
    assert(!synapse::core::evaluateTor9050vs9150ConflictHint("auto", 9150, false, true));
    assert(!synapse::core::evaluateTor9050vs9150ConflictHint("auto", 9050, true, true));
    assert(!synapse::core::evaluateTor9050vs9150ConflictHint("auto", 9050, false, false));
}

static void testExternalTorBootstrapStateTransitions() {
    synapse::core::TorBootstrapStateInput in;
    in.torRequired = true;
    in.torDegraded = false;

    in.torSocksReachable = false;
    in.torWebReady = false;
    assert(synapse::core::evaluateTorBootstrapState(in) == "SOCKS_DOWN");
    assert(synapse::core::evaluateTorBootstrapPercent(in) == 0);
    assert(!synapse::core::evaluateTorReadyForOnion(in));
    assert(synapse::core::evaluateTorBootstrapReasonCode(in, "") == "SOCKS_UNREACHABLE");

    in.torSocksReachable = true;
    in.torWebReady = false;
    assert(synapse::core::evaluateTorBootstrapState(in) == "SOCKS_UP_BOOTSTRAP");
    assert(synapse::core::evaluateTorBootstrapPercent(in) == 70);
    assert(!synapse::core::evaluateTorReadyForOnion(in));
    assert(synapse::core::evaluateTorBootstrapReasonCode(in, "") == "SOCKS_UP_BUT_NO_CIRCUIT");

    in.torSocksReachable = true;
    in.torWebReady = true;
    assert(synapse::core::evaluateTorBootstrapState(in) == "WEB_READY");
    assert(synapse::core::evaluateTorBootstrapPercent(in) == 100);
    assert(synapse::core::evaluateTorReadyForOnion(in));
    assert(synapse::core::evaluateTorBootstrapReasonCode(in, "") == "READY");

    in.torDegraded = true;
    in.torSocksReachable = false;
    in.torWebReady = false;
    assert(synapse::core::evaluateTorBootstrapState(in) == "SOCKS_DOWN_DEGRADED");
    assert(synapse::core::evaluateTorBootstrapPercent(in) == 0);
    assert(!synapse::core::evaluateTorReadyForOnion(in));
    assert(synapse::core::evaluateTorBootstrapReasonCode(in, "") == "SOCKS_UNREACHABLE_DEGRADED");

    in.torSocksReachable = true;
    in.torWebReady = false;
    assert(synapse::core::evaluateTorBootstrapState(in) == "SOCKS_UP_DEGRADED");
    assert(synapse::core::evaluateTorBootstrapPercent(in) == 40);
    assert(!synapse::core::evaluateTorReadyForOnion(in));
    assert(synapse::core::evaluateTorBootstrapReasonCode(in, "curl_exit=28") == "WEB_PROBE_FAILED");

    in.torWebReady = true;
    assert(synapse::core::evaluateTorBootstrapState(in) == "WEB_READY_DEGRADED");
    assert(synapse::core::evaluateTorBootstrapPercent(in) == 100);
    assert(!synapse::core::evaluateTorReadyForOnion(in));
    assert(synapse::core::evaluateTorBootstrapReasonCode(in, "") == "READY_DEGRADED");
}

static void testBootstrapStateWhenTorNotRequired() {
    synapse::core::TorBootstrapStateInput in;
    in.torRequired = false;
    in.torSocksReachable = false;
    in.torWebReady = false;
    in.torDegraded = false;
    assert(synapse::core::evaluateTorBootstrapState(in) == "NOT_REQUIRED");
    assert(synapse::core::evaluateTorBootstrapPercent(in) == 100);
    assert(!synapse::core::evaluateTorReadyForOnion(in));
    assert(synapse::core::evaluateTorBootstrapReasonCode(in, "") == "NOT_REQUIRED");
}

static void testExternalPathDeterminism() {
    for (int i = 0; i < 256; ++i) {
        const bool conflict = synapse::core::evaluateTor9050vs9150ConflictHint("auto", 9050, false, true);
        assert(conflict);
        const std::string state = synapse::core::evaluateTorBootstrapState(
            {true, true, false, false});
        assert(state == "SOCKS_UP_BOOTSTRAP");
    }
}

static void testOnionServiceStateDistinguishesWebOnlyAndControlPath() {
    synapse::core::TorOnionServiceStateInput in;
    in.privacyModeEnabled = true;
    in.torRequired = true;
    in.torSocksReachable = true;
    in.torWebReady = true;
    in.torDegraded = false;
    in.torControlReachable = false;
    in.onionServiceActive = false;

    assert(synapse::core::evaluateTorOnionServiceState(in) == "CONTROL_UNREACHABLE_WEB_READY");
    assert(!synapse::core::evaluateTorReadyForOnionService(in));

    in.torControlReachable = true;
    assert(synapse::core::evaluateTorOnionServiceState(in) == "SERVICE_DOWN");
    assert(!synapse::core::evaluateTorReadyForOnionService(in));

    in.onionServiceActive = true;
    assert(synapse::core::evaluateTorOnionServiceState(in) == "ACTIVE");
    assert(synapse::core::evaluateTorReadyForOnionService(in));
}

static void testOnionServiceStateNotRequestedWhenPrivacyDisabled() {
    synapse::core::TorOnionServiceStateInput in;
    in.privacyModeEnabled = false;
    in.torRequired = true;
    in.torSocksReachable = true;
    in.torWebReady = true;
    in.torControlReachable = true;
    in.onionServiceActive = true;
    assert(synapse::core::evaluateTorOnionServiceState(in) == "NOT_REQUESTED");
    assert(!synapse::core::evaluateTorReadyForOnionService(in));
}

int main() {
    testRuntimeModeNormalizationAndManagedAutostartPolicy();
    testConflictHintForLikely9050vs9150Mismatch();
    testExternalTorBootstrapStateTransitions();
    testBootstrapStateWhenTorNotRequired();
    testExternalPathDeterminism();
    testOnionServiceStateDistinguishesWebOnlyAndControlPath();
    testOnionServiceStateNotRequestedWhenPrivacyDisabled();
    return 0;
}
