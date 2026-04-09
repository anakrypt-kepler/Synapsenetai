#include "web/web.h"

#include <cassert>

static void testFailureAndRecoveryTransitions() {
    synapse::web::ConnectorHealthMachine health;

    auto snap = health.snapshot();
    assert(snap.state == synapse::web::ConnectorHealthState::OFFLINE);

    health.recordSuccess();
    snap = health.snapshot();
    assert(snap.state == synapse::web::ConnectorHealthState::DEGRADED);

    health.recordSuccess();
    snap = health.snapshot();
    assert(snap.state == synapse::web::ConnectorHealthState::ONLINE);

    health.recordFailure();
    snap = health.snapshot();
    assert(snap.state == synapse::web::ConnectorHealthState::ONLINE);

    health.recordFailure();
    snap = health.snapshot();
    assert(snap.state == synapse::web::ConnectorHealthState::DEGRADED);

    health.recordFailure();
    health.recordFailure();
    snap = health.snapshot();
    assert(snap.state == synapse::web::ConnectorHealthState::OFFLINE);
}

static void testPolicyBlockedTransitions() {
    synapse::web::ConnectorHealthMachine health;
    health.recordSuccess();
    health.recordSuccess();

    auto before = health.snapshot();
    assert(before.state == synapse::web::ConnectorHealthState::ONLINE);

    health.recordPolicyBlock();
    auto blocked = health.snapshot();
    assert(blocked.state == synapse::web::ConnectorHealthState::POLICY_BLOCKED);
    assert(blocked.policyBlocks == before.policyBlocks + 1);

    health.recordSuccess();
    auto recovering = health.snapshot();
    assert(recovering.state == synapse::web::ConnectorHealthState::DEGRADED);

    health.recordSuccess();
    auto recovered = health.snapshot();
    assert(recovered.state == synapse::web::ConnectorHealthState::ONLINE);
}

static void testPolicySanitization() {
    synapse::web::ConnectorHealthPolicy policy;
    policy.degradedFailureStreak = 5;
    policy.offlineFailureStreak = 2;
    policy.onlineRecoverSuccessStreak = 0;

    synapse::web::ConnectorHealthMachine health;
    health.setPolicy(policy);
    auto effective = health.getPolicy();
    assert(effective.degradedFailureStreak == 5);
    assert(effective.offlineFailureStreak >= effective.degradedFailureStreak);
    assert(effective.onlineRecoverSuccessStreak >= 1);
}

static void testWebSearchHealthApi() {
    synapse::web::WebSearch webSearch;
    synapse::web::ConnectorHealthPolicy policy;
    policy.degradedFailureStreak = 3;
    policy.offlineFailureStreak = 6;
    policy.onlineRecoverSuccessStreak = 2;
    webSearch.setConnectorHealthPolicy(policy);

    auto effective = webSearch.getConnectorHealthPolicy();
    assert(effective.degradedFailureStreak == 3);
    assert(effective.offlineFailureStreak == 6);
    assert(effective.onlineRecoverSuccessStreak == 2);

    auto health = webSearch.getConnectorHealth();
    assert(health.clearnet.state == synapse::web::ConnectorHealthState::OFFLINE);
    assert(health.tor.state == synapse::web::ConnectorHealthState::OFFLINE);
    assert(health.onion.state == synapse::web::ConnectorHealthState::OFFLINE);
}

int main() {
    testFailureAndRecoveryTransitions();
    testPolicyBlockedTransitions();
    testPolicySanitization();
    testWebSearchHealthApi();
    return 0;
}
