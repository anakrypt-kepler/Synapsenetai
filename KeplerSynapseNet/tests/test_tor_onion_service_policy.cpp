#include "core/tor_route_policy.h"

#include <cassert>

static void testDefaultOnionPolicy() {
    synapse::core::OnionServicePolicyInput input;
    input.networkPort = 0;

    const auto out = synapse::core::evaluateOnionServicePolicy(input);
    assert(out.virtualPort == 8333);
    assert(out.targetPort == 8333);
    assert(out.serviceDir == "./onion_service");
}

static void testOnionPolicyOverrides() {
    synapse::core::OnionServicePolicyInput input;
    input.networkPort = 4040;
    input.overrideVirtualPort = 9050;
    input.overrideTargetPort = 10050;
    input.dataDir = "/tmp/synapsenet";

    const auto out = synapse::core::evaluateOnionServicePolicy(input);
    assert(out.virtualPort == 9050);
    assert(out.targetPort == 10050);
    assert(out.serviceDir == "/tmp/synapsenet/onion_service");
}

static void testOnionPolicyBoundsFallback() {
    synapse::core::OnionServicePolicyInput input;
    input.networkPort = 4040;
    input.overrideVirtualPort = -1;
    input.overrideTargetPort = 70000;

    const auto out = synapse::core::evaluateOnionServicePolicy(input);
    assert(out.virtualPort == 4040);
    assert(out.targetPort == 4040);
}

static void testOnionServiceCanBeActiveWithoutTorRequired() {
    synapse::core::TorOnionServiceStateInput input;
    input.torRequired = false;
    input.privacyModeEnabled = true;
    input.torSocksReachable = true;
    input.torWebReady = true;
    input.torControlReachable = true;
    input.onionServiceActive = true;

    assert(synapse::core::evaluateTorOnionServiceState(input) == "ACTIVE");
    assert(synapse::core::evaluateTorReadyForOnionService(input));
}

int main() {
    testDefaultOnionPolicy();
    testOnionPolicyOverrides();
    testOnionPolicyBoundsFallback();
    testOnionServiceCanBeActiveWithoutTorRequired();
    return 0;
}
