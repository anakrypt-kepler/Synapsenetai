#include "web/web.h"

#include <cassert>

static synapse::web::WebConnectorHealth makeHealth(uint64_t policyBlocks, uint64_t failures) {
    synapse::web::WebConnectorHealth h;
    h.clearnet.policyBlocks = policyBlocks;
    h.clearnet.failures = failures;
    return h;
}

static void testNoTriggerOnFirstObservation() {
    synapse::web::ConnectorAbuseGuard guard;
    auto d = guard.observe(1, makeHealth(10, 20));
    assert(!d.triggered);
    assert(d.deltaPolicyBlocks == 0);
    assert(d.deltaFailures == 0);
}

static void testPolicyBlockThresholdTrigger() {
    synapse::web::ConnectorAbusePolicy policy;
    policy.policyBlockDeltaThreshold = 3;
    policy.failureDeltaThreshold = 50;
    policy.cooldownTicks = 5;
    policy.violationPenaltySteps = 2;

    synapse::web::ConnectorAbuseGuard guard(policy);
    (void)guard.observe(1, makeHealth(0, 0));

    auto d = guard.observe(10, makeHealth(3, 0));
    assert(d.triggered);
    assert(d.deltaPolicyBlocks == 3);
    assert(d.violations >= 2);
}

static void testFailureThresholdTrigger() {
    synapse::web::ConnectorAbusePolicy policy;
    policy.policyBlockDeltaThreshold = 100;
    policy.failureDeltaThreshold = 4;
    policy.cooldownTicks = 1;
    policy.violationPenaltySteps = 2;

    synapse::web::ConnectorAbuseGuard guard(policy);
    (void)guard.observe(1, makeHealth(0, 0));

    auto d = guard.observe(3, makeHealth(0, 4));
    assert(d.triggered);
    assert(d.deltaFailures == 4);
}

static void testCooldownPreventsImmediateRetrigger() {
    synapse::web::ConnectorAbusePolicy policy;
    policy.policyBlockDeltaThreshold = 2;
    policy.failureDeltaThreshold = 2;
    policy.cooldownTicks = 10;
    policy.violationPenaltySteps = 2;

    synapse::web::ConnectorAbuseGuard guard(policy);
    (void)guard.observe(1, makeHealth(0, 0));

    auto first = guard.observe(20, makeHealth(2, 0));
    assert(first.triggered);

    auto second = guard.observe(25, makeHealth(4, 0));
    assert(!second.triggered);

    auto third = guard.observe(31, makeHealth(6, 0));
    assert(third.triggered);
}

static void testCounterResetDoesNotUnderflow() {
    synapse::web::ConnectorAbusePolicy policy;
    policy.policyBlockDeltaThreshold = 1;
    policy.failureDeltaThreshold = 1;
    policy.cooldownTicks = 1;
    policy.violationPenaltySteps = 2;

    synapse::web::ConnectorAbuseGuard guard(policy);
    (void)guard.observe(1, makeHealth(10, 10));

    auto reset = guard.observe(2, makeHealth(1, 1));
    assert(!reset.triggered);
    assert(reset.deltaPolicyBlocks == 0);
    assert(reset.deltaFailures == 0);
}

int main() {
    testNoTriggerOnFirstObservation();
    testPolicyBlockThresholdTrigger();
    testFailureThresholdTrigger();
    testCooldownPreventsImmediateRetrigger();
    testCounterResetDoesNotUnderflow();
    return 0;
}
